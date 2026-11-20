#pragma once

// What eqgame makes of melee and of deaths, whoever's in them (ZoneSession's Damaged and Died
// events); a spell's OP_Damage is SpellCasting's.
//   OP_Damage  msg_successful_hit (@004e0907) -> CEverQuest_ReportSuccessfulHit (@005297d2): told
//              only within 75 units of the victim unless we're in it. On us from someone else it
//              takes its damage off our hit points at once (ModifyCurHP).
//              The text: "%1 %2 %3 %4" (12162) for a hit, the attacker, the verb, the victim and
//              "for %1 points of damage." (12160, 12161 for one); "%1 try to %2 %3, but %4!" (12158;
//              12159 "tries" for someone else) for a miss, %4 "miss" or a defence by its damage
//              (-1 block, -2 parry, -3 riposte, -4 dodge, -5 INVULNERABLE, -6 magical skin). We're
//              "You" attacking, "YOU" hit, "yourself" hitting ourselves; someone hitting themselves
//              is "himself", "herself" or "itself". The verb is the skill's (crush, slash, hit,
//              backstab, bash, strike, kick, pierce) or, bare handed and for the rest, the race's
//              (SetDefaultDamageDescByRace: maul, bite, claw, gore, sting, slice, smash, rend, slash,
//              punch or hit), "s" ending it for someone else's hit. Its colors: 0x109 our hits,
//              0x10a hits on us, 0x117 others', 0x10b our misses, 0x10c misses of us, 0x118 others'.
//              The environment's types: 244 rotting, 245 chilled, 246 freezing, 247 tormented, 248
//              burned, 249 thorns, 250 burning, 251 drowning (suffocating in zone 171), 252 falling,
//              253 a trap (its kind by the spell field), told only with damage.
//              Someone hitting us with no target of ours becomes it (a body type under 65).
//              A hit (damage over 0) on anyone not a corpse: the victim's animation (D02, 13; 12 for
//              the elements, 15 burning or drowning, 14 a fall or trap) and one of its race's four
//              get-hit sounds; a hard melee hit on a race whose first get-hit sound is 32..89 a flesh
//              hit (FlshHit2 when it took a tenth or more of the victim's hit points, else FlshHit1).
//              On us it closes the spellbook and stands us up from sitting or kneeling.
//              A block, parry or riposte clinks (GetSoundEffectForRiposte: SwordBlk blade on blade,
//              ShldBlk half the time from a shield, else StaffBlk); magical skin StaffBlk; a fall or
//              a crushing trap fall_hit.
//   OP_Death   CEverQuest::ReportDeath (@00528e16): the killing blow told as a hit (within 50 units
//              unless we're in it), then "You have slain %1!" (12113), "%1 has been slain by %2!"
//              (12114), "You have been slain by %1!" (12107), "%1 died." (12112), falling (-4),
//              choking (-5), burning (-6) and the unseen hand's (no killer we know): 0x115 our death,
//              0x116 the rest (told as the blow is). An NPC killing itself says nothing.
// The hit modes (the chat window menu's Hit Modes, pinstCChatManager's +0x1d8.., the UI ini's
// [ChatManager] HitMode0..5) say how a melee hit or miss is told (HandleMyHitsMode @00500f86 and the
// five after it): 0 the whole line, 1 abbreviated, 2 the damage alone ("miss", 12156, for a miss), any
// other not at all; the whole line still goes to the log. Abbreviated: our hits "%1 %2 for %3" (2130:
// the verb, the victim, the damage), hits on us "%1 %2 for %3" (2108: the attacker, the verb), others'
// "%1 %2 %3 for %4" (2128); our misses "missed %1" (15039, the victim), misses of us "%1 missed"
// (15040), others' "%1 missed %2" (15041). The environment's and spells' lines aren't. /filter's
// switches (ChatFilters) say which misses and hits are told, and whether others' damage shields are.
// A melee hit on us (EQPlayer::IWasHit) takes off our invisibility (SPA 12, 28, 29 buffs) and pushes
// us along the hit's heading by its force.
// Not done: the blood spurt of our hits (SpurtBloodOnDag), hide and sneak, a duel's end, the group
// window's member HP on a death, and the tracking window.

#include "filters.h"
#include "zone_session.h"

#include <cstdint>
#include <string>
#include <vector>

class EqStr;

class Combat
{
public:
	explicit Combat(const EqStr *eqstr = nullptr) : m_eqstr(eqstr) {}
	void SetStrings(const EqStr *eqstr) { m_eqstr = eqstr; }
	void SetFilters(const ChatFilters *filters) { m_filters = filters; } // /filter's (unset: the defaults)

	struct Line
	{
		std::string text;
		int type; // the chat color type
		// A melee hit or miss: which hit mode it's told by (0 our hits, 1 our misses, 2 hits on us,
		// 3 misses of us, 4 others' hits, 5 others' misses; -1 none), and its parts.
		int hit_mode = -1;
		std::string attacker, verb, victim;
		int32_t damage = 0;
	};
	struct Sound
	{
		uint16_t spawn; // MySpawnId() for us
		int wave;       // EqSoundManager's slot, played from the spawn (EQPlayer::PlaySoundA)
	};
	struct Report
	{
		std::vector<Line> lines;
		std::vector<Sound> sounds;
		uint16_t victim = 0;
		int animation = -1;   // the victim's (PlayAnimationTrack), -1 none
		uint16_t target = 0;  // the attacker becomes our target (we had none); 0 not
		bool hurt_me = false; // we took damage: the book closes, sitting or kneeling stands us up
		bool close_book = false; // msg_successful_hit's for someone else's spell on us: only the book closes
		// EQPlayer::IWasHit on us (a melee hit): our invisibility, hide and sneak go, and its push.
		bool hit_me = false;
		float force = 0, heading = 0, pitch = 0;
		int32_t my_damage = 0; // what to take off our hit points now (ModifyCurHP)
		bool told = true;      // Died: whether the killing blow is told (for a spell's, SpellCasting's)
	};

	// msg_successful_hit of a Damaged event that isn't a spell's. `target`: ours, 0 none.
	// `my_hp`: our hit points (for the flesh hit's sound).
	Report Damaged(const ZoneCast &hit, const ZoneSession &zone, uint16_t target, int32_t my_hp);
	// ReportDeath's text of a Died event (the killing blow, then the death).
	Report Died(const ZoneSession::Event &died, const ZoneSession &zone, uint16_t target, int32_t my_hp);
	// A spell's damage (over 0, detrimental) on `victim`: the end of ReportSuccessfulHit, its sounds
	// and our standing up (its animation is SpellCasting's).
	void SpellHurt(uint16_t victim, int32_t damage, const ZoneSession &zone, Report &report);

	// What a hit mode (`mode`) makes of a melee line: false when it isn't told.
	bool HitModeText(const Line &line, int mode, std::string &text) const;

	// SetDefaultDamageDescByRace: the verb a race attacks with bare handed (eqstr id).
	static uint32_t RaceVerb(int race, bool plural);

private:
	// CEverQuest_ReportSuccessfulHit of anything but a spell.
	void ReportHit(const ZoneCast &hit, bool told, const ZoneSession &zone, uint16_t target, int32_t my_hp,
		Report &report);
	// LAB_0052ad13: the hit's sounds and animation on the victim, and what it does to us.
	void Hurt(uint16_t victim, int32_t damage, int animation, const ZoneSession &zone, Report &report);
	std::string Text(uint32_t id, const std::string &fallback, const std::vector<std::string> &args = {}) const;
	int Rand(); // RandNbr

	const EqStr *m_eqstr;
	const ChatFilters *m_filters = nullptr;
	uint32_t m_random = 0x2545f491u;
};
