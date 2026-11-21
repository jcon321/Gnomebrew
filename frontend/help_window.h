#pragma once

// eqgame's help window (EQUI_HelpWnd.xml's "HelpWindow"; its class is CHelpWnd, though TAKP's
// symbol list calls its constructor CContainerWnd::CContainerWnd @0041f57b):
//   - The constructor: Display (the STML box, +0x138), BackButton (+0x13c, disabled) and Contents
//     (+0x140).
//   - CDisplay_ActivateMainUI, each time the game's UI goes up: CHelpWnd_SetFile (@0041f6ef) of
//     "help\help.html": the name kept (+0x14c) and CStmlWnd_LoadPage of it.
//   - CStmlWnd_LoadPage (@005889b0): the file (EQfopen, the game's folder), a UTF-8 BOM skipped,
//     whole into the box with SetSTMLText (@00588e50) adding it to the box's history: the place
//     (+0x198) one on and the entries past it dropped (AddStmlHistoryEntry @00588be0); a file that
//     won't open changes nothing.
//   - A link clicked in the box (<a href="FILE:///...">, HandleMouseClickOnText @00587540 and
//     HandleStmlAction's 0) is LoadPage of the rest of its href.
//   - WndNotification (@0041f7c4, TAKP's "ScreenWindow_handle"), a click: BackButton is
//     NavigateBackInBrowserHistory (@00589080: the place one back and its text shown again,
//     SetSTMLTextWithoutHistory), then BackButton enabled while CanNavigateBackInHistory
//     (@00589100: the place above 0); Contents is LoadPage of the kept name.
//   - OnProcessFrame (@0041f85a): every 250 ms or more, BackButton enabled while it can go back.

#include <cstdint>
#include <string>
#include <vector>

class WindowManager;

class HelpWindow
{
public:
	static constexpr const char *Screen = "HelpWindow";
	static constexpr const char *File = "help\\help.html";

	void Attach(WindowManager &windows, const std::string &eq_dir); // the constructor's
	void SetFile(const std::string &file);                             // CHelpWnd_SetFile
	bool Click(const std::string &window, const std::string &control);
	void Frame(uint64_t now_ms); // OnProcessFrame
	int Place() const { return m_place; }
	size_t Pages() const { return m_history.size(); }

private:
	void LoadPage(const std::string &file); // CStmlWnd_LoadPage
	void Show(const std::string &stml);     // into the box, scrolled to the top (ResetStmlWindowState)
	void UpdateBack();

	WindowManager *m_windows = nullptr;
	std::string m_eq_dir;
	std::string m_file;                 // +0x14c
	std::vector<std::string> m_history; // the box's, +0x188
	int m_place = -1;                   // +0x198
	uint64_t m_checked_ms = 0;          // +0x148
};
