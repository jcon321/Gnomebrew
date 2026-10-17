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

	std::string DisplayName() const; // "a rat", like the client's CleanName
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

private:
	void Log(const std::string &line);
	void Fail(std::vector<Event> &events, const std::string &text);
	bool Send(uint16_t opcode, const void *body, size_t size, const std::string &what, bool reliable = true);
	void SendPosition();
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
