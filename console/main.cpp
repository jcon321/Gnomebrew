// Headless EQMac (TAKP) client: login server -> world server -> zone handoff.

#include "inventory.h"
#include "login_client.h"
#include "opcodes.h"
#include "world_client.h"
#include "zone_session.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

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
			"  --create-class N      with --create, a Human of class N instead (2 cleric, 11 necromancer...)\n"
			"  --idle SECONDS        without --char, stay at character select this long (default 0)\n"
			"  --zone SECONDS        with --char, enter the zone, stay this long, then /camp (30s) and log out\n"
			"  --say TEXT            with --zone, /say this once in the zone\n"
			"  --click-door ID       with --zone, use door ID once in the zone and print the doors that move\n"
			"  --pickup DROP|far     with --zone, once in, pick up ground item DROP (\"far\": the farthest one that can\n"
			"                        be picked up) with no range check, and print the zone's answer and the cursor\n"
			"  --click-link ID       with --zone, click a chat link to item (or say link) ID once in the zone and\n"
			"                        print the item the zone sends back\n"
			"  --zone-to ID          with --zone, once in, ask to zone to zone ID (the zone checks it against its\n"
			"                        zone points), go there through world, then stay and camp there\n"
			"  --move FROM:TO[:N]    with --zone, once in, move what's in slot FROM to TO as the inventory window\n"
			"                        does (0 the cursor; N takes N of a stack onto the cursor); \"auto\" puts the\n"
			"                        cursor's item away. Repeatable, done in order; the items print before and after\n"
			"  --cast GEM[:SPELL]    with --zone, once in, cast the spell in gem GEM (1..8; SPELL memorized there first)\n"
			"                        on ourselves and print\n"
			"                        the zone's answers. Repeatable: each 3 s after the zone ends the one before\n"
			"  --cast-move           with --cast: step 5 units as each cast is sent, so the zone interrupts it\n"
			"  --attack NAME         with --zone, once in, target the nearest NPC whose name starts with NAME, turn\n"
			"                        auto attack on and walk at it, printing the hits and the death\n"
			"  --skill N             with --attack, use combat skill N on it too (OP_CombatAbility), every 9 s\n"
			"  --group-invite NAME   with --zone, 15 s after coming in and NAME is here, invite NAME to a group\n"
			"  --group-accept        with --zone, accept any group invitation 2 s after it comes (OP_GroupFollow);\n"
			"                        every group packet prints\n"
			"  --raid-invite NAME    with --zone, 15 s after coming in and NAME is here, invite NAME to a raid\n"
			"  --raid-accept         with --zone, accept any raid invitation 2 s after it comes; every raid\n"
			"                        packet prints\n"
			"  --aa TEXT             with --zone, once in, send OP_AAAction TEXT (\"buy 12\", \"on 50\", \"off\",\n"
			"                        \"activate 12 0\"), and print our AA ranks, points and timers as they change.\n"
			"                        Repeatable, with --say-more: each 3 s after the one before, in order\n"
			"  --say-more TEXT       with --zone, a /say in the --aa queue (a GM's #level 60)\n"
			"  --pet N[:TARGET]      with --zone, OP_PetCommands N (1 back off .. 17 feign) in the --aa queue;\n"
			"                        TARGET a spawn id\n"
			"  --target-more WHO     in the --aa queue: target ourselves (\"self\"), the nearest NPC (\"npc\") or\n"
			"                        the nearest spawn whose\n"
			"                        name starts with WHO (\"Replaytest's\": our corpse)\n"
			"  --corpse-more         in the --aa queue: /corpse, our nearest corpse dragged to us (OP_GMSummon)\n"
			"  --home                in the --aa queue: go home dead (OP_ZoneChange to the bind zone, reason 10);\n"
			"                        the queue carries on in the next zone\n"
			"  --answer yes|no       answer the zone's resurrect, sacrifice and translocate questions 2 s on\n"
			"  --record DIR          with --zone, write each zone session's packets to DIR/<char>-<time>.eqr\n"
			"  --replay FILE         play a recorded zone session back (no server): stay --zone seconds\n"
			"                        (default 10), print what's around, then camp out\n"
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
			printf("[zone]      %-28s level %2u %-6s %6.0f away  (%.0f, %.0f, %.0f)  race %u texture %u helm %u fly %u\n",
				s.DisplayName().c_str(), s.level, kind, nearby[i].first, s.x, s.y, s.z, s.race, s.body_texture, s.helm, s.fly_mode);
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

	std::string g_record_dir;
	// --aa (1), --say-more (0) and --pet (2), in order
	std::vector<std::pair<int, std::string>> g_aa_steps;
	size_t g_next_step = 0; // the queue's next step, carried into the next zone (--home)
	std::string g_answer;   // --answer
	int g_click_link = -1; // --click-link
	std::string g_replay_path;
	std::vector<std::string> g_moves; // --move
	std::vector<int> g_casts;         // --cast, gems from 1
	std::vector<int> g_cast_spells;   // --cast GEM:SPELL's spell (0 the gem's)
	std::string g_pickup;             // --pickup: a drop id or "far"
	bool g_cast_move = false;         // --cast-move: step 5 units right after each --cast
	std::string g_attack;             // --attack: the name of an NPC to fight
	uint16_t g_attack_id = 0;         // whom we're fighting (0 nobody)
	int g_skill = -1;                 // --skill: a combat skill to use on them too
	std::string g_group_invite;       // --group-invite: whom to invite
	bool g_group_accept = false;      // --group-accept
	std::string g_raid_invite;        // --raid-invite: whom to invite
	bool g_raid_accept = false;       // --raid-accept
	uint64_t g_skill_at = 0;          // when it's next ready

	// --attack: the nearest NPC whose name starts with g_attack, targeted, auto attack on.
	void StartAttack(ZoneSession &zone)
	{
		const ZoneProfile &me = zone.Profile();
		const ZoneSpawn *best = nullptr;
		float best_d = 0;
		for (const auto &[id, s] : zone.Spawns()) {
			if (s.npc != 1 || s.name.compare(0, g_attack.size(), g_attack) != 0)
				continue;
			float d = std::hypot(s.x - me.x, s.y - me.y);
			if (!best || d < best_d)
				best = &s, best_d = d;
		}
		if (!best) {
			printf("[attack] no NPC named %s*\n", g_attack.c_str());
			return;
		}
		g_attack_id = best->id;
		printf("[attack] %s (%u), level %u, %.0f away: target, auto attack on\n", best->DisplayName().c_str(), best->id,
			best->level, best_d);
		zone.SetTarget(best->id);
		zone.SendAutoAttack(true);
	}

	// Walk at it (30 units a second at most, as running does), and face it.
	void FollowAttackTarget(ZoneSession &zone)
	{
		auto it = zone.Spawns().find(g_attack_id);
		if (it == zone.Spawns().end())
			return;
		const ZoneSpawn &t = it->second;
		const ZoneProfile &me = zone.Profile();
		float dx = t.x - me.x, dy = t.y - me.y;
		float d = std::hypot(dx, dy);
		ZoneMovement m;
		m.x = me.x, m.y = me.y, m.z = me.z;
		if (d > 6) {
			float step = std::min(3.0f, d - 5) / d;
			m.x += dx * step, m.y += dy * step, m.z = t.z;
			m.speed = 0.7f;
		}
		float heading = std::atan2(dx, dy) * 512.0f / (2 * 3.14159265f);
		m.heading = heading < 0 ? heading + 512 : heading;
		zone.SetMovement(m);
		// --skill: OP_CombatAbility {target, 0, 100, skill} as DoMissileHit sends it, every 9 s in reach.
		if (g_skill >= 0 && d <= 10 && NowMs() >= g_skill_at) {
			g_skill_at = NowMs() + 9000;
			uint8_t body[12] = {};
			uint32_t attack = 100, skill = static_cast<uint32_t>(g_skill);
			memcpy(body, &g_attack_id, 2);
			memcpy(body + 4, &attack, 4);
			memcpy(body + 8, &skill, 4);
			printf("[attack] using skill %d\n", g_skill);
			zone.SendSkill(ZoneOp::CombatAbility, body, sizeof(body));
		}
	}

	float GroundDistance(const ZoneSession &zone, const ZoneGroundItem &item)
	{
		const ZoneProfile &me = zone.Profile();
		float dx = item.x - me.x, dy = item.y - me.y, dz = item.z - me.z;
		return std::sqrt(dx * dx + dy * dy + dz * dz);
	}

	// --pickup: OP_ClickObject for a ground item however far away it is (the real client only sends
	// it within 20), to see whether the zone checks the range.
	void DoPickup(ZoneSession &zone)
	{
		const ZoneGroundItem *target = nullptr;
		printf("[pickup] %zu ground items:\n", zone.GroundItems().size());
		for (const auto &[drop_id, item] : zone.GroundItems()) {
			float distance = GroundDistance(zone, item);
			printf("[pickup]   drop %u item %u type %u %-16s %7.1f away  (%.0f, %.0f, %.0f)\n", drop_id, item.item_id,
				item.object_type, item.model.c_str(), distance, item.x, item.y, item.z);
			if (g_pickup == "far") {
				if (item.object_type <= 1 && (!target || distance > GroundDistance(zone, *target)))
					target = &item;
			}
			else if (drop_id == static_cast<uint32_t>(strtoul(g_pickup.c_str(), nullptr, 10)))
				target = &item;
		}
		if (!target) {
			printf("[pickup] nothing to pick up for '%s'\n", g_pickup.c_str());
			return;
		}
		printf("[pickup] picking up drop %u (item %u) from %.1f away\n", target->drop_id, target->item_id,
			GroundDistance(zone, *target));
		zone.PickUp(target->drop_id);
	}

	void PrintItems(const ZoneSession &zone)
	{
		for (const auto &[slot, item] : zone.Items())
			printf("[items] %3d %s%s\n", slot, item.name.c_str(),
				Inventory::Stacks(item) ? (" x" + std::to_string(Inventory::Count(item))).c_str() : "");
	}

	// --move's FROM:TO[:N] and "auto", through core/inventory.h as the windows do them.
	void DoMoves(ZoneSession &zone)
	{
		for (const std::string &move : g_moves) {
			if (move == "auto") {
				printf("[items] auto inventory: %s\n", Inventory::AutoInventory(zone) ? "put away" : "no room");
				continue;
			}
			int from = 0, to = 0, count = 0;
			int parts = sscanf(move.c_str(), "%d:%d:%d", &from, &to, &count);
			if (parts < 2) {
				fprintf(stderr, "[items] bad --move %s\n", move.c_str());
				continue;
			}
			if (parts == 3) {
				printf("[items] taking %d from %d\n", count, from);
				Inventory::TakeFromStack(zone, static_cast<int16_t>(from), count);
				continue;
			}
			uint32_t message = 0;
			bool moved = Inventory::Move(zone, static_cast<int16_t>(from), static_cast<int16_t>(to), message);
			printf("[items] %d -> %d: %s%s\n", from, to, moved ? "moved" : "not moved",
				message ? (std::string(" (") + Inventory::MessageText(message) + ")").c_str() : "");
		}
	}

	bool RunZone(const ZoneHandoff &handoff, int seconds, const std::string &say, bool verbose, uint32_t zone_to = 0,
		const LoginResult *login = nullptr, const WorldOptions *world = nullptr, int click_door = -1)
	{
		ZoneSession zone;
		zone.SetLogger([](const std::string &line) { printf("%s\n", line.c_str()); });
		zone.SetVerbose(verbose);
		zone.SetRecordDir(g_record_dir);
		if (!g_replay_path.empty()) {
			std::string error;
			if (!zone.BeginReplay(g_replay_path, error)) {
				fprintf(stderr, "[zone] %s\n", error.c_str());
				return false;
			}
		}
		else if (!zone.Begin(handoff.address, handoff.port, handoff.character))
			return false;

		uint64_t leave_at = 0;
		bool camping = false;
		size_t next_cast = 0;
		uint64_t cast_at = 0; // when the next --cast goes; 0 not yet
		uint64_t pickup_at = 0;      // when --pickup goes (the ground items come in after we're in)
		uint64_t pickup_check_at = 0; // when to look at the cursor after the zone answers it
		uint64_t invite_at = 0;       // when --group-invite goes
		uint64_t follow_at = 0;       // when --group-accept answers
		std::string inviter;
		uint64_t raid_invite_at = 0;  // when --raid-invite goes
		uint64_t raid_accept_at = 0;  // when --raid-accept answers
		std::string raid_inviter;
		size_t &next_aa = g_next_step;
		uint64_t aa_at = 0; // when the next --aa step goes
		uint64_t answer_at = 0; // when --answer answers
		ZoneSession::Event question;
		for (;;) {
			std::vector<ZoneSession::Event> events;
			zone.Pump(100, events);
			for (const auto &event : events) {
				switch (event.type) {
				case ZoneSession::Event::Type::InZone:
					PrintNearby(zone);
					if (!g_moves.empty()) {
						PrintItems(zone);
						DoMoves(zone);
						PrintItems(zone);
					}
					if (!say.empty())
						zone.Say(say);
					if (next_aa < g_aa_steps.size())
						aa_at = NowMs() + 2000;
					if (!g_attack.empty())
						StartAttack(zone);
					if (click_door >= 0) {
						printf("[zone] using door %d\n", click_door);
						zone.ClickDoor(static_cast<uint8_t>(click_door), 0xffff, 0);
					}
					if (!g_pickup.empty())
						pickup_at = NowMs() + 2000;
					if (!g_group_invite.empty())
						invite_at = NowMs() + 15000;
					if (!g_raid_invite.empty())
						raid_invite_at = NowMs() + 15000;
					if (g_click_link >= 0) {
						printf("[zone] clicking link %d\n", g_click_link);
						zone.ClickItemLink(static_cast<uint16_t>(g_click_link), "link");
					}
					if (!g_casts.empty()) {
						printf("[cast] mana %d, gems:", zone.Profile().mana);
						for (int16_t spell : zone.Profile().mem_spells)
							printf(" %d", spell);
						printf(", book:");
						for (int16_t spell : zone.Profile().spell_book)
							if (spell != -1)
								printf(" %d", spell);
						printf(", buffs:");
						for (const ZoneBuff &buff : zone.Profile().buffs)
							if (buff.type != 0)
								printf(" %d (%d ticks)", buff.spell, buff.duration);
						printf("\n");
						// A gem given a spell gets it; an empty gem gets the book's next spell first, as the book
						// would memorize it.
						int slot = 0;
						for (size_t i = 0; i < g_casts.size(); ++i) {
							int gem = g_casts[i] - 1;
							if (gem < 0 || gem >= ZoneProfile::Gems)
								continue;
							if (g_cast_spells[i] > 0) {
								if (zone.Profile().mem_spells[gem] != g_cast_spells[i]) {
									printf("[cast] memorizing spell %d into gem %d\n", g_cast_spells[i], gem + 1);
									zone.MemorizeSpell(gem, g_cast_spells[i], 1);
								}
								continue;
							}
							if (zone.Profile().mem_spells[gem] != -1)
								continue;
							while (slot < ZoneProfile::BookSlots && zone.Profile().spell_book[slot] == -1)
								++slot;
							if (slot == ZoneProfile::BookSlots)
								break;
							printf("[cast] memorizing book slot %d (spell %d) into gem %d\n", slot, zone.Profile().spell_book[slot],
								gem + 1);
							zone.MemorizeSpell(gem, zone.Profile().spell_book[slot++], 1);
						}
						cast_at = NowMs() + 3000;
					}
					if (zone_to != 0) {
						printf("[zone] asking to zone to %u\n", zone_to);
						zone.RequestZoneChange(zone_to);
						break;
					}
					printf("[zone] staying %d s, then camping\n", seconds);
					leave_at = NowMs() + static_cast<uint64_t>(seconds) * 1000;
					break;
				case ZoneSession::Event::Type::Teleport:
					// DoTeleportB to another zone (a GM's #zone, #summon...): MoveToZone with the type
					// as the reason, as eqgame does.
					if (event.teleport.zone != zone.Profile().zone_id) {
						printf("[zone] the zone sends us to zone %u (type %u)\n", event.teleport.zone, event.teleport.type);
						zone.RequestZoneChange(event.teleport.zone, static_cast<int32_t>(event.teleport.type));
					}
					break;
				case ZoneSession::Event::Type::ZoneApproved:
					printf("[zone] zone change to %d approved\n", event.value);
					break;
				case ZoneSession::Event::Type::RezzRequested:
				case ZoneSession::Event::Type::SacrificeAsked:
				case ZoneSession::Event::Type::TranslocateAsked:
					printf("[death] asked (%s)\n", event.type == ZoneSession::Event::Type::RezzRequested ? "resurrect"
					                               : event.type == ZoneSession::Event::Type::SacrificeAsked ? "sacrifice"
					                                                                                        : "translocate");
					if (!g_answer.empty()) {
						question = event;
						answer_at = NowMs() + 2000;
					}
					break;
				case ZoneSession::Event::Type::RezzCompleted:
					printf("[death] resurrect complete: action %u, zone %u\n", event.data.size() >= 228 ? event.data[224] : 0,
						event.data.size() >= 8 ? event.data[4] | event.data[5] << 8 : 0);
					break;
				case ZoneSession::Event::Type::Translocated:
					printf("[death] translocated\n");
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
				case ZoneSession::Event::Type::DoorMoved:
					printf("[zone] door %u -> state %d\n", event.id, event.value);
					break;
				case ZoneSession::Event::Type::GroundItemRemoved:
					if (g_pickup.empty())
						break;
					if (static_cast<uint32_t>(event.value) == 0xffffffff)
						printf("[pickup] the zone refused the pickup\n");
					else {
						printf("[pickup] drop %u gone (%s)\n", static_cast<uint32_t>(event.value),
							event.id ? "our pickup" : "someone else's");
						if (event.id)
							pickup_check_at = NowMs() + 1000;
					}
					break;
				case ZoneSession::Event::Type::ItemLink:
					printf("[zone] linked item %u '%s' (icon %u, slot %d)\n", zone.LinkedItem().id, zone.LinkedItem().name.c_str(),
						zone.LinkedItem().icon, zone.LinkedItem().slot);
					break;
				case ZoneSession::Event::Type::Damaged:
					if (!g_attack.empty())
						printf("[attack] damage: %u -> %u, type %u, spell %d, %d\n", event.cast.caster, event.cast.target,
							event.cast.type, event.cast.spell, event.cast.damage);
					break;
				case ZoneSession::Event::Type::Died:
					printf("[attack] death: %s (%u) by %u, type %u, %d\n", event.text.c_str(), event.cast.target,
						event.cast.caster, event.cast.type, event.cast.damage);
					zone.BecomeCorpse(event.cast.target);
					if (event.cast.target == g_attack_id) {
						zone.SendAutoAttack(false);
						g_attack_id = 0;
					}
					break;
				case ZoneSession::Event::Type::OwnAnimation:
					if (!g_attack.empty())
						printf("[attack] our animation %d\n", event.value);
					break;
				case ZoneSession::Event::Type::Message:
					if (g_click_link >= 0 || !g_casts.empty() || !g_attack.empty())
						printf("[zone] message %d: %s\n", event.value, event.text.c_str());
					break;
				case ZoneSession::Event::Type::SpellText:
					printf("[zone] spell text from %d: %s\n", event.id, event.text.c_str());
					break;
				case ZoneSession::Event::Type::CastBegan:
					if (!g_casts.empty())
						printf("[cast] %u begins casting %d, %d ms\n", event.cast.caster, event.cast.spell, event.cast.cast_ms);
					break;
				case ZoneSession::Event::Type::SpellRecast:
					printf("[cast] gem %u went off (spell %d): recast starts\n", event.id, event.value);
					break;
				case ZoneSession::Event::Type::SpellAction:
					if (!g_casts.empty())
						printf("[cast] action: %u's spell %d on %u, flags %u\n", event.cast.caster, event.cast.spell, event.cast.target,
							event.cast.flags);
					break;
				case ZoneSession::Event::Type::BuffChanged:
					printf("[buff] OP_Buff slot %u how %d: type %u spell %d level %u duration %d\n", event.id, event.value,
						event.buff.type, event.buff.spell, event.buff.level, event.buff.duration);
					break;
				case ZoneSession::Event::Type::ManaChanged:
					if (!g_casts.empty()) {
						printf("[cast] mana %d, cast over (spell %d)\n", zone.Profile().mana, event.value);
						if (next_cast < g_casts.size())
							cast_at = NowMs() + 3000;
					}
					break;
				case ZoneSession::Event::Type::GroupInvited:
					printf("[group] %s invites us (group '%s')\n", event.group.names[0].c_str(), event.group.second.c_str());
					if (g_group_accept) {
						inviter = event.group.names[0];
						follow_at = NowMs() + 2000;
					}
					break;
				case ZoneSession::Event::Type::GroupInviteCancelled:
					printf("[group] invitation cancelled: %s / %s, why %d\n", event.group.names[0].c_str(), event.group.second.c_str(),
						event.group.param);
					break;
				case ZoneSession::Event::Type::GroupFollowed:
					printf("[group] OP_GroupFollow: %s / %s\n", event.group.names[0].c_str(), event.group.second.c_str());
					break;
				case ZoneSession::Event::Type::GroupUpdated: {
					const ZoneGroup &g = event.group;
					printf("[group] update %u: you '%s', names '%s' '%s' '%s' '%s' '%s', leader '%s', param %d\n", g.action,
						g.your_name.c_str(), g.names[0].c_str(), g.names[1].c_str(), g.names[2].c_str(), g.names[3].c_str(),
						g.names[4].c_str(), g.leader.c_str(), g.param);
					break;
				}
				case ZoneSession::Event::Type::RaidInvited:
				case ZoneSession::Event::Type::AltAdvChanged: {
					const ZoneProfile &me = zone.Profile();
					printf("[aa] level %u, %d unspent, %d%% to AAs, aa exp %d/330;", me.level, me.aa_unspent,
						me.aa_percentage, me.aa_exp);
					for (int id = 1; id < 227; ++id)
						if (me.aa_ranks[id])
							printf(" %d:%u", id, me.aa_ranks[id]);
					if (event.id)
						printf("; ability %u last used %lld s ago", event.id,
							static_cast<long long>(time(nullptr) - zone.AltAdvLastUsed(event.id)));
					printf("\n");
					break;
				}
				case ZoneSession::Event::Type::RaidUpdated: {
					const ZoneRaid &r = event.raid;
					printf("[raid] %s %u: '%s' '%s' param %d, class %d level %d group leader %d\n",
						event.type == ZoneSession::Event::Type::RaidInvited ? "OP_RaidInvite" : "OP_RaidUpdate", r.action,
						r.first.c_str(), r.name.c_str(), r.param, r.class_id, r.level, r.group_leader ? 1 : 0);
					if (event.type == ZoneSession::Event::Type::RaidUpdated && r.action == 6) {
						printf("[raid]   leader '%s', id %u, loot type %u, %zu looters\n", r.leader.c_str(), r.raid_id,
							r.loot_type, r.looters.size());
						for (const auto &m : r.members)
							printf("[raid]   %s: group %d, class %d, level %d, group leader %d\n", m.name.c_str(),
								static_cast<int>(m.group), m.class_id, m.level, m.group_leader ? 1 : 0);
					}
					if (g_raid_accept && event.type == ZoneSession::Event::Type::RaidInvited && (r.action == 0 || r.action == 3)) {
						raid_inviter = r.name;
						raid_accept_at = NowMs() + 2000;
					}
					break;
				}
				case ZoneSession::Event::Type::LoggedOut:
					if (zone.Replaying())
						PrintNearby(zone);
					printf("[done] %s camped out of %s\n", handoff.character.c_str(), zone.Info().long_name.c_str());
					return true;
				case ZoneSession::Event::Type::Error:
					fprintf(stderr, "[zone] error: %s\n", event.text.c_str());
					return false;
				default:
					break;
				}
			}
			if (cast_at && NowMs() >= cast_at && next_cast < g_casts.size()) {
				cast_at = 0;
				int gem = g_casts[next_cast++] - 1;
				int spell = gem >= 0 && gem < ZoneProfile::Gems ? zone.Profile().mem_spells[gem] : -1;
				printf("[cast] casting gem %d (spell %d) on ourselves\n", gem + 1, spell);
				zone.CastSpell(gem, spell, zone.MySpawnId());
				if (g_cast_move) {
					// The zone's CastedSpellFinished finds us more than 1 off where the cast began.
					const ZoneProfile &me = zone.Profile();
					ZoneMovement m;
					m.x = me.x + 5, m.y = me.y, m.z = me.z, m.heading = me.heading;
					zone.SetMovement(m);
					printf("[cast] stepped 5 units while casting\n");
				}
			}
			if (g_attack_id != 0 && !camping)
				FollowAttackTarget(zone);
			// --group-invite waits for them to be in the zone.
			if (invite_at && NowMs() >= invite_at) {
				bool here = false;
				for (const auto &[id, spawn] : zone.Spawns())
					here |= spawn.name == g_group_invite;
				if (!here)
					invite_at = NowMs() + 1000;
			}
			if (invite_at && NowMs() >= invite_at) {
				invite_at = 0;
				printf("[group] inviting %s\n", g_group_invite.c_str());
				zone.GroupInvite(g_group_invite, false);
			}
			if (raid_invite_at && NowMs() >= raid_invite_at) {
				bool here = false;
				for (const auto &[id, spawn] : zone.Spawns())
					here |= spawn.name == g_raid_invite;
				if (!here)
					raid_invite_at = NowMs() + 1000;
			}
			if (raid_invite_at && NowMs() >= raid_invite_at) {
				raid_invite_at = 0;
				printf("[raid] inviting %s\n", g_raid_invite.c_str());
				zone.RaidCommand(3, g_raid_invite, zone.Profile().name, 0); // CreateInviteRaid, not in a raid
			}
			if (raid_accept_at && NowMs() >= raid_accept_at) {
				raid_accept_at = 0;
				printf("[raid] accepting %s's invitation\n", raid_inviter.c_str());
				zone.RaidCommand(4, raid_inviter, zone.Profile().name, 0); // SendInviteResponse
			}
			if (follow_at && NowMs() >= follow_at) {
				follow_at = 0;
				printf("[group] accepting %s's invitation\n", inviter.c_str());
				zone.GroupFollow(inviter);
			}
			if (answer_at && NowMs() >= answer_at) {
				answer_at = 0;
				bool yes = g_answer == "yes";
				printf("[death] answering %s\n", yes ? "yes" : "no");
				if (question.type == ZoneSession::Event::Type::RezzRequested)
					zone.RezzAnswer(question.data, yes);
				else if (question.type == ZoneSession::Event::Type::SacrificeAsked)
					zone.SacrificeAnswer(yes);
				else
					zone.TranslocateAnswer(yes);
			}
			if (aa_at && NowMs() >= aa_at && next_aa < g_aa_steps.size()) {
				const auto &[kind, text] = g_aa_steps[next_aa++];
				if (kind == 1) {
					printf("[aa] sending '%s'\n", text.c_str());
					zone.SendAltAdv(text);
				}
				else if (kind == 3) {
					uint16_t id = 0;
					if (text == "self")
						id = zone.MySpawnId();
					else {
						float best = 1e30f;
						for (const auto &[sid, spawn] : zone.Spawns())
							if ((text == "npc" ? spawn.npc == 1 && spawn.pet_owner == 0 : spawn.name.rfind(text, 0) == 0) &&
								zone.SpawnDistance(sid) < best) {
								best = zone.SpawnDistance(sid);
								id = sid;
							}
					}
					printf("[death] targeting %s: %u\n", text.c_str(), id);
					zone.SetTarget(id, true);
				}
				else if (kind == 5) {
					std::string corpse;
					float best = 1e30f;
					std::string mine = zone.Profile().name + "'s";
					for (const auto &[sid, spawn] : zone.Spawns())
						if (spawn.npc >= 2 && spawn.name.rfind(mine, 0) == 0 && zone.SpawnDistance(sid) < best) {
							best = zone.SpawnDistance(sid);
							corpse = spawn.name;
						}
					const ZoneSpawn &me = zone.Self();
					printf("[death] /corpse %s (%.0f away)\n", corpse.c_str(), best);
					if (!corpse.empty())
						zone.SummonCorpse(corpse, me.y, me.x, me.z);
				}
				else if (kind == 4) {
					printf("[death] going home to zone %u\n", zone.Profile().bind_zone);
					zone.RequestZoneChange(zone.Profile().bind_zone, 10);
				}
				else if (kind == 2) {
					// N[:TARGET]: OP_PetCommands, TARGET a spawn id.
					uint32_t command = static_cast<uint32_t>(atoi(text.c_str()));
					size_t colon = text.find(':');
					uint16_t target = colon != std::string::npos ? static_cast<uint16_t>(atoi(text.c_str() + colon + 1)) : 0;
					printf("[pet] our pet %u; sending command %u target %u\n", zone.MyPetId(), command, target);
					zone.PetCommand(command, target);
				}
				else
					zone.Say(text);
				// After --home the rest waits for the next zone (its InZone).
				aa_at = next_aa < g_aa_steps.size() && kind != 4 ? NowMs() + 3000 : 0;
			}
			if (pickup_at && NowMs() >= pickup_at) {
				pickup_at = 0;
				DoPickup(zone);
			}
			if (pickup_check_at && NowMs() >= pickup_check_at) {
				pickup_check_at = 0;
				auto cursor = zone.Items().find(0);
				printf("[pickup] cursor: %s\n", cursor == zone.Items().end() ? "empty" : cursor->second.name.c_str());
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
	int click_door = -1;

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
		else if (arg == "--create-class")
			world.create_class = static_cast<uint8_t>(atoi(next().c_str()));
		else if (arg == "--idle")
			world.idle_seconds = atoi(next().c_str());
		else if (arg == "--zone")
			zone_seconds = atoi(next().c_str());
		else if (arg == "--say")
			say_text = next();
		else if (arg == "--aa")
			g_aa_steps.push_back({1, next()});
		else if (arg == "--say-more")
			g_aa_steps.push_back({0, next()});
		else if (arg == "--pet")
			g_aa_steps.push_back({2, next()});
		else if (arg == "--target-more")
			g_aa_steps.push_back({3, next()});
		else if (arg == "--corpse-more")
			g_aa_steps.push_back({5, ""});
		else if (arg == "--home")
			g_aa_steps.push_back({4, ""});
		else if (arg == "--answer")
			g_answer = next();
		else if (arg == "--click-door")
			click_door = atoi(next().c_str());
		else if (arg == "--cast") {
			std::string value = next();
			size_t colon = value.find(':');
			g_casts.push_back(atoi(value.c_str()));
			g_cast_spells.push_back(colon == std::string::npos ? 0 : atoi(value.c_str() + colon + 1));
		}
		else if (arg == "--cast-move")
			g_cast_move = true;
		else if (arg == "--pickup")
			g_pickup = next();
		else if (arg == "--attack")
			g_attack = next();
		else if (arg == "--skill")
			g_skill = atoi(next().c_str());
		else if (arg == "--click-link")
			g_click_link = atoi(next().c_str());
		else if (arg == "--zone-to")
			zone_to = static_cast<uint32_t>(atoi(next().c_str()));
		else if (arg == "--move")
			g_moves.push_back(next());
		else if (arg == "--group-invite")
			g_group_invite = next();
		else if (arg == "--group-accept")
			g_group_accept = true;
		else if (arg == "--raid-invite")
			g_raid_invite = next();
		else if (arg == "--raid-accept")
			g_raid_accept = true;
		else if (arg == "--record")
			g_record_dir = next();
		else if (arg == "--replay")
			g_replay_path = next();
		else if (arg == "--no-retry")
			retry = false;
		else if (arg == "-v")
			login.verbose = world.verbose = true;
		else {
			Usage(argv[0]);
			return 2;
		}
	}

	if (!g_replay_path.empty())
		return RunZone({}, zone_seconds < 0 ? 10 : zone_seconds, "", login.verbose) ? 0 : 1;
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
	return RunZone(handoff, zone_seconds, say_text, login.verbose, zone_to, &result, &world, click_door) ? 0 : 1;
}
