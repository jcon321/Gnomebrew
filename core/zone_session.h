#pragma once

// One zone server connection, driven by Pump(). The handshake follows eqgame.exe:
//   ZoneAuthenticate (@0054c80d)       connect, OP_DataRate (float), then
//   GetZoneInfoFromNetwork (@0053d026) OP_ZoneEntry {crc32(name), name[64]}
//                                      <- OP_PlayerProfile (deflated + encrypted), OP_ZoneEntry, OP_ZoneSpawns...
//   CEverQuest_EnterZone (@0053d2c4)   OP_ReqNewZone (name)          <- OP_NewZone
//   CEverQuest::DoMainLoop (@0053df6e) OP_ReqClientSpawn             <- doors, objects, OP_SendExpZonein
//                                      OP_SendExpZonein              <- OP_SpawnAppearance (our id), OP_SendExpZonein
//                                      OP_ClientUpdate (our position) -> the server completes the connect
//   CEverQuest::MoveToZone_2 (@005460b5) leaving through a zone line: OP_ZoneChange {name, zone,
//                                      reason 0} and wait for its answer (IsZoneAvailable); on
//                                      success OP_DeleteSpawn (our id), and the zone answers
//                                      OP_LogoutReply and drops us. Then world again, zoning.
// Struct layouts are from EQMacEmu's common/patches/mac_structs.h and common/eq_packet_structs.h.

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class EqStr;
class PacketLogWriter;
class RdpSession;
struct PacketLog;
struct RdpStats;

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
	int16_t str = 0, sta = 0, cha = 0, dex = 0, int_ = 0, agi = 0, wis = 0; // base stats, without items or spells
	int8_t fatigue = 0; // 0 rested .. 100 exhausted (OP_Stamina keeps it up to date)
	uint8_t languages[32] = {}; // skill in each language, 0..100
	float x = 0, y = 0, z = 0, heading = 0;
	uint32_t zone_id = 0;
	int32_t platinum = 0, gold = 0, silver = 0, copper = 0;
	// By skill id (50 is swimming): 255 the class never has it, 254 it does at a later level,
	// 0 it has it untrained.
	int16_t skills[100] = {};
};

// One entry of OP_SendZonepoints: where a zone line with teleport index `index` goes.
struct ZonePoint
{
	uint32_t index = 0;
	float y = 0, x = 0, z = 0, heading = 0; // the server's coordinates; 999999 (or heading 513) keeps ours
	uint16_t zone = 0;
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
	uint8_t anon = 0;        // 1 anonymous, 2 roleplaying
	uint8_t afk = 0;
	uint8_t linkdead = 0;
	uint8_t lfg = 0;         // OP_LFGCommand only; the spawn packets don't carry it
	uint8_t pvp = 0;
	uint8_t invis = 0;
	int16_t guild_id = -1;   // -1: none
	uint8_t aa_title = 0;    // the AA title rank, 1..3 (CEverQuest_GetTitleDesc)
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
	// EQPlayer::Texture: 0xFF dresses each slot from `equipment` (players); anything else is an NPC's
	// body texture for every part, with `helm` choosing the head (CDisplay::SwapNPCMaterials).
	uint8_t body_texture = 0xff;
	uint8_t helm = 0;
	uint8_t fly_mode = 0;    // LevitationState: 0 on the ground, 1 flying, 2 levitating, 3 floats in water
	uint8_t light = 0;       // LightType, the light it carries (2 a torch, 4 a lantern...); 0 none
	int16_t body_type = 0;
	uint32_t look_serial = 0; // counts OP_WearChanges: the look changed
	// The last OP_Animation (attacks, casting, emotes): a client animation id, played once.
	uint32_t action_serial = 0;
	int32_t action = 0;
	float action_speed = 1;

	std::string DisplayName() const; // "a rat", like the client's CleanName
};

// The guilds' names by id: OP_GuildsList (GuildsList_Struct: 4 bytes, then 96-byte entries
// {u32 id, char name[64], ..., u16 exists @72}), from the world as we enter it and from the zone
// when they change.
using GuildNames = std::map<int16_t, std::string>;
GuildNames ParseGuildList(const std::vector<uint8_t> &data);

// A door or other switch from OP_SpawnDoor (Door_Struct, 44 bytes), as
// EQSwitch_DoorLoadingCode reads it.
struct ZoneDoor
{
	uint8_t id = 0;
	std::string name;        // the model: "<name>_ACTORDEF" in the zone's _obj.s3d
	float y = 0, x = 0, z = 0; // the server's coordinates
	float heading = 0;       // 0..512
	int16_t incline = 0;     // 0..512, a tilt (trapdoors, drawbridges)
	int16_t size = 100;      // percent
	uint8_t type = 0;        // the open type: how it moves (EQSwitch_UpdateDoorStates)
	uint8_t open = 0;        // doorIsOpen
	uint8_t inverted = 0;    // open is its resting state
	int16_t param = 0;       // how far lifts move, a teleporter's destination...
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

// An item we have, from OP_CharInventory or OP_ItemPacket (Mac::structs::Item_Struct, 360 bytes):
// what the windows need of it so far.
// Mac::structs::Item_Struct (360 bytes), as eqgame keeps it in memory; the fields from its union
// only for the kind of item they belong to.
struct ZoneItem
{
	int16_t slot = 0;        // equipSlot: 0 the cursor, 1..21 worn (21 ammo), 22..29 general, 250..329 in their bags
	uint16_t id = 0;         // the item's number: two of the same stack
	uint8_t item_class = 0;  // 0 common, 1 container, 2 book
	std::string name;
	std::string lore;        // its first character marks it: '*' lore, '~' pending lore; then '#' artifact, '&' summoned
	uint8_t weight = 0, no_rent = 0, no_drop = 0, size = 0; // no_rent, no_drop: 0 means it is
	uint16_t icon = 0;
	int32_t slots = 0;       // where it can be worn, a bit per slot
	int32_t cast_time = 0;   // ms; a food or drink's duration
	uint16_t skill_mod_type = 0;
	int16_t skill_mod_value = 0;
	int16_t bane_race = 0, bane_body = 0;
	uint8_t bane_amount = 0;
	uint8_t rec_level = 0, rec_skill = 0;
	uint8_t elem_type = 0, elem_amount = 0; // 1 magic, 2 fire, 3 cold, 4 poison, 5 disease
	int16_t faction_mod[4] = {}, faction_amount[4] = {};
	int32_t deity = 0;       // a bit per deity, from bit 1 (201)
	int16_t req_level = 0;
	int16_t bard_type = 0;   // an instrument's
	int16_t focus_effect = 0;
	// Common items:
	int16_t hp = 0, mana = 0, ac = 0;
	int8_t str = 0, sta = 0, cha = 0, dex = 0, int_ = 0, agi = 0, wis = 0;
	int8_t mr = 0, fr = 0, cr = 0, dr = 0, pr = 0;
	uint8_t max_charges = 0, delay = 0, damage = 0, range = 0, item_type = 0, magic = 0;
	uint8_t light = 0;
	uint8_t effect_level = 0; // EffectLevel1: a clicky's casting level, which we must have from 51
	int8_t effect_type = 0;  // EffectType1: 0 combat, 1 click, 2 worn, 3 expendable, 4 must equip, 5 click, class checked
	int16_t effect1 = 0;     // Effect1: a scroll's spell
	uint8_t stackable = 0;   // IsStackable, @276
	int8_t charges = 0;
	int16_t effect2 = 0;     // Effect2: the spell of its effect
	int32_t classes = 0, races = 0; // a bit per class (WAR first) and per race (HUM first)
	// Containers:
	uint8_t bag_type = 0;    // 2 a quiver, 8 a bandolier...
	uint8_t bag_slots = 0, bag_open = 0, bag_size = 0, bag_wr = 0; // bag_wr: % weight reduction
	// Books (the first two bytes of the union): book or note, and open or not.
	uint8_t book_type = 0, book_open = 0;
};

struct ZoneChat
{
	uint16_t channel = 0;    // ChannelMessage_Struct chan_num: 0 guild, 2 group, 3 shout, 4 auction, 5 ooc, 7 tell, 8 say...
	uint16_t language = 0;
	std::string sender;
	std::string target;
	std::string text;
};

class ZoneSession
{
public:
	// ZoneRequested: OP_ZoneChange is out. Departing: approved, waiting for the zone to let us go.
	enum class State { Idle, Entering, InZone, ZoneRequested, Departing, Camping, LoggedOut, Failed };

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
			Message,      // text: system/special message; value: its color (a chat color type, Chat::*)
			Emote,        // text: "Name waves." (OP_Emote)
			TargetChanged,// id: the zone set our target (OP_TargetCommand's answer); 0 none
			TimeOfDay,    // text: "hour:minute"
			Weather,      // OP_Weather: id the type (0 rain, 1 snow), value the intensity (0 stops it)
			SkillUp,      // OP_SkillUpdate: id the skill, value its new value (Profile() has it already)
			DoorsSpawned, // Doors() is set
			DoorMoved,    // id: the door, value: its new state (OP_MoveDoor)
			LoggedOut,    // camp finished; the zone closes the connection
			ZoneApproved, // value: the zone we're going to; OP_DeleteSpawn is sent
			ZoneDenied,   // value: the answer (ZoningMessage: 0 down, -1 not ready, -7 not experienced...)
			Departed,     // the zone let us go after ZoneApproved: on to world, zoning
			Error,        // the session is over
		};
		Event() = default;
		Event(Type t) : type(t) {} // implicit, so {Type::X} works

		Type type = Type::Message;
		uint16_t id = 0;
		int32_t value = 0;
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
	// Every zone session from Begin on is written to a packet log (.eqr) in `dir`; empty: none.
	void SetRecordDir(const std::string &dir) { m_record_dir = dir; }
	const std::string &RecordingPath() const; // the current session's log, or empty
	// Plays a packet log back instead of connecting: its incoming packets arrive when they did,
	// and what we send goes nowhere. When it runs out we stay in the zone. A camp logs out when
	// its timer runs; a zone line is turned down ("not ready"); a /target is taken.
	bool BeginReplay(const std::string &path, std::string &error);
	bool Replaying() const { return m_replay != nullptr; }
	void Pump(int timeout_ms, std::vector<Event> &events);

	bool Say(const std::string &text) { return SendChannel(8, "", text); }
	// CEverQuest::SendNewText: OP_ChannelMessage on a channel (0 guild, 2 group, 3 shout, 4 auction,
	// 5 ooc, 7 tell, 8 say), to `target` (a tell's recipient; for a say, the NPC we're talking to).
	bool SendChannel(uint16_t channel, const std::string &target, const std::string &text);
	// CEverQuest::Emote: OP_Emote; the zone puts our name in front, so it starts with a space.
	bool Emote(const std::string &text);
	// Our target: OP_TargetMouse (clicked) or OP_TargetCommand (/target), 0 for none. The zone
	// sends the target's HP and, for /target, which target it set (TargetChanged).
	void SetTarget(uint16_t id, bool command = false);
	// Our movement, every frame once we're in. Pump sends OP_ClientUpdate the way DoMainLoop
	// does: when the heading moved 2 or the speed 0.2 since the last flag, and every second
	// (DoPassageOfTime), at most every 150 ms, and not more than 6 identical updates in a row.
	void SetMovement(const ZoneMovement &movement);
	void Jump(); // OP_Jump
	// ChangeStance: OP_SpawnAppearance with our stance (100 standing, 111 crouching...).
	void SetStance(uint8_t stance);
	// /camp: OP_Camp, then OP_Logout once the 30 second camp timer has run (Handle_OP_Logout expects that).
	bool Camp();
	// Ask to leave for another zone (a zone line): our position, then OP_ZoneChange. The answer
	// comes as ZoneApproved (then Departed) or ZoneDenied.
	bool RequestZoneChange(uint32_t zone_id);
	// OP_SendZonepoints: the zone lines' teleport table.
	const std::vector<ZonePoint> &ZonePoints() const { return m_zone_points; }
	// OP_SpawnDoor's doors and switches.
	const std::vector<ZoneDoor> &Doors() const { return m_doors; }
	// EQSwitch::UseSwitch: we used a door, holding `item` (0xffff none) with `skill` (disarm traps).
	// The zone answers everyone with OP_MoveDoor when it opens.
	bool ClickDoor(uint8_t door, uint16_t item, int16_t skill);
	void Close();

	State GetState() const { return m_state; }
	const ZoneProfile &Profile() const { return m_profile; }
	const ZoneInfo &Info() const { return m_info; }
	const std::map<uint16_t, ZoneSpawn> &Spawns() const { return m_spawns; }
	// Our own spawn from OP_ZoneEntry (ServerZoneEntry_Struct): appearance, size, position.
	// Set before the ZoneInfo event.
	const ZoneSpawn &Self() const { return m_self; }
	uint16_t MySpawnId() const { return m_my_id; }
	// Our hit points as OP_HPUpdate gives them: `hp` leaves out what worn items add (the client
	// adds that), `max_hp` is the total. Both 0 until the zone has sent them.
	int32_t BaseHp() const { return m_hp; }
	int32_t MaxHp() const { return m_max_hp; }
	// The items we have (OP_CharInventory, then each OP_ItemPacket), by slot.
	const std::map<int16_t, ZoneItem> &Items() const { return m_items; }
	// The items to change as eqgame changes its own before it tells the zone (an item picked up,
	// put down or split, core/inventory.h): the zone doesn't answer a move it agrees with.
	std::map<int16_t, ZoneItem> &EditItems() { return m_items; }
	// OP_MoveItem: we moved `qty` of a stack (0: the whole item) from one slot to another (0 the cursor).
	bool MoveItem(int16_t from, int16_t to, int32_t qty);
	// The guild names, from the world (SetGuilds, before Begin) and then the zone.
	void SetGuilds(const GuildNames &guilds) { m_guilds = guilds; }
	const GuildNames &Guilds() const { return m_guilds; }
	uint64_t CampRemainingMs() const;
	// Norrath's time: OP_TimeOfDay's hour (1..24) and minute, carried on at 20 game seconds a
	// second as EQWorldData::AdvanceTime does. False until the zone has sent it.
	bool GameTime(int &hour, float &minute) const;
	// The zone connection's round trip and traffic; false when there's no connection.
	bool NetworkStats(RdpStats &out) const;

	// For the debug menu. The packets sent and received lately, newest last; a run of the same
	// opcode the same way is one record with a count.
	struct PacketRecord
	{
		uint64_t ms = 0; // NowMs() of the latest
		bool outgoing = false;
		uint16_t opcode = 0;
		size_t size = 0; // of the latest
		uint32_t count = 1;
	};
	const std::deque<PacketRecord> &RecentPackets() const { return m_recent_packets; }
	// Our position as the server last heard it: the latest OP_ClientUpdate, decoded the way the
	// zone reads it (whole units across, tenths up, heading in 256ths).
	struct SentPosition
	{
		uint64_t ms = 0; // 0: none yet
		float y = 0, x = 0, z = 0, heading = 0;
		uint32_t count = 0; // updates sent this zone
	};
	const SentPosition &LastSent() const { return m_last_sent; }
	static uint64_t Now(); // the clock behind PacketRecord::ms, SentPosition::ms and ZoneSpawn::updated_ms

private:
	void Reset(const std::string &character);
	void Log(const std::string &line);
	void Fail(std::vector<Event> &events, const std::string &text);
	bool Send(uint16_t opcode, const void *body, size_t size, const std::string &what, bool reliable = true);
	void SendPosition();
	bool NextReplayed(uint16_t &opcode, std::vector<uint8_t> &data); // the next recorded packet that's due
	void SendMovement(uint64_t now_ms);
	void HandlePacket(uint16_t opcode, std::vector<uint8_t> &data, std::vector<Event> &events);
	void RecordPacket(bool outgoing, uint16_t opcode, size_t size);
	void RecordSent(const uint8_t *update); // a 15-byte SpawnPositionUpdate_Struct

	std::unique_ptr<RdpSession> m_session;
	std::string m_record_dir;
	std::unique_ptr<PacketLogWriter> m_recorder;
	std::unique_ptr<PacketLog> m_replay;
	size_t m_replay_next = 0;
	bool m_replay_finished = false;
	uint16_t m_replay_target = 0; // a /target to answer
	State m_state = State::Idle;
	std::string m_character;
	ZoneProfile m_profile;
	ZoneInfo m_info;
	std::map<uint16_t, ZoneSpawn> m_spawns;
	ZoneSpawn m_self;
	uint16_t m_my_id = 0;
	int32_t m_hp = 0, m_max_hp = 0;
	std::map<int16_t, ZoneItem> m_items;
	GuildNames m_guilds;
	bool m_have_profile = false;
	bool m_have_zone_entry = false;
	bool m_sent_req_new_zone = false;
	bool m_sent_exp_zonein = false;
	uint64_t m_started_ms = 0;
	uint64_t m_camp_started_ms = 0;
	std::vector<ZonePoint> m_zone_points;
	std::vector<ZoneDoor> m_doors;
	uint32_t m_zone_target = 0;
	uint64_t m_zone_state_ms = 0; // when ZoneRequested or Departing began
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
	std::deque<PacketRecord> m_recent_packets;
	SentPosition m_last_sent;
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
