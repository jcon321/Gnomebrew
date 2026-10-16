#include "world_client.h"
#include "opcodes.h"
#include "rdp_session.h"

#include <chrono>
#include <cstdio>
#include <cstring>

namespace
{
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
	// into a 188 byte buffer, then zoning @188, a uint32 @192 and 0xc3 @196: 200 bytes.
	std::vector<uint8_t> BuildLoginInfo(const std::string &name, const std::string &password, bool zoning)
	{
		std::vector<uint8_t> out(200, 0);
		size_t pos = 0;
		memcpy(&out[pos], name.c_str(), name.size());
		pos += name.size() + 1;
		memcpy(&out[pos], password.c_str(), password.size());
		out[188] = zoning ? 1 : 0;
		out[196] = 0xc3;
		return out;
	}

	const char *ClassName(uint8_t id)
	{
		static const char *names[] = {"?", "Warrior", "Cleric", "Paladin", "Ranger", "Shadowknight", "Druid", "Monk",
			"Bard", "Rogue", "Shaman", "Necromancer", "Wizard", "Magician", "Enchanter", "Beastlord"};
		return id < sizeof(names) / sizeof(names[0]) ? names[id] : "?";
	}

	// CharacterSelect_Struct (common/eq_packet_structs.h), sent unchanged on the wire.
	void PrintCharacterSelect(const std::vector<uint8_t> &data)
	{
		if (data.size() < 1620) {
			printf("[world]    character select is %zu bytes, expected 1620\n", data.size());
			return;
		}

		int shown = 0;
		for (int i = 0; i < 10; ++i) {
			std::string name = CString(data, i * 64, 64);
			if (name.empty() || name == "<none>")
				continue;
			uint8_t level = data[640 + i];
			uint8_t class_id = data[650 + i];
			uint16_t race = Read<uint16_t>(data, 660 + i * 2);
			uint32_t zone = Read<uint32_t>(data, 680 + i * 4);
			printf("[world]      [%d] %-15s level %2u %-13s race %u zone %u\n", i, name.c_str(), level, ClassName(class_id), race, zone);
			++shown;
		}
		if (shown == 0)
			printf("[world]      (no characters on this account)\n");
	}
}

bool RunWorld(const LoginResult &login, const WorldOptions &options, ZoneHandoff &handoff)
{
	std::string host = options.host.empty() ? login.world.address : options.host;

	RdpSession session;
	if (!session.Connect(host, options.port))
		return false;

	printf("[world] -> OP_SendLoginInfo (%s / %s)\n", login.session_name.c_str(), login.world_key.c_str());
	session.Send(WorldOp::SendLoginInfo, BuildLoginInfo(login.session_name, login.world_key, false));

	bool approved = false;
	bool have_char_list = false;
	bool entering = false;
	auto idle_until = std::chrono::steady_clock::time_point::max();

	for (;;) {
		int timeout = options.timeout_ms;
		if (idle_until != std::chrono::steady_clock::time_point::max()) {
			auto left = std::chrono::duration_cast<std::chrono::milliseconds>(idle_until - std::chrono::steady_clock::now()).count();
			if (left <= 0) {
				printf("[world] idle period over, logging out\n");
				session.Send(WorldOp::WorldLogout, nullptr, 0);
				session.Close();
				return true;
			}
			timeout = static_cast<int>(left);
		}

		AppPacket packet;
		auto status = session.Receive(packet, timeout);
		if (status == RdpSession::Status::Timeout) {
			if (idle_until != std::chrono::steady_clock::time_point::max())
				continue;
			fprintf(stderr, "[world] timed out (approved=%d, char list=%d)\n", approved, have_char_list);
			return false;
		}
		if (status == RdpSession::Status::Closed) {
			// World answers a rejected login by dropping the connection.
			fprintf(stderr, "[world] connection closed (reason 0x%08x)%s\n", session.DisconnectReason(),
				approved ? "" : ", probably a bad or expired session key");
			return false;
		}

		printf("[world] <- %s (0x%04x) %zu bytes\n", OpcodeName(packet.opcode, true), packet.opcode, packet.data.size());
		if (options.verbose)
			HexDump(packet.data);

		switch (packet.opcode) {
		case WorldOp::BadLogin:
			fprintf(stderr, "[world] invalid login\n");
			return false;
		case WorldOp::ApproveWorld:
			approved = true;
			printf("[world]    access granted\n");
			break;
		case WorldOp::LogServer:
			printf("[world]    world short name '%s' pvp=%u\n", CString(packet.data, 32, 32).c_str(), Read<uint32_t>(packet.data, 4));
			break;
		case WorldOp::EnterWorld:
			// Before OP_SendCharInfo this is the live character for a zoning reconnect; empty normally.
			if (!entering)
				printf("[world]    live character: '%s'\n", CString(packet.data).c_str());
			break;
		case WorldOp::ExpansionInfo:
			printf("[world]    expansions bitmask 0x%x\n", Read<uint32_t>(packet.data, 0));
			break;
		case WorldOp::SendCharInfo:
			have_char_list = true;
			printf("[world]    character select:\n");
			PrintCharacterSelect(packet.data);

			if (!options.character.empty() && !entering) {
				entering = true;
				printf("[world] -> OP_EnterWorld %s\n", options.character.c_str());
				std::vector<uint8_t> name(options.character.begin(), options.character.end());
				name.push_back(0);
				session.Send(WorldOp::EnterWorld, name);
			}
			else if (options.character.empty()) {
				printf("[world] at character select; idling %d s\n", options.idle_seconds);
				idle_until = std::chrono::steady_clock::now() + std::chrono::seconds(options.idle_seconds);
			}
			break;
		case WorldOp::MOTD:
			printf("[world]    MOTD: %s\n", CString(packet.data).c_str());
			break;
		case WorldOp::SetChatServer:
			printf("[world]    chat server: %s\n", CString(packet.data).c_str());
			break;
		case WorldOp::ZoneUnavail:
			fprintf(stderr, "[world] zone unavailable: %s\n", CString(packet.data).c_str());
			return false;
		case WorldOp::ZoneServerInfo: {
			handoff.character = options.character;
			handoff.address = CString(packet.data, 0, 128);
			handoff.port = Read<uint16_t>(packet.data, 128);
			printf("[world]    zone server %s:%u\n", handoff.address.c_str(), handoff.port);
			// Like the real client, drop the world connection as soon as we have the zone address.
			session.Close(0);
			return true;
		}
		default:
			break;
		}
	}
}
