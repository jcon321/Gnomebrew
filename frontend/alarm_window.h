#pragma once

// eqgame's alarm window (CAlarmWnd @00403bc2 on, EQUI_AlarmWnd.xml's "AlarmWnd"):
//   - The constructor: ALRW_HoursEditbox digits only (AlphaNumericOnly 3), 2 characters at most;
//     ALRW_MinutesEditbox digits only too (its SetMaxChars is the hours box's again, so it keeps
//     its own limit).
//   - Activate (@00403d13), from CDisplay_ActivateMainUI each time the game's UI goes up: once a
//     run (DAT_00807414), shown if [Defaults] ShowAlarmWindow (eqclient.ini; my-client.ini here)
//     starts with t, T or 1, "true" with no key.
//   - WndNotification (@00403dc9), a click: ALRW_OkButton sscanf's "%d" of each box (nothing, or
//     under 0, is 0; minutes over 59 carry into the hours), keeps them (DAT_00807df4,
//     DAT_00807e00), arms the alarm (DAT_0080640c) for time() + (hours * 60 + minutes) * 60
//     (DAT_008090a4), then SaveAlarmWindowVisibilitySetting and hides it; ALRW_CancelButton saves
//     and hides it. SaveAlarmWindowVisibilitySetting (@00403d93) writes ShowAlarmWindow "false"
//     when ALRW_NeverShowAgainCheckbox is checked, else "true". The close box only hides it.
//   - ProcessAlarmTrigger (@00403f34), its OnProcessFrame: armed and past the time, wave 0x8d and
//     the confirmation dialog (category 0x1d, OK only, no timeout) with eqstr 6685 "Your session
//     (%1 hours and %2 minutes) has ended.", and disarmed.
//   - /played (display_played @004f4e19) ends with 6684 "Time until alarm: %1" while it's armed.

#include <cstdint>
#include <ctime>
#include <functional>
#include <string>
#include <vector>

class WindowManager;

class AlarmWindow
{
public:
	static constexpr const char *Screen = "AlarmWnd";

	struct Host
	{
		// eqstr_en.txt's string `id` with its %1.. filled in, or `fallback`.
		std::function<std::string(uint32_t, const std::vector<std::string> &, const std::string &)> text;
		std::function<std::string()> show_setting;                // [Defaults] ShowAlarmWindow, "" none
		std::function<void(const std::string &)> save_setting;    // its new value
		std::function<void(int)> wave;                            // EqSoundManager::WavePlay
		std::function<void(const std::string &)> tell;            // CConfirmationDialog::Activate(0x1d, 0, text)
		std::function<std::time_t()> now;                         // time()
	};

	void Attach(WindowManager &windows, Host host); // the constructor's, hidden
	void Activate();                                // CDisplay_ActivateMainUI's
	bool Click(const std::string &window, const std::string &control);
	void Frame(); // ProcessAlarmTrigger
	bool Armed() const { return m_armed; }
	int SecondsLeft() const; // DAT_008090a4 - time()

private:
	void SaveVisibility();

	WindowManager *m_windows = nullptr;
	Host m_host;
	// eqgame's globals: they outlast the UI (a new zone's windows).
	bool m_shown_once = false; // DAT_00807414
	bool m_armed = false;      // DAT_0080640c
	std::time_t m_at = 0;      // DAT_008090a4
	int m_hours = 0, m_minutes = 0;
};
