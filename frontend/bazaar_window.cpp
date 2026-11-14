#include "bazaar_window.h"

#include "game_values.h"
#include "game_windows.h"
#include "ini_file.h"
#include "inventory.h"
#include "window_manager.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>

namespace
{
	constexpr const char *Section = "ItemToSell";

	// CBazaarWnd_GetPriceString (@00406e6e): " 1p 2g 3s 4c", the coins that aren't 0; "0" for none.
	std::string PriceString(uint32_t copper)
	{
		std::string out;
		auto add = [&](uint32_t n, const char *coin) {
			if (n != 0)
				out += " " + std::to_string(n) + coin;
		};
		add(copper / 1000, "p");
		add(copper % 1000 / 100, "g");
		add(copper % 100 / 10, "s");
		add(copper % 10, "c");
		return out.empty() ? "0" : out;
	}

	std::string Format(const char *format, const std::string &a, const std::string &b = "")
	{
		std::string out = format;
		if (size_t at = out.find("%s"); at != std::string::npos)
			out.replace(at, 2, a);
		if (size_t at = out.find("%s"); at != std::string::npos)
			out.replace(at, 2, b);
		return out;
	}

	std::string SlotName(int i) { return "BZR_BazaarSlot" + std::to_string(i); }
	const char *const MoneyButtons[4] = {"BZW_Money0", "BZW_Money1", "BZW_Money2", "BZW_Money3"};

	template <typename T> T Get(const std::vector<uint8_t> &data, size_t at)
	{
		T value{};
		if (at + sizeof(T) <= data.size())
			std::memcpy(&value, data.data() + at, sizeof(T));
		return value;
	}
}

void BazaarWindow::Attach(WindowManager &windows, ZoneSession &zone, GameWindowState &state, const GameValues &values,
	Host host)
{
	m_windows = &windows;
	m_zone = &zone;
	m_state = &state;
	m_values = &values;
	m_host = std::move(host);
	m_active = m_trader_mode = false;
	if (SidlWindow *wnd = m_windows->Find(Screen))
		for (int i = 0; i < Slots; ++i)
			if (SidlWindow::Control *slot = wnd->Find(SlotName(i)))
				slot->item_slot = FirstSlot + i;
	Init();
	UpdatePriceButtons();
}

void BazaarWindow::SetFiles(const std::string &ini_path, const std::string &log_path)
{
	m_ini_path = ini_path;
	m_log_path = log_path;
}

bool BazaarWindow::IsSpecialBazaarSpot(uint32_t zone_id, float y, float x)
{
	if (zone_id != 151)
		return false;
	auto in = [&](float y_low, float y_high, float x_low, float x_high) {
		return y_low < y && y < y_high && x_low < x && x < x_high;
	};
	return in(-764, -601, -126, -83) || in(-704, -541, -227, -182) || in(-560, -520, -107, 94) ||
	       in(-763, -600, 74, 115) || in(-483, -320, 74, 115) || in(-483, -320, -127, -81);
}

void BazaarWindow::Init()
{
	// CBazaarWnd::Init (@00406aef): the slots in their usual colors, the list, selection and price
	// cleared.
	for (Entry &entry : m_entries)
		entry = {};
	ToggleActive(-1, false);
	m_selected = -1;
	m_selected_slot = -1;
	m_price = 0;
}

void BazaarWindow::AddText(const std::string &text, int color)
{
	// AddBazaarText: <c "#RRGGBB">text</c><br>, scrolled to the end; and bazaarlog's line.
	if (SidlWindow *wnd = m_windows->Find(Screen))
		if (SidlWindow::Control *box = wnd->Find("BZW_BazaarText")) {
			char hex[16];
			std::snprintf(hex, sizeof hex, "%06X", m_host.color ? m_host.color(color) & 0xffffff : 0xffffff);
			box->text += std::string("<c \"#") + hex + "\">" + text + "</c><br>";
			box->scroll_pos = 1 << 24; // SetVScrollPos(VScrollMax): drawing clamps it to the end
		}
	if (m_log_path.empty())
		return;
	std::time_t now = std::time(nullptr);
	std::string stamp = std::ctime(&now);
	if (!stamp.empty() && stamp.back() == '\n')
		stamp.pop_back();
	std::ofstream log(m_log_path, std::ios::app | std::ios::binary);
	log << "[" << stamp << "] " << text << "\n";
}

const ZoneItem *BazaarWindow::SelectedItem() const
{
	if (m_selected_slot < 0)
		return nullptr;
	auto it = m_zone->Items().find(m_selected_slot);
	return it != m_zone->Items().end() ? &it->second : nullptr;
}

const ZoneItem *BazaarWindow::FindInSatchels(int32_t id) const
{
	const auto &items = m_zone->Items();
	for (int16_t slot = Inventory::FirstGeneral; slot <= Inventory::LastGeneral; ++slot) {
		auto bag = items.find(slot);
		if (bag == items.end() || bag->second.id != Satchel)
			continue;
		for (int n = 0; n < bag->second.bag_slots; ++n) {
			auto it = items.find(Inventory::BagSlot(slot, n));
			if (it != items.end() && it->second.id == id)
				return &it->second;
		}
	}
	return nullptr;
}

void BazaarWindow::AddItem(int index, int16_t slot, int32_t price)
{
	// AddItemToBazaarList (@004074c2): the entry, and its stored price by the item's name ("*NULL*",
	// none: written as 0).
	const ZoneItem &item = m_zone->Items().at(slot);
	Entry &entry = m_entries[index];
	entry = {item.id, price, slot, false};
	if (m_ini_path.empty())
		return;
	IniFile ini;
	ini.Load(m_ini_path);
	std::string stored = ini.Get(Section, item.name, "*NULL*");
	if (!stored.empty() && stored[0] == '*') {
		ini.Set(Section, item.name, "0");
		ini.Save(m_ini_path);
	}
	else
		entry.price = std::atoi(stored.c_str());
}

void BazaarWindow::BuildItemArray()
{
	// BuildBazaarItemArray: every satchel's items in the general slots, but a NO DROP one unless
	// CanDrop lets it go under the FV rules.
	const auto &items = m_zone->Items();
	int count = 0;
	bool satchel = false;
	for (int16_t slot = Inventory::FirstGeneral; slot <= Inventory::LastGeneral; ++slot) {
		auto bag = items.find(slot);
		if (bag == items.end() || bag->second.id != Satchel)
			continue;
		satchel = true;
		for (int n = 0; n < bag->second.bag_slots && count < Slots; ++n) {
			int16_t at = Inventory::BagSlot(slot, n);
			auto it = items.find(at);
			if (it == items.end())
				continue;
			bool fv = m_values && m_values->fv_rules;
			if (it->second.no_drop == 0 && !(Inventory::CanDrop(items, it->second, at, fv) && fv))
				continue;
			AddItem(count++, at, 0);
		}
	}
	if (count == 0) {
		AddText(satchel ? "No items were found in your Trader Satchels."
		                : "You must purchase a Trader Satchel before you can sell items.",
			10);
		return;
	}
	AddText(std::to_string(count) + " items were found in Trader Satchels.", 10);
	AddText("Items with stored prices have been loaded from your INI file.", 10);
}

void BazaarWindow::UpdateFromInventory()
{
	// UpdateBazaarItemsFromInventory (@00407293): the list made again from the satchels (every item
	// in them), each keeping the price and sale of an entry of the same item before.
	std::array<Entry, Slots> before = m_entries;
	for (Entry &entry : m_entries)
		entry = {};
	const auto &items = m_zone->Items();
	int count = 0;
	for (int16_t slot = Inventory::FirstGeneral; slot <= Inventory::LastGeneral; ++slot) {
		auto bag = items.find(slot);
		if (bag == items.end() || bag->second.id != Satchel)
			continue;
		for (int n = 0; n < bag->second.bag_slots && count < Slots; ++n) {
			int16_t at = Inventory::BagSlot(slot, n);
			if (items.count(at))
				AddItem(count++, at, 0);
		}
	}
	for (Entry &entry : m_entries)
		for (const Entry &old : before)
			if (entry.item == old.item) {
				entry.price = old.price;
				entry.active = old.active;
				ToggleActive(entry.item, old.active);
			}
}

void BazaarWindow::ToggleActive(int32_t item, bool on)
{
	// ToggleBzrItemActive: a slot's picture green while its item's on sale (0xff00df00, 0xff40ff40
	// under the mouse), else chat colors 12 and 10; -1 colors every slot by its entry.
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	if (!wnd)
		return;
	uint32_t normal = m_host.color ? m_host.color(12) & 0xffffff : 0xffffff;
	uint32_t hover = m_host.color ? m_host.color(10) & 0xffffff : 0xffffff;
	for (int i = 0; i < Slots; ++i) {
		Entry &entry = m_entries[i];
		if (item != -1) {
			if (entry.item != item)
				continue;
			entry.active = on;
		}
		if (SidlWindow::Control *slot = wnd->Find(SlotName(i))) {
			slot->slot_tint = entry.active ? 0x00df00 : normal;
			slot->slot_hover_tint = entry.active ? 0x40ff40 : hover;
		}
	}
}

int BazaarWindow::ButtonValue(int coin) const
{
	SidlWindow *wnd = m_windows->Find(Screen);
	const SidlWindow::Control *button = wnd ? wnd->Find(MoneyButtons[coin]) : nullptr;
	return button ? std::atoi(button->text.c_str()) : 0;
}

int32_t BazaarWindow::ButtonPrice() const
{
	return ((ButtonValue(0) * 10 + ButtonValue(1)) * 10 + ButtonValue(2)) * 10 + ButtonValue(3);
}

void BazaarWindow::UpdateCoinButtons()
{
	SidlWindow *wnd = m_windows->Find(Screen);
	if (!wnd)
		return;
	int32_t values[4] = {m_price / 1000, m_price % 1000 / 100, m_price % 100 / 10, m_price % 10};
	for (int coin = 0; coin < 4; ++coin)
		if (SidlWindow::Control *button = wnd->Find(MoneyButtons[coin]))
			button->text = std::to_string(values[coin]);
}

void BazaarWindow::SetMoneyButton(int coin, int value)
{
	if (SidlWindow *wnd = m_windows->Find(Screen))
		if (SidlWindow::Control *button = wnd->Find(MoneyButtons[coin]))
			button->text = std::to_string(value);
	m_price = std::min(ButtonPrice(), 1000000000);
	UpdateCoinButtons();
}

void BazaarWindow::ClickedMoneyButton(int coin, int value, bool shift, bool ctrl)
{
	if (m_selected_slot < 0)
		return;
	if (value < 0) {
		// The quantity window over the button: up to 1000000 platinum, 100 of the others.
		SidlWindow *wnd = m_windows->Find(Screen);
		const SidlWindow::Control *button = wnd ? wnd->Find(MoneyButtons[coin]) : nullptr;
		int x = button ? button->rect.x + button->rect.w / 2 : -1, y = button ? button->rect.y + button->rect.h / 2 : -1;
		AskQuantity(*m_windows, *m_state, coin == 0 ? 1000000 : 100, x, y, shift, ctrl,
			[this, coin](int chosen) { ClickedMoneyButton(coin, chosen, false, false); }, ButtonValue(coin));
	}
	else
		SetMoneyButton(coin, value);
	UpdatePriceButtons();
}

void BazaarWindow::Enable(const char *control, bool on)
{
	if (SidlWindow *wnd = m_windows->Find(Screen))
		if (SidlWindow::Control *c = wnd->Find(control))
			c->enabled = on;
}

void BazaarWindow::Show(const char *control, bool on)
{
	if (SidlWindow *wnd = m_windows->Find(Screen))
		if (SidlWindow::Control *c = wnd->Find(control))
			c->visible = on;
}

void BazaarWindow::UpdatePriceButtons()
{
	for (const char *button : {"BZW_Add_Button", "BZW_Remove_Button", "BZW_List_Button", "BZW_SetPrice_Button",
	                           "BZW_Clear_Button"})
		Enable(button, false);
	Show("BZW_End_Button", m_trader_mode);
	Show("BZW_Start_Button", !m_trader_mode);
	if (m_trader_mode)
		Enable("BZW_List_Button", true);
	if (m_selected_slot >= 0)
		Enable("BZW_SetPrice_Button", true);
	if (ButtonPrice() != 0) {
		Enable("BZW_Clear_Button", true);
		if (m_trader_mode && m_selected >= 0 && m_selected < Slots)
			Enable(m_entries[m_selected].active ? "BZW_Remove_Button" : "BZW_Add_Button", true);
	}
}

bool BazaarWindow::StoreSelectedPrice()
{
	const ZoneItem *item = SelectedItem();
	if (!item)
		return false;
	int32_t price = ButtonPrice();
	bool found = false;
	for (Entry &entry : m_entries)
		if (entry.item == item->id) {
			entry.price = price;
			found = true;
		}
	if (found) {
		AddText(Format("Item: %s price set to (%s), INI file updated.", item->name, PriceString(price)), 10);
		if (!m_ini_path.empty()) {
			IniFile ini;
			ini.Load(m_ini_path);
			ini.Set(Section, item->name, std::to_string(price));
			ini.Save(m_ini_path);
		}
	}
	return found;
}

void BazaarWindow::SendPrice(int32_t item, int32_t price)
{
	std::vector<uint8_t> body(12, 0);
	body[0] = 3;
	std::memcpy(&body[4], &item, 4);
	std::memcpy(&body[8], &price, 4);
	m_zone->SendTrader(body, "3 (item " + std::to_string(item) + " at " + std::to_string(price) + ")");
}

void BazaarWindow::SendCode(uint16_t code)
{
	std::vector<uint8_t> body(20, 0);
	std::memcpy(&body[0], &code, 2);
	m_zone->SendTrader(body, std::to_string(code));
}

int BazaarWindow::SubmitItems()
{
	const ZoneProfile &me = m_zone->Profile();
	if (!IsSpecialBazaarSpot(me.zone_id, me.y, me.x))
		return -1;
	std::vector<uint8_t> body(644, 0);
	body[0] = 1;
	int n = 0;
	for (Entry &entry : m_entries) {
		if (entry.price == 0)
			continue;
		std::memcpy(&body[4 + n * 4], &entry.item, 4);
		std::memcpy(&body[324 + n * 4], &entry.price, 4);
		entry.active = true;
		++n;
	}
	if (n == 0)
		return -2;
	m_zone->SendTrader(body, "1 (start, " + std::to_string(n) + " items)");
	return 1;
}

void BazaarWindow::Activate()
{
	if (m_active || m_zone->Profile().zone_id != 151 || (m_host.ok_to_transact && !m_host.ok_to_transact()))
		return;
	m_windows->Show(Screen, true);
	m_windows->Raise(Screen);
	m_active = true;
	if (SidlWindow *wnd = m_windows->Find(Screen))
		if (SidlWindow::Control *box = wnd->Find("BZW_BazaarText")) {
			box->text = " ";
			box->scroll_pos = 0;
		}
	Init();
	UpdateCoinButtons();
	AddText("Welcome to your Bazaar Manager Window.", 10);
	BuildItemArray();
	UpdatePriceButtons();
}

void BazaarWindow::Deactivate()
{
	if (!m_active)
		return;
	m_active = false;
	m_windows->Show(Screen, false);
	SendCode(2);
}

void BazaarWindow::TraderMessage(const std::vector<uint8_t> &data)
{
	uint16_t code = Get<uint16_t>(data, 0);
	switch (code) {
	case 1:
		if (Get<uint32_t>(data, 4) == 0) {
			AddText("You must be in one of the bazaar merchant areas before you can make your items available to "
			        "other players.",
				13);
			break;
		}
		AddText("Bazaar Trader Mode *ON*", 13);
		m_trader_mode = true;
		ToggleActive(-1, true);
		UpdatePriceButtons();
		SendCode(11);
		break;
	case 2:
		AddText("Bazaar Trader Mode *OFF*", 13);
		if (m_host.chat)
			m_host.chat("Bazaar Trader Mode is now *OFF*", 15);
		m_trader_mode = false;
		UpdatePriceButtons();
		for (Entry &entry : m_entries)
			entry.active = false;
		ToggleActive(-1, false);
		break;
	case 3:
		if (const ZoneItem *item = SelectedItem()) {
			switch (Get<uint32_t>(data, 4)) {
			case 0:
				ToggleActive(item->id, false);
				AddText("Unable to add item to Bazaar List.", 15);
				break;
			case 1:
				ToggleActive(item->id, true);
				AddText(Format("Price updated for item %s.", item->name), 15);
				break;
			case 2:
				ToggleActive(item->id, false);
				AddText(Format("Item %s removed from bazaar list.", item->name), 15);
				break;
			case 3:
				ToggleActive(item->id, true);
				AddText(Format("Item %s added to bazaar list.", item->name), 15);
				break;
			default:
				break;
			}
		}
		UpdatePriceButtons();
		break;
	case 10: {
		// TraderBuy_Struct: the customer, the price, how many and the item's name.
		uint16_t who = Get<uint16_t>(data, 2);
		auto it = m_zone->Spawns().find(who);
		if (it != m_zone->Spawns().end()) {
			std::string name(reinterpret_cast<const char *>(data.data()) + std::min<size_t>(16, data.size()),
				data.size() > 16 ? strnlen(reinterpret_cast<const char *>(data.data()) + 16, data.size() - 16) : 0);
			AddText(it->second.name + " purchased " + std::to_string(Get<int16_t>(data, 12)) + " " + name + " for (" +
			            PriceString(Get<uint32_t>(data, 8)) + ").",
				15);
		}
		UpdateFromInventory();
		break;
	}
	case 11:
		AddText("You have placed these items up for sale.", 15);
		for (int i = 0; i < Slots; ++i) {
			int32_t id = Get<int32_t>(data, 4 + i * 4);
			if (id == 0)
				continue;
			const ZoneItem *item = FindInSatchels(id);
			AddText(item ? " " + std::to_string(i + 1) + ".) " + item->name + " (Price " +
			                   PriceString(Get<uint32_t>(data, 324 + i * 4)) + ")."
			             : " " + std::to_string(i) + ".) Item NOT FOUND",
				10);
		}
		AddText("The items listed above and high lighted in green can automatically be purchased by a buyer by "
		        "right-clicking on you.",
			10);
		break;
	case 13: {
		auto it = m_zone->Spawns().find(Get<uint16_t>(data, 4));
		if (it != m_zone->Spawns().end())
			AddText(it->second.name + " is browsing your wares.", 15);
		break;
	}
	default:
		break;
	}
}

bool BazaarWindow::SlotClick(const std::string &window, const std::string &control)
{
	if (window != Screen)
		return false;
	int slot = InvSlotAt(*m_windows, window, control);
	if (slot < FirstSlot || slot >= FirstSlot + Slots)
		return false;
	// CInvSlotMgr_SelectSlot's outline, then ProcessBazaarItemSelection: the item there, and the
	// first entry of that item with its price.
	if (SidlWindow *wnd = m_windows->Find(Screen)) {
		for (int i = 0; i < Slots; ++i)
			if (SidlWindow::Control *c = wnd->Find(SlotName(i)))
				c->checked = false;
		if (SidlWindow::Control *c = wnd->Find(control))
			c->checked = true;
	}
	int index = slot - FirstSlot;
	m_selected = index;
	m_selected_slot = m_entries[index].item != 0 ? m_entries[index].slot : -1;
	if (m_selected_slot >= 0) {
		m_price = 0;
		for (int i = 0; i < Slots; ++i)
			if (m_entries[i].item == m_entries[index].item) {
				m_selected = i;
				m_price = m_entries[i].price;
				break;
			}
	}
	else {
		m_selected = -1;
		m_price = 0;
	}
	UpdateCoinButtons();
	UpdatePriceButtons();
	return true;
}

bool BazaarWindow::Click(const std::string &window, const std::string &control, bool shift, bool ctrl)
{
	if (window != Screen)
		return false;
	const ZoneItem *item = SelectedItem();
	if (control == "BZW_SetPrice_Button") {
		int32_t price = ButtonPrice();
		if (m_selected != -1 && StoreSelectedPrice() && m_trader_mode && item) {
			if (!m_entries[m_selected].active)
				AddText("Click Add to add this item to your bazaar list.", 15);
			else {
				SendPrice(item->id, price);
				AddText("Price change updated to your bazaar list.", 15);
			}
		}
	}
	else if (control == "BZW_Add_Button") {
		int32_t price = ButtonPrice();
		if (price != 0 && item && m_trader_mode)
			SendPrice(item->id, price);
	}
	else if (control == "BZW_Remove_Button") {
		if (!item || !m_trader_mode)
			return true;
		SendPrice(item->id, 0);
		ToggleActive(item->id, false);
		return true;
	}
	else if (control == "BZW_Start_Button") {
		int result = SubmitItems();
		if (result == -1)
			AddText("To start the bazaar feature, you must stand under one of the rows of trader stalls.", 15);
		else if (result == -2)
			AddText("To become a merchant you must assign a price to an item in your list. Do this by selecting an "
			        "item, then selecting a money amount, and then clicking set price.",
				15);
		return true;
	}
	else if (control == "BZW_List_Button") {
		if (m_trader_mode)
			SendCode(11);
		return true;
	}
	else if (control == "BZW_Clear_Button") {
		m_price = 0;
		UpdateCoinButtons();
		if (m_selected != -1 && StoreSelectedPrice() && m_trader_mode && item) {
			SendPrice(item->id, 0);
			ToggleActive(item->id, false);
		}
	}
	else if (control == "BZW_End_Button") {
		SendCode(2);
		for (Entry &entry : m_entries)
			entry.active = false;
		ToggleActive(-1, false);
		return true;
	}
	else {
		for (int coin = 0; coin < 4; ++coin)
			if (control == MoneyButtons[coin]) {
				ClickedMoneyButton(coin, -1, shift, ctrl);
				return true;
			}
		return false;
	}
	UpdatePriceButtons();
	return true;
}

void BazaarWindow::Frame()
{
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	if (!wnd)
		return;
	// The close box (or anything else) put it down: Deactivate's.
	if (m_active && !m_windows->Shown(Screen))
		Deactivate();
	// CInvSlotMgr::UpdateSlots: each slot shows its entry's item.
	for (int i = 0; i < Slots; ++i)
		if (SidlWindow::Control *slot = wnd->Find(SlotName(i))) {
			const ZoneItem *item = nullptr;
			if (m_entries[i].item != 0 && m_entries[i].slot >= 0) {
				auto it = m_zone->Items().find(m_entries[i].slot);
				if (it != m_zone->Items().end() && it->second.id == m_entries[i].item)
					item = &it->second;
			}
			if (m_values)
				FillInvSlot(*slot, item, *m_values);
		}
}
