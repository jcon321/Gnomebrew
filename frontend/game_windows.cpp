#include "game_windows.h"
#include "game_values.h"
#include "ini_file.h"
#include "window_manager.h"
#include "zone_session.h"

#include <cstdlib>
#include <iterator>
#include <string>

namespace
{
	constexpr int BuffSlots = 15;    // CBuffWindow's buttons, Buff0..Buff14
	constexpr int GroupMembers = 5;  // the others in a group
	constexpr int Pages = 10;        // of socials, and of hot buttons
	constexpr int SocialsPerPage = 12;

	// The Actions window's buttons that go on hot buttons, in eqgame's order for each page
	// (CActionsWnd's button arrays). The main page's pairs share a place, one shown at a time.
	const char *const MainButtons[][2] = {
		{"AMP_WhoButton", nullptr}, {"AMP_InviteButton", "AMP_FollowButton"}, {"AMP_DisbandButton", nullptr},
		{"AMP_CampButton", nullptr}, {"AMP_SitButton", "AMP_StandButton"}, {"AMP_RunButton", "AMP_WalkButton"},
	};
	const char *const CombatButtons[] = {
		"ACP_MeleeAttackButton", "ACP_RangeAttackButton", "ACP_FirstAbilityButton",
		"ACP_SecondAbilityButton", "ACP_ThirdAbilityButton", "ACP_FourthAbilityButton",
	};
	const char *const AbilityButtons[] = {
		"AAP_FirstAbilityButton", "AAP_SecondAbilityButton", "AAP_ThirdAbilityButton",
		"AAP_FourthAbilityButton", "AAP_FifthAbilityButton", "AAP_SixthAbilityButton",
	};

	void Show(SidlWindow &wnd, const std::string &screen_id, bool shown)
	{
		if (SidlWindow::Control *control = wnd.Find(screen_id))
			control->visible = shown;
	}

	void PlayerWindow(SidlWindow &wnd, const GameValues &values)
	{
		Show(wnd, "PetHP", values.Pet() != nullptr);
	}

	void GroupWindow(SidlWindow &wnd, const GameValues &values)
	{
		const bool invited = false, in_group = false;
		Show(wnd, "FollowButton", invited);
		Show(wnd, "DeclineButton", invited);
		Show(wnd, "InviteButton", !invited);
		Show(wnd, "DisbandButton", !invited);
		if (SidlWindow::Control *disband = wnd.Find("DisbandButton"))
			disband->enabled = in_group;
		if (SidlWindow::Control *invite = wnd.Find("InviteButton")) {
			const ZoneSpawn *target = values.Target();
			invite->enabled = target && target->npc == 0 && target->id != values.zone->MySpawnId();
		}
		for (int i = 1; i <= GroupMembers; ++i)
			Show(wnd, "PetGauge" + std::to_string(i), false);
	}

	void BuffWindow(SidlWindow &wnd)
	{
		const SidlWindow::Control *first = wnd.Find("Buff0");
		if (!first)
			return;
		// A button a pixel apart from the next, down each column, the columns leftward from the right.
		int w = first->def->rect.w, h = first->def->rect.h;
		const Rect &c = wnd.Root().client;
		int columns = c.w / (w + 1), rows = c.h / (h + 1);
		for (int i = 0; i < BuffSlots; ++i) {
			std::string id = "Buff" + std::to_string(i);
			SidlWindow::Control *button = wnd.Find(id);
			if (!button)
				continue;
			int column = rows > 0 ? i / rows : columns;
			button->visible = column < columns;
			button->blank = true;
			if (button->visible)
				wnd.Move(id, {c.Right() - (w + 1) * (column + 1), c.y + (h + 1) * (i % rows), w, h});
		}
	}

	void ActionsWindow(SidlWindow &wnd, const GameValues &values)
	{
		const bool invited = false, in_group = false;
		Show(wnd, "AMP_InviteButton", !invited);
		Show(wnd, "AMP_FollowButton", invited);
		auto me = values.zone->Spawns().find(values.zone->MySpawnId());
		bool sitting = me != values.zone->Spawns().end() && me->second.stance == 110;
		Show(wnd, "AMP_SitButton", !sitting);
		Show(wnd, "AMP_StandButton", sitting);
		Show(wnd, "AMP_RunButton", !values.running);
		Show(wnd, "AMP_WalkButton", values.running);
		if (SidlWindow::Control *disband = wnd.Find("AMP_DisbandButton"))
			disband->enabled = in_group || invited;
		if (SidlWindow::Control *invite = wnd.Find("AMP_InviteButton")) {
			const ZoneSpawn *target = values.Target();
			invite->enabled = target && target->npc == 0 && target->id != values.zone->MySpawnId();
		}
	}

	// CXStr's number constructor as the page labels use it: the page from 1, padded to two places.
	std::string PageLabel(int page)
	{
		std::string label = std::to_string(page + 1);
		return label.size() < 2 ? " " + label : label;
	}

	// The arrows' page, one on or back, round the ends.
	bool StepPage(int &page, const std::string &control, const char *left, const char *right)
	{
		if (control == right)
			page = (page + 1) % Pages;
		else if (control == left)
			page = (page + Pages - 1) % Pages;
		else
			return false;
		return true;
	}

	void SetText(SidlWindow &wnd, const std::string &screen_id, const std::string &text)
	{
		if (SidlWindow::Control *control = wnd.Find(screen_id))
			control->text = text;
	}

	// CActionsWnd::GetActionButton (@00403044): of a main page pair, the one showing.
	std::string ActionButton(WindowManager &windows, int index)
	{
		if (index < 0 || index >= static_cast<int>(std::size(MainButtons)))
			return "";
		const char *const (&pair)[2] = MainButtons[index];
		if (pair[1]) {
			SidlWindow *actions = windows.Find("ActionsWindow");
			const SidlWindow::Control *first = actions ? actions->Find(pair[0]) : nullptr;
			if (first && !first->visible)
				return pair[1];
		}
		return pair[0];
	}

	// The Actions window button a hot button holds, as CHotButtonWnd::OnProcessFrame and
	// CCursorAttachment::Activate find it; "" for one that isn't there.
	std::string SourceButton(WindowManager &windows, const HotButton &button)
	{
		int v = button.value;
		switch (button.type) {
		case HotButton::Combat: return v >= 0 && v < 2 ? CombatButtons[v] : "";
		case HotButton::CombatSkill: return v >= 0 && v < 4 ? CombatButtons[v + 2] : "";
		case HotButton::Ability: return v >= 0 && v < 6 ? AbilityButtons[v] : "";
		case HotButton::Action: return ActionButton(windows, v);
		default: return "";
		}
	}

	// The text on a hot button: its button's (GetWindowText), or for the menu buttons and socials,
	// their names (we don't have the socials yet, so they have none).
	std::string HotButtonText(WindowManager &windows, const HotButton &button)
	{
		if (button.type == HotButton::Action && button.value >= static_cast<int>(std::size(MainButtons)))
			return button.value == 10 ? "Spells" : "Menu Button";
		std::string id = SourceButton(windows, button);
		SidlWindow *actions = windows.Find("ActionsWindow");
		const SidlWindow::Control *source = actions && !id.empty() ? actions->Find(id) : nullptr;
		return source ? source->text : "";
	}

	// What holding one of the Actions window's buttons puts on the cursor (CCursorAttachment's
	// types 4..7, kept here as the hot buttons they become); Empty for the rest.
	HotButton ActionsHotButton(const GameWindowState &state, const std::string &control)
	{
		for (int i = 0; i < static_cast<int>(std::size(MainButtons)); ++i)
			if (control == MainButtons[i][0] || (MainButtons[i][1] && control == MainButtons[i][1]))
				return {HotButton::Action, i};
		for (int i = 0; i < static_cast<int>(std::size(CombatButtons)); ++i)
			if (control == CombatButtons[i])
				return i < 2 ? HotButton{HotButton::Combat, i} : HotButton{HotButton::CombatSkill, i - 2};
		for (int i = 0; i < static_cast<int>(std::size(AbilityButtons)); ++i)
			if (control == AbilityButtons[i])
				return {HotButton::Ability, i};
		for (int i = 1; i <= SocialsPerPage; ++i)
			if (control == "ASP_SocialButton" + std::to_string(i))
				return {HotButton::Social, state.social_page * SocialsPerPage + i - 1};
		return {};
	}

	// HB_Button1..HB_Button10: 0..9; -1 for the window's other controls.
	int HotButtonSlot(const std::string &control)
	{
		for (int i = 1; i <= HotButtonsPerPage; ++i)
			if (control == "HB_Button" + std::to_string(i))
				return i - 1;
		return -1;
	}

	HotButton &PageButton(GameWindowState &state, int slot)
	{
		return state.hot_buttons[state.hot_button_page * HotButtonsPerPage + slot];
	}

	// CCursorAttachment::Activate: the button on the cursor, with its text as it is now.
	void Attach(WindowManager &windows, GameWindowState &state, const HotButton &button)
	{
		state.cursor = button;
		state.cursor_text = HotButtonText(windows, button);
	}

	void HotButtonWindow(SidlWindow &wnd, WindowManager &windows, const GameWindowState &state)
	{
		SetText(wnd, "HB_CurrentPageLabel", PageLabel(state.hot_button_page));
		// CHotButtonWnd::OnProcessFrame: a hot button with something on it shows its text, unchecked;
		// an empty one has no text and is checked, so it shows its Pressed picture.
		for (int i = 1; i <= HotButtonsPerPage; ++i) {
			std::string n = std::to_string(i);
			const HotButton &held = state.hot_buttons[state.hot_button_page * HotButtonsPerPage + i - 1];
			if (SidlWindow::Control *button = wnd.Find("HB_Button" + n)) {
				button->checked = held.type == HotButton::Empty;
				button->text = button->checked ? "" : HotButtonText(windows, held);
				button->text_color = 0xffffff;
			}
			Show(wnd, "HB_InvSlot" + n, false);
			Show(wnd, "HB_SpellGem" + n, false);
		}
	}
}

std::string GameWindowClick(WindowManager &windows, GameWindowState &state, const std::string &window, const std::string &control)
{
	// The socials' label keeps the XML's text until the page first changes.
	if (window == "ActionsWindow" && StepPage(state.social_page, control, "ASP_SocialPageLeftButton", "ASP_SocialPageRightButton")) {
		if (SidlWindow *wnd = windows.Find(window))
			SetText(*wnd, "ASP_CurrentSocialPageLabel", PageLabel(state.social_page));
	}
	if (window != "HotButtonWnd")
		return "";
	StepPage(state.hot_button_page, control, "HB_PageLeftButton", "HB_PageRightButton");
	int slot = HotButtonSlot(control);
	if (slot < 0)
		return "";
	HotButton &button = PageButton(state, slot);
	if (state.cursor.type == HotButton::Empty) {
		// DoHotButton: of what we can do so far, the main page's buttons.
		return button.type == HotButton::Action ? ActionButton(windows, button.value) : "";
	}
	// What's on the cursor goes on the button, and what was on it onto the cursor.
	HotButton was = button;
	button = state.cursor;
	state.ClearCursor();
	if (was.type != HotButton::Empty)
		Attach(windows, state, was);
	state.hot_buttons_changed = true;
	return "";
}

void GameWindowHold(WindowManager &windows, GameWindowState &state, const std::string &window, const std::string &control)
{
	if (window == "ActionsWindow") {
		HotButton button = ActionsHotButton(state, control);
		if (button.type != HotButton::Empty)
			Attach(windows, state, button);
	}
	else if (window == "HotButtonWnd" && state.cursor.type == HotButton::Empty) {
		int slot = HotButtonSlot(control);
		if (slot < 0)
			return;
		HotButton &button = PageButton(state, slot);
		if (button.type == HotButton::Empty)
			return;
		Attach(windows, state, button);
		button = {};
		state.hot_buttons_changed = true;
	}
}

void UpdateGameWindows(WindowManager &windows, const GameValues &values, const GameWindowState &state)
{
	if (!values.zone)
		return;
	if (SidlWindow *wnd = windows.Find("PlayerWindow"))
		PlayerWindow(*wnd, values);
	if (SidlWindow *wnd = windows.Find("GroupWindow"))
		GroupWindow(*wnd, values);
	if (SidlWindow *wnd = windows.Find("BuffWindow"))
		BuffWindow(*wnd);
	// The Actions window first: the hot buttons show its buttons as they are this frame.
	if (SidlWindow *wnd = windows.Find("ActionsWindow"))
		ActionsWindow(*wnd, values);
	if (SidlWindow *wnd = windows.Find("HotButtonWnd"))
		HotButtonWindow(*wnd, windows, state);
}

void DrawCursorAttachment(Canvas &canvas, TextRenderer &text, const WindowManager &windows, const GameWindowState &state,
	int x, int y, uint64_t now_ms)
{
	if (state.cursor.type == HotButton::Empty)
		return;
	// CCursorAttachment: its screen a pixel right of and below the mouse, CA_Anim in it showing
	// A_SquareBtnNormal, and the text over that in font 2, white (DrawCursorAttachmentBackground).
	Rect anim{5, 5, 40, 40};
	if (const SidlDefinitions *defs = windows.Definitions())
		if (const SidlPiece *piece = defs->Piece("CA_Anim"))
			anim = piece->rect;
	Rect dest{x + 1 + anim.x, y + 1 + anim.y, anim.w, anim.h};
	windows.DrawAnimation(canvas, "A_SquareBtnNormal", dest, now_ms);
	if (!state.cursor_text.empty())
		SidlDrawButtonText(text, canvas, dest, state.cursor_text, SidlFontHeight(2), 0xffffff, SidlFontWeight(2));
}

void LoadHotButtons(const IniFile &ini, GameWindowState &state)
{
	for (int page = 0; page < HotButtonPages; ++page)
		for (int i = 0; i < HotButtonsPerPage; ++i) {
			HotButton &button = state.hot_buttons[page * HotButtonsPerPage + i];
			button = {};
			std::string value = ini.Get("HotButtons", "Page" + std::to_string(page + 1) + "Button" + std::to_string(i + 1));
			if (value.empty() || value[0] <= 'A' || value[0] > 'A' + HotButton::PetCommand)
				continue;
			button.type = static_cast<HotButton::Type>(value[0] - 'A');
			button.value = std::atoi(value.c_str() + 1);
		}
	state.hot_buttons_changed = false;
}

void StoreHotButtons(IniFile &ini, const GameWindowState &state)
{
	// An empty one's key goes (SaveToCustomIniFile with no value).
	for (int page = 0; page < HotButtonPages; ++page)
		for (int i = 0; i < HotButtonsPerPage; ++i) {
			const HotButton &button = state.hot_buttons[page * HotButtonsPerPage + i];
			std::string key = "Page" + std::to_string(page + 1) + "Button" + std::to_string(i + 1);
			if (button.type == HotButton::Empty)
				ini.Remove("HotButtons", key);
			else
				ini.Set("HotButtons", key, std::string(1, static_cast<char>('A' + button.type)) + std::to_string(button.value));
		}
}
