#pragma once

#include "login_client.h"

#include <cstdint>
#include <string>

struct WorldOptions
{
	std::string host;           // defaults to the address from the server list
	uint16_t port = 9000;       // the client has no port in the server list; world listens on 9000
	std::string character;      // if set, send OP_EnterWorld for this character
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

bool RunWorld(const LoginResult &login, const WorldOptions &options, ZoneHandoff &handoff);
