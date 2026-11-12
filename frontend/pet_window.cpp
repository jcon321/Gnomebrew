#include "pet_window.h"
#include "window_manager.h"
#include "../core/zone_session.h"

#include <cctype>

namespace
{
	constexpr uint64_t UpdateMs = 250; // OnProcessFrame's 0xfa

	// GetButton's 1..7 (the constructor's GetChildItem names, 0x13c..0x154).
	constexpr const char *ButtonIds[PetWindow::Buttons] = {
		"AttackButton", "GuardButton", "LostButton", "SitButton", "BackButton", "FollowButton", "TauntButton",
	};

	// What each button does: WndNotification's eqstr ids (the sit button's is its own text).
	constexpr uint32_t ButtonWords[PetWindow::Buttons] = {13092, 7506, 7504, 0, 7500, 7508, 7502};

	// The /pet words' fallbacks, for an eqstr_en.txt without them.
	const char *Fallback(uint32_t id)
	{
		switch (id) {
		case 7500: return "back off";
		case 7501: return "calm down";
		case 7502: return "taunt";
		case 7503: return "notaunt";
		case 7504: return "get lost";
		case 7505: return "as you were";
		case 7506: return "guard here";
		case 7507: return "guard me";
		case 7508: return "follow me";
		case 7509: return "no taunt";
		case 7510: return "sit down";
		case 7511: return "stand up";
		case 7512: return "report health";
		case 7513: return "hold";
		case 7514: return "taunt off";
		case 7515: return "health report";
		case 7516: return "health";
		case 7517: return "taunt on";
		case 7518: return "sit";
		case 7519: return "stand";
		case 7520: return "leader";
		case 7521: return "who leader";
		case 3272: return "feign";
		case 13091: return "You don't have a pet to command!";
		case 13092: return "attack";
		case 13093: return "kill";
		case 13094: return "kick";
		case 13095: return "maim";
		case 13096: return "slay";
		case 13097: return "murder";
		case 13098: return "execute";
		case 13099: return "sack";
		case 13100: return "destroy";
		case 13101: return "whack";
		case 13103: return "target";
		case 13104: return "Targeting your pet.";
		default: return "";
		}
	}

	int Lower(char c)
	{
		return std::tolower(static_cast<unsigned char>(c));
	}

	// UTF8_stricmp
	bool Same(const std::string &a, const std::string &b)
	{
		if (a.size() != b.size())
			return false;
		for (size_t i = 0; i < a.size(); ++i)
			if (Lower(a[i]) != Lower(b[i]))
				return false;
		return true;
	}

	// strnicmp(name, start, start's length) == 0
	bool Starts(const char *name, const std::string &start)
	{
		for (size_t i = 0; i < start.size(); ++i)
			if (name[i] == '\0' || Lower(name[i]) != Lower(start[i]))
				return false;
		return true;
	}
}

void PetWindow::Attach(WindowManager &windows, ZoneSession &zone, Host host)
{
	m_windows = &windows;
	m_zone = &zone;
	m_host = std::move(host);
	m_updated_ms = 0;
	m_shown = false;
	Update();
}

const char *PetWindow::ButtonId(int index)
{
	return index >= 1 && index <= Buttons ? ButtonIds[index - 1] : nullptr;
}

int PetWindow::ButtonIndex(const std::string &control)
{
	for (int i = 0; i < Buttons; ++i)
		if (control == ButtonIds[i])
			return i + 1;
	return 0;
}

std::string PetWindow::Word(uint32_t id) const
{
	return m_host.eqstr ? m_host.eqstr(id, Fallback(id)) : Fallback(id);
}

void PetWindow::Frame(uint64_t now_ms)
{
	if (!m_windows)
		return;
	bool shown = m_windows->Shown(Screen);
	if (shown && (!m_shown || now_ms - m_updated_ms > UpdateMs)) {
		Update();
		m_updated_ms = now_ms;
	}
	m_shown = shown;
}

void PetWindow::Update()
{
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	if (!wnd || !m_zone)
		return;
	bool have = false, guard = false, attack = false, back = false, follow = false, sitting = false;
	uint16_t id = m_zone->MyPetId();
	auto it = id != 0xffff && id != 0 ? m_zone->Spawns().find(id) : m_zone->Spawns().end();
	if (it != m_zone->Spawns().end()) {
		const ZoneSpawn &pet = it->second;
		have = true;
		// An animation (race 127 with EQPlayer+0x120): Animation Empathy's rank gives its buttons.
		if (pet.race == 127 && pet.summoned != 0) {
			have = false;
			int rank = m_host.ability ? m_host.ability(0xc3) : 0;
			guard = follow = rank > 0;
			attack = rank > 1;
			back = rank > 2;
		}
		sitting = pet.stance == 110; // StandingState 'n'
	}
	auto enable = [&](const char *button, bool on) {
		if (SidlWindow::Control *control = wnd->Find(button))
			control->enabled = on;
	};
	enable("GuardButton", have || guard);
	enable("AttackButton", have || attack);
	enable("BackButton", have || back);
	enable("SitButton", have);
	enable("LostButton", have);
	enable("FollowButton", have || follow);
	enable("TauntButton", have);
	if (SidlWindow::Control *sit = wnd->Find("SitButton")) {
		std::string text = Word(sitting ? 7519 : 7518);
		if (!text.empty())
			text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
		sit->text = text;
	}
}

bool PetWindow::Click(const std::string &window, const std::string &control)
{
	if (window != Screen)
		return false;
	int index = ButtonIndex(control);
	if (index == 0)
		return false;
	Notify(index);
	return true;
}

void PetWindow::Press(int index)
{
	// GetButton: nothing without the button.
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	const char *id = ButtonId(index);
	if (wnd && id && wnd->Find(id))
		Notify(index);
}

void PetWindow::Notify(int index)
{
	std::string text;
	if (index == 4) { // the sit button: its text (GetWindowText)
		SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
		const SidlWindow::Control *sit = wnd ? wnd->Find("SitButton") : nullptr;
		if (sit)
			text = sit->text;
	}
	else if (index >= 1 && index <= Buttons)
		text = Word(ButtonWords[index - 1]);
	else
		return;
	Command(text);
}

void PetWindow::Command(const std::string &text)
{
	auto is = [&](std::initializer_list<uint32_t> ids) {
		for (uint32_t id : ids)
			if (Same(text, Word(id)))
				return true;
		return false;
	};
	uint32_t command = 0;
	uint16_t target = 0;
	if (is({7500, 7501}))
		command = 1;
	else if (is({7502}))
		command = 11;
	else if (is({7514, 7503, 7509}))
		command = 14;
	else if (is({7517}))
		command = 13;
	else if (is({7504}))
		command = 2;
	else if (is({7505}))
		command = 3;
	else if (is({7506}))
		command = 5;
	else if (is({7507}))
		command = 6;
	else if (is({7508}))
		command = 8;
	else if (is({7510, 7518}))
		command = 9;
	else if (is({7511, 7519}))
		command = 10;
	else if (is({7512, 7515, 7516}))
		command = 4;
	else if (is({7513}))
		command = 12;
	else if (is({13103}))
		command = 15;
	else if (is({7521, 7520}))
		command = 16;
	else if (is({3272}))
		command = 17;
	else {
		// The attack words, 13092..13101: the first the line starts.
		for (uint32_t id = 13092; id <= 13101; ++id) {
			std::string word = Word(id);
			if (word.empty() || !Starts(text.c_str(), word))
				continue;
			size_t at = word.size();
			if (at < text.size()) {
				// A name after it: spaces before it skipped, those in it as '_'.
				while (at < text.size() && text[at] == ' ')
					++at;
				std::string name = text.substr(at);
				for (char &c : name)
					if (c == ' ')
						c = '_';
				uint16_t found = FindByPartialName(name);
				for (const char *format : {"#", "a_", "#a_"})
					if (found == 0)
						found = FindByPartialName(format + name);
				if (found != 0) {
					command = 7;
					target = found;
				}
			}
			else {
				// None: our target, but not us.
				uint16_t t = m_host.target ? m_host.target() : 0;
				if (t != 0 && t != m_zone->MySpawnId()) {
					command = 7;
					target = t;
				}
			}
			break;
		}
	}
	Issue(command, target);
}

uint16_t PetWindow::FindByPartialName(const std::string &name) const
{
	if (!m_zone)
		return 0;
	uint16_t me = m_zone->MySpawnId();
	const ZoneSpawn &self = m_zone->Self();
	// EQPlayer_get_player: one of that name, us too.
	if (!name.empty()) {
		if (me != 0 && Same(self.name, name))
			return me;
		for (const auto &[id, spawn] : m_zone->Spawns())
			if (Same(spawn.name, name))
				return id;
	}
	if (name.empty())
		return 0;
	// The nearest player or NPC (us too) whose name starts with it, or does after a '#'.
	auto starts = [&](const std::string &spawn_name) {
		return Starts(spawn_name.c_str(), name) || (!spawn_name.empty() && spawn_name[0] == '#' && Starts(spawn_name.c_str() + 1, name));
	};
	ZoneMovement at = m_host.position ? m_host.position() : ZoneMovement{};
	uint16_t best = 0;
	float best_d = 9e32f;
	auto consider = [&](uint16_t id, const ZoneSpawn &spawn, float x, float y, float z) {
		if (id == 0 || spawn.npc >= 2 || !starts(spawn.name))
			return;
		float dy = at.y - y, dx = at.x - x, dz = at.z - z;
		float d = dy * dy + dx * dx + dz * dz;
		if (best == 0 || d < best_d) {
			best = id;
			best_d = d;
		}
	};
	consider(me, self, at.x, at.y, at.z);
	for (const auto &[id, spawn] : m_zone->Spawns())
		if (id != me)
			consider(id, spawn, spawn.x, spawn.y, spawn.z);
	return best;
}

void PetWindow::Issue(uint32_t command, uint16_t target)
{
	if (!m_zone)
		return;
	if (command == 15) {
		// The first spawn whose PetOwnerSpawnId is ours, temporary or not.
		uint16_t me = m_zone->MySpawnId();
		for (const auto &[id, spawn] : m_zone->Spawns())
			if (me != 0 && spawn.pet_owner == me) {
				if (m_host.chat)
					m_host.chat(Word(13104));
				if (m_host.set_target)
					m_host.set_target(id);
				return;
			}
		if (m_host.chat)
			m_host.chat(Word(13091));
		return;
	}
	if (command != 0)
		m_zone->PetCommand(command, target);
}
