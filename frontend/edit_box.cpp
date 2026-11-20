#include "edit_box.h"

#include "canvas.h"
#include "item_tags.h"
#include "text.h"

#include <algorithm>
#include <cctype>

namespace
{
	Rect Intersect(const Rect &a, const Rect &b)
	{
		int x0 = std::max(a.x, b.x), y0 = std::max(a.y, b.y);
		int x1 = std::min(a.Right(), b.Right()), y1 = std::min(a.Bottom(), b.Bottom());
		return {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
	}

	// ConvertIndexPrintableToTagged: the tagged text's index of shown index `shown` (a tag's name's
	// characters are its own; the tag's start is its opening marker's, its end after its closing one).
	size_t ToTagged(const std::string &tagged, int shown)
	{
		int at = 0;
		size_t i = 0;
		while (i < tagged.size()) {
			if (at >= shown && tagged[i] != ItemTags::Marker)
				return i;
			if (tagged[i] != ItemTags::Marker) {
				++at;
				++i;
				continue;
			}
			size_t close = tagged.find(ItemTags::Marker, i + 2);
			if (close == std::string::npos) {
				++i;
				continue;
			}
			if (at >= shown)
				return i;
			size_t name = std::min(i + 8, close);
			int length = static_cast<int>(close - name);
			if (shown < at + length)
				return name + static_cast<size_t>(shown - at);
			at += length;
			i = close + 1;
		}
		return tagged.size();
	}

	int ShownLength(const std::string &tagged) { return static_cast<int>(ShowItemTags(tagged).size()); }

	// FilterInputStr: AlphaNumericOnly's characters, and never a tag's marker typed in.
	std::string Filter(const std::string &in, int mode)
	{
		std::string out;
		for (char c : in) {
			unsigned char u = static_cast<unsigned char>(c);
			bool keep = true;
			if (mode == 1 || mode == 2)
				keep = std::isalpha(u) != 0; // isAlphanumeric: ctype 0x103
			else if (mode == 3)
				keep = std::isdigit(u) != 0; // isSpecialCharacter: 4
			else if (mode == 4)
				keep = std::isalnum(u) != 0; // isNumericCharacter: 0x107
			else
				keep = c != ItemTags::Marker;
			if (keep)
				out.push_back(c);
		}
		return out;
	}

	void SetCaret(SidlWindow::Control &box, int at)
	{
		box.edit_start = box.edit_end = at;
		box.edit_caret_end = true;
	}

	void Saw(SidlWindow::Control &box)
	{
		box.edit_seen = box.text;
		box.edit_synced = true;
	}
}

void EditBox::Sync(SidlWindow::Control &box)
{
	if (box.edit_synced && box.text == box.edit_seen)
		return;
	int end = ShownLength(box.text);
	SetCaret(box, end);
	Saw(box);
}

void EditBox::SetSel(SidlWindow::Control &box, int start, int end)
{
	Sync(box);
	int length = ShownLength(box.text);
	if (end < 0 || end > length)
		end = length;
	start = std::clamp(start, 0, end);
	box.edit_start = start;
	box.edit_end = end;
	box.edit_caret_end = true;
}

int EditBox::Caret(const SidlWindow::Control &box)
{
	return box.edit_caret_end ? box.edit_end : box.edit_start;
}

void EditBox::ReplaceSelection(SidlWindow::Control &box, const std::string &text, bool filter)
{
	Sync(box);
	std::string add = filter ? Filter(text, box.edit_filter) : text;
	std::vector<ItemTag> tags;
	ShowItemTags(box.text, &tags);
	// A selection that cuts into a tag takes the whole tag.
	int start = box.edit_start, end = box.edit_end;
	for (const ItemTag &tag : tags) {
		if (start > static_cast<int>(tag.begin) && start < static_cast<int>(tag.end))
			start = static_cast<int>(tag.begin);
		if (end > static_cast<int>(tag.begin) && end < static_cast<int>(tag.end))
			end = static_cast<int>(tag.end);
	}
	size_t from = ToTagged(box.text, start), to = ToTagged(box.text, end);
	if (box.text.size() - (to - from) + add.size() > box.max_chars)
		return;
	if (CountItemTags(box.text.substr(0, from) + add + box.text.substr(to)) > ItemTags::MaxPerLine)
		return;
	box.text = box.text.substr(0, from) + add + box.text.substr(to);
	SetCaret(box, start + ShownLength(add));
	Saw(box);
}

bool EditBox::Key(SidlWindow::Control &box, const InputEvent &event)
{
	Sync(box);
	if (event.type == InputEvent::Type::Char) {
		if (event.ch < 0x20 || event.ch >= 0x7f)
			return false;
		ReplaceSelection(box, std::string(1, static_cast<char>(event.ch)), true);
		return true;
	}
	if (event.type != InputEvent::Type::Key)
		return false;
	std::vector<ItemTag> tags;
	std::string shown = ShowItemTags(box.text, &tags);
	const int length = static_cast<int>(shown.size());
	const int caret = Caret(box);
	auto tag_ending = [&](int at) -> const ItemTag * {
		for (const ItemTag &tag : tags)
			if (static_cast<int>(tag.end) == at)
				return &tag;
		return nullptr;
	};
	auto tag_starting = [&](int at) -> const ItemTag * {
		for (const ItemTag &tag : tags)
			if (static_cast<int>(tag.begin) == at)
				return &tag;
		return nullptr;
	};
	int scan = event.scan;
	if (scan == 0 && event.key == InputEvent::Key::Backspace)
		scan = 0x0e;
	if (scan == 0 && event.key == InputEvent::Key::Delete)
		scan = 0xd3;
	switch (scan) {
	case 0x0e: // Backspace: the selection, or the character (or the tag) before the caret
		if (box.edit_start != box.edit_end) {
			ReplaceSelection(box, "", false);
			return true;
		}
		if (caret <= 0)
			return true;
		if (const ItemTag *tag = tag_ending(caret))
			box.edit_start = static_cast<int>(tag->begin);
		else
			box.edit_start = caret - 1;
		box.edit_end = caret;
		ReplaceSelection(box, "", false);
		return true;
	case 0xd3: // Delete: Shift clears it all; else the selection, or what's after the caret
		if (event.shift) {
			box.text.clear();
			SetCaret(box, 0);
			Saw(box);
			return true;
		}
		if (box.edit_start == box.edit_end && caret < length) {
			box.edit_start = caret;
			const ItemTag *tag = tag_starting(caret);
			box.edit_end = tag ? static_cast<int>(tag->end) : caret + 1;
		}
		if (box.edit_start != box.edit_end)
			ReplaceSelection(box, "", false);
		return true;
	case 0xcb: // Shift+Left: back a character, or over a tag
		if (!event.shift)
			return false;
		if (const ItemTag *tag = tag_ending(caret))
			SetCaret(box, static_cast<int>(tag->begin));
		else
			SetCaret(box, std::max(caret - 1, 0));
		box.edit_caret_end = false;
		return true;
	case 0xcd: // Shift+Right
		if (!event.shift)
			return false;
		if (const ItemTag *tag = tag_starting(caret))
			SetCaret(box, static_cast<int>(tag->end));
		else
			SetCaret(box, std::min(box.edit_end + 1, length));
		return true;
	case 0xc7: // Shift+Home
		if (!event.shift)
			return false;
		SetCaret(box, 0);
		box.edit_caret_end = false;
		return true;
	case 0xcf: // Shift+End
		if (!event.shift)
			return false;
		SetCaret(box, length);
		return true;
	default:
		return false;
	}
}

namespace
{
	// SelectableCharFromPoint: the shown index whose character's middle is past `x`, as last drawn.
	int CharAt(const SidlWindow::Control &box, int x)
	{
		const std::vector<int> &xs = box.edit_x;
		if (xs.size() < 2)
			return box.edit_offset;
		for (size_t i = 0; i + 1 < xs.size(); ++i)
			if (x < (xs[i] + xs[i + 1]) / 2)
				return box.edit_offset + static_cast<int>(i);
		return box.edit_offset + static_cast<int>(xs.size()) - 1;
	}
}

void EditBox::Press(SidlWindow::Control &box, int x, bool shift)
{
	Sync(box);
	int at = CharAt(box, x);
	if (!shift) {
		SetCaret(box, at);
		return;
	}
	// With Shift: from the end away from the caret to here.
	int other = box.edit_caret_end ? box.edit_start : box.edit_end;
	if (other < at) {
		box.edit_start = other;
		box.edit_end = at;
		box.edit_caret_end = true;
	}
	else {
		box.edit_start = at;
		box.edit_end = other;
		box.edit_caret_end = false;
	}
}

void EditBox::Drag(SidlWindow::Control &box, int x)
{
	int at = CharAt(box, x);
	int anchor = box.edit_caret_end ? box.edit_start : box.edit_end;
	if (at > anchor) {
		box.edit_start = anchor;
		box.edit_end = at;
		box.edit_caret_end = true;
	}
	else {
		box.edit_start = at;
		box.edit_end = anchor;
		box.edit_caret_end = false;
	}
}

void EditBox::Draw(const SidlWindow::Control &box, Canvas &canvas, TextRenderer &text, int height, int weight, bool focused,
	uint64_t now_ms)
{
	SidlWindow::Control &edit = const_cast<SidlWindow::Control &>(box); // the drawn layout is kept
	Sync(edit);
	const Rect &c = box.client;
	std::vector<ItemTag> tags;
	std::string all = ShowItemTags(box.text, &tags);
	int length = static_cast<int>(all.size());
	int caret = std::clamp(Caret(box), 0, length);
	edit.edit_start = std::clamp(edit.edit_start, 0, length);
	edit.edit_end = std::clamp(edit.edit_end, edit.edit_start, length);
	// CalculateTextOffset: the first shown character follows the caret both ways.
	int width = c.w - 6;
	int offset = std::clamp(edit.edit_offset, 0, length);
	if (caret < offset)
		offset = caret;
	while (offset < caret && text.Measure(all.substr(offset, caret - offset), height, weight).w > width)
		++offset;
	edit.edit_offset = offset;
	TextSize line = text.Measure("W", height, weight);
	int y = c.y + (c.h - line.h) / 2;
	int x0 = c.x + 2;
	// Where each shown character starts (and the end), for the mouse.
	edit.edit_x.clear();
	for (int i = offset; i <= length; ++i) {
		int x = x0 + (i == offset ? 0 : text.Measure(all.substr(offset, i - offset), height, weight).w);
		if (x > c.Right() + 1)
			break;
		edit.edit_x.push_back(x);
	}
	auto x_of = [&](int i) {
		int k = i - offset;
		if (k < 0)
			return x0;
		if (k < static_cast<int>(edit.edit_x.size()))
			return edit.edit_x[k];
		return c.Right();
	};
	Rect clip = canvas.Clip();
	canvas.SetClip(Intersect(c, clip));
	for (const ItemTagPiece &piece : SplitAtItemTags(tags, static_cast<size_t>(offset), all.size())) {
		for (int part = 0; part < 3; ++part) {
			// Before, in and after the selection.
			int from = static_cast<int>(piece.begin), to = static_cast<int>(piece.end);
			int s = std::max(from, edit.edit_start), e = std::min(to, edit.edit_end);
			int a = part == 0 ? from : part == 1 ? s : std::max(from, edit.edit_end);
			int b = part == 0 ? std::min(to, edit.edit_start) : part == 1 ? e : to;
			if (a >= b)
				continue;
			uint32_t color = piece.tag >= 0 ? box.link_color : box.text_color;
			std::string run = all.substr(a, b - a);
			int x = x_of(a);
			if (part == 1) {
				canvas.Fill({x, y, x_of(b) - x, line.h}, box.text_color);
				color = ~box.text_color & 0xffffff;
			}
			text.Draw(canvas, x, y, run, height, color, weight);
		}
	}
	if (focused && (now_ms & 0x200) == 0)
		canvas.Fill({x_of(caret), y, 1, line.h}, box.text_color);
	canvas.SetClip(clip);
}
