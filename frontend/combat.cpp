#include "combat.h"

#include "actor_sound.h"
#include "actor_sound_table.h"
#include "chat_text.h"

#include <cctype>
#include <cmath>

namespace
{
	constexpr uint8_t SpellType = 231;
	// The chat color types of hits, misses and deaths.
	constexpr int MyHits = 0x109, HitsOnMe = 0x10a, MyMisses = 0x10b, MissesOnMe = 0x10c, OthersHits = 0x117,
	              OthersMisses = 0x118, MyDeath = 0x115, OtherDeath = 0x116;
	// /filter (CEverQuest_loadOptions' defaults): DAT_00798c0c, c10, c14, c18, and DAT_00799670.
	constexpr bool ShowMyMisses = true, ShowOthersMisses = false, ShowOthersHits = true, ShowMissesOnMe = true;
	constexpr bool HideDamageShields = false;
	// EqSoundManager's waves.
	constexpr int FallHit = 115, SwordBlock = 120, ShieldBlock = 125, StaffBlock = 132, FleshHit1 = 147,
	              FleshHit2 = 148;
	constexpr int NoAnimation = -1;

	const ZoneSpawn *FindSpawn(const ZoneSession &zone, uint16_t id)
	{
		if (id == 0)
			return nullptr;
		if (id == zone.MySpawnId())
			return &zone.Self();
		auto it = zone.Spawns().find(id);
		return it != zone.Spawns().end() ? &it->second : nullptr;
	}

	// CEverQuest::stripName: the clean name (digits and '#' gone, '_' a space) up to an apostrophe,
	// so a corpse is named as it was.
	std::string StripName(const std::string &display)
	{
		return display.substr(0, display.find('\''));
	}
	std::string StripName(const ZoneSpawn &spawn) { return StripName(spawn.DisplayName()); }

	// Where a spawn is (the server's x, y, z), ours as we last moved.
	void Position(const ZoneSession &zone, const ZoneSpawn &spawn, float &x, float &y, float &z)
	{
		if (&spawn == &zone.Self()) {
			const ZoneProfile &me = zone.Profile();
			x = me.x, y = me.y, z = me.z;
			return;
		}
		x = spawn.x, y = spawn.y, z = spawn.z;
	}

	audio::ActorSounds SoundsOf(const ZoneSpawn &spawn)
	{
		return audio::SoundsForActor(spawn.race, spawn.gender, spawn.body_texture, spawn.size > 0 ? spawn.size : 6);
	}
}

std::string Combat::Text(uint32_t id, const std::string &fallback, const std::vector<std::string> &args) const
{
	return FormatEqString(m_eqstr, id, fallback, args);
}

int Combat::Rand()
{
	m_random = m_random * 214013u + 2531011u;
	return static_cast<int>((m_random >> 16) & 0x7fff);
}

uint32_t Combat::RaceVerb(int race, bool plural)
{
	enum Verb { Maul, Bite, Claw, Gore, Sting, Slice, Smash, Rend, Slash, Default };
	Verb verb = Default;
	switch (race) {
	case 43: case 177: case 305:
		verb = Maul;
		break;
	case 22: case 24: case 34: case 36: case 37: case 38: case 42: case 61: case 74: case 83: case 84: case 87: case 91:
	case 102: case 113: case 120: case 148: case 171: case 176: case 194: case 222: case 223: case 326: case 327:
		verb = Bite;
		break;
	case 14: case 23: case 47: case 49: case 50: case 63: case 76: case 111: case 119: case 158: case 184: case 196:
	case 200: case 221: case 304:
		verb = Claw;
		break;
	case 107: case 135: case 169: case 191:
		verb = Gore;
		break;
	case 79: case 109: case 129: case 134: case 172:
		verb = Sting;
		break;
	case 85: case 105: case 174:
		verb = Slice;
		break;
	case 220: verb = Smash; break;
	case 229: verb = Rend; break;
	case 199: verb = Slash; break;
	default: break;
	}
	switch (verb) {
	case Maul: return plural ? 12165 : 12163;
	case Bite: return plural ? 12166 : 12164;
	case Claw: return plural ? 12168 : 12167;
	case Gore: return plural ? 12170 : 12169;
	case Sting: return plural ? 12172 : 12171;
	case Slice: return plural ? 12173 : 12174;
	case Smash: return plural ? 12176 : 12175;
	case Rend: return plural ? 12178 : 12177;
	case Slash: return plural ? 12180 : 12179;
	default:
		// The player races punch; the rest hit.
		if (race < 13 || race == 128)
			return plural ? 12182 : 12181;
		return plural ? 12184 : 12183;
	}
}

Combat::Report Combat::Damaged(const ZoneCast &hit, const ZoneSession &zone, uint16_t target, int32_t my_hp)
{
	Report report;
	if (hit.type == SpellType)
		return report;
	uint16_t me = zone.MySpawnId();
	bool told = true;
	if (hit.caster == me || hit.target == me) {
		// On us from someone else: its damage off our hit points at once.
		if (hit.target == me && hit.caster != me && hit.damage > 0)
			report.my_damage = hit.damage;
	}
	else if (const ZoneSpawn *victim = FindSpawn(zone, hit.target)) {
		// CDisplay::PlayerSimpleDistance from us.
		float mx, my, mz, vx, vy, vz;
		Position(zone, zone.Self(), mx, my, mz);
		Position(zone, *victim, vx, vy, vz);
		told = std::sqrt((vx - mx) * (vx - mx) + (vy - my) * (vy - my) + (vz - mz) * (vz - mz)) <= 75;
	}
	ReportHit(hit, told, zone, target, my_hp, report);
	return report;
}

void Combat::SpellHurt(uint16_t victim, int32_t damage, const ZoneSession &zone, Report &report)
{
	if (damage > 0)
		Hurt(victim, damage, NoAnimation, zone, report);
}

void Combat::ReportHit(const ZoneCast &hit, bool told, const ZoneSession &zone, uint16_t target, int32_t my_hp,
	Report &report)
{
	uint16_t me = zone.MySpawnId();
	const ZoneSpawn *victim = FindSpawn(zone, hit.target);
	if (!victim)
		return;
	const ZoneSpawn *attacker = nullptr;
	if (hit.caster != 0) {
		attacker = FindSpawn(zone, hit.caster);
		if (!attacker)
			return;
	}
	report.victim = hit.target;
	const uint8_t type = hit.type;
	const int32_t damage = hit.damage;

	// Who's who, and the line's color.
	std::string attacker_name, victim_name;
	bool attacker_me = false, victim_me = false;
	int color;
	if (type < 232 && me != 0 && hit.caster == me) {
		color = MyHits;
		attacker_name = Text(12185, "You");
		attacker_me = true;
		if (hit.caster == hit.target) {
			victim_name = Text(12186, "yourself");
			victim_me = true;
			color = HitsOnMe;
		}
		else
			victim_name = StripName(*victim);
	}
	else if (me != 0 && hit.target == me) {
		if (attacker) {
			attacker_name = StripName(*attacker);
			// Someone hitting us with no target of ours becomes it.
			if (target == 0 && attacker->body_type < 65)
				report.target = hit.caster;
		}
		victim_name = Text(12187, "YOU");
		victim_me = true;
		color = HitsOnMe;
	}
	else {
		color = OthersHits;
		if (type < SpellType && hit.caster == hit.target) {
			attacker_name = StripName(*attacker);
			victim_name = attacker->gender == 0   ? Text(12188, "himself")
			              : attacker->gender == 1 ? Text(12189, "herself")
			                                      : Text(12190, "itself");
		}
		else {
			if (attacker)
				attacker_name = StripName(*attacker);
			victim_name = StripName(*victim);
		}
	}
	// The verb, or the environment's whole line.
	std::string verb, text;
	int animation = NoAnimation;
	const bool plural = !attacker_me && damage >= 1;
	auto pick = [&](uint32_t one, const char *one_text, uint32_t more, const char *more_text) {
		verb = plural ? Text(more, more_text) : Text(one, one_text);
	};
	if (type < SpellType) {
		switch (type) {
		case 0: case 2: pick(12191, "crush", 12192, "crushes"); break;      // blunt
		case 1: case 3: pick(12179, "slash", 12180, "slashes"); break;      // slashing
		case 7: case 51: pick(12183, "hit", 12184, "hits"); break;          // archery, throwing
		case 8: pick(12199, "backstab", 12200, "backstabs"); break;
		case 10: pick(12201, "bash", 12202, "bashes"); break;
		case 21: case 23: case 52: pick(12197, "strike", 12198, "strikes"); break; // dragon punch, eagle strike, tiger claw
		case 26: case 30: case 38: case 71: pick(12195, "kick", 12196, "kicks"); break; // flying, kick, round, intimidation
		case 36: pick(12193, "pierce", 12194, "pierces"); break;
		default: { // hand to hand and the rest: the attacker's race's
			uint32_t id = RaceVerb(attacker ? attacker->race : 0, plural);
			verb = Text(id, "hit");
			break;
		}
		}
	}
	else {
		// The environment's, said of the victim (DAT_00799670 hides others' damage shields).
		auto said = [&](uint32_t mine, const char *mine_text, uint32_t theirs, const char *theirs_text) {
			if (victim_me)
				text = Text(mine, mine_text, {victim_name});
			else if (!HideDamageShields)
				text = Text(theirs, theirs_text, {victim_name});
		};
		switch (type) {
		case 244: animation = 12; said(5722, "YOU are rotting!", 5723, "%1 begins to decay!"); break;
		case 245: animation = 12; said(12139, "YOU are chilled to the bone!", 12140, "%1 was chilled to the bone."); break;
		case 246: animation = 12; said(12141, "YOU are freezing!", 12142, "%1 is freezing!"); break;
		case 247: animation = 12; said(12137, "YOU are tormented!", 12138, "%1 was tormented."); break;
		case 248: animation = 12; said(12135, "YOU are burned!", 12136, "%1 was burned."); break;
		case 249: animation = 12; said(12133, "YOU are pierced by thorns!", 12134, "%1 was pierced by thorns."); break;
		case 250: animation = 15; said(12131, "YOU are burning!", 12132, "%1 is burning."); break;
		case 251: {
			// Drowning; in zone 171 (DAT_007f9488, set from the profile) suffocating.
			animation = 15;
			bool no_air = zone.Profile().zone_id == 171;
			if (victim_me)
				text = no_air ? Text(12127, "YOU are suffocating!") : Text(12128, "YOU are drowning!");
			else
				text = no_air ? Text(12129, "%1 is suffocating.", {victim_name}) : Text(12130, "%1 is drowning.", {victim_name});
			break;
		}
		case 252: // a fall
			animation = 14;
			text = victim_me ? Text(12118, "%1 were injured by falling.", {victim_name})
			                 : Text(12119, "%1 was injured by falling.", {victim_name});
			report.sounds.push_back({hit.target, FallHit});
			break;
		case 253: { // a trap, its kind by the spell field
			animation = 14;
			if (!victim_me) {
				text = Text(12120, "%1 was injured by a trap.", {victim_name});
				break;
			}
			int kind = hit.spell;
			bool crushed = kind == 140 || kind == 145 || kind == 146 || kind == 150 || kind == 151 || kind == 152;
			if (crushed) {
				report.sounds.push_back({hit.target, FallHit});
				text = Text(12125, "YOU were crushed.");
			}
			else if (kind == 115 || kind == 116)
				text = Text(12121, "YOU were sliced by a saw blade.");
			else if (kind == 120 || kind == 125)
				text = Text(12122, "YOU were skewered by a spear.");
			else if (kind == 130)
				text = Text(12123, "YOU were sliced by a pendulum.");
			else if (kind == 135)
				text = Text(12124, "YOU were slashed by a blade.");
			else
				text = Text(12126, "YOU were injured by a trap.");
			break;
		}
		default:
			break;
		}
	}
	if (attacker_name.empty())
		attacker_name = "Pain and suffering";

	int line_color = color;
	if (type < SpellType) {
		if (damage < 1) {
			// A miss, or how it was stopped.
			line_color = victim_me ? MissesOnMe : attacker_me ? MyMisses : OthersMisses;
			std::string but;
			auto stopped = [&](uint32_t mine, const char *mine_text, uint32_t theirs, const char *theirs_text) {
				but = victim_me ? Text(mine, mine_text, {victim_name}) : Text(theirs, theirs_text, {victim_name});
			};
			switch (damage) {
			case -6:
				stopped(12154, "YOUR magical skin absorbs the blow", 12155, "%1's magical skin absorbs the blow");
				report.sounds.push_back({hit.target, StaffBlock});
				break;
			case -5: stopped(12152, "%1 are INVULNERABLE", 12153, "%1 is INVULNERABLE"); break;
			case -4: stopped(12150, "%1 dodge", 12151, "%1 dodges"); break;
			case -3: case -2: case -1: {
				if (damage == -3)
					stopped(12148, "%1 riposte", 12149, "%1 ripostes");
				else if (damage == -2)
					stopped(12146, "%1 parry", 12147, "%1 parries");
				else
					stopped(12144, "%1 block", 12145, "%1 blocks");
				if (attacker) {
					// CEverQuest_GetSoundEffectForRiposte: blade on blade, a shield half the time.
					int sound = StaffBlock;
					if (ActorSound::ItemType(attacker->equipment[7]) == 2 && ActorSound::ItemType(victim->equipment[7]) == 2)
						sound = SwordBlock;
					if (ActorSound::ItemType(victim->equipment[8]) == 3 && Rand() % 2 == 0)
						sound = ShieldBlock;
					report.sounds.push_back({hit.target, sound});
				}
				break;
			}
			default:
				but = attacker_me ? Text(12156, "miss") : Text(12157, "misses");
				break;
			}
			if (victim_me && !ShowMissesOnMe)
				told = false;
			if (attacker_me ? !ShowMyMisses : !victim_me && !ShowOthersMisses)
				told = false;
			text = attacker_me ? Text(12158, "%1 try to %2 %3, but %4!", {attacker_name, verb, victim_name, but})
			                   : Text(12159, "%1 tries to %2 %3, but %4!", {attacker_name, verb, victim_name, but});
		}
		else {
			line_color = victim_me ? HitsOnMe : attacker_me ? MyHits : OthersHits;
			// A hard hit on a race with a flesh hit sound.
			audio::ActorSounds sounds = SoundsOf(*victim);
			int first = sounds.slot[audio::ActorSounds::GetHit1];
			if (first > 31 && first < 90) {
				int hp = victim_me ? my_hp : victim->level * 30;
				report.sounds.push_back({hit.target, hp / damage < 10 ? FleshHit2 : FleshHit1});
			}
			if (!victim_me && !attacker_me && !ShowOthersHits)
				told = false;
			else {
				std::string points = damage < 2 ? Text(12161, "for %1 point of damage.", {std::to_string(damage)})
				                                 : Text(12160, "for %1 points of damage.", {std::to_string(damage)});
				text = Text(12162, "%1 %2 %3 %4", {attacker_name, verb, victim_name, points});
				if (attacker && attacker == victim && (victim->npc == 1 || victim->npc == 3)) // an NPC's on itself
					told = false;
			}
		}
	}
	else {
		if (damage == 0)
			animation = NoAnimation;
		line_color = victim_me ? HitsOnMe : OthersHits;
		if (damage == 0)
			text.clear();
	}
	if (told && !text.empty()) {
		text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
		report.lines.push_back({text, line_color});
	}
	if (damage > 0)
		Hurt(hit.target, damage, animation == NoAnimation ? 13 : animation, zone, report); // D02 unless the environment's
}

void Combat::Hurt(uint16_t id, int32_t damage, int animation, const ZoneSession &zone, Report &report)
{
	const ZoneSpawn *victim = FindSpawn(zone, id);
	if (!victim || victim->npc > 1 || damage <= 0)
		return; // a corpse
	if (id == zone.MySpawnId())
		report.hurt_me = true;
	report.victim = id;
	report.animation = animation;
	// One of the race's four get-hit sounds.
	audio::ActorSounds sounds = SoundsOf(*victim);
	int sound = sounds.slot[audio::ActorSounds::GetHit1 + Rand() % 4];
	if (sound != 0)
		report.sounds.push_back({id, sound});
}

Combat::Report Combat::Died(const ZoneSession::Event &died, const ZoneSession &zone, uint16_t target, int32_t my_hp)
{
	Report report;
	const ZoneCast &d = died.cast;
	uint16_t me = zone.MySpawnId();
	const ZoneSpawn *victim = FindSpawn(zone, d.target);
	if (!victim)
		return report;
	const ZoneSpawn *killer = FindSpawn(zone, d.caster);
	std::string victim_name = StripName(died.text.empty() ? victim->DisplayName() : died.text);
	// Told within 50 of the victim (CDisplay::PlayerDistance, across the ground) unless we're in it.
	bool told = true;
	if (d.caster != me && d.target != me) {
		float mx, my, mz, vx, vy, vz;
		Position(zone, zone.Self(), mx, my, mz);
		Position(zone, *victim, vx, vy, vz);
		told = std::sqrt((vx - mx) * (vx - mx) + (vy - my) * (vy - my)) <= 50;
	}
	report.told = told;
	// The killing blow (a spell's is SpellCasting's).
	if (d.type != SpellType)
		ReportHit(d, told, zone, target, my_hp, report);

	int8_t how = static_cast<int8_t>(d.type);
	std::string text;
	if (d.target == me) {
		if (how == -6)
			text = Text(12105, "You have burned to death!");
		else if (how == -5)
			text = Text(12104, "You have choked to death, unable to breathe!");
		else if (how == -4)
			text = Text(12103, "You have fallen to your death!");
		else if (d.caster != 0)
			text = killer ? Text(12107, "You have been slain by %1!", {StripName(*killer)})
			              : Text(12108, "You have been slain by an unseen hand!");
		else
			text = Text(12106, "You died.");
		report.lines.push_back({text, MyDeath});
		return report;
	}
	if (how == -6)
		text = Text(12111, "%1 has burned to death!", {victim_name});
	else if (how == -5)
		text = Text(12110, "%1 has choked to death, unable to breathe!", {victim_name});
	else if (how == -4)
		text = Text(12109, "%1 has fallen to their death!", {victim_name});
	else if (d.caster == 0)
		text = Text(12112, "%1 died.", {victim_name});
	else if (d.caster == me)
		text = Text(12113, "You have slain %1!", {victim_name});
	else if (!killer)
		text = Text(12115, "%1 has been slain by an unseen hand!", {victim_name});
	else if (killer == victim && (victim->npc == 1 || victim->npc == 3))
		return report; // an NPC that killed itself
	else
		text = Text(12114, "%1 has been slain by %2!", {victim_name, StripName(*killer)});
	if (told)
		report.lines.push_back({text, OtherDeath});
	return report;
}
