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

class GameValues;
class WindowManager;

void UpdateGameWindows(WindowManager &windows, const GameValues &values);
