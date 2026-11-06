#pragma once

// SIDL ("Sony Interface Definition Language"): eqgame's XML-defined UI, loaded
// from uifiles/<skin>/EQUI.xml and the files it <Include>s. This covers the
// pieces character select, creation and the in-game windows use so far:
// Ui2DAnimation, ButtonDrawTemplate, WindowDrawTemplate (with its vertical
// scrollbar), FrameTemplate, Screen (nested too), TabBox and its Pages, Button,
// Label, StaticAnimation, Editbox, STMLbox, Gauge, Listbox, Combobox, InvSlot, Slider, and the empty
// look of SpellGem, with AutoStretch anchors. Schema defaults come from uifiles/default/SIDL.xml.

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
	// Grid: each frame is a sheet of CellWidth x CellHeight cells (the item icons, A_DragItem),
	// numbered down the columns when Vertical, else across the rows.
	bool grid = false, vertical = false;
	int cell_width = 0, cell_height = 0;

	const SidlFrame *FrameAt(uint64_t now_ms) const;
	// CTextureAnimation::SetCurCell: a grid's cell, counting on through the frames; one past them
	// all is cell 0. False when it isn't a grid or cell is below 0.
	bool Cell(int cell, SidlFrame &out) const;
};

struct SidlButtonTemplate
{
	std::string normal, pressed, flyby, disabled, pressed_flyby;
	std::string normal_decal; // NormalDecal: drawn over the button at DecalOffset (a coin)
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

// FrameTemplate: a border on its own (a TabBox's tabs and page), in CTAFrameDraw order, and how
// far the inside reaches back over it on each side.
struct SidlFrameTemplate
{
	std::string border[BorderPieceCount];
	int overlap_left = 0, overlap_top = 0, overlap_right = 0, overlap_bottom = 0;
};

// GaugeDrawTemplate: animations, each drawn at its own size.
struct SidlGaugeTemplate
{
	std::string background, fill, lines, lines_fill, cap_left, cap_right;
};

// SliderDrawTemplate: the thumb's looks, and the bar's background between its end caps.
struct SidlSliderTemplate
{
	SidlButtonTemplate thumb;
	std::string background, cap_left, cap_right;
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
	std::string tooltip; // TooltipReference
	uint32_t text_color = 0xffffff;
	bool has_text_color = false;
	bool align_center = false;
	bool no_wrap = false; // NoWrap: a label's text on one line, however long (false: wrapped at words)
	bool align_right = false;

	// Button
	bool checkbox = false;
	std::string radio_group;
	SidlButtonTemplate button;
	Rect decal; // DecalOffset and DecalSize; no size: the decal's own

	// Listbox: its Columns' headings and widths.
	struct Column
	{
		std::string heading;
		int width = 0;
	};
	std::vector<Column> columns;

	// Combobox (CComboWnd): its Choices, how tall its list may grow (ListHeight), and its button's
	// ButtonDrawTemplate (Button, a template's item name).
	std::vector<std::string> choices;
	int list_height = 0;
	std::string combo_button;

	// StaticAnimation
	std::string animation;

	// Slider: its SliderDrawTemplate (SliderArt).
	std::string slider_art;

	// InvSlot: the empty slot's picture (Background). SpellGem: SpellGemDrawTemplate, and where
	// the icon goes in it (SpellIconOffsetX, Y).
	std::string slot_background;
	std::string gem_holder, gem_background, gem_highlight;
	int gem_icon_x = 0, gem_icon_y = 0;

	// Gauge: the text (TextOffset) and the bar (GaugeOffset) from the client area's corner.
	int text_offset_x = 0, text_offset_y = 0, gauge_offset_x = 0, gauge_offset_y = 0;
	uint32_t fill_tint = 0xffffff, lines_fill_tint = 0xffffff;
	bool draw_lines_fill = false;
	SidlGaugeTemplate gauge;

	// Page: its tab's text and icon, and their colors and icons on the current tab.
	std::string tab_text, tab_icon, tab_icon_active;
	uint32_t tab_text_color = 0xffffff, tab_text_active_color = 0xffffff;
	// TabBox: the borders of its tabs and of the page under them, and its Pages.
	std::string tab_border, page_border;
	std::vector<std::string> pages;

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
	// The label's text; false keeps its own. `color` comes in as the label's and can be changed
	// (fn_GetLabelFromEQ's color: a stat above its base green, below it red).
	virtual bool Label(int eq_type, std::string &text, uint32_t &color) = 0;
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
	// Our own windows' definitions, after Load: an XML document as the UI files have, its pieces
	// drawn with the loaded templates and animations. `name` is for the warnings.
	void LoadText(const std::string &xml, const std::string &name);

	const SidlAnimation *Animation(const std::string &name) const;
	const SidlWindowTemplate *WindowTemplate(const std::string &name) const;
	const SidlFrameTemplate *FrameTemplate(const std::string &name) const;
	const SidlSliderTemplate *SliderTemplate(const std::string &name) const;
	const SidlButtonTemplate *ButtonTemplate(const std::string &name) const; // a named ButtonDrawTemplate
	const SidlPiece *Piece(const std::string &item) const;
	const std::string &Directory() const { return m_dir; }
	const std::vector<std::string> &Warnings() const { return m_warnings; }

private:
	void LoadFile(const std::string &file);
	void ReadElement(const XmlNode &node);

	std::string m_dir;
	std::map<std::string, SidlAnimation> m_animations;
	std::map<std::string, SidlWindowTemplate> m_window_templates;
	std::map<std::string, SidlFrameTemplate> m_frame_templates;
	std::map<std::string, SidlSliderTemplate> m_slider_templates;
	std::map<std::string, SidlButtonTemplate> m_button_templates;
	std::map<std::string, SidlPiece> m_pieces;
	std::vector<std::string> m_warnings;
};

// SIDL font numbers index the seven fonts CDisplay::InitNewUI makes (0..6): Arial 10, 12, 14, 15, 16, 20
// and 24, the heights Zeal's CreateFontIndirectA hook sees for each (3, 15 pixels, is the default).
// eqgfx's font table (@10073c30) asks weight 100 for 0..4, which Arial draws as its regular face,
// and 700 (bold) for 5 and 6.
int SidlFontHeight(int font);
int SidlFontWeight(int font);

// A button's text, centered in rect; text wider than it breaks between words onto more lines
// (a hot button's "Melee Attack").
void SidlDrawButtonText(TextRenderer &text, Canvas &canvas, const Rect &rect, const std::string &str, int height,
	uint32_t rgb, int weight);

// A live window built from a <Screen>: a tree of controls (nested screens and
// their pieces) with absolute rects and state. Control 0 is the screen itself.
class SidlWindow
{
public:
	struct StmlRun
	{
		std::string text;
		uint32_t color;
	};

	struct Control
	{
		const SidlPiece *def = nullptr;
		const SidlWindowTemplate *frame = nullptr; // DrawTemplate, for screens, edit boxes and STML boxes
		Rect rect;
		Rect client; // inside the border and title bar
		Rect title;  // the title bar, when it has one
		int parent = -1;
		std::vector<int> children;
		std::string text; // STMLbox: STML (<BR> line breaks, <c "#RRGGBB"> colors; other tags are dropped)
		// What shows when the mouse rests on it (CXWnd's ToolTipText): its TooltipReference to start
		// with (CSidlManager_CreateXWndFromTemplate), and whatever the window sets (an item's name).
		std::string tooltip;
		uint32_t text_color = 0xffffff;
		bool visible = true;
		bool enabled = true;
		bool checked = false; // a checkbox's, or the selected InvSlot (CInvSlotWnd's 0x114)
		bool hover = false;
		bool pressed = false;
		// A button drawing none of its ButtonDrawTemplate, just its text (an empty buff slot:
		// CBuffWindow::SetBuffIcon clears the template it draws).
		bool blank = false;
		// A screen that takes clicks as a button does (the inventory window's character view, while
		// an item's on the cursor: CInventoryWnd::HandleLButtonUp).
		bool clickable = false;
		size_t max_chars = 64; // Editbox
		uint32_t link_color = 0xff00ff; // Editbox: its item tags' (item_tags.h)
		std::string animation; // StaticAnimation: another animation in place of its own; "" its own
		// InvSlot: what's in it, a cell of a grid animation (an item's icon, A_DragItem), over the
		// slot's picture; cell < 0 nothing. A Button with one draws that cell in place of its
		// template and decal (the item display's icon). A Button whose NormalDecal is a grid
		// animation (a buff's BuffIcons) shows that decal's cell the same way.
		std::string cell_animation;
		int cell = -1;
		// A Button's pictures as CBuffWindow::SetBuffIcon sets a spellbook slot's: `background` drawn
		// in place of its template's ("" the template's), none of them with `no_background`, and the
		// decal's animation in place of NormalDecal (A_SpellIcons) with `cell` its cell.
		std::string background, decal;
		bool no_background = false;
		uint32_t decal_tint = 0xffffff; // the decal's color (CButtonWnd's 0x11c: a color picker's swatch)
		// A Button's own opacity, 0..255 (CBuffWindow::RefreshBuffDisplay sets a buff's UnfadedAlpha
		// as it blinks out).
		int alpha = 255;
		// A screen's selection lasso (CXWnd_DrawLasso): a one-pixel box of this color (0xRRGGBB)
		// round its client area, over everything in it; 0 none (CPlayerWnd::DrawSelectionLasso).
		uint32_t lasso = 0;
		// SpellGem (CSpellGemWnd): `cell` is the spell's icon in A_SpellGems (-1: an empty gem, the
		// Holder), `gem_color` its color (setGaugeColor: the Background is tinted by it), and
		// `gem_fade` how far it's dimmed, 0 ready .. 10 the darkest (the spell not ready to cast).
		uint32_t gem_color = 0;
		int gem_fade = 0;
		// InvSlot: how many of the item are in it, in a dark box at its corner (CInvSlotWnd::Draw);
		// 0 shows none.
		int count = 0;
		// InvSlot: the item slot it shows, as its window sets it (CInvSlotWnd's SidlScreen: a bag
		// window's slots are the bag's); -1 its EQType's, as the inventory's are.
		int item_slot = -1;
		int ItemSlot() const { return item_slot >= 0 ? item_slot : def->eq_type; }
		// CXWnd::Move by its window: it sits at `place` in its parent's client area, not its
		// Location (a bag window's Combine and Done under its last slot).
		bool placed = false;
		Rect place;
		// Slider (CSliderWnd): its value, 0 to ticks - 1. The mouse moves it; its owner reads it.
		int slider_value = 0, slider_ticks = 2;
		// Listbox (CListWnd): its rows' text by column, each column's justification (0 left, 1
		// centered, 2 right: SetColumnJustification's, which DrawHeader turns into text flags 0x10
		// and 8; that 1 centers is our reading, since the skills window's Value column would be cut
		// off right-justified), the rows' height (0: the font's), and the colors.
		std::vector<std::vector<std::string>> rows;
		std::vector<int> justification;
		int row_height = 0;
		uint32_t row_color = 0xffffff, header_color = 0xffffff;
		std::vector<uint32_t> row_colors; // a row's own, over row_color (0: row_color)
		bool header_hidden = false;       // no header row at all (a context menu's)
		// CListWnd style 0x10000: the row under the mouse in 0x150's color, if enabled (DrawItem).
		int hover_row = -1;
		uint32_t hover_color = 0xffff00;
		// The row clicked last, drawn highlighted, and the column the click was in
		// (CListWnd::GetCurCol); -1 none. A click on a row reports the list's ScreenID as a button's
		// would.
		int selected_row = -1;
		int selected_column = -1;
		// Its columns: the Listbox's own to start with; its window can add more.
		std::vector<SidlPiece::Column> columns;
		// Combobox: its choices (its Choices to start with), the one showing (-1 none:
		// CComboWnd::GetCurChoice), and its list dropped down under it. Picking one reports the
		// combo box's ScreenID as a button's click would.
		std::vector<std::string> choices;
		int choice = 0;
		bool list_open = false;
		// Style_VScroll: the content's scroll position in rows, how far it goes, and a page of it.
		// Whoever fills the control sets max and page (an STML box its own, as it wraps its text to
		// draw it); the scrollbar, the wheel and the owner move pos.
		int scroll_pos = 0;
		mutable int scroll_max = 0, scroll_page = 1;
		// STMLbox: its text wrapped into rows, for the text, width and color they were wrapped for.
		mutable std::vector<std::vector<StmlRun>> stml_rows;
		mutable std::string stml_text;
		mutable int stml_width = -1;
		mutable uint32_t stml_color = 0;
		int tab = 0; // TabBox: the page showing (its children are its pages)
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
	// CSliderWnd::SetValue: to 0..ticks - 1.
	void SetSlider(Control &slider, int value);

	Control *Find(const std::string &screen_id);
	Control &Item(int index) { return m_controls[index]; } // by index (a Control's children, parent)
	const Control *Find(const std::string &screen_id) const;
	// Radio group handling: checks this one, unchecks the rest of its group.
	void Check(const std::string &screen_id);
	// CTabWnd::SetActivePage: a TabBox shows its index'th page and hides the rest.
	void SelectTab(Control &tab_box, int index);
	// CXWnd::MoveAndInvalidate: a control's new rect, with its children laid out again.
	void Move(const std::string &screen_id, const Rect &rect);
	void Move(const Rect &rect) { Place(0, rect); } // the window itself
	// CXWnd::Move for a child: its top left at (x, y) in its parent's client area, kept there as
	// the window moves.
	void Reposition(const std::string &screen_id, int x, int y);
	void Focus(const std::string &screen_id); // "" (or a missing one): nothing
	bool Focused(const std::string &screen_id) const;
	bool Typing() const; // a shown, enabled edit box has the focus, and OnKey goes to it
	// Where a control's content goes: its client area, less the scrollbar.
	Rect ContentRect(const Control &control) const;
	// A list box's scroll range and page, from its rows and size; whoever fills it calls this.
	void UpdateListScroll(Control &control) const;
	// Where a list box's row is drawn (scrolled), as a press on it finds it (for tools).
	Rect ListRowRect(const Control &control, int row) const;
	bool Shown(const Control &control) const; // it and every parent visible
	const Rect &Bounds() const { return m_controls.front().rect; }
	const SidlPiece &Def() const { return *m_controls.front().def; }
	const Control &Root() const { return m_controls.front(); }
	Control &Root() { return m_controls.front(); }
	bool Empty() const { return m_controls.empty(); }

	// The title bar's close and minimize boxes (Style_Closebox, Style_Minimizebox), close at its
	// right end and minimize at its left, and how they look: the mouse over one, one held down.
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
	// CXWnd::DoAllDrawing's background: the template's texture tinted (BGType 1, DrawClientBackground's
	// DrawTexturedQuadWithAlpha), or the client area filled with the tint (2, DrawColoredRect).
	void SetBackground(uint32_t tint, bool texture)
	{
		m_bg_tint = tint & 0xffffff;
		m_bg_texture = texture;
	}
	bool Minimized() const { return m_minimized; }
	// A control under (x, y) that takes clicks (a shown button, slot, spell gem, edit box or scrollbar): clicks
	// there don't drag.
	bool InteractiveAt(int x, int y) const;
	// (x, y) is on the control where it's drawn: inside it and its parents' client areas (the
	// part of a control outside its parent is cut off, so it can't be clicked either).
	bool HitTest(const Control &control, int x, int y) const;
	// The ScreenID of the shown, enabled button (or slot or spell gem) under (x, y), or "".
	std::string ButtonAt(int x, int y) const;
	// CXWnd::GetChildWndAt for the tooltip: the shown control on top under (x, y), not the window
	// itself (a window's own TooltipReference never shows); null for none.
	const Control *ControlAt(int x, int y) const;
	// A held button let go of without being clicked (after CXWndManager's left button hold).
	void CancelPress();
	// Whether a combo box's list is down, and putting it away.
	bool ComboOpen() const { return OpenCombo() >= 0; }
	void CloseCombos();

private:
	int Add(const SidlPiece &piece, int parent);
	void Place(int index, const Rect &rect);
	void DrawControl(int index, Canvas &canvas, TextRenderer &text, SidlTextures &textures, uint64_t now_ms,
		const Rect &parent_clip) const;
	void DrawFrame(const Control &control, Canvas &canvas, SidlTextures &textures, uint64_t now_ms) const;
	void LayoutBorder(const Control &control, uint64_t now_ms, Rect (&pieces)[BorderPieceCount]) const;
	void DrawAnimation(Canvas &canvas, SidlTextures &textures, const std::string &name, const Rect &dest, uint64_t now_ms) const;
	void DrawAnimation(Canvas &canvas, SidlTextures &textures, const std::string &name, const Rect &dest, uint64_t now_ms,
		uint32_t tint) const;
	void DrawSpellGem(const Control &control, Canvas &canvas, SidlTextures &textures, uint64_t now_ms) const;
	void DrawTitleBox(Canvas &canvas, SidlTextures &textures, TitleBox box, const SidlButtonTemplate &look, uint64_t now_ms) const;
	Rect AnimationSize(const std::string &name) const; // the first frame's size, at 0, 0
	// CTAFrameDraw: where each of a border's pieces goes around `rect`, and the inside of it.
	void LayoutFrame(const std::string (&border)[BorderPieceCount], const Rect &rect, uint64_t now_ms,
		Rect (&pieces)[BorderPieceCount]) const;
	Rect FrameInside(const SidlFrameTemplate &frame, const Rect &rect) const;

	// CTabWnd: the tabs across the top of a TabBox and the page under them.
	int TabHeight(const Control &tab_box) const;
	Rect PageRect(const Control &tab_box) const;        // GetTabContentRect
	Rect TabRect(const Control &tab_box, int index) const; // CalculateTabRectangle
	int TabAt(const Control &tab_box, int x, int y) const; // GetTabPageFromMousePosition; -1 none
	void DrawTabBox(const Control &tab_box, Canvas &canvas, TextRenderer &text, SidlTextures &textures, uint64_t now_ms,
		const Rect &clip) const;
	void DrawTab(const Control &tab_box, int index, Canvas &canvas, TextRenderer &text, SidlTextures &textures,
		uint64_t now_ms, const Rect &clip) const;
	void DrawGauge(const Control &control, Canvas &canvas, TextRenderer &text, SidlTextures &textures, uint64_t now_ms) const;
	void DrawListbox(const Control &control, Canvas &canvas, TextRenderer &text) const;
	// CComboWnd: its button down the right (GetButtonRect), and the list under the rest of it
	// (GetListRect: as tall as half a line and a line a choice, ListHeight at most).
	Rect ComboButtonRect(const Control &combo) const;
	Rect ComboListRect(const Control &combo) const;
	void DrawCombo(const Control &combo, Canvas &canvas, TextRenderer &text, SidlTextures &textures, uint64_t now_ms) const;
	void DrawComboList(const Control &combo, Canvas &canvas, TextRenderer &text, SidlTextures &textures, uint64_t now_ms) const;
	int OpenCombo() const; // the combo box whose list is down, or -1
	std::string Press(const InputEvent &event); // OnMouse, but for the press's pick
	void ComboListInsets(const Control &combo, int &left, int &top, int &right, int &bottom) const; // its border
	void DrawStml(const Control &control, Canvas &canvas, TextRenderer &text) const;
	int ListHeaderHeight(const Control &control) const;
	int ListRowHeight(const Control &control) const;
	void DrawCell(Canvas &canvas, SidlTextures &textures, const std::string &name, int cell, const Rect &dest) const;

	// CSliderWnd's parts: the end caps, the bar between them (GetSliderBounds), the thumb on it.
	struct SliderLayout
	{
		Rect cap_left, cap_right, bar, thumb;
	};
	SliderLayout LayoutSlider(const Control &slider) const;
	void DrawSlider(const Control &slider, Canvas &canvas, SidlTextures &textures, uint64_t now_ms) const;
	void SlideTo(Control &slider, int x); // the value at x on the bar

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
	void Scrolled(Control &control); // its scroll position changed
	static constexpr int ScreenScrollUnit = 10; // a scrolling screen's step in pixels (ours)

	const SidlDefinitions *m_defs = nullptr;
	SidlValues *m_values = nullptr;
	std::vector<Control> m_controls;
	int m_focus = -1;
	// A scrollbar part held down: which control, which part; for the thumb, where it was grabbed.
	int m_scroll_control = -1;
	ScrollPart m_scroll_part = ScrollPart::None;
	int m_scroll_grab_y = 0, m_scroll_grab_pos = 0;
	int m_scroll_hover = -1;
	int m_slider = -1; // a slider being moved
	std::string m_pressed_pick; // a list row or combo choice picked by the press, for the release
	ScrollPart m_scroll_hover_part = ScrollPart::None;
	bool m_minimized = false;
	// CXWnd's 0x6c and 0x80: the client background's tint (BGTint), and whether it's the
	// template's texture (BGType 1) or the tint alone (2).
	uint32_t m_bg_tint = 0xffffff;
	bool m_bg_texture = true;
	TitleBox m_box_hover = TitleBox::None, m_box_pressed = TitleBox::None;
};
