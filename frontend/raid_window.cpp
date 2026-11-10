#include "raid_window.h"

#include "game_values.h"
#include "raid.h"
#include "window_manager.h"

#include <cstdlib>

namespace
{
	constexpr const char *GroupList = "RAID_PlayerList";
	constexpr const char *OtherList = "RAID_NotInGroupPlayerList";
	constexpr const char *LooterList = "RAIDOPTIONS_LooterList";
	constexpr const char *LootTypeCombo = "RAIDOPTIONS_SetLootTypeCombo";
	constexpr uint32_t None = 0xffffffff;
	constexpr int Groups = 12, RowsPerGroup = 7, Rows = 0x53;
	constexpr uint32_t White = 0xffffff, Gray = 0x808080, Red = 0xff0000;
	constexpr const char *Empty = "empty";

	// The class colors' order (CRaid::UpdateClassColor's switch): bard, beastlord, cleric, druid,
	// enchanter, magician, monk, necromancer, paladin, ranger, rogue, shadow knight, shaman, warrior,
	// wizard.
	constexpr int ColorClass[RaidWindow::Classes] = {8, 15, 2, 6, 14, 13, 7, 11, 3, 4, 9, 5, 10, 1, 12};

	SidlWindow::Control *List(SidlWindow *wnd, const char *id)
	{
		return wnd ? wnd->Find(id) : nullptr;
	}

	// A list's text and colors at a row and column, made to fit.
	void Fit(SidlWindow::Control &list, int row)
	{
		size_t columns = list.columns.size() > 5 ? list.columns.size() : 5;
		if (list.rows.size() <= static_cast<size_t>(row))
			list.rows.resize(row + 1);
		if (list.rows[row].size() < columns)
			list.rows[row].resize(columns);
		if (list.cell_colors.size() <= static_cast<size_t>(row))
			list.cell_colors.resize(row + 1);
		if (list.cell_colors[row].size() < columns)
			list.cell_colors[row].resize(columns);
	}
	void SetText(SidlWindow::Control &list, int row, int column, const std::string &text)
	{
		Fit(list, row);
		list.rows[row][column] = text;
	}
	std::string GetText(const SidlWindow::Control &list, int row, int column)
	{
		if (row < 0 || row >= static_cast<int>(list.rows.size()) || column >= static_cast<int>(list.rows[row].size()))
			return "";
		return list.rows[row][column];
	}
	void SetColor(SidlWindow::Control &list, int row, int column, uint32_t color)
	{
		Fit(list, row);
		list.cell_colors[row][column] = color & 0xffffff;
	}
	uint32_t GetColor(const SidlWindow::Control &list, int row, int column)
	{
		if (row < 0 || row >= static_cast<int>(list.cell_colors.size()) || column >= static_cast<int>(list.cell_colors[row].size()))
			return 0;
		return list.cell_colors[row][column];
	}
	int AddRow(SidlWindow::Control &list)
	{
		int row = static_cast<int>(list.rows.size());
		Fit(list, row);
		if (list.separators.size() < list.rows.size())
			list.separators.resize(list.rows.size(), false);
		return row;
	}
	void Clear(SidlWindow::Control &list)
	{
		list.rows.clear();
		list.cell_colors.clear();
		list.separators.clear();
		list.selected_row = -1;
	}
	void SetLabel(SidlWindow *wnd, const char *id, const std::string &text)
	{
		if (SidlWindow::Control *label = wnd ? wnd->Find(id) : nullptr)
			label->text = text;
	}
}

void RaidWindow::Attach(WindowManager &windows, Raid &raid, const GameValues &values, Host host)
{
	m_windows = &windows;
	m_raid = &raid;
	m_values = &values;
	m_host = std::move(host);
	m_updated_ms = 0;
	// CRaidWnd::CRaidWnd: SetColors(white, yellow, gray), the headings' color, the justification.
	for (const char *id : {GroupList, OtherList})
		if (SidlWindow::Control *list = List(Window(), id)) {
			list->row_color = White;
			list->hover_color = 0xffff00;
			list->select_color = Gray;
			list->header_color = 0x8aa3ff;
			list->justification = {1, 0, 1, 0, 0};
		}
	// CRaidOptionsWnd::CRaidOptionsWnd: SetColors(white, yellow, gray), left-justified.
	if (SidlWindow::Control *list = List(Options(), LooterList)) {
		list->row_color = White;
		list->hover_color = 0xffff00;
		list->select_color = Gray;
		list->justification = {0};
	}
	// LoadClassColors and InitializeClassColor: [RaidWindow] ClassColor<n>, -1 (white) written
	// for one that isn't there.
	for (int i = 0; i < Classes; ++i) {
		std::string key = "ClassColor" + std::to_string(i);
		std::string value = m_host.load ? m_host.load(key) : "";
		uint32_t color = 0xffffffff;
		if (value.empty()) {
			if (m_host.save)
				m_host.save(key, "-1");
		}
		else
			color = static_cast<uint32_t>(std::atoi(value.c_str()));
		m_class_colors[i] = color & 0xffffff;
		if (SidlWindow::Control *button = Options() ? Options()->Find("RAIDOPTIONS_ClassColor" + std::to_string(i)) : nullptr)
			button->text_color = color & 0xffffff;
	}
}

SidlWindow *RaidWindow::Window() const
{
	return m_windows ? m_windows->Find(Screen) : nullptr;
}

SidlWindow *RaidWindow::Options() const
{
	return m_windows ? m_windows->Find(OptionsScreen) : nullptr;
}

void RaidWindow::Activate()
{
	if (!Window())
		return;
	UpdateUI();
	ClearPlayerList();
	m_raid->UpdateRaidPlayerList();
	m_windows->Show(Screen, true);
	m_windows->Raise(Screen);
}

void RaidWindow::Toggle()
{
	if (!Window())
		return;
	if (m_windows->Shown(Screen))
		m_windows->Show(Screen, false);
	else
		Activate();
}

void RaidWindow::Frame(uint64_t now_ms)
{
	if (!Window())
		return;
	if (now_ms - m_updated_ms > 250) {
		UpdateUI();
		m_updated_ms = now_ms;
	}
}

void RaidWindow::UpdateUI()
{
	SidlWindow *wnd = Window();
	if (!wnd)
		return;
	SidlWindow::Control *invite = wnd->Find("Raid_InviteButton"), *disband = wnd->Find("Raid_DisbandButton");
	SidlWindow::Control *accept = wnd->Find("Raid_AcceptButton"), *decline = wnd->Find("Raid_DeclineButton");
	SidlWindow::Control *leader = wnd->Find("Raid_MakeLeaderButton"), *options = wnd->Find("Raid_OptionsButton");
	SidlWindow::Control *add = wnd->Find("Raid_AddLooterButton"), *remove = wnd->Find("Raid_RemoveLooterButton");
	if (!invite || !disband || !accept || !decline || !leader || !options || !add || !remove)
		return;
	const Raid &raid = *m_raid;
	leader->enabled = false;
	options->enabled = raid.InRaid();
	accept->visible = decline->visible = raid.Invited();
	invite->visible = !raid.Invited();
	disband->visible = !raid.Invited();
	disband->enabled = raid.InRaid();
	invite->enabled = false;
	const ZoneSpawn *target = m_values->Target();
	if (target && target->npc == 0 && target->id != m_values->zone->MySpawnId()) {
		if (!raid.InRaid())
			invite->enabled = true;
		else if (raid.Leading()) {
			if (raid.Count() < Raid::MaxMembers)
				invite->enabled = true;
			leader->enabled = true;
		}
	}
	add->enabled = remove->enabled = raid.InRaid() && raid.Leading();
}

bool RaidWindow::Click(const std::string &window, const std::string &control)
{
	if (window == Screen && Window()) {
		Raid &raid = *m_raid;
		if (control == "Raid_AcceptButton")
			raid.Respond(true);
		else if (control == "Raid_DeclineButton")
			raid.Respond(false);
		else if (control == "Raid_InviteButton")
			raid.Invite();
		else if (control == "Raid_DisbandButton")
			raid.Disband();
		else if (control == GroupList || control == OtherList) {
			SidlWindow::Control *list = Window()->Find(control);
			if (list && list->selected_row >= 0)
				raid.Select(GetText(*list, list->selected_row, 1));
		}
		else if (control == "Raid_MakeLeaderButton")
			raid.MakeSelectedLeader();
		else if (control == "Raid_OptionsButton")
			ActivateOptions();
		else if (control == "Raid_AddLooterButton")
			raid.AddLooter();
		else if (control == "Raid_RemoveLooterButton")
			raid.RemoveLooter();
		else
			return false;
		return true;
	}
	if (window == OptionsScreen && Options()) {
		if (control == LootTypeCombo) {
			SidlWindow::Control *combo = Options()->Find(control);
			if (combo && combo->choice >= 0 && combo->choice < 3)
				m_raid->SetLootType(std::to_string(combo->choice + 1));
			return true;
		}
		const std::string prefix = "RAIDOPTIONS_ClassColor";
		if (control.compare(0, prefix.size(), prefix) == 0) {
			int index = std::atoi(control.c_str() + prefix.size());
			SidlWindow::Control *button = Options()->Find(control);
			if (index < 0 || index >= Classes || !button || !m_host.pick_color)
				return true;
			m_host.pick_color(button->text_color, [this, index, control](uint32_t rgb) {
				// The picker's 0x1e: the color in the ini, on the button, and in the list.
				uint32_t color = 0xff000000 | (rgb & 0xffffff);
				if (m_host.save)
					m_host.save("ClassColor" + std::to_string(index), std::to_string(static_cast<int32_t>(color)));
				if (SidlWindow::Control *b = Options() ? Options()->Find(control) : nullptr)
					b->text_color = rgb & 0xffffff;
				m_raid->UpdateClassColor(index, rgb & 0xffffff);
			});
			return true;
		}
	}
	return false;
}

void RaidWindow::ActivateOptions()
{
	SidlWindow *wnd = Options();
	if (!wnd)
		return;
	// UpdateRaidOptionsUI: only the raid's leader changes the loot type.
	const Raid &raid = *m_raid;
	if (SidlWindow::Control *combo = wnd->Find(LootTypeCombo)) {
		combo->enabled = false;
		if (raid.InRaid()) {
			combo->enabled = raid.Leading();
			SetLabel(wnd, "RAIDOPTIONS_CurrentLootType", std::to_string(raid.LootType()));
			combo->choice = static_cast<int>(raid.LootType()) - 1;
		}
	}
	ClearLooterList();
	m_raid->UpdateOptionsWindow();
	m_windows->Show(OptionsScreen, true);
	m_windows->Raise(OptionsScreen);
}

void RaidWindow::ClearPlayerList()
{
	SidlWindow *wnd = Window();
	SidlWindow::Control *list = List(wnd, GroupList), *other = List(wnd, OtherList);
	if (!list || !other)
		return;
	Clear(*list);
	Clear(*other);
	int group = 1, in_group = 0;
	for (int i = 0; i < Rows; ++i) {
		int row = AddRow(*list);
		if (in_group == 6) {
			list->separators[row] = true; // AddEmptyGroupSeparatorRow
			++group;
			in_group = 0;
			continue;
		}
		SetText(*list, row, 0, std::to_string(group));
		SetText(*list, row, 1, Empty);
		SetColor(*list, row, 1, Gray);
		++in_group;
	}
	wnd->UpdateListScroll(*list);
	wnd->UpdateListScroll(*other);
	SetLabel(wnd, "RAID_PlayerCountLabel", "0");
}

int RaidWindow::FindEmptyRow(uint32_t group) const
{
	SidlWindow::Control *list = List(Window(), GroupList);
	if (!list || group >= Groups)
		return -1;
	for (int row = group * RowsPerGroup; row < static_cast<int>(group) * RowsPerGroup + 6; ++row)
		if (GetText(*list, row, 1) == Empty)
			return row;
	return -1;
}

int RaidWindow::FindInGroup(const std::string &name, uint32_t group) const
{
	SidlWindow::Control *list = List(Window(), GroupList);
	if (!list || group >= Groups)
		return -1;
	for (int row = group * RowsPerGroup; row < static_cast<int>(group) * RowsPerGroup + 6; ++row)
		if (GetText(*list, row, 1) == name)
			return row;
	return -1;
}

int RaidWindow::FindNotInGroup(const std::string &name) const
{
	SidlWindow::Control *list = List(Window(), OtherList);
	if (!list)
		return -1;
	for (int row = 0; row < static_cast<int>(list->rows.size()); ++row)
		if (GetText(*list, row, 1) == name)
			return row;
	return -1;
}

uint32_t RaidWindow::ClassColor(int class_id) const
{
	for (int i = 0; i < Classes; ++i)
		if (ColorClass[i] == class_id)
			return m_class_colors[i];
	return 0;
}

void RaidWindow::AddPlayer(const std::string &rank, const std::string &name, const std::string &class_title,
	const std::string &level, uint32_t group, int class_id, bool group_leader)
{
	SidlWindow *wnd = Window();
	if (!wnd)
		return;
	auto fill = [&](SidlWindow::Control &list, int row) {
		SetText(list, row, 1, name);
		SetColor(list, row, 1, White);
		SetText(list, row, 2, level);
		SetText(list, row, 3, class_title);
		SetText(list, row, 4, rank);
		if (uint32_t color = ClassColor(class_id)) // SetPlayerClassColor?
			SetColor(list, row, 3, color);
	};
	if (group == None) {
		SidlWindow::Control *other = List(wnd, OtherList);
		if (!other)
			return;
		fill(*other, AddRow(*other));
		wnd->UpdateListScroll(*other);
		return;
	}
	SidlWindow::Control *list = List(wnd, GroupList);
	int row = FindEmptyRow(group);
	if (!list || row == -1)
		return; // "CRaidWnd::AddPlayertoList: no space"
	int first = static_cast<int>(group) * RowsPerGroup;
	if (group_leader && row != first) {
		// The leader into the group's first row, and whoever was there into the empty one.
		for (int column = 1; column <= 4; ++column)
			SetText(*list, row, column, GetText(*list, first, column));
		SetColor(*list, row, 1, White);
		SetColor(*list, row, 3, GetColor(*list, first, 3));
		row = first;
	}
	fill(*list, row);
}

void RaidWindow::Resort(int row, uint32_t group)
{
	SidlWindow::Control *list = List(Window(), GroupList);
	if (!list)
		return;
	for (; row < static_cast<int>(group) * RowsPerGroup + 5; ++row) {
		int next = row + 1;
		for (int column = 1; column <= 4; ++column)
			SetText(*list, row, column, GetText(*list, next, column));
		SetColor(*list, row, 1, GetColor(*list, next, 1));
		SetColor(*list, row, 3, GetColor(*list, next, 3));
		SetText(*list, next, 1, Empty);
		SetColor(*list, next, 1, Gray);
		for (int column = 2; column <= 4; ++column)
			SetText(*list, next, column, "");
	}
}

void RaidWindow::RemovePlayer(const std::string &name, uint32_t group)
{
	SidlWindow *wnd = Window();
	if (!wnd)
		return;
	if (group == None) {
		SidlWindow::Control *other = List(wnd, OtherList);
		int row = FindNotInGroup(name);
		if (!other || row == -1)
			return;
		other->rows.erase(other->rows.begin() + row);
		if (row < static_cast<int>(other->cell_colors.size()))
			other->cell_colors.erase(other->cell_colors.begin() + row);
		if (row < static_cast<int>(other->separators.size()))
			other->separators.erase(other->separators.begin() + row);
		if (other->selected_row >= static_cast<int>(other->rows.size()))
			other->selected_row = -1;
		wnd->UpdateListScroll(*other);
		return;
	}
	SidlWindow::Control *list = List(wnd, GroupList);
	int row = FindInGroup(name, group);
	if (!list || row == -1)
		return;
	SetText(*list, row, 1, Empty);
	SetColor(*list, row, 1, Gray);
	for (int column = 2; column <= 4; ++column)
		SetText(*list, row, column, "");
	Resort(row, group);
}

void RaidWindow::ChangePosition(const std::string &name, uint32_t from, uint32_t to, int class_id, bool group_leader)
{
	SidlWindow::Control *list = List(Window(), from == None ? OtherList : GroupList);
	int row = from == None ? FindNotInGroup(name) : FindInGroup(name, from);
	if (!list || row == -1)
		return; // "CRaidWnd::ChangePosition - player not found"
	std::string moved = GetText(*list, row, 1), level = GetText(*list, row, 2);
	std::string title = GetText(*list, row, 3), rank = GetText(*list, row, 4);
	RemovePlayer(moved, from);
	AddPlayer(rank, moved, title, level, to, class_id, group_leader);
}

void RaidWindow::ChangeGroupLeader(const std::string &name, uint32_t group, const std::string *previous)
{
	SidlWindow::Control *list = List(Window(), GroupList);
	if (!list)
		return;
	int row = FindInGroup(name, group);
	int other = previous ? FindInGroup(*previous, group) : -1;
	if (row == -1)
		return; // "CRaidWnd::ChangeRaidGroupLeader - ..."
	if (other == -1) {
		other = static_cast<int>(group) * RowsPerGroup;
		if (other == row)
			return;
	}
	// The two rows' text and class colors trade places (the name's color stays with the row).
	for (int column = 1; column <= 4; ++column) {
		std::string a = GetText(*list, row, column);
		SetText(*list, row, column, GetText(*list, other, column));
		SetText(*list, other, column, a);
	}
	uint32_t a = GetColor(*list, row, 3);
	SetColor(*list, row, 3, GetColor(*list, other, 3));
	SetColor(*list, other, 3, a);
}

void RaidWindow::SetRank(const std::string &name, const std::string &rank, uint32_t group)
{
	SidlWindow::Control *list = List(Window(), group == None ? OtherList : GroupList);
	int row = group == None ? FindNotInGroup(name) : FindInGroup(name, group);
	if (list && row != -1)
		SetText(*list, row, 4, rank);
}

void RaidWindow::Rename(const std::string &name, const std::string &old, uint32_t group)
{
	SidlWindow::Control *list = List(Window(), group == None ? OtherList : GroupList);
	int row = group == None ? FindNotInGroup(old) : FindInGroup(old, group);
	if (list && row != -1)
		SetText(*list, row, 1, name);
}

void RaidWindow::Highlight(const std::string &name, uint32_t group, const std::string &old, uint32_t old_group)
{
	auto paint = [&](const std::string &who, uint32_t in, uint32_t color) {
		if (who.empty())
			return;
		SidlWindow::Control *list = List(Window(), in == None ? OtherList : GroupList);
		int row = in == None ? FindNotInGroup(who) : FindInGroup(who, in);
		if (list && row != -1)
			SetColor(*list, row, 1, color);
	};
	paint(old, old_group, White);
	paint(name, group, Red);
}

void RaidWindow::SetRaidCount(int count)
{
	SetLabel(Window(), "RAID_PlayerCountLabel", std::to_string(count));
}

void RaidWindow::SetLevelAverage(int level)
{
	SetLabel(Window(), "RAID_LevelAverageLabel", std::to_string(level));
}

void RaidWindow::SetPlayerClassColor(const std::string &name, int class_id, uint32_t group)
{
	SidlWindow::Control *list = List(Window(), group == None ? OtherList : GroupList);
	int row = group == None ? FindNotInGroup(name) : FindInGroup(name, group);
	uint32_t color = ClassColor(class_id);
	if (list && row != -1 && color)
		SetColor(*list, row, 3, color);
}

void RaidWindow::ClearLooterList()
{
	if (SidlWindow::Control *list = List(Options(), LooterList)) {
		Clear(*list);
		Options()->UpdateListScroll(*list);
	}
}

void RaidWindow::AddLooterToList(const std::string &name)
{
	if (SidlWindow::Control *list = List(Options(), LooterList)) {
		int row = static_cast<int>(list->rows.size());
		list->rows.push_back({name});
		list->cell_colors.resize(row + 1);
		Options()->UpdateListScroll(*list);
	}
}
