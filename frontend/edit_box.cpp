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

	int CaretOf(const SidlWindow::Control &box) { return box.edit_caret_end ? box.edit_end : box.edit_start; }

	// GetLineIndexFromPosition: the last line starting at or before `at`.
	int LineOf(const SidlWindow::Control &box, int at)
	{
		int line = 0;
		for (int i = 0; i < static_cast<int>(box.edit_lines.size()) && box.edit_lines[i] <= at; ++i)
			line = i;
		return line;
	}

	// Where the line after `line` starts, or the text's end.
	int NextLine(const SidlWindow::Control &box, int line, int length)
	{
		return line + 1 < static_cast<int>(box.edit_lines.size()) ? box.edit_lines[line + 1] : length;
	}

	// SelectableCharFromPoint (@005a2660), at window pixels: the first character whose extent from
	// its line's start passes x (the caret goes before it), else the line's end (with what ends it:
	// a wrapped line's space, a '\n'). A multi-line box's row from y (past the last row: the end;
	// above the first: the start), left of it its line's start, right of it the next line's start.
	// Inside an item tag: the tag's start. From the layout as last drawn.
	int CharAt(const SidlWindow::Control &box, int x, int y)
	{
		std::vector<ItemTag> tags;
		const int length = static_cast<int>(ShowItemTags(box.text, &tags).size());
		int at = length;
		if (!box.def->multiline) {
			const std::vector<int> &xs = box.edit_x;
			at = box.edit_offset + std::max(static_cast<int>(xs.size()) - 1, 0);
			if (!xs.empty() && x < xs[0])
				at = box.edit_offset;
			else
				for (size_t k = 1; k < xs.size(); ++k)
					if (x < xs[k]) {
						at = box.edit_offset + static_cast<int>(k) - 1;
						break;
					}
		}
		else if (!box.edit_lines.empty() && box.edit_line_h > 0) {
			int row = (y + box.scroll_pos - box.edit_top) / box.edit_line_h;
			if (row >= static_cast<int>(box.edit_lines.size()))
				return length;
			if (row < 0)
				return 0;
			int start = box.edit_lines[row], next = NextLine(box, row, length);
			if (x < box.client.x)
				at = start;
			else if (x > box.client.Right())
				at = next;
			else {
				const std::vector<int> &xs = box.edit_line_x[row];
				at = next;
				for (int k = 1; k <= next - start; ++k)
					if (k < static_cast<int>(xs.size()) && x < xs[k]) {
						at = start + k - 1;
						break;
					}
			}
		}
		for (const ItemTag &tag : tags)
			if (static_cast<int>(tag.begin) <= at && at < static_cast<int>(tag.end)) {
				at = static_cast<int>(tag.begin);
				break;
			}
		return std::clamp(at, 0, length);
	}

	// GetCaretPt (@005a3a00) for a multi-line box: the caret's line and x as last drawn, at the
	// end of the line before when it's at a wrapped line's start and edit_line_end says so; y with
	// the box scrolled.
	struct CaretPoint
	{
		int x = 0, y = 0, line = 0;
	};
	CaretPoint CaretAt(const SidlWindow::Control &box)
	{
		CaretPoint pt{box.client.x, box.client.y, 0};
		if (box.edit_lines.empty() || box.edit_line_x.size() != box.edit_lines.size())
			return pt;
		int caret = CaretOf(box);
		int line = LineOf(box, caret);
		if (box.edit_line_end && line > 0 && caret == box.edit_lines[line]) {
			--line;
			pt.x = box.edit_line_x[line].empty() ? box.client.x : box.edit_line_x[line].back();
		}
		else {
			const std::vector<int> &xs = box.edit_line_x[line];
			int k = std::clamp(caret - box.edit_lines[line], 0, std::max(static_cast<int>(xs.size()) - 1, 0));
			pt.x = xs.empty() ? box.client.x : xs[k];
		}
		pt.line = line;
		pt.y = box.edit_top + line * box.edit_line_h - box.scroll_pos;
		return pt;
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
	if (!box.editable)
		return false;
	Sync(box);
	box.edit_follow = true; // HandleKeyboardMsg ends with EnsureCaretVisible
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
		box.edit_line_end = false;
		return true;
	case 0xcd: // Shift+Right
		if (!event.shift)
			return false;
		if (const ItemTag *tag = tag_starting(caret))
			SetCaret(box, static_cast<int>(tag->end));
		else
			SetCaret(box, std::min(box.edit_end + 1, length));
		box.edit_line_end = true;
		return true;
	case 0xc7: // Shift+Home: the start; a multi-line box's, of the caret's line
		if (!event.shift)
			return false;
		if (box.def->multiline) {
			CaretPoint at = CaretAt(box);
			SetCaret(box, CharAt(box, box.client.x, at.y));
		}
		else
			SetCaret(box, 0);
		box.edit_caret_end = false;
		box.edit_line_end = false;
		return true;
	case 0xcf: // Shift+End: the end; a multi-line box's, of the caret's line
		if (!event.shift)
			return false;
		if (box.def->multiline) {
			CaretPoint at = CaretAt(box);
			SetCaret(box, CharAt(box, box.client.Right(), at.y));
		}
		else
			SetCaret(box, length);
		box.edit_line_end = true;
		return true;
	default:
		return false;
	}
}

void EditBox::Press(SidlWindow::Control &box, int x, int y, bool shift)
{
	// HandleLButtonDown: not a box that's only shown; the caret where the mouse went down, with
	// Shift the selection from the end away from the caret to there; a press right of the left
	// edge puts a wrapped line's start at the end of the line before.
	if (!box.editable)
		return;
	Sync(box);
	int at = CharAt(box, x, y);
	if (!shift)
		SetCaret(box, at);
	else {
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
	box.edit_line_end = x > box.client.x;
	box.edit_follow = true;
}

void EditBox::Drag(SidlWindow::Control &box, int x, int y)
{
	if (!box.editable)
		return;
	int at = CharAt(box, x, y);
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
	box.edit_follow = true;
}

namespace
{
	// RenderEditWindowText's runs of [from, to) of a line at y, the line's characters starting at
	// x_of(i): split at the item tags (their link color) and the selection (a box of the text's
	// color, the text in the inverse).
	template <typename XOf>
	void DrawRuns(const SidlWindow::Control &box, Canvas &canvas, TextRenderer &text, const std::string &all,
		const std::vector<ItemTag> &tags, int from, int to, XOf x_of, int y, int line_h, int height, int weight)
	{
		if (from >= to)
			return;
		for (const ItemTagPiece &piece : SplitAtItemTags(tags, static_cast<size_t>(from), static_cast<size_t>(to))) {
			for (int part = 0; part < 3; ++part) {
				// Before, in and after the selection.
				int a0 = static_cast<int>(piece.begin), b0 = static_cast<int>(piece.end);
				int s = std::max(a0, box.edit_start), e = std::min(b0, box.edit_end);
				int a = part == 0 ? a0 : part == 1 ? s : std::max(a0, box.edit_end);
				int b = part == 0 ? std::min(b0, box.edit_start) : part == 1 ? e : b0;
				if (a >= b)
					continue;
				uint32_t color = piece.tag >= 0 ? box.link_color : box.text_color;
				std::string run = all.substr(a, b - a);
				int x = x_of(a);
				if (part == 1) {
					canvas.Fill({x, y, x_of(b) - x, line_h}, box.text_color);
					color = ~box.text_color & 0xffffff;
				}
				text.Draw(canvas, x, y, run, height, color, weight);
			}
		}
	}

	// WrapTextToWidth (CTextureFont @005a4b00): where each line starts. Characters go on until one
	// takes the line past `width`; it breaks after the line's last space (the space stays on the
	// line), or with none before the character that overflowed. A '\n' ends a line.
	std::vector<int> WrapLines(TextRenderer &text, const std::string &s, int width, int height, int weight)
	{
		std::vector<int> starts{0};
		int start = 0, space = -1;
		int i = 0;
		const int n = static_cast<int>(s.size());
		while (i < n) {
			if (s[i] == '\n') {
				starts.push_back(i + 1);
				start = i + 1;
				space = -1;
				++i;
				continue;
			}
			if (s[i] == ' ')
				space = i;
			if (text.Measure(s.substr(start, i - start + 1), height, weight).w > width) {
				if (space < 0 && i == 0)
					break; // nothing fits: one line
				int next = space >= 0 ? space + 1 : i;
				if (next <= start)
					next = i + 1; // one character wider than the box: it has a line of its own
				starts.push_back(next);
				start = next;
				space = -1;
				i = next;
				continue;
			}
			++i;
		}
		return starts;
	}

	void DrawMultiline(SidlWindow::Control &edit, Canvas &canvas, TextRenderer &text, int height, int weight,
		bool focused, uint64_t now_ms)
	{
		const Rect &c = edit.client;
		std::vector<ItemTag> tags;
		std::string all = ShowItemTags(edit.text, &tags);
		const int length = static_cast<int>(all.size());
		edit.edit_start = std::clamp(edit.edit_start, 0, length);
		edit.edit_end = std::clamp(edit.edit_end, edit.edit_start, length);
		const int line_h = text.Measure("W", height, weight).h;
		// ProcessEditWindowText: the lines, again when the text, width or font changed; where each
		// character starts, for the mouse.
		const int x0 = c.x;
		bool moved = edit.edit_line_x.empty() || edit.edit_line_x[0].empty() || edit.edit_line_x[0][0] != x0;
		if (all != edit.edit_wrapped || edit.edit_wrap_w != c.w || edit.edit_line_h != line_h || moved ||
		    edit.edit_line_x.size() != edit.edit_lines.size()) {
			edit.edit_lines = WrapLines(text, all, c.w, height, weight);
			edit.edit_line_x.assign(edit.edit_lines.size(), {});
			for (size_t l = 0; l < edit.edit_lines.size(); ++l) {
				int start = edit.edit_lines[l], next = NextLine(edit, static_cast<int>(l), length);
				std::vector<int> &xs = edit.edit_line_x[l];
				xs.push_back(x0);
				for (int k = 1; k <= next - start; ++k)
					xs.push_back(all[start + k - 1] == '\n'
					                 ? xs.back()
					                 : x0 + text.Measure(all.substr(start, k), height, weight).w);
			}
			edit.edit_wrapped = all;
			edit.edit_wrap_w = c.w;
		}
		edit.edit_line_h = line_h;
		edit.edit_top = c.y;
		// CalculateScrollRange: the lines' height past the box's, the position kept in it.
		int total = static_cast<int>(edit.edit_lines.size()) * line_h;
		edit.scroll_page = std::max(c.h, 1);
		edit.scroll_max = total > c.h ? total - c.h : 0;
		edit.scroll_pos = std::clamp(edit.scroll_pos, 0, edit.scroll_max);
		// EnsureCaretVisible: the caret's line scrolled into the box.
		if (edit.edit_follow) {
			edit.edit_follow = false;
			if (edit.editable) {
				CaretPoint pt = CaretAt(edit);
				if (pt.y < c.y)
					edit.scroll_pos += pt.y - c.y;
				if (c.Bottom() < pt.y + line_h)
					edit.scroll_pos += pt.y - c.Bottom() + line_h;
				edit.scroll_pos = std::clamp(edit.scroll_pos, 0, edit.scroll_max);
			}
		}
		Rect clip = canvas.Clip();
		canvas.SetClip(Intersect(c, clip));
		for (size_t l = 0; l < edit.edit_lines.size(); ++l) {
			int y = c.y + static_cast<int>(l) * line_h - edit.scroll_pos;
			if (y + line_h <= c.y || y >= c.Bottom())
				continue;
			int start = edit.edit_lines[l], next = NextLine(edit, static_cast<int>(l), length);
			int end = next > start && all[next - 1] == '\n' ? next - 1 : next;
			const std::vector<int> &xs = edit.edit_line_x[l];
			auto x_of = [&](int i) {
				int k = std::clamp(i - start, 0, static_cast<int>(xs.size()) - 1);
				return xs[k];
			};
			DrawRuns(edit, canvas, text, all, tags, start, end, x_of, y, line_h, height, weight);
		}
		// DrawCaret: shown 512 ms of every 1024 while it has the focus, if it takes typing.
		if (focused && edit.editable && (now_ms & 0x200) == 0) {
			CaretPoint pt = CaretAt(edit);
			canvas.Fill({pt.x, pt.y, 1, line_h}, edit.text_color);
		}
		canvas.SetClip(clip);
	}
}

void EditBox::Draw(const SidlWindow::Control &box, Canvas &canvas, TextRenderer &text, int height, int weight, bool focused,
	uint64_t now_ms)
{
	SidlWindow::Control &edit = const_cast<SidlWindow::Control &>(box); // the drawn layout is kept
	Sync(edit);
	if (box.def->multiline) {
		DrawMultiline(edit, canvas, text, height, weight, focused, now_ms);
		return;
	}
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
	DrawRuns(box, canvas, text, all, tags, offset, length, x_of, y, line.h, height, weight);
	if (focused && box.editable && (now_ms & 0x200) == 0)
		canvas.Fill({x_of(caret), y, 1, line.h}, box.text_color);
	canvas.SetClip(clip);
}
