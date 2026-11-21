#include "alarm_window.h"

#include "window_manager.h"

#include <algorithm>
#include <cstdio>

namespace
{
	SidlWindow::Control *Child(WindowManager *windows, const char *id)
	{
		SidlWindow *wnd = windows ? windows->Find(AlarmWindow::Screen) : nullptr;
		return wnd ? wnd->Find(id) : nullptr;
	}

	// sscanf's "%d": 0 when there's no number.
	int ScanInt(const std::string &text)
	{
		int n = 0;
		if (std::sscanf(text.c_str(), "%d", &n) != 1)
			n = 0;
		return n;
	}
}

void AlarmWindow::Attach(WindowManager &windows, Host host)
{
	m_windows = &windows;
	m_host = std::move(host);
	windows.Show(Screen, false);
	if (SidlWindow::Control *hours = Child(m_windows, "ALRW_HoursEditbox")) {
		hours->edit_filter = 3;
		hours->max_chars = 2;
	}
	if (SidlWindow::Control *minutes = Child(m_windows, "ALRW_MinutesEditbox"))
		minutes->edit_filter = 3;
}

void AlarmWindow::Activate()
{
	if (m_shown_once || !m_windows || !m_windows->Find(Screen))
		return;
	std::string value = m_host.show_setting ? m_host.show_setting() : std::string();
	if (value.empty())
		value = "true";
	if (value[0] == 't' || value[0] == 'T' || value[0] == '1') {
		m_windows->Show(Screen, true);
		m_windows->Raise(Screen);
		m_shown_once = true;
	}
}

void AlarmWindow::SaveVisibility()
{
	const SidlWindow::Control *never = Child(m_windows, "ALRW_NeverShowAgainCheckbox");
	if (m_host.save_setting)
		m_host.save_setting(!never || never->checked ? "false" : "true");
}

bool AlarmWindow::Click(const std::string &window, const std::string &control)
{
	if (window != Screen)
		return false;
	if (control == "ALRW_OkButton") {
		int hours = 0, minutes = 0;
		if (const SidlWindow::Control *box = Child(m_windows, "ALRW_HoursEditbox"))
			hours = std::max(ScanInt(box->text), 0);
		if (const SidlWindow::Control *box = Child(m_windows, "ALRW_MinutesEditbox")) {
			minutes = std::max(ScanInt(box->text), 0);
			if (minutes > 59) {
				hours += minutes / 60;
				minutes %= 60;
			}
		}
		m_hours = hours;
		m_minutes = minutes;
		m_armed = true;
		m_at = (m_host.now ? m_host.now() : std::time(nullptr)) + (static_cast<std::time_t>(hours) * 60 + minutes) * 60;
		SaveVisibility();
		m_windows->Show(Screen, false);
	}
	else if (control == "ALRW_CancelButton") {
		SaveVisibility();
		m_windows->Show(Screen, false);
	}
	return true;
}

void AlarmWindow::Frame()
{
	if (!m_armed)
		return;
	std::time_t now = m_host.now ? m_host.now() : std::time(nullptr);
	if (m_at >= now)
		return;
	if (m_host.wave)
		m_host.wave(0x8d);
	// itoa of each into build_token_string_PARAM.
	if (m_host.tell && m_host.text)
		m_host.tell(m_host.text(6685, {std::to_string(m_hours), std::to_string(m_minutes)},
			"Your session (%1 hours and %2 minutes) has ended."));
	m_armed = false;
}

int AlarmWindow::SecondsLeft() const
{
	std::time_t now = m_host.now ? m_host.now() : std::time(nullptr);
	return static_cast<int>(m_at - now);
}
