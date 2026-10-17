#pragma once

// One zone server connection, driven by Pump(). The handshake follows eqgame.exe:
//   ZoneAuthenticate (@0054c80d)       connect, OP_DataRate (float), then
//   GetZoneInfoFromNetwork (@0053d026) OP_ZoneEntry {crc32(name), name[64]}
//                                      <- OP_PlayerProfile (deflated + encrypted), OP_ZoneEntry, OP_ZoneSpawns...
//   CEverQuest_EnterZone (@0053d2c4)   OP_ReqNewZone (name)          <- OP_NewZone
//   CEverQuest::DoMainLoop (@0053df6e) OP_ReqClientSpawn             <- doors, objects, OP_SendExpZonein
//                                      OP_SendExpZonein              <- OP_SpawnAppearance (our id), OP_SendExpZonein
//                                      OP_ClientUpdate (our position) -> the server completes the connect
// Struct layouts are from EQMacEmu's common/patches/mac_structs.h and common/eq_packet_structs.h.

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class EqStr;
class RdpSession;

struct ZoneProfile
{
	std::string name;
	std::string last_name;
	uint8_t level = 0;
	uint16_t race = 0;
	uint16_t class_id = 0;
	uint8_t gender = 0;
	uint16_t deity = 0;
	int16_t cur_hp = 0;
	int16_t mana = 0;
	uint32_t exp = 0;
	float x = 0, y = 0, z = 0, heading = 0;
	uint32_t zone_id = 0;
	int32_t platinum = 0, gold = 0, silver = 0, copper = 0;
};

// OP_NewZone (NewZone_Struct, 572 bytes), with the fallbacks CEverQuest::HandleWorldMessage
// applies when it copies the header into instEQZoneInfo.
struct ZoneInfo
{
	std::string short_name;
	std::string long_name;
	uint8_t zone_type = 0;
	uint32_t fog_color = 0xc0c0c0; // 0x00RRGGBB
	float fog_start = 500, fog_end = 600;
	float min_clip = 50, max_clip = 2400;
	uint8_t sky = 0;                 // sky.s3d type; 0 = no sky
	uint8_t time_type = 0;           // 1, 2 and 5 follow the time of day (outdoors)
	float gravity = 0.4f;            // per 50 ms tick, squared
	float safe_x = 0, safe_y = 0, safe_z = 0;
};

struct ZoneSpawn
{
	uint16_t id = 0;
	std::string name;        // raw spawn name, e.g. "a_rat003"
	std::string surname;
	uint8_t level = 0;
	uint8_t class_id = 0;
	uint16_t race = 0;
	uint8_t gender = 0;
	uint8_t npc = 0;         // 0 player, 1 NPC, 2 player corpse, 3 NPC corpse
	uint16_t pet_owner = 0;
	int16_t hp = 0;          // percent for everything but ourselves
	float x = 0, y = 0, z = 0;
	float heading = 0;       // 0..512, counterclockwise from north (spawn packets send 256ths)
	uint8_t gm = 0;
	uint8_t anon = 0;
	uint8_t afk = 0;
	float size = 0;          // model height; 0 = the race's default
	uint8_t face = 0;
	uint16_t equipment[9] = {}; // material per slot: head, chest, arms, wrists, hands, legs, feet, primary, secondary
	uint32_t colors[9] = {};    // 0xFFRRGGBB when tinted
	float walk_speed = 0.46f, run_speed = 0.7f; // EQPlayer's MovementBackward/ForwardSpeedMultiplier
	// Motion from the last update (unpack_physics): per 50 ms tick, for the client to carry on with.
	float dx = 0, dy = 0, dz = 0;
	float speed = 0;         // MovementSpeed (the update's animation byte / 10)
	float heading_speed = 0; // per tick
	uint8_t stance = 100;    // SpawnAppearance Animation: 100 standing, 110 sitting, 111 crouching, 115 lying
	uint64_t updated_ms = 0; // when the position came in

	std::string DisplayName() const; // "a rat", like the client's CleanName
};

// Our position and motion as pack_physics puts them in OP_ClientUpdate: the server's
// coordinates (z is the model's center), heading 0..512, and the speeds per 50 ms tick.
struct ZoneMovement
{
	float y = 0, x = 0, z = 0, heading = 0;
	float speed = 0;         // MovementSpeed: forward (+) or back (-), sent as the animation byte
	float heading_speed = 0; // MovementSpeedHeading
	float dy = 0, dx = 0, dz = 0;
};

struct ZoneChat
{
	uint16_t channel = 0;    // ChannelMessage_Struct chan_num: 0 guild, 2 group, 3 shout, 4 auction, 5 ooc, 7 tell, 8 say...
	std::string sender;
	std::string target;
	std::string text;
};

class ZoneSession
{
public:
	enum class State { Idle, Entering, InZone, Camping, LoggedOut, Failed };

	struct Event
	{
		enum class Type
		{
			Profile,      // Profile() is set
			ZoneInfo,     // Info() is set (OP_NewZone)
			InZone,       // handshake complete
			SpawnAdded,   // id
			SpawnRemoved, // id
			SpawnsMoved,
			SpawnChanged, // id: stance, corpse...
			Chat,         // chat
			Message,      // text: system/special message
			TimeOfDay,    // text: "hour:minute"
			LoggedOut,    // camp finished; the zone closes the connection
			Error,        // the session is over
		};
		Event() = default;
		Event(Type t) : type(t) {} // implicit, so {Type::X} works

		Type type = Type::Message;
		uint16_t id = 0;
		std::string text;
		ZoneChat chat;
	};

	ZoneSession();
	~ZoneSession();

	void SetLogger(std::function<void(const std::string &)> logger) { m_log = std::move(logger); }
	void SetVerbose(bool verbose) { m_verbose = verbose; }
	// eqstr_en.txt, for OP_FormattedMessage. Optional; without it the ids are shown raw.
	void SetStringTable(const EqStr *strings) { m_eqstr = strings; }

	bool Begin(const std::string &host, uint16_t port, const std::string &character);
	void Pump(int timeout_ms, std::vector<Event> &events);

	bool Say(const std::string &text);
	// Our movement, every frame once we're in. Pump sends OP_ClientUpdate the way DoMainLoop
	// does: when the heading moved 2 or the speed 0.2 since the last flag, and every second
	// (DoPassageOfTime), at most every 150 ms, and not more than 6 identical updates in a row.
	void SetMovement(const ZoneMovement &movement);
	void Jump(); // OP_Jump
	// /camp: OP_Camp, then OP_Logout once the 30 second camp timer has run (Handle_OP_Logout expects that).
	bool Camp();
	void Close();

	State GetState() const { return m_state; }
	const ZoneProfile &Profile() const { return m_profile; }
	const ZoneInfo &Info() const { return m_info; }
	const std::map<uint16_t, ZoneSpawn> &Spawns() const { return m_spawns; }
	// Our own spawn from OP_ZoneEntry (ServerZoneEntry_Struct): appearance, size, position.
	// Set before the ZoneInfo event.
	const ZoneSpawn &Self() const { return m_self; }
	uint16_t MySpawnId() const { return m_my_id; }
	uint64_t CampRemainingMs() const;
	// Norrath's time: OP_TimeOfDay's hour (1..24) and minute, carried on at 20 game seconds a
	// second as EQWorldData::AdvanceTime does. False until the zone has sent it.
	bool GameTime(int &hour, float &minute) const;

private:
	void Log(const std::string &line);
	void Fail(std::vector<Event> &events, const std::string &text);
	bool Send(uint16_t opcode, const void *body, size_t size, const std::string &what, bool reliable = true);
	void SendPosition();
	void SendMovement(uint64_t now_ms);
	void HandlePacket(uint16_t opcode, std::vector<uint8_t> &data, std::vector<Event> &events);

	std::unique_ptr<RdpSession> m_session;
	State m_state = State::Idle;
	std::string m_character;
	ZoneProfile m_profile;
	ZoneInfo m_info;
	std::map<uint16_t, ZoneSpawn> m_spawns;
	ZoneSpawn m_self;
	uint16_t m_my_id = 0;
	bool m_have_profile = false;
	bool m_have_zone_entry = false;
	bool m_sent_req_new_zone = false;
	bool m_sent_exp_zonein = false;
	uint64_t m_started_ms = 0;
	uint64_t m_camp_started_ms = 0;
	int m_time_hour = 0, m_time_minute = 0;
	uint64_t m_time_received_ms = 0;
	// OP_ClientUpdate pacing (DoMainLoop / ProcessControls)
	ZoneMovement m_movement;
	bool m_have_movement = false;
	bool m_movement_dirty = false;
	float m_flagged_heading = 0, m_flagged_speed = 0;
	uint64_t m_last_update_ms = 0, m_last_tick_ms = 0;
	uint8_t m_last_update[13] = {};
	int m_identical_updates = 0;
	std::function<void(const std::string &)> m_log;
	bool m_verbose = false;
	const EqStr *m_eqstr = nullptr;
};

namespace ZoneCrypto
{
	// Inverses of EncryptProfilePacket / EncryptZoneSpawnPacket in EQMacEmu common/packet_functions.cpp.
	void DecryptProfile(uint8_t *data, size_t size);
	void DecryptZoneSpawns(uint8_t *data, size_t size);
	// Decrypt then inflate. Returns false if the result isn't a zlib stream.
	bool Unpack(std::vector<uint8_t> &data, bool profile);
}
