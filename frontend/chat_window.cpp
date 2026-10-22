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
		output->draw_content = [this](Canvas &canvas, TextRenderer &text, const Rect &area) { DrawOutput(canvas, text, area); };
	}
	if (SidlWindow::Control *input = Input()) {
		input->max_chars = MaxInput;
		input->text_color = 0xffffff;
	}
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
		m_lines.push_back({text.substr(start, end == std::string::npos ? std::string::npos : end - start), rgb});
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
	if (SidlWindow::Control *output = Output())
		output->scroll_pos = output->scroll_max = 0;
}

void ChatWindow::Wrap(TextRenderer &text, const Line &line, std::vector<std::string> &rows) const
{
	// Word wrap at spaces; a word wider than the whole row is broken where it has to be.
	rows.clear();
	std::string row;
	size_t i = 0;
	const std::string &s = line.text;
	while (i < s.size()) {
		size_t end = s.find(' ', i);
		std::string word = s.substr(i, end == std::string::npos ? std::string::npos : end - i + 1);
		i = end == std::string::npos ? s.size() : end + 1;
		std::string joined = row + word;
		while (!word.empty() && text.Measure(joined, m_font).w > m_wrap_width) {
			if (!row.empty()) {
				rows.push_back(row);
				row.clear();
				joined = word;
				if (text.Measure(joined, m_font).w <= m_wrap_width)
					break;
			}
			size_t fit = 1;
			while (fit < word.size() && text.Measure(word.substr(0, fit + 1), m_font).w <= m_wrap_width)
				++fit;
			rows.push_back(word.substr(0, fit));
			word.erase(0, fit);
			joined = word;
		}
		row = joined;
	}
	if (!row.empty() || rows.empty())
		rows.push_back(row);
}

void ChatWindow::Layout(TextRenderer &text)
{
	SidlWindow::Control *output = Output();
	if (!output)
		return;
	Rect area = m_window->ContentRect(*output);
	int width = std::max(area.w - 4, 8);
	m_row_height = std::max(text.Measure("Wg", m_font).h, 1);
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
	std::vector<std::string> rows;
	for (; m_wrapped < m_dropped + m_lines.size(); ++m_wrapped) {
		const Line &line = m_lines[m_wrapped - m_dropped];
		Wrap(text, line, rows);
		for (auto &row : rows)
			m_rows.push_back({m_wrapped, std::move(row), line.rgb});
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
	for (int i = first; i < static_cast<int>(m_rows.size()) && y + m_row_height <= area.Bottom(); ++i, y += m_row_height)
		text.Draw(canvas, area.x + 2, y, m_rows[i].text, m_font, m_rows[i].rgb);
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
		if (!input->text.empty())
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
