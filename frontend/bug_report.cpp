#include "bug_report.h"

#include "window_manager.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace
{
	// The table at 0x5fc120: {id, name eqstr, prompt eqstr}.
	struct BugType
	{
		int id;
		uint32_t name;
		uint32_t prompt;
		const char *english;
	};
	constexpr BugType BugTypes[] = {
		{0, 3165, 3176, "Other"},         {1, 3155, 3166, "Video"},         {2, 3156, 3167, "Audio"},
		{3, 3157, 3168, "Pathing"},       {4, 3158, 3169, "Quest"},         {5, 3159, 3170, "Tradeskills"},
		{6, 3160, 3171, "Spell stacking"}, {7, 3161, 3172, "Doors/Portals"}, {8, 3162, 3173, "Items"},
		{9, 3163, 3174, "NPC"},           {10, 3164, 3175, "Dialogs"},
	};
	constexpr int BugTypeCount = static_cast<int>(sizeof(BugTypes) / sizeof(BugTypes[0]));

	const char *PromptText =
		"This window is for the use of reporting bugs that affect game play such as text errors, graphical issues, "
		"sound issues, and incorrect functionality. Please provide steps in which to reproduce these issues. If you "
		"have an item loss, experience loss, or character death issue please use the /petition command for in game "
		"support. If you are having connectivity or system issues, please contact your ISP or Tech Support. If you "
		"have suggestions concerning balance or game play issues please utilize the /feedback command.";

	constexpr size_t MaxChars = 1023; // SetMaxChars 0x3ff (English)

	void PutString(std::vector<uint8_t> &body, size_t at, size_t size, const std::string &text)
	{
		std::memcpy(body.data() + at, text.data(), std::min(text.size(), size - 1));
	}

	void PutU32(std::vector<uint8_t> &body, size_t at, uint32_t value)
	{
		std::memcpy(body.data() + at, &value, 4);
	}

	void PutFloat(std::vector<uint8_t> &body, size_t at, float value)
	{
		std::memcpy(body.data() + at, &value, 4);
	}

	SidlWindow::Control *Child(WindowManager *windows, const char *screen, const char *id)
	{
		SidlWindow *wnd = windows ? windows->Find(screen) : nullptr;
		return wnd ? wnd->Find(id) : nullptr;
	}
}

void BugReport::Attach(WindowManager &windows, Host host)
{
	m_windows = &windows;
	m_host = std::move(host);
	m_target = {};
	m_y = m_x = m_z = 0;
	windows.Show(Screen, false);
	if (SidlWindow::Control *edit = Child(m_windows, Screen, "Edit"))
		edit->max_chars = MaxChars;
	int chosen = 0;
	if (SidlWindow::Control *types = Child(m_windows, Screen, "BugTypes")) {
		types->choices.clear();
		for (int i = 0; i < BugTypeCount; ++i) {
			types->choices.push_back(m_host.text(BugTypes[i].name, BugTypes[i].english));
			if (BugTypes[i].id == 0)
				chosen = i;
		}
		types->choice = chosen;
	}
	SetPrompt(chosen);
}

void BugReport::SetPrompt(int choice)
{
	if (SidlWindow::Control *prompt = Child(m_windows, Screen, "Prompt"))
		if (choice >= 0 && choice < BugTypeCount)
			prompt->text = m_host.text(BugTypes[choice].prompt, PromptText);
}

void BugReport::Activate()
{
	if (!m_windows || !m_windows->Find(Screen))
		return;
	m_y = m_x = m_z = 0;
	if (m_host.position)
		m_host.position(m_y, m_x, m_z);
	auto show = [&](const char *id, float value) {
		if (SidlWindow::Control *label = Child(m_windows, Screen, id)) {
			char text[32];
			std::snprintf(text, sizeof(text), "%.1f", value);
			label->text = text;
		}
	};
	show("LocX", m_y);
	show("LocY", m_x);
	show("LocZ", m_z);
	for (const char *id : {"CrashButton", "DupButton", "FlagsButton", "TargetButton"})
		if (SidlWindow::Control *button = Child(m_windows, Screen, id))
			button->checked = false;
	if (SidlWindow::Control *name = Child(m_windows, Screen, "TargetName"))
		name->text.clear();
	m_windows->Show(Screen, true);
	m_windows->Raise(Screen);
}

bool BugReport::Click(const std::string &window, const std::string &control)
{
	if (window != Screen)
		return false;
	if (control == "BugTypes") {
		if (const SidlWindow::Control *types = Child(m_windows, Screen, "BugTypes"))
			SetPrompt(types->choice);
	}
	else if (control == "CancelButton")
		m_windows->Show(Screen, false);
	else if (control == "TargetButton") {
		SidlWindow::Control *button = Child(m_windows, Screen, "TargetButton");
		SidlWindow::Control *name = Child(m_windows, Screen, "TargetName");
		Target target;
		if (button && !button->checked) {
			if (name)
				name->text.clear();
		}
		else if (!m_host.target || !m_host.target(target)) {
			if (button)
				button->checked = false;
		}
		else {
			m_target = target;
			if (name)
				name->text = target.display;
		}
	}
	else if (control == "SendButton")
		Send();
	return true;
}

void BugReport::Send()
{
	SidlWindow::Control *edit = Child(m_windows, Screen, "Edit");
	if (edit && !edit->text.empty()) {
		std::vector<uint8_t> body(PacketSize, 0);
		int choice = 0;
		if (const SidlWindow::Control *types = Child(m_windows, Screen, "BugTypes"))
			choice = std::clamp(BugTypes[std::clamp(types->choice, 0, BugTypeCount - 1)].id, 0, BugTypeCount - 1);
		PutString(body, 0, 64, m_host.text(BugTypes[choice].name, BugTypes[choice].english));
		PutString(body, 64, 64, m_host.name ? m_host.name() : std::string());
		PutFloat(body, 160, m_y);
		PutFloat(body, 164, m_x);
		PutFloat(body, 168, m_z);
		PutU32(body, 176, m_host.minutes_played ? m_host.minutes_played() : 0);
		uint32_t flags = 0;
		auto checked = [&](const char *id) {
			const SidlWindow::Control *button = Child(m_windows, Screen, id);
			return button && button->checked;
		};
		if (checked("CrashButton"))
			flags = 2;
		if (checked("DupButton"))
			flags |= 1;
		if (checked("FlagsButton"))
			flags |= 8;
		if (checked("TargetButton")) {
			PutU32(body, 188, m_target.id);
			flags |= 4;
			PutString(body, 192, 64, m_target.name);
		}
		PutU32(body, 256, flags);
		// strncpy of 0x402 then UTF8_truncate to 0x400: 1024 bytes and its 0 at most.
		std::memcpy(body.data() + 2312, edit->text.data(), std::min<size_t>(edit->text.size(), 1023));
		if (m_host.send)
			m_host.send(body);
		edit->text.clear();
	}
	m_windows->Show(Screen, false);
}

void Feedback::Attach(WindowManager &windows, Host host)
{
	m_windows = &windows;
	m_host = std::move(host);
	if (SidlWindow::Control *name = Child(m_windows, Screen, "FBKW_UserName"))
		name->text = m_host.name ? m_host.name() : std::string();
	if (SidlWindow::Control *input = Child(m_windows, Screen, "FBKW_FeedbackInput"))
		input->max_chars = MaxChars;
	windows.Show(Screen, false);
}

void Feedback::Activate()
{
	if (!m_windows || !m_windows->Find(Screen))
		return;
	m_windows->Show(Screen, true);
	m_windows->Raise(Screen);
}

bool Feedback::Click(const std::string &window, const std::string &control)
{
	if (window != Screen)
		return false;
	SidlWindow::Control *input = Child(m_windows, Screen, "FBKW_FeedbackInput");
	if (control == "FBKW_SendButton") {
		if (input && !input->text.empty()) {
			std::vector<uint8_t> body(PacketSize, 0);
			PutString(body, 0, 64, m_host.name ? m_host.name() : std::string());
			std::memcpy(body.data() + 107, input->text.data(), std::min<size_t>(input->text.size(), 1023));
			if (m_host.send)
				m_host.send(body);
			input->text.clear();
		}
		m_windows->Show(Screen, false);
	}
	else if (control == "FBKW_CancelButton") {
		if (input)
			input->text.clear();
		m_windows->Show(Screen, false);
	}
	return true;
}
