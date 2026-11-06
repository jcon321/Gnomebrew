#pragma once

// eqgame's CChatWindow, on the "ChatWindow" SIDL screen (EQUI_ChatWindow.xml): the output
// (CW_ChatOutput, an STML box with a scrollbar) keeps the last 400 lines, wrapped to its width and
// kept scrolled to the bottom unless scrolled up (AddOutputText); the input (CW_ChatInput) takes up
// to 512 characters, with the last 40 lines sent a press of the keymap's History up or down away
// (AddHistory, HandleKeyboardInput; Shift+Up and Shift+Down by default). Every channel goes to this one window so far. Item tags (item_tags.h) in
// a line are links, and the input takes them (CEditWnd::AddItemTag).

#include "item_tags.h"
#include "sidl.h"

#include <cstdint>
#include <deque>
#include <string>
#include <utility>
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
	std::string InputText() const; // what's typed in it so far
	// A key while it's open; true when it took it. A line sent with Enter comes back in `line`.
	bool OnKey(const InputEvent &event, std::string &line);
	// The keymap's History, up and down, while it's open: a line sent before into the input.
	void History(bool up);
	void OnChar(uint32_t ch);
	// CEditWnd::AddItemTag: an item's tag on the end of the input, which opens for it; not past
	// ten of them, or what the line takes.
	void AddItemTag(uint16_t id, const std::string &name);

	// The links' color (User_71's).
	void SetLinkColor(uint32_t rgb);
	// The mouse, before the windows have it; `over`: the chat window is the top one under it. The
	// link under the mouse is drawn lit, and one let go of where it was pressed comes back in
	// `clicked` (true then).
	bool OnMouse(const InputEvent &event, bool over, ItemTag &clicked);

	// The lines added so far, oldest first (for tools).
	struct Line
	{
		std::string text; // as it shows: the tags' names in their places
		uint32_t rgb = 0xffffff;
		std::vector<ItemTag> links;
	};
	const std::deque<Line> &Lines() const { return m_lines; }

private:
	// A row's text in pieces at its links' ends: where each is drawn from its left, and how wide.
	struct Piece
	{
		std::string text;
		int x = 0, w = 0;
		int link = -1; // the line's link, or -1
	};
	struct Row
	{
		size_t line; // the line, counting the ones dropped off the front
		uint32_t rgb;
		std::vector<Piece> pieces;
	};
	// A link: its line (counting the dropped ones) and which of the line's.
	struct LinkAt
	{
		size_t line = 0;
		int link = -1; // -1: none
		bool operator==(const LinkAt &o) const { return line == o.line && link == o.link; }
	};

	SidlWindow::Control *Output() const;
	SidlWindow::Control *Input() const;
	void DrawOutput(Canvas &canvas, TextRenderer &text, const Rect &area) const;
	// [begin, end) of each row the text wraps into.
	void Wrap(TextRenderer &text, const std::string &s, std::vector<std::pair<size_t, size_t>> &rows) const;
	LinkAt LinkUnder(int x, int y) const;

	SidlWindow *m_window = nullptr;
	std::deque<Line> m_lines;
	size_t m_dropped = 0;       // lines dropped off the front since the start
	std::vector<Row> m_rows;    // m_lines wrapped
	size_t m_wrapped = 0;       // lines (counting dropped ones) wrapped into m_rows so far
	int m_wrap_width = -1;
	int m_row_height = 14;
	int m_font = 14;
	int m_weight = 0;
	std::vector<std::string> m_history; // newest last
	int m_history_pos = -1;             // History: which one is showing, -1 none
	uint32_t m_link_rgb = 0xff00ff;
	LinkAt m_hover, m_pressed;
};
