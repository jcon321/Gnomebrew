#include "merchant_window.h"
#include "chat_text.h"
#include "eqstr.h"
#include "game_values.h"
#include "game_windows.h"
#include "inventory.h"
#include "window_manager.h"

#include <cmath>
#include <string>

namespace
{
	constexpr float MerchantRange = 20; // RightClickedOnPlayer, OnProcessFrame (across the ground)
	constexpr uint64_t CheckMs = 500;
	constexpr int MerchantSlots = 80;
	constexpr int MaxBuy = 20;          // HandleBuy: a stack's most
	constexpr uint8_t RentalItem = 39;  // DisplayBuyOrSellPrice's item type with a monthly rent
	// What the merchant says: eqgame prints it in 0x111 (User_18, the default text color); ours is
	// yellow until the chat colors are settable.
	constexpr int SaidColor = ChatType::Yellow;
	constexpr int SoldColor = 0x114, NotBuyingColor = 0x113;

	const ZoneItem *Find(const std::map<int16_t, ZoneItem> &items, int slot)
	{
		auto it = items.find(static_cast<int16_t>(slot));
		return it != items.end() ? &it->second : nullptr;
	}

	bool IsMerchantSlot(int slot)
	{
		return slot >= Inventory::FirstMerchant && slot <= Inventory::LastMerchant;
	}
}

void MerchantWindow::Attach(WindowManager &windows, ZoneSession &zone, GameWindowState &state, const GameValues &values,
	Host host)
{
	m_windows = &windows;
	m_zone = &zone;
	m_state = &state;
	m_values = &values;
	m_host = std::move(host);
	Reset();
}

void MerchantWindow::Reset()
{
	m_merchant = 0;
	m_active = false;
	m_selected = -1;
	m_busy = 0;
	m_opened_inventory = false;
	if (m_windows)
		m_windows->Show("MerchantWnd", false);
}

std::string MerchantWindow::Text(uint32_t id, const std::vector<std::string> &args, const std::string &fallback) const
{
	const EqStr *eqstr = m_values ? m_values->eqstr : nullptr;
	return eqstr && eqstr->Has(id) ? eqstr->Format(id, args) : fallback;
}

std::string MerchantWindow::MerchantName() const
{
	auto it = m_zone->Spawns().find(m_merchant);
	if (it != m_zone->Spawns().end())
		return it->second.DisplayName();
	return Text(1365, {}, "The merchant");
}

std::string MerchantWindow::Coins(int32_t copper, bool selling) const
{
	// EQ_Item::ValueSBuy and ValueSSell: " 1 platinum 2 gold 3 silver 4 copper", the coins there are.
	std::string out;
	const struct
	{
		int32_t amount;
		uint32_t id;
		const char *name;
	} coins[] = {
		{copper / 1000, 2409, " platinum"},
		{copper % 1000 / 100, 2410, " gold"},
		{copper % 100 / 10, 2411, " silver"},
		{copper % 10, 2412, " copper"},
	};
	for (const auto &coin : coins)
		if (coin.amount > 0)
			out += Text(coin.id, {std::to_string(coin.amount)}, " " + std::to_string(coin.amount) + coin.name);
	if (out.empty())
		out = selling ? Text(2408, {}, " absolutely nothing") : Text(2413, {}, " 0 money");
	return out;
}

void MerchantWindow::Request(uint16_t merchant)
{
	if (m_merchant != 0 || !m_zone)
		return;
	m_merchant = merchant;
	m_active = false;
	m_zone->ShopRequest(merchant);
}

void MerchantWindow::ZoneEvent(const ZoneSession::Event &event)
{
	using Type = ZoneSession::Event::Type;
	switch (event.type) {
	case Type::MerchantOpened:
		// msg_req_merchantitems: for the merchant we asked, the window up, or (0) not.
		if (m_active || event.merchant.npc != m_merchant)
			break;
		if (event.merchant.command == 0) {
			m_merchant = 0;
			break;
		}
		m_active = true;
		m_greed = event.merchant.rate;
		m_selected = -1;
		m_busy = 0;
		if (SidlWindow *wnd = m_windows->Find("MerchantWnd")) {
			if (SidlWindow::Control *name = wnd->Find("MW_MerchantName"))
				name->text = MerchantName();
			if (SidlWindow::Control *slots = wnd->Find("MerchantSlotsWnd")) {
				slots->scroll_pos = 0;
				wnd->Move(wnd->Bounds());
			}
		}
		m_windows->Show("MerchantWnd", true);
		m_windows->Raise("MerchantWnd");
		m_opened_inventory = !m_windows->Shown("InventoryWindow");
		if (m_opened_inventory)
			m_windows->Show("InventoryWindow", true);
		m_next_check_ms = 0;
		break;
	case Type::MerchantBought:
		if (m_active)
			FinishBuying(event.merchant);
		break;
	case Type::MerchantSold:
		if (m_active)
			FinishSelling(event.merchant);
		break;
	case Type::MerchantItemGone:
		if (m_selected == Inventory::FirstMerchant + event.merchant.slot)
			Select(-1);
		break;
	default:
		break;
	}
}

void MerchantWindow::Deactivate()
{
	if (!m_merchant)
		return;
	bool was_active = m_active;
	m_active = false;
	m_windows->Show("MerchantWnd", false);
	m_zone->ShopEnd(m_merchant); // its items go
	m_merchant = 0;
	m_busy = 0;
	Select(-1);
	if (!was_active)
		return;
	if (m_host.clear_target)
		m_host.clear_target();
	if (m_opened_inventory)
		m_windows->Show("InventoryWindow", false);
	m_opened_inventory = false;
}

const ZoneItem *MerchantWindow::Selected() const
{
	if (m_selected < 0)
		return nullptr;
	// SelectBuySellSlot: ours only from the worn and general slots and their bags.
	if (!IsMerchantSlot(m_selected) && m_selected > 330)
		return nullptr;
	return Find(m_zone->Items(), m_selected);
}

void MerchantWindow::Highlight(const std::string &window, const std::string &control)
{
	// CInvSlotMgr_SelectSlot: the old slot's outline off, the new one's on.
	auto mark = [this](bool on) {
		SidlWindow *wnd = m_highlight_window.empty() ? nullptr : m_windows->Find(m_highlight_window);
		if (SidlWindow::Control *c = wnd ? wnd->Find(m_highlight_control) : nullptr)
			c->checked = on;
	};
	mark(false);
	m_highlight_window = window;
	m_highlight_control = control;
	mark(true);
}

void MerchantWindow::Select(int slot)
{
	m_selected = slot;
	if (slot < 0)
		Highlight("", "");
	if (SidlWindow *wnd = m_windows->Find("MerchantWnd"))
		if (SidlWindow::Control *icon = wnd->Find("MW_SelectedItem")) {
			// The item as MW_SelectedItem's decal (A_DragItem's cell) over its A_RecessedBox.
			const ZoneItem *item = slot >= 0 ? Find(m_zone->Items(), slot) : nullptr;
			icon->cell = item && item->icon >= 500 ? item->icon - 500 : -1;
		}
	if (slot < 0)
		return;
	const ZoneItem *item = Selected();
	if (!item)
		return;
	bool buying = IsMerchantSlot(slot);
	// A bag's contents are priced first.
	if (!buying && item->item_class == 1)
		for (int n = 0; n < item->bag_slots; ++n)
			if (const ZoneItem *inside = Find(m_zone->Items(), Inventory::BagSlot(static_cast<int16_t>(slot), n)))
				ShowPrice(false, *inside);
	ShowPrice(buying, *item);
}

void MerchantWindow::ShowPrice(bool buying, const ZoneItem &item)
{
	SidlWindow *wnd = m_windows->Find("MerchantWnd");
	SidlWindow::Control *buy = wnd ? wnd->Find("MW_Buy_Button") : nullptr;
	SidlWindow::Control *sell = wnd ? wnd->Find("MW_Sell_Button") : nullptr;
	if (sell)
		sell->enabled = true;
	const ZoneProfile &me = m_zone->Profile();
	std::string price;
	uint32_t per, whole;
	std::string per_text, whole_text;
	if (buying) {
		price = item.price < 1 ? Coins(0, false) : Coins(Inventory::ValueBuy(item, m_greed, 1), false);
		per = 4020, whole = 4019;
		per_text = "'That'll be%2 per %3.'", whole_text = "'That'll be%2 for the %3.'";
		if (buy)
			buy->enabled = Inventory::TotalOnPerson(me) >= Inventory::ValueBuy(item, m_greed, 1);
	}
	else {
		price = item.price < 1 ? Coins(0, true) : Coins(Inventory::ValueSell(item, m_greed, 1), true);
		per = 12026, whole = 12027;
		per_text = "'I'll give you%2 per %3'", whole_text = "'I'll give you%2 for the %3.'";
		if (sell)
			sell->enabled = item.no_rent != 0 &&
			                Inventory::CanDrop(m_zone->Items(), item, item.slot, m_values && m_values->fv_rules) &&
			                Inventory::ValueSell(item, m_greed, 1) > 0;
	}
	std::string name = MerchantName();
	std::string said;
	auto fill = [&](std::string text) {
		for (const auto &[from, to] : {std::pair<std::string, std::string>{"%2", price}, {"%3", item.name}})
			if (size_t at = text.find(from); at != std::string::npos)
				text.replace(at, from.size(), to);
		return name + " tells you, " + text;
	};
	if (item.item_class == 0 && buying && item.item_type == RentalItem) {
		ZoneItem monthly = item;
		monthly.price = item.price / 12;
		std::string rent = monthly.price < 1 ? Coins(0, false) : Coins(Inventory::ValueBuy(monthly, 1, 1), false);
		said = Text(4021, {name, price, item.name, rent}, fill("'That'll be%2 for the %3, and the rent will be") + rent +
			" per month.'");
	}
	else if (item.item_class == 0 && Inventory::Stacks(item))
		said = Text(per, {name, price, item.name}, fill(per_text));
	else
		said = Text(whole, {name, price, item.name}, fill(whole_text));
	if (m_host.chat)
		m_host.chat(said, SaidColor);
}

bool MerchantWindow::SlotClick(const std::string &window, const std::string &control)
{
	if (!m_active)
		return false;
	int slot = InvSlotAt(*m_windows, window, control);
	if (slot < 0)
		return false;
	if (m_busy == 0) {
		Select(slot);
		Highlight(window, control);
	}
	return true;
}

bool MerchantWindow::Click(const std::string &window, const std::string &control, bool shift, bool ctrl)
{
	if (window != "MerchantWnd" || !m_active)
		return false;
	// The quantity window's Shift (all) and Ctrl (one), as the button was clicked.
	m_shift = shift;
	m_ctrl = ctrl;
	if (control == "DoneButton")
		Deactivate();
	else if (control == "MW_Buy_Button")
		HandleBuy(-1);
	else if (control == "MW_Sell_Button")
		HandleSell(-1);
	else
		return false;
	return true;
}

void MerchantWindow::HandleBuy(int count)
{
	const ZoneItem *item = Selected();
	if (!item || !IsMerchantSlot(m_selected) || !m_merchant)
		return;
	const std::map<int16_t, ZoneItem> &items = m_zone->Items();
	std::string name = MerchantName();
	if (Inventory::HasLoreItem(items, *item)) {
		if (m_host.chat)
			m_host.chat(Text(1573, {name}, name + " tells you, 'You already have that Lore Item on your person or in the "
				"bank.  You cannot have more than one of a particular Lore Item at a time.'"), SaidColor);
		return;
	}
	// Room: an empty general slot, or an empty slot in a bag there, or the same stacking item.
	auto same = [&](const ZoneItem &there) {
		return Inventory::Stacks(there) && Inventory::Stacks(*item) && there.id == item->id;
	};
	bool room = false;
	for (int16_t slot = Inventory::FirstGeneral; slot <= Inventory::LastGeneral && !room; ++slot) {
		const ZoneItem *there = Find(items, slot);
		if (!there)
			room = true;
		else if (there->item_class == 1) {
			for (int n = 0; n < there->bag_slots && !room; ++n) {
				const ZoneItem *inside = Find(items, Inventory::BagSlot(slot, n));
				room = !inside || same(*inside);
			}
		}
		else
			room = same(*there);
	}
	if (!room) {
		if (m_host.chat)
			m_host.chat(Text(1571, {name}, name + " tells you, 'Your inventory appears full!  How can you buy more?'"),
				SaidColor);
		return;
	}
	if (count < 0) {
		SidlWindow *wnd = m_windows->Find("MerchantWnd");
		const SidlWindow::Control *button = wnd ? wnd->Find("MW_Buy_Button") : nullptr;
		int most = Inventory::Stacks(*item) ? MaxBuy : 1;
		AskQuantity(*m_windows, *m_state, most, button ? button->rect.x : 0, button ? button->rect.y : 0,
			m_shift, m_ctrl, [this](int value) { HandleBuy(value); });
		return;
	}
	// RequestBuyItem.
	if (m_busy != 0 || count <= 0)
		return;
	int32_t cost = Inventory::ValueBuy(*item, m_greed, count);
	if (Inventory::TotalOnPerson(m_zone->Profile()) < cost) {
		std::string said = count == 1
			? Text(12060, {name, item->name}, name + " tells you, 'You cannot afford the " + item->name + ".'")
			: Text(12061, {name, Coins(cost, false), std::to_string(count), item->name},
				  name + " tells you, 'I charge " + Coins(cost, false) + " for " + std::to_string(count) + " " + item->name +
					  ", but you can't afford to buy them.'");
		if (m_host.chat)
			m_host.chat(said, SaidColor);
		return;
	}
	if (m_zone->ShopBuy(m_merchant, static_cast<int16_t>(m_selected - Inventory::FirstMerchant), count))
		++m_busy;
}

void MerchantWindow::HandleSell(int count)
{
	const ZoneItem *item = Selected();
	if (!item || IsMerchantSlot(m_selected) || !m_merchant)
		return;
	if (count < 0) {
		SidlWindow *wnd = m_windows->Find("MerchantWnd");
		const SidlWindow::Control *button = wnd ? wnd->Find("MW_Buy_Button") : nullptr;
		int most = Inventory::Stacks(*item) ? Inventory::Count(*item) : 1;
		AskQuantity(*m_windows, *m_state, most, button ? button->rect.x : 0, button ? button->rect.y : 0,
			m_shift, m_ctrl, [this](int value) { HandleSell(value); });
		return;
	}
	// RequestSellItem.
	if (count <= 0 || m_selected < 0 || m_selected > 330)
		return;
	std::string name = MerchantName();
	auto say = [&](uint32_t id, const std::string &text) {
		if (m_host.chat)
			m_host.chat(Text(id, {name}, name + " tells you," + text), SaidColor);
	};
	if (m_busy != 0) {
		say(12063, "'Whoa!  Hold your horses there!  One thing at a time.'");
		return;
	}
	const std::map<int16_t, ZoneItem> &items = m_zone->Items();
	if (!Inventory::CanDrop(items, *item, static_cast<int16_t>(m_selected), m_values && m_values->fv_rules)) {
		// CanDrop's own say (mode 1), then the merchant's.
		bool bag = item->no_drop != 0;
		if (m_host.chat)
			m_host.chat(bag ? Text(12922, {}, "That container cannot be dropped, traded, or sold, as it contains an item "
			                                 "which cannot be dropped, traded or sold.")
			                : Text(12921, {}, "That item cannot be dropped, traded, or sold."),
				ChatType::Yellow);
		say(12062, "'I am not interested in buying that.'");
		return;
	}
	if (item->item_class == 1)
		for (int n = 0; n < item->bag_slots; ++n)
			if (Find(items, Inventory::BagSlot(static_cast<int16_t>(m_selected), n))) {
				say(12064, "'Sorry, but you're going to need to empty that before I'll buy it.'");
				return;
			}
	if (m_zone->ShopSell(m_merchant, static_cast<int16_t>(m_selected), count))
		++m_busy;
}

void MerchantWindow::FinishBuying(const ZoneMerchant &answer)
{
	m_busy = std::max(m_busy - 1, 0);
	int slot = answer.slot;
	if (slot >= MerchantSlots)
		return;
	if (slot < 0) {
		if (slot == -1 && m_host.chat)
			m_host.chat(Text(1378, {}, "Could not purchase.  You may only possess one of those items at a time."),
				ChatType::Red);
		return;
	}
	const ZoneItem *item = Find(m_zone->Items(), Inventory::FirstMerchant + slot);
	if (!item) {
		if (m_host.chat)
			m_host.chat(Text(1376, {}, "The item you were interested in has already been sold."), ChatType::Default);
		return;
	}
	ZoneProfile &me = m_zone->EditProfile();
	if (!Inventory::HandleMoney(me, answer.price))
		me.platinum = me.gold = me.silver = me.copper = 0;
	else if (m_host.sound)
		m_host.sound(0x8a);
	std::string price = item->price < 1 ? Coins(0, false) : Coins(Inventory::ValueBuy(*item, m_greed, answer.quantity), false);
	std::string name = MerchantName();
	if (m_host.chat)
		m_host.chat(Text(1437, {price, name}, "You give" + price + " to " + name + "."), ChatType::Default);
}

void MerchantWindow::FinishSelling(const ZoneMerchant &answer)
{
	std::string name = MerchantName();
	int16_t slot = answer.slot;
	int count = answer.quantity;
	std::map<int16_t, ZoneItem> &items = m_zone->EditItems();
	auto sold = [&](const ZoneItem &item) {
		int32_t worth = Inventory::ValueSell(item, m_greed, count);
		std::string price = item.price < 1 ? Coins(0, true) : Coins(worth, true);
		Inventory::AddCoins(m_zone->EditProfile(), worth);
		if (m_host.chat)
			m_host.chat(Text(1361, {price, name, item.name}, "You receive" + price + " from " + name + " for the " + item.name +
				"(s)."), SoldColor);
	};
	if (slot < 0) {
		if (m_host.chat)
			m_host.chat(Text(1364, {name}, name + " tells you, 'I'm not interested in buying that at this time.'"),
				NotBuyingColor);
	}
	else if (auto it = items.find(slot); it != items.end() && (slot < 30 || Inventory::BagOf(slot) >= 0)) {
		ZoneItem item = it->second;
		sold(item);
		if (m_host.sound)
			m_host.sound(0x8a);
		// A bag (empty by now) goes with what was in it.
		if (item.item_class == 1) {
			if (m_host.close_bag)
				m_host.close_bag(slot);
			for (int n = 0; n < item.bag_slots; ++n)
				if (const ZoneItem *inside = Find(items, Inventory::BagSlot(slot, n)))
					sold(*inside);
		}
		// The zone doesn't say: the item, or that many of a stack, goes.
		if (!Inventory::Stacks(item) || count >= Inventory::Count(item))
			Inventory::Remove(items, slot);
		else
			it->second.charges = static_cast<int8_t>(static_cast<uint8_t>(it->second.charges) - count * item.max_charges);
		if (slot < 30)
			m_zone->UpdateAppearance();
		if (m_selected == slot)
			Select(-1);
	}
	else if (m_host.chat)
		m_host.chat("Error: Tried to sell possessions that you don't have.", ChatType::Red);
	m_busy = std::max(m_busy - 1, 0);
}

void MerchantWindow::Frame(uint64_t now_ms)
{
	if (!m_active)
		return;
	SidlWindow *wnd = m_windows->Find("MerchantWnd");
	// Done, the close box or Escape (CXWnd::Show 0 through its Deactivate).
	if (!wnd || !m_windows->Shown("MerchantWnd")) {
		Deactivate();
		return;
	}
	if (now_ms >= m_next_check_ms) {
		m_next_check_ms = now_ms + CheckMs;
		auto it = m_zone->Spawns().find(m_merchant);
		const ZoneProfile &me = m_zone->Profile();
		float distance = it == m_zone->Spawns().end() ? 0 : std::hypot(it->second.x - me.x, it->second.y - me.y);
		if (distance > MerchantRange) {
			Deactivate();
			return;
		}
		if (m_selected < 0 && m_zone->Items().count(Inventory::FirstMerchant)) {
			Select(Inventory::FirstMerchant);
			Highlight("MerchantWnd", "MW_MerchantSlot0");
		}
	}
	const std::map<int16_t, ZoneItem> &items = m_zone->Items();
	for (int n = 0; n < MerchantSlots; ++n)
		if (SidlWindow::Control *c = wnd->Find("MW_MerchantSlot" + std::to_string(n)))
			FillInvSlot(*c, Find(items, c->ItemSlot()), *m_values);
	// UpdateMerchantWindowVisibility: Buy for nothing or a merchant's slot, Sell for ours.
	bool buying = m_selected < 0 || IsMerchantSlot(m_selected);
	if (SidlWindow::Control *buy = wnd->Find("MW_Buy_Button"))
		buy->visible = buying;
	if (SidlWindow::Control *sell = wnd->Find("MW_Sell_Button"))
		sell->visible = !buying;
}

void MerchantWindow::DrawTooltips(Canvas &canvas, TextRenderer &text, int mouse_x, int mouse_y)
{
	if (!m_active || m_windows->ScreenAt(mouse_x, mouse_y) != "MerchantWnd")
		return;
	const SidlWindow *wnd = m_windows->Find("MerchantWnd");
	const SidlWindow::Control *area = wnd ? wnd->Find("MerchantSlotsWnd") : nullptr;
	if (!wnd || wnd->Minimized())
		return;
	int first_x = -1;
	for (int n = 0; n < MerchantSlots; ++n) {
		const SidlWindow::Control *c = wnd->Find("MW_MerchantSlot" + std::to_string(n));
		if (!c || !wnd->Shown(*c) || c->tooltip.empty() || c->cell < 0)
			continue;
		if (first_x < 0)
			first_x = c->rect.x;
		int y = c->rect.y + (c->rect.x == first_x ? 2 : 22);
		// Only those in view: below the slots' top, 15 above their bottom.
		if (area && (y > area->rect.Bottom() - 15 || y < area->rect.y))
			continue;
		m_windows->DrawTooltip(canvas, text, c->tooltip, c->def->font, c->rect.Right() - 5, y, false);
	}
}
