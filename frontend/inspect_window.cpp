#include "inspect_window.h"

#include "edit_box.h"
#include "game_windows.h"
#include "ini_file.h"
#include "window_manager.h"

#include <cstring>

namespace
{
	constexpr size_t NamesAt = 4, NameSize = 64, IconsAt = 0x584, InspectTextAt = 0x5b0, InspectTextSize = 256;
	constexpr int FirstSlotType = 8000; // INSW_InvSlot<n>'s EQType: 8000 + n
	constexpr uint64_t CheckMs = 1000, AnswerMs = 1000;

	std::string CString(const std::vector<uint8_t> &d, size_t at, size_t size)
	{
		if (at >= d.size())
			return {};
		size = std::min(size, d.size() - at);
		const char *p = reinterpret_cast<const char *>(d.data() + at);
		return std::string(p, strnlen(p, size));
	}
}

void InspectWindow::Attach(WindowManager &windows, ZoneSession &zone, const GameValues &values, Host host)
{
	m_windows = &windows;
	m_zone = &zone;
	m_values = &values;
	m_host = std::move(host);
	Forget();
	m_active = false;
	windows.Show(Screen, false);
	if (SidlWindow *wnd = windows.Find(Screen))
		if (SidlWindow::Control *edit = wnd->Find(EditId))
			edit->max_chars = 254; // English (DAT_00799770 0); 84 otherwise
}

void InspectWindow::Load(const IniFile &ini)
{
	// loadCustomFromINI: a value that starts with '*' (*NULL*, no key) leaves ours as it was.
	std::string text = ini.Get("InspectText", "Text", "*NULL*");
	if (!text.empty() && text[0] != '*')
		m_text = text.substr(0, 255);
}

void InspectWindow::Store(IniFile &ini) const
{
	// saveCustom: an empty text writes no value (the key goes).
	if (m_text.empty())
		ini.Remove("InspectText", "Text");
	else
		ini.Set("InspectText", "Text", m_text);
}

void InspectWindow::Reset()
{
	if (m_active)
		Deactivate();
	m_answered = false;
}

float InspectWindow::Distance(uint16_t id) const
{
	// CDisplay::PlayerDistance from us; ourselves 0.
	return id == m_zone->MySpawnId() ? 0.0f : m_zone->SpawnDistance(id);
}

std::string InspectWindow::Say(uint32_t id, const std::vector<std::string> &args, const std::string &fallback) const
{
	return m_host.text ? m_host.text(id, args, fallback) : fallback;
}

void InspectWindow::Forget()
{
	m_check_ms = 0;
	m_inspected = 0;
	m_has.fill(false);
}

void InspectWindow::DoInspect(uint16_t spawn)
{
	auto it = m_zone->Spawns().find(spawn);
	if (it == m_zone->Spawns().end() || it->second.npc != 0)
		return;
	if (Distance(spawn) >= Range) {
		m_host.chat(Say(12378, {it->second.name}, "%1 is too far away to inspect.  Get closer."), 0x111);
		return;
	}
	m_zone->InspectRequest(spawn);
}

void InspectWindow::Requested(uint16_t from, uint64_t now_ms)
{
	if (m_answered && now_ms < m_answered_ms + AnswerMs)
		return;
	m_answered = true;
	m_answered_ms = now_ms;
	auto it = m_zone->Spawns().find(from);
	if (it != m_zone->Spawns().end() && from != m_zone->MySpawnId() && show_message)
		m_host.chat(Say(1404, {it->second.name}, "%1 is looking at your equipment..."), 0x111);
	m_zone->InspectAnswer(from, m_text);
}

void InspectWindow::Answered(const ZoneSession::Event &event)
{
	SidlWindow *wnd = m_windows->Find(Screen);
	if (!wnd)
		return;
	Forget();
	bool ours = event.id == m_zone->MySpawnId();
	auto it = m_zone->Spawns().find(event.id);
	if (!ours && it == m_zone->Spawns().end())
		return;
	m_inspected = event.id;
	const std::string &name = ours ? m_zone->Self().name : it->second.name;
	if (Distance(event.id) > Range) {
		m_host.chat(Say(1402, {name}, "%1 is too far away to inspect.  Get closer."), 0x111);
		return;
	}
	std::string line = Say(1401, {name}, "You are inspecting %1.");
	wnd->Root().text = line;
	if (SidlWindow::Control *edit = wnd->Find(EditId)) {
		if (!ours)
			m_host.chat(line, 10);
		edit->editable = ours;
		edit->text = CString(event.data, InspectTextAt, InspectTextSize);
		if (!ours)
			wnd->Focus("");
	}
	for (int n = 0; n < Slots; ++n) {
		std::string item = CString(event.data, NamesAt + n * NameSize, NameSize);
		if (item.empty())
			continue;
		m_has[n] = true;
		m_items[n] = ZoneItem{};
		m_items[n].slot = static_cast<int16_t>(FirstSlotType + n);
		m_items[n].name = item;
		if (IconsAt + n * 2 + 2 <= event.data.size())
			std::memcpy(&m_items[n].icon, event.data.data() + IconsAt + n * 2, 2);
	}
	m_windows->Show(Screen, true);
	m_windows->Raise(Screen);
	m_active = true;
	m_check_ms = 0;
}

void InspectWindow::SpawnRemoved(uint16_t id)
{
	if (m_active && id != 0 && id == m_inspected)
		Deactivate();
}

void InspectWindow::Frame(uint64_t now_ms)
{
	if (!m_windows)
		return;
	SidlWindow *wnd = m_windows->Find(Screen);
	if (!wnd)
		return;
	if (m_active && !m_windows->Shown(Screen)) { // the close box
		Deactivate();
		return;
	}
	if (m_active && now_ms >= m_check_ms) {
		m_check_ms = now_ms + CheckMs;
		if (m_inspected != 0 && Distance(m_inspected) > Range) {
			Deactivate();
			return;
		}
	}
	if (!m_active)
		return;
	for (int n = 1; n < Slots; ++n)
		if (SidlWindow::Control *c = wnd->Find("InvSlot" + std::to_string(n)))
			FillInvSlot(*c, m_has[n] ? &m_items[n] : nullptr, *m_values);
}

void InspectWindow::AcceptInspectText()
{
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	const SidlWindow::Control *edit = wnd ? wnd->Find(EditId) : nullptr;
	if (!edit || m_inspected == 0 || m_inspected != m_zone->MySpawnId())
		return;
	m_text = edit->text.substr(0, 255);
	if (m_host.save)
		m_host.save();
}

void InspectWindow::Deactivate()
{
	AcceptInspectText();
	Forget();
	m_active = false;
	if (m_windows) {
		if (SidlWindow *wnd = m_windows->Find(Screen))
			wnd->Focus("");
		m_windows->Show(Screen, false);
	}
}

bool InspectWindow::Click(const std::string &window, const std::string &control)
{
	if (window != Screen)
		return false;
	if (control == "DoneButton")
		Deactivate();
	return true;
}

bool InspectWindow::Enter()
{
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	if (!wnd || !wnd->Typing())
		return false;
	AcceptInspectText();
	return true;
}
