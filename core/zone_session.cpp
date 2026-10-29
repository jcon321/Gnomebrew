#include "zone_session.h"
#include "eqstr.h"
#include "inflate.h"
#include "inventory.h"
#include "opcodes.h"
#include "packet_log.h"
#include "rdp_session.h"
#include "spell_file.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
	constexpr uint64_t HandshakeTimeoutMs = 30000;
	constexpr uint64_t CampMs = 30000;          // the client logs out after 30s; the server's timer goes stale at 35s
	constexpr uint16_t AppearanceSpawnId = 0x10; // SpawnAppearance type that tells the client its spawn id
	constexpr uint16_t AppearanceAnimation = 14; // SpawnAppearance: standing, sitting, lying...
	constexpr uint16_t AppearanceLight = 5;      // SpawnAppearance: the light it carries (LightType)
	// SpawnAppearance types for what the name over a head shows (AppearanceType in eq_constants.h)
	constexpr uint16_t AppearanceInvis = 3, AppearancePvp = 4, AppearanceLinkdead = 18, AppearanceGm = 20,
	                   AppearanceAnonymous = 21, AppearanceGuild = 22, AppearanceAfk = 24;
	constexpr size_t SpawnSize = 224;            // Mac::structs::Spawn_Struct
	constexpr size_t ProfileSize = 8460;         // Mac::structs::PlayerProfile_Struct
	constexpr size_t ZoneEntrySize = 356;        // ServerZoneEntry_Struct
	constexpr size_t ItemSize = 360;             // Mac::structs::Item_Struct

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

	// SpellBuff_Struct (10 bytes).
	ZoneBuff ReadBuff(const std::vector<uint8_t> &data, size_t offset)
	{
		ZoneBuff buff;
		buff.type = Read<uint8_t>(data, offset);
		buff.level = Read<uint8_t>(data, offset + 1);
		buff.bard_modifier = Read<uint8_t>(data, offset + 2);
		buff.activated = Read<uint8_t>(data, offset + 3);
		buff.spell = Read<int16_t>(data, offset + 4);
		buff.duration = Read<int16_t>(data, offset + 6);
		buff.counters = Read<uint16_t>(data, offset + 8);
		return buff;
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
		s.body_type = Read<int16_t>(d, o + 78);
		s.hp = Read<int16_t>(d, o + 80);
		s.race = Read<uint16_t>(d, o + 84);
		s.npc = d[o + 86];
		s.class_id = d[o + 87];
		s.gender = d[o + 88];
		s.level = d[o + 89];
		s.stance = d[o + 93];
		s.light = d[o + 94];
		s.aa_title = d[o + 27];
		s.guild_id = Read<int16_t>(d, o + 82);
		s.deity = Read<int16_t>(d, o + 124);
		s.invis = d[o + 90];
		s.pvp = d[o + 92];
		s.anon = d[o + 95];
		s.afk = d[o + 96];
		s.linkdead = d[o + 98];
		s.gm = d[o + 99];
		s.fly_mode = d[o + 100];
		s.body_texture = d[o + 101];
		s.helm = d[o + 102];
		s.face = d[o + 103];
		for (int k = 0; k < 9; ++k)
			s.equipment[k] = Read<uint16_t>(d, o + 104 + k * 2);
		s.name = CString(d, o + 127, 64);
		s.surname = CString(d, o + 191, 32);
		return s;
	}

	// Mac::structs::Item_Struct at `o`: the fields ZoneItem keeps.
	ZoneItem ParseItem(const std::vector<uint8_t> &d, size_t o)
	{
		ZoneItem item;
		item.name = CString(d, o, 64);
		item.lore = CString(d, o + 64, 80);
		item.weight = d[o + 174];
		item.no_rent = d[o + 175];
		item.no_drop = d[o + 176];
		item.size = d[o + 177];
		item.item_class = static_cast<uint8_t>(Read<int16_t>(d, o + 178));
		item.id = Read<uint16_t>(d, o + 180);
		item.icon = Read<uint16_t>(d, o + 182);
		item.slot = Read<int16_t>(d, o + 184);
		item.idfile = CString(d, o + 144, 30);
		item.slots = Read<int32_t>(d, o + 188);
		item.price = Read<int32_t>(d, o + 192);
		item.sell_rate = Read<float>(d, o + 288);
		if (item.item_class == 0) {
			item.str = Read<int8_t>(d, o + 228);
			item.sta = Read<int8_t>(d, o + 229);
			item.cha = Read<int8_t>(d, o + 230);
			item.dex = Read<int8_t>(d, o + 231);
			item.int_ = Read<int8_t>(d, o + 232);
			item.agi = Read<int8_t>(d, o + 233);
			item.wis = Read<int8_t>(d, o + 234);
			item.mr = Read<int8_t>(d, o + 235);
			item.fr = Read<int8_t>(d, o + 236);
			item.cr = Read<int8_t>(d, o + 237);
			item.dr = Read<int8_t>(d, o + 238);
			item.pr = Read<int8_t>(d, o + 239);
			item.hp = Read<int16_t>(d, o + 240);
			item.mana = Read<int16_t>(d, o + 242);
			item.ac = Read<int16_t>(d, o + 244);
			item.max_charges = d[o + 246];
			item.light = d[o + 248];
			item.delay = d[o + 249];
			item.damage = d[o + 250];
			item.effect_type = Read<int8_t>(d, o + 251);
			item.range = d[o + 252];
			item.item_type = d[o + 253];
			item.magic = d[o + 254];
			item.effect_level = d[o + 255];
			item.material = d[o + 256];
			item.color = Read<uint32_t>(d, o + 260);
			item.effect1 = Read<int16_t>(d, o + 266);
			item.classes = Read<int32_t>(d, o + 268);
			item.races = Read<int32_t>(d, o + 272);
			item.stackable = d[o + 276];
		}
		else if (item.item_class == 1) {
			item.bag_type = d[o + 268];
			item.bag_slots = d[o + 269];
			item.bag_open = d[o + 270];
			item.bag_size = d[o + 271];
			item.bag_wr = d[o + 272];
		}
		else if (item.item_class == 2) {
			item.book_type = d[o + 228];
			item.book_open = d[o + 229];
		}
		item.charges = Read<int8_t>(d, o + 278);
		item.effect2 = Read<int16_t>(d, o + 280);
		item.cast_time = Read<int32_t>(d, o + 292);
		item.skill_mod_type = Read<uint16_t>(d, o + 312);
		item.skill_mod_value = Read<int16_t>(d, o + 314);
		item.bane_race = Read<int16_t>(d, o + 316);
		item.bane_body = Read<int16_t>(d, o + 318);
		item.bane_amount = d[o + 320];
		item.rec_level = Read<uint8_t>(d, o + 324);
		item.rec_skill = Read<uint8_t>(d, o + 325);
		item.elem_type = d[o + 328];
		item.elem_amount = d[o + 329];
		for (int i = 0; i < 4; ++i) {
			item.faction_mod[i] = Read<int16_t>(d, o + 330 + i * 2);
			item.faction_amount[i] = Read<int16_t>(d, o + 338 + i * 2);
		}
		item.deity = Read<int32_t>(d, o + 348);
		item.req_level = Read<int16_t>(d, o + 352);
		item.bard_type = Read<int16_t>(d, o + 354);
		item.focus_effect = Read<int16_t>(d, o + 358);
		return item;
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
		s.light = d[182];
		s.face = d[183];
		for (int k = 0; k < 9; ++k) {
			s.equipment[k] = Read<uint16_t>(d, 184 + k * 2);
			s.colors[k] = Read<uint32_t>(d, 204 + k * 4);
		}
		s.size = Read<float>(d, 244);
		s.walk_speed = Read<float>(d, 260);
		s.run_speed = Read<float>(d, 264);
		s.guild_id = Read<int16_t>(d, 164);
		s.invis = d[178];
		s.pvp = d[180];
		s.linkdead = d[268];
		s.gm = d[269];
		s.anon = d[280];
		s.afk = d[283];
		s.aa_title = d[285];
		s.surname = CString(d, 304, 32);
		s.hp = 100;
		return s;
	}
}

void ZoneSession::PlayAnimation(uint16_t spawn, int animation, float speed)
{
	auto it = m_spawns.find(spawn);
	if (it == m_spawns.end())
		return;
	it->second.action = animation;
	it->second.action_speed = speed;
	++it->second.action_serial;
}

std::string ZoneSpawn::DisplayName() const
{
	// The client's CleanName (CEverQuest::trimName): underscores become spaces, and the digits
	// (NPC names end in them, before "'s_corpse" for their corpses) and '#' go.
	std::string out;
	for (char c : name) {
		if (std::isdigit(static_cast<unsigned char>(c)) || c == '#')
			continue;
		out.push_back(c == '_' ? ' ' : c);
	}
	return out;
}

GuildNames ParseGuildList(const std::vector<uint8_t> &data)
{
	// HandleWorldMessage 0x4192 keeps up to 512 entries of 0x60 bytes after the 4-byte head.
	constexpr size_t Head = 4, EntrySize = 96, MaxGuilds = 512;
	GuildNames guilds;
	for (size_t i = 0; i < MaxGuilds && Head + (i + 1) * EntrySize <= data.size(); ++i) {
		size_t o = Head + i * EntrySize;
		if (Read<uint16_t>(data, o + 72) == 0)
			continue;
		guilds[static_cast<int16_t>(Read<uint32_t>(data, o))] = CString(data, o + 4, 64);
	}
	return guilds;
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
	if (m_recorder)
		m_recorder->Write(true, opcode, body, size);
	if (m_replay) {
		RecordPacket(true, opcode, size);
		return true;
	}
	if (!m_session)
		return false;
	RecordPacket(true, opcode, size);
	return m_session->Send(opcode, body, size, reliable);
}

uint64_t ZoneSession::Now()
{
	return NowMs();
}

void ZoneSession::RecordPacket(bool outgoing, uint16_t opcode, size_t size)
{
	constexpr size_t MaxRecords = 64;
	uint64_t now = NowMs();
	if (!m_recent_packets.empty()) {
		PacketRecord &last = m_recent_packets.back();
		if (last.outgoing == outgoing && last.opcode == opcode) {
			last.ms = now;
			last.size = size;
			++last.count;
			return;
		}
	}
	m_recent_packets.push_back({now, outgoing, opcode, size, 1});
	if (m_recent_packets.size() > MaxRecords)
		m_recent_packets.pop_front();
}

void ZoneSession::RecordSent(const uint8_t *update)
{
	int16_t y, x, z;
	memcpy(&y, update + 5, 2);
	memcpy(&x, update + 7, 2);
	memcpy(&z, update + 9, 2);
	m_last_sent.ms = NowMs();
	m_last_sent.y = y;
	m_last_sent.x = x;
	m_last_sent.z = z / 10.0f;
	m_last_sent.heading = update[3] * 2.0f;
	++m_last_sent.count;
}

void ZoneSession::Reset(const std::string &character)
{
	Close();
	m_character = character;
	m_profile = ZoneProfile{};
	m_info = ZoneInfo{};
	m_spawns.clear();
	m_self = ZoneSpawn{};
	m_my_id = 0;
	m_worn_known = false;
	m_hp = m_max_hp = 0;
	m_items.clear();
	m_replay_answers.clear();
	m_have_profile = m_have_zone_entry = m_sent_req_new_zone = m_sent_exp_zonein = false;
	m_movement = ZoneMovement{};
	m_have_movement = m_movement_dirty = false;
	m_identical_updates = 0;
	m_recent_packets.clear();
	m_last_sent = SentPosition{};
	m_zone_points.clear();
	m_doors.clear();
	m_zone_target = 0;
	m_camp_started_ms = 0;
}

const std::string &ZoneSession::RecordingPath() const
{
	static const std::string none;
	return m_recorder ? m_recorder->Path() : none;
}

bool ZoneSession::BeginReplay(const std::string &path, std::string &error)
{
	auto replay = std::make_unique<PacketLog>();
	if (!replay->Load(path, error))
		return false;
	Reset(replay->character);
	m_replay = std::move(replay);
	m_replay_next = 0;
	m_replay_finished = false;
	m_state = State::Entering;
	m_started_ms = NowMs();
	Log("[zone] replaying " + path + ": " + m_character + " at " + m_replay->peer + ", " +
		std::to_string(m_replay->packets.size()) + " packets over " +
		std::to_string(m_replay->packets.empty() ? 0 : m_replay->packets.back().ms / 1000) + " s");
	return true;
}

bool ZoneSession::Begin(const std::string &host, uint16_t port, const std::string &character)
{
	Reset(character);
	m_session = std::make_unique<RdpSession>();
	if (!m_session->Connect(host, port)) {
		m_state = State::Failed;
		return false;
	}
	m_state = State::Entering;
	m_started_ms = NowMs();
	if (!m_record_dir.empty()) {
		std::string error;
		m_recorder = std::make_unique<PacketLogWriter>();
		if (m_recorder->Open(m_record_dir, character, host + ":" + std::to_string(port), error))
			Log("[zone] recording to " + m_recorder->Path());
		else {
			Log("[zone] not recording: " + error);
			m_recorder.reset();
		}
	}

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
	if (Send(ZoneOp::ClientUpdate, update, sizeof(update), "OP_ClientUpdate"))
		RecordSent(update);
}

bool ZoneSession::RequestZoneChange(uint32_t zone_id)
{
	if (m_state != State::InZone || zone_id == 0)
		return false;
	// The zone checks the request against where it last heard we are.
	SendPosition();
	// ZoneChange_Struct: char_name[64], zoneID @64, zone_reason @68 (0: a zone line), success @72.
	uint8_t body[76] = {};
	memcpy(body, m_profile.name.c_str(), std::min<size_t>(m_profile.name.size(), 63));
	int32_t zone = static_cast<int32_t>(zone_id);
	memcpy(body + 64, &zone, 4);
	m_zone_target = zone_id;
	m_zone_state_ms = NowMs();
	m_state = State::ZoneRequested;
	return Send(ZoneOp::ZoneChange, body, sizeof(body), "OP_ZoneChange " + std::to_string(zone_id));
}

bool ZoneSession::SendChannel(uint16_t channel, const std::string &target, const std::string &text)
{
	if (m_state != State::InZone || text.empty())
		return false;
	// ChannelMessage_Struct: targetname[64], sender[64], language u16 @128, chan_num u16 @130,
	// skill u16 @134, message @136. The client sends strlen + 141 bytes (Handle_OP_ChannelMessage notes the extra 4).
	std::string message = text.substr(0, 2000);
	std::vector<uint8_t> body(141 + message.size(), 0);
	memcpy(&body[0], target.c_str(), std::min<size_t>(target.size(), 63));
	memcpy(&body[64], m_profile.name.c_str(), std::min<size_t>(m_profile.name.size(), 63));
	uint16_t skill = 100; // common tongue, which everyone speaks fully
	memcpy(&body[130], &channel, 2);
	memcpy(&body[134], &skill, 2);
	memcpy(&body[136], message.c_str(), message.size());
	return Send(ZoneOp::ChannelMessage, body.data(), body.size(),
		"OP_ChannelMessage " + std::to_string(channel) + (target.empty() ? "" : " to " + target) + ": " + message);
}

bool ZoneSession::Emote(const std::string &text)
{
	if (m_state != State::InZone || text.empty())
		return false;
	// Emote_Struct: u16 unknown, message[1024], sent whole.
	std::vector<uint8_t> body(1026, 0);
	memcpy(&body[2], text.c_str(), std::min<size_t>(text.size(), 511));
	return Send(ZoneOp::Emote, body.data(), body.size(), "OP_Emote" + text);
}

void ZoneSession::SetTarget(uint16_t id, bool command)
{
	if (m_state != State::InZone)
		return;
	Send(command ? ZoneOp::TargetCommand : ZoneOp::TargetMouse, &id, sizeof(id),
		std::string(command ? "OP_TargetCommand " : "OP_TargetMouse ") + std::to_string(id));
	if (m_replay && command && m_spawns.count(id))
		m_replay_target = id; // no zone to answer: as if it took it
}

bool ZoneSession::ClickDoor(uint8_t door, uint16_t item, int16_t skill)
{
	if (m_state != State::InZone)
		return false;
	// ClickDoor_Struct: door id, a byte unused, then UseSwitch's lockpick/disarm skill (i16), the item
	// held (u16, 0xffff none) and who's using it (our spawn id).
	uint8_t body[8] = {door, 0};
	memcpy(body + 2, &skill, 2);
	memcpy(body + 4, &item, 2);
	memcpy(body + 6, &m_my_id, 2);
	return Send(ZoneOp::ClickDoor, body, sizeof(body), "OP_ClickDoor " + std::to_string(door));
}

bool ZoneSession::ClickItemLink(uint16_t id, const std::string &text)
{
	if (m_state != State::InZone)
		return false;
	// ItemViewRequest_Struct: the id, then the name (sscanf's "%s": up to the first space).
	uint8_t body[66] = {};
	memcpy(body, &id, 2);
	std::string word = text.substr(0, text.find(' '));
	memcpy(body + 2, word.c_str(), std::min<size_t>(word.size(), 63));
	return Send(ZoneOp::ItemLinkResponse, body, sizeof(body), "OP_ItemLinkResponse " + std::to_string(id));
}

bool ZoneSession::MemorizeSpell(int slot, int spell, int scribing)
{
	if (m_state != State::InZone)
		return false;
	uint32_t body[3] = {static_cast<uint32_t>(slot), static_cast<uint32_t>(spell), static_cast<uint32_t>(scribing)};
	bool sent = Send(ZoneOp::MemorizeSpell, body, sizeof(body),
		"OP_MemorizeSpell " + std::to_string(slot) + " " + std::to_string(spell) + " " + std::to_string(scribing));
	if (m_replay) {
		// The zone's answers: OPMemorizeSpell's Scribe, Mem or UnmemSpell (which sends the gem's
		// spell after clearing it), and for a scribe, DeleteItemInInventory's OP_MoveItem.
		if (scribing == 2)
			body[1] = 0xffff;
		ReplayAnswer(ZoneOp::MemorizeSpell, body, sizeof(body));
		if (scribing == 0) {
			int32_t gone[3] = {Inventory::Cursor, -1, -1};
			ReplayAnswer(ZoneOp::MoveItem, gone, sizeof(gone));
		}
	}
	return sent;
}

bool ZoneSession::SwapSpells(int from, int to)
{
	if (m_state != State::InZone)
		return false;
	uint32_t body[2] = {static_cast<uint32_t>(from), static_cast<uint32_t>(to)};
	bool sent = Send(ZoneOp::SwapSpell, body, sizeof(body), "OP_SwapSpell " + std::to_string(from) + " " + std::to_string(to));
	if (m_replay)
		ReplayAnswer(ZoneOp::SwapSpell, body, sizeof(body)); // Handle_OP_SwapSpell sends it back
	return sent;
}

bool ZoneSession::DeleteSpell(int slot)
{
	if (m_state != State::InZone)
		return false;
	// RequestSpellDeletion: {slot, 0} as two i32s, success in the second's low byte.
	int32_t body[2] = {slot, 0};
	bool sent = Send(ZoneOp::DeleteSpell, body, sizeof(body), "OP_DeleteSpell " + std::to_string(slot));
	if (m_replay) {
		body[1] = slot >= 0 && slot < ZoneProfile::BookSlots && m_profile.spell_book[slot] != -1;
		ReplayAnswer(ZoneOp::DeleteSpell, body, sizeof(body));
	}
	return sent;
}

void ZoneSession::ReplayAnswer(uint16_t opcode, const void *body, size_t size, uint64_t delay_ms)
{
	const uint8_t *bytes = static_cast<const uint8_t *>(body);
	if (delay_ms > 0)
		m_replay_later.push_back({NowMs() + delay_ms, opcode, std::vector<uint8_t>(bytes, bytes + size)});
	else
		m_replay_answers.emplace_back(opcode, std::vector<uint8_t>(bytes, bytes + size));
}

bool ZoneSession::StopCasting()
{
	if (m_state != State::InZone)
		return false;
	return Send(ZoneOp::ManaChange, nullptr, 0, "OP_ManaChange (stop casting)");
}

bool ZoneSession::SendBuff(int slot, const ZoneBuff &buff, uint32_t how)
{
	if (m_state != State::InZone)
		return false;
	uint8_t body[20] = {};
	memcpy(body, &m_my_id, 2);
	body[2] = buff.type;
	body[3] = buff.level;
	body[4] = buff.bard_modifier;
	body[5] = buff.activated;
	memcpy(body + 6, &buff.spell, 2);
	memcpy(body + 8, &buff.duration, 2);
	memcpy(body + 10, &buff.counters, 2);
	uint16_t slot16 = static_cast<uint16_t>(slot);
	memcpy(body + 12, &slot16, 2);
	memcpy(body + 16, &how, 4);
	return Send(ZoneOp::Buff, body, sizeof(body),
		"OP_Buff slot " + std::to_string(slot) + " spell " + std::to_string(buff.spell) + " how " + std::to_string(how));
}

bool ZoneSession::CastSpell(int gem, int spell, uint16_t target)
{
	if (m_state != State::InZone)
		return false;
	uint16_t body[6] = {static_cast<uint16_t>(gem), static_cast<uint16_t>(spell), 0xffff, target, 0, 0};
	bool sent = Send(ZoneOp::CastSpell, body, sizeof(body),
		"OP_CastSpell " + std::to_string(gem) + " " + std::to_string(spell) + " at " + std::to_string(target));
	if (m_replay) {
		// Mob::CastSpell's answers when it goes off: OP_BeginCast, then at the end CastedSpellFinished's
		// OP_Action on the target, OP_MemorizeSpell 3 (memSpellSpellbar) and SendSpellBarEnable.
		const SpellInfo *info = m_spells ? m_spells->Find(spell) : nullptr;
		int32_t cast_ms = info ? info->cast_ms : 0;
		uint8_t begin[8] = {};
		memcpy(begin, &m_my_id, 2);
		memcpy(begin + 2, &body[1], 2);
		memcpy(begin + 4, &cast_ms, 4);
		ReplayAnswer(ZoneOp::BeginCast, begin, sizeof(begin));
		uint64_t end_ms = std::max(cast_ms, 1);
		uint8_t action[36] = {};
		uint16_t on = target != 0 ? target : m_my_id;
		int32_t instrument_mod = 10;
		memcpy(action, &on, 2);
		memcpy(action + 2, &m_my_id, 2);
		action[4] = m_profile.level;
		memcpy(action + 8, &instrument_mod, 4);
		action[24] = 231;
		memcpy(action + 30, &body[1], 2);
		action[33] = 4;
		ReplayAnswer(ZoneOp::Action, action, sizeof(action), end_ms);
		uint32_t recast[3] = {static_cast<uint32_t>(gem), static_cast<uint32_t>(spell), 3};
		ReplayAnswer(ZoneOp::MemorizeSpell, recast, sizeof(recast), end_ms);
		uint16_t mana[2] = {static_cast<uint16_t>(std::max(m_profile.mana - (info ? info->mana : 0), 0)), body[1]};
		ReplayAnswer(ZoneOp::ManaChange, mana, sizeof(mana), end_ms);
	}
	return sent;
}

bool ZoneSession::MoveItem(int16_t from, int16_t to, int32_t qty)
{
	if (m_state != State::InZone)
		return false;
	// MoveItem_Struct: from, to and the count, each an i32.
	int32_t body[3] = {from, to, qty};
	bool sent = Send(ZoneOp::MoveItem, body, sizeof(body),
		"OP_MoveItem " + std::to_string(from) + " -> " + std::to_string(to) + (qty ? " x" + std::to_string(qty) : ""));
	UpdateAppearance();
	return sent;
}

float ZoneSession::SpawnDistance(uint16_t id) const
{
	auto it = m_spawns.find(id);
	if (it == m_spawns.end())
		return -1e27f;
	float dx = it->second.x - m_profile.x, dy = it->second.y - m_profile.y, dz = it->second.z - m_profile.z;
	return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool ZoneSession::ShopRequest(uint16_t npc)
{
	if (m_state != State::InZone)
		return false;
	uint8_t body[12] = {};
	std::memcpy(body, &npc, 2);
	std::memcpy(body + 2, &m_my_id, 2);
	bool sent = Send(ZoneOp::ShopRequest, body, sizeof(body), "OP_ShopRequest " + std::to_string(npc));
	if (sent && m_replay) { // the replay's merchant is closed
		uint8_t answer[12] = {};
		std::memcpy(answer, &npc, 2);
		ReplayAnswer(ZoneOp::ShopRequest, answer, sizeof(answer));
	}
	return sent;
}

bool ZoneSession::ShopBuy(uint16_t npc, int16_t slot, int32_t quantity)
{
	if (m_state != State::InZone)
		return false;
	uint8_t body[16] = {};
	std::memcpy(body, &npc, 2);
	std::memcpy(body + 2, &m_my_id, 2);
	std::memcpy(body + 4, &slot, 2);
	std::memcpy(body + 8, &quantity, 4);
	return Send(ZoneOp::ShopPlayerBuy, body, sizeof(body),
		"OP_ShopPlayerBuy slot " + std::to_string(slot) + " x" + std::to_string(quantity));
}

bool ZoneSession::ShopSell(uint16_t npc, int16_t slot, int32_t quantity)
{
	if (m_state != State::InZone)
		return false;
	uint8_t body[16] = {};
	std::memcpy(body, &npc, 2);
	std::memcpy(body + 2, &m_my_id, 2);
	std::memcpy(body + 4, &slot, 2);
	std::memcpy(body + 8, &quantity, 4);
	return Send(ZoneOp::ShopPlayerSell, body, sizeof(body),
		"OP_ShopPlayerSell slot " + std::to_string(slot) + " x" + std::to_string(quantity));
}

bool ZoneSession::ShopEnd(uint16_t npc)
{
	for (int16_t slot = 6000; slot < 6080; ++slot)
		m_items.erase(slot);
	if (m_state != State::InZone)
		return false;
	uint16_t body[2] = {npc, m_my_id};
	return Send(ZoneOp::ShopEnd, body, sizeof(body), "OP_ShopEnd " + std::to_string(npc));
}

bool ZoneSession::MoveCoin(int from, int to, int from_coin, int to_coin, int32_t amount)
{
	if (m_state != State::InZone)
		return false;
	int32_t body[5] = {from, to, from_coin, to_coin, amount};
	return Send(ZoneOp::MoveCoin, body, sizeof(body),
		"OP_MoveCoin " + std::to_string(from) + ":" + std::to_string(from_coin) + " -> " + std::to_string(to) + ":" +
			std::to_string(to_coin) + " x" + std::to_string(amount));
}

bool ZoneSession::TradeSkillCombine(uint8_t bag_type, int16_t slot, const int16_t ids[10], uint16_t bag_id)
{
	if (m_state != State::InZone)
		return false;
	// Combine_Struct (32 bytes): {u8 type, u8, i16 skill (0), i16 slot @4, i16 ids[10] @6,
	// i16 product (-1) @26, i16 @28, u16 the bag's id @30}.
	uint8_t body[32] = {};
	body[0] = bag_type;
	std::memcpy(body + 4, &slot, 2);
	std::memcpy(body + 6, ids, 20);
	int16_t none = -1;
	std::memcpy(body + 26, &none, 2);
	std::memcpy(body + 30, &bag_id, 2);
	bool sent = Send(ZoneOp::TradeSkillCombine, body, sizeof(body), "OP_TradeSkillCombine " + std::to_string(slot));
	if (sent && m_replay) // the replay's zone has nothing to make
		ReplayAnswer(ZoneOp::TradeSkillCombine, nullptr, 0);
	return sent;
}

void ZoneSession::WornLook(uint16_t material[9], uint32_t color[9]) const
{
	auto at = [&](int16_t slot) -> const ZoneItem * {
		auto it = m_items.find(slot);
		return it != m_items.end() && it->second.item_class == 0 ? &it->second : nullptr;
	};
	auto it_number = [](const ZoneItem *item) {
		return item && item->idfile.size() > 2 ? static_cast<uint16_t>(atoi(item->idfile.c_str() + 2)) : uint16_t{0};
	};
	// HandleMaterial: head, chest, arms, wrists (the first that has one), hands, legs and feet show
	// their item's material and tint; a helm whose model is IT240 or over (the custom ones) that.
	static const int16_t worn[7] = {2, 17, 7, 9, 12, 18, 19};
	for (int k = 0; k < 9; ++k) {
		material[k] = 0;
		color[k] = 0;
	}
	for (int k = 0; k < 7; ++k) {
		const ZoneItem *item = at(worn[k]);
		if (k == 3 && !item)
			item = at(10);
		if (!item)
			continue;
		material[k] = item->material;
		color[k] = item->color;
		if (k == 0 && it_number(item) >= 240)
			material[k] = it_number(item);
	}
	// UpdateEquipmentDisplay: the hands hold their items' IT models, the left the ranged item when
	// both are empty.
	const ZoneItem *left = at(14);
	if (!at(13) && !left)
		left = at(11);
	material[7] = it_number(at(13));
	material[8] = it_number(left);
}

void ZoneSession::UpdateAppearance()
{
	uint16_t material[9];
	uint32_t color[9];
	WornLook(material, color);
	if (!m_worn_known) {
		std::copy(material, material + 9, m_worn_material);
		std::copy(color, color + 9, m_worn_color);
		m_worn_known = true;
	}
	bool tell = m_my_id != 0 && (m_state == State::InZone || m_state == State::Camping);
	bool looks = false;
	for (uint8_t k = 0; k < 9; ++k) {
		if (m_worn_material[k] == material[k] && m_worn_color[k] == color[k])
			continue;
		m_worn_material[k] = material[k];
		m_worn_color[k] = color[k];
		m_self.equipment[k] = material[k];
		m_self.colors[k] = color[k];
		looks = true;
		if (!tell)
			continue;
		// WearChange_Struct: {u16 spawn, u8 slot, pad, u16 material, pad, u32 color}.
		uint8_t body[12] = {};
		memcpy(body, &m_my_id, 2);
		body[2] = k;
		memcpy(body + 4, &material[k], 2);
		memcpy(body + 8, &color[k], 4);
		Send(ZoneOp::WearChange, body, sizeof(body),
			"OP_WearChange slot " + std::to_string(k) + " material " + std::to_string(material[k]));
	}
	if (looks)
		++m_self.look_serial;

	auto at = [&](int16_t slot) -> const ZoneItem * {
		auto it = m_items.find(slot);
		return it != m_items.end() && it->second.item_class == 0 ? &it->second : nullptr;
	};
	// UpdateLight: the brightest light (CDisplay_GetIntensity) of a common item we have on, unless
	// one in a general slot is a lightstone's (types 9 to 13), which shines instead.
	auto intensity = [](uint8_t type) {
		static const uint8_t by_type[16] = {0, 1, 2, 3, 6, 7, 8, 9, 10, 3, 7, 9, 4, 5, 4, 5};
		return type < 16 ? by_type[type] : uint8_t{0};
	};
	uint8_t light = 0, brightest = 0;
	for (int16_t slot = 1; slot <= 21; ++slot)
		if (const ZoneItem *item = at(slot); item && intensity(item->light) > brightest) {
			brightest = intensity(item->light);
			light = item->light;
		}
	for (int16_t slot = 22; slot <= 29; ++slot)
		if (const ZoneItem *item = at(slot); item && item->light >= 9 && item->light <= 13 && item->light > intensity(item->light))
			light = item->light;
	if (light != m_self.light) {
		m_self.light = light;
		// CDisplay::ChangeLight -> CEverQuest_SendLightInfo: SpawnAppearance_Struct {u16 spawn, u16 type, u32 value}.
		if (tell) {
			uint8_t body[8] = {};
			memcpy(body, &m_my_id, 2);
			uint16_t type = AppearanceLight;
			memcpy(body + 2, &type, 2);
			uint32_t value = light;
			memcpy(body + 4, &value, 4);
			Send(ZoneOp::SpawnAppearance, body, sizeof(body), "OP_SpawnAppearance light " + std::to_string(light));
		}
	}
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

void ZoneSession::SetStance(uint8_t stance)
{
	if (m_state != State::InZone)
		return;
	// SpawnAppearance_Struct: {u16 spawn id, u16 type, u32 value}.
	uint8_t body[8] = {};
	uint32_t value = stance;
	memcpy(body, &m_my_id, 2);
	memcpy(body + 2, &AppearanceAnimation, 2);
	memcpy(body + 4, &value, 4);
	Send(ZoneOp::SpawnAppearance, body, sizeof(body), "OP_SpawnAppearance " + std::to_string(stance));
	m_self.stance = stance;
}

void ZoneSession::SetAfk(bool afk)
{
	if (m_state != State::InZone)
		return;
	uint8_t body[8] = {};
	uint32_t value = afk ? 1 : 0;
	memcpy(body, &m_my_id, 2);
	memcpy(body + 2, &AppearanceAfk, 2);
	memcpy(body + 4, &value, 4);
	Send(ZoneOp::SpawnAppearance, body, sizeof(body), "OP_SpawnAppearance afk " + std::to_string(value));
	m_self.afk = static_cast<uint8_t>(value);
}

void ZoneSession::SetAnonymous(uint8_t anon)
{
	if (m_state != State::InZone)
		return;
	uint8_t body[8] = {};
	uint32_t value = anon;
	memcpy(body, &m_my_id, 2);
	memcpy(body + 2, &AppearanceAnonymous, 2);
	memcpy(body + 4, &value, 4);
	Send(ZoneOp::SpawnAppearance, body, sizeof(body), "OP_SpawnAppearance anonymous " + std::to_string(value));
	m_self.anon = anon;
}

bool ZoneSession::Consider(uint16_t id, bool corpse)
{
	if (m_state != State::InZone)
		return false;
	uint8_t body[24] = {};
	memcpy(body, &m_my_id, 2);
	memcpy(body + 2, &id, 2);
	return Send(corpse ? ZoneOp::ConsiderCorpse : ZoneOp::Consider, body, sizeof(body),
		std::string(corpse ? "OP_ConsiderCorpse " : "OP_Consider ") + std::to_string(id));
}

bool ZoneSession::Assist(uint16_t id)
{
	if (m_state != State::InZone)
		return false;
	return Send(ZoneOp::Assist, &id, sizeof(id), "OP_Assist " + std::to_string(id));
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
	if (Send(ZoneOp::ClientUpdate, update, sizeof(update), m_verbose ? "OP_ClientUpdate" : ""))
		RecordSent(update);
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

bool ZoneSession::NetworkStats(RdpStats &out) const
{
	return m_session && m_session->Stats(out);
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

bool ZoneSession::GameDate(int &hour, int &minute, int &day, int &month, int &year) const
{
	// EQWorldData::AdvanceTime: the time since OP_TimeOfDay at 20 game seconds a second, into
	// years (12 months), months (28 days), days, hours and minutes, each added to what the zone sent
	// and carried over.
	if (!m_time_received_ms)
		return false;
	uint64_t game = (NowMs() - m_time_received_ms) / 50; // game seconds
	int years = static_cast<int>(game / 29030400);
	game %= 29030400;
	int months = static_cast<int>(game / 2419200);
	game %= 2419200;
	int days = static_cast<int>(game / 86400);
	game %= 86400;
	int hours = static_cast<int>(game / 3600);
	minute = static_cast<int>(game % 3600 / 60) + m_time_minute;
	for (; minute > 59; minute -= 60)
		++hours;
	hour = m_time_hour + hours - 1;
	for (; hour > 23; hour -= 24)
		++days;
	++hour;
	day = m_time_day + days - 1;
	for (; day > 27; day -= 28)
		++months;
	++day;
	month = m_time_month + months;
	for (; month > 12; month -= 12)
		++years;
	year = m_time_year + years;
	if (year > 9999)
		year = 0;
	return true;
}

uint32_t ZoneSession::MinutesPlayed() const
{
	return m_profile.minutes_played + (m_profile_ms ? static_cast<uint32_t>((NowMs() - m_profile_ms) / 60000) : 0);
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
	m_recorder.reset();
	m_replay.reset();
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
		p.str = Read<int16_t>(data, 164);
		p.sta = Read<int16_t>(data, 166);
		p.cha = Read<int16_t>(data, 168);
		p.dex = Read<int16_t>(data, 170);
		p.int_ = Read<int16_t>(data, 172);
		p.agi = Read<int16_t>(data, 174);
		p.wis = Read<int16_t>(data, 176);
		p.fatigue = Read<int8_t>(data, 4962);
		for (int i = 0; i < 32; ++i)
			p.languages[i] = Read<uint8_t>(data, 284 + i);
		p.y = Read<float>(data, 2904);
		p.x = Read<float>(data, 2908);
		p.z = Read<float>(data, 2912);
		p.heading = Read<float>(data, 2916);
		p.platinum = Read<int32_t>(data, 2924);
		p.gold = Read<int32_t>(data, 2928);
		p.silver = Read<int32_t>(data, 2932);
		p.copper = Read<int32_t>(data, 2936);
		p.bank_platinum = Read<int32_t>(data, 2940);
		p.bank_gold = Read<int32_t>(data, 2944);
		p.bank_silver = Read<int32_t>(data, 2948);
		p.bank_copper = Read<int32_t>(data, 2952);
		p.cursor_platinum = Read<int32_t>(data, 2956);
		p.cursor_gold = Read<int32_t>(data, 2960);
		p.cursor_silver = Read<int32_t>(data, 2964);
		p.cursor_copper = Read<int32_t>(data, 2968);
		p.zone_id = Read<uint32_t>(data, 3444);
		p.deity = Read<uint16_t>(data, 4944);
		p.birthday = Read<uint32_t>(data, 4948);
		p.minutes_played = Read<uint32_t>(data, 4956);
		m_profile_ms = NowMs();
		for (int i = 0; i < 100; ++i)
			p.skills[i] = Read<int16_t>(data, 2988 + i * 2);
		for (int i = 0; i < ZoneProfile::BookSlots; ++i)
			p.spell_book[i] = Read<int16_t>(data, 1846 + i * 2);
		for (int i = 0; i < ZoneProfile::Gems; ++i)
			p.mem_spells[i] = Read<int16_t>(data, 2870 + i * 2);
		for (int i = 0; i < ZoneProfile::BuffSlots; ++i)
			p.buffs[i] = ReadBuff(data, 616 + i * 10);
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
		// SpawnHPUpdate_Struct: a percentage (of 100) for everyone but us; ours is our hit points
		// less what worn items add, and the total maximum.
		uint16_t id = Read<uint16_t>(data, 0);
		int32_t cur = Read<int32_t>(data, 4), max = Read<int32_t>(data, 8);
		if (id != 0 && id == m_my_id) {
			m_hp = cur;
			m_max_hp = max;
		}
		auto it = m_spawns.find(id);
		if (it != m_spawns.end())
			it->second.hp = static_cast<int16_t>(max > 0 && max != 100 ? cur * 100 / max : cur);
		break;
	}
	case ZoneOp::ManaChange: {
		// ManaChange_Struct: {u16 new mana, u16 the spell that used it}. The spell is there in the
		// zone's (SendSpellBarEnable's) 4 bytes; eqgame stops the cast it has without it.
		if (data.size() < 2)
			break;
		m_profile.mana = Read<int16_t>(data, 0);
		Event e(Event::Type::ManaChanged);
		e.value = data.size() == 4 ? Read<int16_t>(data, 2) : -1;
		events.push_back(e);
		break;
	}
	case ZoneOp::Stamina:
		// Stamina_Struct: {i16 food, i16 water, i8 fatigue}.
		if (data.size() >= 5)
			m_profile.fatigue = Read<int8_t>(data, 4);
		break;
	case ZoneOp::ExpUpdate:
		m_profile.exp = Read<uint32_t>(data, 0);
		break;
	case ZoneOp::LevelUpdate:
		// LevelUpdate_Struct: {u32 level, u32 old level, u32 exp}.
		if (data.size() >= 12) {
			m_profile.level = static_cast<uint8_t>(Read<uint32_t>(data, 0));
			m_profile.exp = Read<uint32_t>(data, 8);
		}
		break;
	case ZoneOp::SkillUpdate: {
		// msg_skillimprove: skills 0..99, with its message.
		int32_t skill = Read<int32_t>(data, 0);
		if (data.size() < 8 || skill < 0 || skill >= 100)
			break;
		m_profile.skills[skill] = static_cast<int16_t>(Read<int32_t>(data, 4));
		Event e{Event::Type::SkillUp};
		e.id = static_cast<uint16_t>(skill);
		e.value = m_profile.skills[skill];
		events.push_back(e);
		break;
	}
	case ZoneOp::SkillUpdate2: {
		// HandleWorldMessage's 0x41be: ours only, skills 0..73, and no value below 0.
		int32_t skill = Read<int32_t>(data, 4), value = Read<int32_t>(data, 8);
		if (data.size() >= 12 && Read<uint16_t>(data, 0) == m_my_id && skill >= 0 && skill <= 73 && value >= 0)
			m_profile.skills[skill] = static_cast<int16_t>(value);
		break;
	}
	case ZoneOp::TradeMoneyUpdate: {
		// UpdateMoneyDisplay: the amount added to one kind of coin.
		if (data.size() < 8)
			break;
		int32_t amount = Read<int32_t>(data, 4);
		switch (data[2]) {
		case 0: m_profile.copper += amount; break;
		case 1: m_profile.silver += amount; break;
		case 2: m_profile.gold += amount; break;
		case 3: m_profile.platinum += amount; break;
		default: break;
		}
		break;
	}
	case ZoneOp::CharInventory: {
		// A count byte, a byte unused, then deflated {u16 opcode, Item_Struct} records.
		std::vector<uint8_t> items;
		if (data.size() > 2 && Inflate::Zlib(data.data() + 2, data.size() - 2, items)) {
			m_items.clear();
			for (size_t o = 0; o + 2 + ItemSize <= items.size(); o += 2 + ItemSize) {
				ZoneItem item = ParseItem(items, o + 2);
				m_items[item.slot] = item;
			}
			Log("[zone]    inventory: " + std::to_string(m_items.size()) + " items");
			WornLook(m_worn_material, m_worn_color); // what the zone already showed us in
			m_worn_known = true;
		}
		break;
	}
	case ZoneOp::ItemPacket:
	case ZoneOp::ContainerPacket:
	case ZoneOp::BookPacket:
	case ZoneOp::SummonedItem:
		if (data.size() >= ItemSize) {
			ZoneItem item = ParseItem(data, 0);
			m_items[item.slot] = item;
			UpdateAppearance(); // msg_snd_ieq
		}
		break;
	case ZoneOp::ShopRequest: {
		// msg_req_merchantitems: {u16 npc, u16, u8 command, u8[3], float rate}.
		if (data.size() < 12)
			break;
		Event e{Event::Type::MerchantOpened};
		e.merchant.npc = Read<uint16_t>(data, 0);
		e.merchant.command = data[4];
		e.merchant.rate = Read<float>(data, 8);
		events.push_back(e);
		break;
	}
	case ZoneOp::ShopInventoryPacket: {
		// A count, a byte, then deflated {u16 item class, Item_Struct} records, each in its slot.
		std::vector<uint8_t> list;
		if (data.size() > 2 && Inflate::Zlib(data.data() + 2, data.size() - 2, list))
			for (size_t o = 0; o + 2 + ItemSize <= list.size(); o += 2 + ItemSize) {
				ZoneItem item = ParseItem(list, o + 2);
				if (item.slot >= 0 && item.slot < 80) {
					item.slot = static_cast<int16_t>(6000 + item.slot);
					m_items[item.slot] = item;
				}
			}
		break;
	}
	case ZoneOp::MerchantItemPacket:
		// One more of a merchant's (something we sold it), in its slot.
		if (data.size() >= ItemSize) {
			ZoneItem item = ParseItem(data, 0);
			if (item.slot >= 0 && item.slot < 80) {
				item.slot = static_cast<int16_t>(6000 + item.slot);
				m_items[item.slot] = item;
			}
		}
		break;
	case ZoneOp::ShopPlayerBuy:
	case ZoneOp::ShopPlayerSell: {
		// Merchant_Sell_Struct {u16 npc, u16, u16 slot, u8, u8, u8 quantity @8, u8[3], u32 price @12};
		// Merchant_Purchase_Struct {u16 npc, u16, u16 slot, u16 price, u8 quantity @8, ...}.
		if (data.size() < 12)
			break;
		bool buy = opcode == ZoneOp::ShopPlayerBuy;
		Event e{buy ? Event::Type::MerchantBought : Event::Type::MerchantSold};
		e.merchant.npc = Read<uint16_t>(data, 0);
		e.merchant.slot = Read<int16_t>(data, 4);
		e.merchant.quantity = Read<int32_t>(data, 8);
		e.merchant.price = buy ? (data.size() >= 16 ? Read<int32_t>(data, 12) : 0) : Read<uint16_t>(data, 6);
		events.push_back(e);
		break;
	}
	case ZoneOp::ShopDelItem: {
		// Merchant_DelItem_Struct {u16 npc, u16, u8 slot, u8 type}.
		if (data.size() < 5)
			break;
		int16_t slot = static_cast<int8_t>(data[4]);
		if (slot >= 0)
			m_items.erase(static_cast<int16_t>(6000 + slot));
		Event e{Event::Type::MerchantItemGone};
		e.merchant.slot = slot;
		events.push_back(e);
		break;
	}
	case ZoneOp::ItemLinkResponse:
		// msg_request_inspect_item: a link's item, for the item display.
		if (data.size() >= ItemSize) {
			m_linked_item = ParseItem(data, 0);
			events.push_back({Event::Type::ItemLink});
		}
		break;
	case ZoneOp::MoveItem: {
		// The zone moving one of our items (or deleting it: to -1); one already there swaps, and a
		// bag's contents go with it.
		if (data.size() < 8)
			break;
		int16_t from = static_cast<int16_t>(Read<int32_t>(data, 0)), to = static_cast<int16_t>(Read<int32_t>(data, 4));
		if (!m_items.count(from))
			break;
		if (to < 0)
			Inventory::Remove(m_items, from);
		else
			Inventory::Swap(m_items, from, to);
		UpdateAppearance();
		break;
	}
	case ZoneOp::TradeSkillCombine:
		events.push_back({Event::Type::CombineAnswered});
		break;
	case ZoneOp::MemorizeSpell: {
		// msg_launchspell_info: 0 a spell scribed (FinishScribing), 1 memorized (FinishMemorizing),
		// 2 forgotten (ForgetMemorizedSpell), 3 a gem's spell gone off (its recast and recovery
		// times start), -1 neither (the zone turning one down).
		if (data.size() < 12)
			break;
		int32_t slot = Read<int32_t>(data, 0), spell = Read<int32_t>(data, 4), scribing = Read<int32_t>(data, 8);
		Event e;
		e.id = static_cast<uint16_t>(slot);
		e.value = spell;
		if (scribing == 0 && slot >= 0 && slot < ZoneProfile::BookSlots) {
			m_profile.spell_book[slot] = static_cast<int16_t>(spell);
			e.type = Event::Type::SpellScribed;
		}
		else if (scribing == 1 && slot >= 0 && slot < ZoneProfile::Gems) {
			m_profile.mem_spells[slot] = static_cast<int16_t>(spell);
			e.type = Event::Type::SpellMemorized;
		}
		else if (scribing == 2 && slot >= 0 && slot < ZoneProfile::Gems) {
			e.value = m_profile.mem_spells[slot];
			m_profile.mem_spells[slot] = -1;
			e.type = Event::Type::SpellForgotten;
		}
		else if (scribing == 3) {
			if (slot >= ZoneProfile::Gems && slot != 9) // a gem, or 9 (an ability's)
				break;
			e.type = Event::Type::SpellRecast;
		}
		else
			e.type = Event::Type::SpellBarUnlocked;
		events.push_back(e);
		break;
	}
	case ZoneOp::SwapSpell: {
		// HandleWorldMessage's 0x41ce: the two book slots trade spells.
		if (data.size() < 8)
			break;
		int32_t from = Read<int32_t>(data, 0), to = Read<int32_t>(data, 4);
		if (from < 0 || to < 0 || from >= ZoneProfile::BookSlots || to >= ZoneProfile::BookSlots)
			break;
		std::swap(m_profile.spell_book[from], m_profile.spell_book[to]);
		Event e(Event::Type::SpellsSwapped);
		e.id = static_cast<uint16_t>(from);
		e.value = to;
		events.push_back(e);
		break;
	}
	case ZoneOp::DeleteSpell: {
		// CSpellBookWnd::DeleteSpellFromBook: success 1 empties the slot.
		if (data.size() < 5)
			break;
		int16_t slot = Read<int16_t>(data, 0);
		bool deleted = data[4] == 1 && slot >= 0 && slot < ZoneProfile::BookSlots;
		if (deleted)
			m_profile.spell_book[slot] = -1;
		Event e(Event::Type::SpellDeleted);
		e.id = static_cast<uint16_t>(slot);
		e.value = deleted;
		events.push_back(e);
		break;
	}
	case ZoneOp::Emote: {
		// Emote_Struct: u16 unknown, then the text with the sender's name in front.
		Event e;
		e.type = Event::Type::Emote;
		e.text = CString(data, 2, 1024);
		events.push_back(e);
		break;
	}
	case ZoneOp::LFGCommand: {
		uint16_t id = Read<uint16_t>(data, 0);
		auto it = m_spawns.find(id);
		ZoneSpawn *spawn = it != m_spawns.end() ? &it->second : id != 0 && id == m_my_id ? &m_self : nullptr;
		if (spawn)
			spawn->lfg = Read<int32_t>(data, 4) != 0;
		break;
	}
	case ZoneOp::GuildsList:
		m_guilds = ParseGuildList(data);
		break;
	case ZoneOp::Consider: {
		if (data.size() < 24)
			break;
		Event e(Event::Type::Considered);
		e.consider.target = Read<uint16_t>(data, 2);
		e.consider.faction = Read<uint32_t>(data, 4);
		e.consider.color = Read<uint32_t>(data, 8);
		e.consider.pvpcon = data[20];
		events.push_back(e);
		break;
	}
	case ZoneOp::Assist: {
		// HandleWorldMessage's 0x4200: a spawn we know becomes our target; -1 (no one) changes nothing.
		Event e(Event::Type::Assisted);
		e.id = Read<uint16_t>(data, 0);
		if (e.id != 0 && e.id != 0xffff)
			events.push_back(e);
		break;
	}
	case ZoneOp::TargetCommand: {
		Event e;
		e.type = Event::Type::TargetChanged;
		e.id = Read<uint16_t>(data, 0);
		events.push_back(e);
		break;
	}
	case ZoneOp::SpawnAppearance: {
		uint16_t type = Read<uint16_t>(data, 2);
		if (type == AppearanceSpawnId) {
			m_my_id = static_cast<uint16_t>(Read<uint32_t>(data, 4));
			Log("[zone]    my spawn id " + std::to_string(m_my_id));
		}
		else if (type == AppearanceInvis || type == AppearancePvp || type == AppearanceLinkdead || type == AppearanceGm ||
		         type == AppearanceAnonymous || type == AppearanceGuild || type == AppearanceAfk) {
			// What the name over their head shows; drawn from the spawn each frame.
			uint16_t id = Read<uint16_t>(data, 0);
			auto it = m_spawns.find(id);
			ZoneSpawn *spawn = it != m_spawns.end() ? &it->second : id != 0 && id == m_my_id ? &m_self : nullptr;
			uint32_t value = Read<uint32_t>(data, 4);
			if (spawn) {
				switch (type) {
				case AppearanceInvis: spawn->invis = static_cast<uint8_t>(value); break;
				case AppearancePvp: spawn->pvp = static_cast<uint8_t>(value); break;
				case AppearanceLinkdead: spawn->linkdead = static_cast<uint8_t>(value); break;
				case AppearanceGm: spawn->gm = static_cast<uint8_t>(value); break;
				case AppearanceAnonymous: spawn->anon = static_cast<uint8_t>(value); break;
				case AppearanceGuild: spawn->guild_id = value == 0 ? -1 : static_cast<int16_t>(value); break;
				default: spawn->afk = static_cast<uint8_t>(value); break;
				}
			}
		}
		else if (type == AppearanceLight && Read<uint16_t>(data, 0) == m_my_id && m_my_id != 0) {
			m_self.light = static_cast<uint8_t>(Read<uint32_t>(data, 4)); // we're not among the spawns
		}
		else if (type == AppearanceAnimation || type == AppearanceLight) {
			auto it = m_spawns.find(Read<uint16_t>(data, 0));
			if (it != m_spawns.end()) {
				uint8_t value = static_cast<uint8_t>(Read<uint32_t>(data, 4));
				(type == AppearanceLight ? it->second.light : it->second.stance) = value;
				Event e;
				e.type = Event::Type::SpawnChanged;
				e.id = it->first;
				events.push_back(e);
			}
		}
		break;
	}
	case ZoneOp::WearChange: {
		// WearChange_Struct: a slot's material and tint changed (an item worn or taken off).
		uint16_t id = Read<uint16_t>(data, 0);
		auto it = m_spawns.find(id);
		uint8_t slot = data.size() >= 12 ? data[2] : 0xff;
		if (id == m_my_id && id != 0 && slot < 9) {
			// SwapItem's for us, after an item went on: we're not among the spawns.
			uint16_t material = Read<uint16_t>(data, 4);
			uint32_t color = Read<uint32_t>(data, 8);
			if (m_self.equipment[slot] != material || m_self.colors[slot] != color) {
				m_self.equipment[slot] = material;
				m_self.colors[slot] = color;
				++m_self.look_serial;
			}
		}
		else if (it != m_spawns.end() && slot < 9) {
			it->second.equipment[slot] = Read<uint16_t>(data, 4);
			it->second.colors[slot] = Read<uint32_t>(data, 8);
			++it->second.look_serial;
			Event e;
			e.type = Event::Type::SpawnChanged;
			e.id = it->first;
			events.push_back(e);
		}
		break;
	}
	case ZoneOp::Animation: {
		// Animation_Struct: {u16 spawn, pad, i32 animation id, float speed}, played once.
		auto it = m_spawns.find(Read<uint16_t>(data, 0));
		if (it != m_spawns.end() && data.size() >= 12) {
			it->second.action = Read<int32_t>(data, 4);
			it->second.action_speed = Read<float>(data, 8);
			++it->second.action_serial;
		}
		break;
	}
	case ZoneOp::BeginCast: {
		if (data.size() < 8)
			break;
		Event e;
		e.type = Event::Type::CastBegan;
		e.cast.caster = Read<uint16_t>(data, 0);
		e.cast.spell = Read<uint16_t>(data, 2);
		e.cast.cast_ms = Read<int32_t>(data, 4);
		events.push_back(e);
		break;
	}
	case ZoneOp::Action: {
		if (data.size() < 34)
			break;
		Event e;
		e.type = Event::Type::SpellAction;
		e.cast.target = Read<uint16_t>(data, 0);
		e.cast.caster = Read<uint16_t>(data, 2);
		e.cast.level = Read<uint16_t>(data, 4);
		e.cast.instrument_mod = Read<int32_t>(data, 8);
		e.cast.type = data[24];
		e.cast.spell = Read<int16_t>(data, 30);
		e.cast.flags = data[33];
		events.push_back(e);
		break;
	}
	case ZoneOp::Buff: {
		if (data.size() < 20)
			break;
		Event e(Event::Type::BuffChanged);
		e.buff = ReadBuff(data, 2);
		e.id = Read<uint16_t>(data, 12);
		e.value = static_cast<int32_t>(Read<uint32_t>(data, 16));
		events.push_back(e);
		break;
	}
	case ZoneOp::Damage: {
		if (data.size() < 12)
			break;
		Event e;
		e.type = Event::Type::Damaged;
		e.cast.target = Read<uint16_t>(data, 0);
		e.cast.caster = Read<uint16_t>(data, 2);
		e.cast.type = data[4];
		e.cast.spell = Read<int16_t>(data, 6);
		e.cast.damage = Read<int32_t>(data, 8);
		events.push_back(e);
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
		// TimeOfDay_Struct: {u8 hour, u8 minute, u8 day, u8 month, u16 year}.
		if (data.size() >= 2) {
			m_time_hour = data[0];
			m_time_minute = data[1];
			m_time_received_ms = NowMs();
		}
		if (data.size() >= 6) {
			m_time_day = data[2];
			m_time_month = data[3];
			m_time_year = Read<uint16_t>(data, 4);
		}
		char text[32];
		snprintf(text, sizeof(text), "%u:%02u", data.size() > 0 ? data[0] : 0, data.size() > 1 ? data[1] : 0);
		Event e;
		e.type = Event::Type::TimeOfDay;
		e.text = text;
		events.push_back(e);
		break;
	}
	case ZoneOp::Weather: {
		// Weather_Struct: u32 type, u32 intensity. HandleWorldMessage keeps them for RealRender_World.
		Event e;
		e.type = Event::Type::Weather;
		e.id = static_cast<uint16_t>(Read<uint32_t>(data, 0));
		e.value = Read<int32_t>(data, 4);
		events.push_back(e);
		break;
	}
	case ZoneOp::ChannelMessage: {
		Event e;
		e.type = Event::Type::Chat;
		e.chat.target = CString(data, 0, 64);
		e.chat.sender = CString(data, 64, 64);
		e.chat.language = Read<uint16_t>(data, 128);
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
		e.value = Read<int32_t>(data, 0);
		e.text = CString(data, 4, 2048);
		Log("[zone]    message: " + e.text);
		events.push_back(e);
		break;
	}
	case ZoneOp::TokenText: {
		// HandleWorldMessage's 0x4235: an eqstr string as it is, in its color (InterruptSpell's).
		if (data.size() < 4)
			break;
		int16_t id = Read<int16_t>(data, 0);
		Event e(Event::Type::Message);
		e.value = Read<int16_t>(data, 2);
		e.text = m_eqstr ? m_eqstr->Format(id, {}) : "[string " + std::to_string(id) + "]";
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
		e.value = Read<uint16_t>(data, 4);
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
	case ZoneOp::SendZonepoints: {
		// ZonePoints: count, then {iterator, y, x, z, heading, zoneid u16, unused u16}; one extra at the end.
		m_zone_points.clear();
		uint32_t count = data.size() >= 4 ? Read<uint32_t>(data, 0) : 0;
		for (uint32_t i = 0; i < count && 4 + (i + 1) * 24 <= data.size(); ++i) {
			size_t at = 4 + i * 24;
			ZonePoint point;
			point.index = Read<uint32_t>(data, at);
			point.y = Read<float>(data, at + 4);
			point.x = Read<float>(data, at + 8);
			point.z = Read<float>(data, at + 12);
			point.heading = Read<float>(data, at + 16);
			point.zone = Read<uint16_t>(data, at + 20);
			m_zone_points.push_back(point);
		}
		Log("[zone]    " + std::to_string(m_zone_points.size()) + " zone points");
		break;
	}
	case ZoneOp::SpawnDoor: {
		// A u16 count, then the Door_Structs deflated (EntityList::MakeDoorSpawnPacket).
		std::vector<uint8_t> doors;
		uint16_t count = Read<uint16_t>(data, 0);
		if (data.size() < 2 || !Inflate::Zlib(data.data() + 2, data.size() - 2, doors)) {
			Log("[zone]    couldn't inflate OP_SpawnDoor");
			break;
		}
		m_doors.clear();
		for (size_t i = 0; i < count && (i + 1) * 44 <= doors.size(); ++i) {
			size_t at = i * 44;
			ZoneDoor door;
			door.name = CString(doors, at, 16);
			door.y = Read<float>(doors, at + 16);
			door.x = Read<float>(doors, at + 20);
			door.z = Read<float>(doors, at + 24);
			door.heading = Read<float>(doors, at + 28);
			door.incline = Read<int16_t>(doors, at + 32);
			door.size = Read<int16_t>(doors, at + 34);
			door.id = doors[at + 38];
			door.type = doors[at + 39];
			door.open = doors[at + 40];
			door.inverted = doors[at + 41];
			door.param = Read<int16_t>(doors, at + 42);
			m_doors.push_back(door);
		}
		Log("[zone]    " + std::to_string(m_doors.size()) + " doors");
		events.push_back({Event::Type::DoorsSpawned});
		break;
	}
	case ZoneOp::MoveDoor: {
		if (data.size() < 2)
			break;
		Event e(Event::Type::DoorMoved);
		e.id = data[0];
		e.value = data[1];
		if (m_verbose)
			Log("[zone]    door " + std::to_string(e.id) + " -> " + std::to_string(e.value));
		events.push_back(e);
		break;
	}
	case ZoneOp::ZoneChange: {
		// IsZoneAvailable reads the answer's success: 1, or why not.
		int32_t zone = data.size() >= 76 ? Read<int32_t>(data, 64) : 0;
		int32_t success = data.size() >= 76 ? Read<int32_t>(data, 72) : 0;
		Log("[zone]    zone change to " + std::to_string(zone) + ": " + std::to_string(success));
		if (m_state != State::ZoneRequested)
			break;
		Event e(success == 1 ? Event::Type::ZoneApproved : Event::Type::ZoneDenied);
		if (success == 1) {
			// MoveToZone_2: OP_DeleteSpawn with our spawn id, then wait to be let go.
			Send(ZoneOp::DeleteSpawn, &m_my_id, sizeof(m_my_id), "OP_DeleteSpawn (leaving)");
			m_state = State::Departing;
			m_zone_state_ms = NowMs();
			e.value = static_cast<int32_t>(m_zone_target);
		}
		else {
			m_state = State::InZone;
			e.value = success;
		}
		events.push_back(e);
		break;
	}
	case ZoneOp::LogoutReply:
		if (m_state == State::Departing) {
			// The zone lets us go after OP_DeleteSpawn: on to world.
			m_state = State::LoggedOut;
			Log("[zone]    left for zone " + std::to_string(m_zone_target));
			events.push_back({Event::Type::Departed});
			if (m_session)
				m_session->Close();
			break;
		}
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

bool ZoneSession::NextReplayed(uint16_t &opcode, std::vector<uint8_t> &data)
{
	uint64_t elapsed = NowMs() - m_started_ms;
	const auto &packets = m_replay->packets;
	while (m_replay_next < packets.size()) {
		const LoggedPacket &p = packets[m_replay_next];
		if (p.ms > elapsed)
			return false;
		++m_replay_next;
		// What we sent then, and the zone letting that session go (it camped or zoned), are skipped.
		if (p.outgoing || p.opcode == ZoneOp::LogoutReply)
			continue;
		opcode = p.opcode;
		data = p.data;
		return true;
	}
	if (!m_replay_finished) {
		m_replay_finished = true;
		Log("[zone] replay finished; staying in the zone");
	}
	return false;
}

void ZoneSession::Pump(int timeout_ms, std::vector<Event> &events)
{
	if ((!m_session && !m_replay) || m_state == State::Idle || m_state == State::Failed || m_state == State::LoggedOut)
		return;

	for (;;) {
		AppPacket packet;
		RdpSession::Status status;
		if (m_replay)
			status = NextReplayed(packet.opcode, packet.data) ? RdpSession::Status::Packet : RdpSession::Status::Timeout;
		else
			status = m_session->Receive(packet, timeout_ms);
		timeout_ms = 0;
		if (status == RdpSession::Status::Timeout)
			break;
		if (status == RdpSession::Status::Closed) {
			if (m_state == State::Departing) {
				m_state = State::LoggedOut;
				events.push_back({Event::Type::Departed});
				return;
			}
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
		              packet.opcode == ZoneOp::HPUpdate || packet.opcode == ZoneOp::ManaChange ||
		              packet.opcode == ZoneOp::Animation;
		if (!chatty || m_verbose) {
			char header[96];
			snprintf(header, sizeof(header), "[zone] <- %s (0x%04x) %zu bytes", ZoneOpcodeName(packet.opcode), packet.opcode,
				packet.data.size());
			Log(header);
		}
		if (m_verbose)
			HexDump(packet.data);
		if (m_recorder)
			m_recorder->Write(false, packet.opcode, packet.data.data(), packet.data.size());
		RecordPacket(false, packet.opcode, packet.data.size());
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
	while (m_replay && !m_replay_later.empty() && NowMs() >= m_replay_later.front().due_ms) {
		m_replay_answers.emplace_back(m_replay_later.front().opcode, std::move(m_replay_later.front().body));
		m_replay_later.pop_front();
	}
	while (m_replay && !m_replay_answers.empty() && m_state == State::InZone) {
		auto answer = std::move(m_replay_answers.front());
		m_replay_answers.pop_front();
		HandlePacket(answer.first, answer.second, events);
	}
	if (m_replay && m_replay_target) {
		Event e(Event::Type::TargetChanged);
		e.id = m_replay_target;
		m_replay_target = 0;
		events.push_back(e);
	}
	if (m_replay && m_state == State::ZoneRequested) {
		// No zone to ask: "not ready".
		m_state = State::InZone;
		Event e(Event::Type::ZoneDenied);
		e.value = -1;
		events.push_back(e);
	}
	if (m_state == State::Entering && NowMs() - m_started_ms > HandshakeTimeoutMs)
		Fail(events, "timed out entering the zone");
	// The zone gives up on world after a while and answers "not ready"; this is in case it never answers.
	if (m_state == State::ZoneRequested && NowMs() - m_zone_state_ms > 60000) {
		m_state = State::InZone;
		Event e(Event::Type::ZoneDenied);
		e.value = -1;
		events.push_back(e);
	}
	if (m_state == State::Departing && NowMs() - m_zone_state_ms > 10000) {
		// No OP_LogoutReply; the zone drops us on its own timer anyway.
		m_state = State::LoggedOut;
		events.push_back({Event::Type::Departed});
		if (m_session)
			m_session->Close();
	}
	if (m_state == State::Camping && CampRemainingMs() == 0 && m_camp_started_ms != 0) {
		m_camp_started_ms = 0; // send once
		Send(ZoneOp::Logout, nullptr, 0, "OP_Logout");
		if (m_replay) { // no zone to answer
			m_state = State::LoggedOut;
			events.push_back({Event::Type::LoggedOut});
		}
	}
}
