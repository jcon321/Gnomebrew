#include "login_client.h"
#include "des.h"
#include "opcodes.h"
#include "rdp_session.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
	// server/loginserver/eq_crypto.cpp: Verant's DES key, also used as the IV.
	const uint8_t VerantKey[8] = {19, 217, 19, 109, 208, 52, 21, 251};
	constexpr uint64_t ResponseTimeoutMs = 10000;

	uint64_t NowMs()
	{
		return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
	}

	std::vector<uint8_t> EncryptCredentials(const std::string &user, const std::string &password)
	{
		uint8_t plain[40] = {};
		memcpy(plain, user.c_str(), user.size() < 19 ? user.size() : 19);
		memcpy(plain + 20, password.c_str(), password.size() < 19 ? password.size() : 19);
		std::vector<uint8_t> out(40);
		Des::CbcEncrypt(VerantKey, VerantKey, plain, out.data(), sizeof(plain));
		return out;
	}

	std::string CString(const std::vector<uint8_t> &data, size_t offset = 0, size_t max = SIZE_MAX)
	{
		std::string s;
		for (size_t i = offset; i < data.size() && s.size() < max && data[i]; ++i)
			s.push_back(static_cast<char>(data[i]));
		return s;
	}

	// ServerList_Struct (u16 count, pad[2], u8 showusercount), then per world:
	// name\0 address\0 ServerListServerFlags_Struct (13 bytes packed).
	bool ParseServerList(const std::vector<uint8_t> &data, std::vector<WorldListing> &worlds, bool &show_counts)
	{
		if (data.size() < 5)
			return false;

		uint16_t count = static_cast<uint16_t>(data[0] | (data[1] << 8));
		show_counts = data[4] == 0xff;
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
			memcpy(&w.status, &data[pos + 9], 4);
			pos += 13;
			worlds.push_back(w);
		}
		return true;
	}
}

LoginSession::LoginSession() = default;
LoginSession::~LoginSession() = default;

void LoginSession::Log(const std::string &line)
{
	if (m_log)
		m_log(line);
}

void LoginSession::Fail(std::vector<Event> &events, Event::Error error, const std::string &text)
{
	Log("[login] " + text);
	m_state = State::Failed;
	if (m_session)
		m_session->Close();
	events.push_back({Event::Type::Error, text, error});
}

bool LoginSession::Send(uint16_t opcode, const void *body, size_t size, const char *what)
{
	Log(std::string("[login] -> ") + what);
	return m_session && m_session->Send(opcode, body, size);
}

bool LoginSession::Begin(const std::string &host, uint16_t port, const std::string &user, const std::string &password)
{
	Close();
	m_user = user;
	m_password = password;
	m_worlds.clear();
	m_banner.clear();
	m_result = LoginResult{};

	m_session = std::make_unique<RdpSession>();
	if (!m_session->Connect(host, port)) {
		m_state = State::Failed;
		return false;
	}

	m_state = State::Connecting;
	m_wait_started_ms = NowMs();
	// The server ignores the SessionReady body.
	return Send(LoginOp::SessionReady, nullptr, 0, "OP_SessionReady");
}

void LoginSession::RequestServerList()
{
	if (m_state == State::ServerList || m_state == State::LoggingIn)
		Send(LoginOp::ServerListRequest, nullptr, 0, "OP_ServerListRequest");
}

bool LoginSession::Play(const WorldListing &world)
{
	if (m_state != State::ServerList)
		return false;
	m_result.world = world;
	m_state = State::Playing;
	m_wait_started_ms = NowMs();
	// Handle_Play matches this string against the world's local or remote IP.
	return Send(LoginOp::PlayEverquest, world.address.c_str(), world.address.size() + 1,
		("OP_PlayEverquestRequest " + world.address).c_str());
}

void LoginSession::Close()
{
	if (m_session)
		m_session->Close();
	m_session.reset();
	if (m_state != State::PlayApproved && m_state != State::Failed)
		m_state = State::Idle;
}

void LoginSession::Pump(int timeout_ms, std::vector<Event> &events)
{
	if (!m_session || m_state == State::Idle || m_state == State::Failed)
		return;

	for (;;) {
		AppPacket packet;
		auto status = m_session->Receive(packet, timeout_ms);
		timeout_ms = 0; // only wait for the first packet
		if (status == RdpSession::Status::Timeout)
			break;
		if (status == RdpSession::Status::Closed) {
			if (m_state != State::PlayApproved) {
				char reason[64];
				snprintf(reason, sizeof(reason), "connection closed (reason 0x%08x)", m_session->DisconnectReason());
				Fail(events, Event::Error::ConnectionLost, reason);
			}
			return;
		}

		char header[96];
		snprintf(header, sizeof(header), "[login] <- %s (0x%04x) %zu bytes", OpcodeName(packet.opcode, false), packet.opcode,
			packet.data.size());
		Log(header);
		if (m_verbose)
			HexDump(packet.data);

		switch (packet.opcode) {
		case LoginOp::SessionReady:
			Log("[login]    server build: " + CString(packet.data));
			if (m_state == State::Connecting) {
				auto credentials = EncryptCredentials(m_user, m_password);
				Send(LoginOp::LoginPC, credentials.data(), credentials.size(), ("OP_LoginPC (user '" + m_user + "')").c_str());
				m_state = State::LoggingIn;
				m_wait_started_ms = NowMs();
				events.push_back({Event::Type::Status, "Logging in"});
			}
			break;
		case LoginOp::ClientError:
			if (m_state == State::Playing) {
				// e.g. "Error 1020: Your chosen World Server is DOWN." The login connection stays up.
				Log("[login] play rejected: " + CString(packet.data));
				m_state = State::ServerList;
				events.push_back({Event::Type::Error, CString(packet.data), Event::Error::PlayRejected});
				break;
			}
			Fail(events, Event::Error::Rejected, CString(packet.data));
			return;
		case LoginOp::LoginAccepted:
			m_result.session_name = CString(packet.data, 0, 10);
			Log("[login]    accepted, session name " + m_result.session_name);
			events.push_back({Event::Type::LoggedIn, m_result.session_name});
			Send(LoginOp::LoginBanner, nullptr, 0, "OP_LoginBanner");
			Send(LoginOp::ServerListRequest, nullptr, 0, "OP_ServerListRequest");
			m_wait_started_ms = NowMs();
			break;
		case LoginOp::LoginBanner:
			m_banner = CString(packet.data, 4);
			Log("[login]    banner: " + m_banner);
			break;
		case LoginOp::ServerListRequest: {
			std::vector<WorldListing> worlds;
			bool show_counts = false;
			if (!ParseServerList(packet.data, worlds, show_counts)) {
				Fail(events, Event::Error::ConnectionLost, "malformed server list");
				return;
			}
			m_worlds = std::move(worlds);
			m_show_user_count = show_counts;
			char line[64];
			snprintf(line, sizeof(line), "[login]    %zu world(s)", m_worlds.size());
			Log(line);
			if (m_state == State::LoggingIn)
				m_state = State::ServerList;
			events.push_back({Event::Type::ServerList, ""});
			break;
		}
		case LoginOp::PlayEverquest:
			// world_server.cpp ProcessUsertoWorldResp: 17 bytes, key copied to offset 1.
			if (packet.data.size() < 11) {
				Fail(events, Event::Error::ConnectionLost, "short play response");
				return;
			}
			m_result.world_key = CString(packet.data, 1, 10);
			Log("[login]    play approved, world key " + m_result.world_key);
			m_state = State::PlayApproved;
			events.push_back({Event::Type::PlayApproved, m_result.world_key});
			m_session->Close();
			return;
		default:
			break;
		}
	}

	bool waiting = m_state == State::Connecting || m_state == State::LoggingIn || m_state == State::Playing;
	if (waiting && NowMs() - m_wait_started_ms > ResponseTimeoutMs)
		Fail(events, Event::Error::ConnectFailed, "timed out waiting for the login server");
}

namespace
{
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
	LoginSession session;
	session.SetLogger([](const std::string &line) { printf("%s\n", line.c_str()); });
	session.SetVerbose(options.verbose);
	if (!session.Begin(options.host, options.port, options.user, options.password))
		return LoginOutcome::Failed;

	bool requested_play = false;
	for (;;) {
		std::vector<LoginSession::Event> events;
		session.Pump(100, events);
		for (const auto &event : events) {
			switch (event.type) {
			case LoginSession::Event::Type::Error:
				fprintf(stderr, "[login] error: %s\n", event.text.c_str());
				return event.text.find("Invalid username or password") != std::string::npos ? LoginOutcome::BadCredentials
				                                                                             : LoginOutcome::Failed;
			case LoginSession::Event::Type::ServerList: {
				if (requested_play)
					break;
				const auto &worlds = session.Worlds();
				for (size_t i = 0; i < worlds.size(); ++i)
					printf("[login]      [%zu] %-30s %-15s id=%d status=%d flags=0x%x\n", i, worlds[i].name.c_str(),
						worlds[i].address.c_str(), worlds[i].world_id, worlds[i].status, worlds[i].flags);
				const WorldListing *world = PickWorld(worlds, options.world);
				if (!world) {
					fprintf(stderr, "[login] no world matches '%s'\n", options.world.c_str());
					return LoginOutcome::Failed;
				}
				requested_play = session.Play(*world);
				break;
			}
			case LoginSession::Event::Type::PlayApproved:
				result = session.Result();
				return LoginOutcome::Ok;
			default:
				break;
			}
		}
	}
}
