#include "loadskin_window.h"

#include "window_manager.h"

namespace
{
	const char *const SkinList = "SkinList";
	const char *const LoadButton = "LoadButton";
	const char *const IniButton = "INIButton";

	SidlWindow::Control *Child(WindowManager *windows, const char *id)
	{
		SidlWindow *wnd = windows ? windows->Find(LoadskinWindow::Screen) : nullptr;
		return wnd ? wnd->Find(id) : nullptr;
	}
}

void LoadskinWindow::Attach(WindowManager &windows, Host host)
{
	m_windows = &windows;
	m_host = std::move(host);
	m_active = false;
	if (!windows.Find(Screen)) // no SidlPiece: the constructor makes no children
		return;
	windows.Show(Screen, false);
	if (SidlWindow::Control *load = Child(m_windows, LoadButton))
		load->enabled = false;
	if (SidlWindow::Control *ini = Child(m_windows, IniButton))
		ini->checked = true;
	if (SidlWindow::Control *list = Child(m_windows, SkinList)) {
		list->row_color = 0xffffff;
		list->hover_color = 0xffff00;
		list->select_color = 0x808080;
		list->justification = {0};
	}
}

void LoadskinWindow::Activate()
{
	if (!m_windows || !m_windows->Find(Screen))
		return;
	UpdateSkinList();
	m_windows->Show(Screen, true);
	m_windows->Raise(Screen);
	m_active = true;
}

void LoadskinWindow::UpdateSkinList()
{
	if (SidlWindow::Control *list = Child(m_windows, SkinList)) {
		list->selected_row = list->selected_column = -1; // ClearAllSel
		list->rows.clear();                               // vtable 0x114, DeleteAll
		list->row_colors.clear();
		list->scroll_pos = 0;
		if (m_host.skins)
			for (const std::string &skin : m_host.skins())
				list->rows.push_back({skin}); // AddString
	}
	if (SidlWindow::Control *load = Child(m_windows, LoadButton))
		load->enabled = false;
}

bool LoadskinWindow::Click(const std::string &window, const std::string &control)
{
	if (window != Screen)
		return false;
	SidlWindow::Control *list = Child(m_windows, SkinList);
	std::string skin;
	if (list && list->selected_row >= 0 && list->selected_row < static_cast<int>(list->rows.size()) &&
	    !list->rows[list->selected_row].empty())
		skin = list->rows[list->selected_row][0];
	if (control == SkinList) {
		if (SidlWindow::Control *load = Child(m_windows, LoadButton))
			load->enabled = true;
		return true;
	}
	if (control == LoadButton) {
		const SidlWindow::Control *ini = Child(m_windows, IniButton);
		if (m_host.loadskin)
			m_host.loadskin(skin + " " + (ini && ini->checked ? '1' : '0'));
		return true;
	}
	return false;
}
