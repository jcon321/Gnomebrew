#include "trade_window.h"
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
	constexpr uint64_t CheckMs = 500;
	constexpr int PlayerSlots = 8, NpcSlots = 4;
	constexpr int SaidColor = ChatType::Default; // 0x111
	constexpr int FullColor = 10;
	constexpr uint8_t Busy = 1;                   // msg_req_trade's refusal
	constexpr uint32_t Red = 0xff0000, Green = 0x00ff00; // UpdateTradeDisplay's name colors
}

void TradeWindow::Attach(WindowManager &windows, ZoneSession &zone, GameWindowState &state, const GameValues &values,
	Host host)
{
	m_windows = &windows;
	m_zone = &zone;
	m_state = &state;
	m_values = &values;
	m_host = std::move(host);
	Reset();
}

void TradeWindow::Reset()
{
	m_active = m_give = false;
	m_target = 0;
	m_my_ready = m_his_ready = false;
	for (int32_t &coins : m_his_coins)
		coins = 0;
	m_opened_inventory = false;
	if (m_state)
		m_state->trading = m_state->trade_npc = false;
	if (m_windows) {
		m_windows->Show("TradeWnd", false);
		m_windows->Show("GiveWnd", false);
	}
}

std::string TradeWindow::Text(uint32_t id, const std::vector<std::string> &args, const std::string &fallback) const
{
	const EqStr *eqstr = m_values ? m_values->eqstr : nullptr;
	return eqstr && eqstr->Has(id) ? eqstr->Format(id, args) : fallback;
}

std::string TradeWindow::Name(uint16_t spawn) const
{
	auto it = m_zone->Spawns().find(spawn);
	return it != m_zone->Spawns().end() ? it->second.DisplayName() : "";
}

void TradeWindow::DropOnPlayer(uint16_t spawn)
{
	auto it = m_zone->Spawns().find(spawn);
	if (it == m_zone->Spawns().end() || it->second.npc > 1)
		return;
	const ZoneSpawn &who = it->second;
	if (m_active) {
		if (spawn != m_target)
			return;
		if (m_zone->Items().count(Inventory::Cursor))
			DropItem();
		else
			DropMoney();
		return;
	}
	if (spawn == m_zone->MySpawnId() || (m_host.ok_to_transact && !m_host.ok_to_transact()))
		return;
	float height = m_host.height ? m_host.height(spawn) : 0;
	if (m_zone->SpawnDistance(spawn) > height + 20) {
		std::string name = Name(spawn);
		if (m_host.chat)
			m_host.chat(Text(12294, {name}, "You are too far away from " + name + " to trade."), ChatType::Default);
		return;
	}
	const std::map<int16_t, ZoneItem> &items = m_zone->Items();
	auto held = items.find(Inventory::Cursor);
	if (who.npc == 0 && held != items.end() &&
	    !Inventory::CanDrop(items, held->second, Inventory::Cursor, m_values && m_values->fv_rules)) {
		bool bag = held->second.no_drop != 0;
		if (m_host.chat)
			m_host.chat(bag ? Text(12922, {}, "That container cannot be dropped, traded, or sold, as it contains an item "
			                                 "which cannot be dropped, traded or sold.")
			                : Text(12921, {}, "That item cannot be dropped, traded, or sold."),
				ChatType::Yellow);
		return;
	}
	m_zone->TradeRequest(spawn);
}

void TradeWindow::Activate(uint16_t target, bool ours)
{
	m_zone->ClearTrade(); // CEverQuest::MoveMoney: nothing in it yet
	for (int32_t &coins : m_his_coins)
		coins = 0;
	m_my_ready = m_his_ready = false;
	m_target = target;
	if (m_host.set_target)
		m_host.set_target(target);
	auto it = m_zone->Spawns().find(target);
	bool npc = it != m_zone->Spawns().end() && it->second.npc == 1;
	m_active = true;
	m_give = npc && ours;
	m_state->trading = true;
	m_state->trade_npc = npc;
	if (!ours)
		m_zone->TradeRequestAck(target);
	else if (m_zone->Items().count(Inventory::Cursor))
		DropItem();
	else
		DropMoney();
	m_windows->Show(Screen(), true);
	m_windows->Raise(Screen());
	m_opened_inventory = !m_windows->Shown("InventoryWindow");
	if (m_opened_inventory)
		m_windows->Show("InventoryWindow", true);
	m_next_check_ms = 0;
}

void TradeWindow::Close(bool cancel)
{
	if (!m_active)
		return;
	m_active = false;
	m_windows->Show(Screen(), false);
	if (cancel && m_target != 0) {
		m_zone->CancelTrade(m_target);
		if (m_host.chat)
			m_host.chat(Text(1450, {}, "You have cancelled the trade."), SaidColor);
	}
	m_zone->ClearTrade(); // the zone hands back what we put in
	m_target = 0;
	m_give = false;
	m_state->trading = m_state->trade_npc = false;
	if (m_opened_inventory)
		m_windows->Show("InventoryWindow", false);
	m_opened_inventory = false;
}

void TradeWindow::Deactivate()
{
	Close(true);
}

void TradeWindow::DropItem()
{
	auto it = m_zone->Spawns().find(m_target);
	if (it == m_zone->Spawns().end())
		return;
	bool npc = it->second.npc == 1;
	const std::map<int16_t, ZoneItem> &items = m_zone->Items();
	auto held = [&]() { auto h = items.find(Inventory::Cursor); return h != items.end() ? &h->second : nullptr; };
	if (!held())
		return;
	bool fv = m_values && m_values->fv_rules;
	if (!npc && !Inventory::CanDrop(items, *held(), Inventory::Cursor, fv)) {
		if (m_host.chat)
			m_host.chat(Text(12921, {}, "That item cannot be dropped, traded, or sold."), ChatType::Yellow);
		return;
	}
	int slots = npc ? NpcSlots : PlayerSlots;
	bool placed = false;
	uint32_t message = 0;
	// Onto the same stack (SwapCombine), then into the first empty slot.
	if (Inventory::Stacks(*held()))
		for (int n = 0; n < slots && held(); ++n) {
			auto there = items.find(static_cast<int16_t>(Inventory::FirstTrade + n));
			if (there != items.end() && there->second.id == held()->id &&
			    Inventory::Move(*m_zone, Inventory::Cursor, static_cast<int16_t>(Inventory::FirstTrade + n), message))
				placed = true;
		}
	for (int n = 0; n < slots && held(); ++n)
		if (!items.count(static_cast<int16_t>(Inventory::FirstTrade + n)) &&
		    Inventory::Move(*m_zone, Inventory::Cursor, static_cast<int16_t>(Inventory::FirstTrade + n), message))
			placed = true;
	if (!placed) {
		if (m_host.chat)
			m_host.chat(Text(12319, {}, "All your trade slots are full!"), FullColor);
		return;
	}
	Changed(false);
}

void TradeWindow::DropMoney()
{
	const ZoneProfile &me = m_zone->Profile();
	int coin = Inventory::CursorMoneyType(me) - 1;
	if (coin < 0)
		return;
	Inventory::MoveMoney(*m_zone, Inventory::CursorCoins, Inventory::TradeCoins, coin, coin,
		Inventory::Coins(me, Inventory::CursorCoins, coin), false);
	Changed(false);
}

void TradeWindow::ZoneEvent(const ZoneSession::Event &event)
{
	using Type = ZoneSession::Event::Type;
	switch (event.type) {
	case Type::TradeRequested: {
		// msg_req_trade: turned down by the options' "Allow trading with" ('b', 'c'), else busy (1)
		// or the trade window up.
		std::string name = Name(event.id);
		if (name.empty())
			break;
		uint8_t reason = m_host.refusal ? m_host.refusal(name) : 0;
		if (reason == 0 && !m_active && (!m_host.ok_to_transact || m_host.ok_to_transact())) {
			Activate(event.id, false);
			break;
		}
		m_zone->TradeRefuse(event.id, reason ? reason : Busy);
		if (m_host.chat)
			m_host.chat(Text(1447, {name}, name + " is interested in making a trade."), SaidColor);
		break;
	}
	case Type::TradeBegan: {
		std::string name = Name(event.id);
		if (name.empty() || m_active)
			break;
		if (!m_host.ok_to_transact || m_host.ok_to_transact()) {
			Activate(event.id, true);
			break;
		}
		// Busy: a tell back, the trade cancelled.
		const std::string &me = m_zone->Profile().name;
		m_zone->SendChannel(7, Name(event.id), Text(1446, {me}, me + " tells you, 'I'm busy right now.'"));
		m_zone->CancelTrade(event.id);
		if (m_host.chat)
			m_host.chat(Text(1447, {name}, name + " is interested in making a trade."), SaidColor);
		break;
	}
	case Type::TradeRefused: {
		std::string name = Name(event.id);
		if (name.empty())
			break;
		std::string said = event.value == 'b' ? Text(1444, {name}, "'I'm not interested in trading with anyone at all.'")
		                   : event.value == 'c'
		                       ? Text(1445, {name}, "I'm only interested in trading with members of my group.'")
		                       : Text(1446, {name}, name + " tells you, 'I'm busy right now.'");
		if (m_host.chat)
			m_host.chat(said, SaidColor);
		break;
	}
	case Type::TradeCancelled: {
		if (!m_active)
			break;
		std::string said;
		if (event.id == m_zone->MySpawnId())
			said = Text(1450, {}, "You have cancelled the trade.");
		else if (std::string name = Name(event.id); !name.empty())
			said = Text(1448, {name}, name + " has cancelled the trade.");
		else
			said = Text(1449, {}, "The trade has been cancelled.");
		if (m_host.chat)
			m_host.chat(said, SaidColor);
		// Deactivate: with the trade target still set, it cancels again (a second OP_CancelTrade)
		// and says "You have cancelled the trade." after the zone's line.
		Deactivate();
		break;
	}
	case Type::TradeReady:
		if (m_active)
			Changed(event.value != 0);
		break;
	case Type::TradeFinished:
		if (m_active) {
			if (m_host.save)
				m_host.save(); // CTradeWnd::CompleteTrade's SavePC
			Close(false);
		}
		break;
	case Type::TradeCoins:
		// SetHisMoney: 0 copper .. 3 platinum, kept platinum first.
		if (m_active && event.id <= 3) {
			m_his_coins[3 - event.id] += event.value;
			Changed(false);
		}
		break;
	case Type::TradeItem:
		if (m_active)
			Changed(false);
		break;
	case Type::SpawnRemoved:
		if (m_active && event.id == m_target)
			Close(true);
		break;
	default:
		break;
	}
}

bool TradeWindow::Click(const std::string &window, const std::string &control)
{
	if ((window != "TradeWnd" && window != "GiveWnd") || !m_active)
		return false;
	if (control == "TRDW_Trade_Button" || control == "GVW_Give_Button") {
		// ClickedTradeButton.
		if (m_target != 0 && !m_my_ready) {
			m_my_ready = true;
			m_zone->TradeAccept(m_target);
		}
		return true;
	}
	if (control == "TRDW_Cancel_Button" || control == "GVW_Cancel_Button") {
		// ClickedCancelButton: the zone's answer closes it.
		if (m_target != 0)
			m_zone->CancelTrade(m_target);
		else
			Deactivate();
		return true;
	}
	// Our money buttons: HandleCurrencyTrade, the coins on the cursor onto that pile.
	for (const char *prefix : {"TRDW_MyMoney", "GVW_MyMoney"})
		if (control.size() == std::string(prefix).size() + 1 && control.rfind(prefix, 0) == 0) {
			int coin = control.back() - '0';
			const ZoneProfile &me = m_zone->Profile();
			int held = Inventory::CursorMoneyType(me);
			if (held && coin >= 0 && coin <= 3 &&
			    Inventory::MoveMoney(*m_zone, Inventory::CursorCoins, Inventory::TradeCoins, held - 1, coin,
				    Inventory::Coins(me, Inventory::CursorCoins, held - 1), false))
				Changed(false);
			return true;
		}
	return false;
}

void TradeWindow::Frame(uint64_t now_ms)
{
	if (!m_active)
		return;
	SidlWindow *wnd = m_windows->Find(Screen());
	// The close box (HideTradeWindow for the give window): as Escape, a cancel.
	if (!wnd || !m_windows->Shown(Screen())) {
		Deactivate();
		return;
	}
	if (now_ms >= m_next_check_ms) {
		m_next_check_ms = now_ms + CheckMs;
		float height = m_host.height ? m_host.height(m_target) : 0;
		if (m_zone->SpawnDistance(m_target) > height + 20) {
			Deactivate();
			return;
		}
	}
	// UpdateTradeDisplay and UpdateGiveWindowDisplay.
	const ZoneProfile &me = m_zone->Profile();
	const std::map<int16_t, ZoneItem> &items = m_zone->Items();
	auto text = [&](const std::string &id, const std::string &value, uint32_t color = 0xffffffff) {
		if (SidlWindow::Control *c = wnd->Find(id)) {
			c->text = value;
			if (color != 0xffffffff)
				c->text_color = color;
		}
	};
	auto slot = [&](const std::string &id) {
		if (SidlWindow::Control *c = wnd->Find(id)) {
			auto it = items.find(static_cast<int16_t>(c->ItemSlot()));
			FillInvSlot(*c, it != items.end() ? &it->second : nullptr, *m_values);
		}
	};
	if (m_give) {
		text("GVW_NPCName", Name(m_target));
		for (int coin = 0; coin < 4; ++coin)
			text("GVW_MyMoney" + std::to_string(coin), std::to_string(me.trade_coins[coin]));
		for (int n = 0; n < NpcSlots; ++n)
			slot("GVW_MyItemSlot" + std::to_string(n));
		return;
	}
	text("TRDW_HisName", Name(m_target), m_his_ready ? Green : Red);
	text("TRDW_MyName", me.name, m_my_ready ? Green : Red);
	for (int coin = 0; coin < 4; ++coin) {
		text("TRDW_HisMoney" + std::to_string(coin), std::to_string(m_his_coins[coin]));
		text("TRDW_MyMoney" + std::to_string(coin), std::to_string(me.trade_coins[coin]));
	}
	for (int n = 0; n < 2 * PlayerSlots; ++n)
		slot("TRDW_TradeSlot" + std::to_string(n));
}

void TradeWindow::DrawTooltips(Canvas &canvas, TextRenderer &text, int mouse_x, int mouse_y)
{
	// CGiveWnd::DrawWindowTooltips, and the trade window's as the bank's.
	if (!m_active || m_windows->ScreenAt(mouse_x, mouse_y) != Screen())
		return;
	const SidlWindow *wnd = m_windows->Find(Screen());
	if (!wnd || wnd->Minimized())
		return;
	int count = m_give ? NpcSlots : 2 * PlayerSlots;
	int first_x = -1;
	for (int n = 0; n < count; ++n) {
		const SidlWindow::Control *c = wnd->Find((m_give ? "GVW_MyItemSlot" : "TRDW_TradeSlot") + std::to_string(n));
		if (!c || !wnd->Shown(*c) || c->tooltip.empty() || c->cell < 0)
			continue;
		if (first_x < 0)
			first_x = c->rect.x;
		int y = c->rect.y + (c->rect.x == first_x ? 2 : 22);
		m_windows->DrawTooltip(canvas, text, c->tooltip, c->def->font, c->rect.Right() - 5, y, false);
	}
}
