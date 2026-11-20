#include "tracking.h"

#include "ini_file.h"
#include "window_manager.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace
{
	constexpr int TrackingSkill = 53, RangerClass = 4;
	constexpr uint64_t ControlsMs = 500, LineMs = 2000, TrackMs = 360000;
	const char *const ListId = "TRW_TrackingList";
	const char *const SortId = "TRW_TrackSortCombobox";
	const char *const PlayersId = "TRW_TrackPlayersCombobox";
	const char *const TrackId = "TRW_TrackButton";
	const char *const DoneId = "DoneButton";
	// The filter buttons and their bits (UpdateTrackingControls, /trackfilter).
	struct FilterButton
	{
		const char *id;
		int bit;
	};
	constexpr FilterButton FilterButtons[] = {
		{"TRW_FilterGreenButton", 1}, {"TRW_FilterBlueButton", 2}, {"TRW_FilterWhiteButton", 4},
		{"TRW_FilterYellowButton", 8}, {"TRW_FilterRedButton", 0x10}, {"TRW_FilterLightBlueButton", 0x20},
	};
	// The consider colors' flags (DoTrackFilter) and the filter bit for each.
	constexpr uint32_t Green = 0x100, LightBlue = 0x200, Blue = 0x80, White = 0x40, Yellow = 0x20, Red = 0x10;
	constexpr uint32_t Hidden = 1, Player = 2, PlayersPet = 4;

	std::string Lower(std::string s)
	{
		for (auto &c : s)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return s;
	}
	char Up(char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
}

void Tracking::Attach(WindowManager &windows, ZoneSession &zone, Host host)
{
	m_windows = &windows;
	m_zone = &zone;
	m_host = std::move(host);
	m_active = false;
	m_entries.fill(Entry{});
	windows.Show(Screen, false);
	SidlWindow *wnd = windows.Find(Screen);
	if (!wnd)
		return;
	if (SidlWindow::Control *list = wnd->Find(ListId)) {
		list->hover_color = 0xffffff; // SetColors(0xffffffff, 0xff8aa3ff)
		list->select_color = 0x8aa3ff;
	}
	if (SidlWindow::Control *sort = wnd->Find(SortId)) {
		sort->choices = {Say(13120, {}, "normal"), Say(13144, {}, "distance"), Say(13145, {}, "consider"),
			Say(13146, {}, "rdistance"), Say(13147, {}, "rconsider")};
		sort->choice = 0;
	}
	if (SidlWindow::Control *players = wnd->Find(PlayersId)) {
		players->choices = {Say(12946, {}, "off"), Say(12947, {}, "on"), Say(13148, {}, "group")};
		players->choice = 0;
	}
}

void Tracking::Load(const IniFile &settings)
{
	std::string players = settings.Get("Defaults", "TrackPlayers", "*");
	char p = players.empty() ? '*' : players[0];
	m_players = p == '*' || p == 'T' || p == 't' || p == '1' ? 1 : p == 'G' || p == 'g' || p == '2' ? 2 : 0;
	std::string sort = settings.Get("Defaults", "TrackSortType", "*");
	char s = sort.empty() ? '*' : Up(sort[0]);
	char s2 = sort.size() > 1 ? Up(sort[1]) : '\0';
	if (s == 'D' || s == '1')
		m_sort = 1;
	else if (s == 'C' || s == '2')
		m_sort = 2;
	else if (s == '3' || (s == 'R' && s2 == 'D'))
		m_sort = 3;
	else if (s == '4' || (s == 'R' && s2 == 'C'))
		m_sort = 4;
	else // '*', N, 0, an R of neither (eqgame keeps the value before it there), anything else
		m_sort = 0;
	m_filter = std::atoi(settings.Get("Defaults", "TrackFilterType", "0").c_str());
}

void Tracking::Reset()
{
	// CEverQuest_EnterZone: no tracking target. The window's entries point at spawns that are gone.
	m_target = 0;
	if (m_host.activity && m_host.activity() == Activity)
		m_host.set_activity(-1);
	if (m_active)
		Deactivate();
	m_entries.fill(Entry{});
	m_row_entries.clear();
}

std::string Tracking::Say(uint32_t id, const std::vector<std::string> &args, const std::string &fallback) const
{
	return m_host.text ? m_host.text(id, args, fallback) : fallback;
}

void Tracking::Line(uint32_t id, const std::string &fallback)
{
	m_host.chat(Say(id, {}, fallback), 0x111);
}

bool Tracking::Ranger() const
{
	return m_zone->Profile().class_id == RangerClass;
}

bool Tracking::InGroup(uint16_t id) const
{
	if (!m_host.group)
		return false;
	std::vector<uint16_t> members = m_host.group();
	return std::find(members.begin(), members.end(), id) != members.end();
}

void Tracking::Activate()
{
	// OnProcessFrame (@00438295): the controls now, then the list, shown.
	m_controls_ms = 0;
	UpdateControls();
	Generate();
	m_windows->Show(Screen, true);
	m_windows->Raise(Screen);
	m_active = true;
}

void Tracking::Deactivate()
{
	m_active = false;
	if (m_windows)
		m_windows->Show(Screen, false);
}

void Tracking::Generate()
{
	const ZoneProfile &me = m_zone->Profile();
	int skill = me.skills[TrackingSkill];
	uint8_t time_type = m_zone->Info().time_type;
	float range;
	if (time_type == 1 || time_type == 2 || time_type == 5) {
		range = static_cast<float>(skill) * 10.0f + 500.0f;
		if (skill > 100)
			range += static_cast<float>(skill - 100) * 10.0f;
	}
	else {
		range = static_cast<float>(skill / 2) + 500.0f;
		if (skill > 100)
			range += static_cast<float>(skill - 100) * 4.0f;
	}
	m_entries.fill(Entry{});
	auto across = [&](const ZoneSpawn &s) { return std::hypot(s.x - me.x, s.y - me.y); };
	int count = 0;
	std::vector<uint16_t> group = m_host.group ? m_host.group() : std::vector<uint16_t>{};
	for (uint16_t id : group) {
		auto it = m_zone->Spawns().find(id);
		if (it == m_zone->Spawns().end() || count >= MaxEntries)
			continue;
		Entry &e = m_entries[count];
		e.id = id;
		e.distance = across(it->second);
		e.index = count++;
	}
	for (const auto &[id, s] : m_zone->Spawns()) {
		if (id == m_zone->MySpawnId() || s.npc >= 2)
			continue;
		if (s.race == 143 || s.race == 141 || s.race == 72 || s.race == 73 || s.race == 114 || s.race == 142)
			continue;
		if ((m_host.ridden && m_host.ridden(id)) || (m_host.invisible && m_host.invisible(s)))
			continue;
		if (std::find(group.begin(), group.end(), id) != group.end() || count >= MaxEntries)
			continue;
		float d = across(s);
		if (d > range)
			continue;
		Entry &e = m_entries[count];
		e.id = id;
		e.distance = d;
		e.index = count++;
	}
	Sort(false);
	Filter(false);
	UpdateList(false);
}

void Tracking::Sort(bool update)
{
	int type = Ranger() ? m_sort : 0;
	auto npc = [&](const Entry &e) -> const ZoneSpawn * {
		if (e.id == 0)
			return nullptr;
		auto it = m_zone->Spawns().find(e.id);
		return it != m_zone->Spawns().end() ? &it->second : nullptr;
	};
	auto is_npc = [&](const Entry &e) {
		const ZoneSpawn *s = npc(e);
		return s && s->npc == 1;
	};
	auto level = [&](const Entry &e) {
		const ZoneSpawn *s = npc(e);
		return s ? s->level : 0;
	};
	for (int i = 0; i < MaxEntries; ++i) {
		Entry &cur = m_entries[i];
		switch (type) {
		case 0:
			// Each entry swapped to its order until the one here belongs here.
			while (cur.index != i && cur.index != -1)
				std::swap(cur, m_entries[cur.index]);
			break;
		case 1:
		case 3:
			for (int j = i + 1; j < MaxEntries; ++j) {
				const Entry &other = m_entries[j];
				if (other.distance < -0.0001f)
					continue;
				if (type == 1 ? other.distance < cur.distance : other.distance > cur.distance)
					std::swap(cur, m_entries[j]);
			}
			break;
		case 2:
		case 4:
			if (!is_npc(cur))
				break;
			for (int j = i + 1; j < MaxEntries; ++j) {
				const Entry &other = m_entries[j];
				if (other.id == 0)
					continue;
				if (!is_npc(other)) {
					std::swap(cur, m_entries[j]);
					break;
				}
				if (type == 2 ? level(cur) < level(other) : level(other) < level(cur))
					std::swap(cur, m_entries[j]);
			}
			break;
		default:
			break;
		}
	}
	if (update)
		UpdateList(false);
}

void Tracking::Filter(bool update)
{
	const ZoneProfile &me = m_zone->Profile();
	for (Entry &e : m_entries) {
		if (e.id == 0)
			continue;
		auto it = m_zone->Spawns().find(e.id);
		if (it == m_zone->Spawns().end())
			continue;
		const ZoneSpawn &s = it->second;
		e.flags = 0;
		uint32_t mine = me.level, theirs = s.level;
		int diff = static_cast<int>(theirs) - static_cast<int>(mine);
		uint32_t gray = mine == 61 ? 48 : mine == 62 ? 51 : mine < 61 ? mine * 3 / 4 : mine - 10;
		if (static_cast<int>(theirs) < static_cast<int>(gray) && diff < -3) {
			e.flags = Green;
			int d = static_cast<int>(theirs) - static_cast<int>(gray);
			uint32_t square = theirs * theirs * 100;
			if (mine < 55)
				--d;
			if (mine < 41)
				--d;
			if (mine < 31)
				--d;
			int32_t add = d == -4 || d == -5 ? static_cast<int32_t>(theirs * theirs * static_cast<uint32_t>(-8000)) / 100
			                                 : static_cast<int32_t>((square / 5) * static_cast<uint32_t>(d));
			if (static_cast<int32_t>(square + static_cast<uint32_t>(add)) > 0 && mine > 7)
				e.flags = LightBlue;
		}
		else if (diff < 0)
			e.flags = Blue;
		else if (theirs == mine)
			e.flags = White;
		else
			e.flags = diff > 2 ? Red : Yellow;
		bool hide = false;
		if (Ranger() && s.npc == 1) {
			uint32_t f = e.flags;
			hide = ((m_filter & 1) && (f & Green)) || ((m_filter & 2) && (f & Blue)) || ((m_filter & 4) && (f & White)) ||
			       ((m_filter & 8) && (f & Yellow)) || ((m_filter & 0x10) && (f & Red)) ||
			       ((m_filter & 0x20) && (f & LightBlue));
		}
		if (s.npc == 0) {
			e.flags |= Player;
			if (m_players == 0)
				hide = true;
			else if (m_players == 1)
				hide = false;
			else if (m_players == 2 && !InGroup(e.id))
				hide = true;
		}
		// A player's pet: its owner (+0x96) there and not an NPC.
		if (uint16_t owner = s.pet_owner; owner != 0) {
			if (owner == m_zone->MySpawnId())
				e.flags |= PlayersPet;
			else if (auto o = m_zone->Spawns().find(owner); o != m_zone->Spawns().end() && o->second.npc != 1)
				e.flags |= PlayersPet;
		}
		if (hide)
			e.flags |= Hidden;
	}
	if (update)
		UpdateList(false);
}

uint32_t Tracking::RowColor(const Entry &e) const
{
	int index;
	if (e.flags & Player)
		index = InGroup(e.id) ? 12 : 7;
	else if (e.flags & Green)
		index = 14;
	else if (e.flags & Blue)
		index = 16;
	else if (e.flags & LightBlue)
		index = 18;
	else if (e.flags & White)
		index = 10;
	else
		index = (e.flags & Yellow) ? 15 : 13;
	return m_host.color ? m_host.color(index) : 0xffffff;
}

void Tracking::UpdateList(bool)
{
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	SidlWindow::Control *list = wnd ? wnd->Find(ListId) : nullptr;
	if (!list)
		return;
	// ClearAllSel and the rows gone; UpdateTrackingList's callers all pass 0, keeping no selection.
	list->selected_row = -1;
	list->rows.clear();
	list->row_colors.clear();
	m_row_entries.clear();
	for (int i = 0; i < MaxEntries; ++i) {
		const Entry &e = m_entries[i];
		if (e.id == 0 || (e.flags & Hidden))
			continue;
		auto it = m_zone->Spawns().find(e.id);
		if (it == m_zone->Spawns().end())
			continue;
		std::string name = it->second.DisplayName();
		if (e.flags & PlayersPet)
			name = ":" + name;
		list->rows.push_back({name});
		list->row_colors.push_back(RowColor(e));
		m_row_entries.push_back(i);
	}
}

void Tracking::UpdateControls()
{
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	if (!wnd)
		return;
	bool ranger = Ranger();
	if (SidlWindow::Control *sort = wnd->Find(SortId)) {
		if (!ranger)
			m_sort = m_filter = 0;
		sort->choice = m_sort;
		sort->enabled = ranger;
	}
	if (SidlWindow::Control *players = wnd->Find(PlayersId))
		players->choice = m_players;
	for (const FilterButton &b : FilterButtons)
		if (SidlWindow::Control *c = wnd->Find(b.id)) {
			c->checked = (m_filter & b.bit) != 0;
			c->enabled = ranger; // eqgame only ever disables them: a character's class doesn't change
		}
}

void Tracking::Stop()
{
	if (m_host.activity && m_host.activity() == Activity)
		m_host.set_activity(-1);
	m_target = 0;
}

void Tracking::Frame(uint64_t now_ms)
{
	if (!m_windows || !m_zone)
		return;
	m_now_ms = now_ms;
	if (m_active && !m_windows->Shown(Screen)) // the close box
		m_active = false;
	if (m_active && now_ms >= m_controls_ms) {
		m_controls_ms = now_ms + ControlsMs;
		UpdateControls();
	}
	// DoPassageOfTime's.
	if (!m_host.activity || m_host.activity() != Activity)
		return;
	if (m_target == 0) {
		m_host.chat(Say(12499, {}, "You have lost or do not have a tracking target."), 0x10e);
		m_host.set_activity(-1);
		return;
	}
	if (now_ms - m_line_ms > LineMs) {
		m_line_ms = now_ms;
		auto it = m_zone->Spawns().find(m_target);
		const ZoneProfile &me = m_zone->Profile();
		if (it != m_zone->Spawns().end() && me.skills[TrackingSkill] >= 0) {
			const ZoneSpawn &them = it->second;
			// get_bearing from us to them, 0..512, and how far round from our heading.
			float degrees = std::atan2(them.x - me.x, them.y - me.y) * 180.0f / 3.14159265f;
			if (degrees < 0)
				degrees += 360;
			float bearing = degrees * 511.5f / 360.0f;
			float heading = me.heading, d = 0;
			if (heading < bearing) {
				d = bearing - heading;
				if (d > 256)
					d = 512 - bearing + heading;
			}
			else if (bearing < heading) {
				d = heading - bearing;
				if (d > 256)
					d = 512 - heading + bearing;
			}
			double a = heading * 6.28 * 0.001953125 + 1.57;
			double side = (static_cast<double>(them.y) - me.y) * (std::cos(a) * 100.0) +
			              (static_cast<double>(them.x) - me.x) * std::sin(a) * 100.0;
			std::string way = side >= 0 ? Say(12675, {}, "left") : Say(12674, {}, "right");
			std::string name = them.DisplayName();
			std::string line;
			if (d <= 32)
				line = Say(12676, {name}, "%1 is straight ahead.");
			else if (d <= 96)
				line = Say(12677, {name, way}, "%1 is ahead and to the %2.");
			else if (d <= 160)
				line = Say(12678, {name, way}, "%1 is to the %2.");
			else if (d <= 224)
				line = Say(12679, {name, way}, "%1 is behind and to the %2.");
			else if (d <= 256)
				line = Say(12680, {name}, "%1 is behind you.");
			if (!line.empty())
				m_host.chat(line, 0x10e);
		}
	}
	if (now_ms > m_end_ms) {
		m_host.set_activity(-1);
		m_target = 0;
		m_host.chat(Say(12681, {}, "You have lost your tracking target."), 0x10e);
	}
}

bool Tracking::Click(const std::string &window, const std::string &control)
{
	if (window != Screen)
		return false;
	if (control == DoneId) {
		if (m_host.activity && m_host.activity() == Activity)
			m_host.set_activity(-1);
		Deactivate();
		return true;
	}
	SidlWindow *wnd = m_windows->Find(Screen);
	if (control == TrackId) {
		const SidlWindow::Control *list = wnd ? wnd->Find(ListId) : nullptr;
		int row = list ? list->selected_row : -1;
		if (row < 0 || row >= static_cast<int>(m_row_entries.size()))
			return true;
		const Entry &e = m_entries[m_row_entries[row]];
		auto it = m_zone->Spawns().find(e.id);
		if (e.id == 0 || it == m_zone->Spawns().end())
			return true;
		m_host.set_activity(Activity);
		m_target = e.id;
		m_end_ms = m_now_ms + TrackMs; // DAT_007989e4 the start
		m_line_ms = 0;
		m_host.chat(Say(12040, {it->second.DisplayName()}, "You begin tracking %1."), 0x10e);
		Deactivate();
		return true;
	}
	static const char *const filters[] = {"green", "blue", "white", "yellow", "red", "lblue"};
	for (size_t i = 0; i < std::size(FilterButtons); ++i)
		if (control == FilterButtons[i].id) {
			TrackFilter(filters[i]);
			UpdateControls();
			return true;
		}
	if (control == SortId) {
		static const char *const sorts[] = {"normal", "distance", "consider", "rdistance", "rconsider"};
		const SidlWindow::Control *c = wnd ? wnd->Find(SortId) : nullptr;
		if (c && c->choice >= 0 && c->choice < 5)
			TrackSort(Say(c->choice == 0 ? 13120 : 13143 + c->choice, {}, sorts[c->choice]));
		return true;
	}
	if (control == PlayersId) {
		const SidlWindow::Control *c = wnd ? wnd->Find(PlayersId) : nullptr;
		if (c && c->choice >= 0 && c->choice < 3)
			TrackPlayers(c->choice == 0 ? Say(12946, {}, "off") : c->choice == 1 ? Say(12947, {}, "on") : Say(13148, {}, "group"));
		return true;
	}
	return true;
}

void Tracking::TrackPlayers(const std::string &args)
{
	std::string word = Lower(args);
	if (word == Lower(Say(12947, {}, "on"))) {
		m_players = 1;
		Line(13140, "Track players * ON *");
		m_host.save("TrackPlayers", "TRUE");
	}
	else if (word == Lower(Say(12946, {}, "off"))) {
		m_players = 0;
		Line(13141, "Track players * OFF *");
		m_host.save("TrackPlayers", "FALSE");
	}
	else if (word == Lower(Say(13148, {}, "group"))) {
		m_players = 2;
		Line(13142, "Track players * GROUP ONLY *");
		m_host.save("TrackPlayers", "GROUP");
	}
	else
		Line(13143, "FORMAT: /trackplayers <on/off/group>.");
	Filter(true);
}

void Tracking::TrackSort(const std::string &args)
{
	if (!Ranger()) {
		Line(13155, "Only available to Rangers.");
		return;
	}
	std::string word = Lower(args);
	char c = args.empty() ? '\0' : Up(args[0]), c2 = args.size() > 1 ? Up(args[1]) : '\0';
	int type;
	if (c == 'N' || word == Lower(Say(13120, {}, "normal")))
		type = 0;
	else if (c == 'D' || word == Lower(Say(13144, {}, "distance")))
		type = 1;
	else if (c == 'C' || word == Lower(Say(13145, {}, "consider")))
		type = 2;
	else if ((c == 'R' && c2 == 'D') || word == Lower(Say(13146, {}, "rdistance")))
		type = 3;
	else if ((c == 'R' && c2 == 'C') || word == Lower(Say(13147, {}, "rconsider")))
		type = 4;
	else {
		Line(13154, "FORMAT: /tracksort <normal/distance/consider/rdistance/rconsider>.");
		return;
	}
	static const char *const said[] = {"Track Sort type set to normal.", "Track Sort type set to distance.",
		"Track Sort type set to consider.", "Track Sort type set to reverse distance.",
		"Track Sort type set to reverse consider."};
	static const char *const kept[] = {"NORMAL", "DISTANCE", "CONSIDER", "RDISTANCE", "RCONSIDER"};
	m_sort = type;
	Line(13149 + type, said[type]);
	m_host.save("TrackSortType", kept[type]);
	Sort(true);
}

void Tracking::TrackFilter(const std::string &args)
{
	if (!Ranger()) {
		Line(13155, "Only available to Rangers.");
		return;
	}
	struct Color
	{
		char letter;
		uint32_t id;
		const char *name;
		int bit;
	};
	static const Color colors[] = {{'G', 13156, "green", 1}, {'B', 13157, "blue", 2}, {'W', 13158, "white", 4},
		{'Y', 13160, "yellow", 8}, {'R', 13159, "red", 0x10}, {'L', 13161, "lblue", 0x20}};
	char c = args.empty() ? '\0' : Up(args[0]);
	std::string word = Lower(args);
	for (const Color &color : colors)
		if (c == color.letter || word == Lower(Say(color.id, {}, color.name))) {
			std::string name = Say(color.id, {}, color.name);
			m_filter ^= color.bit;
			if (m_filter & color.bit)
				m_host.chat(Say(13164, {name}, "You will NOT see %1 NPCs when tracking."), 0x111);
			else
				m_host.chat(Say(13163, {name}, "You will see %1 NPCs when tracking."), 0x111);
			Filter(true);
			return;
		}
	Line(13162, "FORMAT: /trackfilter <red/yellow/white/blue/lblue/green>.");
	Filter(true);
}

void Tracking::SpawnGone(uint16_t id)
{
	if (id == 0)
		return;
	if (m_target == id && m_host.activity && m_host.activity() != -1) {
		m_host.set_activity(-1);
		m_target = 0;
		Line(12102, "You have lost your target.");
	}
	// RemovePlayerFromTracking: its entry emptied and its row out of the list.
	if (!m_active)
		return;
	for (Entry &e : m_entries)
		if (e.id == id) {
			e = Entry{};
			UpdateList(false);
			return;
		}
}
