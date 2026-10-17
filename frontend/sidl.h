#pragma once

// SIDL ("Sony Interface Definition Language"): eqgame's XML-defined UI, loaded
// from uifiles/<skin>/EQUI.xml and the files it <Include>s. This covers the
// pieces character select uses: Ui2DAnimation, ButtonDrawTemplate,
// WindowDrawTemplate, Screen, Button, Label and StaticAnimation.
// Schema defaults come from uifiles/default/SIDL.xml.

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

	// Screen
	std::string draw_template;
	bool style_border = false;
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

// A live window built from a <Screen>: child controls with absolute rects and state.
class SidlWindow
{
public:
	struct Control
	{
		const SidlPiece *def = nullptr;
		Rect rect;
		std::string text;
		bool visible = true;
		bool enabled = true;
		bool checked = false;
		bool hover = false;
		bool pressed = false;
	};

	bool Build(const SidlDefinitions &defs, const std::string &screen_item, std::string &error);
	void Draw(Canvas &canvas, TextRenderer &text, SidlTextures &textures, uint64_t now_ms) const;
	// Returns the ScreenID of a button clicked by this event, or "".
	std::string OnMouse(const InputEvent &event);

	Control *Find(const std::string &screen_id);
	// Radio group handling: checks this one, unchecks the rest of its group.
	void Check(const std::string &screen_id);
	const Rect &Bounds() const { return m_rect; }

private:
	void DrawAnimation(Canvas &canvas, SidlTextures &textures, const std::string &name, const Rect &dest, uint64_t now_ms) const;

	const SidlDefinitions *m_defs = nullptr;
	const SidlPiece *m_screen = nullptr;
	const SidlWindowTemplate *m_template = nullptr;
	Rect m_rect;
	Rect m_client;
	std::vector<Control> m_controls;
};
