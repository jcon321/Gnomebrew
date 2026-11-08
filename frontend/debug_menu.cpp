#include "debug_menu.h"

#include "world_client.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
	constexpr uint32_t White = 0xffffff;
	constexpr uint32_t Grey = 0xa0a0a0;
	constexpr uint32_t Dim = 0x606060;
	constexpr uint32_t Title = 0x80a0c0;    // section titles
	constexpr uint32_t Outgoing = 0xc0a060; // packets we sent
	constexpr uint32_t Highlight = 0x203048; // the target's row
	constexpr int CardWidth = 250;
	constexpr int RadarSize = 220;
	constexpr float RadarRange = 300.0f; // world units from the center to the edge
	constexpr size_t PacketLines = 12;

	struct NearSpawn
	{
		float distance;
		const ZoneSpawn *spawn;
	};

	std::vector<NearSpawn> SortedByDistance(const ZoneSession &zone)
	{
		const ZoneProfile &me = zone.Profile();
		std::vector<NearSpawn> out;
		for (const auto &entry : zone.Spawns()) {
			if (entry.first == zone.MySpawnId())
				continue;
			float dx = entry.second.x - me.x, dy = entry.second.y - me.y;
			out.push_back({std::sqrt(dx * dx + dy * dy), &entry.second});
		}
		std::sort(out.begin(), out.end(), [](const NearSpawn &a, const NearSpawn &b) { return a.distance < b.distance; });
		return out;
	}

	uint32_t SpawnColor(const ZoneSpawn &spawn)
	{
		if (spawn.npc == 0)
			return 0x40c0ff; // players
		if (spawn.npc == 1)
			return spawn.pet_owner ? 0xc080ff : 0xff8040;
		return 0x808080; // corpses
	}

	const char *SpawnKind(const ZoneSpawn &spawn)
	{
		switch (spawn.npc) {
		case 0: return "Player";
		case 1: return spawn.pet_owner ? "Pet" : "NPC";
		case 2: return "Player corpse";
		case 3: return "NPC corpse";
		default: return "?";
		}
	}

	// EQ headings run counterclockwise from north: 128 west, 256 south, 384 east.
	const char *Compass(float heading)
	{
		static const char *const points[8] = {"N", "NW", "W", "SW", "S", "SE", "E", "NE"};
		return points[static_cast<int>(std::fmod(heading + 32, 512.0f) / 64) % 8];
	}

	std::string Format(const char *format, ...)
	{
		char buffer[160];
		va_list args;
		va_start(args, format);
		vsnprintf(buffer, sizeof(buffer), format, args);
		va_end(args);
		return buffer;
	}

	// [Debug]'s names: Folded's sections (in Section's order) and the toggles (in Toggle's, VSync aside).
	const char *const SectionKeys[] = {"Performance", "Movement", "ServerSync", "Camera", "Network", "Packets", "Zone",
		"Sound", "Render", "Target", "Nearby"};
	const char *const ToggleKeys[] = {"Wireframe", "NoFog", "CollisionWalls", "FloorMarker", "NoOcclusionCulling",
		"NoClipPlane", "NoWeatherEffects", "LuclinSpellEffects"};
	const char *const SoundKeys[] = {"AllSound", "Music", "ZoneAmbience", "GameSounds", "WeatherSounds"};

	bool SameText(const std::string &a, const char *b)
	{
		size_t i = 0;
		for (; i < a.size() && b[i]; ++i)
			if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
				return false;
		return i == a.size() && !b[i];
	}

	bool *SoundSwitch(audio::EqSound::Switches &switches, int index)
	{
		switch (index) {
		case 0: return &switches.sound;
		case 1: return &switches.music;
		case 2: return &switches.ambience;
		case 3: return &switches.game;
		default: return &switches.weather;
		}
	}

	std::string Age(uint64_t then_ms, uint64_t now_ms)
	{
		double seconds = now_ms > then_ms ? (now_ms - then_ms) / 1000.0 : 0;
		return seconds < 100 ? Format("%.1fs", seconds) : Format("%.0fs", seconds);
	}
}

void DebugMenu::Load(const IniFile &ini)
{
	auto flag = [&](const char *key, bool fallback) { return ini.GetInt("Debug", key, fallback ? 1 : 0) != 0; };
	open = flag("MenuOpen", open);
	if (ini.Has("Debug", "Folded")) {
		std::string list = ini.Get("Debug", "Folded");
		std::fill(std::begin(m_folded), std::end(m_folded), false);
		size_t start = 0;
		while (start <= list.size()) {
			size_t comma = std::min(list.find(',', start), list.size());
			std::string name = list.substr(start, comma - start);
			name.erase(std::remove_if(name.begin(), name.end(), [](unsigned char c) { return std::isspace(c); }), name.end());
			for (int i = 0; i < SectionCount; ++i)
				if (SameText(name, SectionKeys[i]))
					m_folded[i] = true;
			start = comma + 1;
		}
	}
	for (int i = 0; i < 8; ++i)
		*Toggle(i) = flag(ToggleKeys[i], *Toggle(i));
	m_draw.vision = std::clamp(ini.GetInt("Debug", "Vision", m_draw.vision), -1, 2);
	for (int i = 0; i < 5; ++i)
		*SoundSwitch(m_switches, i) = flag(SoundKeys[i], *SoundSwitch(m_switches, i));
	if (m_sound)
		m_sound->SetSwitches(m_switches);
}

void DebugMenu::Store(IniFile &ini) const
{
	ini.SetInt("Debug", "MenuOpen", open ? 1 : 0);
	std::string folded;
	for (int i = 0; i < SectionCount; ++i)
		if (m_folded[i])
			folded += (folded.empty() ? "" : ",") + std::string(SectionKeys[i]);
	ini.Set("Debug", "Folded", folded);
	const bool toggles[] = {m_draw.wireframe, m_draw.no_fog, m_draw.walls, m_draw.floor, m_draw.no_culling,
		m_draw.no_clip, m_draw.no_weather, m_draw.new_spell_effects};
	for (int i = 0; i < 8; ++i)
		ini.SetInt("Debug", ToggleKeys[i], toggles[i] ? 1 : 0);
	ini.SetInt("Debug", "Vision", m_draw.vision);
	audio::EqSound::Switches switches = m_sound ? m_sound->GetSwitches() : m_switches;
	for (int i = 0; i < 5; ++i)
		ini.SetInt("Debug", SoundKeys[i], *SoundSwitch(switches, i) ? 1 : 0);
}

void DebugMenu::Reset()
{
	m_last_frame = {};
	m_window_frames = 0;
	m_window_ms = m_window_worst_ms = 0;
	m_window_times = {};
	m_fps = m_frame_ms = m_worst_ms = 0;
	m_times = {};
	m_have_network = false;
	m_target = 0;
}

void DebugMenu::Frame(const FrameTimes &times)
{
	Clock::time_point now = Clock::now();
	if (m_last_frame != Clock::time_point{}) {
		double ms = std::chrono::duration<double, std::milli>(now - m_last_frame).count();
		++m_window_frames;
		m_window_ms += ms;
		m_window_worst_ms = std::max(m_window_worst_ms, ms);
		m_window_times.zone += times.zone;
		m_window_times.models += times.models;
		m_window_times.submit += times.submit;
		m_window_times.tags += times.tags;
		m_window_times.ui += times.ui;
		m_window_times.present += times.present;
		m_window_times.ui_windows += times.ui_windows;
		m_window_times.ui_debug += times.ui_debug;
		m_window_times.ui_text += times.ui_text;
		m_window_times.ui_rasterized += times.ui_rasterized;
		m_window_times.upload += times.upload;
		m_window_times.update += times.update;
		m_window_times.ui_redrawn += times.ui_redrawn;
		if (times.slowest_ms > m_window_times.slowest_ms) {
			m_window_times.slowest = times.slowest;
			m_window_times.slowest_ms = times.slowest_ms;
		}
	}
	else
		m_window_start = now;
	m_last_frame = now;
	if (now - m_window_start >= std::chrono::seconds(1) && m_window_frames > 0) {
		m_fps = m_window_frames * 1000.0 / m_window_ms;
		m_frame_ms = m_window_ms / m_window_frames;
		m_worst_ms = m_window_worst_ms;
		double n = m_window_frames;
		const FrameTimes &w = m_window_times;
		m_times = {w.zone / n, w.models / n, w.submit / n, w.tags / n, w.ui / n, w.present / n, w.ui_windows / n,
			w.ui_debug / n, w.ui_text / n, w.ui_rasterized / n, w.upload / n, w.update / n, w.ui_redrawn / n, w.slowest, w.slowest_ms};
		m_window_start = now;
		m_window_frames = 0;
		m_window_ms = m_window_worst_ms = 0;
		m_window_times = {};
	}
}

void DebugMenu::SampleNetwork(const ZoneSession &zone)
{
	if (!zone.NetworkStats(m_network)) {
		m_have_network = false;
		return;
	}
	Clock::time_point now = Clock::now();
	if (!m_have_network) {
		m_have_network = true;
		m_network_sample = m_network;
		m_network_sample_time = now;
		m_in_rate = m_out_rate = m_packets_in_rate = m_packets_out_rate = 0;
		return;
	}
	double seconds = std::chrono::duration<double>(now - m_network_sample_time).count();
	if (seconds < 1)
		return;
	m_in_rate = (m_network.bytes_in - m_network_sample.bytes_in) / seconds;
	m_out_rate = (m_network.bytes_out - m_network_sample.bytes_out) / seconds;
	m_packets_in_rate = (m_network.packets_in - m_network_sample.packets_in) / seconds;
	m_packets_out_rate = (m_network.packets_out - m_network_sample.packets_out) / seconds;
	m_network_sample = m_network;
	m_network_sample_time = now;
}

bool *DebugMenu::Toggle(int index)
{
	switch (index) {
	case 0: return &m_draw.wireframe;
	case 1: return &m_draw.no_fog;
	case 2: return &m_draw.walls;
	case 3: return &m_draw.floor;
	case 4: return &m_draw.no_culling;
	case 5: return &m_draw.no_clip;
	case 6: return &m_draw.no_weather;
	case 7: return &m_draw.new_spell_effects;
	case 8: return m_set_vsync ? &m_vsync : nullptr;
	default: return nullptr;
	}
}

bool DebugMenu::OnMouse(const InputEvent &event)
{
	if (!open || !m_card.Contains(event.x, event.y))
		return false;
	if (event.type != InputEvent::Type::MouseDown && event.type != InputEvent::Type::DoubleClick)
		return true;
	for (const Hit &hit : m_hits) {
		if (event.y < hit.top || event.y >= hit.bottom)
			continue;
		if (hit.action != Line::Action::Pick && hit.action != Line::Action::None)
			m_changed = true;
		switch (hit.action) {
		case Line::Action::Fold:
			m_folded[hit.value] = !m_folded[hit.value];
			break;
		case Line::Action::Toggle:
			if (bool *flag = Toggle(hit.value)) {
				*flag = !*flag;
				if (flag == &m_vsync)
					m_set_vsync(m_vsync);
			}
			break;
		case Line::Action::Vision:
			m_draw.vision = hit.value;
			break;
		case Line::Action::Sound:
			if (m_sound) {
				audio::EqSound::Switches switches = m_sound->GetSwitches();
				bool *flag = SoundSwitch(switches, hit.value);
				*flag = !*flag;
				m_sound->SetSwitches(switches);
			}
			break;
		case Line::Action::Pick:
			m_target = m_target == hit.value ? 0 : static_cast<uint16_t>(hit.value);
			break;
		case Line::Action::None:
			break;
		}
		break;
	}
	return true;
}

void DebugMenu::Draw(Canvas &canvas, TextRenderer &text, const ZoneSession &zone, const ZoneView *view, int bottom)
{
	SampleNetwork(zone);
	if (m_target && !zone.Spawns().count(m_target))
		m_target = 0; // gone
	if (!open) {
		m_card = {0, 0, 0, 0};
		return;
	}
	DrawCard(canvas, text, zone, view, bottom);
	DrawRadar(canvas, text, zone);
}

void DebugMenu::DrawCard(Canvas &canvas, TextRenderer &text, const ZoneSession &zone, const ZoneView *view, int bottom)
{
	const ZoneProfile &me = zone.Profile();
	const ZoneInfo &info = zone.Info();
	uint64_t now = ZoneSession::Now();
	bool world = view && view->Loaded() && view->Error().empty();
	ZoneView::DebugStats stats;
	if (world)
		stats = view->Stats();

	// Laid out first, so the backdrop fits what's in it.
	std::vector<Line> lines;
	auto row = [&](const std::string &line, uint32_t color = Grey) {
		lines.push_back({line, FontSize::Small, color});
		return &lines.back();
	};
	auto section = [&](Section id, const char *title) {
		if (!lines.empty())
			lines.back().gap += 6;
		Line &line = *row(std::string(m_folded[id] ? "+ " : "- ") + title, Title);
		line.action = Line::Action::Fold;
		line.value = id;
		return !m_folded[id];
	};

	lines.push_back({info.long_name.empty() ? "Entering the zone..." : info.long_name, 20, White, 2});
	if (zone.GetState() == ZoneSession::State::Camping)
		row("Camping... " + std::to_string((zone.CampRemainingMs() + 999) / 1000) + "s");
	else {
		// Norrath's clock: hour 1 is midnight.
		int hour;
		float minute;
		if (zone.GameTime(hour, minute)) {
			int h = (hour - 1) % 24;
			row(Format("%d:%02d %s", h % 12 == 0 ? 12 : h % 12, static_cast<int>(minute), h < 12 ? "AM" : "PM"));
		}
	}
	if (!me.name.empty()) {
		lines.back().gap += 6;
		lines.push_back({me.name + (me.last_name.empty() ? "" : " " + me.last_name), 16, White});
		row("Level " + std::to_string(me.level) + " " + RaceName(me.race) + " " + ClassName(static_cast<uint8_t>(me.class_id)));
		row("HP " + std::to_string(me.cur_hp) + "   Mana " + std::to_string(me.mana));
		row(Format("%dp %dg %ds %dc", me.platinum, me.gold, me.silver, me.copper));
	}

	if (section(Performance, "PERFORMANCE")) {
		row(m_fps > 0 ? Format("%.0f fps   %.1f ms (worst %.0f)", m_fps, m_frame_ms, m_worst_ms) : "measuring...");
		if (world) {
			row(Format("Zone %.1f  Models %.1f  Submit %.1f ms", m_times.zone, m_times.models, m_times.submit));
			row(Format("Tags %.1f  UI %.1f  Present %.1f ms", m_times.tags, m_times.ui, m_times.present));
			row(Format("UI: windows %.1f  this card %.1f  text %.1f ms", m_times.ui_windows, m_times.ui_debug, m_times.ui_text));
			row(Format("Text rasterized %.0f a frame   Upload %.1f ms", m_times.ui_rasterized, m_times.upload));
			// What the frame spent outside all of these (the host's loop, the messages, the rest of Render).
			double counted = m_times.update + m_times.zone + m_times.models + m_times.submit + m_times.tags + m_times.ui +
			                 m_times.present;
			row(Format("Update %.1f  Not counted %.1f ms", m_times.update, std::max(0.0, m_frame_ms - counted)));
			row(Format("UI redrawn %.1f%% of the screen a frame", m_times.ui_redrawn));
			if (!m_times.slowest.empty())
				row(Format("Slowest window %s %.1f ms (worst)", m_times.slowest.c_str(), m_times.slowest_ms));
			row(Format("%zuk triangles   %s", (stats.triangles + 500) / 1000, stats.renderer));
			row(Format("Models %zu drawn, %zu built, %zu spawns", stats.spawns_drawn, stats.spawns_built, stats.spawns));
			if (!m_draw.no_culling)
				row(Format("Zone meshes %zu in view, %zu hidden", stats.meshes_in_view, stats.meshes_hidden));
		}
	}

	if (world && section(Movement, "MOVEMENT")) {
		row(Format("Loc %.1f, %.1f, %.1f", me.y, me.x, me.z)); // /loc order: y, x, z
		row(Format("Heading %.0f (%s)", stats.heading, Compass(stats.heading)));
		float speed = std::sqrt(stats.velocity.x * stats.velocity.x + stats.velocity.y * stats.velocity.y) * 20; // ticks/s
		row(Format("Speed %.1f u/s   %s", speed, stats.on_ground ? "on ground" : stats.velocity.z > 0 ? "rising" : "falling"));
		if (stats.have_floor)
			row(Format("Floor z %.1f   feet %.2f above", stats.floor_z, stats.feet_z - stats.floor_z));
		else
			row("No floor below");
		auto environment = [](render::Environment e) {
			switch (e) {
			case render::Environment::Water: return "water";
			case render::Environment::Slime: return "slime";
			case render::Environment::Lava: return "lava";
			case render::Environment::VWater: return "vwater";
			case render::Environment::Water2: return "water2";
			case render::Environment::Water3: return "water3";
			default: return "dry";
			}
		};
		row(Format("Feet %s  body %s  eyes %s", environment(stats.feet_environment), environment(stats.body_environment),
			environment(stats.head_environment)));
		if (!stats.region_types.empty())
			row("Region " + stats.region_types.substr(0, 40));
		row(Format("Animation %s x%.2f", stats.animation.c_str(), stats.animation_rate));
	}

	if (section(Sync, "SERVER SYNC")) {
		const ZoneSession::SentPosition &sent = zone.LastSent();
		if (sent.ms) {
			row(Format("Sent %s ago (%u updates)", Age(sent.ms, now).c_str(), sent.count));
			row(Format("Zone has %.0f, %.0f, %.1f  heading %.0f", sent.y, sent.x, sent.z, sent.heading));
			float dy = me.y - sent.y, dx = me.x - sent.x, dz = me.z - sent.z;
			row(Format("We're %.2f from it", std::sqrt(dx * dx + dy * dy + dz * dz)));
		}
		else
			row("No update sent yet");
		// Our own spawn only moves when the zone puts us somewhere (zoning in, a summon, a correction).
		auto self = zone.Spawns().find(zone.MySpawnId());
		if (self != zone.Spawns().end())
			row(Format("Zone placed us %.0f, %.0f, %.1f", self->second.y, self->second.x, self->second.z))->right =
				self->second.updated_ms ? Age(self->second.updated_ms, now) : "at entry";
	}

	if (world && section(Camera, "CAMERA")) {
		if (stats.view == ZoneView::CameraView::FirstPerson)
			row(Format("First person   pitch %.0f", stats.camera_pitch));
		else
			row(Format("Chase %.0f units   pitch %.0f", stats.camera_distance, stats.camera_pitch));
	}

	if (section(Network, "NETWORK")) {
		if (m_have_network) {
			row(Format("Ping %u ms (+/- %u)", m_network.rtt_ms, m_network.rtt_deviation_ms));
			row(Format("In %.1f kB/s (%.0f/s)   Out %.1f kB/s (%.0f/s)", m_in_rate / 1024, m_packets_in_rate,
				m_out_rate / 1024, m_packets_out_rate));
			row(Format("Resent %llu of %llu sent", static_cast<unsigned long long>(m_network.resent),
				static_cast<unsigned long long>(m_network.packets_out)));
		}
		else
			row("Not connected");
	}

	if (section(Packets, "PACKETS")) {
		const auto &packets = zone.RecentPackets();
		size_t shown = 0;
		for (auto it = packets.rbegin(); it != packets.rend() && shown < PacketLines; ++it, ++shown) {
			std::string name = Format("%s %s", it->outgoing ? "->" : "<-", ZoneOpcodeName(it->opcode));
			if (name.back() == '?')
				name += Format(" 0x%04x", it->opcode);
			if (it->count > 1)
				name += Format(" x%u", it->count);
			row(name, it->outgoing ? Outgoing : Grey)->right = Format("%zub  %s", it->size, Age(it->ms, now).c_str());
		}
		if (packets.empty())
			row("None yet");
	}

	if (section(ZoneHeader, "ZONE")) {
		row(Format("%s (zone %u)   type %u", info.short_name.c_str(), me.zone_id, info.zone_type));
		row(Format("Fog %.0f to %.0f   clip %.0f to %.0f", info.fog_start, info.fog_end, info.min_clip, info.max_clip));
		row(Format("Fog color #%06x   sky %u   time type %u", info.fog_color & 0xffffff, info.sky, info.time_type));
		row(Format("Gravity %.2f   safe %.0f, %.0f, %.0f", info.gravity, info.safe_y, info.safe_x, info.safe_z));
		if (world) {
			row(Format("Ambient %.2f %.2f %.2f   sky light %.2f", stats.light.ambient.x, stats.light.ambient.y,
				stats.light.ambient.z, stats.light.sky));
			row(Format("Vision ambient %.2f %.2f %.2f%s", stats.light.special.x, stats.light.special.y,
				stats.light.special.z, stats.light.glow.x > 0 ? "   infravision glow" : ""));
			row(Format("%zuk triangles, %zu objects", (stats.zone_triangles + 500) / 1000, stats.zone_objects));
		}
	}

	if (m_sound && section(Sound, "SOUND")) {
		audio::EqSound::Stats sound = m_sound->GetStats();
		row("Music " + (sound.music.empty() ? std::string("none") : sound.music) +
			(sound.music_track ? Format("   track %d", sound.music_track) : ""));
		row(Format("Voices %zu   ambient %zu of 4   creature loops %zu", sound.voices, sound.ambient_voices, sound.actor_loops));
		static const char *const environments[] = {"-", "outside", "high ceiling", "low ceiling"};
		row(Format("Emitters %zu of %zu in range   %s, %s", sound.emitters_in_range, sound.emitters,
			sound.time_of_day == 2 ? "night" : sound.time_of_day == 1 ? "day" : "-",
			environments[sound.environment >= 0 && sound.environment <= 3 ? sound.environment : 0]));
		for (const auto &playing : sound.ambient)
			row(Format("  %s  %.0f away", playing.name.c_str(), playing.distance));
		if (m_weather) {
			// What's coming, here or going (and what the zone last said), then what it sounds.
			Weather::Stats weather = m_weather->GetStats();
			const char *kind = weather.kind == Weather::Kind::Snow ? "snow" : "rain";
			std::string state = !weather.active && !weather.changing ? std::string("none")
			                    : weather.changing ? Format("%s %s %d%%", kind, weather.arriving ? "coming in" : "going",
			                                             weather.cloud * 100 / 256)
			                    : weather.kind == Weather::Kind::Rain ? Format("rain   thunder in %ds", (weather.next_strike_ms + 999) / 1000)
			                                                          : std::string(kind);
			std::string received = weather.received ? Format("OP_Weather %s %d", weather.type == 1 ? "snow" : "rain", weather.intensity)
			                                        : std::string("no OP_Weather yet");
			row("Weather " + state + "   (" + received + ")");
			std::string sounding;
			for (const auto &name : sound.weather)
				sounding += (sounding.empty() ? "" : ", ") + name;
			row("  " + (sounding.empty() ? std::string("silent") : sounding) +
				(sound.covered && (sound.raining || sound.wind) ? "   (covered: no rain or wind)" : ""));
		}
		if (view) {
			// The spells' stage sounds (EQPlayer::PlaySoundA's numbered waves) in the last 10 seconds,
			// newest first: they're game sounds, on that switch.
			const audio::EqSound::Switches &on = m_sound->GetSwitches();
			row(std::string("Spell sounds   on the Game sounds switch") + (on.sound && on.game ? "" : " (off: silent)"));
			const std::vector<ZoneView::SpellSound> &played = view->SpellSounds();
			uint64_t now = view->Effects().Now();
			int shown = 0;
			for (auto it = played.rbegin(); it != played.rend() && shown < 4; ++it, ++shown) {
				uint64_t ago = now - std::min(now, it->at_ms);
				if (ago > 10000)
					break;
				std::string wave = m_sound->WaveFile(it->slot);
				if (wave.empty())
					wave = Format("wave %d", it->slot);
				const SpellInfo *spell = m_spells ? m_spells->Find(it->spell) : nullptr;
				std::string name = spell ? spell->name : Format("spell %d", it->spell);
				std::string who = it->on == zone.MySpawnId() ? std::string("you") : Format("spawn %u", it->on);
				auto spawn = zone.Spawns().find(it->on);
				if (it->on != zone.MySpawnId() && spawn != zone.Spawns().end())
					who = spawn->second.DisplayName();
				std::string when = m_sound->WavePlaying(it->slot) ? std::string("playing") : Format("%.1fs ago", ago / 1000.0);
				row(Format("  %s  %s %s, from %s   %s", wave.c_str(), name.c_str(), it->stage == 0 ? "casting" : "landing",
					who.c_str(), when.c_str()));
			}
		}
		if (!sound.missing.empty())
			row("Missing " + sound.missing.back() + (sound.missing.size() > 1 ? Format(" +%zu", sound.missing.size() - 1) : ""));
		const audio::EqSound::Switches &switches = m_sound->GetSwitches();
		const std::pair<const char *, bool> toggles[] = {{"Sound", switches.sound}, {"Music", switches.music},
			{"Zone ambience", switches.ambience}, {"Game sounds (doors, steps, creatures, spells)", switches.game},
			{"Weather (rain, wind, thunder)", switches.weather}};
		for (int i = 0; i < 5; ++i) {
			bool on = toggles[i].second && (i == 0 || switches.sound);
			Line &line = *row(std::string(toggles[i].second ? "[x] " : "[  ] ") + toggles[i].first, on ? White : Grey);
			line.action = Line::Action::Sound;
			line.value = i;
		}
	}

	if (section(Render, "RENDER")) {
		static const char *const names[] = {"Wireframe", "No fog", "Collision walls", "Floor marker", "No occlusion culling",
			"No clip plane", "No weather effects", "Luclin spell effects", "VSync"};
		for (int i = 0; i < 9; ++i) {
			if (!Toggle(i))
				continue;
			Line &line = *row(std::string(*Toggle(i) ? "[x] " : "[  ] ") + names[i], *Toggle(i) ? White : Grey);
			line.action = Line::Action::Toggle;
			line.value = i;
		}
		if (view && m_weather) {
			// What the weather draws: its fog (with no sky behind it) and its particles.
			Weather::Effects effects = m_weather->GetEffects();
			ZoneView::WeatherCounts counts = view->WeatherParticles();
			std::string drawn = effects.active ? Format("fog %.0f..%.0f, no sky", effects.fog_start, effects.fog_end) : "zone's fog";
			const char *falling = effects.clouds == Weather::Kind::Snow ? "flakes" : "drops";
			row(Format("Weather %s   %zu %s, %zu on the ground, %zu on water%s", drawn.c_str(), counts.falling, falling,
				counts.ground, counts.water, effects.blood ? "   (blood)" : ""));
		}
		// What we see in at night: ours (our race's; spells and items to come) or one to try.
		static const char *const visions[] = {"None", "Infravision", "Ultravision"};
		for (int v = -1; v < 3; ++v) {
			std::string name = std::string("Vision: ") + (v < 0 ? "my own" : visions[v]);
			if (v < 0 && view)
				name += std::string(" (") + visions[static_cast<int>(view->OwnVision())] + ")";
			bool chosen = m_draw.vision == v;
			Line &line = *row(std::string(chosen ? "(o) " : "(  ) ") + name, chosen ? White : Grey);
			line.action = Line::Action::Vision;
			line.value = v;
		}
	}

	auto nearby = SortedByDistance(zone);
	if (section(Target, m_target ? "TARGET" : "NEAREST (click a spawn to target)")) {
		const ZoneSpawn *spawn = nullptr;
		float distance = 0;
		for (const NearSpawn &n : nearby)
			if (m_target ? n.spawn->id == m_target : true) {
				spawn = n.spawn;
				distance = n.distance;
				break;
			}
		if (spawn) {
			const ZoneSpawn &s = *spawn;
			row(s.DisplayName(), SpawnColor(s))->right = "#" + std::to_string(s.id);
			row(Format("Level %u %s %s", s.level, RaceName(s.race), ClassName(s.class_id)));
			row(Format("%s%s   HP %d%%", SpawnKind(s), s.pet_owner ? Format(" of #%u", s.pet_owner).c_str() : "", s.hp));
			row(Format("Loc %.1f, %.1f, %.1f", s.y, s.x, s.z))->right = Format("%.0f away", distance);
			row(Format("Heading %.0f (%s)   size %.1f", s.heading, Compass(s.heading), s.size));
			float moving = std::sqrt(s.dx * s.dx + s.dy * s.dy) * 20;
			row(Format("Moving %.1f u/s   speed %.2f (walk %.2f run %.2f)", moving, s.speed, s.walk_speed, s.run_speed));
			row(s.body_texture == 0xff ? Format("Dressed per slot   fly %u", s.fly_mode)
			                           : Format("Texture %u   helm %u   fly %u", s.body_texture, s.helm, s.fly_mode));
			ZoneView::SpawnDebug actor;
			if (world && view->SpawnStats(s.id, actor)) {
				row(Format("Model %s   %s x%.2f%s", actor.model.empty() ? "none" : actor.model.c_str(), actor.animation.c_str(),
					actor.animation_rate, actor.drawn ? "" : "   (not drawn)"));
				row(Format("Drawn at %.1f, %.1f, %.1f (feet)%s", actor.feet.y, actor.feet.x, actor.feet.z,
					render::Swimming(actor.body_environment) ? "   in water" : ""));
			}
			row(Format("Stance %u", s.stance))->right = s.updated_ms ? "updated " + Age(s.updated_ms, now) + " ago" : "not moved";
		}
		else
			row("Nobody here");
	}

	section(Nearby, "NEARBY");
	const int row_height = 14;
	int y = 8;
	for (const Line &line : lines)
		y += line.height + line.gap;
	if (!m_folded[Nearby]) {
		int rows = std::clamp((bottom - 8 - y) / row_height, 0, static_cast<int>(nearby.size()));
		for (int i = 0; i < rows; ++i) {
			const ZoneSpawn &spawn = *nearby[i].spawn;
			Line &line = *row(Format("%.24s (%u)", spawn.DisplayName().c_str(), spawn.level), SpawnColor(spawn));
			line.gap = row_height - FontSize::Small;
			line.right = std::to_string(static_cast<int>(nearby[i].distance));
			line.action = Line::Action::Pick;
			line.value = spawn.id;
			line.highlight = spawn.id == m_target;
			y += row_height;
		}
	}

	m_card = {0, 0, CardWidth, std::min(bottom, y + 4)};
	canvas.Darken(m_card);
	m_hits.clear();
	y = 8;
	for (const Line &line : lines) {
		int next = y + line.height + line.gap;
		if (next > m_card.h + 2)
			break; // past the chat
		if (line.highlight)
			canvas.Fill({4, y - 1, CardWidth - 8, line.height + 2}, Highlight);
		text.Draw(canvas, 10, y, line.text, line.height, line.color);
		if (!line.right.empty()) {
			TextSize size = text.Measure(line.right, FontSize::Small);
			text.Draw(canvas, CardWidth - 10 - size.w, y, line.right, FontSize::Small, Dim);
		}
		if (line.action != Line::Action::None)
			m_hits.push_back({y - 1, next - 1, line.action, line.value});
		y = next;
	}
}

void DebugMenu::DrawRadar(Canvas &canvas, TextRenderer &text, const ZoneSession &zone)
{
	// North up, east right. EQ's +y is north and +x is west.
	const ZoneProfile &me = zone.Profile();
	auto nearby = SortedByDistance(zone);
	const int radar_x = canvas.Width() - RadarSize - 10, radar_y = 10;
	Rect radar{radar_x, radar_y, RadarSize, RadarSize};
	canvas.Darken(radar);
	canvas.Frame(radar, 0x404850);
	canvas.Fill({radar_x + RadarSize / 2, radar_y + 4, 1, RadarSize - 8}, 0x303840);
	canvas.Fill({radar_x + 4, radar_y + RadarSize / 2, RadarSize - 8, 1}, 0x303840);
	text.Draw(canvas, radar_x + RadarSize / 2 - 3, radar_y + 3, "N", FontSize::Small, Grey);
	float scale = (RadarSize / 2 - 6) / RadarRange;
	auto position = [&](const ZoneSpawn &spawn, int &px, int &py) {
		px = radar_x + RadarSize / 2 + static_cast<int>(-(spawn.x - me.x) * scale);
		py = radar_y + RadarSize / 2 + static_cast<int>(-(spawn.y - me.y) * scale);
	};
	for (auto it = nearby.rbegin(); it != nearby.rend(); ++it) { // farthest first so near dots draw on top
		if (it->distance > RadarRange)
			continue;
		int px, py;
		position(*it->spawn, px, py);
		canvas.Fill({px - 1, py - 1, 3, 3}, SpawnColor(*it->spawn));
		if (it->spawn->id == m_target)
			canvas.Frame({px - 4, py - 4, 9, 9}, White);
	}
	for (size_t i = 0, labels = 0; i < nearby.size() && labels < 6; ++i, ++labels) {
		if (nearby[i].distance > RadarRange)
			break;
		int px, py;
		position(*nearby[i].spawn, px, py);
		text.Draw(canvas, px + 4, py - 6, nearby[i].spawn->DisplayName(), 10, Grey);
	}
	canvas.Fill({radar_x + RadarSize / 2 - 2, radar_y + RadarSize / 2 - 2, 5, 5}, White); // us
	text.Draw(canvas, radar_x + 4, radar_y + RadarSize - 14, std::to_string(static_cast<int>(RadarRange)) + " units", 10, Grey);
}
