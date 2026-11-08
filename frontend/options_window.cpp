#include "options_window.h"

#include "chat_text.h"
#include "window_manager.h"

#include <algorithm>

namespace
{
	const char *const Screen = "OptionsWindow";
	const char *const Tabs = "OPTW_OptionsSubwindows";
	const char *const KeyboardPage = "OptionsKeyboardPage";
	const char *const FilterCombo = "OKP_KeyboardFilterCombobox";
	const char *const AssignmentList = "OKP_KeyboardAssignmentList";
	const char *const DefaultsButton = "OKP_DefaultKeysButton";
	const char *const ColorPage = "OptionsColorPage";
	const char *const ColorDefaultsButton = "UserColorDefault";
	constexpr int UserColorButtons = 72; // ColorPageWndNotification's 0x48
	const char *const Dialog = "ConfirmationDialogBox";
	constexpr uint64_t ConfirmMs = 20000; // CConfirmationDialog::Activate(0x1c, 20000)

	// Ours, for now: a command my-client doesn't do yet is listed in red.
	constexpr uint32_t NotImplementedColor = 0xff0000;
}

void OptionsWindow::Attach(WindowManager &windows, KeyBindings &keys, Host host)
{
	m_windows = &windows;
	m_keys = &keys;
	m_host = std::move(host);
	m_row = m_column = -1;
	m_confirming = false;
	SidlWindow *wnd = windows.Find(Screen);
	if (!wnd)
		return;
	// Every page's pieces hidden but the keyboard and color pages' (ours, until the others do
	// something).
	if (SidlWindow::Control *tabs = wnd->Find(Tabs))
		for (int page : tabs->children) {
			SidlWindow::Control &p = wnd->Item(page);
			if (p.def->screen_id != KeyboardPage && p.def->screen_id != ColorPage)
				for (int piece : p.children)
					wnd->Item(piece).visible = false;
		}
	if (SidlWindow::Control *list = wnd->Find(AssignmentList))
		list->header_color = 0x8aa3ff; // COptionsWnd::COptionsWnd: the list's +0x14c, 0xff8aa3ff
	// UpdateKeyboardAssignmentList: the first category.
	if (SidlWindow::Control *combo = wnd->Find(FilterCombo))
		combo->choice = 0;
	Fill();
	m_confirming_colors = false;
	m_color_page_shown = false;
	UpdateUserColors(); // COptionsWnd::COptionsWnd
}

void OptionsWindow::UpdateUserColors()
{
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	if (!wnd || !m_host.user_color)
		return;
	for (int i = 0; i < UserColorButtons; ++i)
		if (SidlWindow::Control *button = wnd->Find("UserColor" + std::to_string(i))) {
			uint32_t color = m_host.user_color(i) & 0xffffff;
			button->text_color = color == 0 ? 0xffffff : color;
		}
}

void OptionsWindow::Fill()
{
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	SidlWindow::Control *list = wnd ? wnd->Find(AssignmentList) : nullptr;
	if (!list)
		return;
	const SidlWindow::Control *combo = wnd->Find(FilterCombo);
	uint32_t mask = KeyMap::CategoryMask(combo ? combo->choice : 8);
	list->rows.clear();
	list->row_colors.clear();
	m_row_ids.clear();
	for (const KeyMap::Command &command : KeyMap::List()) {
		if (!(command.categories & mask))
			continue;
		list->rows.push_back({m_host.eqstr && command.eqstr ? m_host.eqstr(command.eqstr, command.name) : command.name,
			KeyMap::Describe(m_keys->Get(command.id, false)), KeyMap::Describe(m_keys->Get(command.id, true))});
		list->row_colors.push_back(command.implemented ? 0 : NotImplementedColor);
		m_row_ids.push_back(command.id);
	}
	list->selected_row = list->selected_column = -1;
	list->scroll_pos = 0;
	wnd->UpdateListScroll(*list);
}

void OptionsWindow::SetCell(int row, int column, const std::string &text)
{
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	SidlWindow::Control *list = wnd ? wnd->Find(AssignmentList) : nullptr;
	if (list && row >= 0 && row < static_cast<int>(list->rows.size()) && column < static_cast<int>(list->rows[row].size()))
		list->rows[row][column] = text;
}

void OptionsWindow::Cancel()
{
	if (m_row < 0)
		return;
	if (m_row < static_cast<int>(m_row_ids.size()))
		SetCell(m_row, m_column, KeyMap::Describe(m_keys->Get(m_row_ids[m_row], m_column == 2)));
	m_row = m_column = -1;
}

bool OptionsWindow::Click(const std::string &window, const std::string &control, uint64_t now_ms)
{
	if (!m_windows)
		return false;
	if (window == Dialog && m_confirming) {
		// ProcessYes's 0x1c: ResetKeysToDefault; 0x21: RestoreDefaultColors.
		if (control == "Yes_Button" && m_confirming_colors) {
			if (m_host.save_color)
				for (int i = 0; i < UserColorButtons; ++i)
					m_host.save_color(i, ChatColors::Default(i));
		}
		else if (control == "Yes_Button") {
			m_keys->Defaults();
			Fill();
			if (m_host.reset)
				m_host.reset();
		}
		if (control == "Yes_Button" || control == "No_Button" || control == "OK_Button") {
			m_confirming = m_confirming_colors = false;
			m_windows->Show(Dialog, false);
		}
		return true;
	}
	if (window != Screen)
		return false;
	SidlWindow *wnd = m_windows->Find(Screen);
	if (control == FilterCombo) {
		Cancel();
		Fill();
		return true;
	}
	if (control == AssignmentList) {
		const SidlWindow::Control *list = wnd->Find(AssignmentList);
		Cancel();
		// The key's or the alternate's cell: "Press a key" there until one comes (eqstr 6598). The
		// command's own does nothing.
		if (list && (list->selected_column == 1 || list->selected_column == 2) && list->selected_row >= 0 &&
		    list->selected_row < static_cast<int>(m_row_ids.size())) {
			m_row = list->selected_row;
			m_column = list->selected_column;
			SetCell(m_row, m_column, m_host.eqstr ? m_host.eqstr(6598, "Press a key") : "Press a key");
		}
		return true;
	}
	if (control == DefaultsButton) {
		// CConfirmationDialog::Activate(0x1c, 20000) with eqstr 6602.
		if (!m_windows->Shown(Dialog)) {
			m_confirming_colors = false;
			ShowConfirmation(now_ms);
		}
		return true;
	}
	// The Colors page: a color's button opens the picker with its color, and what's accepted is
	// saved; Reset Default Colors asks first (0x21, 20000, eqstr 6602).
	if (control == ColorDefaultsButton) {
		if (!m_windows->Shown(Dialog)) {
			m_confirming_colors = true;
			ShowConfirmation(now_ms);
		}
		return true;
	}
	if (control.rfind("UserColor", 0) == 0) {
		int index = -1;
		const std::string digits = control.substr(9);
		if (!digits.empty() && std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; }))
			index = std::stoi(digits);
		const SidlWindow::Control *button = index >= 0 && index < UserColorButtons ? wnd->Find(control) : nullptr;
		if (!button || !m_host.pick_color)
			return true;
		m_host.pick_color(button->text_color, [this, index](uint32_t rgb) {
			if (m_host.save_color)
				m_host.save_color(index, rgb);
		});
		return true;
	}
	return false;
}

void OptionsWindow::ShowConfirmation(uint64_t now_ms)
{
	SidlWindow *dialog = m_windows->Find(Dialog);
	if (!dialog)
		return;
	for (const char *id : {"Yes_Button", "No_Button"})
		if (SidlWindow::Control *button = dialog->Find(id))
			button->visible = true;
	if (SidlWindow::Control *ok = dialog->Find("OK_Button"))
		ok->visible = false;
	if (SidlWindow::Control *text = dialog->Find("TextOutput")) {
		text->text = m_host.eqstr ? m_host.eqstr(6602, "Are you sure you want to lose your current settings?")
		                          : "Are you sure you want to lose your current settings?";
		text->scroll_pos = 0;
	}
	const Rect &r = dialog->Bounds();
	m_windows->MoveTo(Dialog, (m_windows->ScreenWidth() - r.w) / 2, (m_windows->ScreenHeight() - r.h) / 2);
	m_windows->Show(Dialog, true);
	m_windows->Raise(Dialog);
	m_confirming = true;
	m_confirm_until_ms = now_ms + ConfirmMs;
}

bool OptionsWindow::Key(int scan, bool shift, bool ctrl, bool alt)
{
	if (m_row < 0 || m_row >= static_cast<int>(m_row_ids.size()))
		return false;
	int id = m_row_ids[m_row];
	if (id > KeyMap::Assignable && id < KeyMap::OwnFirst) {
		Cancel();
		return true;
	}
	bool alternate = m_column == 2;
	uint32_t key;
	if (!KeyMap::IsValidKey(scan) && scan != 0x01) {
		// CEverQuest_IsValidKeyMap: eqstr 12092, and the key stays as it was.
		if (m_host.chat)
			m_host.chat(m_host.eqstr ? m_host.eqstr(12092, "That Key is used by EverQuest and cannot be assigned by the "
			                                               "user to a user-defined keymap.")
			                         : "That Key is used by EverQuest and cannot be assigned by the user to a "
			                           "user-defined keymap.",
				ChatType::Yellow);
		key = m_keys->Get(id, alternate);
	}
	else {
		// Shift, Ctrl and Alt alone wait for the key they go with; Escape unbinds.
		if (KeyMap::IsModifierKey(scan))
			return true;
		if (scan == 0x01)
			key = KeyMap::NoKey;
		else
			key = static_cast<uint32_t>(scan) | (ctrl ? KeyMap::Ctrl : 0) | (shift ? KeyMap::Shift : 0) |
			      (alt ? KeyMap::Alt : 0);
	}
	m_keys->Set(id, alternate, key);
	SetCell(m_row, m_column, KeyMap::Describe(key));
	if (m_host.saved)
		m_host.saved(id, alternate, key);
	m_row = m_column = -1;
	return true;
}

bool OptionsWindow::KeyboardPageShowing() const
{
	const SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	if (!wnd || !m_windows->Shown(Screen))
		return false;
	const SidlWindow::Control *tabs = wnd->Find(Tabs);
	const SidlWindow::Control *page = wnd->Find(KeyboardPage);
	return tabs && page && page->visible;
}

void OptionsWindow::Frame(uint64_t now_ms)
{
	if (!m_windows)
		return;
	if (m_row >= 0 && !KeyboardPageShowing())
		Cancel();
	// ProcessTabPageSelection's ProcessUserDefinedColorTab: the buttons' colors as the page is
	// picked (not as the window comes back up on it).
	const SidlWindow *options = m_windows->Find(Screen);
	const SidlWindow::Control *color_page = options ? options->Find(ColorPage) : nullptr;
	bool colors = color_page && color_page->visible;
	if (colors && !m_color_page_shown)
		UpdateUserColors();
	m_color_page_shown = colors;
	if (SidlWindow *wnd = m_windows->Find(Screen); wnd && !m_windows->Shown(Screen))
		wnd->CloseCombos();
	if (m_confirming && (now_ms >= m_confirm_until_ms || !m_windows->Shown(Dialog))) {
		m_confirming = m_confirming_colors = false; // ProcessNo, as the time runs out
		m_windows->Show(Dialog, false);
	}
}
