// Headless EQMac (TAKP) client: login server -> world server -> zone handoff.

#include "login_client.h"
#include "world_client.h"
#include "zone_session.h"

#include <algorithm>
#include <chrono>
#include <cmath>

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
			"  --create NAME         create a Human Warrior with this name first (for testing)\n"
			"  --idle SECONDS        without --char, stay at character select this long (default 0)\n"
			"  --zone SECONDS        with --char, enter the zone, stay this long, then /camp (30s) and log out\n"
			"  --say TEXT            with --zone, /say this once in the zone\n"
			"  --zone-to ID          with --zone, once in, ask to zone to zone ID (the zone checks it against its\n"
			"                        zone points), go there through world, then stay and camp there\n"
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

namespace
{
	uint64_t NowMs()
	{
		return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
	}

	void PrintNearby(const ZoneSession &zone)
	{
		const ZoneProfile &me = zone.Profile();
		std::vector<std::pair<float, const ZoneSpawn *>> nearby;
		for (const auto &entry : zone.Spawns()) {
			if (entry.first == zone.MySpawnId())
				continue;
			float dx = entry.second.x - me.x, dy = entry.second.y - me.y;
			nearby.push_back({std::sqrt(dx * dx + dy * dy), &entry.second});
		}
		std::sort(nearby.begin(), nearby.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
		printf("[zone]    %zu spawns; nearest:\n", nearby.size());
		for (size_t i = 0; i < nearby.size() && i < 10; ++i) {
			const ZoneSpawn &s = *nearby[i].second;
			const char *kind = s.npc == 0 ? "player" : s.npc == 1 ? "npc" : "corpse";
			printf("[zone]      %-28s level %2u %-6s %6.0f away  (%.0f, %.0f, %.0f)\n", s.DisplayName().c_str(), s.level, kind,
				nearby[i].first, s.x, s.y, s.z);
		}
	}

	// Back through world after leaving a zone, to the next zone's address (WorldSession::BeginZoning).
	bool RunZoningWorld(const LoginResult &login, const WorldOptions &options, const std::string &character, ZoneHandoff &handoff)
	{
		WorldSession world;
		world.SetLogger([](const std::string &line) { printf("%s\n", line.c_str()); });
		std::string host = options.host.empty() ? login.world.address : options.host;
		if (!world.BeginZoning(host, options.port, login.session_name, login.world_key, character))
			return false;
		for (;;) {
			std::vector<WorldSession::Event> events;
			world.Pump(100, events);
			for (const auto &event : events) {
				if (event.type == WorldSession::Event::Type::ZoneServer) {
					handoff = {world.EnteringCharacter(), world.ZoneAddress(), world.ZonePort()};
					return true;
				}
				if (event.type == WorldSession::Event::Type::Error || event.type == WorldSession::Event::Type::ZoneUnavailable) {
					fprintf(stderr, "[world] %s\n", event.text.c_str());
					return false;
				}
			}
		}
	}

	bool RunZone(const ZoneHandoff &handoff, int seconds, const std::string &say, bool verbose, uint32_t zone_to = 0,
		const LoginResult *login = nullptr, const WorldOptions *world = nullptr)
	{
		ZoneSession zone;
		zone.SetLogger([](const std::string &line) { printf("%s\n", line.c_str()); });
		zone.SetVerbose(verbose);
		if (!zone.Begin(handoff.address, handoff.port, handoff.character))
			return false;

		uint64_t leave_at = 0;
		bool camping = false;
		for (;;) {
			std::vector<ZoneSession::Event> events;
			zone.Pump(100, events);
			for (const auto &event : events) {
				switch (event.type) {
				case ZoneSession::Event::Type::InZone:
					PrintNearby(zone);
					if (!say.empty())
						zone.Say(say);
					if (zone_to != 0) {
						printf("[zone] asking to zone to %u\n", zone_to);
						zone.RequestZoneChange(zone_to);
						break;
					}
					printf("[zone] staying %d s, then camping\n", seconds);
					leave_at = NowMs() + static_cast<uint64_t>(seconds) * 1000;
					break;
				case ZoneSession::Event::Type::ZoneApproved:
					printf("[zone] zone change to %d approved\n", event.value);
					break;
				case ZoneSession::Event::Type::ZoneDenied:
					printf("[zone] zone change denied (%d); staying %d s, then camping\n", event.value, seconds);
					leave_at = NowMs() + static_cast<uint64_t>(seconds) * 1000;
					break;
				case ZoneSession::Event::Type::Departed: {
					printf("[zone] left; back to world, zoning\n");
					ZoneHandoff next;
					if (!login || !world || !RunZoningWorld(*login, *world, handoff.character, next))
						return false;
					return RunZone(next, seconds, "", verbose);
				}
				case ZoneSession::Event::Type::LoggedOut:
					printf("[done] %s camped out of %s\n", handoff.character.c_str(), zone.Info().long_name.c_str());
					return true;
				case ZoneSession::Event::Type::Error:
					fprintf(stderr, "[zone] error: %s\n", event.text.c_str());
					return false;
				default:
					break;
				}
			}
			if (leave_at && !camping && NowMs() >= leave_at) {
				camping = true;
				printf("[zone] camping (30 s)\n");
				zone.Camp();
			}
		}
	}
}

int main(int argc, char **argv)
{
	LoginOptions login;
	WorldOptions world;
	bool retry = true;
	int zone_seconds = -1;
	std::string say_text;
	uint32_t zone_to = 0;

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
		else if (arg == "--create")
			world.create = next();
		else if (arg == "--idle")
			world.idle_seconds = atoi(next().c_str());
		else if (arg == "--zone")
			zone_seconds = atoi(next().c_str());
		else if (arg == "--say")
			say_text = next();
		else if (arg == "--zone-to")
			zone_to = static_cast<uint32_t>(atoi(next().c_str()));
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

	if (handoff.address.empty()) {
		printf("[done] login and world handshake complete\n");
		return 0;
	}
	if (zone_seconds < 0) {
		printf("[done] %s can connect to zone %s:%u (add --zone SECONDS to go in)\n", handoff.character.c_str(),
			handoff.address.c_str(), handoff.port);
		return 0;
	}
	return RunZone(handoff, zone_seconds, say_text, login.verbose, zone_to, &result, &world) ? 0 : 1;
}
