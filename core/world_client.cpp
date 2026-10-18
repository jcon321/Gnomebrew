#include "world_client.h"
#include "opcodes.h"
#include "rdp_session.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace
{
	constexpr uint64_t ResponseTimeoutMs = 15000;

	uint64_t NowMs()
	{
		return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
	}

	std::string CString(const std::vector<uint8_t> &data, size_t offset = 0, size_t max = SIZE_MAX)
	{
		std::string s;
		for (size_t i = offset; i < data.size() && s.size() < max && data[i]; ++i)
			s.push_back(static_cast<char>(data[i]));
		return s;
	}

	template <typename T>
	T Read(const std::vector<uint8_t> &data, size_t offset)
	{
		T value{};
		if (offset + sizeof(T) <= data.size())
			memcpy(&value, &data[offset], sizeof(T));
		return value;
	}

	// WorldAuthenticate builds this with ConcatenateStringsWithSeparators(name, password)
	// into a 188 byte buffer, then a uint32 0 @188, the zoning flag @192 (DAT_00799776, which
	// MoveToZone_2 sets) and 0xc3 @196: 200 bytes. The server's Mac LoginInfo_Struct reads @192.
	std::vector<uint8_t> BuildLoginInfo(const std::string &name, const std::string &password, bool zoning)
	{
		std::vector<uint8_t> out(200, 0);
		size_t pos = 0;
		memcpy(&out[pos], name.c_str(), name.size());
		pos += name.size() + 1;
		memcpy(&out[pos], password.c_str(), password.size());
		out[192] = zoning ? 1 : 0;
		out[196] = 0xc3;
		return out;
	}

	// CharacterSelect_Struct: name[10][64] @0, level[10] @640, class[10] @650, race u16[10] @660,
	// zone u32[10] @680, gender[10] @720, face[10] @730, equip u32[10][9] @740, tints u32[10][9] @1100,
	// deity u16[10] @1460. 1620 bytes.
	std::vector<CharacterSlot> ParseCharacterSelect(const std::vector<uint8_t> &data)
	{
		std::vector<CharacterSlot> slots;
		if (data.size() < 1620)
			return slots;
		for (int i = 0; i < 10; ++i) {
			CharacterSlot slot;
			slot.name = CString(data, i * 64, 64);
			slot.level = data[640 + i];
			slot.class_id = data[650 + i];
			slot.race = Read<uint16_t>(data, 660 + i * 2);
			slot.zone = Read<uint32_t>(data, 680 + i * 4);
			slot.gender = data[720 + i];
			slot.face = data[730 + i];
			slot.deity = Read<uint16_t>(data, 1460 + i * 2);
			for (int k = 0; k < 9; ++k) {
				slot.equip[k] = Read<uint32_t>(data, 740 + i * 36 + k * 4);
				slot.tint[k] = Read<uint32_t>(data, 1100 + i * 36 + k * 4);
			}
			slots.push_back(slot);
		}
		return slots;
	}
}

const char *ClassName(uint8_t class_id)
{
	static const char *names[] = {"?", "Warrior", "Cleric", "Paladin", "Ranger", "Shadow Knight", "Druid", "Monk", "Bard",
		"Rogue", "Shaman", "Necromancer", "Wizard", "Magician", "Enchanter", "Beastlord"};
	return class_id < sizeof(names) / sizeof(names[0]) ? names[class_id] : "?";
}

const char *RaceName(uint16_t race)
{
	switch (race) {
	case 1: return "Human";
	case 2: return "Barbarian";
	case 3: return "Erudite";
	case 4: return "Wood Elf";
	case 5: return "High Elf";
	case 6: return "Dark Elf";
	case 7: return "Half Elf";
	case 8: return "Dwarf";
	case 9: return "Troll";
	case 10: return "Ogre";
	case 11: return "Halfling";
	case 12: return "Gnome";
	case 128: return "Iksar";
	case 130: return "Vah Shir";
	default: return "?";
	}
}

WorldSession::WorldSession() = default;
WorldSession::~WorldSession() = default;

void WorldSession::Log(const std::string &line)
{
	if (m_log)
		m_log(line);
}

void WorldSession::Fail(std::vector<Event> &events, const std::string &text)
{
	Log("[world] " + text);
	m_state = State::Failed;
	if (m_session)
		m_session->Close();
	events.push_back({Event::Type::Error, text});
}

bool WorldSession::Send(uint16_t opcode, const void *body, size_t size, const std::string &what)
{
	Log("[world] -> " + what);
	return m_session && m_session->Send(opcode, body, size);
}

bool WorldSession::Begin(const std::string &host, uint16_t port, const std::string &session_name, const std::string &key)
{
	Close();
	m_zoning = false;
	m_approved = false;
	m_characters.clear();
	m_zone_address.clear();
	m_zone_port = 0;
	m_entering.clear();

	m_session = std::make_unique<RdpSession>();
	if (!m_session->Connect(host, port)) {
		m_state = State::Failed;
		return false;
	}
	m_state = State::Authenticating;
	m_wait_started_ms = NowMs();
	auto info = BuildLoginInfo(session_name, key, false);
	return Send(WorldOp::SendLoginInfo, info.data(), info.size(), "OP_SendLoginInfo (" + session_name + " / " + key + ")");
}

bool WorldSession::BeginZoning(const std::string &host, uint16_t port, const std::string &session_name,
	const std::string &key, const std::string &character)
{
	Close();
	m_zoning = true;
	m_approved = false;
	m_characters.clear();
	m_zone_address.clear();
	m_zone_port = 0;
	m_entering = character;

	m_session = std::make_unique<RdpSession>();
	if (!m_session->Connect(host, port)) {
		m_state = State::Failed;
		return false;
	}
	m_state = State::Authenticating;
	m_wait_started_ms = NowMs();
	auto info = BuildLoginInfo(session_name, key, true);
	return Send(WorldOp::SendLoginInfo, info.data(), info.size(), "OP_SendLoginInfo (zoning, " + session_name + ")");
}

bool WorldSession::EnterWorld(const std::string &character)
{
	if (m_state != State::CharacterSelect)
		return false;
	m_entering = character;
	m_state = State::EnteringWorld;
	m_wait_started_ms = NowMs();
	// DoCharacterSelection: send_message(0x8001, name, strlen(name) + 1)
	return Send(WorldOp::EnterWorld, character.c_str(), character.size() + 1, "OP_EnterWorld " + character);
}

bool WorldSession::DeleteCharacter(const std::string &character)
{
	if (m_state != State::CharacterSelect)
		return false;
	// CCharacterSelect::DeleteCharacter: send_message(0x405a, name, strlen(name) + 1). The server answers with a new list.
	return Send(WorldOp::DeleteCharacter, character.c_str(), character.size() + 1, "OP_DeleteCharacter " + character);
}

bool WorldSession::CreateCharacter(const NewCharacter &character)
{
	if (m_state != State::CharacterSelect || character.name.empty() || character.name.size() > 63)
		return false;
	m_new_character = character;
	m_state = State::ApprovingName;
	m_wait_started_ms = NowMs();

	// CCharacterCreation::SubmitNameToWorld (@0040c84f) sends 76 bytes: name[64], race u32 @64,
	// class u32 @68, deity i32 @72. (World checks Size(), which counts the 2-byte opcode, against
	// the 78-byte NameApproval_Struct, so 76 is also the only size it accepts.)
	std::vector<uint8_t> body(76, 0);
	uint32_t race = character.race, class_id = character.class_id;
	int32_t deity = character.deity;
	memcpy(body.data(), character.name.c_str(), character.name.size());
	memcpy(&body[64], &race, 4);
	memcpy(&body[68], &class_id, 4);
	memcpy(&body[72], &deity, 4);
	return Send(WorldOp::ApproveName, body.data(), body.size(), "OP_ApproveName " + character.name);
}

namespace
{
	// CharCreate_Struct, 8452 bytes (0x2104, as FinalizeNewPCAndSendToWorld @0040c966 sends it); offsets from
	// EQMacEmu common/eq_packet_structs.h, sent unchanged to Mac clients.
	std::vector<uint8_t> BuildCharCreate(const NewCharacter &c)
	{
		std::vector<uint8_t> out(8452, 0);
		auto put = [&out](size_t offset, const void *value, size_t size) { memcpy(&out[offset], value, size); };
		memcpy(&out[2], c.name.c_str(), std::min<size_t>(c.name.size(), 63));
		put(136, &c.gender, 1);
		put(138, &c.race, 2);
		put(140, &c.class_id, 2);
		uint8_t level = 1;
		put(144, &level, 1);
		put(160, &c.str, 2);
		put(162, &c.sta, 2);
		put(164, &c.cha, 2);
		put(166, &c.dex, 2);
		put(168, &c.int_, 2);
		put(170, &c.agi, 2);
		put(172, &c.wis, 2);
		put(174, &c.face, 1);       // oldface
		put(3440, &c.start_zone, 4); // zone_id
		put(4940, &c.deity, 2);
		put(5428, &c.face, 1);      // luclinface
		return out;
	}
}

void WorldSession::Logout()
{
	if (m_session && m_state == State::CharacterSelect)
		Send(WorldOp::WorldLogout, nullptr, 0, "OP_WorldLogout");
	Close();
}

void WorldSession::Close()
{
	if (m_session)
		m_session->Close();
	m_session.reset();
	if (m_state != State::ZoneHandoff && m_state != State::Failed)
		m_state = State::Idle;
}

void WorldSession::Pump(int timeout_ms, std::vector<Event> &events)
{
	if (!m_session || m_state == State::Idle || m_state == State::Failed || m_state == State::ZoneHandoff)
		return;

	for (;;) {
		AppPacket packet;
		auto status = m_session->Receive(packet, timeout_ms);
		timeout_ms = 0;
		if (status == RdpSession::Status::Timeout)
			break;
		if (status == RdpSession::Status::Closed) {
			// World answers a rejected login by dropping the connection.
			char text[96];
			snprintf(text, sizeof(text), "connection closed (reason 0x%08x)%s", m_session->DisconnectReason(),
				m_approved ? "" : ", probably a bad or expired session key");
			Fail(events, text);
			return;
		}

		char header[96];
		snprintf(header, sizeof(header), "[world] <- %s (0x%04x) %zu bytes", OpcodeName(packet.opcode, true), packet.opcode,
			packet.data.size());
		Log(header);
		if (m_verbose)
			HexDump(packet.data);

		switch (packet.opcode) {
		case WorldOp::BadLogin:
			Fail(events, "invalid login");
			return;
		case WorldOp::ApproveWorld:
			m_approved = true;
			Log("[world]    access granted");
			events.push_back({Event::Type::Approved, ""});
			break;
		case WorldOp::LogServer:
			// HandleWorldMessage 0x41c3: the ruleset at 4 and the name rule at 12.
			m_server_type = Read<uint32_t>(packet.data, 4);
			m_strict_names = Read<uint32_t>(packet.data, 12) != 0;
			m_world_short_name = CString(packet.data, 32, 32);
			Log("[world]    world short name '" + m_world_short_name + "'");
			break;
		case WorldOp::EnterWorld:
			// Before OP_SendCharInfo this is the live character for a zoning reconnect; empty normally.
			if (m_state == State::Authenticating) {
				std::string live = CString(packet.data);
				Log("[world]    live character: '" + live + "'");
				if (m_zoning) {
					// Carry on as that character: straight to the zone's address.
					if (!live.empty())
						m_entering = live;
					m_state = State::EnteringWorld;
					m_wait_started_ms = NowMs();
					Send(WorldOp::EnterWorld, m_entering.c_str(), m_entering.size() + 1, "OP_EnterWorld " + m_entering);
				}
			}
			break;
		case WorldOp::ExpansionInfo:
			m_expansions = Read<uint32_t>(packet.data, 0);
			break;
		case WorldOp::SendCharInfo:
			m_characters = ParseCharacterSelect(packet.data);
			if (m_state == State::Creating) {
				Log("[world]    created " + m_new_character.name);
				events.push_back({Event::Type::CharacterCreated, m_new_character.name});
			}
			if (m_state == State::Authenticating || m_state == State::Creating)
				m_state = State::CharacterSelect;
			events.push_back({Event::Type::Characters, ""});
			break;
		case WorldOp::ApproveName: {
			bool approved = !packet.data.empty() && packet.data[0] == 1;
			if (m_state == State::ApprovingName && approved) {
				auto body = BuildCharCreate(m_new_character);
				m_state = State::Creating;
				m_wait_started_ms = NowMs();
				Send(WorldOp::CharacterCreate, body.data(), body.size(), "OP_CharacterCreate " + m_new_character.name);
			}
			else if (m_state == State::ApprovingName || m_state == State::Creating) {
				// A rejected name, or HandleCharacterCreatePacket failing (it answers with OP_ApproveName 0).
				std::string why = m_state == State::ApprovingName ? "the name was rejected" : "the server rejected the character";
				m_state = State::CharacterSelect;
				events.push_back({Event::Type::CreateFailed, why});
			}
			break;
		}
		case WorldOp::MOTD:
			m_motd = CString(packet.data);
			Log("[world]    MOTD: " + m_motd);
			break;
		case WorldOp::SetChatServer:
			Log("[world]    chat server: " + CString(packet.data));
			break;
		case WorldOp::ZoneUnavail:
			m_state = State::CharacterSelect;
			events.push_back({Event::Type::ZoneUnavailable, CString(packet.data)});
			break;
		case WorldOp::ZoneServerInfo:
			m_zone_address = CString(packet.data, 0, 128);
			// world/client.cpp stores ntohs(port), i.e. network byte order on the wire.
			m_zone_port = packet.data.size() >= 130 ? static_cast<uint16_t>((packet.data[128] << 8) | packet.data[129]) : 0;
			Log("[world]    zone server " + m_zone_address + ":" + std::to_string(m_zone_port));
			m_state = State::ZoneHandoff;
			// Like the real client, drop the world connection as soon as we have the zone address.
			m_session->Close(0);
			events.push_back({Event::Type::ZoneServer, ""});
			return;
		default:
			break;
		}
	}

	bool waiting = m_state == State::Authenticating || m_state == State::EnteringWorld || m_state == State::ApprovingName ||
	               m_state == State::Creating;
	if (waiting && NowMs() - m_wait_started_ms > ResponseTimeoutMs)
		Fail(events, m_state == State::EnteringWorld ? "timed out waiting for the zone server address"
		                                             : "timed out waiting for the world server");
}

bool RunWorld(const LoginResult &login, const WorldOptions &options, ZoneHandoff &handoff)
{
	WorldSession session;
	session.SetLogger([](const std::string &line) { printf("%s\n", line.c_str()); });
	session.SetVerbose(options.verbose);
	std::string host = options.host.empty() ? login.world.address : options.host;
	if (!session.Begin(host, options.port, login.session_name, login.world_key))
		return false;

	uint64_t idle_until = 0;
	bool created = false;
	for (;;) {
		std::vector<WorldSession::Event> events;
		session.Pump(100, events);
		for (const auto &event : events) {
			switch (event.type) {
			case WorldSession::Event::Type::Error:
				fprintf(stderr, "[world] error: %s\n", event.text.c_str());
				return false;
			case WorldSession::Event::Type::ZoneUnavailable:
				fprintf(stderr, "[world] zone unavailable: %s\n", event.text.c_str());
				return false;
			case WorldSession::Event::Type::Characters: {
				printf("[world]    character select:\n");
				int shown = 0;
				const auto &slots = session.Characters();
				for (size_t i = 0; i < slots.size(); ++i) {
					if (slots[i].Empty())
						continue;
					printf("[world]      [%zu] %-15s level %2u %-13s %-10s zone %u\n", i, slots[i].name.c_str(), slots[i].level,
						ClassName(slots[i].class_id), RaceName(slots[i].race), slots[i].zone);
					++shown;
				}
				if (shown == 0)
					printf("[world]      (no characters on this account)\n");

				if (!options.create.empty() && !created) {
					created = true;
					NewCharacter character;
					character.name = options.create;
					if (!session.CreateCharacter(character)) {
						fprintf(stderr, "[world] couldn't start character creation\n");
						return false;
					}
					break;
				}
				if (!options.character.empty())
					session.EnterWorld(options.character);
				else if (idle_until == 0) {
					printf("[world] at character select; idling %d s\n", options.idle_seconds);
					idle_until = NowMs() + static_cast<uint64_t>(options.idle_seconds) * 1000;
				}
				break;
			}
			case WorldSession::Event::Type::CreateFailed:
				fprintf(stderr, "[world] character creation failed: %s\n", event.text.c_str());
				return false;
			case WorldSession::Event::Type::ZoneServer:
				handoff.character = session.EnteringCharacter();
				handoff.address = session.ZoneAddress();
				handoff.port = session.ZonePort();
				return true;
			default:
				break;
			}
		}

		if (idle_until != 0 && NowMs() >= idle_until) {
			printf("[world] idle period over, logging out\n");
			session.Logout();
			return true;
		}
	}
}
