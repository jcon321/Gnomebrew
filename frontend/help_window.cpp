#include "help_window.h"

#include "window_manager.h"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace
{
	SidlWindow::Control *Child(WindowManager *windows, const char *id)
	{
		SidlWindow *wnd = windows ? windows->Find(HelpWindow::Screen) : nullptr;
		return wnd ? wnd->Find(id) : nullptr;
	}
}

void HelpWindow::Attach(WindowManager &windows, const std::string &eq_dir)
{
	m_windows = &windows;
	m_eq_dir = eq_dir;
	m_history.clear();
	m_place = -1;
	m_checked_ms = 0;
	if (SidlWindow::Control *back = Child(m_windows, "BackButton"))
		back->enabled = false;
}

void HelpWindow::SetFile(const std::string &file)
{
	m_file = file;
	if (Child(m_windows, "Display"))
		LoadPage(file);
}

void HelpWindow::LoadPage(const std::string &file)
{
	std::string path = file;
	std::replace(path.begin(), path.end(), '\\', '/');
	if (!m_eq_dir.empty())
		path = m_eq_dir + "/" + path;
	std::ifstream in(path, std::ios::binary);
	if (!in)
		return;
	std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	if (text.size() >= 3 && text.compare(0, 3, "\xef\xbb\xbf") == 0)
		text.erase(0, 3);
	++m_place;
	m_history.resize(static_cast<size_t>(m_place) + 1);
	m_history[m_place] = text;
	Show(text);
}

void HelpWindow::Show(const std::string &stml)
{
	if (SidlWindow::Control *display = Child(m_windows, "Display")) {
		display->text = stml;
		display->scroll_pos = 0;
		display->link_hover = display->link_pressed = -1;
	}
}

void HelpWindow::UpdateBack()
{
	if (SidlWindow::Control *back = Child(m_windows, "BackButton"))
		back->enabled = m_place > 0;
}

bool HelpWindow::Click(const std::string &window, const std::string &control)
{
	if (window != Screen)
		return false;
	if (control == "BackButton") {
		if (m_place > 0) {
			--m_place;
			Show(m_history[m_place]);
		}
		UpdateBack();
	}
	else if (control == "Contents")
		LoadPage(m_file);
	else if (control == "Display") {
		if (SidlWindow::Control *display = Child(m_windows, "Display"); display && !display->link_clicked.empty())
			LoadPage(display->link_clicked);
	}
	return true;
}

void HelpWindow::Frame(uint64_t now_ms)
{
	if (now_ms - m_checked_ms <= 250)
		return;
	UpdateBack();
	m_checked_ms = now_ms;
}
