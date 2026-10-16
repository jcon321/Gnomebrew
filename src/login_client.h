#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct WorldListing
{
	std::string name;
	std::string address;
	int32_t world_id = 0;
	int32_t flags = 0;
	uint32_t user_count = 0;
	uint8_t green_name = 0;
};

struct LoginResult
{
	std::string session_name; // "LS#<lsaccount id>", used as the world login name
	std::string world_key;    // 10 char key, used as the world login password
	WorldListing world;
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

// One login server session: handshake, credentials, server list, play request.
LoginOutcome RunLogin(const LoginOptions &options, LoginResult &result);
