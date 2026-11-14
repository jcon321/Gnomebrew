#include "bazaar_search.h"

#include "window_manager.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace
{
	// eqgame's combo tables (@005e3844 on): each choice's name and what's sent for it, in order.
	struct Choice
	{
		const char *name;
		int16_t value;
	};
	constexpr Choice Types[] = {
		{"1H Slashing", 0}, {"1H Blunt", 3}, {"2H Blunt", 4}, {"2H Slashing", 1}, {"2H Piercing", 35},
		{"Alcohol", 38}, {"All Effects", 46}, {"Armor", 10}, {"Arrow", 27}, {"Bandage", 18}, {"Book", 31},
		{"Bow", 5}, {"Brass Instruments", 25}, {"Drink", 15}, {"Combinable", 17}, {"Flowing Thought", 48},
		{"Focus Effect", 49}, {"Food", 14}, {"Haste", 47}, {"Jewelry", 29}, {"Key", 33}, {"Light", 16},
		{"Martial", 45}, {"Misc", 11}, {"Note", 32}, {"Percussion Instruments", 26}, {"Scroll", 20},
		{"Shield", 8}, {"Throwing", 19}, {"Stringed Instruments", 24}, {"Piercing", 2}, {"Potion", 21},
		{"Wind Instruments", 23},
	};
	constexpr Choice SlotChoices[] = {
		{"Ammo", 21}, {"Arms", 7}, {"Back", 8}, {"Chest", 17}, {"Ear", 1}, {"Face", 3}, {"Feet", 19},
		{"Fingers", 15}, {"Hands", 12}, {"Head", 2}, {"Held", 0}, {"Legs", 18}, {"Neck", 5}, {"Primary", 13},
		{"Range", 11}, {"Secondary", 14}, {"Shoulders", 6}, {"Waist", 20}, {"Wrist", 9},
	};
	constexpr Choice Races[] = {
		{"Barbarian", 2}, {"Dark Elf", 6}, {"Dwarf", 8}, {"Erudite", 3}, {"Gnome", 12}, {"Half Elf", 7},
		{"Halfling", 11}, {"High Elf", 5}, {"Human", 1}, {"Iksar", 13}, {"Ogre", 10}, {"Troll", 9},
		{"Vah Shir", 14}, {"Wood Elf", 4},
	};
	constexpr Choice Classes[] = {
		{"Bard", 8}, {"Beastlord", 15}, {"Cleric", 2}, {"Druid", 6}, {"Enchanter", 14}, {"Magician", 13},
		{"Monk", 7}, {"Necromancer", 11}, {"Paladin", 3}, {"Ranger", 4}, {"Rogue", 9}, {"Shadow Knight", 5},
		{"Shaman", 10}, {"Warrior", 1}, {"Wizard", 12},
	};
	constexpr Choice Stats[] = {
		{"Armor Class", 14}, {"Agility", 2}, {"Charisma", 6}, {"Dexterity", 3}, {"Intelligence", 4},
		{"Stamina", 1}, {"Strength", 0}, {"Wisdom", 5}, {"Vs Cold", 8}, {"Vs Disease", 11}, {"Vs Fire", 9},
		{"Vs Magic", 7}, {"Vs Poison", 10}, {"Mana", 12}, {"Hit Points", 13},
	};

	struct Combo
	{
		const char *control, *first;
		const Choice *choices;
		size_t count;
	};
	const Combo Combos[] = {
		{"BZR_ItemTypeCombobox", "Any Type", Types, std::size(Types)},
		{"BZR_ClassSlotCombobox", "Any Class", Classes, std::size(Classes)},
		{"BZR_RaceSlotCombobox", "Any Race", Races, std::size(Races)},
		{"BZR_StatSlotCombobox", "Any Stat", Stats, std::size(Stats)},
		{"BZR_ItemSlotCombobox", "Any Slot", SlotChoices, std::size(SlotChoices)},
	};

	// CBazaarSearchWnd_GetPriceString (@0040570e): " 1p 2g 3s 4c" without the coins that are 0.
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
		return out;
	}

	std::string Upper(std::string s)
	{
		for (char &c : s)
			c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		return s;
	}

	template <typename T> T Get(const std::vector<uint8_t> &data, size_t at)
	{
		T value{};
		if (at + sizeof(T) <= data.size())
			std::memcpy(&value, data.data() + at, sizeof(T));
		return value;
	}

	std::string CString(const std::vector<uint8_t> &data, size_t at, size_t size)
	{
		if (at >= data.size())
			return "";
		size = std::min(size, data.size() - at);
		const char *p = reinterpret_cast<const char *>(data.data() + at);
		return std::string(p, strnlen(p, size));
	}
}

void BazaarSearch::Attach(WindowManager &windows, ZoneSession &zone, Host host)
{
	m_windows = &windows;
	m_zone = &zone;
	m_host = std::move(host);
	m_active = false;
	m_results.clear();
	SidlWindow *wnd = m_windows->Find(Screen);
	if (!wnd)
		return;
	// The constructor: each combo's first choice and the table's, the first chosen; the boxes'
	// limits; the list's columns justified (the price's centered).
	for (const Combo &combo : Combos)
		if (SidlWindow::Control *c = wnd->Find(combo.control)) {
			c->choices = {combo.first};
			for (size_t i = 0; i < combo.count; ++i)
				c->choices.push_back(combo.choices[i].name);
			c->choice = 0;
		}
	if (SidlWindow::Control *c = wnd->Find("BZR_PlayersCombobox")) {
		c->choices = {"All Traders"};
		c->choice = 0;
	}
	if (SidlWindow::Control *c = wnd->Find("BZR_ItemNameInput"))
		c->max_chars = 0x1e;
	for (const char *box : {"BZR_MaxPriceInput", "BZR_MinPriceInput"})
		if (SidlWindow::Control *c = wnd->Find(box))
			c->max_chars = 7;
	if (SidlWindow::Control *list = wnd->Find("BZR_ItemList")) {
		list->justification = {0, 1, 0, 0};
		list->column_clicks = true;
		list->rows.clear();
	}
}

void BazaarSearch::Init()
{
	m_results.clear();
	UpdatePlayerCombo();
	if (SidlWindow *wnd = m_windows->Find(Screen))
		if (SidlWindow::Control *list = wnd->Find("BZR_ItemList")) {
			list->rows.clear();
			list->selected_row = -1;
		}
	SetText("BZR_MaxPriceInput", "0");
	SetText("BZR_MinPriceInput", "0");
	if (m_host.chat)
		m_host.chat("To begin searching for items, select a search parameter from one of the drop down menus or type in "
		            "an item name in the edit box and click Find.",
			15);
}

void BazaarSearch::Activate()
{
	if (m_zone->Profile().zone_id == 151) {
		Init();
		m_windows->Show(Screen, true);
		m_windows->Raise(Screen);
		m_active = true;
		for (bool &d : m_descending)
			d = false;
		m_sort_column = 0;
	}
	UpdateUIState();
}

void BazaarSearch::Deactivate()
{
	m_active = false;
	m_windows->Show(Screen, false);
}

std::string BazaarSearch::InputText(const char *control) const
{
	SidlWindow *wnd = m_windows->Find(Screen);
	const SidlWindow::Control *c = wnd ? wnd->Find(control) : nullptr;
	return c ? c->text : "";
}

void BazaarSearch::SetText(const char *control, const std::string &text)
{
	if (SidlWindow *wnd = m_windows->Find(Screen))
		if (SidlWindow::Control *c = wnd->Find(control))
			c->text = text;
	if (std::string(control) == "BZR_ItemNameInput")
		m_name_text = text;
	else if (std::string(control) == "BZR_MinPriceInput")
		m_min_text = text;
	else if (std::string(control) == "BZR_MaxPriceInput")
		m_max_text = text;
}

int BazaarSearch::Choice(const char *control) const
{
	SidlWindow *wnd = m_windows->Find(Screen);
	const SidlWindow::Control *c = wnd ? wnd->Find(control) : nullptr;
	return c ? c->choice : 0;
}

void BazaarSearch::Enable(const char *control, bool on)
{
	if (SidlWindow *wnd = m_windows->Find(Screen))
		if (SidlWindow::Control *c = wnd->Find(control))
			c->enabled = on;
}

void BazaarSearch::UpdatePlayerCombo()
{
	// Every trader here by name, sorted, after All Traders; Update Players off.
	std::vector<std::string> names;
	for (const auto &[id, spawn] : m_zone->Spawns())
		if (spawn.trader)
			names.push_back(spawn.name);
	if (m_zone->Self().trader)
		names.push_back(m_zone->Self().name);
	std::sort(names.begin(), names.end());
	if (names.size() > 499)
		names.resize(499);
	if (SidlWindow *wnd = m_windows->Find(Screen))
		if (SidlWindow::Control *c = wnd->Find("BZR_PlayersCombobox")) {
			c->choices = {"All Traders"};
			c->choices.insert(c->choices.end(), names.begin(), names.end());
			c->choice = 0;
		}
	Enable("BZR_UpdatePlayerButton", false);
}

void BazaarSearch::TraderChanged()
{
	Enable("BZR_UpdatePlayerButton", true);
}

void BazaarSearch::UpdateUIState()
{
	Enable("BZR_RequestItemButton", false);
	Enable("BZR_QueryButton", false);
	for (const Combo &combo : Combos)
		Enable(combo.control, true);
	Enable("BZR_PlayersCombobox", true);
	Enable("BZR_ItemNameInput", true);
	bool any = InputText("BZR_ItemNameInput").size() >= 2 || Choice("BZR_PlayersCombobox") >= 1;
	for (const Combo &combo : Combos)
		any = any || Choice(combo.control) >= 1;
	SidlWindow *wnd = m_windows->Find(Screen);
	if (const SidlWindow::Control *list = wnd ? wnd->Find("BZR_ItemList") : nullptr; list && list->selected_row >= 0)
		Enable("BZR_RequestItemButton", true);
	const SidlWindow::Control *players = wnd ? wnd->Find("BZR_PlayersCombobox") : nullptr;
	if (any && players && players->choices.size() > 1)
		Enable("BZR_QueryButton", true);
}

void BazaarSearch::Perform()
{
	// PerformBazaarSearch: BazaarSearch_Struct {u16 7, u16 trader, u16 class, race, stat, slot,
	// type (each -1 for any), char name[66], i32 min, max}.
	SidlWindow *wnd = m_windows->Find(Screen);
	if (!wnd)
		return;
	m_results.clear();
	if (SidlWindow::Control *list = wnd->Find("BZR_ItemList")) {
		list->rows.clear();
		list->selected_row = -1;
	}
	auto value = [&](const char *control) -> int16_t {
		for (const Combo &combo : Combos)
			if (std::string(combo.control) == control) {
				int choice = Choice(control);
				return choice >= 1 && choice <= static_cast<int>(combo.count) ? combo.choices[choice - 1].value : -1;
			}
		return -1;
	};
	int16_t race = value("BZR_RaceSlotCombobox"), class_id = value("BZR_ClassSlotCombobox"),
	        stat = value("BZR_StatSlotCombobox"), slot = value("BZR_ItemSlotCombobox"),
	        type = value("BZR_ItemTypeCombobox");
	// The stat's name as the fourth column's heading.
	if (int choice = Choice("BZR_StatSlotCombobox"); choice >= 1)
		if (SidlWindow::Control *list = wnd->Find("BZR_ItemList"); list && list->columns.size() > 3)
			list->columns[3].heading = Stats[choice - 1].name;
	int32_t max = std::atoi(InputText("BZR_MaxPriceInput").c_str());
	int32_t min = std::atoi(InputText("BZR_MinPriceInput").c_str());
	uint16_t trader = 0;
	if (const SidlWindow::Control *players = wnd->Find("BZR_PlayersCombobox");
		players && players->choice >= 0 && players->choice < static_cast<int>(players->choices.size())) {
		const std::string &name = players->choices[players->choice];
		if (name == m_zone->Self().name)
			trader = m_zone->MySpawnId();
		for (const auto &[id, spawn] : m_zone->Spawns())
			if (spawn.npc == 0 && spawn.name == name)
				trader = id;
	}
	std::vector<uint8_t> body(0x58, 0);
	auto put16 = [&](size_t at, int16_t v) { std::memcpy(&body[at], &v, 2); };
	put16(0, 7);
	std::memcpy(&body[2], &trader, 2);
	put16(4, class_id);
	put16(6, race);
	put16(8, stat);
	put16(10, slot);
	put16(12, type);
	std::string name = InputText("BZR_ItemNameInput").substr(0, 63);
	std::memcpy(&body[14], name.c_str(), name.size());
	std::memcpy(&body[80], &min, 4);
	std::memcpy(&body[84], &max, 4);
	m_zone->SendBazaarSearch(body, "7 (search '" + name + "')");
}

void BazaarSearch::AddResult(const Result &result)
{
	if (static_cast<int>(m_results.size()) >= MaxResults)
		return;
	m_results.push_back(result);
	if (SidlWindow *wnd = m_windows->Find(Screen))
		if (SidlWindow::Control *list = wnd->Find("BZR_ItemList"))
			list->rows.push_back({result.name, PriceString(static_cast<uint32_t>(result.cost)), result.seller,
				result.stat != 0 ? std::to_string(result.stat) : ""});
}

void BazaarSearch::Sort(int column)
{
	// SortItemList: the results again in the column's order, ascending or (its flag) descending.
	std::vector<Result> results = m_results;
	bool down = column >= 0 && column < 4 && m_descending[column];
	auto less = [&](const Result &a, const Result &b) {
		switch (column) {
		case 0: return Upper(a.name) < Upper(b.name);
		case 1: return a.cost < b.cost;
		case 2: return a.seller < b.seller;
		case 3: return a.stat < b.stat;
		default: return false;
		}
	};
	std::stable_sort(results.begin(), results.end(),
		[&](const Result &a, const Result &b) { return down ? less(b, a) : less(a, b); });
	m_results.clear();
	if (SidlWindow *wnd = m_windows->Find(Screen))
		if (SidlWindow::Control *list = wnd->Find("BZR_ItemList")) {
			list->rows.clear();
			list->selected_row = -1;
		}
	for (const Result &result : results)
		AddResult(result);
}

void BazaarSearch::BazaarMessage(const std::vector<uint8_t> &data)
{
	int16_t action = Get<int16_t>(data, 0);
	if (action == 9) {
		// CBazaarWnd_HandleBazaarMsg's welcome.
		if (m_host.chat) {
			m_host.chat("Welcome to The Bazaar!", 10);
			m_host.chat("There are currently " + std::to_string(Get<int32_t>(data, 4)) + " traders and " +
			                std::to_string(Get<int32_t>(data, 8)) + " items for trade.",
				10);
			m_host.chat("Type /bazaar to begin viewing items for sale or type /trader to begin putting items up for sale.",
				10);
		}
		return;
	}
	if (action == 7) {
		Result result;
		uint16_t seller = Get<uint16_t>(data, 6);
		if (auto it = m_zone->Spawns().find(seller); it != m_zone->Spawns().end())
			result.seller = it->second.name;
		else if (seller != 0 && seller == m_zone->MySpawnId())
			result.seller = m_zone->Self().name;
		result.name = CString(data, 12, 64);
		result.cost = Get<int32_t>(data, 8);
		result.item = Get<int32_t>(data, 4);
		result.stat = Get<int32_t>(data, 16);
		AddResult(result);
	}
	else if (action == 12) {
		Sort(m_sort_column);
		Enable("BZR_QueryButton", true);
		UpdateUIState();
	}
}

void BazaarSearch::RequestItem(int row)
{
	if (row < 0 || row >= static_cast<int>(m_results.size()))
		return;
	// The name up to its last '(' (the count the zone adds), and the item's id.
	std::string name = m_results[row].name;
	if (size_t paren = name.rfind('('); paren != std::string::npos)
		name = name.substr(0, paren);
	m_zone->RequestItem(static_cast<uint16_t>(m_results[row].item & 0xffff), name);
}

bool BazaarSearch::Click(const std::string &window, const std::string &control)
{
	if (window != Screen)
		return false;
	SidlWindow *wnd = m_windows->Find(Screen);
	SidlWindow::Control *list = wnd ? wnd->Find("BZR_ItemList") : nullptr;
	if (control == "BZR_ItemList" && list && list->header_clicked >= 0) {
		// Notification 0xe: the column's direction flipped and the list sorted by it.
		int column = list->header_clicked;
		list->header_clicked = -1;
		m_sort_column = column;
		if (column >= 0 && column < 4)
			m_descending[column] = !m_descending[column];
		Sort(column);
		return true;
	}
	if (control == "BZR_Default") {
		SetText("BZR_ItemNameInput", "");
		if (wnd)
			for (const char *combo : {"BZR_PlayersCombobox", "BZR_ItemTypeCombobox", "BZR_ClassSlotCombobox",
			                          "BZR_RaceSlotCombobox", "BZR_StatSlotCombobox", "BZR_ItemSlotCombobox"})
				if (SidlWindow::Control *c = wnd->Find(combo))
					c->choice = 0;
		SetText("BZR_MaxPriceInput", "0");
		SetText("BZR_MinPriceInput", "0");
		UpdateUIState();
		return true;
	}
	if (control == "BZR_WelcomeButton") {
		std::vector<uint8_t> body(20, 0);
		body[0] = 9;
		m_zone->SendBazaarSearch(body, "9 (welcome)");
		return true;
	}
	if (control == "BZR_QueryButton") {
		Enable("BZR_QueryButton", false);
		Perform();
		return true;
	}
	if (control == "BZR_RequestItemButton") {
		RequestItem(list ? list->selected_row : -1);
		return true;
	}
	if (control == "BZR_UpdatePlayerButton")
		UpdatePlayerCombo();
	UpdateUIState(); // a combo's choice, a result selected, Update Players
	return true;
}

bool BazaarSearch::RightClick(const std::string &window, const std::string &control)
{
	if (window != Screen || control != "BZR_ItemList")
		return false;
	SidlWindow *wnd = m_windows->Find(Screen);
	const SidlWindow::Control *list = wnd ? wnd->Find("BZR_ItemList") : nullptr;
	if (!list || list->selected_row < 0 || list->selected_row >= static_cast<int>(m_results.size()))
		return true;
	if (list->selected_column == 0)
		RequestItem(list->selected_row);
	else if (list->selected_column == 2 && m_host.face) {
		const std::string &seller = m_results[list->selected_row].seller;
		for (const auto &[id, spawn] : m_zone->Spawns())
			if (spawn.npc == 0 && spawn.name == seller) {
				m_host.face(id);
				break;
			}
	}
	return true;
}

bool BazaarSearch::Enter()
{
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	if (!wnd || !m_windows->Shown(Screen) || !wnd->Focused("BZR_ItemNameInput"))
		return false;
	Perform();
	return true;
}

void BazaarSearch::Frame()
{
	if (!m_windows)
		return;
	if (m_active && !m_windows->Shown(Screen))
		m_active = false; // the close box
	// Notification 0xd: a box's text changed.
	std::string name = InputText("BZR_ItemNameInput"), min = InputText("BZR_MinPriceInput"),
	            max = InputText("BZR_MaxPriceInput");
	if (name != m_name_text || min != m_min_text || max != m_max_text) {
		m_name_text = name;
		m_min_text = min;
		m_max_text = max;
		UpdateUIState();
	}
}
