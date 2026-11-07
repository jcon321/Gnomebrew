#pragma once

// A book's window: eqgame's CBookWnd (@004083a2 on), BookWindow in the UI files.
//   - EQ_Note::SendTextRequestMsg (@004ce45d): the right button on a book (book type 1) closes the
//     window (Deactivate) and CheckBook (@00408681): the same item as last time forgets it and
//     asks nothing, another remembers it (its name the title) and asks the zone (OP_ReadBook).
//   - HandleWorldMessage's OP_ReadBook answer of type 1: SetBook (@00408635), the title and
//     ParseAndFormatBookContent (@0040887f), then Activate (@0040860b): ShowButtons, DisplayText,
//     shown with the keyboard's focus.
//   - ParseAndFormatBookContent: pages of 12 lines (100 pages at most). A line runs to a '`' or the
//     text's end; a character at a time goes on until the font (Page0's) wraps it into a second
//     line in Page0's width (CTextureFont::DrawFormattedText), and then it ends at the last space,
//     or after that character with no space in it. A '`' or space it ends at is skipped.
//   - DisplayText (@00408719): Page0 and Page1 the two pages from SpellBookIndex (the left one,
//     +0x2e8), Page0Number and Page1Number theirs from 1.
//   - ShowButtons (@00408aa4): RightButton enabled while there's a page two on (and the left page
//     under 98), LeftButton off the first page.
//   - WndNotification (@004086b2): DoneButton deactivates, RightButton and LeftButton turn two pages;
//     HandleKeyboardMsg (@00408b03): Right and Left (no Shift, Ctrl or Alt) do what an enabled
//     button would.
//   - The page isn't reset for another book: it opens where the last one was left.
//   - Escape closes it (CDisplay_NewUIProcessEscape, after the trade window).

#include "widgets.h"

#include <array>
#include <cstdint>
#include <string>

class TextRenderer;
class WindowManager;

class BookWindow
{
public:
	void Attach(WindowManager &windows, TextRenderer &text);
	// EQ_Note::SendTextRequestMsg's half for a book: true when its text should be asked for.
	// `which` tells one item from another (the slot and name); `title` is its name.
	bool Check(const std::string &which, const std::string &title);
	void SetBook(const std::string &text); // the zone's answer, then Activate
	void Deactivate();
	bool Active() const;
	// A click on one of its buttons; false if it isn't the book window's.
	bool Click(const std::string &window, const std::string &control);
	// Right and Left while it has the keyboard; true when the key's taken.
	bool Key(InputEvent::Key key, bool shift, bool ctrl, bool alt);

	static constexpr int Pages = 100;
	static constexpr int LinesPerPage = 12;
	int Page() const { return m_page; }
	const std::string &PageText(int page) const;

private:
	void Format();
	void ShowButtons();
	void DisplayText();
	bool Fits(const std::string &line) const; // one line in Page0's width and font

	WindowManager *m_windows = nullptr;
	TextRenderer *m_text = nullptr;
	std::string m_checked;  // +0x6f0: the item last asked about ("": none)
	std::string m_title;    // +0x2ed
	std::string m_contents; // +0x138
	std::array<std::string, Pages> m_pages;
	int m_page = 0; // +0x2e8, the left page
};
