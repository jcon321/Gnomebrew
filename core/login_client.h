#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class RdpSession;

struct WorldListing
{
	std::string name;
	std::string address;
	int32_t world_id = 0;
	int32_t flags = 0;
	int32_t status = 0;    // ServerListServerFlags_Struct.usercount: -1 down, -2 locked, else players/rank
	uint8_t green_name = 0; // "preferred" server, drawn green
};

struct LoginResult
{
	std::string session_name; // "LS#<lsaccount id>", used as the world login name
	std::string world_key;    // 10 char key, used as the world login password
	WorldListing world;
};

// One login server connection, driven by Pump(). It runs the handshake
// automatically: SessionReady -> LoginPC -> LoginBanner -> ServerListRequest.
// The caller then picks a world with Play().
class LoginSession
{
public:
	enum class State { Idle, Connecting, LoggingIn, ServerList, Playing, PlayApproved, Failed };

	struct Event
	{
		enum class Type
		{
			Status,        // progress text (connecting, logging in...)
			LoggedIn,      // credentials accepted
			ServerList,    // Worlds() changed
			PlayApproved,  // Result() has the world key
			Error,         // the session is over; text is the reason
		};
		// PlayRejected is the only error that leaves the session usable (back at the server list).
		enum class Error { None, ConnectFailed, Rejected, ConnectionLost, PlayRejected };

		Type type;
		std::string text;
		Error error = Error::None;
	};

	LoginSession();
	~LoginSession();

	void SetLogger(std::function<void(const std::string &)> logger) { m_log = std::move(logger); }
	void SetVerbose(bool verbose) { m_verbose = verbose; }

	bool Begin(const std::string &host, uint16_t port, const std::string &user, const std::string &password);
	// Services the connection; waits up to timeout_ms for traffic. Appends events.
	void Pump(int timeout_ms, std::vector<Event> &events);
	void RequestServerList();
	bool Play(const WorldListing &world);
	void Close();

	State GetState() const { return m_state; }
	const std::vector<WorldListing> &Worlds() const { return m_worlds; }
	bool ShowUserCounts() const { return m_show_user_count; }
	const std::string &Banner() const { return m_banner; }
	const LoginResult &Result() const { return m_result; }

private:
	void Log(const std::string &line);
	void Fail(std::vector<Event> &events, Event::Error error, const std::string &text);
	bool Send(uint16_t opcode, const void *body, size_t size, const char *what);

	std::unique_ptr<RdpSession> m_session;
	State m_state = State::Idle;
	std::string m_user;
	std::string m_password;
	std::vector<WorldListing> m_worlds;
	bool m_show_user_count = false;
	std::string m_banner;
	LoginResult m_result;
	uint64_t m_wait_started_ms = 0;
	std::function<void(const std::string &)> m_log;
	bool m_verbose = false;
};

struct LoginOptions
{
	std::string host = "127.0.0.1";
	uint16_t port = 6000;
	std::string user;
	std::string password;
	std::string world;        // name substring or index; empty = first
	bool verbose = false;
	int timeout_ms = 10000;
};

enum class LoginOutcome { Ok, BadCredentials, Failed };

// Blocking helper for the console client: logs in, picks a world, requests play.
LoginOutcome RunLogin(const LoginOptions &options, LoginResult &result);
