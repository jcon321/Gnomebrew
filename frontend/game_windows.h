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
//   - CHotButtonWnd::OnProcessFrame (@0041fae9): an empty hot button has no text and its item slot
//     and spell gem are hidden. We don't know the hot buttons yet.
//   - CHotButtonWnd::Activate_HotkeyPage (@00420969): the page number between the arrows.
//   - CActionsWnd::MainPageOnProcessFrame (@00403213): of the main page's buttons that share a place,
//     Follow for an invitation (else Invite), Stand while sitting (else Sit), Walk while running
//     (else Run); Disband only in a group or invited, Invite only with another player targeted.
// And what their page arrows do: CActionsWnd::SocialsPageWndNotification (@00403a68) and
// CHotButtonWnd::WndNotification (@00420462) step through ten pages, round from the last to the first.

#include <string>

class GameValues;
class WindowManager;

// The page of socials the Actions window shows and the page of hot buttons, 0..9 (eqgame's
// globals @007f67f9 and __HotkeyPage).
struct GameWindowPages
{
	int social = 0;
	int hot_button = 0;
};

// A button clicked in one of the windows: the page arrows.
void GameWindowClick(WindowManager &windows, GameWindowPages &pages, const std::string &window, const std::string &control);
void UpdateGameWindows(WindowManager &windows, const GameValues &values, const GameWindowPages &pages);
