#pragma once

// What eqgame's in-game windows do for themselves every frame, beyond what their SIDL says (their
// OnProcessFrame), for the windows we put up:
//   - CPlayerWnd::UpdatePlayerWindowState (@0042ee3a): the pet's HP gauge only while we have a pet.
//   - CGroupWnd::UpdateButtons (@0041f06e): Invite and Disband, with Follow and Decline (for an
//     invitation) hidden where they overlap them; Invite only with another player targeted, Disband
//     only in a group; a member's pet gauge only while that member has a pet. We have no group yet.
//   - CBuffWindow::RefreshIconArrangement and RefreshBuffDisplay (@004090e9, @00409334): the buff
//     buttons stacked in columns from the top right, as many as fit (the rest hidden), and an empty
//     slot drawing nothing (SetBuffIcon clears its picture). We don't know our buffs yet.
//   - CHotButtonWnd::OnProcessFrame (@0041fae9): a hot button shows the text of the button it
//     holds (the Actions window's, or a social's name); an empty one has no text and is checked,
//     and the item slots and spell gems are hidden (we have no item or spell hot buttons yet).
//   - CHotButtonWnd::Activate_HotkeyPage (@00420969): the page number between the arrows.
//   - CActionsWnd::MainPageOnProcessFrame (@00403213): of the main page's buttons that share a place,
//     Follow for an invitation (else Invite), Stand while sitting (else Sit), Walk while running
//     (else Run); Disband only in a group or invited, Invite only with another player targeted.
// And what their page arrows do: CActionsWnd::SocialsPageWndNotification (@00403a68) and
// CHotButtonWnd::WndNotification (@00420462) step through ten pages, round from the last to the first.
//
// Hot buttons, and the button on the cursor (CCursorAttachment):
//   - holding a button on any of the Actions window's pages puts it on the cursor (the pages'
//     WndNotification on 0x16, CCursorAttachment::Activate @00418abb);
//   - holding a hot button takes what's on it onto the cursor, when the cursor has nothing;
//   - clicking a hot button with a button on the cursor puts it there, and whatever was there
//     onto the cursor; without one, it does what the button it holds does (DoHotButton @004209bd);
//   - clicking the world or pressing Escape drops it (CEverQuest_LMouseUp, CDisplay_NewUIProcessEscape
//     through CCursorAttachment::RemoveAttachment @00419474).
// They're kept in <character>_<server>.ini's [HotButtons] as eqgame's CEverQuest_saveCustom and
// loadCustomFromINI (@00548348, @0054870d) keep them: Page<p>Button<b>=<type letter, from A><value>.
//
// Socials (ten pages of twelve, @007d02f0), each a name, a color and five lines of commands:
//   - CEverQuest_initCustom (@00548faf): the first page's are eqstr 6652..6663 ("Afk"...), each
//     with one line, 6664..6675 ("/afk"...); the ini's take their places;
//   - CActionsWnd::SocialsPageOnProcessFrame (@004039f4) and CHotButtonWnd::OnProcessFrame: a
//     social's button shows its name, in the TextColor of the editor's SEW_TC<color>Button;
//   - clicking one on the Actions window starts it, over one that's going
//     (SocialsPageWndNotification); a hot button starts it only when none is (DoHotButton);
//   - right clicking either opens CSocialEditWnd (@00432c80, Activate @00432e1f) on it: the name
//     (15 characters), the lines (255 each) and the color buttons; Accept keeps them
//     (SaveSocialCommandData @00432efc) and closes it, Clear empties the boxes;
//   - EQ_Character::DoPassageOfTime (@004c131d) does the one going, a step every 101 ms or more:
//     a wait to count down, or the next line that isn't empty, through InterpretCmd, and a step's
//     wait after it. A line from "/pau" on waits its number of steps (1..600, from its eighth
//     character on), doing what's after its first comma, if anything is.
// They're kept in [Socials]: Page<p>Button<b>Name, ...Color and ...Line1..5, written only for the
// ones changed since (an empty name takes them all out); the names are cut to 8 as they're read.
//
// The inventory window (CInventoryWnd, @0042128f on):
//   - Activate: ClassAnim shows A_ClassAnim<class, 2 digits>;
//   - ProcessInventoryWindowFrame: the money buttons' counts (UpdateCurrencyDisplays), and the
//     Alt. Adv. label and gauge only from level 51;
//   - CInvSlot::SetItem: a slot with an item shows its icon, A_DragItem's cell icon - 500;
//   - WndNotification: Done closes it, Skills opens the skills window. Destroy, Alt. Advance and
//     the money buttons (coins onto the cursor) do nothing yet.
// The skills window (CSkillsWnd, @0043273a on): Activate lists the skills we have, by number,
// then the languages we know some of; UpdateSkill puts each one's name, rank and value in the list
// (white, the headings 0x8aa3ff, rows 16 high, the rank and value justified 1). We go by what
// the server sends: a skill (0..73) that isn't 254 or 255; eqgame's EQ_Character::HasSkill asks
// its class and level table, and also lists racial skills (a halfling's sneak) the server marks 255.
//
// The item display (CItemDisplayWnd, @00423331 on), on the item in an inventory slot:
//   - CInvSlot::HandleLButtonUp (@00421e48): a click with Alt down shows it, and does nothing else;
//   - CInvSlotWnd::HandleRButtonHeld (@005a7f30, DisplayItemIfValid @00422987): the right button
//     held on a slot shows it, and HandleRButtonUpAfterHeld (@005a7fc0,
//     CheckItemDisplayWindowAndActivat @004229cf) closes it when the button's let go over a slot;
//   - SetItem and Activate (@00423640, @00423606): the item's name for the title, its description
//     (item_display.h) in ItemDescription, its icon (A_DragItem's cell icon - 500) on IconButton,
//     and the window shown on top. It stays as it was shown until it's shown again.

#include <cstdint>
#include <string>

class Canvas;
class EqStr;
class GameValues;
struct ZoneItem;
class IniFile;
class TextRenderer;
class WindowManager;

// A hot button (eqgame's ten pages of ten @007f6860): which kind of button it holds, and which one.
struct HotButton
{
	enum Type : uint8_t
	{
		Empty,
		Combat,      // the combat page's Melee Attack (0) and Range Attack (1)
		CombatSkill, // the combat page's four skill buttons, 0..3
		Ability,     // the abilities page's six, 0..5
		Social,      // page * 12 + button
		Item,        // an inventory slot
		Action,      // the main page's Who, Invite, Disband, Camp, Sit, Run (0..5); 7.. the menu buttons
		Spell,       // a spell gem
		PetCommand,  // a pet window button
	};
	Type type = Empty;
	int value = -1;
};

constexpr int HotButtonPages = 10;
constexpr int HotButtonsPerPage = 10;

struct Social
{
	std::string name;
	std::string lines[5];
	int color = 0;        // which of the editor's SEW_TC<n>Button's colors (0..19)
	bool changed = false; // since saved (@007d0278)
};

constexpr int SocialPages = 10;
constexpr int SocialsPerPage = 12;
constexpr int SocialLines = 5;

// The game windows' own state: the page of socials the Actions window shows and the page of hot
// buttons, 0..9 (eqgame's globals @007f67f9 and __HotkeyPage), the hot buttons, the button on
// the cursor, and the socials.
struct GameWindowState
{
	int social_page = 0;
	int hot_button_page = 0;
	HotButton hot_buttons[HotButtonPages * HotButtonsPerPage];
	HotButton cursor;        // Empty: nothing on the cursor
	std::string cursor_text; // its text when it was picked up
	bool hot_buttons_changed = false; // since they were last saved

	Social socials[SocialPages * SocialsPerPage];
	bool socials_changed = false;
	// __CurrentSocial: the social going (-1: none), its next line, the steps to wait before it,
	// and when the last step was (@0063db18).
	int current_social = -1, social_line = 0, social_wait = 0;
	uint64_t social_step_ms = 0;
	// The social editor's: the social it's on, and the color picked.
	int edit_social = 0, edit_color = 0;

	void ClearCursor() { cursor = {}; cursor_text.clear(); } // CCursorAttachment::RemoveAttachment
};

// A button clicked in one of the windows: the page arrows and the hot buttons. A hot button that
// holds one of the Actions window's main page buttons returns that button's ScreenID, to be done
// as if it had been clicked there; otherwise "".
std::string GameWindowClick(WindowManager &windows, GameWindowState &state, const std::string &window, const std::string &control);
// A button held down: onto the cursor.
void GameWindowHold(WindowManager &windows, GameWindowState &state, const std::string &window, const std::string &control);
// A button right clicked: a social's opens the social editor on it.
void GameWindowRightClick(WindowManager &windows, GameWindowState &state, const std::string &window, const std::string &control);
// An inventory slot's item in the item display; false if the control isn't a slot with an item.
bool ShowItemDisplay(WindowManager &windows, const GameValues &values, const std::string &window, const std::string &control);
// The right button let go over a slot after holding it: the item display closes.
void GameWindowRightRelease(WindowManager &windows, const std::string &window, const std::string &control);
void UpdateGameWindows(WindowManager &windows, const GameValues &values, const GameWindowState &state);

// A skill's name (eqgame's __SkillDict, from InitializeSkillData), from eqstr (null: English).
std::string SkillName(const EqStr *eqstr, int skill);

// The button on the cursor, drawn at the mouse (x, y), over the windows and under the cursor.
void DrawCursorAttachment(Canvas &canvas, TextRenderer &text, const WindowManager &windows, const GameWindowState &state,
	int x, int y, uint64_t now_ms);

void LoadHotButtons(const IniFile &ini, GameWindowState &state);
void StoreHotButtons(IniFile &ini, const GameWindowState &state);

// The first page's socials, from eqstr (null: its English).
void DefaultSocials(GameWindowState &state, const EqStr *eqstr);
void LoadSocials(const IniFile &ini, GameWindowState &state);
void StoreSocials(IniFile &ini, GameWindowState &state); // the changed ones, which aren't any more

// The social going's step, if it's time for one: the line to do (InterpretCmd), or "".
std::string SocialStep(GameWindowState &state, uint64_t now_ms);
