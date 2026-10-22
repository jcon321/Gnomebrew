#pragma once

// SIDL ("Sony Interface Definition Language"): eqgame's XML-defined UI, loaded
// from uifiles/<skin>/EQUI.xml and the files it <Include>s. This covers the
// pieces character select and creation use: Ui2DAnimation, ButtonDrawTemplate,
// WindowDrawTemplate, Screen (nested too), Button, Label, StaticAnimation,
// Editbox and STMLbox, with AutoStretch anchors. Schema defaults come from
// uifiles/default/SIDL.xml.

#include "canvas.h"
#include "text.h"
#include "widgets.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

struct XmlNode;

struct SidlFrame
{
	std::string texture;
	Rect source;
	int hotspot_x = 0, hotspot_y = 0;
	int duration = 1000;
};

struct SidlAnimation
{
	std::vector<SidlFrame> frames;
	bool cycle = true;

	const SidlFrame *FrameAt(uint64_t now_ms) const;
};

struct SidlButtonTemplate
{
	std::string normal, pressed, flyby, disabled, pressed_flyby;
};

// WindowDrawTemplate border pieces, in CTAFrameDraw order.
enum SidlBorderPiece
{
	TopLeft, Top, TopRight, RightTop, Right, RightBottom, BottomRight, Bottom, BottomLeft, LeftTop, Left, LeftBottom,
	BorderPieceCount
};

struct SidlWindowTemplate
{
	std::string background; // texture, tiled over the client area
	std::string border[BorderPieceCount];
	std::string title_left, title_middle, title_right; // Titlebar, under the top border
	SidlButtonTemplate close_box, minimize_box;        // the title bar's buttons
};

struct SidlPiece
{
	std::string type; // element name: Screen, Button, Label, StaticAnimation, ...
	std::string item;
	std::string screen_id;
	int font = 3;
	bool relative = true;
	Rect rect;
	bool has_size = false;

	// AutoStretch: each edge sits an offset from the parent's left/top or right/bottom edge.
	bool auto_stretch = false;
	bool left_to_left = true, top_to_top = true, right_to_left = true, bottom_to_top = true;
	int left_offset = 0, top_offset = 0, right_offset = 0, bottom_offset = 0;
	std::string text;
	uint32_t text_color = 0xffffff;
	bool has_text_color = false;
	bool align_center = false;
	bool align_right = false;

	// Button
	bool checkbox = false;
	std::string radio_group;
	SidlButtonTemplate button;

	// StaticAnimation
	std::string animation;

	// Screen (and the Editbox and STMLbox frames)
	std::string draw_template;
	bool style_border = false;
	bool transparent = false; // no background
	bool titlebar = false;    // Style_Titlebar: the template's title bar with <Text> in it
	bool closebox = false;    // Style_Closebox, Style_Minimizebox: buttons in the title bar
	bool minimizebox = false;
	bool sizable = false;     // Style_Sizable: the border edges resize it
	std::vector<std::string> pieces;
};

class SidlTextures
{
public:
	explicit SidlTextures(std::string ui_dir) : m_dir(std::move(ui_dir)) {}
	const Image *Get(const std::string &name); // null when missing
	const std::vector<std::string> &Missing() const { return m_missing; }

private:
	std::string m_dir;
	std::map<std::string, std::string> m_files; // lowercase -> real name
	bool m_scanned = false;
	std::map<std::string, std::unique_ptr<Image>> m_cache;
	std::vector<std::string> m_missing;
};

class SidlDefinitions
{
public:
	// ui_dir is e.g. <EQ>/uifiles/default. Loads EQUI.xml and every include.
	bool Load(const std::string &ui_dir, std::string &error);

	const SidlAnimation *Animation(const std::string &name) const;
	const SidlWindowTemplate *WindowTemplate(const std::string &name) const;
	const SidlPiece *Piece(const std::string &item) const;
	const std::string &Directory() const { return m_dir; }
	const std::vector<std::string> &Warnings() const { return m_warnings; }

private:
	void LoadFile(const std::string &file);
	void ReadElement(const XmlNode &node);

	std::string m_dir;
	std::map<std::string, SidlAnimation> m_animations;
	std::map<std::string, SidlWindowTemplate> m_window_templates;
	std::map<std::string, SidlPiece> m_pieces;
	std::vector<std::string> m_warnings;
};

// SIDL font numbers index the fonts CDisplay::InitDDraw creates: Arial 10, 12, 14, 15, 16, 20 (1-based; 3 = default).
int SidlFontHeight(int font);

// A live window built from a <Screen>: a tree of controls (nested screens and
// their pieces) with absolute rects and state. Control 0 is the screen itself.
class SidlWindow
{
public:
	struct Control
	{
		const SidlPiece *def = nullptr;
		const SidlWindowTemplate *frame = nullptr; // DrawTemplate, for screens, edit boxes and STML boxes
		Rect rect;
		Rect client; // inside the border and title bar
		Rect title;  // the title bar, when it has one
		int parent = -1;
		std::vector<int> children;
		std::string text; // STMLbox: STML (<BR> line breaks; other tags are dropped)
		uint32_t text_color = 0xffffff;
		bool visible = true;
		bool enabled = true;
		bool checked = false;
		bool hover = false;
		bool pressed = false;
		size_t max_chars = 64; // Editbox
	};

	bool Build(const SidlDefinitions &defs, const std::string &screen_item, std::string &error);
	void Draw(Canvas &canvas, TextRenderer &text, SidlTextures &textures, uint64_t now_ms) const;
	// Returns the ScreenID of a button clicked by this event, or "". A click on an edit box focuses it.
	std::string OnMouse(const InputEvent &event);
	// Typing into the focused edit box; false if there's none (or it's hidden).
	bool OnKey(const InputEvent &event);

	Control *Find(const std::string &screen_id);
	const Control *Find(const std::string &screen_id) const;
	// Radio group handling: checks this one, unchecks the rest of its group.
	void Check(const std::string &screen_id);
	// CXWnd::MoveAndInvalidate: a control's new rect, with its children laid out again.
	void Move(const std::string &screen_id, const Rect &rect);
	void Move(const Rect &rect) { Place(0, rect); } // the window itself
	void Focus(const std::string &screen_id);
	bool Shown(const Control &control) const; // it and every parent visible
	const Rect &Bounds() const { return m_controls.front().rect; }
	const SidlPiece &Def() const { return *m_controls.front().def; }
	const Control &Root() const { return m_controls.front(); }
	Control &Root() { return m_controls.front(); }
	bool Empty() const { return m_controls.empty(); }

	// The title bar's close and minimize boxes (Style_Closebox, Style_Minimizebox), at its right
	// end, and how they look: the mouse over one, one held down.
	enum class TitleBox { None, Close, Minimize };
	Rect TitleBoxRect(TitleBox box) const; // empty when it has no such box
	TitleBox TitleBoxAt(int x, int y) const;
	void SetTitleBoxes(TitleBox hover, TitleBox pressed) { m_box_hover = hover; m_box_pressed = pressed; }
	// The border's thickness on each side (Style_Border), not counting the title bar.
	void BorderInsets(int &left, int &top, int &right, int &bottom) const;
	// Minimized (CXWnd::IsMinimized): just the title bar and border, no client area or pieces.
	void SetMinimized(bool minimized) { m_minimized = minimized; }
	bool Minimized() const { return m_minimized; }
	// A control under (x, y) that takes clicks (a shown button or edit box): clicks there don't drag.
	bool InteractiveAt(int x, int y) const;

private:
	int Add(const SidlPiece &piece, int parent);
	void Place(int index, const Rect &rect);
	void DrawControl(int index, Canvas &canvas, TextRenderer &text, SidlTextures &textures, uint64_t now_ms) const;
	void DrawFrame(const Control &control, Canvas &canvas, SidlTextures &textures, uint64_t now_ms) const;
	void DrawAnimation(Canvas &canvas, SidlTextures &textures, const std::string &name, const Rect &dest, uint64_t now_ms) const;
	void DrawTitleBox(Canvas &canvas, SidlTextures &textures, TitleBox box, const SidlButtonTemplate &look, uint64_t now_ms) const;
	Rect AnimationSize(const std::string &name) const; // the first frame's size, at 0, 0

	const SidlDefinitions *m_defs = nullptr;
	std::vector<Control> m_controls;
	int m_focus = -1;
	bool m_minimized = false;
	TitleBox m_box_hover = TitleBox::None, m_box_pressed = TitleBox::None;
};
