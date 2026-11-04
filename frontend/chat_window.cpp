#include "chat_window.h"

#include <algorithm>

namespace
{
	const char *const OutputId = "CWChatOutput";
	const char *const InputId = "CWChatInput";
}

void ChatWindow::Attach(SidlWindow *window)
{
	m_window = window;
	m_wrap_width = -1; // wrap everything again for this one
	if (!m_window)
		return;
	if (SidlWindow::Control *output = Output()) {
		m_font = SidlFontHeight(output->def->font);
		m_weight = SidlFontWeight(output->def->font);
		output->draw_content = [this](Canvas &canvas, TextRenderer &text, const Rect &area) { DrawOutput(canvas, text, area); };
	}
	if (SidlWindow::Control *input = Input()) {
		input->max_chars = MaxInput;
		input->text_color = 0xffffff;
		input->link_color = m_link_rgb;
	}
	m_hover = m_pressed = {};
}

SidlWindow::Control *ChatWindow::Output() const
{
	return m_window ? m_window->Find(OutputId) : nullptr;
}

SidlWindow::Control *ChatWindow::Input() const
{
	return m_window ? m_window->Find(InputId) : nullptr;
}

void ChatWindow::Add(const std::string &text, uint32_t rgb)
{
	// A line per '\n', like <br> in the STML.
	size_t start = 0;
	while (start <= text.size()) {
		size_t end = text.find('\n', start);
		Line line;
		line.text = ShowItemTags(text.substr(start, end == std::string::npos ? std::string::npos : end - start), &line.links);
		line.rgb = rgb;
		m_lines.push_back(std::move(line));
		if (end == std::string::npos)
			break;
		start = end + 1;
	}
	while (m_lines.size() > MaxLines) {
		m_lines.pop_front();
		++m_dropped;
	}
}

void ChatWindow::Clear()
{
	m_dropped += m_lines.size();
	m_lines.clear();
	m_rows.clear();
	m_wrapped = m_dropped;
	m_hover = m_pressed = {};
	if (SidlWindow::Control *output = Output())
		output->scroll_pos = output->scroll_max = 0;
}

void ChatWindow::Wrap(TextRenderer &text, const std::string &s, std::vector<std::pair<size_t, size_t>> &rows) const
{
	// Word wrap at spaces; a word wider than the whole row is broken where it has to be.
	rows.clear();
	auto width = [&](size_t from, size_t to) { return text.Measure(s.substr(from, to - from), m_font, m_weight).w; };
	size_t row = 0; // where the row being filled starts
	size_t i = 0;
	while (i < s.size()) {
		size_t end = s.find(' ', i);
		size_t next = end == std::string::npos ? s.size() : end + 1; // the word and its space
		if (width(row, next) > m_wrap_width) {
			if (row < i) {
				rows.push_back({row, i});
				row = i;
			}
			while (width(row, next) > m_wrap_width) {
				size_t fit = row + 1;
				while (fit < next && width(row, fit + 1) <= m_wrap_width)
					++fit;
				rows.push_back({row, fit});
				row = fit;
			}
		}
		i = next;
	}
	if (row < s.size() || rows.empty())
		rows.push_back({row, s.size()});
}

void ChatWindow::Layout(TextRenderer &text)
{
	SidlWindow::Control *output = Output();
	if (!output)
		return;
	Rect area = m_window->ContentRect(*output);
	int width = std::max(area.w - 4, 8);
	m_row_height = std::max(text.Measure("Wg", m_font, m_weight).h, 1);
	if (width != m_wrap_width) {
		m_wrap_width = width;
		m_rows.clear();
		m_wrapped = m_dropped;
	}
	// Rows of lines that dropped off the front go, and new lines are wrapped onto the end.
	size_t gone = 0;
	while (gone < m_rows.size() && m_rows[gone].line < m_dropped)
		++gone;
	m_rows.erase(m_rows.begin(), m_rows.begin() + static_cast<long>(gone));
	m_wrapped = std::max(m_wrapped, m_dropped);
	std::vector<std::pair<size_t, size_t>> rows;
	for (; m_wrapped < m_dropped + m_lines.size(); ++m_wrapped) {
		const Line &line = m_lines[m_wrapped - m_dropped];
		Wrap(text, line.text, rows);
		for (const auto &range : rows) {
			Row row{m_wrapped, line.rgb, {}};
			for (const ItemTagPiece &cut : SplitAtItemTags(line.links, range.first, range.second)) {
				Piece piece;
				piece.text = line.text.substr(cut.begin, cut.end - cut.begin);
				piece.x = cut.begin > range.first ? text.Measure(line.text.substr(range.first, cut.begin - range.first), m_font, m_weight).w : 0;
				piece.w = text.Measure(piece.text, m_font, m_weight).w;
				piece.link = cut.tag;
				row.pieces.push_back(std::move(piece));
			}
			m_rows.push_back(std::move(row));
		}
	}

	// Scrolled to the bottom, it stays there as lines come in.
	bool bottom = output->scroll_pos >= output->scroll_max;
	int page = std::max((area.h - 4) / m_row_height, 1);
	output->scroll_page = page;
	output->scroll_max = std::max(static_cast<int>(m_rows.size()) - page, 0);
	output->scroll_pos = bottom ? output->scroll_max : std::min(output->scroll_pos, output->scroll_max);
}

void ChatWindow::DrawOutput(Canvas &canvas, TextRenderer &text, const Rect &area) const
{
	const SidlWindow::Control *output = Output();
	if (!output)
		return;
	int first = std::max(output->scroll_pos, 0);
	int y = area.y + 2;
	for (int i = first; i < static_cast<int>(m_rows.size()) && y + m_row_height <= area.Bottom(); ++i, y += m_row_height) {
		const Row &row = m_rows[i];
		for (const Piece &piece : row.pieces) {
			uint32_t rgb = row.rgb;
			if (piece.link >= 0) {
				// CStmlWnd::DrawInRect: the one under the mouse blue, navy while it's pressed there.
				LinkAt at{row.line, piece.link};
				rgb = !(at == m_hover) ? m_link_rgb : at == m_pressed ? ItemTags::PressedColor : ItemTags::HoverColor;
			}
			text.Draw(canvas, area.x + 2 + piece.x, y, piece.text, m_font, rgb, m_weight);
		}
	}
}

ChatWindow::LinkAt ChatWindow::LinkUnder(int x, int y) const
{
	const SidlWindow::Control *output = Output();
	if (!output || m_window->Minimized() || !m_window->Shown(*output))
		return {};
	Rect area = m_window->ContentRect(*output);
	if (!area.Contains(x, y) || y < area.y + 2)
		return {};
	int i = std::max(output->scroll_pos, 0) + (y - area.y - 2) / m_row_height;
	if (i >= static_cast<int>(m_rows.size()) || area.y + 2 + (i - std::max(output->scroll_pos, 0) + 1) * m_row_height > area.Bottom())
		return {};
	const Row &row = m_rows[i];
	for (const Piece &piece : row.pieces)
		if (piece.link >= 0 && x >= area.x + 2 + piece.x && x < area.x + 2 + piece.x + piece.w)
			return {row.line, piece.link};
	return {};
}

void ChatWindow::SetLinkColor(uint32_t rgb)
{
	m_link_rgb = rgb;
	if (SidlWindow::Control *input = Input())
		input->link_color = rgb;
}

bool ChatWindow::OnMouse(const InputEvent &event, bool over, ItemTag &clicked)
{
	LinkAt under = over && m_window ? LinkUnder(event.x, event.y) : LinkAt{};
	switch (event.type) {
	case InputEvent::Type::MouseMove:
		m_hover = under;
		return false;
	case InputEvent::Type::MouseDown:
	case InputEvent::Type::DoubleClick:
		m_hover = m_pressed = under;
		return false;
	case InputEvent::Type::MouseUp: {
		LinkAt pressed = m_pressed;
		m_pressed = {};
		m_hover = under;
		if (pressed.link < 0 || !(pressed == under) || pressed.line < m_dropped)
			return false;
		const Line &line = m_lines[pressed.line - m_dropped];
		if (pressed.link >= static_cast<int>(line.links.size()))
			return false;
		clicked = line.links[pressed.link];
		return true;
	}
	default:
		return false;
	}
}

bool ChatWindow::Typing() const
{
	return m_window && m_window->Focused(InputId);
}

void ChatWindow::Open(const std::string &text)
{
	if (!m_window)
		return;
	m_window->Focus(InputId);
	if (SidlWindow::Control *input = Input())
		input->text = text;
	m_history_pos = -1;
}

void ChatWindow::Close()
{
	if (m_window && Typing())
		m_window->Focus("");
}

void ChatWindow::OnChar(uint32_t ch)
{
	SidlWindow::Control *input = Input();
	if (input && ch >= 0x20 && ch < 0x7f && input->text.size() < MaxInput)
		input->text += static_cast<char>(ch);
}

void ChatWindow::AddItemTag(uint16_t id, const std::string &name)
{
	SidlWindow::Control *input = Input();
	if (!input)
		return;
	if (!Typing()) {
		m_window->Focus(InputId);
		m_history_pos = -1;
	}
	std::string tag = MakeItemTag(id, name);
	if (CountItemTags(input->text) < ItemTags::MaxPerLine && input->text.size() + tag.size() <= MaxInput)
		input->text += tag;
}

bool ChatWindow::OnKey(const InputEvent &event, std::string &line)
{
	SidlWindow::Control *input = Input();
	if (!input || !Typing())
		return false;
	switch (event.key) {
	case InputEvent::Key::Enter:
		line = input->text;
		input->text.clear();
		if (!line.empty()) {
			m_history.push_back(line);
			if (m_history.size() > MaxHistory)
				m_history.erase(m_history.begin());
		}
		m_history_pos = -1;
		Close();
		return true;
	case InputEvent::Key::Escape:
		input->text.clear();
		Close();
		return true;
	case InputEvent::Key::Backspace:
		// A tag goes whole.
		if (input->text.size() >= 2 && input->text.back() == ItemTags::Marker) {
			size_t open = input->text.rfind(ItemTags::Marker, input->text.size() - 2);
			input->text.erase(open == std::string::npos ? input->text.size() - 1 : open);
		}
		else if (!input->text.empty())
			input->text.pop_back();
		return true;
	case InputEvent::Key::Up:
	case InputEvent::Key::Down: {
		// Back through what was sent, newest first; past the newest, an empty line.
		if (m_history.empty())
			return true;
		int count = static_cast<int>(m_history.size());
		if (event.key == InputEvent::Key::Up)
			m_history_pos = m_history_pos < 0 ? count - 1 : std::max(m_history_pos - 1, 0);
		else if (m_history_pos >= 0)
			m_history_pos = m_history_pos + 1 < count ? m_history_pos + 1 : -1;
		input->text = m_history_pos >= 0 ? m_history[m_history_pos] : "";
		return true;
	}
	default:
		return false;
	}
}

void ChatWindow::History(bool up)
{
	// CChatWindow::ScrollChatHistoryUp and HandleKeyboardInput's down: back through what was sent,
	// newest first; past the newest, an empty line.
	SidlWindow::Control *input = Input();
	if (!input || !Typing() || m_history.empty())
		return;
	int count = static_cast<int>(m_history.size());
	if (up)
		m_history_pos = m_history_pos < 0 ? count - 1 : std::max(m_history_pos - 1, 0);
	else if (m_history_pos >= 0)
		m_history_pos = m_history_pos + 1 < count ? m_history_pos + 1 : -1;
	input->text = m_history_pos >= 0 ? m_history[m_history_pos] : "";
}
