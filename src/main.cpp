// Headless EQMac (TAKP) client: login server -> world server -> zone handoff.

#include "login_client.h"
#include "world_client.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace
{
	void Usage(const char *argv0)
	{
		fprintf(stderr,
			"usage: %s -u USER -p PASS [options]\n"
			"  --login HOST[:PORT]   login server (default 127.0.0.1:6000)\n"
			"  --world NAME|INDEX    world from the server list (default: first)\n"
			"  --world-host HOST     override the world address from the server list\n"
			"  --world-port PORT     world port (default 9000)\n"
			"  --char NAME           enter world with this character and print the zone address\n"
			"  --idle SECONDS        without --char, stay at character select this long (default 0)\n"
			"  --no-retry            don't retry once after 'Invalid username or password'\n"
			"                        (the first login of a new account creates it and is rejected)\n"
			"  -v                    hex dump every packet\n",
			argv0);
	}

	void SplitHostPort(const std::string &value, std::string &host, uint16_t &port)
	{
		auto colon = value.rfind(':');
		if (colon == std::string::npos) {
			host = value;
			return;
		}
		host = value.substr(0, colon);
		port = static_cast<uint16_t>(atoi(value.c_str() + colon + 1));
	}
}

int main(int argc, char **argv)
{
	LoginOptions login;
	WorldOptions world;
	bool retry = true;

	for (int i = 1; i < argc; ++i) {
		std::string arg = argv[i];
		auto next = [&]() -> std::string {
			if (i + 1 >= argc) {
				Usage(argv[0]);
				exit(2);
			}
			return argv[++i];
		};

		if (arg == "-u")
			login.user = next();
		else if (arg == "-p")
			login.password = next();
		else if (arg == "--login")
			SplitHostPort(next(), login.host, login.port);
		else if (arg == "--world")
			login.world = next();
		else if (arg == "--world-host")
			world.host = next();
		else if (arg == "--world-port")
			world.port = static_cast<uint16_t>(atoi(next().c_str()));
		else if (arg == "--char")
			world.character = next();
		else if (arg == "--idle")
			world.idle_seconds = atoi(next().c_str());
		else if (arg == "--no-retry")
			retry = false;
		else if (arg == "-v")
			login.verbose = world.verbose = true;
		else {
			Usage(argv[0]);
			return 2;
		}
	}

	if (login.user.empty() || login.password.empty()) {
		Usage(argv[0]);
		return 2;
	}
	if (login.user.size() > 19 || login.password.size() > 19) {
		fprintf(stderr, "username and password are limited to 19 characters\n");
		return 2;
	}

	LoginResult result;
	LoginOutcome outcome = RunLogin(login, result);
	if (outcome == LoginOutcome::BadCredentials && retry) {
		// Handle_Login auto-creates a missing account but still rejects that first attempt,
		// and it refuses a second login on the same connection, so reconnect.
		printf("[login] retrying once on a fresh connection (new accounts are created on the first attempt)\n");
		result = LoginResult{};
		outcome = RunLogin(login, result);
	}
	if (outcome != LoginOutcome::Ok)
		return 1;

	ZoneHandoff handoff;
	if (!RunWorld(result, world, handoff))
		return 1;

	if (!handoff.address.empty())
		printf("[done] %s should connect to zone %s:%u (zone client not written yet)\n", handoff.character.c_str(),
			handoff.address.c_str(), handoff.port);
	else
		printf("[done] login and world handshake complete\n");
	return 0;
}
