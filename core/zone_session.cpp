#include "zone_session.h"
#include "eqstr.h"
#include "inflate.h"
#include "opcodes.h"
#include "rdp_session.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace
{
	constexpr uint64_t HandshakeTimeoutMs = 30000;
	constexpr uint64_t CampMs = 30000;          // the client logs out after 30s; the server's timer goes stale at 35s
	constexpr uint16_t AppearanceSpawnId = 0x10; // SpawnAppearance type that tells the client its spawn id
	constexpr uint16_t AppearanceAnimation = 14; // SpawnAppearance: standing, sitting, lying...
	constexpr size_t SpawnSize = 224;            // Mac::structs::Spawn_Struct
	constexpr size_t ProfileSize = 8460;         // Mac::structs::PlayerProfile_Struct
	constexpr size_t ZoneEntrySize = 356;        // ServerZoneEntry_Struct

	uint64_t NowMs()
	{
		return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
	}

	template <typename T>
	T Read(const std::vector<uint8_t> &data, size_t offset)
	{
		T value{};
		if (offset + sizeof(T) <= data.size())
			memcpy(&value, &data[offset], sizeof(T));
		return value;
	}

	std::string CString(const std::vector<uint8_t> &data, size_t offset, size_t max)
	{
		std::string s;
		for (size_t i = offset; i < data.size() && s.size() < max && data[i]; ++i)
			s.push_back(static_cast<char>(data[i]));
		return s;
	}

	uint64_t Rotl(uint64_t v, int n) { return (v << n) | (v >> (64 - n)); }
	uint64_t Rotr(uint64_t v, int n) { return (v >> n) | (v << (64 - n)); }

	uint32_t Crc32(const uint8_t *data, size_t size)
	{
		uint32_t crc = 0xffffffff;
		for (size_t i = 0; i < size; ++i) {
			crc ^= data[i];
			for (int k = 0; k < 8; ++k)
				crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
		}
		return ~crc;
	}

	// The packed velocities in Spawn_Struct and SpawnPositionUpdate_Struct: y in bits 0-10,
	// z in 11-21, x in 22-31, each signed and 16ths of a unit per tick (pack_physics).
	void UnpackDeltas(uint32_t value, float &dy, float &dz, float &dx)
	{
		auto field = [](uint32_t bits, int width) {
			int32_t v = static_cast<int32_t>(bits);
			if (bits & (1u << (width - 1)))
				v -= 1 << width;
			return static_cast<float>(v) * 0.0625f;
		};
		dy = field(value & 0x7ff, 11);
		dz = field((value >> 11) & 0x7ff, 11);
		dx = field(value >> 22, 10);
	}

	// Mac::structs::Spawn_Struct, 224 bytes.
	ZoneSpawn ParseSpawn(const std::vector<uint8_t> &d, size_t o)
	{
		ZoneSpawn s;
		s.heading_speed = static_cast<float>(static_cast<int8_t>(d[o + 6]));
		UnpackDeltas(Read<uint32_t>(d, o + 13), s.dy, s.dz, s.dx);
		s.heading = d[o + 5] * 2.0f;
		s.y = static_cast<float>(Read<int16_t>(d, o + 7));
		s.x = static_cast<float>(Read<int16_t>(d, o + 9));
		s.z = static_cast<float>(Read<int16_t>(d, o + 11)) / 10.0f;
		s.pet_owner = Read<uint16_t>(d, o + 18);
		s.size = Read<float>(d, o + 28);
		s.walk_speed = Read<float>(d, o + 32);
		s.run_speed = Read<float>(d, o + 36);
		for (int k = 0; k < 9; ++k)
			s.colors[k] = Read<uint32_t>(d, o + 40 + k * 4);
		s.id = Read<uint16_t>(d, o + 76);
		s.hp = Read<int16_t>(d, o + 80);
		s.race = Read<uint16_t>(d, o + 84);
		s.npc = d[o + 86];
		s.class_id = d[o + 87];
		s.gender = d[o + 88];
		s.level = d[o + 89];
		s.stance = d[o + 93];
		s.anon = d[o + 95];
		s.afk = d[o + 96];
		s.gm = d[o + 99];
		s.face = d[o + 103];
		for (int k = 0; k < 9; ++k)
			s.equipment[k] = Read<uint16_t>(d, o + 104 + k * 2);
		s.name = CString(d, o + 127, 64);
		s.surname = CString(d, o + 191, 32);
		return s;
	}

	// ServerZoneEntry_Struct (eq_packet_structs.h), 356 bytes: our own spawn, sent as is.
	ZoneSpawn ParseZoneEntry(const std::vector<uint8_t> &d)
	{
		ZoneSpawn s;
		s.name = CString(d, 5, 64);
		s.y = Read<float>(d, 76);
		s.x = Read<float>(d, 80);
		s.z = Read<float>(d, 84);
		s.heading = Read<float>(d, 88);
		s.pet_owner = Read<uint16_t>(d, 154);
		s.npc = d[172];
		s.class_id = d[173];
		s.race = Read<uint16_t>(d, 174);
		s.gender = d[176];
		s.level = d[177];
		s.face = d[183];
		for (int k = 0; k < 9; ++k) {
			s.equipment[k] = Read<uint16_t>(d, 184 + k * 2);
			s.colors[k] = Read<uint32_t>(d, 204 + k * 4);
		}
		s.size = Read<float>(d, 244);
		s.walk_speed = Read<float>(d, 260);
		s.run_speed = Read<float>(d, 264);
		s.gm = d[269];
		s.anon = d[280];
		s.afk = d[283];
		s.surname = CString(d, 304, 32);
		s.hp = 100;
		return s;
	}
}

std::string ZoneSpawn::DisplayName() const
{
	// The client's CleanName: underscores become spaces and the digits go (NPC names end in
	// them, before "'s_corpse" for their corpses).
	std::string out;
	for (char c : name) {
		if (std::isdigit(static_cast<unsigned char>(c)))
			continue;
		out.push_back(c == '_' ? ' ' : c);
	}
	return out;
}

// ---------------------------------------------------------------- crypto

void ZoneCrypto::DecryptProfile(uint8_t *bytes, size_t size)
{
	size_t len = size >> 3;
	if (len == 0)
		return;
	uint64_t crypt = 0x659365E7;
	for (size_t i = 0; i < len; ++i) {
		uint64_t v;
		memcpy(&v, bytes + i * 8, 8);
		v += crypt;
		v = Rotr(v, 7);
		v -= 0x422437A9;
		v = Rotl(v, 25);
		crypt = crypt + v - 0x422437A9;
		memcpy(bytes + i * 8, &v, 8);
	}
	uint64_t a, b;
	memcpy(&a, bytes, 8);
	memcpy(&b, bytes + (len / 2) * 8, 8);
	memcpy(bytes, &b, 8);
	memcpy(bytes + (len / 2) * 8, &a, 8);
}

void ZoneCrypto::DecryptZoneSpawns(uint8_t *bytes, size_t size)
{
	size_t len = size >> 3;
	if (len == 0)
		return;
	uint64_t crypt = 0;
	for (size_t i = 0; i < len; ++i) {
		uint64_t v;
		memcpy(&v, bytes + i * 8, 8);
		v += crypt;
		v = Rotr(v, 14);
		v -= 0x659365E7;
		v = Rotr(v, 29);
		crypt = crypt + v - 0x659365E7;
		memcpy(bytes + i * 8, &v, 8);
	}
	uint64_t a, b;
	memcpy(&a, bytes, 8);
	memcpy(&b, bytes + (len / 2) * 8, 8);
	memcpy(bytes, &b, 8);
	memcpy(bytes + (len / 2) * 8, &a, 8);
}

bool ZoneCrypto::Unpack(std::vector<uint8_t> &data, bool profile)
{
	if (profile)
		DecryptProfile(data.data(), data.size());
	else
		DecryptZoneSpawns(data.data(), data.size());
	std::vector<uint8_t> out;
	if (!Inflate::Zlib(data.data(), data.size(), out))
		return false;
	data.swap(out);
	return true;
}

// ---------------------------------------------------------------- session

ZoneSession::ZoneSession() = default;
ZoneSession::~ZoneSession() = default;

void ZoneSession::Log(const std::string &line)
{
	if (m_log)
		m_log(line);
}

void ZoneSession::Fail(std::vector<Event> &events, const std::string &text)
{
	Log("[zone] " + text);
	m_state = State::Failed;
	if (m_session)
		m_session->Close();
	Event e;
	e.type = Event::Type::Error;
	e.text = text;
	events.push_back(e);
}

bool ZoneSession::Send(uint16_t opcode, const void *body, size_t size, const std::string &what, bool reliable)
{
	if (!what.empty())
		Log("[zone] -> " + what);
	return m_session && m_session->Send(opcode, body, size, reliable);
}

bool ZoneSession::Begin(const std::string &host, uint16_t port, const std::string &character)
{
	Close();
	m_character = character;
	m_profile = ZoneProfile{};
	m_info = ZoneInfo{};
	m_spawns.clear();
	m_self = ZoneSpawn{};
	m_my_id = 0;
	m_have_profile = m_have_zone_entry = m_sent_req_new_zone = m_sent_exp_zonein = false;
	m_movement = ZoneMovement{};
	m_have_movement = m_movement_dirty = false;
	m_identical_updates = 0;

	m_session = std::make_unique<RdpSession>();
	if (!m_session->Connect(host, port)) {
		m_state = State::Failed;
		return false;
	}
	m_state = State::Entering;
	m_started_ms = NowMs();

	// ZoneAuthenticate: the data rate from DataRate.txt (default 5.0, clamped to 2..15).
	float data_rate = 5.0f;
	Send(ZoneOp::DataRate, &data_rate, sizeof(data_rate), "OP_DataRate 5.0");

	// GetZoneInfoFromNetwork: { crc32mem(name, 0x20), name[64] }, 0x44 bytes.
	uint8_t entry[68] = {};
	memcpy(entry + 4, character.c_str(), std::min<size_t>(character.size(), 63));
	uint32_t crc = Crc32(entry + 4, 0x20);
	memcpy(entry, &crc, 4);
	return Send(ZoneOp::ZoneEntry, entry, sizeof(entry), "OP_ZoneEntry " + character);
}

void ZoneSession::SendPosition()
{
	// SpawnPositionUpdate_Struct, 15 bytes (DoMainLoop sends 0xf bytes from pack_physics).
	uint8_t update[15] = {};
	int16_t y = static_cast<int16_t>(m_profile.y), x = static_cast<int16_t>(m_profile.x);
	int16_t z = static_cast<int16_t>(m_profile.z * 10.0f);
	uint8_t heading = static_cast<uint8_t>(static_cast<int>(m_profile.heading / 2) & 0xff); // 512ths -> 256ths
	memcpy(update, &m_my_id, 2);
	update[3] = heading;
	memcpy(update + 5, &y, 2);
	memcpy(update + 7, &x, 2);
	memcpy(update + 9, &z, 2);
	Send(ZoneOp::ClientUpdate, update, sizeof(update), "OP_ClientUpdate");
}

bool ZoneSession::Say(const std::string &text)
{
	if (m_state != State::InZone || text.empty())
		return false;
	// ChannelMessage_Struct: targetname[64], sender[64], language u16 @128, chan_num u16 @130,
	// skill u16 @134, message @136. The client sends strlen + 141 bytes (Handle_OP_ChannelMessage notes the extra 4).
	std::string message = text.substr(0, 2000);
	std::vector<uint8_t> body(141 + message.size(), 0);
	memcpy(&body[64], m_profile.name.c_str(), std::min<size_t>(m_profile.name.size(), 63));
	uint16_t channel = 8; // say
	uint16_t skill = 100;
	memcpy(&body[130], &channel, 2);
	memcpy(&body[134], &skill, 2);
	memcpy(&body[136], message.c_str(), message.size());
	return Send(ZoneOp::ChannelMessage, body.data(), body.size(), "OP_ChannelMessage say: " + message);
}

void ZoneSession::SetMovement(const ZoneMovement &movement)
{
	if (m_state != State::InZone && m_state != State::Camping)
		return;
	if (!m_have_movement) {
		m_have_movement = true;
		m_flagged_heading = movement.heading;
		m_flagged_speed = movement.speed;
		m_last_tick_ms = NowMs();
	}
	m_movement = movement;
	m_profile.x = movement.x;
	m_profile.y = movement.y;
	m_profile.z = movement.z;
	m_profile.heading = movement.heading;
	// ProcessControls: flag an update when the heading or the speed changed enough.
	if (std::fabs(movement.heading - m_flagged_heading) >= 2) {
		m_movement_dirty = true;
		m_flagged_heading = movement.heading;
	}
	if (std::fabs(movement.speed - m_flagged_speed) >= 0.2f) {
		m_movement_dirty = true;
		m_flagged_speed = movement.speed;
	}
}

void ZoneSession::Jump()
{
	if (m_state != State::InZone && m_state != State::Camping)
		return;
	m_movement_dirty = true;
	Send(ZoneOp::Jump, nullptr, 0, "OP_Jump");
}

void ZoneSession::SendMovement(uint64_t now_ms)
{
	// DoPassageOfTime flags an update every second.
	if (now_ms - m_last_tick_ms >= 1000) {
		m_last_tick_ms = now_ms;
		m_movement_dirty = true;
	}
	if (!m_movement_dirty || now_ms - m_last_update_ms <= 150)
		return;

	// pack_physics: SpawnPositionUpdate_Struct after the spawn id.
	const ZoneMovement &m = m_movement;
	uint8_t body[13] = {};
	body[0] = static_cast<uint8_t>(static_cast<int8_t>(std::clamp(m.speed, -12.8f, 12.7f) * 10));
	float heading = std::fmod(m.heading, 512.0f);
	if (heading < 0)
		heading += 512;
	body[1] = static_cast<uint8_t>(static_cast<int>(heading * 0.5f) & 0xff);
	body[2] = static_cast<uint8_t>(static_cast<int8_t>(std::clamp(m.heading_speed, -128.0f, 127.0f)));
	int16_t y = static_cast<int16_t>(m.y), x = static_cast<int16_t>(m.x), z = static_cast<int16_t>(m.z * 10);
	memcpy(body + 3, &y, 2);
	memcpy(body + 5, &x, 2);
	memcpy(body + 7, &z, 2);
	int32_t dy = static_cast<int32_t>(std::clamp(m.dy, -64.0f, 63.0f) * 16);
	int32_t dx = static_cast<int32_t>(std::clamp(m.dx, -32.0f, 31.0f) * 16);
	int32_t dz = static_cast<int32_t>(std::clamp(m.dz, -64.0f, 63.0f) * 16);
	uint32_t deltas = (static_cast<uint32_t>(dx) & 0x3ff) << 22 | (static_cast<uint32_t>(dz) & 0x7ff) << 11 |
	                  (static_cast<uint32_t>(dy) & 0x7ff);
	memcpy(body + 9, &deltas, 4);

	// The same update goes out at most 6 times in a row.
	if (memcmp(body, m_last_update, sizeof(body)) == 0) {
		if (++m_identical_updates >= 7) {
			m_identical_updates = 7;
			return;
		}
	}
	else {
		m_identical_updates = 0;
		memcpy(m_last_update, body, sizeof(body));
	}
	uint8_t update[15];
	memcpy(update, &m_my_id, 2);
	memcpy(update + 2, body, sizeof(body));
	Send(ZoneOp::ClientUpdate, update, sizeof(update), m_verbose ? "OP_ClientUpdate" : "");
	m_movement_dirty = false;
	m_last_update_ms = now_ms;
}

bool ZoneSession::Camp()
{
	if (m_state != State::InZone)
		return false;
	m_state = State::Camping;
	m_camp_started_ms = NowMs();
	return Send(ZoneOp::Camp, nullptr, 0, "OP_Camp");
}

bool ZoneSession::GameTime(int &hour, float &minute) const
{
	if (!m_time_received_ms)
		return false;
	double seconds = ((m_time_hour - 1) * 60.0 + m_time_minute) * 60.0 + (NowMs() - m_time_received_ms) * 0.02;
	seconds = std::fmod(seconds, 86400.0);
	if (seconds < 0)
		seconds += 86400;
	hour = static_cast<int>(seconds / 3600) + 1;
	minute = static_cast<float>(std::fmod(seconds, 3600.0) / 60);
	return true;
}

uint64_t ZoneSession::CampRemainingMs() const
{
	if (m_state != State::Camping)
		return 0;
	uint64_t elapsed = NowMs() - m_camp_started_ms;
	return elapsed >= CampMs ? 0 : CampMs - elapsed;
}

void ZoneSession::Close()
{
	if (m_session)
		m_session->Close();
	m_session.reset();
	if (m_state != State::LoggedOut && m_state != State::Failed)
		m_state = State::Idle;
}

void ZoneSession::HandlePacket(uint16_t opcode, std::vector<uint8_t> &data, std::vector<Event> &events)
{
	switch (opcode) {
	case ZoneOp::PlayerProfile: {
		if (!ZoneCrypto::Unpack(data, true) || data.size() < ProfileSize) {
			Fail(events, "couldn't decrypt the player profile");
			return;
		}
		ZoneProfile &p = m_profile;
		p.name = CString(data, 6, 64);
		p.last_name = CString(data, 70, 66);
		p.gender = data[140];
		p.race = Read<uint16_t>(data, 142);
		p.class_id = Read<uint16_t>(data, 144);
		p.level = data[148];
		p.exp = Read<uint32_t>(data, 152);
		p.mana = Read<int16_t>(data, 158);
		p.cur_hp = Read<int16_t>(data, 160);
		p.y = Read<float>(data, 2904);
		p.x = Read<float>(data, 2908);
		p.z = Read<float>(data, 2912);
		p.heading = Read<float>(data, 2916);
		p.platinum = Read<int32_t>(data, 2924);
		p.gold = Read<int32_t>(data, 2928);
		p.silver = Read<int32_t>(data, 2932);
		p.copper = Read<int32_t>(data, 2936);
		p.zone_id = Read<uint32_t>(data, 3444);
		p.deity = Read<uint16_t>(data, 4944);
		m_have_profile = true;
		char line[160];
		snprintf(line, sizeof(line), "[zone]    profile: %s level %u, zone %u at (%.1f, %.1f, %.1f), hp %d", p.name.c_str(), p.level,
			p.zone_id, p.x, p.y, p.z, p.cur_hp);
		Log(line);
		events.push_back({Event::Type::Profile});
		break;
	}
	case ZoneOp::ZoneEntry: {
		// ServerZoneEntry_Struct: our own spawn record.
		if (data.size() >= ZoneEntrySize) {
			m_self = ParseZoneEntry(data);
			char line[160];
			snprintf(line, sizeof(line), "[zone]    our spawn: race %u gender %u face %u size %.2f at (%.1f, %.1f, %.1f) heading %.0f",
				m_self.race, m_self.gender, m_self.face, m_self.size, m_self.x, m_self.y, m_self.z, m_self.heading);
			Log(line);
		}
		m_have_zone_entry = true;
		break;
	}
	case ZoneOp::NewZone: {
		m_info.short_name = CString(data, 64, 32);
		m_info.long_name = CString(data, 96, 278);
		m_info.zone_type = Read<uint8_t>(data, 374);
		m_info.fog_color = (Read<uint8_t>(data, 375) << 16) | (Read<uint8_t>(data, 379) << 8) | Read<uint8_t>(data, 383);
		m_info.fog_start = Read<float>(data, 388);
		m_info.fog_end = Read<float>(data, 404);
		m_info.sky = Read<uint8_t>(data, 474);
		m_info.time_type = Read<uint8_t>(data, 424);
		m_info.gravity = Read<float>(data, 420);
		if (!(m_info.gravity > 0 && m_info.gravity < 10))
			m_info.gravity = 0.4f;
		m_info.min_clip = Read<float>(data, 508);
		m_info.max_clip = Read<float>(data, 512);
		// HandleWorldMessage's fallbacks: no fog means grey fog from 500 to 600, and the clip
		// range is at least 50, out to four times the fog when it's missing or too close.
		if (m_info.fog_start == 0 && m_info.fog_end == 0) {
			m_info.fog_start = 500;
			m_info.fog_end = 600;
			m_info.fog_color = 0xc0c0c0;
		}
		if (m_info.min_clip < 50)
			m_info.min_clip = 50;
		if (m_info.max_clip < 51)
			m_info.max_clip = m_info.fog_end * 4;
		if (m_info.max_clip < m_info.min_clip)
			m_info.max_clip = m_info.min_clip * 4;
		m_info.safe_y = Read<float>(data, 488);
		m_info.safe_x = Read<float>(data, 492);
		m_info.safe_z = Read<float>(data, 496);
		{
			char line[400];
			snprintf(line, sizeof(line), "[zone]    zone: %s (%s), sky %u, fog #%06x %.0f..%.0f, clip %.0f..%.0f", m_info.long_name.c_str(),
				m_info.short_name.c_str(), m_info.sky, m_info.fog_color, m_info.fog_start, m_info.fog_end, m_info.min_clip,
				m_info.max_clip);
			Log(line);
		}
		events.push_back({Event::Type::ZoneInfo});
		Send(ZoneOp::ReqClientSpawn, nullptr, 0, "OP_ReqClientSpawn");
		break;
	}
	case ZoneOp::ZoneSpawns: {
		// Bulk spawns and single new spawns (OP_NewSpawn is encoded as this) are deflated + encrypted.
		if (!ZoneCrypto::Unpack(data, false)) {
			Log("[zone]    couldn't decrypt OP_ZoneSpawns");
			break;
		}
		size_t count = data.size() / SpawnSize;
		for (size_t i = 0; i < count; ++i) {
			ZoneSpawn spawn = ParseSpawn(data, i * SpawnSize);
			spawn.updated_ms = NowMs();
			m_spawns[spawn.id] = spawn;
			Event e;
			e.type = Event::Type::SpawnAdded;
			e.id = spawn.id;
			events.push_back(e);
		}
		if (count > 1) {
			char line[64];
			snprintf(line, sizeof(line), "[zone]    %zu spawns", count);
			Log(line);
		}
		break;
	}
	case ZoneOp::DeleteSpawn: {
		uint16_t id = Read<uint16_t>(data, 0);
		m_spawns.erase(id);
		Event e;
		e.type = Event::Type::SpawnRemoved;
		e.id = id;
		events.push_back(e);
		break;
	}
	case ZoneOp::MobUpdate:
	case ZoneOp::ClientUpdate: {
		// Batched SpawnPositionUpdate_Structs: u32 count, then 15 bytes each.
		uint32_t count = Read<uint32_t>(data, 0);
		uint64_t now = NowMs();
		for (uint32_t i = 0; i < count && 4 + (i + 1) * 15 <= data.size(); ++i) {
			size_t o = 4 + i * 15;
			auto it = m_spawns.find(Read<uint16_t>(data, o));
			if (it == m_spawns.end())
				continue;
			// unpack_physics: the animation byte is the speed * 10, then the heading, its change per tick,
			// the position and the velocities.
			it->second.speed = static_cast<float>(static_cast<int8_t>(data[o + 2])) * 0.1f;
			it->second.heading = data[o + 3] * 2.0f;
			it->second.heading_speed = static_cast<float>(static_cast<int8_t>(data[o + 4]));
			UnpackDeltas(Read<uint32_t>(data, o + 11), it->second.dy, it->second.dz, it->second.dx);
			it->second.updated_ms = now;
			it->second.y = static_cast<float>(Read<int16_t>(data, o + 5));
			it->second.x = static_cast<float>(Read<int16_t>(data, o + 7));
			it->second.z = static_cast<float>(Read<int16_t>(data, o + 9)) / 10.0f;
		}
		events.push_back({Event::Type::SpawnsMoved});
		break;
	}
	case ZoneOp::HPUpdate: {
		auto it = m_spawns.find(Read<uint16_t>(data, 0));
		if (it != m_spawns.end()) {
			int32_t cur = Read<int32_t>(data, 4), max = Read<int32_t>(data, 8);
			it->second.hp = static_cast<int16_t>(max > 0 && max != 100 ? cur * 100 / max : cur);
		}
		break;
	}
	case ZoneOp::SpawnAppearance: {
		uint16_t type = Read<uint16_t>(data, 2);
		if (type == AppearanceSpawnId) {
			m_my_id = static_cast<uint16_t>(Read<uint32_t>(data, 4));
			Log("[zone]    my spawn id " + std::to_string(m_my_id));
		}
		else if (type == AppearanceAnimation) {
			auto it = m_spawns.find(Read<uint16_t>(data, 0));
			if (it != m_spawns.end()) {
				it->second.stance = static_cast<uint8_t>(Read<uint32_t>(data, 4));
				Event e;
				e.type = Event::Type::SpawnChanged;
				e.id = it->first;
				events.push_back(e);
			}
		}
		break;
	}
	case ZoneOp::Death: {
		// Death_Struct: the spawn falls over and becomes its corpse.
		auto it = m_spawns.find(Read<uint16_t>(data, 0));
		if (it != m_spawns.end()) {
			ZoneSpawn &s = it->second;
			s.npc = s.npc == 0 ? 2 : 3;
			s.speed = s.heading_speed = s.dx = s.dy = s.dz = 0;
			if (s.name.find("_corpse") == std::string::npos)
				s.name += "'s_corpse";
			Event e;
			e.type = Event::Type::SpawnChanged;
			e.id = it->first;
			events.push_back(e);
		}
		break;
	}
	case ZoneOp::SendExpZonein:
		if (!m_sent_exp_zonein) {
			// After doors/objects: tell the server we're ready for our own spawn.
			m_sent_exp_zonein = true;
			Send(ZoneOp::SendExpZonein, nullptr, 0, "OP_SendExpZonein");
		}
		else if (m_state == State::Entering) {
			// The second one: send our position, which makes the server CompleteConnect().
			SendPosition();
			m_state = State::InZone;
			Log("[zone]    in zone");
			events.push_back({Event::Type::InZone});
		}
		break;
	case ZoneOp::TimeOfDay: {
		if (data.size() >= 2) {
			m_time_hour = data[0];
			m_time_minute = data[1];
			m_time_received_ms = NowMs();
		}
		char text[32];
		snprintf(text, sizeof(text), "%u:%02u", data.size() > 0 ? data[0] : 0, data.size() > 1 ? data[1] : 0);
		Event e;
		e.type = Event::Type::TimeOfDay;
		e.text = text;
		events.push_back(e);
		break;
	}
	case ZoneOp::ChannelMessage: {
		Event e;
		e.type = Event::Type::Chat;
		e.chat.target = CString(data, 0, 64);
		e.chat.sender = CString(data, 64, 64);
		e.chat.channel = Read<uint16_t>(data, 130);
		e.chat.text = CString(data, 136, 2048);
		Log("[zone]    chat " + std::to_string(e.chat.channel) + " " + e.chat.sender + ": " + e.chat.text);
		events.push_back(e);
		break;
	}
	case ZoneOp::SpecialMesg: {
		// Mac::structs::SpecialMesg_Struct: u32 msg_type, then the text.
		Event e;
		e.type = Event::Type::Message;
		e.text = CString(data, 4, 2048);
		Log("[zone]    message: " + e.text);
		events.push_back(e);
		break;
	}
	case ZoneOp::FormattedMessage: {
		// FormattedMessage_Struct: u16 unknown, u16 string_id, u16 type, then NUL-separated arguments.
		uint16_t id = Read<uint16_t>(data, 2);
		std::vector<std::string> args;
		for (size_t pos = 6; pos < data.size();) {
			std::string arg = CString(data, pos, data.size());
			pos += arg.size() + 1;
			if (!arg.empty() || pos < data.size())
				args.push_back(arg);
		}
		Event e;
		e.type = Event::Type::Message;
		if (m_eqstr) {
			e.text = m_eqstr->Format(id, args);
		}
		else {
			e.text = "[string " + std::to_string(id) + "]";
			for (const auto &arg : args)
				e.text += " " + arg;
		}
		Log("[zone]    message: " + e.text);
		events.push_back(e);
		break;
	}
	case ZoneOp::LogoutReply:
		m_state = State::LoggedOut;
		Log("[zone]    logged out");
		events.push_back({Event::Type::LoggedOut});
		if (m_session)
			m_session->Close();
		break;
	default:
		break;
	}
}

void ZoneSession::Pump(int timeout_ms, std::vector<Event> &events)
{
	if (!m_session || m_state == State::Idle || m_state == State::Failed || m_state == State::LoggedOut)
		return;

	for (;;) {
		AppPacket packet;
		auto status = m_session->Receive(packet, timeout_ms);
		timeout_ms = 0;
		if (status == RdpSession::Status::Timeout)
			break;
		if (status == RdpSession::Status::Closed) {
			if (m_state == State::Camping) {
				// The zone may just drop the connection once the logout is processed.
				m_state = State::LoggedOut;
				events.push_back({Event::Type::LoggedOut});
				return;
			}
			char text[64];
			snprintf(text, sizeof(text), "connection closed (reason 0x%08x)", m_session->DisconnectReason());
			Fail(events, text);
			return;
		}

		bool chatty = packet.opcode == ZoneOp::MobUpdate || packet.opcode == ZoneOp::ClientUpdate ||
		              packet.opcode == ZoneOp::HPUpdate || packet.opcode == ZoneOp::ManaChange;
		if (!chatty || m_verbose) {
			char header[96];
			snprintf(header, sizeof(header), "[zone] <- %s (0x%04x) %zu bytes", ZoneOpcodeName(packet.opcode), packet.opcode,
				packet.data.size());
			Log(header);
		}
		if (m_verbose)
			HexDump(packet.data);
		HandlePacket(packet.opcode, packet.data, events);
		if (m_state == State::Failed || m_state == State::LoggedOut)
			return;

		// EnterZone requests the zone header once the profile and our spawn are in (after loading the zone).
		if (m_state == State::Entering && m_have_profile && m_have_zone_entry && !m_sent_req_new_zone) {
			m_sent_req_new_zone = true;
			Send(ZoneOp::ReqNewZone, m_character.c_str(), m_character.size() + 1, "OP_ReqNewZone");
		}
	}

	if ((m_state == State::InZone || m_state == State::Camping) && m_have_movement)
		SendMovement(NowMs());
	if (m_state == State::Entering && NowMs() - m_started_ms > HandshakeTimeoutMs)
		Fail(events, "timed out entering the zone");
	if (m_state == State::Camping && CampRemainingMs() == 0 && m_camp_started_ms != 0) {
		m_camp_started_ms = 0; // send once
		Send(ZoneOp::Logout, nullptr, 0, "OP_Logout");
	}
}
