#pragma once

// eqgame's CChatWindow, on the "ChatWindow" SIDL screen (EQUI_ChatWindow.xml): the output
// (CW_ChatOutput, an STML box with a scrollbar) keeps the last 400 lines, wrapped to its width and
// kept scrolled to the bottom unless scrolled up (AddOutputText); the input (CW_ChatInput) takes up
// to 512 characters, with the last 40 lines sent a press of Up or Down away (AddHistory,
// HandleKeyboardInput). Every channel goes to this one window so far.

#include "sidl.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

class ChatWindow
{
public:
	static constexpr size_t MaxLines = 400;
	static constexpr size_t MaxHistory = 40;
	static constexpr size_t MaxInput = 512;

	// The window it shows in (null: none). Lines added before are kept.
	void Attach(SidlWindow *window);
	bool Attached() const { return m_window != nullptr; }

	void Add(const std::string &text, uint32_t rgb);
	void Clear(); // /clearchat
	// Before drawing: wraps what's new to the output's width and sets its scrollbar's range.
	void Layout(TextRenderer &text);

	// The input line. Enter (or a / typed) opens it, Enter sends it and Escape closes it.
	bool Typing() const;
	void Open(const std::string &text = "");
	void Close();
	// A key while it's open; true when it took it. A line sent with Enter comes back in `line`.
	bool OnKey(const InputEvent &event, std::string &line);
	void OnChar(uint32_t ch);

	// The lines added so far, oldest first (for tools).
	struct Line
	{
		std::string text;
		uint32_t rgb = 0xffffff;
	};
	const std::deque<Line> &Lines() const { return m_lines; }

private:
	struct Row
	{
		size_t line; // index into m_lines, counted from m_first_line
		std::string text;
		uint32_t rgb;
	};

	SidlWindow::Control *Output() const;
	SidlWindow::Control *Input() const;
	void DrawOutput(Canvas &canvas, TextRenderer &text, const Rect &area) const;
	void Wrap(TextRenderer &text, const Line &line, std::vector<std::string> &rows) const;

	SidlWindow *m_window = nullptr;
	std::deque<Line> m_lines;
	size_t m_dropped = 0;       // lines dropped off the front since the start
	std::vector<Row> m_rows;    // m_lines wrapped
	size_t m_wrapped = 0;       // lines (counting dropped ones) wrapped into m_rows so far
	int m_wrap_width = -1;
	int m_row_height = 14;
	int m_font = 14;
	std::vector<std::string> m_history; // newest last
	int m_history_pos = -1;             // Up/Down: which one is showing, -1 none
};
