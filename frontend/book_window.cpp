#include "book_window.h"
#include "sidl.h"
#include "text.h"
#include "window_manager.h"

#include <string>

namespace
{
	const char *const Screen = "BookWindow";
}

void BookWindow::Attach(WindowManager &windows, TextRenderer &text)
{
	m_windows = &windows;
	m_text = &text;
}

bool BookWindow::Check(const std::string &which, const std::string &title)
{
	Deactivate();
	if (which == m_checked) {
		m_checked.clear();
		return false;
	}
	m_checked = which;
	m_title = title;
	return true;
}

void BookWindow::SetBook(const std::string &text)
{
	if (!m_windows)
		return;
	m_contents = text;
	if (SidlWindow *wnd = m_windows->Find(Screen))
		wnd->Root().text = m_title;
	Format();
	// Activate.
	ShowButtons();
	DisplayText();
	m_windows->Show(Screen, true);
	m_windows->Raise(Screen);
}

void BookWindow::Deactivate()
{
	if (m_windows)
		m_windows->Show(Screen, false);
}

bool BookWindow::Active() const
{
	return m_windows && m_windows->Shown(Screen);
}

const std::string &BookWindow::PageText(int page) const
{
	static const std::string none;
	return page >= 0 && page < Pages ? m_pages[page] : none;
}

bool BookWindow::Fits(const std::string &line) const
{
	// DrawFormattedText's line count in Page0's client width: more than one is too long.
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	const SidlWindow::Control *page = wnd ? wnd->Find("Page0") : nullptr;
	if (!page || !m_text)
		return true;
	int width = page->client.w > 0 ? page->client.w : page->def->rect.w;
	int font = page->def->font;
	return m_text->Measure(line, SidlFontHeight(font), SidlFontWeight(font)).w <= width;
}

void BookWindow::Format()
{
	for (std::string &page : m_pages)
		page.clear();
	const std::string &text = m_contents;
	size_t at = 0;
	for (int page = 0; at < text.size() && page < Pages; ++page) {
		for (int line = 0; line < LinesPerPage; ++line) {
			// Characters on until a '`', the end, or the line's too long; then back to its last
			// space, if it had one and it was cut short.
			size_t p = at, space = std::string::npos;
			std::string shown;
			while (p < text.size() && text[p] != '`') {
				if (text[p] == ' ')
					space = p;
				shown += text[p];
				++p;
				if (!Fits(shown))
					break;
			}
			size_t end = p;
			if (space != std::string::npos && p < text.size() && text[p] != '`')
				end = space;
			m_pages[page] += text.substr(at, end - at);
			m_pages[page] += '\n';
			at = end;
			if (at >= text.size())
				break;
			if (text[at] == '`' || text[at] == ' ')
				++at;
		}
	}
}

void BookWindow::ShowButtons()
{
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	if (!wnd)
		return;
	if (SidlWindow::Control *right = wnd->Find("RightButton"))
		right->enabled = m_page < Pages - 2 && !m_pages[m_page + 2].empty();
	if (SidlWindow::Control *left = wnd->Find("LeftButton"))
		left->enabled = m_page != 0;
}

void BookWindow::DisplayText()
{
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	if (!wnd)
		return;
	if (SidlWindow::Control *number = wnd->Find("Page0Number"))
		number->text = std::to_string(m_page + 1);
	if (SidlWindow::Control *number = wnd->Find("Page1Number"))
		number->text = std::to_string(m_page + 2);
	if (SidlWindow::Control *page = wnd->Find("Page0"))
		page->text = PageText(m_page);
	if (SidlWindow::Control *page = wnd->Find("Page1"))
		page->text = PageText(m_page + 1);
}

bool BookWindow::Click(const std::string &window, const std::string &control)
{
	if (window != Screen)
		return false;
	if (control == "DoneButton")
		Deactivate();
	else if (control == "RightButton" || control == "LeftButton") {
		m_page += control == "RightButton" ? 2 : -2;
		DisplayText();
		ShowButtons();
	}
	return true;
}

bool BookWindow::Key(InputEvent::Key key, bool shift, bool ctrl, bool alt)
{
	if (!Active() || m_windows->TopShown() != Screen || shift || ctrl || alt)
		return false;
	SidlWindow *wnd = m_windows->Find(Screen);
	// DIK_LEFT (0xcb) and DIK_RIGHT (0xcd).
	const char *button = key == InputEvent::Key::Left ? "LeftButton" : key == InputEvent::Key::Right ? "RightButton" : nullptr;
	const SidlWindow::Control *control = button && wnd ? wnd->Find(button) : nullptr;
	if (!control || !control->enabled)
		return false;
	return Click(Screen, button);
}
