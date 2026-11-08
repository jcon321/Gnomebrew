#pragma once

// The in-game SIDL windows, managed the way eqgame's CXWndManager does it (@0059dcf0 on):
//   - a click brings a window to the top (BringWndToTop) before anything else;
//   - HandleLeftMouseDown: the title bar drags; so does anywhere else on a window that isn't
//     Style_Sizable, while a sizable one resizes from its border's pieces instead (corners both
//     ways), with the resize cursors over them; locked windows don't move; the title bar's close
//     and minimize boxes act when let go over them;
//   - ProcessMouseEvent and HandleLeftMouseHold (@00525db4, @0059fe70): the left button held
//     750 ms is a hold, sent to the button under the mouse then (CButtonWnd's HandleLButtonHeld,
//     @005953e0, notification 0x16), and letting go after it isn't a click;
//   - CXWnd::OnProcessFrame: a window that Fades starts fading to FadeToAlpha once the mouse
//     has been off it for Delay ms, and back to Alpha when it comes back, over Duration ms
//     (StartFade / ProcessTransition), everything in it drawn at that alpha (DoAllDrawing);
//   - minimizing leaves just the title bar (ProcessTransition 1 and 2, without the animation);
//   - HandleRButtonUp (@005a0440): the right button let go over a button it went down on is a
//     right click (notification 3), for its window to handle;
//   - HandleRightMouseHold and HandleRButtonUpAfterHeld (@0059ff70, @005a0060): the right button
//     held 750 ms is a hold, for the button it went down on if the mouse is still over it
//     (CInvSlotWnd's HandleRButtonHeld, @005a7f30), and letting go after it isn't a right click
//     but a release, for that button if the mouse is over it then (@005a7fc0);
//   - DrawWindows (@0059e000): once the mouse has been still 500 ms (HandleMouseMove's tick count),
//     the tooltip of the control under it (FindWnd, GetChildWndAt), over all the windows. In the
//     control's own font, as DrawTooltipAtPoint draws it: DrawWindows' own path uses the system
//     font (2), but the TAKP client shows an item's or a gem's in their font 3;
//   - CContextMenuManager (@00417ce6 on): popup menus over everything. A window's right button
//     going down off its buttons opens CreateDefaultMenu's window menu there (CXWnd::
//     HandleRButtonDown: CSidlScreenWnd::Init makes a screen's 0x12c the default menu). The mouse
//     on an item with a submenu opens it to the right of that row (HandleContextMenuEvent's 0x14,
//     closing any deeper); a click on another enabled item picks it (2: the window's
//     WndNotification 0x13, HandleContextMenuAction) and closes them all, and a press anywhere
//     else closes them.
// Layouts are kept in eqgame's UI_<character>_<server>.ini format, one [section] per window
// named after its screen (CSidlScreenWnd::LoadIniInfo / StoreIniInfo): XPos<W>x<H> and
// YPos<W>x<H> per resolution, Width and Height for sizable windows, Show, Locked, Fades, Delay,
// Duration, Alpha, FadeToAlpha and BGTint.

#include "retained_ui.h"
#include "sidl.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

class EqStr;
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
		uint32_t tint = 0xffffff; // BGTint: the client background's color
		bool texture = true;      // BGType 1, the template's texture tinted; 2 (false) the tint alone. Not kept.
	};

	// A button let go over in one of the windows.
	struct Click
	{
		std::string window; // its screen, e.g. "SelectorWindow"
		std::string control; // the button's ScreenID
		bool alt = false;    // Alt was down (GetKeyboardFlags' 4), for a click
		bool shift = false, ctrl = false; // and Shift and Ctrl (1 and 2)
	};

	// Forgets every window. The layout is for a screen this size.
	void Reset(const SidlDefinitions *defs, SidlTextures *textures, int screen_width, int screen_height);
	// Builds a window from its <Screen>, where the UI files put it (laid out for 640x480, so
	// moved for this resolution as LoadIniInfo's ConvertToRes does). Null if there's no such screen.
	// `name` (default: the screen's) is what the other calls, the clicks and the layout's
	// [section] know it by: one screen can be up more than once (a bag window per bag,
	// CContainerWnd's EnableIniStorage names: BagInv1...).
	SidlWindow *Add(const std::string &screen, bool shown, std::string *error = nullptr, const std::string &name = "");
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
	void Raise(const std::string &screen); // on top of the others (CXWnd::Show's bring to front)
	void MoveTo(const std::string &screen, int x, int y); // CXWnd::Move: its top left, the same size
	void Resize(const std::string &screen, int w, int h); // CXWnd::Resize: the same top left
	// CXWnd's 0x78: which of a sizable window's sides and corners size it (all by default; the
	// merchant, loot and options windows set top and bottom, the social editor the sides).
	enum ResizeEdge : uint8_t
	{
		ResizeTop = 0x01, ResizeTopRight = 0x02, ResizeBottomRight = 0x08, ResizeBottom = 0x10,
		ResizeBottomLeft = 0x20, ResizeSides = 0x40, ResizeTopLeft = 0x80, ResizeAll = 0xff,
	};
	void SetResizeEdges(const std::string &screen, uint8_t edges);
	Rect WindowRect(const std::string &screen) const;      // where it is unminimized; empty if there's none
	int ScreenWidth() const { return m_width; }
	int ScreenHeight() const { return m_height; }
	std::string ScreenAt(int x, int y) const; // the topmost shown window under (x, y), or ""
	std::string TopShown() const; // the topmost shown window (the last raised), or ""

	// Mouse events, in screen pixels. True when the windows took it (it's over one, or one is
	// being dragged); the world shouldn't see it then.
	bool OnMouse(const InputEvent &event, uint64_t now_ms);
	bool Over(int x, int y) const; // a shown window is under (x, y)
	bool Busy() const { return m_drag != Drag::None || m_press >= 0; }
	// Buttons clicked since the last call.
	std::vector<Click> TakeClicks();
	// Buttons held down (the left button held on them) since the last call.
	std::vector<Click> TakeHolds();
	// Buttons right clicked since the last call.
	std::vector<Click> TakeRightClicks();
	// Buttons the right button was held on, and let go of after a hold, since the last call.
	std::vector<Click> TakeRightHolds();
	std::vector<Click> TakeRightReleases();

	void Update(uint64_t now_ms, int mouse_x, int mouse_y); // the fades, and a button held (either button)
	void Draw(Canvas &canvas, TextRenderer &text, uint64_t now_ms);
	// The same drawing a piece at a time, in order (each shown window, then the menus and the
	// tooltip), for a RetainedUi; valid until the windows change.
	void Pieces(TextRenderer &text, uint64_t now_ms, std::vector<RetainedUi::Piece> &out);
	// The window that took longest in the last Draw, and how long (the debug menu's Performance).
	const std::string &SlowestWindow() const { return m_slowest; }
	static constexpr uint64_t MenusPiece = 1; // Pieces' id for the menus and tooltip
	double SlowestWindowMs() const { return m_slowest_ms; }
	// The cursor for the mouse at (x, y), if the windows want their own (moving or sizing one,
	// or over a sizable one's border): its SIDL animation, drawn at its hotspot. False otherwise.
	bool DrawCursor(Canvas &canvas, int x, int y, uint64_t now_ms) const;
	// A SIDL animation stretched over dest, for what's drawn outside the windows (the cursor's
	// attachment). False when there's no such animation.
	bool DrawAnimation(Canvas &canvas, const std::string &name, const Rect &dest, uint64_t now_ms) const;
	bool DrawCell(Canvas &canvas, const std::string &name, int cell, const Rect &dest) const; // a grid's cell
	// CXWnd::DrawTooltipAtPoint: `tip` in a SIDL font, white, on a box 4 wider and 2 taller than it
	// filled 0xc8000080 (navy, 200 of 255 opaque). At the mouse (x, y): 16 right of it, or left of
	// it when that runs off the screen (GetTooltipRect @005746f0); elsewhere its left is x, or its
	// right when it would run off (@00574750). Either way it's kept above the bottom.
	void DrawTooltip(Canvas &canvas, TextRenderer &text, const std::string &tip, int font, int x, int y,
		bool at_mouse) const;
	const SidlDefinitions *Definitions() const { return m_defs; }

	// The window menu's items (CreateDefaultMenu's ids, HandleContextMenuAction's cases).
	enum MenuId
	{
		MenuTexture = 10, MenuTint, MenuNormalAlpha, MenuFadedAlpha, MenuMinimize, MenuClose, MenuLock,
	};
	struct MenuPick
	{
		std::string window;
		int id = 0;
	};
	// The menu's screen (a WDT_RoundedNoTitle window with a list), into the UI's definitions.
	static void AddDefinitions(SidlDefinitions &defs);
	void SetStrings(const EqStr *strings) { m_strings = strings; } // the items' names
	// Picks the windows can't do alone (Tint Color: CColorPickerWnd; Normal and Faded Level:
	// CQuantityWnd), for whoever has those.
	std::vector<MenuPick> TakeMenuPicks();
	bool MenuOpen() const { return !m_open_menus.empty(); }
	void CloseMenus(); // CloseAllContextMenus
	// Where an open menu's row was last drawn (for tools); empty if there's no such row.
	Rect MenuRowRect(int level, int row) const;
	// A window's settings, and what the window menu's picks change in them (kept in the layout).
	const Settings *SettingsOf(const std::string &screen) const;
	void SetTint(const std::string &screen, uint32_t rgb);  // the color picker's 0x1e
	void SetAlpha(const std::string &screen, int alpha);    // Normal Level: UnfadedAlpha
	void SetFadeTo(const std::string &screen, int alpha);   // Faded Level: FadeToAlpha, fading on (Refade)

	int Count() const { return static_cast<int>(m_windows.size()); }

private:
	struct Window
	{
		std::string screen;
		SidlWindow wnd;
		bool shown = false;
		Rect rect;           // where it is unminimized
		Settings settings;
		uint8_t resize_edges = ResizeAll;
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
	Window *WindowNamed(const std::string &screen);

	// The menus (CContextMenu): their items, an item's submenu the index of another menu.
	struct MenuItem
	{
		std::string text;
		int id = 0;
		int submenu = -1;
		bool separator = false;
	};
	struct OpenMenu
	{
		int menu = 0;
		SidlWindow wnd;
		std::vector<bool> enabled;
		int parent_width = 0; // a submenu's parent's, for going left of it
	};
	void BuildMenus();
	// CContextMenu::Activate: the menu's top left at (x, y), kept on the screen; a submenu that would
	// run off the right goes left of its parent (`parent_width` wide) instead.
	void OpenMenuAt(int menu, int x, int y, int parent_width);
	bool MenuMouse(const InputEvent &event);
	void MenuAction(int id); // HandleContextMenuAction
	void DrawMenus(Canvas &canvas, TextRenderer &text, uint64_t now_ms);
	void LayoutMenu(OpenMenu &open, TextRenderer &text); // its columns and rect, from its text
	TextRenderer *m_text = nullptr; // the last Draw's, to lay a menu out as it opens
	std::string MenuText(uint32_t id, const char *fallback) const;
	std::vector<std::vector<MenuItem>> m_menus; // CreateDefaultMenu's: 0 Background's, 1 Alpha's, 2 the window menu
	std::vector<OpenMenu> m_open_menus;         // the menu, then its open submenus
	std::string m_menu_window;                  // the window the window menu is for
	std::vector<MenuPick> m_menu_picks;
	const EqStr *m_strings = nullptr;

	const SidlDefinitions *m_defs = nullptr;
	SidlTextures *m_textures = nullptr;
	SidlValues *m_values = nullptr;
	int m_width = 640, m_height = 480;
	std::vector<std::unique_ptr<Window>> m_windows; // back to front
	std::vector<Click> m_clicks;
	std::vector<Click> m_holds;
	std::vector<Click> m_right_clicks;
	std::vector<Click> m_right_holds;
	std::vector<Click> m_right_releases;
	Click m_right_press;      // the button the right button went down on
	uint64_t m_right_ms = 0;  // when
	bool m_right_down = false, m_right_held = false;
	bool m_changed = false;

	Drag m_drag = Drag::None;
	int m_drag_window = -1;
	int m_grab_x = 0, m_grab_y = 0;   // where the mouse went down
	Rect m_grab_rect;                 // the window's rect then
	bool m_edge_left = false, m_edge_top = false, m_edge_right = false, m_edge_bottom = false;
	int m_press = -1;                 // the window a button or title box went down in
	uint64_t m_press_ms = 0;          // when
	bool m_held = false;              // held long enough to be a hold, not a click
	SidlWindow::TitleBox m_box_down = SidlWindow::TitleBox::None;
	int m_mouse_x = -1, m_mouse_y = -1;
	uint64_t m_moved_ms = 0;          // when the mouse last moved (for the tooltips)
	std::string m_slowest;
	double m_slowest_ms = 0;
};
