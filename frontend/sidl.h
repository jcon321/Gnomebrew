#pragma once

// SIDL ("Sony Interface Definition Language"): eqgame's XML-defined UI, loaded
// from uifiles/<skin>/EQUI.xml and the files it <Include>s. This covers the
// pieces character select, creation and the in-game windows use so far:
// Ui2DAnimation, ButtonDrawTemplate, WindowDrawTemplate (with its vertical
// scrollbar), Screen (nested too), Button, Label, StaticAnimation, Editbox,
// STMLbox, Gauge, and the empty look of SpellGem and InvSlot, with AutoStretch
// anchors. Schema defaults come from uifiles/default/SIDL.xml.

#include "canvas.h"
#include "text.h"
#include "widgets.h"

#include <functional>
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

// ScrollbarDrawTemplate: the arrow buttons, the thumb's three pieces and the gutter between.
struct SidlScrollbarTemplate
{
	SidlButtonTemplate up, down;
	std::string thumb_top, thumb_middle, thumb_bottom;
	std::string gutter;            // MiddleTextureInfo, a texture
	uint32_t gutter_tint = 0xffffff;
};

struct SidlWindowTemplate
{
	std::string background; // texture, tiled over the client area
	SidlScrollbarTemplate vscroll; // VSBTemplate
	std::string border[BorderPieceCount];
	std::string title_left, title_middle, title_right; // Titlebar, under the top border
	SidlButtonTemplate close_box, minimize_box;        // the title bar's buttons
};

// GaugeDrawTemplate: animations, each drawn at its own size.
struct SidlGaugeTemplate
{
	std::string background, fill, lines, lines_fill, cap_left, cap_right;
};

struct SidlPiece
{
	std::string type; // element name: Screen, Button, Label, StaticAnimation, ...
	std::string item;
	std::string screen_id;
	int eq_type = 0;  // EQType: which of the game's values a gauge or label shows (0 none)
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

	// InvSlot: the empty slot's picture (Background). SpellGem: SpellGemDrawTemplate.
	std::string slot_background;
	std::string gem_holder, gem_background, gem_highlight;

	// Gauge: the text (TextOffset) and the bar (GaugeOffset) from the client area's corner.
	int text_offset_x = 0, text_offset_y = 0, gauge_offset_x = 0, gauge_offset_y = 0;
	uint32_t fill_tint = 0xffffff, lines_fill_tint = 0xffffff;
	bool draw_lines_fill = false;
	SidlGaugeTemplate gauge;

	// Screen (and the Editbox and STMLbox frames)
	std::string draw_template;
	bool style_border = false;
	bool transparent = false; // no background
	bool titlebar = false;    // Style_Titlebar: the template's title bar with <Text> in it
	bool closebox = false;    // Style_Closebox, Style_Minimizebox: buttons in the title bar
	bool minimizebox = false;
	bool sizable = false;     // Style_Sizable: the border edges resize it
	bool vscroll = false;     // Style_VScroll: the template's vertical scrollbar down the right
	std::vector<std::string> pieces;
};

// The game's values behind gauges and labels with an EQType (eqgame's fn_GetGaugeLabelFromEQ and
// fn_GetLabelFromEQ), supplied by whoever puts the windows up.
class SidlValues
{
public:
	virtual ~SidlValues() = default;
	// 0..1000 full, with the text to show on it (empty: the gauge's own). -1 hides the gauge, -3
	// draws it empty, -2 is a full dark red bar (no character); below -9, a purple bar of -value.
	virtual int Gauge(int eq_type, std::string &text) = 0;
	// The label's text; false keeps its own.
	virtual bool Label(int eq_type, std::string &text) = 0;
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

// SIDL font numbers index the seven fonts CDisplay::InitNewUI makes (0..6): Arial 10, 12, 14, 15, 16, 20
// and 24, the heights Zeal's CreateFontIndirectA hook sees for each (3, 15 pixels, is the default).
// eqgfx's font table (@10073c30) asks weight 100 for 0..4, which Arial draws as its regular face,
// and 700 (bold) for 5 and 6.
int SidlFontHeight(int font);
int SidlFontWeight(int font);

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
		// A button drawing none of its ButtonDrawTemplate, just its text (an empty buff slot:
		// CBuffWindow::SetBuffIcon clears the template it draws).
		bool blank = false;
		size_t max_chars = 64; // Editbox
		// Style_VScroll: the content's scroll position in rows, how far it goes, and a page of it.
		// Whoever fills the control sets max and page; the scrollbar, the wheel and the owner move pos.
		int scroll_pos = 0, scroll_max = 0, scroll_page = 1;
		// Draws the content of a control the window doesn't fill itself (a chat window's text),
		// inside ContentRect, after the frame and under the children.
		std::function<void(Canvas &, TextRenderer &, const Rect &)> draw_content;
		// A gauge's bar slides to a new value at 1000 a second (CGaugeWnd::Draw), while its text stays.
		mutable float gauge_shown = 0;
		mutable std::string gauge_text;
		mutable uint64_t gauge_ms = 0;
	};

	bool Build(const SidlDefinitions &defs, const std::string &screen_item, std::string &error);
	void SetValues(SidlValues *values) { m_values = values; }
	void Draw(Canvas &canvas, TextRenderer &text, SidlTextures &textures, uint64_t now_ms) const;
	// Returns the ScreenID of a button clicked by this event, or "". A click on an edit box focuses it.
	std::string OnMouse(const InputEvent &event);
	// Typing into the focused edit box; false if there's none (or it's hidden).
	bool OnKey(const InputEvent &event);
	// The wheel over a scrolling control: three rows a notch. False if there's none under (x, y).
	bool OnWheel(int x, int y, int notches);

	Control *Find(const std::string &screen_id);
	const Control *Find(const std::string &screen_id) const;
	// Radio group handling: checks this one, unchecks the rest of its group.
	void Check(const std::string &screen_id);
	// CXWnd::MoveAndInvalidate: a control's new rect, with its children laid out again.
	void Move(const std::string &screen_id, const Rect &rect);
	void Move(const Rect &rect) { Place(0, rect); } // the window itself
	void Focus(const std::string &screen_id); // "" (or a missing one): nothing
	bool Focused(const std::string &screen_id) const;
	// Where a control's content goes: its client area, less the scrollbar.
	Rect ContentRect(const Control &control) const;
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
	// The window's border piece (SidlBorderPiece) under (x, y), as drawn; -1 for none.
	int BorderPieceAt(int x, int y) const;
	// Minimized (CXWnd::IsMinimized): just the title bar and border, no client area or pieces.
	void SetMinimized(bool minimized) { m_minimized = minimized; }
	bool Minimized() const { return m_minimized; }
	// A control under (x, y) that takes clicks (a shown button, edit box or scrollbar): clicks
	// there don't drag.
	bool InteractiveAt(int x, int y) const;
	// (x, y) is on the control where it's drawn: inside it and its parents' client areas (the
	// part of a control outside its parent is cut off, so it can't be clicked either).
	bool HitTest(const Control &control, int x, int y) const;

private:
	int Add(const SidlPiece &piece, int parent);
	void Place(int index, const Rect &rect);
	void DrawControl(int index, Canvas &canvas, TextRenderer &text, SidlTextures &textures, uint64_t now_ms,
		const Rect &parent_clip) const;
	void DrawFrame(const Control &control, Canvas &canvas, SidlTextures &textures, uint64_t now_ms) const;
	void LayoutBorder(const Control &control, uint64_t now_ms, Rect (&pieces)[BorderPieceCount]) const;
	void DrawAnimation(Canvas &canvas, SidlTextures &textures, const std::string &name, const Rect &dest, uint64_t now_ms) const;
	void DrawTitleBox(Canvas &canvas, SidlTextures &textures, TitleBox box, const SidlButtonTemplate &look, uint64_t now_ms) const;
	Rect AnimationSize(const std::string &name) const; // the first frame's size, at 0, 0
	void DrawGauge(const Control &control, Canvas &canvas, TextRenderer &text, SidlTextures &textures, uint64_t now_ms) const;

	// The vertical scrollbar's parts, for a control that has one.
	enum class ScrollPart { None, Up, Down, PageUp, PageDown, Thumb };
	struct ScrollLayout
	{
		Rect bar, up, down, track, thumb;
	};
	bool HasScrollbar(const Control &control) const;
	ScrollLayout LayoutScrollbar(const Control &control) const;
	ScrollPart ScrollPartAt(const Control &control, int x, int y) const;
	void DrawScrollbar(const Control &control, Canvas &canvas, SidlTextures &textures, uint64_t now_ms) const;
	void ScrollBy(Control &control, int rows);

	const SidlDefinitions *m_defs = nullptr;
	SidlValues *m_values = nullptr;
	std::vector<Control> m_controls;
	int m_focus = -1;
	// A scrollbar part held down: which control, which part; for the thumb, where it was grabbed.
	int m_scroll_control = -1;
	ScrollPart m_scroll_part = ScrollPart::None;
	int m_scroll_grab_y = 0, m_scroll_grab_pos = 0;
	int m_scroll_hover = -1;
	ScrollPart m_scroll_hover_part = ScrollPart::None;
	bool m_minimized = false;
	TitleBox m_box_hover = TitleBox::None, m_box_pressed = TitleBox::None;
};
