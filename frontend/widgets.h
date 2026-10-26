#pragma once

// Widgets modeled on eqmain.dll's UI elements. Everything is in the 640x480
// front-end coordinate space and redrawn every frame.

#include "canvas.h"
#include "text.h"

#include <cstdint>
#include <string>
#include <vector>

struct InputEvent
{
	// Key comes with each press (and auto-repeat), KeyUp when it's let go. FocusLost: the window
	// lost the keyboard, so every held key counts as released.
	enum class Type { MouseMove, MouseDown, MouseUp, DoubleClick, RightDown, RightUp, Wheel, Char, Key, KeyUp, FocusLost };
	enum class Key { None, Enter, Escape, Tab, Backspace, Up, Down, Left, Right, PageUp, PageDown, F9, F11, W, A, S, D, R, Space, Delete, Alt };

	Type type = Type::MouseMove;
	int x = 0, y = 0;     // mouse position, front-end pixels
	int wheel = 0;        // notches, positive = away from the user
	uint32_t ch = 0;      // Char: the character (Latin-1 range is all the login fields accept)
	Key key = Key::None;  // Key
	bool shift = false;
	bool ctrl = false;
	bool alt = false;
	bool IsMouse() const { return type != Type::Char && type != Type::Key && type != Type::KeyUp && type != Type::FocusLost; }
};

// eqmain colors, from the COLORREFs in the decompile.
namespace Colors
{
	constexpr uint32_t ButtonText = FromColorRef(0x000000);
	constexpr uint32_t Label = FromColorRef(0x021a3c);      // dark brown labels on parchment
	constexpr uint32_t Entry = FromColorRef(0x022d69);      // input and list text
	constexpr uint32_t Preferred = FromColorRef(0x006410);  // green
	constexpr uint32_t Red = FromColorRef(0x1818f0);
	constexpr uint32_t Blue = FromColorRef(0xc81000);
	constexpr uint32_t Light = FromColorRef(0xf3f2f2);
	constexpr uint32_t Overlay = FromColorRef(0xe8c9cc);
}

// Font heights from setFontForTextRendering / initializeGraphicsAndFonts.
namespace FontSize
{
	constexpr int Small = 12;
	constexpr int Normal = 14; // list and label font
	constexpr int Large = 25;  // buttons and input fields
}

struct ButtonArt
{
	const Image *normal = nullptr;
	const Image *over = nullptr;
	const Image *down = nullptr;
};

class Button
{
public:
	Rect rect;
	ButtonArt art;
	std::string label;
	int text_height = FontSize::Large;
	bool visible = true;

	void Place(int x, int y, const ButtonArt &button_art); // size comes from the art
	void Draw(Canvas &canvas, TextRenderer &text) const;
	// Feeds a mouse event; returns true when this completes a click.
	bool OnMouse(const InputEvent &event);
	void Reset() { m_hover = m_pressed = false; }

private:
	bool m_hover = false;
	bool m_pressed = false;
};

class InputField
{
public:
	Rect rect;
	std::string text;
	size_t max_chars = 19;
	bool password = false;
	bool focused = false;
	bool centered = false; // createInputField's flag 1: the text centered in the field
	int text_height = FontSize::Large;
	uint32_t color = Colors::Entry;

	void Draw(Canvas &canvas, TextRenderer &text_renderer, uint64_t now_ms) const;
	void OnChar(uint32_t ch);
	void OnBackspace();
};

struct ListItem
{
	std::string text;
	uint32_t color = Colors::Entry;
};

class ScrollList
{
public:
	Rect rect;
	int row_height = 13; // renderScrollableList: 0xd per row
	int text_height = FontSize::Normal;
	std::vector<ListItem> items;
	int top = 0;
	int selected = -1;
	const Image *highlight = nullptr; // tiled behind the selected row; null = no selection drawn

	void Draw(Canvas &canvas, TextRenderer &text) const;
	int RowAt(int x, int y) const; // item index or -1
	int VisibleRows() const { return rect.h / row_height; }
	void ClampTop();
	void EnsureVisible(int index);
};

class Scrollbar
{
public:
	Rect rect;
	const Image *up = nullptr, *up_pressed = nullptr;
	const Image *down = nullptr, *down_pressed = nullptr;
	const Image *thumb = nullptr;

	void Draw(Canvas &canvas, int top, int max_top) const;
	// Returns the scroll delta for a click (-1/+1 arrows, +-page on the track), 0 if not on the bar.
	int OnMouseDown(int x, int y, int top, int max_top, int page);
	void OnMouseUp() { m_up_held = m_down_held = false; }

private:
	bool m_up_held = false;
	bool m_down_held = false;
};

class CheckBox
{
public:
	Rect rect;
	const Image *unchecked = nullptr;
	const Image *checked = nullptr;
	bool value = false;

	void Draw(Canvas &canvas) const;
};

// The largest font height <= start at which every label fits w x h
// (drawTextWithVerticalAdjustment run over a group of buttons).
int FitTextHeight(TextRenderer &text, const std::vector<std::string> &labels, int w, int h, int start = FontSize::Large);
