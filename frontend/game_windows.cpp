#include "game_windows.h"
#include "buffs.h"
#include "chat_text.h"
#include "eqstr.h"
#include "game_values.h"
#include "group.h"
#include "ini_file.h"
#include "inventory.h"
#include "item_display.h"
#include "skills.h"
#include "spell_file.h"
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
		// CPlayerWnd::UpdatePlayerWindowState: while auto attack's on, a red lasso every other frame.
		SidlWindow::Control &root = wnd.Root();
		root.lasso = root.lasso == 0 && values.attacking ? 0xff0000 : 0;
	}

	// Invite's enabled (UpdateButtons, MainPageOnProcessFrame): another player targeted, and no group
	// or ours with a place free.
	bool CanInvite(const GameValues &values)
	{
		const ZoneSpawn *target = values.Target();
		if (!target || target->npc != 0 || target->id == values.zone->MySpawnId())
			return false;
		const Group *group = values.group;
		if (!group || !group->InGroup())
			return true;
		if (group->Leader() != values.zone->Profile().name)
			return false;
		for (int i = 0; i < Group::Members; ++i)
			if (group->Member(i).empty())
				return true;
		return false;
	}

	void GroupWindow(SidlWindow &wnd, const GameValues &values)
	{
		const bool invited = values.group && values.group->Invited();
		const bool in_group = values.group && values.group->InGroup();
		Show(wnd, "FollowButton", invited);
		Show(wnd, "DeclineButton", invited);
		Show(wnd, "InviteButton", !invited);
		Show(wnd, "DisbandButton", !invited);
		if (SidlWindow::Control *disband = wnd.Find("DisbandButton"))
			disband->enabled = in_group;
		if (SidlWindow::Control *invite = wnd.Find("InviteButton"))
			invite->enabled = CanInvite(values);
		// A member's pet gauge while they have a pet here, and the window menu's pets are on.
		for (int i = 1; i <= GroupMembers; ++i) {
			const ZoneSpawn *member = values.group ? values.group->MemberSpawn(i - 1) : nullptr;
			Show(wnd, "PetGauge" + std::to_string(i), member && values.group->show_pets && values.group->PetOf(*member));
		}
	}

	void BuffWindow(SidlWindow &wnd, const GameValues &values)
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
			const SpellInfo *spell = values.buffs ? values.buffs->SlotSpell(i) : nullptr;
			button->blank = !spell;
			button->tooltip = spell ? spell->name : "";
			button->decal = "A_SpellIcons"; // CBuffWindow's copy of it for each button's BuffIcon
			button->cell = spell ? std::max(spell->icon, 0) : -1;
			button->background = !spell ? "" : spell->good_effect == 0 ? "RedIconBackground" : "BlueIconBackground";
			button->alpha = 255;
			if (spell) {
				const ZoneBuff &buff = values.buffs->Slot(i);
				bool short_song = Buffs::Duration(*spell, buff.level) < 4 && values.zone->Profile().class_id == 8 &&
				                  spell->ClassLevel(8) != 0 && spell->ClassLevel(8) != 255;
				if (buff.duration < 4 && buff.duration != -1 && !short_song)
					button->alpha = static_cast<int>((100 - values.now_ms % 1000 / 10) * 255 / 100);
			}
			if (button->visible)
				wnd.Move(id, {c.Right() - (w + 1) * (column + 1), c.y + (h + 1) * (i % rows), w, h});
		}
	}

	void ActionsWindow(SidlWindow &wnd, const GameValues &values)
	{
		const bool invited = values.group && values.group->Invited();
		const bool in_group = values.group && values.group->InGroup();
		Show(wnd, "AMP_InviteButton", !invited);
		Show(wnd, "AMP_FollowButton", invited);
		bool sitting = values.zone->Self().stance == 110;
		Show(wnd, "AMP_SitButton", !sitting);
		Show(wnd, "AMP_StandButton", sitting);
		Show(wnd, "AMP_RunButton", !values.running);
		Show(wnd, "AMP_WalkButton", values.running);
		if (SidlWindow::Control *disband = wnd.Find("AMP_DisbandButton"))
			disband->enabled = in_group || invited;
		if (SidlWindow::Control *invite = wnd.Find("AMP_InviteButton"))
			invite->enabled = CanInvite(values);
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

	constexpr int SkillCount = Skills::Count;

	// CSkillsWnd::UpdateSkill's ranks (@005fe3ec, @005fe418): the first whose limit the value is
	// within, "Awful" (eqstr 3224) to "Master" (3234).
	constexpr int RankLimits[] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 2000};
	constexpr uint32_t FirstRankName = 3224;

	std::string EqText(const EqStr *eqstr, uint32_t id, const std::string &fallback)
	{
		return eqstr && eqstr->Has(id) ? eqstr->Format(id, {}) : fallback;
	}

	// CInvSlot::SetItem's tooltip for an empty worn slot (0..21, by its EQType; eqstr ids @005e5988).
	const struct
	{
		uint32_t id;
		const char *name;
	} EmptySlotNames[] = {
		{558, "Held"}, {559, "Ear"}, {560, "Head"}, {561, "Face"}, {559, "Ear"}, {562, "Neck"}, {563, "Shoulders"},
		{564, "Arms"}, {565, "Back"}, {566, "Wrist"}, {566, "Wrist"}, {567, "Range"}, {568, "Hands"},
		{569, "Primary"}, {570, "Secondary"}, {571, "Fingers"}, {571, "Fingers"}, {572, "Chest"}, {573, "Legs"},
		{574, "Feet"}, {575, "Waist"}, {576, "Ammo"},
	};

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
				auto it = items.find(static_cast<int16_t>(control->ItemSlot()));
				FillInvSlot(*control, it != items.end() ? &it->second : nullptr, values);
			}
		// The character view takes a click only to put away what's on the cursor; otherwise it
		// moves the window, as the rest of it does.
		if (SidlWindow::Control *view = wnd.Find("IW_CharacterView"))
			view->clickable = items.count(Inventory::Cursor) != 0 || Inventory::CursorMoneyType(me) != 0;
	}

	// CQuantityWnd's notifications as the slider and the edit box change: the box shows the
	// slider's value (UpdateEditWndFromSlider), and more than the stack in the box is the stack
	// (CheckMaxEditWnd). Closed by its close box or Escape, it takes nothing.
	void QuantityWindow(SidlWindow &wnd, const WindowManager &windows, GameWindowState &state)
	{
		if (!windows.Shown("QuantityWnd")) {
			state.quantity_slot = state.quantity_coin = -1;
			state.quantity_done = nullptr;
			return;
		}
		SidlWindow::Control *slider = wnd.Find("QTYW_Slider"), *edit = wnd.Find("QTYW_SliderInput");
		if (!slider || !edit)
			return;
		if (slider->slider_value != state.quantity_value)
			edit->text = std::to_string(slider->slider_value);
		else if (edit->text != state.quantity_text && std::atoi(edit->text.c_str()) > state.quantity_max) {
			wnd.SetSlider(*slider, state.quantity_max);
			edit->text = std::to_string(state.quantity_max);
		}
		state.quantity_value = slider->slider_value;
		state.quantity_text = edit->text;
	}

	// CQuantityWnd::Activate for `count` of something: all of it with Shift down, one with Ctrl,
	// and the one of a single one, are taken without the window (the value returned). Otherwise
	// the window comes up with the slider from 0 to the count at the count, its top left at (x,
	// y) unless that puts it off the screen, and the edit box taking the keys (0 returned); its
	// target is the state's quantity_slot or quantity_coin. None at all: nothing.
	int ActivateQuantity(WindowManager &windows, GameWindowState &state, int count, int x, int y, bool shift, bool ctrl,
		int start = -1)
	{
		if (count <= 0)
			return 0;
		int value = count < 2 ? count : 0;
		if (shift)
			value = count;
		else if (ctrl)
			value = 1;
		SidlWindow *wnd = windows.Find("QuantityWnd");
		SidlWindow::Control *slider = wnd ? wnd->Find("QTYW_Slider") : nullptr;
		SidlWindow::Control *edit = wnd ? wnd->Find("QTYW_SliderInput") : nullptr;
		if (!slider || !edit) // no quantity window in the UI files: all of it
			value = count;
		if (value != 0)
			return value;
		slider->slider_ticks = std::max(count + 1, 2); // SetNumTicks
		wnd->SetSlider(*slider, start >= 0 ? start : count); // SetValue(param_4)
		edit->text = std::to_string(slider->slider_value);
		state.quantity_max = count;
		state.quantity_value = slider->slider_value;
		state.quantity_text = edit->text;
		const Rect &r = wnd->Bounds();
		if (x + r.w > windows.ScreenWidth())
			x = windows.ScreenWidth() - r.w;
		if (y + r.h > windows.ScreenHeight())
			y = windows.ScreenHeight() - r.h;
		windows.MoveTo("QuantityWnd", x, y);
		wnd->Focus("QTYW_SliderInput");
		windows.Show("QuantityWnd", true);
		windows.Raise("QuantityWnd");
		return 0;
	}

	// The stack in `slot`: CInvSlot::HandleLButtonUp's quantity window, then SliderComplete.
	void OpenQuantity(WindowManager &windows, GameWindowState &state, ZoneSession &zone, int16_t slot, int count,
		int x, int y, bool shift, bool ctrl)
	{
		state.quantity_slot = -1;
		state.quantity_coin = -1;
		state.quantity_done = nullptr;
		if (int value = ActivateQuantity(windows, state, count, x, y, shift, ctrl)) {
			Inventory::TakeFromStack(zone, slot, value);
			return;
		}
		if (windows.Shown("QuantityWnd"))
			state.quantity_slot = slot;
	}

	// ClickedMoneyButton's second half: `amount` of the coin onto the cursor.
	bool CoinsToCursor(GameWindowState &state, ZoneSession &zone, int place, int coin, int amount)
	{
		state.ClearCursor(); // a button on the cursor goes (Activate)
		return Inventory::MoveMoney(zone, place, Inventory::CursorCoins, coin, coin, amount, state.bank_open);
	}

	// Accept, or Enter in the box: the slider takes the box's number (UpdateSliderFromEditWnd), the
	// window goes and its target gets the value (CSliderWnd::GetValue's notification 0x1d, to the
	// slot's window, CInvSlot::SliderComplete; or to the money button's, ClickedMoneyButton).
	void FinishQuantity(WindowManager &windows, GameWindowState &state, ZoneSession &zone, InventoryClickResult &result)
	{
		SidlWindow *wnd = windows.Find("QuantityWnd");
		SidlWindow::Control *slider = wnd ? wnd->Find("QTYW_Slider") : nullptr;
		SidlWindow::Control *edit = wnd ? wnd->Find("QTYW_SliderInput") : nullptr;
		int slot = state.quantity_slot, coin = state.quantity_coin;
		std::function<void(int)> done = std::move(state.quantity_done);
		windows.Show("QuantityWnd", false);
		state.quantity_slot = state.quantity_coin = -1;
		state.quantity_done = nullptr;
		if (!slider || !edit)
			return;
		wnd->SetSlider(*slider, std::atoi(edit->text.c_str()));
		if (done)
			done(slider->slider_value);
		else if (slot >= 0)
			Inventory::TakeFromStack(zone, static_cast<int16_t>(slot), slider->slider_value);
		else if (coin >= 0)
			result.coin_sound = CoinsToCursor(state, zone, state.quantity_place, coin, slider->slider_value);
	}

	void SkillsWindow(SidlWindow &wnd, const GameValues &values, GameWindowState &state)
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
		// CSkillsWnd::Activate: the skills HasSkill gives us (0..99), then the languages we know any
		// of (100 + n), kept until it's opened again.
		if (state.skill_rows.empty()) {
			for (int skill = 0; skill < 100; ++skill)
				if (Skills::HasSkill(me, skill) != 0)
					state.skill_rows.push_back(skill);
			for (int language = 0; language < 32; ++language)
				if (me.languages[language] != 0)
					state.skill_rows.push_back(100 + language);
		}
		// UpdateSkill: the name, the rank (the first of RankLimits it's no more than; a language
		// spoken fully is mastered) and the value.
		auto rank = [&](int value, bool language) {
			int i = 0;
			while (i < static_cast<int>(std::size(RankLimits)) && value > RankLimits[i])
				++i;
			if (i == static_cast<int>(std::size(RankLimits)))
				i = 0; // past them all: the first (local_18 left 0)
			if (i == 9 && language && value == 100)
				i = 10;
			return EqText(values.eqstr, FirstRankName + i, "");
		};
		list->rows.clear();
		for (int row : state.skill_rows) {
			if (row >= 100) {
				int language = row - 100, value = me.languages[language];
				uint32_t name = LanguageNameId(language);
				list->rows.push_back({name ? EqText(values.eqstr, name, "Language " + std::to_string(language)) : "Unknown",
					rank(value, true), std::to_string(value)});
				continue;
			}
			// 255 (can't) is HasSkill's racial level instead; 254 (not yet) says the level our
			// class gets it at, "LEVEL: %1" (eqstr 2161), and a 255 HasSkill leaves "LEVEL: 66".
			int value = me.skills[row];
			if (value == 255)
				value = Skills::HasSkill(me, row);
			std::string rank_text;
			if (value == 254 || value == 255) {
				int cls = me.class_id;
				std::string level = value == 255 ? "66"
				                    : std::to_string(cls >= 1 && cls <= Skills::Classes ? Skills::Get(row).level[cls - 1] : 0);
				rank_text = FormatEqString(values.eqstr, 2161, "LEVEL: %1", {level});
			}
			else
				rank_text = rank(value, false);
			list->rows.push_back({SkillName(values.eqstr, row), rank_text, value < 254 ? std::to_string(value) : ""});
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
		state.ClearCursor();
		state.cursor = button;
		state.cursor_text = HotButtonText(windows, state, button);
	}

	void HotButtonWindow(SidlWindow &wnd, WindowManager &windows, const GameWindowState &state, const GameValues &values)
	{
		SetText(wnd, "HB_CurrentPageLabel", PageLabel(state.hot_button_page));
		// CHotButtonWnd::OnProcessFrame: a hot button with something on it shows its text, unchecked;
		// an empty one has no text and is checked, so it shows its Pressed picture. One that isn't
		// ready is checked too (yellow and down): Attack and Range while __RangeAttackReady is
		// clear (both of them, by it alone), a combat skill or ability while its skill isn't ready
		// (__AltTimerReady or __DoAbilityAvailable), as its Actions window button shows.
		SidlWindow *actions = windows.Find("ActionsWindow");
		for (int i = 1; i <= HotButtonsPerPage; ++i) {
			std::string n = std::to_string(i);
			const HotButton &held = state.hot_buttons[state.hot_button_page * HotButtonsPerPage + i - 1];
			if (SidlWindow::Control *button = wnd.Find("HB_Button" + n)) {
				bool empty = held.type == HotButton::Empty;
				button->text = empty ? "" : HotButtonText(windows, state, held);
				button->checked = empty;
				if (held.type == HotButton::Combat)
					button->checked = !values.range_ready;
				else if (held.type == HotButton::CombatSkill || held.type == HotButton::Ability) {
					std::string id = SourceButton(windows, held);
					const SidlWindow::Control *source = actions && !id.empty() ? actions->Find(id) : nullptr;
					button->checked = source && source->checked;
				}
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
		state.skill_rows.clear(); // listed again on the next frame
		windows.Show("SkillsWindow", true);
	}
	if (window != "HotButtonWnd")
		return "";
	StepPage(state.hot_button_page, control, "HB_PageLeftButton", "HB_PageRightButton");
	int slot = HotButtonSlot(control);
	if (slot < 0)
		return "";
	HotButton &button = PageButton(state, slot);
	if (state.cursor.type == HotButton::Empty)
		return DoHotButton(windows, state, slot);
	// What's on the cursor goes on the button, and what was on it onto the cursor.
	HotButton was = button;
	button = state.cursor;
	state.ClearCursor();
	if (was.type != HotButton::Empty)
		Attach(windows, state, was);
	state.hot_buttons_changed = true;
	return "";
}

std::string DoHotButton(WindowManager &windows, GameWindowState &state, int slot)
{
	// Of what we can do so far, socials, and the Actions window buttons: the main page's, the combat
	// page's Attack / Range (ExecuteCmd 8, DoAttack 0xb) and the combat skills and abilities
	// (UseSkill of __DoAbilityList's or the combat list's skill), which the caller clicks.
	if (slot < 0 || slot >= HotButtonsPerPage)
		return "";
	HotButton &button = PageButton(state, slot);
	if (button.type == HotButton::Social && ValidSocial(button.value) && state.current_social < 0)
		StartSocial(state, button.value);
	return SourceButton(windows, button);
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

void FillInvSlot(SidlWindow::Control &control, const ZoneItem *item, const GameValues &values)
{
	control.cell_animation = "A_DragItem";
	control.cell = item && item->icon >= 500 ? item->icon - 500 : -1;
	control.count = item ? Inventory::Count(*item) : 0;
	// SetItem's tooltip: the item's name; an empty worn slot's name, an empty general slot none.
	int n = control.ItemSlot();
	if (item)
		control.tooltip = item->name;
	else if (n >= 0 && n < static_cast<int>(std::size(EmptySlotNames)))
		control.tooltip = EqText(values.eqstr, EmptySlotNames[n].id, EmptySlotNames[n].name);
	else
		control.tooltip.clear();
}

int InvSlotAt(WindowManager &windows, const std::string &window, const std::string &control)
{
	SidlWindow *wnd = windows.Find(window);
	const SidlWindow::Control *slot = wnd ? wnd->Find(control) : nullptr;
	if (!slot || slot->def->type != "InvSlot" || !wnd->Shown(*slot))
		return -1;
	return slot->ItemSlot();
}

const ZoneItem *SlotItem(WindowManager &windows, const GameValues &values, const std::string &window, const std::string &control)
{
	// The InvSlot a control is and the item in it.
	int slot = InvSlotAt(windows, window, control);
	if (slot < 0 || !values.zone)
		return nullptr;
	const std::map<int16_t, ZoneItem> &items = values.zone->Items();
	auto it = items.find(static_cast<int16_t>(slot));
	return it != items.end() ? &it->second : nullptr;
}

bool ShowItemDisplay(WindowManager &windows, const GameValues &values, GameWindowState &state, const std::string &window,
	const std::string &control)
{
	const ZoneItem *item = SlotItem(windows, values, window, control);
	if (!item || !windows.Find("ItemDisplayWindow"))
		return false;
	ShowItemDisplay(windows, values, state, *item);
	return true;
}

void ShowItemDisplay(WindowManager &windows, const GameValues &values, GameWindowState &state, const ZoneItem &item)
{
	SidlWindow *display = windows.Find("ItemDisplayWindow");
	if (!display)
		return;
	state.display_item = item;
	display->Root().text = item.name;
	if (SidlWindow::Control *description = display->Find("ItemDescription")) {
		description->text = ItemDisplayText(values, item);
		description->scroll_pos = 0;
	}
	if (SidlWindow::Control *icon = display->Find("IconButton")) {
		icon->cell_animation = "A_DragItem";
		icon->cell = item.icon >= 500 ? item.icon - 500 : -1;
		icon->decal.clear();
		icon->background.clear();
	}
	windows.Show("ItemDisplayWindow", true);
	windows.Raise("ItemDisplayWindow");
}

void ToggleSpellDisplay(WindowManager &windows, const GameValues &values, GameWindowState &state, int spell_id,
	bool details)
{
	SidlWindow *display = windows.Find("ItemDisplayWindow");
	const SpellInfo *spell = values.spells ? values.spells->Find(spell_id) : nullptr;
	if (!display || !spell)
		return;
	// Showing already (IsActivated), it goes, whatever it shows.
	if (windows.Shown("ItemDisplayWindow")) {
		windows.Show("ItemDisplayWindow", false);
		return;
	}
	state.display_item = {}; // ItemValid 0: the icon links nothing
	display->Root().text = SpellDisplayTitle(values, *spell);
	if (SidlWindow::Control *description = display->Find("ItemDescription")) {
		description->text = SpellDisplayText(values, *spell, details);
		description->scroll_pos = 0;
	}
	// CBuffWindow::SetBuffIcon on the icon: the spell's icon on red (detrimental) or blue.
	if (SidlWindow::Control *icon = display->Find("IconButton")) {
		icon->cell_animation.clear();
		icon->decal = "A_SpellIcons";
		icon->cell = std::max(spell->icon, 0);
		icon->no_background = false;
		icon->background = spell->good_effect == 0 ? "RedIconBackground" : "BlueIconBackground";
	}
	windows.Show("ItemDisplayWindow", true);
	windows.Raise("ItemDisplayWindow");
}

bool InventoryClick(WindowManager &windows, GameWindowState &state, ZoneSession &zone, const std::string &window,
	const std::string &control, bool shift, bool ctrl, InventoryClickResult &result)
{
	using namespace Inventory;
	result = {};
	if (window == "QuantityWnd") {
		if (control != "QTYW_Accept_Button")
			return false;
		FinishQuantity(windows, state, zone, result);
		return true;
	}
	const std::map<int16_t, ZoneItem> &items = zone.Items();
	// Coins on the cursor: a slot does nothing with them (HandleLButtonUp); the character view and
	// the money buttons take them (MoneyClick).
	if (CursorMoneyType(zone.Profile()) != 0 && !(window == "InventoryWindow" && control == "IW_CharacterView"))
		return InvSlotAt(windows, window, control) >= 0;
	if (window == "InventoryWindow" && control == "IW_CharacterView") {
		if (items.count(Cursor)) {
			result.sound = true;
			AutoInventory(zone, &result.message, state.fv_rules);
			result.message_color = 15;
		}
		return true;
	}
	SidlWindow *wnd = windows.Find(window);
	const SidlWindow::Control *slot_control = wnd ? wnd->Find(control) : nullptr;
	if (!slot_control || slot_control->def->type != "InvSlot")
		return false;
	// Nothing while the quantity window's up, or while the zone has a combine to answer.
	if (state.QuantityUp() || state.busy)
		return true;
	// A button on the cursor goes; then the slot does what it does with an item or none.
	state.ClearCursor();
	int16_t slot = static_cast<int16_t>(slot_control->ItemSlot());
	auto in = [slot](int first, int last) { return slot >= first && slot <= last; };
	auto held = items.find(Cursor), there = items.find(slot);
	bool bag_there = there != items.end() && there->second.item_class == 1;
	if (held == items.end()) {
		// Nothing comes back out of the trade window.
		if (there == items.end() || in(FirstTrade, 3999))
			return true;
		// A stack opens the quantity window, but for a corpse's or a world container's.
		if (Stacks(there->second) && !in(FirstLoot, LastLoot) && !in(WorldContainer, 4999)) {
			const Rect &r = slot_control->rect;
			OpenQuantity(windows, state, zone, slot, Count(there->second), r.x + r.w / 2, r.y + r.h / 2, shift, ctrl);
			return true;
		}
		if (bag_there)
			result.close_bag = slot;
		result.sound = Move(zone, slot, Cursor, result.message);
		return true;
	}
	const ZoneProfile &me = zone.Profile();
	if (slot > 0 && slot <= LastGeneral) {
		if (!CanEquip(me, items, held->second, slot))
			return true;
		if (slot < FirstGeneral && !CanUse(me, held->second)) {
			result.message = CantEquip;
			return true;
		}
	}
	else if (in(FirstTrade, 3029)) {
		// Into an empty slot of ours while trading, a NO DROP item only to an NPC.
		if (there != items.end() || !state.trading || slot > LastTrade)
			return true;
		if (!state.trade_npc && !CanDrop(items, held->second, Cursor, false)) {
			result.message = held->second.no_drop != 0 ? 12922 : 12921;
			result.message_color = 15;
			return true;
		}
		result.trade_changed = true;
	}
	else if (slot <= LastGeneral || in(3030, 3999) || in(FirstLoot, LastLoot))
		return true;
	if (bag_there)
		result.close_bag = slot;
	result.sound = Move(zone, Cursor, slot, result.message);
	return true;
}

bool MoneyClick(WindowManager &windows, GameWindowState &state, ZoneSession &zone, const std::string &window,
	const std::string &control, bool shift, bool ctrl, InventoryClickResult &result)
{
	using namespace Inventory;
	result = {};
	int place = -1;
	const char *prefix = nullptr;
	if (window == "InventoryWindow")
		place = Purse, prefix = "IW_Money";
	else if (window == "BankWnd" && state.bank_open)
		place = Bank, prefix = "BW_Money";
	if (place < 0)
		return false;
	ZoneProfile &me = zone.EditProfile();
	int coin = -1;
	if (control.size() == std::string(prefix).size() + 1 && control.rfind(prefix, 0) == 0)
		coin = control.back() - '0';
	// CInventoryWnd::HandleLButtonUp: the character view takes coins on the cursor as their own button would.
	if (place == Purse && control == "IW_CharacterView" && CursorMoneyType(me) != 0)
		coin = CursorMoneyType(me) - 1;
	if (coin < 0 || coin > 3)
		return false;
	if (state.QuantityUp())
		return true;
	if (int held = CursorMoneyType(me)) {
		MoveMoney(zone, CursorCoins, place, held - 1, coin, Coins(me, CursorCoins, held - 1), state.bank_open);
		return true;
	}
	SidlWindow *wnd = windows.Find(window);
	const SidlWindow::Control *button = wnd ? wnd->Find(control) : nullptr;
	int x = -1, y = -1;
	if (button)
		x = button->rect.x + button->rect.w / 2, y = button->rect.y + button->rect.h / 2;
	state.quantity_slot = state.quantity_coin = -1;
	state.quantity_done = nullptr;
	if (int value = ActivateQuantity(windows, state, Coins(me, place, coin), x, y, shift, ctrl)) {
		result.coin_sound = CoinsToCursor(state, zone, place, coin, value);
		return true;
	}
	if (windows.Shown("QuantityWnd")) {
		state.quantity_coin = coin;
		state.quantity_place = place;
	}
	return true;
}

void AskQuantity(WindowManager &windows, GameWindowState &state, int count, int x, int y, bool shift, bool ctrl,
	std::function<void(int)> done, int start)
{
	state.quantity_slot = state.quantity_coin = -1;
	state.quantity_done = nullptr;
	if (x < 0 || y < 0) { // CXWnd::Center
		if (const SidlWindow *wnd = windows.Find("QuantityWnd")) {
			x = (windows.ScreenWidth() - wnd->Bounds().w) / 2;
			y = (windows.ScreenHeight() - wnd->Bounds().h) / 2;
		}
	}
	if (int value = ActivateQuantity(windows, state, count, x, y, shift, ctrl, start)) {
		done(value);
		return;
	}
	if (windows.Shown("QuantityWnd"))
		state.quantity_done = std::move(done);
}

bool QuantityEnter(WindowManager &windows, GameWindowState &state, ZoneSession &zone, InventoryClickResult &result)
{
	result = {};
	SidlWindow *wnd = windows.Find("QuantityWnd");
	if (!wnd || !windows.Shown("QuantityWnd") || !wnd->Focused("QTYW_SliderInput"))
		return false;
	FinishQuantity(windows, state, zone, result);
	return true;
}

void GameWindowRightRelease(WindowManager &windows, const std::string &window, const std::string &control)
{
	if (InvSlotAt(windows, window, control) >= 0)
		windows.Show("ItemDisplayWindow", false);
}

void UpdateGameWindows(WindowManager &windows, const GameValues &values, GameWindowState &state)
{
	if (!values.zone)
		return;
	if (SidlWindow *wnd = windows.Find("PlayerWindow"))
		PlayerWindow(*wnd, values);
	if (SidlWindow *wnd = windows.Find("GroupWindow"))
		GroupWindow(*wnd, values);
	if (SidlWindow *wnd = windows.Find("BuffWindow"))
		BuffWindow(*wnd, values);
	// The Actions window first: the hot buttons show its buttons as they are this frame.
	if (SidlWindow *wnd = windows.Find("ActionsWindow")) {
		ActionsWindow(*wnd, values);
		SocialsPage(*wnd, windows, state);
	}
	if (SidlWindow *wnd = windows.Find("HotButtonWnd"))
		HotButtonWindow(*wnd, windows, state, values);
	if (SidlWindow *wnd = windows.Find("InventoryWindow"); wnd && windows.Shown("InventoryWindow"))
		InventoryWindow(*wnd, windows, values);
	if (SidlWindow *wnd = windows.Find("SkillsWindow"); wnd && windows.Shown("SkillsWindow"))
		SkillsWindow(*wnd, values, state);
	if (SidlWindow *wnd = windows.Find("QuantityWnd"))
		QuantityWindow(*wnd, windows, state);
}

std::string SkillName(const EqStr *eqstr, int skill)
{
	// Past them, InitializeSkillData's default: "None" (eqstr 1216).
	if (skill < 0 || skill >= SkillCount)
		return EqText(eqstr, 1216, "None");
	return EqText(eqstr, Skills::Get(skill).name, "Skill " + std::to_string(skill));
}

void DrawCursorAttachment(Canvas &canvas, TextRenderer &text, const WindowManager &windows, const GameWindowState &state,
	const GameValues &values, int x, int y, uint64_t now_ms)
{
	const ZoneItem *held = nullptr;
	if (values.zone) {
		auto it = values.zone->Items().find(Inventory::Cursor);
		if (it != values.zone->Items().end())
			held = &it->second;
	}
	int money = values.zone ? Inventory::CursorMoneyType(values.zone->Profile()) : 0;
	if (!state.Attached() && !held && !money)
		return;
	// CCursorAttachment: its screen (50 by 50) a pixel right of and below the mouse, with CA_Anim in it.
	Rect anim{5, 5, 40, 40}, screen{0, 0, 50, 50};
	if (const SidlDefinitions *defs = windows.Definitions()) {
		if (const SidlPiece *piece = defs->Piece("CA_Anim"))
			anim = piece->rect;
		if (const SidlPiece *piece = defs->Piece("CursorAttachment"))
			screen = piece->rect;
	}
	Rect dest{x + 1 + anim.x, y + 1 + anim.y, anim.w, anim.h};
	if (money && !held) {
		// Coins (type 3): A_DragItem's cell 143 + CursorMoneyType, and how many in font 1 centered
		// over a dark box across the bottom, 4 up from it (DrawCursorAttachmentText).
		windows.DrawCell(canvas, "A_DragItem", 143 + money, dest);
		int amount = Inventory::Coins(values.zone->Profile(), Inventory::CursorCoins, money - 1);
		std::string number = std::to_string(amount);
		int h = SidlFontHeight(1);
		int left = x + 1, right = x + 1 + screen.w, top = y + 1 + screen.h - 4;
		canvas.Fill({left, top, right - left, h}, 0x404040);
		TextSize size = text.Measure(number, h, SidlFontWeight(1));
		text.Draw(canvas, left + (right - left - size.w) / 2, top, number, h, 0xffffff, SidlFontWeight(1));
		return;
	}
	if (held) {
		// The item's icon, and a stack's count in font 1 over a dark box 12 by 10, 6 in from the
		// screen's bottom right (DrawCursorAttachmentText), padded to two places.
		if (held->icon < 500)
			return;
		windows.DrawCell(canvas, "A_DragItem", held->icon - 500, dest);
		if (int count = Inventory::Count(*held); count > 0) {
			int r = x + 1 + screen.w, b = y + 1 + screen.h;
			canvas.Fill({r - 18, b - 16, 12, 10}, 0x404040);
			std::string number = std::to_string(count);
			if (number.size() < 2)
				number = " " + number;
			text.Draw(canvas, r - 18, b - 16, number, SidlFontHeight(1), 0xffffff, SidlFontWeight(1));
		}
		return;
	}
	// A spell from the book: its icon (the book slot's decal, A_SpellIcons).
	if (state.cursor_spell >= 0) {
		windows.DrawCell(canvas, "A_SpellIcons", state.cursor_spell_icon, dest);
		return;
	}
	// A button: CA_Anim showing A_SquareBtnNormal, and the text over that in font 2, white
	// (DrawCursorAttachmentBackground).
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
