#pragma once

#include "login_client.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class RdpSession;

// One slot of CharacterSelect_Struct (common/eq_packet_structs.h in EQMacEmu).
struct CharacterSlot
{
	std::string name;
	uint8_t level = 0;
	uint8_t class_id = 0;
	uint16_t race = 0;
	uint32_t zone = 0;
	uint8_t gender = 0;
	uint8_t face = 0;
	uint16_t deity = 0;
	uint32_t equip[9] = {}; // material per slot: head, chest, arms, wrists, hands, legs, feet, primary, secondary
	uint32_t tint[9] = {};  // 0xFFRRGGBB when tinted

	bool Empty() const { return name.empty() || name == "<none>"; }
};

// What character creation sends. The defaults are a legal Human Warrior of
// deity 201 starting in Qeynos (zone 1) on an EQMacEmu database.
struct NewCharacter
{
	std::string name;
	uint16_t race = 1;
	uint16_t class_id = 1;
	uint8_t gender = 0;
	uint16_t deity = 201;
	uint32_t start_zone = 1;
	int16_t str = 85, sta = 110, cha = 75, dex = 75, int_ = 75, agi = 80, wis = 75;
	uint8_t face = 0;
};

const char *ClassName(uint8_t class_id);
const char *RaceName(uint16_t race);

// One world server connection, driven by Pump(). Mirrors WorldAuthenticate and
// CEverQuest::DoCharacterSelection in eqgame.exe.
class WorldSession
{
public:
	enum class State { Idle, Authenticating, CharacterSelect, ApprovingName, Creating, EnteringWorld, ZoneHandoff, Failed };

	struct Event
	{
		enum class Type
		{
			Approved,       // OP_ApproveWorld: the session key was accepted
			Characters,     // Characters() changed (OP_SendCharInfo)
			ZoneServer,     // ZoneAddress()/ZonePort() are set; the world connection is closed
			ZoneUnavailable,
			CharacterCreated, // the new list follows as a Characters event
			CreateFailed,     // text says why; the session stays at character select
			Error,          // the session is over
		};
		Type type;
		std::string text;
	};

	WorldSession();
	~WorldSession();

	void SetLogger(std::function<void(const std::string &)> logger) { m_log = std::move(logger); }
	void SetVerbose(bool verbose) { m_verbose = verbose; }

	bool Begin(const std::string &host, uint16_t port, const std::string &session_name, const std::string &key);
	void Pump(int timeout_ms, std::vector<Event> &events);
	bool EnterWorld(const std::string &character);
	bool DeleteCharacter(const std::string &character);
	// OP_ApproveName, then OP_CharacterCreate once the name is approved.
	bool CreateCharacter(const NewCharacter &character);
	void Logout(); // OP_WorldLogout, then close
	void Close();

	State GetState() const { return m_state; }
	const std::vector<CharacterSlot> &Characters() const { return m_characters; }
	uint32_t Expansions() const { return m_expansions; }
	const std::string &WorldShortName() const { return m_world_short_name; }
	const std::string &Motd() const { return m_motd; }
	const std::string &ZoneAddress() const { return m_zone_address; }
	uint16_t ZonePort() const { return m_zone_port; }
	const std::string &EnteringCharacter() const { return m_entering; }

private:
	void Log(const std::string &line);
	void Fail(std::vector<Event> &events, const std::string &text);
	bool Send(uint16_t opcode, const void *body, size_t size, const std::string &what);

	std::unique_ptr<RdpSession> m_session;
	State m_state = State::Idle;
	bool m_approved = false;
	std::vector<CharacterSlot> m_characters;
	uint32_t m_expansions = 0;
	std::string m_world_short_name;
	std::string m_motd;
	std::string m_entering;
	NewCharacter m_new_character;
	std::string m_zone_address;
	uint16_t m_zone_port = 0;
	uint64_t m_wait_started_ms = 0;
	std::function<void(const std::string &)> m_log;
	bool m_verbose = false;
};

struct WorldOptions
{
	std::string host;           // defaults to the address from the server list
	uint16_t port = 9000;       // the client has no port in the server list; world listens on 9000
	std::string character;      // if set, send OP_EnterWorld for this character
	std::string create;         // if set, first create this character (a Human Warrior)
	int idle_seconds = 0;       // stay at character select this long when not entering world
	bool verbose = false;
	int timeout_ms = 10000;
};

struct ZoneHandoff
{
	std::string character;
	std::string address;
	uint16_t port = 0;
};

// Blocking helper for the console client.
bool RunWorld(const LoginResult &login, const WorldOptions &options, ZoneHandoff &handoff);
