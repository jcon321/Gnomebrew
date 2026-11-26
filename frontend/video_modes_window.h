#pragma once

// eqgame's Video Modes window (EQUI_VideoModesWnd.xml's "VideoModesWindow", CVideoModesWnd):
//   - The constructor (@0043b198, from InitializeGameUserInterface): VIDW_VideoResolutionsList
//     (+0x138) SetColors(white, yellow, gray) with a "Resolution" column 200 wide, the refresh rate
//     (+0x13c) and bit depth (+0x140) combo boxes SetColors the same, VIDW_AcceptButton (+0x144),
//     then Init.
//   - Init (@0043b408): the adapter's modes (eqgfx's mode count and mode calls) at least 640x480
//     in a 32-bit (A8R8G8B8 0x15, X8R8G8B8 0x16) or 16-bit (R5G6B5 0x17, X1R5G5B5 0x18) format,
//     one row a resolution ("%dx%d", its index the row's data) with up to 32 refresh rates and 4
//     formats each (CVideoModesWnd_AddVideoModeToAvailableResolutions @0043b2eb); for the row of
//     the mode we're in, the combo boxes filled and set to its refresh rate and bit depth, and the
//     list's selection on it.
//   - UpdateSelection (@0043b77a), a click on a row: the combo boxes refilled for its resolution,
//     a rate "%1Hz" (eqstr 6628) or 0 "Default" (6629), a format "32bit" (6627), "16bit(565)" (6626)
//     or "16bit(555)" (6625); their choices kept where they were.
//   - WndNotification (@0043b958), Accept: the row's size, the rate chosen and 32 or 16 bits
//     by the format chosen; the mode we're in kept (for CVideoModesWnd_RestoreOldMode), then
//     the change (FrontEnd::ChangeVideoMode).
//   - Activate (@0043b943): shown, +0x134 set; ExecuteCmd 106 (Toggle video mode window) toggles it.

#include <functional>
#include <string>
#include <vector>

class WindowManager;
class EqStr;

struct VideoMode
{
	int width = 0, height = 0;
	int refresh = 0; // Hz, 0 the adapter's default
	int format = 0x16; // D3DFORMAT: 0x15, 0x16 32-bit; 0x17 R5G6B5, 0x18 X1R5G5B5
	int BitsPerPixel() const { return format == 0x15 || format == 0x16 ? 32 : 16; }
};

class VideoModesWindow
{
public:
	static constexpr const char *Screen = "VideoModesWindow";

	struct Host
	{
		std::function<std::vector<VideoMode>()> modes; // the adapter's (none without a host)
		std::function<VideoMode()> current;           // the mode we're in
		std::function<void(const VideoMode &)> accept; // Accept's mode
	};

	void Attach(WindowManager &windows, const EqStr *eqstr, Host host); // the constructor's
	void Activate();
	void Deactivate();
	bool Active() const { return m_active; } // +0x134
	bool Click(const std::string &window, const std::string &control);

private:
	// One resolution of the table (DAT_00630e88's 0xa0-byte entries, 256 at most).
	struct Resolution
	{
		int width = 0, height = 0;
		std::vector<int> rates;   // up to 32
		std::vector<int> formats; // up to 4
	};

	void Init();
	void UpdateSelection(int resolution);
	void FillCombos(const Resolution &r);
	std::string RateText(int rate) const;
	std::string FormatText(int format) const;

	WindowManager *m_windows = nullptr;
	const EqStr *m_eqstr = nullptr;
	Host m_host;
	std::vector<Resolution> m_resolutions;
	bool m_active = false;
};
