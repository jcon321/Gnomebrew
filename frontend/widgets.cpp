#include "widgets.h"

#include <algorithm>

void Button::Place(int x, int y, const ButtonArt &button_art)
{
	art = button_art;
	rect = {x, y, art.normal ? art.normal->width : 0, art.normal ? art.normal->height : 0};
}

void Button::Draw(Canvas &canvas, TextRenderer &text) const
{
	if (!visible)
		return;
	const Image *image = m_pressed && m_hover ? art.down : (m_hover ? art.over : art.normal);
	if (!image)
		image = art.normal;
	if (image)
		canvas.Blit(*image, rect.x, rect.y, true);

	if (!label.empty()) {
		// centerTextInButton: centered on the bitmap, black.
		TextSize size = text.Measure(label, text_height);
		text.Draw(canvas, rect.x + rect.w / 2 - size.w / 2, rect.y + rect.h / 2 - size.h / 2, label, text_height,
			Colors::ButtonText);
	}
}

bool Button::OnMouse(const InputEvent &event)
{
	if (!visible)
		return false;
	bool inside = rect.Contains(event.x, event.y);
	switch (event.type) {
	case InputEvent::Type::MouseMove:
		m_hover = inside;
		return false;
	case InputEvent::Type::MouseDown:
	case InputEvent::Type::DoubleClick:
		m_hover = inside;
		m_pressed = inside;
		return false;
	case InputEvent::Type::MouseUp: {
		bool clicked = m_pressed && inside;
		m_pressed = false;
		return clicked;
	}
	default:
		return false;
	}
}

void InputField::Draw(Canvas &canvas, TextRenderer &text_renderer, uint64_t now_ms) const
{
	std::string shown = password ? std::string(text.size(), '*') : text;

	// renderUIElement (@10007020): keep the end of the text (and the caret) visible when it's
	// wider than the field, less 10 pixels.
	size_t start = 0;
	while (start < shown.size() && text_renderer.Measure(shown.substr(start), text_height).w > rect.w - 10)
		++start;
	shown = shown.substr(start);

	TextSize size = text_renderer.Measure(shown.empty() ? "X" : shown, text_height);
	int width = shown.empty() ? 0 : text_renderer.Measure(shown, text_height).w;
	int x = centered ? rect.x + rect.w / 2 - width / 2 : rect.x + 2;
	int y = rect.y + (rect.h - size.h) / 2;
	if (!shown.empty())
		text_renderer.Draw(canvas, x, y, shown, text_height, color);

	if (focused && (now_ms / 500) % 2 == 0)
		canvas.Fill({x + width, rect.y + 3, 2, rect.h - 6}, color); // the caret, just after the text
}

void InputField::OnChar(uint32_t ch)
{
	if (ch < 0x20 || ch > 0x7e || text.size() >= max_chars)
		return; // the 2002 client's fields are plain ASCII
	text.push_back(static_cast<char>(ch));
}

void InputField::OnBackspace()
{
	if (!text.empty())
		text.pop_back();
}

void ScrollList::Draw(Canvas &canvas, TextRenderer &text) const
{
	int y = rect.y;
	for (int index = top; index < static_cast<int>(items.size()) && y <= rect.Bottom() - row_height; ++index) {
		if (index == selected && highlight)
			canvas.Tile(*highlight, {rect.x, y, rect.w, std::min(highlight->height, rect.Bottom() - y)});
		text.Draw(canvas, rect.x + 2, y, items[index].text, text_height, items[index].color);
		y += row_height;
	}
}

int ScrollList::RowAt(int x, int y) const
{
	if (!rect.Contains(x, y))
		return -1;
	int index = top + (y - rect.y) / row_height;
	return index < static_cast<int>(items.size()) ? index : -1;
}

void ScrollList::ClampTop()
{
	int max_top = std::max(0, static_cast<int>(items.size()) - VisibleRows());
	top = std::clamp(top, 0, max_top);
}

void ScrollList::EnsureVisible(int index)
{
	if (index < 0)
		return;
	if (index < top)
		top = index;
	else if (index >= top + VisibleRows())
		top = index - VisibleRows() + 1;
	ClampTop();
}

void Scrollbar::Draw(Canvas &canvas, int top, int max_top) const
{
	const Image *u = m_up_held && up_pressed ? up_pressed : up;
	const Image *d = m_down_held && down_pressed ? down_pressed : down;
	if (u)
		canvas.Blit(*u, rect.x, rect.y, true);
	if (d)
		canvas.Blit(*d, rect.x, rect.Bottom() - d->height, true);
	if (thumb && max_top > 0) {
		int track_top = rect.y + (u ? u->height : 0);
		int track_len = rect.h - (u ? u->height : 0) - (d ? d->height : 0) - thumb->height;
		canvas.Blit(*thumb, rect.x, track_top + track_len * top / max_top, true);
	}
}

int Scrollbar::OnMouseDown(int x, int y, int top, int max_top, int page)
{
	if (!rect.Contains(x, y))
		return 0;
	int up_h = up ? up->height : 0;
	int down_h = down ? down->height : 0;
	if (y < rect.y + up_h) {
		m_up_held = true;
		return -1;
	}
	if (y >= rect.Bottom() - down_h) {
		m_down_held = true;
		return 1;
	}
	if (max_top <= 0)
		return 0;
	int track_top = rect.y + up_h;
	int track_len = rect.h - up_h - down_h - (thumb ? thumb->height : 0);
	int thumb_y = track_top + track_len * top / max_top;
	return y < thumb_y ? -page : page;
}

void CheckBox::Draw(Canvas &canvas) const
{
	const Image *image = value ? checked : unchecked;
	if (image)
		canvas.Blit(*image, rect.x, rect.y, true);
}

int FitTextHeight(TextRenderer &text, const std::vector<std::string> &labels, int w, int h, int start)
{
	for (int height = start; height > 6; --height) {
		bool fits = true;
		for (const auto &label : labels) {
			TextSize size = text.Measure(label, height);
			if (size.w >= w || size.h >= h) {
				fits = false;
				break;
			}
		}
		if (fits)
			return height;
	}
	return 6;
}
