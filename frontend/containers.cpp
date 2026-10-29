#include "containers.h"
#include "chat_text.h"
#include "eqstr.h"
#include "game_values.h"
#include "game_windows.h"
#include "inventory.h"
#include "window_manager.h"
#include "zone_session.h"

#include <algorithm>
#include <string>

namespace
{
	constexpr int MaxOpen = 17; // CContainerMgr's pPCContainers
	constexpr uint8_t TradeskillCombineMin = 9, NoCombineType = 51;

	// The places a bag window opens from, and how far over each one after the first is moved
	// (SetContainer).
	struct BagPlace
	{
		const char *prefix;
		int16_t first, last;
		int dx, dy;
	};
	constexpr BagPlace Places[] = {
		{"BagInv", Inventory::FirstGeneral, Inventory::LastGeneral, 5, 2},
		{"BagBank", Inventory::FirstBank, Inventory::LastBank, -5, 2},
		{"BagTrade", Inventory::FirstTrade, Inventory::LastHisTrade, -5, -2},
	};

	// The class and racial containers HandleCombine checks, by bag type: the class or race
	// that may combine in it, and the message for anyone else.
	struct Restriction
	{
		uint8_t type;
		bool by_race;
		int who;
		uint32_t message;
		const char *text;
	};
	constexpr Restriction Restrictions[] = {
		{24, false, 12, 12010, "Your class may not use this tome."}, // a wizard's
		{25, false, 13, 12010, "Your class may not use this tome."}, // a magician's
		{26, false, 11, 12010, "Your class may not use this tome."}, // a necromancer's
		{27, false, 14, 12010, "Your class may not use this tome."}, // an enchanter's
		{31, true, 5, 12011, "Only High Elves may use this forge."},
		{32, true, 6, 12012, "Only Dark Elves may use this forge."},
		{33, true, 10, 12013, "Only Ogres may use this forge."},
		{34, true, 8, 12014, "Only Dwarves may use this forge."},
		{35, true, 12, 12015, "Only Gnomes may use this forge."},
		{36, true, 2, 12016, "Only Barbarians may use this forge."},
		{38, true, 128, 12017, "Only Iksars may use this forge."},
		{39, true, 1, 12018, "Only Humans may use this forge."},
		{40, true, 1, 12018, "Only Humans may use this forge."},
		{45, true, 128, 12019, "Only Iksar may use this pottery wheel."},
		{42, true, 3, 12020, "Only Erudites may use this tailoring kit."},
		{41, true, 11, 12021, "Only Halflings may use this tailoring kit."},
		{43, true, 4, 12022, "Only Wood Elves may use this tailoring kit."},
		{44, true, 4, 12023, "Only Wood Elves may use this fletching kit."},
	};

	std::string EqText(const EqStr *eqstr, uint32_t id, const std::string &fallback)
	{
		return eqstr && eqstr->Has(id) ? eqstr->Format(id, {}) : fallback;
	}

	// The bag types that open and shut with their own sounds (OpenContainer, CloseEQContainer).
	bool OpensAndShuts(uint8_t type)
	{
		return type > 5 && (type < 8 || type == 10 || type == 15 || (type > 17 && (type < 21 || type == 22)));
	}

	const ZoneItem *Find(const std::map<int16_t, ZoneItem> &items, int16_t slot)
	{
		auto it = items.find(slot);
		return it != items.end() ? &it->second : nullptr;
	}
}

std::string Containers::WindowName(int16_t slot)
{
	for (const BagPlace &place : Places)
		if (slot >= place.first && slot <= place.last)
			return place.prefix + std::to_string(slot - place.first + 1);
	return "";
}

void Containers::AddWindows(WindowManager &windows)
{
	for (const BagPlace &place : Places)
		for (int16_t slot = place.first; slot <= place.last; ++slot) {
			std::string name = WindowName(slot);
			if (!windows.Add("ContainerWindow", false, nullptr, name))
				return; // no bag window in the UI files
			Rect r = windows.WindowRect(name);
			int n = slot - place.first;
			windows.MoveTo(name, r.x + place.dx * n, r.y + place.dy * n);
		}
}

void Containers::Attach(WindowManager &windows, ZoneSession &zone, const GameValues &values, Host host)
{
	m_windows = &windows;
	m_zone = &zone;
	m_values = &values;
	m_host = std::move(host);
	Reset();
}

void Containers::Reset()
{
	m_open.clear();
	m_combining = false;
	if (!m_windows)
		return;
	for (const BagPlace &place : Places)
		for (int16_t slot = place.first; slot <= place.last; ++slot)
			m_windows->Show(WindowName(slot), false);
}

bool Containers::IsOpen(int16_t slot) const
{
	return std::any_of(m_open.begin(), m_open.end(), [slot](const Bag &bag) { return bag.slot == slot; });
}

std::string Containers::WindowOf(int16_t slot) const
{
	for (const Bag &bag : m_open)
		if (bag.slot == slot)
			return bag.window;
	return "";
}

bool Containers::Toggle(int16_t slot)
{
	if (IsOpen(slot)) {
		Close(slot);
		return true;
	}
	return Open(slot);
}

void Containers::PlayOpenClose(const ZoneItem *bag, bool open)
{
	if (!m_host.sound)
		return;
	int sound = 0x91;
	if (bag && OpensAndShuts(bag->bag_type))
		sound = open ? 0x86 : 0x85;
	m_host.sound(sound);
}

bool Containers::Open(int16_t slot)
{
	if (!m_windows || !m_zone)
		return false;
	const ZoneItem *bag = Find(m_zone->Items(), slot);
	std::string window = WindowName(slot);
	if (!bag || bag->item_class != 1 || window.empty() || !m_windows->Find(window))
		return false;
	if (IsOpen(slot))
		return true;
	if (static_cast<int>(m_open.size()) >= MaxOpen)
		return false;
	PlayOpenClose(bag, true);
	m_open.push_back({slot, window});
	SetContainer(window, *bag, slot);
	m_windows->Show(window, true);
	m_windows->Raise(window);
	return true;
}

void Containers::Close(int16_t slot)
{
	auto it = std::find_if(m_open.begin(), m_open.end(), [slot](const Bag &bag) { return bag.slot == slot; });
	if (it == m_open.end())
		return;
	PlayOpenClose(m_zone ? Find(m_zone->Items(), slot) : nullptr, false);
	if (m_windows)
		m_windows->Show(it->window, false);
	m_open.erase(it);
}

bool Containers::CloseAll()
{
	bool any = !m_open.empty();
	while (!m_open.empty())
		Close(m_open.front().slot);
	return any;
}

void Containers::SetContainer(const std::string &window, const ZoneItem &bag, int16_t slot)
{
	SidlWindow *wnd = m_windows->Find(window);
	if (!wnd)
		return;
	wnd->Root().text = bag.name;
	if (SidlWindow::Control *label = wnd->Find("Container_Label"))
		label->text = bag.name;
	if (SidlWindow::Control *icon = wnd->Find("Container_Icon")) {
		icon->cell_animation = "A_DragItem";
		icon->cell = bag.icon >= 500 ? bag.icon - 500 : -1;
	}
	// What the label, the icon and the slots shown take up, in the client area.
	bool any = false;
	int left = 0, top = 0, right = 0, bottom = 0;
	auto expand = [&](const SidlWindow::Control *c) {
		if (!c)
			return;
		const Rect &r = c->def->rect;
		if (!any) {
			left = r.x, top = r.y, right = r.Right(), bottom = r.Bottom();
			any = true;
			return;
		}
		left = std::min(left, r.x);
		top = std::min(top, r.y);
		right = std::max(right, r.Right());
		bottom = std::max(bottom, r.Bottom());
	};
	expand(wnd->Find("Container_Label"));
	expand(wnd->Find("Container_Icon"));
	for (int n = 0; n < Inventory::BagSlots; ++n) {
		SidlWindow::Control *c = wnd->Find("ContainerSlot" + std::to_string(n + 1));
		if (!c)
			continue;
		bool shown = n < bag.bag_slots;
		c->visible = shown;
		c->item_slot = shown ? Inventory::BagSlot(slot, n) : -1;
		if (shown)
			expand(c);
	}
	// Combine (for a tradeskill container) and Done under them, centered.
	auto below = [&](const std::string &id, bool shown) {
		SidlWindow::Control *c = wnd->Find(id);
		if (!c)
			return;
		c->visible = shown;
		if (!shown)
			return;
		const Rect &r = c->def->rect;
		wnd->Reposition(id, (right - left) / 2 - r.w / 2, bottom + 4);
		bottom += 4 + r.h;
	};
	below("Container_Combine", bag.bag_type >= TradeskillCombineMin && bag.bag_type != NoCombineType);
	below("DoneButton", true);
	if (any && left < right)
		m_windows->Resize(window, right - left + 14, bottom - top + 36);
}

void Containers::Update()
{
	if (!m_windows || !m_zone)
		return;
	const std::map<int16_t, ZoneItem> &items = m_zone->Items();
	for (size_t i = 0; i < m_open.size();) {
		const Bag &bag = m_open[i];
		const ZoneItem *item = Find(items, bag.slot);
		// The close box hides it; the bag gone from its slot (the zone moved it) closes it.
		if (!m_windows->Shown(bag.window) || !item || item->item_class != 1) {
			Close(bag.slot);
			continue;
		}
		if (SidlWindow *wnd = m_windows->Find(bag.window))
			for (int n = 0; n < Inventory::BagSlots; ++n)
				if (SidlWindow::Control *c = wnd->Find("ContainerSlot" + std::to_string(n + 1)); c && c->visible)
					FillInvSlot(*c, Find(items, static_cast<int16_t>(c->item_slot)), *m_values);
		++i;
	}
}

bool Containers::Click(const std::string &window, const std::string &control)
{
	auto it = std::find_if(m_open.begin(), m_open.end(), [&](const Bag &bag) { return bag.window == window; });
	if (it == m_open.end())
		return false;
	if (control == "DoneButton") {
		Close(it->slot);
		return true;
	}
	if (control == "Container_Combine") {
		Combine(*it);
		return true;
	}
	return false;
}

void Containers::Combine(const Bag &open)
{
	// Only a bag in a general slot (field190_0x178; and a world container, which we don't open yet).
	if (open.slot < Inventory::FirstGeneral || open.slot > Inventory::LastGeneral || m_combining)
		return;
	const std::map<int16_t, ZoneItem> &items = m_zone->Items();
	const ZoneItem *bag = Find(items, open.slot);
	if (!bag)
		return;
	const ZoneProfile &me = m_zone->Profile();
	const EqStr *eqstr = m_values ? m_values->eqstr : nullptr;
	for (const Restriction &r : Restrictions)
		if (bag->bag_type == r.type && (r.by_race ? me.race : me.class_id) != r.who) {
			if (m_host.chat)
				m_host.chat(EqText(eqstr, r.message, r.text), ChatType::Yellow);
			return;
		}
	bool coins = me.cursor_platinum || me.cursor_gold || me.cursor_silver || me.cursor_copper;
	if (items.count(Inventory::Cursor) || coins) {
		if (m_host.chat)
			m_host.chat(EqText(eqstr, 12024, "You cannot combine items when your hands are full!  Your cursor must be "
				"free of items and money before you can combine items together in a special container."), ChatType::Yellow);
		return;
	}
	int16_t ids[Inventory::BagSlots];
	int count = 0;
	for (int n = 0; n < Inventory::BagSlots; ++n) {
		const ZoneItem *item = n < bag->bag_slots ? Find(items, Inventory::BagSlot(open.slot, n)) : nullptr;
		ids[n] = item ? static_cast<int16_t>(item->id) : -1;
		count += item != nullptr;
	}
	if (count == 0) {
		if (m_host.chat)
			m_host.chat(EqText(eqstr, 12025, "You must place items in this object in order to create something new."),
				ChatType::Default);
		return;
	}
	if (m_zone->TradeSkillCombine(bag->bag_type, open.slot, ids, bag->id))
		m_combining = true;
}

void Containers::DrawTooltips(Canvas &canvas, TextRenderer &text, int mouse_x, int mouse_y)
{
	if (!m_windows)
		return;
	std::string under = m_windows->ScreenAt(mouse_x, mouse_y);
	for (const Bag &bag : m_open) {
		if (bag.window != under)
			continue;
		const SidlWindow *wnd = m_windows->Find(bag.window);
		if (!wnd || wnd->Minimized())
			return;
		int first_x = -1;
		for (int n = 0; n < Inventory::BagSlots; ++n) {
			const SidlWindow::Control *c = wnd->Find("ContainerSlot" + std::to_string(n + 1));
			if (!c || !wnd->Shown(*c) || c->tooltip.empty() || c->cell < 0)
				continue;
			if (first_x < 0)
				first_x = c->rect.x;
			int y = c->rect.y + (c->rect.x == first_x ? 2 : 22);
			m_windows->DrawTooltip(canvas, text, c->tooltip, c->def->font, c->rect.Right() - 5, y, false);
		}
		return;
	}
}
