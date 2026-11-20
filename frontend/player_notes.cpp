#include "player_notes.h"

#include "edit_box.h"
#include "window_manager.h"

#include <cstdio>
#include <fstream>
#include <iterator>

namespace
{
	const std::string ErrorText = "ERROR: Failed to add note, please make sure 'notes.txt' file is not read only.";
}

void PlayerNotes::Attach(WindowManager &windows, const std::string &eq_dir, Host host)
{
	m_windows = &windows;
	m_host = std::move(host);
	m_active = false;
	m_path = eq_dir.empty() ? "notes.txt" : eq_dir + "/notes.txt";
	windows.Show(Screen, false);
	SidlWindow *wnd = windows.Find(Screen);
	SidlWindow::Control *input = wnd ? wnd->Find(InputId) : nullptr;
	if (!input)
		return;
	input->max_chars = MaxChars;
	std::ifstream file(m_path, std::ios::binary);
	if (!file)
		return;
	std::string text;
	char c;
	while (text.size() < MaxChars && file.get(c))
		text += c;
	input->text = text;
}

void PlayerNotes::Command(const std::string &args)
{
	if (args.empty())
		Activate();
	else
		AppendText(args);
}

void PlayerNotes::Activate()
{
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	if (!wnd) {
		m_host.chat(m_host.text(13224, ErrorText));
		return;
	}
	m_windows->Show(Screen, true);
	m_windows->Raise(Screen);
	m_active = true;
}

void PlayerNotes::Deactivate()
{
	if (!m_windows || !m_windows->Find(Screen))
		return;
	Save();
	m_active = false;
	if (SidlWindow *wnd = m_windows->Find(Screen))
		wnd->Focus("");
	m_windows->Show(Screen, false);
}

void PlayerNotes::Frame()
{
	if (m_active && m_windows && !m_windows->Shown(Screen))
		Deactivate();
}

bool PlayerNotes::Save() const
{
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	const SidlWindow::Control *input = wnd ? wnd->Find(InputId) : nullptr;
	if (!input)
		return false;
	std::ofstream file(m_path, std::ios::binary | std::ios::trunc);
	if (!file)
		return false;
	file << input->text;
	return static_cast<bool>(file);
}

void PlayerNotes::AppendText(const std::string &text)
{
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	SidlWindow::Control *input = wnd ? wnd->Find(InputId) : nullptr;
	if (!input) {
		m_host.chat(m_host.text(13224, ErrorText));
		return;
	}
	std::string date = m_host.now ? m_host.now() : std::string();
	if (!date.empty() && date.back() == '\n')
		date.back() = ' ';
	std::string line = "[" + date + "]";
	std::string body = " " + text + "\n";
	// The item links out: a 0x12 then '0' loses those 8 characters, and the next 0x12.
	for (size_t at = body.find('\x12'); at != std::string::npos; at = body.find('\x12', at)) {
		if (at + 1 >= body.size() || body[at + 1] != '0') {
			++at; // eqgame finds the same one again for ever
			continue;
		}
		body.erase(at, 8);
		if (size_t end = body.find('\x12', at); end != std::string::npos)
			body.erase(end, 1);
	}
	line += body;
	std::string all = input->text + line;
	if (all.size() >= MaxChars) {
		m_host.chat(m_host.text(13224, ErrorText));
		return;
	}
	input->text = all;
	Save();
}

bool PlayerNotes::Enter()
{
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	SidlWindow::Control *input = wnd ? wnd->Find(InputId) : nullptr;
	if (!input || !wnd->Typing())
		return false;
	EditBox::ReplaceSelection(*input, "\n", true);
	return true;
}
