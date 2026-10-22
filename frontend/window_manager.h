#pragma once

// The in-game SIDL windows, managed the way eqgame's CXWndManager does it (@0059dcf0 on):
//   - a click brings a window to the top (BringWndToTop) before anything else;
//   - HandleLeftMouseDown: the title bar drags; so does anywhere else on a window that isn't
//     Style_Sizable, while a sizable one resizes from its border's pieces instead (corners both
//     ways), with the resize cursors over them; locked windows don't move; the title bar's close
//     and minimize boxes act when let go over them;
//   - CXWnd::OnProcessFrame: a window that Fades starts fading to FadeToAlpha once the mouse
//     has been off it for Delay ms, and back to Alpha when it comes back, over Duration ms
//     (StartFade / ProcessTransition), everything in it drawn at that alpha (DoAllDrawing);
//   - minimizing leaves just the title bar (ProcessTransition 1 and 2, without the animation).
// Layouts are kept in eqgame's UI_<character>_<server>.ini format, one [section] per window
// named after its screen (CSidlScreenWnd::LoadIniInfo / StoreIniInfo): XPos<W>x<H> and
// YPos<W>x<H> per resolution, Width and Height for sizable windows, Show, Locked, Fades, Delay,
// Duration, Alpha, FadeToAlpha and BGTint.

#include "sidl.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

class IniFile;

class WindowManager
{
public:
	// The defaults every window starts with (CXWndManager's global fade settings).
	struct Settings
	{
		bool fades = true;
		int delay = 2000;    // ms the mouse is away before it fades
		int duration = 500;  // ms the fade takes
		int alpha = 255;     // Alpha: how opaque it is unfaded
		int fade_to = 128;   // FadeToAlpha
		bool locked = false;
		uint32_t tint = 0xffffff; // BGTint (kept in the ini; not drawn yet)
	};

	// A button let go over in one of the windows.
	struct Click
	{
		std::string window; // its screen, e.g. "SelectorWindow"
		std::string control; // the button's ScreenID
	};

	// Forgets every window. The layout is for a screen this size.
	void Reset(const SidlDefinitions *defs, SidlTextures *textures, int screen_width, int screen_height);
	// Builds a window from its <Screen>, where the UI files put it (laid out for 640x480, so
	// moved for this resolution as LoadIniInfo's ConvertToRes does). Null if there's no such screen.
	SidlWindow *Add(const std::string &screen, bool shown, std::string *error = nullptr);
	SidlWindow *Find(const std::string &screen);
	// What gauges and labels with an EQType show, for every window (kept across Reset).
	void SetValues(SidlValues *values);

	// A layout from a UI_*.ini, for every window added so far; Store writes them back.
	void Load(const IniFile &ini);
	void Store(IniFile &ini) const;
	// The layout changed (moved, resized, shown, hidden) since the last call.
	bool TakeChanged();

	void Show(const std::string &screen, bool shown);
	void Toggle(const std::string &screen) { Show(screen, !Shown(screen)); }
	bool Shown(const std::string &screen) const;

	// Mouse events, in screen pixels. True when the windows took it (it's over one, or one is
	// being dragged); the world shouldn't see it then.
	bool OnMouse(const InputEvent &event, uint64_t now_ms);
	bool Over(int x, int y) const; // a shown window is under (x, y)
	bool Busy() const { return m_drag != Drag::None || m_press >= 0; }
	// Buttons clicked since the last call.
	std::vector<Click> TakeClicks();

	void Update(uint64_t now_ms, int mouse_x, int mouse_y); // the fades
	void Draw(Canvas &canvas, TextRenderer &text, uint64_t now_ms);
	// The cursor for the mouse at (x, y), if the windows want their own (moving or sizing one,
	// or over a sizable one's border): its SIDL animation, drawn at its hotspot. False otherwise.
	bool DrawCursor(Canvas &canvas, int x, int y, uint64_t now_ms) const;

	int Count() const { return static_cast<int>(m_windows.size()); }

private:
	struct Window
	{
		std::string screen;
		SidlWindow wnd;
		bool shown = false;
		Rect rect;           // where it is unminimized
		Settings settings;
		// The fade: alpha (AlphaTransparency) moving from fade_from to fade_target.
		bool unfaded = true;
		uint64_t hover_ms = 0; // when the mouse was last over it; 0 until its first frame
		int alpha = 255, fade_from = 255, fade_target = 255;
		uint64_t fade_start_ms = 0;
		int fade_ms = 0;
	};
	enum class Drag { None, Move, Resize };

	int TopAt(int x, int y) const; // index of the topmost shown window under (x, y), or -1
	// The sides a press at (x, y) on the window's border would size; false off the border.
	bool ResizeEdges(const Window &w, int x, int y, bool &left, bool &top, bool &right, bool &bottom) const;
	const char *Cursor(int x, int y) const; // the SIDL cursor animation, or null for the usual one
	void BringToTop(int &index);
	void Place(Window &w, const Rect &rect);
	void SetMinimized(Window &w, bool minimized);
	int EffectiveAlpha(const Window &w) const;
	Rect Clamped(const Rect &rect) const;

	const SidlDefinitions *m_defs = nullptr;
	SidlTextures *m_textures = nullptr;
	SidlValues *m_values = nullptr;
	int m_width = 640, m_height = 480;
	std::vector<std::unique_ptr<Window>> m_windows; // back to front
	std::vector<Click> m_clicks;
	bool m_changed = false;

	Drag m_drag = Drag::None;
	int m_drag_window = -1;
	int m_grab_x = 0, m_grab_y = 0;   // where the mouse went down
	Rect m_grab_rect;                 // the window's rect then
	bool m_edge_left = false, m_edge_top = false, m_edge_right = false, m_edge_bottom = false;
	int m_press = -1;                 // the window a button or title box went down in
	SidlWindow::TitleBox m_box_down = SidlWindow::TitleBox::None;
	std::vector<uint32_t> m_kept;     // Canvas::Keep, for drawing faded windows
};
