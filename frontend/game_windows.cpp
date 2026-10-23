#include "game_windows.h"
#include "chat_text.h"
#include "eqstr.h"
#include "game_values.h"
#include "ini_file.h"
#include "window_manager.h"
#include "zone_session.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <string>

namespace
{
	constexpr int BuffSlots = 15;    // CBuffWindow's buttons, Buff0..Buff14
	constexpr int GroupMembers = 5;  // the others in a group
	constexpr int Pages = 10;        // of socials, and of hot buttons
	constexpr int SocialStepMs = 101;
	constexpr int SocialColors = 20; // SEW_TC0Button..SEW_TC19Button
	constexpr size_t SocialNameChars = 15, SocialLineChars = 255;
	constexpr size_t SocialNameLoaded = 8; // what loadCustomFromINI keeps of a name

	// The first page's socials (eqstr 6652..6663, their lines 6664..6675) in English.
	const char *const DefaultSocialNames[SocialsPerPage][2] = {
		{"Afk", "/afk"}, {"Anon", "/anon"}, {"Split", "/autosplit"}, {"Bug", "/bug"},
		{"Consider", "/con"}, {"Duel", "/duel"}, {"Feedback", "/feedback"}, {"Hail", "/hail"},
		{"Played", "/played"}, {"Time", "/time"}, {"GM List", "/who all GM"}, {"Wave", "/wave"},
	};

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

	// InitializeSkillData (@004d0e88): each skill's name, eqstr ids by skill number.
	constexpr uint32_t SkillNames[74] = {13855, 13856, 13857, 13858, 13859, 13861, 13862, 13863, 13864, 13866, 13867,
		13871, 13872, 13874, 13875, 13876, 13877, 13878, 13879, 13880, 13881, 13882, 13883, 13884, 13885, 13886, 13888,
		13889, 13890, 13891, 13893, 13894, 13895, 13896, 13897, 13899, 13900, 13903, 13904, 13905, 13906, 13908, 13909,
		13910, 13911, 13912, 13913, 13914, 13915, 13916, 13917, 13919, 13920, 13921, 13923, 13854, 13853, 13852, 13851,
		13850, 13865, 13918, 13907, 13870, 13887, 13873, 13860, 13868, 13892, 13901, 13898, 13922, 13869, 13902};
	constexpr int SkillCount = 74;

	// CSkillsWnd::UpdateSkill's ranks (@005fe3ec, @005fe418): the first whose limit the value is
	// within, "Awful" (eqstr 3224) to "Master" (3234).
	constexpr int RankLimits[] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 2000};
	constexpr uint32_t FirstRankName = 3224;

	std::string EqText(const EqStr *eqstr, uint32_t id, const std::string &fallback)
	{
		return eqstr && eqstr->Has(id) ? eqstr->Format(id, {}) : fallback;
	}

	void InventoryWindow(SidlWindow &wnd, const WindowManager &windows, const GameValues &values)
	{
		const ZoneProfile &me = values.zone->Profile();
		const int32_t coins[4] = {me.platinum, me.gold, me.silver, me.copper};
		for (int i = 0; i < 4; ++i)
			SetText(wnd, "IW_Money" + std::to_string(i), std::to_string(coins[i]));
		Show(wnd, "AltAdvLabel", me.level > 50);
		Show(wnd, "AltAdvGauge", me.level > 50);
		if (SidlWindow::Control *anim = wnd.Find("ClassAnim")) {
			char name[32];
			snprintf(name, sizeof(name), "A_ClassAnim%02d", me.class_id);
			const SidlDefinitions *defs = windows.Definitions();
			if (defs && defs->Animation(name))
				anim->animation = name;
		}
		// The slots are numbered by their EQType, as the items are.
		const std::map<int16_t, ZoneItem> &items = values.zone->Items();
		for (int slot = 1; slot <= 29; ++slot)
			if (SidlWindow::Control *control = wnd.Find("InvSlot" + std::to_string(slot))) {
				auto it = items.find(static_cast<int16_t>(control->def->eq_type));
				control->cell_animation = "A_DragItem";
				control->cell = it != items.end() && it->second.icon >= 500 ? it->second.icon - 500 : -1;
			}
	}

	void SkillsWindow(SidlWindow &wnd, const GameValues &values)
	{
		SidlWindow::Control *list = wnd.Find("SkillList");
		if (!list)
			return;
		// CSkillsWnd::CSkillsWnd's look.
		list->row_color = 0xffffff;
		list->header_color = 0x8aa3ff;
		list->row_height = 16;
		list->justification = {0, 1, 1};
		const ZoneProfile &me = values.zone->Profile();
		auto rank = [&](int value, bool language) {
			int i = 0;
			while (i + 1 < static_cast<int>(std::size(RankLimits)) && value > RankLimits[i])
				++i;
			if (i == 9 && language && value == 100) // a language spoken fully is mastered
				i = 10;
			return EqText(values.eqstr, FirstRankName + i, "");
		};
		list->rows.clear();
		for (int skill = 0; skill < SkillCount; ++skill) {
			int value = me.skills[skill];
			if (value < 0 || value >= 254)
				continue;
			list->rows.push_back({SkillName(values.eqstr, skill), rank(value, false), std::to_string(value)});
		}
		for (int language = 0; language < 32; ++language) {
			int value = me.languages[language];
			if (value == 0)
				continue;
			uint32_t name = LanguageNameId(language);
			list->rows.push_back({name ? EqText(values.eqstr, name, "Language " + std::to_string(language)) : "Unknown",
				rank(value, true), std::to_string(value)});
		}
		wnd.UpdateListScroll(*list);
	}

	// CSocialEditWnd::GetSocialTextColor: the TextColor of the editor's SEW_TC<color>Button.
	uint32_t SocialColor(WindowManager &windows, int color)
	{
		SidlWindow *editor = windows.Find("SocialEditWnd");
		const SidlWindow::Control *button =
			editor && color >= 0 && color < SocialColors ? editor->Find("SEW_TC" + std::to_string(color) + "Button") : nullptr;
		return button ? button->text_color : 0x606060;
	}

	// CActionsWnd::SocialsPageOnProcessFrame: the page's socials' names, in their colors.
	void SocialsPage(SidlWindow &wnd, WindowManager &windows, const GameWindowState &state)
	{
		for (int i = 0; i < SocialsPerPage; ++i)
			if (SidlWindow::Control *button = wnd.Find("ASP_SocialButton" + std::to_string(i + 1))) {
				const Social &social = state.socials[state.social_page * SocialsPerPage + i];
				button->text = social.name;
				button->text_color = SocialColor(windows, social.color);
			}
	}

	bool ValidSocial(int social)
	{
		return social >= 0 && social < SocialPages * SocialsPerPage;
	}

	// Its keys in [Socials]: Page<p>Button<b>...
	std::string SocialKey(int social)
	{
		return "Page" + std::to_string(social / SocialsPerPage + 1) + "Button" + std::to_string(social % SocialsPerPage + 1);
	}

	// strnicmp(line, "/pause", 4).
	bool IsPause(const std::string &line)
	{
		const char *pause = "/pau";
		for (int i = 0; i < 4; ++i)
			if (i >= static_cast<int>(line.size()) || std::tolower(static_cast<unsigned char>(line[i])) != pause[i])
				return false;
		return true;
	}

	// SocialsPageWndNotification's click (the one going or not) and DoHotButton's.
	void StartSocial(GameWindowState &state, int social)
	{
		state.current_social = social;
		state.social_line = 0;
		state.social_wait = 0;
	}

	// CSocialEditWnd::Activate and UpdateControlsFromSocial: the editor on a social, its boxes
	// filled from it, on top.
	void OpenSocialEditor(WindowManager &windows, GameWindowState &state, int social)
	{
		SidlWindow *editor = windows.Find("SocialEditWnd");
		if (!editor || !ValidSocial(social))
			return;
		const Social &from = state.socials[social];
		state.edit_social = social;
		state.edit_color = from.color;
		if (SidlWindow::Control *name = editor->Find("SEW_NameInput")) {
			name->text = from.name;
			name->max_chars = SocialNameChars;
			name->text_color = SocialColor(windows, from.color); // UpdateSelectedSocialHotkey
		}
		for (int i = 0; i < SocialLines; ++i)
			if (SidlWindow::Control *line = editor->Find("SEW_Line" + std::to_string(i) + "Input")) {
				line->text = from.lines[i];
				line->max_chars = SocialLineChars;
			}
		windows.Show("SocialEditWnd", true);
		windows.Raise("SocialEditWnd");
	}

	// CSocialEditWnd's WndNotification: Accept, Clear, and the color buttons.
	void SocialEditorClick(WindowManager &windows, GameWindowState &state, const std::string &control)
	{
		SidlWindow *editor = windows.Find("SocialEditWnd");
		if (!editor)
			return;
		SidlWindow::Control *name = editor->Find("SEW_NameInput");
		auto line = [&](int i) { return editor->Find("SEW_Line" + std::to_string(i) + "Input"); };
		if (control == "SEW_Accept_Button" && ValidSocial(state.edit_social)) {
			// SaveSocialCommandData: the boxes into the social, kept, and the editor closed.
			Social &social = state.socials[state.edit_social];
			if (name)
				social.name = name->text;
			for (int i = 0; i < SocialLines; ++i)
				if (SidlWindow::Control *box = line(i))
					social.lines[i] = box->text;
			social.color = state.edit_color;
			social.changed = true;
			state.socials_changed = true;
			windows.Show("SocialEditWnd", false);
		}
		else if (control == "SEW_Clear_Button") {
			// ClearSocialEditFields: the boxes emptied; the social keeps what it had until Accept.
			if (name)
				name->text.clear();
			for (int i = 0; i < SocialLines; ++i)
				if (SidlWindow::Control *box = line(i))
					box->text.clear();
		}
		else {
			// UpdateSelectedSocialHotkey: the color picked, the name shown in it.
			for (int i = 0; i < SocialColors; ++i)
				if (control == "SEW_TC" + std::to_string(i) + "Button") {
					state.edit_color = i;
					if (name)
						name->text_color = SocialColor(windows, i);
				}
		}
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
	// their names (no AA abilities yet: socials from 120 on).
	std::string HotButtonText(WindowManager &windows, const GameWindowState &state, const HotButton &button)
	{
		if (button.type == HotButton::Action && button.value >= static_cast<int>(std::size(MainButtons)))
			return button.value == 10 ? "Spells" : "Menu Button";
		if (button.type == HotButton::Social)
			return ValidSocial(button.value) ? state.socials[button.value].name : "";
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
		state.cursor_text = HotButtonText(windows, state, button);
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
				button->text = button->checked ? "" : HotButtonText(windows, state, held);
				button->text_color = 0xffffff;
				if (held.type == HotButton::Social && ValidSocial(held.value))
					button->text_color = SocialColor(windows, state.socials[held.value].color);
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
	if (window == "ActionsWindow") {
		HotButton social = ActionsHotButton(state, control);
		if (social.type == HotButton::Social)
			StartSocial(state, social.value);
	}
	if (window == "SocialEditWnd")
		SocialEditorClick(windows, state, control);
	if ((window == "InventoryWindow" || window == "SkillsWindow") && control == "DoneButton")
		windows.Show(window, false);
	if (window == "InventoryWindow" && control == "IW_Skills" && !windows.Shown("SkillsWindow")) {
		// CSkillsWnd::Activate: the list from the top.
		if (SidlWindow *skills = windows.Find("SkillsWindow"))
			if (SidlWindow::Control *list = skills->Find("SkillList"))
				list->scroll_pos = 0;
		windows.Show("SkillsWindow", true);
	}
	if (window != "HotButtonWnd")
		return "";
	StepPage(state.hot_button_page, control, "HB_PageLeftButton", "HB_PageRightButton");
	int slot = HotButtonSlot(control);
	if (slot < 0)
		return "";
	HotButton &button = PageButton(state, slot);
	if (state.cursor.type == HotButton::Empty) {
		// DoHotButton: of what we can do so far, the main page's buttons and socials.
		if (button.type == HotButton::Social && ValidSocial(button.value) && state.current_social < 0)
			StartSocial(state, button.value);
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

void GameWindowRightClick(WindowManager &windows, GameWindowState &state, const std::string &window, const std::string &control)
{
	// SocialsPageWndNotification's 3, and DoHotButtonRightClick for a social.
	if (window == "ActionsWindow") {
		HotButton social = ActionsHotButton(state, control);
		if (social.type == HotButton::Social)
			OpenSocialEditor(windows, state, social.value);
	}
	else if (window == "HotButtonWnd") {
		int slot = HotButtonSlot(control);
		if (slot >= 0 && PageButton(state, slot).type == HotButton::Social)
			OpenSocialEditor(windows, state, PageButton(state, slot).value);
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
	if (SidlWindow *wnd = windows.Find("ActionsWindow")) {
		ActionsWindow(*wnd, values);
		SocialsPage(*wnd, windows, state);
	}
	if (SidlWindow *wnd = windows.Find("HotButtonWnd"))
		HotButtonWindow(*wnd, windows, state);
	if (SidlWindow *wnd = windows.Find("InventoryWindow"); wnd && windows.Shown("InventoryWindow"))
		InventoryWindow(*wnd, windows, values);
	if (SidlWindow *wnd = windows.Find("SkillsWindow"); wnd && windows.Shown("SkillsWindow"))
		SkillsWindow(*wnd, values);
}

std::string SkillName(const EqStr *eqstr, int skill)
{
	// Past them, InitializeSkillData's default: "None" (eqstr 1216).
	if (skill < 0 || skill >= SkillCount)
		return EqText(eqstr, 1216, "None");
	return EqText(eqstr, SkillNames[skill], "Skill " + std::to_string(skill));
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

void DefaultSocials(GameWindowState &state, const EqStr *eqstr)
{
	// initCustom: everything empty, then the first page's names and first lines.
	for (Social &social : state.socials)
		social = {};
	auto text = [eqstr](uint32_t id, const char *fallback) {
		return eqstr && eqstr->Has(id) ? eqstr->Format(id, {}) : std::string(fallback);
	};
	for (int i = 0; i < SocialsPerPage; ++i) {
		state.socials[i].name = text(6652 + i, DefaultSocialNames[i][0]);
		state.socials[i].lines[0] = text(6664 + i, DefaultSocialNames[i][1]);
	}
	state.socials_changed = false;
}

void LoadSocials(const IniFile &ini, GameWindowState &state)
{
	// loadCustomFromINI: a social with a Name takes it (its first 8 characters), and whichever of
	// its Color and lines are there; a missing line keeps what it had. "*..." is missing.
	auto read = [&ini](const std::string &key, std::string &value) {
		value = ini.Get("Socials", key, "*NULL*");
		return value.empty() || value[0] != '*';
	};
	for (int i = 0; i < SocialPages * SocialsPerPage; ++i) {
		Social &social = state.socials[i];
		std::string key = SocialKey(i), value;
		if (!read(key + "Name", value))
			continue;
		social.name = value.substr(0, SocialNameLoaded);
		if (read(key + "Color", value))
			social.color = std::atoi(value.c_str());
		for (int line = 0; line < SocialLines; ++line)
			if (read(key + "Line" + std::to_string(line + 1), value))
				social.lines[line] = value;
	}
}

void StoreSocials(IniFile &ini, GameWindowState &state)
{
	// saveCustom: the changed ones; without a name, all of its keys go, and so does an empty line's.
	for (int i = 0; i < SocialPages * SocialsPerPage; ++i) {
		Social &social = state.socials[i];
		if (!social.changed)
			continue;
		social.changed = false;
		std::string key = SocialKey(i);
		if (social.name.empty()) {
			ini.Remove("Socials", key + "Name");
			ini.Remove("Socials", key + "Color");
		}
		else {
			ini.Set("Socials", key + "Name", social.name);
			ini.SetInt("Socials", key + "Color", social.color);
		}
		for (int line = 0; line < SocialLines; ++line) {
			std::string line_key = key + "Line" + std::to_string(line + 1);
			if (social.name.empty() || social.lines[line].empty())
				ini.Remove("Socials", line_key);
			else
				ini.Set("Socials", line_key, social.lines[line]);
		}
	}
	state.socials_changed = false;
}

std::string SocialStep(GameWindowState &state, uint64_t now_ms)
{
	if (state.current_social < 0 || now_ms - state.social_step_ms < SocialStepMs)
		return "";
	state.social_step_ms = now_ms;
	if (state.social_wait > 0) {
		--state.social_wait;
		return "";
	}
	if (!ValidSocial(state.current_social)) { // an AA ability's "/alt activate" (not yet)
		state.current_social = -1;
		return "";
	}
	const Social &social = state.socials[state.current_social];
	int &line = state.social_line;
	while (line < SocialLines && social.lines[line].empty())
		++line;
	if (line >= SocialLines) {
		state.current_social = -1;
		return "";
	}
	// "/pau..." (strnicmp 4 of "/pause"): atoi from its eighth character, and what's after a comma.
	const std::string &text = social.lines[line];
	std::string command = text;
	int wait = 1;
	if (IsPause(text)) {
		wait = std::clamp(text.size() > 7 ? std::atoi(text.c_str() + 7) : 0, 1, 600);
		size_t comma = text.find(',');
		command = comma != std::string::npos && text.size() - comma > 1 ? text.substr(comma + 1) : "";
	}
	if (++line >= SocialLines)
		state.current_social = -1;
	else
		state.social_wait = wait;
	return command;
}
