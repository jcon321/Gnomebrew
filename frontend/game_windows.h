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

#include <cstdint>
#include <string>

class Canvas;
class GameValues;
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

// The game windows' own state: the page of socials the Actions window shows and the page of hot
// buttons, 0..9 (eqgame's globals @007f67f9 and __HotkeyPage), the hot buttons, and the button on
// the cursor.
struct GameWindowState
{
	int social_page = 0;
	int hot_button_page = 0;
	HotButton hot_buttons[HotButtonPages * HotButtonsPerPage];
	HotButton cursor;        // Empty: nothing on the cursor
	std::string cursor_text; // its text when it was picked up
	bool hot_buttons_changed = false; // since they were last saved

	void ClearCursor() { cursor = {}; cursor_text.clear(); } // CCursorAttachment::RemoveAttachment
};

// A button clicked in one of the windows: the page arrows and the hot buttons. A hot button that
// holds one of the Actions window's main page buttons returns that button's ScreenID, to be done
// as if it had been clicked there; otherwise "".
std::string GameWindowClick(WindowManager &windows, GameWindowState &state, const std::string &window, const std::string &control);
// A button held down: onto the cursor.
void GameWindowHold(WindowManager &windows, GameWindowState &state, const std::string &window, const std::string &control);
void UpdateGameWindows(WindowManager &windows, const GameValues &values, const GameWindowState &state);

// The button on the cursor, drawn at the mouse (x, y), over the windows and under the cursor.
void DrawCursorAttachment(Canvas &canvas, TextRenderer &text, const WindowManager &windows, const GameWindowState &state,
	int x, int y, uint64_t now_ms);

void LoadHotButtons(const IniFile &ini, GameWindowState &state);
void StoreHotButtons(IniFile &ini, const GameWindowState &state);
