#include "aa_window.h"

#include "../core/eqstr.h"
#include "game_values.h"
#include "window_manager.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace
{
	constexpr uint32_t White = 0xffffff, Gray = 0xc0c0c0;

	std::string ListId(int page) { return "List" + std::to_string(page + 1); }

	std::string Text(const EqStr *eqstr, int32_t id, const char *fallback = "")
	{
		return eqstr && id >= 0 && eqstr->Has(static_cast<uint32_t>(id)) ? eqstr->Format(static_cast<uint32_t>(id), {})
		                                                                   : std::string(fallback);
	}

	void SetLabel(SidlWindow &wnd, const char *id, const std::string &text)
	{
		if (SidlWindow::Control *label = wnd.Find(id))
			label->text = text;
	}

	void Enable(SidlWindow &wnd, const char *id, bool enabled)
	{
		if (SidlWindow::Control *button = wnd.Find(id))
			button->enabled = enabled;
	}

	// CListWnd::SetItemText and SetItemColor, the list made to fit.
	void SetCell(SidlWindow::Control &list, int row, int column, const std::string *text, uint32_t color)
	{
		if (row < 0)
			return;
		if (list.rows.size() <= static_cast<size_t>(row))
			list.rows.resize(row + 1);
		if (list.rows[row].size() < 3)
			list.rows[row].resize(3);
		if (list.cell_colors.size() <= static_cast<size_t>(row))
			list.cell_colors.resize(row + 1);
		if (list.cell_colors[row].size() < 3)
			list.cell_colors[row].resize(3);
		if (text)
			list.rows[row][column] = *text;
		else
			list.cell_colors[row][column] = color;
	}
}

void AAWindow::Attach(WindowManager &windows, const GameValues &values, Host host)
{
	m_windows = &windows;
	m_values = &values;
	m_host = std::move(host);
	m_inited = false;
	m_page = 0;
	m_selected = -1;
	m_timer_ms = 0;
	SidlWindow *wnd = Window();
	if (!wnd)
		return;
	Enable(*wnd, "TrainButton", false);
	Enable(*wnd, "HotButton", false);
	for (int page = 0; page < Pages; ++page)
		if (SidlWindow::Control *list = wnd->Find(ListId(page))) {
			list->row_color = White;
			list->hover_color = 0xffff00;
			list->select_color = 0x808080;
			list->justification = {0, 1, 1};
			list->header_color = 0x8aa3ff;
		}
	if (m_values->zone && m_values->zone->Profile().class_id != 0)
		Init();
}

SidlWindow *AAWindow::Window() const
{
	return m_windows ? m_windows->Find(Screen) : nullptr;
}

bool AAWindow::Shown() const
{
	return m_windows && m_windows->Shown(Screen);
}

AltAdv::Character AAWindow::Character() const
{
	AltAdv::Character ch;
	if (!m_values || !m_values->zone)
		return ch;
	const ZoneProfile &me = m_values->zone->Profile();
	ch.class_id = me.class_id;
	ch.level = me.level;
	ch.expansions = me.expansions;
	ch.unspent = me.aa_unspent;
	ch.ranks = me.aa_ranks;
	return ch;
}

int AAWindow::SelectedId() const
{
	if (m_selected < 0 || m_selected >= static_cast<int>(m_pages[m_page].size()))
		return -1;
	return m_pages[m_page][m_selected].id;
}

void AAWindow::Init()
{
	SidlWindow *wnd = Window();
	if (!wnd || !m_values->zone)
		return;
	m_inited = true;
	int class_id = m_values->zone->Profile().class_id;
	for (auto &page : m_pages)
		page.clear();
	for (int id = 0; id < AltAdv::Count; ++id) {
		const AltAdv::Ability &a = AltAdv::Abilities[id];
		if (AltAdv::ClassHas(class_id, id) && a.enabled && a.category >= AltAdv::General && a.category <= AltAdv::PopAbility)
			m_pages[a.category - 1].push_back({id, 0});
	}
	// Each list emptied (DeleteAll) and a row a name (AddString, its row kept).
	for (int page = 0; page < Pages; ++page) {
		SidlWindow::Control *list = wnd->Find(ListId(page));
		if (!list)
			continue;
		list->rows.clear();
		list->cell_colors.clear();
		list->separators.clear();
		list->selected_row = -1;
		for (Entry &entry : m_pages[page]) {
			entry.row = static_cast<int>(list->rows.size());
			std::string name = Text(m_values->eqstr, AltAdv::Abilities[entry.id].name);
			SetCell(*list, entry.row, 0, &name, 0);
		}
		wnd->UpdateListScroll(*list);
	}
	m_shown_unspent = m_shown_total = m_shown_percent = -1;
}

void AAWindow::Activate()
{
	SidlWindow *wnd = Window();
	if (!wnd)
		return;
	if (!m_inited)
		Init();
	Update();
	m_timer_ms = m_now_ms;
	m_windows->Show(Screen, true);
	m_windows->Raise(Screen);
}

void AAWindow::Toggle()
{
	if (!Window())
		return;
	if (Shown())
		m_windows->Show(Screen, false);
	else
		Activate();
}

void AAWindow::Update()
{
	SidlWindow *wnd = Window();
	if (!wnd || !m_values->zone)
		return;
	const ZoneProfile &me = m_values->zone->Profile();
	AltAdv::Character ch = Character();
	int percent = static_cast<uint8_t>(me.aa_percentage);
	char line[32];
	if (percent != m_shown_percent) {
		m_shown_percent = percent;
		snprintf(line, sizeof(line), "%d%%", percent);
		SetLabel(*wnd, "ExpCount", line);
	}
	// The world's Luclin and Planes of Power bits (DAT_007cf1f0, DAT_007cf1f4).
	uint32_t world = m_host.world_expansions ? m_host.world_expansions() : 0;
	bool expansion = (world & 0xc) != 0;
	Enable(*wnd, "LessExpButton", percent != 0 && me.level >= 51 && expansion);
	Enable(*wnd, "MoreExpButton", percent < 100 && me.level > 50 && expansion);
	int total = 0;
	for (int page = 0; page < Pages; ++page) {
		SidlWindow::Control *list = wnd->Find(ListId(page));
		if (!list)
			continue;
		for (const Entry &entry : m_pages[page]) {
			int rank = me.aa_ranks[entry.id];
			const AltAdv::Ability &a = AltAdv::Abilities[entry.id];
			snprintf(line, sizeof(line), "%d/%d", rank, a.max_rank);
			std::string text = line;
			SetCell(*list, entry.row, 1, &text, 0);
			int cost = AltAdv::PointCost(rank, entry.id);
			text = rank < a.max_rank ? std::to_string(cost) : "";
			SetCell(*list, entry.row, 2, &text, 0);
			total += AltAdv::TotalPoints(ch, rank, entry.id);
			uint32_t color = AltAdv::CanTrain(ch, entry.id) ? White : Gray;
			for (int column = 0; column < 3; ++column)
				SetCell(*list, entry.row, column, nullptr, color);
		}
	}
	if (me.aa_unspent != m_shown_unspent) {
		m_shown_unspent = me.aa_unspent;
		SetLabel(*wnd, "CurrentCount", std::to_string(m_shown_unspent));
	}
	if (total != m_shown_total) {
		m_shown_total = total;
		SetLabel(*wnd, "TotalCount", std::to_string(total));
	}
	UpdateSelected();
}

void AAWindow::UpdateSelected()
{
	SidlWindow *wnd = Window();
	if (!wnd)
		return;
	const EqStr *eqstr = m_values->eqstr;
	SidlWindow::Control *description = wnd->Find("Description");
	int id = SelectedId();
	if (id < 0) {
		if (description)
			description->text = Text(eqstr, 3108);
		Enable(*wnd, "TrainButton", false);
		Enable(*wnd, "HotButton", false);
		return;
	}
	const AltAdv::Ability &a = AltAdv::Abilities[id];
	AltAdv::Character ch = Character();
	Enable(*wnd, "TrainButton", AltAdv::CanTrain(ch, id));
	std::string text;
	char line[64];
	if (m_values->zone->Profile().gm) {
		snprintf(line, sizeof(line), "(ID#%d) ", id);
		text += line;
	}
	UpdateTimer();
	if (a.reuse == 0) {
		Enable(*wnd, "HotButton", false);
		text += Text(eqstr, 3110);
		SetLabel(*wnd, "Timer", "");
	}
	else {
		text += Text(eqstr, 3109);
		snprintf(line, sizeof(line), " %u:%02u:%02u", a.reuse / 3600, a.reuse / 60 % 60, a.reuse % 60);
		text += line;
	}
	text += "<BR>";
	text += AltAdv::Requirements(ch, id, eqstr);
	text += "<BR>";
	text += Text(eqstr, a.description);
	if (description)
		description->text = text;
}

void AAWindow::UpdateTimer()
{
	SidlWindow *wnd = Window();
	int id = SelectedId();
	if (!wnd || id < 0)
		return;
	AltAdv::Character ch = Character();
	if (m_values->zone->Profile().aa_ranks[id] == 0) {
		SetLabel(*wnd, "Timer", "");
		return;
	}
	Enable(*wnd, "HotButton", true);
	int64_t remaining = 0;
	AltAdv::Ready(ch, id, m_values->zone->AltAdvLastUsed(id), static_cast<int64_t>(std::time(nullptr)), &remaining);
	if (remaining < 1) {
		SetLabel(*wnd, "Timer", Text(m_values->eqstr, 3111, "Ready"));
		return;
	}
	char line[32];
	snprintf(line, sizeof(line), "%02d:%02d:%02d", static_cast<int>(remaining / 3600), static_cast<int>(remaining / 60 % 60),
		static_cast<int>(remaining % 60));
	SetLabel(*wnd, "Timer", line);
}

void AAWindow::Frame(uint64_t now_ms)
{
	m_now_ms = now_ms;
	if (!Shown())
		return;
	int id = SelectedId();
	if (id < 0 || AltAdv::Abilities[id].reuse == 0 || m_values->zone->Profile().aa_ranks[id] == 0)
		return;
	if (m_timer_ms + 500 < now_ms) {
		UpdateTimer();
		m_timer_ms = now_ms;
	}
}

bool AAWindow::Click(const std::string &window, const std::string &control)
{
	if (window != Screen)
		return false;
	SidlWindow *wnd = Window();
	if (!wnd || !m_values->zone)
		return true;
	int percent = static_cast<uint8_t>(m_values->zone->Profile().aa_percentage);
	if (control == "DoneButton") {
		m_windows->Show(Screen, false);
		return true;
	}
	// CAAWnd_WndNotification_AAAction: "on <percent>", or "off" at 0.
	auto send_percent = [&](int value) {
		if (m_host.set_percentage)
			m_host.set_percentage(value);
		if (m_host.send)
			m_host.send(value == 0 ? std::string("off") : "on " + std::to_string(value));
	};
	if (control == "LessExpButton") {
		if (percent != 0)
			send_percent(percent - 10);
		Update();
		return true;
	}
	if (control == "MoreExpButton") {
		if (percent < 100)
			send_percent(percent + 10);
		Update();
		return true;
	}
	if (control == "TrainButton") {
		int id = SelectedId();
		if (id < 0)
			return true;
		if (m_host.send)
			m_host.send("buy " + std::to_string(id));
		Enable(*wnd, "TrainButton", false);
		return true;
	}
	if (control == "HotButton") {
		int id = SelectedId();
		if (id >= 0 && m_host.hotkey)
			m_host.hotkey(id);
		return true;
	}
	// A row clicked: the page's ability with that row (GetCurSel).
	for (int page = 0; page < Pages; ++page)
		if (control == ListId(page)) {
			const SidlWindow::Control *list = wnd->Find(control);
			int row = list ? list->selected_row : -1;
			const std::vector<Entry> &entries = m_pages[m_page];
			for (int i = 0; i < static_cast<int>(entries.size()); ++i)
				if (entries[i].row == row) {
					m_selected = i;
					UpdateSelected();
					break;
				}
			return true;
		}
	// Another page: the list that was showing loses its selection, nothing's selected.
	if (control == "Subwindows") {
		const SidlWindow::Control *tabs = wnd->Find(control);
		if (!tabs)
			return true;
		if (SidlWindow::Control *list = wnd->Find(ListId(m_page)))
			list->selected_row = -1;
		m_selected = -1;
		m_page = std::clamp(tabs->tab, 0, Pages - 1);
		UpdateSelected();
		Update();
		return true;
	}
	return true;
}

void AAWindow::ShowAbility(int id)
{
	if (!Window())
		return;
	m_windows->Raise(Screen);
	int found_page = -1, found_index = -1;
	for (int page = 0; page < Pages; ++page)
		for (int i = 0; i < static_cast<int>(m_pages[page].size()); ++i)
			if (m_pages[page][i].id == id) {
				found_page = page;
				found_index = i;
			}
	if (found_page < 0)
		return;
	// SetCurSel on the list of the page that was showing (its row by the index), then the ability's
	// page and index: the tab doesn't change.
	if (SidlWindow::Control *list = Window()->Find(ListId(m_page)))
		list->selected_row = found_index;
	m_page = found_page;
	m_selected = found_index;
	UpdateSelected();
	Update();
}
