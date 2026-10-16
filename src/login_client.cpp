#include "login_client.h"
#include "opcodes.h"
#include "rdp_session.h"

#include <openssl/des.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
	// server/loginserver/eq_crypto.cpp: Verant's DES key, also used as the IV.
	const DES_cblock VerantKey = {19, 217, 19, 109, 208, 52, 21, 251};

	std::vector<uint8_t> EncryptCredentials(const std::string &user, const std::string &password)
	{
		uint8_t plain[40] = {};
		strncpy(reinterpret_cast<char *>(plain), user.c_str(), 19);
		strncpy(reinterpret_cast<char *>(plain + 20), password.c_str(), 19);

		DES_key_schedule schedule;
		DES_cblock key, iv;
		memcpy(key, VerantKey, sizeof(key));
		memcpy(iv, VerantKey, sizeof(iv));
		DES_key_sched(&key, &schedule);

		std::vector<uint8_t> out(40);
		DES_ncbc_encrypt(plain, out.data(), sizeof(plain), &schedule, &iv, DES_ENCRYPT);
		return out;
	}

	std::string CString(const std::vector<uint8_t> &data, size_t offset = 0, size_t max = SIZE_MAX)
	{
		std::string s;
		for (size_t i = offset; i < data.size() && s.size() < max && data[i]; ++i)
			s.push_back(static_cast<char>(data[i]));
		return s;
	}

	// ServerList_Struct, then per world: name\0 address\0 ServerListServerFlags_Struct (13 bytes packed).
	bool ParseServerList(const std::vector<uint8_t> &data, std::vector<WorldListing> &worlds)
	{
		if (data.size() < 5)
			return false;

		uint16_t count = static_cast<uint16_t>(data[0] | (data[1] << 8));
		size_t pos = 5;
		for (uint16_t i = 0; i < count; ++i) {
			WorldListing w;
			w.name = CString(data, pos);
			pos += w.name.size() + 1;
			w.address = CString(data, pos);
			pos += w.address.size() + 1;
			if (pos + 13 > data.size())
				return false;
			w.green_name = data[pos];
			memcpy(&w.flags, &data[pos + 1], 4);
			memcpy(&w.world_id, &data[pos + 5], 4);
			memcpy(&w.user_count, &data[pos + 9], 4);
			pos += 13;
			worlds.push_back(w);
		}
		return true;
	}

	const WorldListing *PickWorld(const std::vector<WorldListing> &worlds, const std::string &wanted)
	{
		if (worlds.empty())
			return nullptr;
		if (wanted.empty())
			return &worlds[0];

		char *end = nullptr;
		long index = strtol(wanted.c_str(), &end, 10);
		if (end && *end == '\0' && index >= 0 && static_cast<size_t>(index) < worlds.size())
			return &worlds[index];

		for (const auto &w : worlds)
			if (w.name.find(wanted) != std::string::npos)
				return &w;
		return nullptr;
	}
}

LoginOutcome RunLogin(const LoginOptions &options, LoginResult &result)
{
	RdpSession session;
	if (!session.Connect(options.host, options.port))
		return LoginOutcome::Failed;

	enum class Stage { SessionReady, Credentials, Banner, ServerList, Play, Done } stage = Stage::SessionReady;
	std::vector<WorldListing> worlds;

	// The server ignores the SessionReady body.
	printf("[login] -> OP_SessionReady\n");
	session.Send(LoginOp::SessionReady, nullptr, 0);

	while (stage != Stage::Done) {
		AppPacket packet;
		auto status = session.Receive(packet, options.timeout_ms);
		if (status == RdpSession::Status::Timeout) {
			fprintf(stderr, "[login] timed out waiting for the login server\n");
			return LoginOutcome::Failed;
		}
		if (status == RdpSession::Status::Closed) {
			fprintf(stderr, "[login] connection closed (reason 0x%08x)\n", session.DisconnectReason());
			return LoginOutcome::Failed;
		}

		printf("[login] <- %s (0x%04x) %zu bytes\n", OpcodeName(packet.opcode, false), packet.opcode, packet.data.size());
		if (options.verbose)
			HexDump(packet.data);

		switch (packet.opcode) {
		case LoginOp::SessionReady: {
			printf("[login]    server build: %s\n", CString(packet.data).c_str());
			printf("[login] -> OP_LoginPC (user '%s')\n", options.user.c_str());
			session.Send(LoginOp::LoginPC, EncryptCredentials(options.user, options.password));
			stage = Stage::Credentials;
			break;
		}
		case LoginOp::ClientError: {
			std::string message = CString(packet.data);
			fprintf(stderr, "[login] server error: %s\n", message.c_str());
			session.Close();
			return message.find("Invalid username or password") != std::string::npos ? LoginOutcome::BadCredentials
			                                                                          : LoginOutcome::Failed;
		}
		case LoginOp::LoginAccepted: {
			result.session_name = CString(packet.data, 0, 10);
			printf("[login]    accepted, session name %s\n", result.session_name.c_str());
			printf("[login] -> OP_LoginBanner\n");
			session.Send(LoginOp::LoginBanner, nullptr, 0);
			stage = Stage::Banner;
			break;
		}
		case LoginOp::LoginBanner: {
			printf("[login]    banner: %s\n", CString(packet.data, 4).c_str());
			printf("[login] -> OP_ServerListRequest\n");
			session.Send(LoginOp::ServerListRequest, nullptr, 0);
			stage = Stage::ServerList;
			break;
		}
		case LoginOp::ServerListRequest: {
			worlds.clear();
			if (!ParseServerList(packet.data, worlds)) {
				fprintf(stderr, "[login] malformed server list\n");
				return LoginOutcome::Failed;
			}
			printf("[login]    %zu world(s):\n", worlds.size());
			for (size_t i = 0; i < worlds.size(); ++i)
				printf("[login]      [%zu] %-30s %-15s id=%d users=%u flags=0x%x\n", i, worlds[i].name.c_str(),
					worlds[i].address.c_str(), worlds[i].world_id, worlds[i].user_count, worlds[i].flags);

			const WorldListing *world = PickWorld(worlds, options.world);
			if (!world) {
				fprintf(stderr, "[login] no world matches '%s'\n", options.world.c_str());
				return LoginOutcome::Failed;
			}
			result.world = *world;

			// Handle_Play matches this string against the world's local or remote IP.
			printf("[login] -> OP_PlayEverquestRequest %s\n", world->address.c_str());
			session.Send(LoginOp::PlayEverquest, world->address.c_str(), world->address.size() + 1);
			stage = Stage::Play;
			break;
		}
		case LoginOp::PlayEverquest: {
			// world_server.cpp ProcessUsertoWorldResp: 17 bytes, key copied to offset 1.
			if (packet.data.size() < 11) {
				fprintf(stderr, "[login] short play response\n");
				return LoginOutcome::Failed;
			}
			result.world_key = CString(packet.data, 1, 10);
			printf("[login]    play approved, world key %s\n", result.world_key.c_str());
			stage = Stage::Done;
			break;
		}
		default:
			break;
		}
	}

	session.Close();
	return LoginOutcome::Ok;
}
