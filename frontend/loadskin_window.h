#pragma once

// eqgame's Load UI Skin window (EQUI_LoadskinWnd.xml's "LoadskinWnd", CLoadskinWnd):
//   - The constructor (@00425e1d): LoadButton (+0x138) disabled, INIButton (+0x13c, "Use INI")
//     checked, SkinList (+0x140) SetColors(white, yellow, gray) and its column left-justified (it
//     also clears the list's style bit 0x200000, which nothing of ours reads).
//   - Activate (@0042600d), from the Options window's Load UI Skin button and a bare /loadskin:
//     UpdateSkinList, then shown (+0x134 set).
//   - UpdateSkinList (@00425f6a): the selection cleared and the rows gone, then a row for each
//     folder FindFirstFileA(".\UIFiles\*.*") finds that doesn't start with '.'; LoadButton disabled.
//   - HandleWindowNotification (@00426032), a click: on SkinList, LoadButton enabled; on LoadButton,
//     /loadskin of "%s %c", the selected row's text and '1' or '0' as Use INI's checked
//     (`_loadskin`, which writes the UI ini's UISkin and has DoPassageOfTime reload the UI).

#include <functional>
#include <string>
#include <vector>

class WindowManager;

class LoadskinWindow
{
public:
	static constexpr const char *Screen = "LoadskinWnd";

	struct Host
	{
		std::function<std::vector<std::string>()> skins; // the uifiles folders
		std::function<void(const std::string &)> loadskin; // /loadskin's text
	};

	void Attach(WindowManager &windows, Host host); // the constructor's
	void Activate();
	bool Click(const std::string &window, const std::string &control);
	bool Active() const { return m_active; } // +0x134

private:
	void UpdateSkinList();

	WindowManager *m_windows = nullptr;
	Host m_host;
	bool m_active = false;
};
