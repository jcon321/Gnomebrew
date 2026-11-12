#pragma once

// What eqgame makes of a spell's packets (ZoneCast), whoever casts it:
//   OP_BeginCast  CEverQuest_StartCasting (@0052cbe7): "%1 begins to cast a spell." (eqstr 12206, in
//                 color 0x120) for someone else, "You begin casting %1." (12205, 0x108) for us, a
//                 bard's song by its texts instead ("%1%2", 12207, with other_casts); our rogue's
//                 poisons say nothing. A GM sees the spell's name after it, and the cast time after
//                 their own. A dragon breath (spells.eff effects 61..64) plays the caster's animation
//                 at once and says nothing, and its effect goes off a second later (for 1.25
//                 seconds). Then the casting effect (DoSpellEffect stage 0, for the cast time and
//                 the casting animation's length), the SpellsNew.eff one or the spells.eff one.
//   OP_Action     EQPlayer::IWasHit -> TriggerSpellEffect (@0050b27c), on the target: a spell with a
//                 target type plays the caster's CastingAnim (not a bard's song without an
//                 instrument, instrument_mod 10 or less, nor a dragon breath), then its effect on
//                 the target (DoSpellEffect stage 2); a summoning's (its first SPA 33, 106 or 108;
//                 71 at the caster) on the spot `range` in front of the target too.
//   OP_Damage     msg_successful_hit -> CEverQuest_ReportSuccessfulHit (@005297d2), type 231: on us,
//                 cast_on_you, or "%1  You have taken %2 points of damage." (12203, 12204 for one)
//                 with the damage; on anyone else, their name and cast_on_other; in color 0x108,
//                 its first letter made upper case. Someone else's on someone else is only told
//                 within 75 units of the target. Then the target's TargetAnim, standing (stances 100
//                 and 102) and not a corpse; a detrimental spell's damage without one plays 13 (D02).
// The begin casting line and the spell lines from the zone go by /filter's spell switches as
// SpellTextShown says; a damage line by DamageShown's.
//   OP_SpellTextMessage, OP_SpellTextMessageStringID  HandleWorldMessage's 0x41d3 and 0x4241: the
//                 zone's line about someone's spell (another's fizzle, interrupt, regained
//                 concentration), in color 0x108, only for a spawn in the zone, and as /filter's
//                 switches say (SpellTextShown).
// Not done: being hit standing us up, the hit's sounds, a tap's amount (target type 13), HitBySpell
// (our buffs: flag 4) and the fishing line (spell 0xffff).

#include "filters.h"
#include "spell_effects.h"
#include "zone_session.h"

#include <string>
#include <vector>

class EqStr;
class SpellFile;

class SpellCasting
{
public:
	// filters: /filter's switches (null: the defaults, everything shown); group: our group's spawns
	// in the zone (pinstGroup).
	SpellCasting(const SpellFile *spells, const EqStr *eqstr, const ChatFilters *filters = nullptr,
		std::vector<uint16_t> group = {})
		: m_spells(spells), m_eqstr(eqstr), m_filters(filters), m_group(std::move(group))
	{
	}

	struct Line
	{
		std::string text;
		int type; // the chat color type
	};
	struct Animation
	{
		uint16_t spawn; // MySpawnId() for us
		int id;         // CDisplay::PlayAnimationTrack's
	};
	// One of ZoneSession's CastBegan, SpellAction or Damaged events.
	void Handle(const ZoneSession::Event &event, const ZoneSession &zone, std::vector<Line> &lines,
		std::vector<Animation> &animations, std::vector<SpellEffects::Request> &effects) const;

	// ReportDeath's killing blow by a spell (an OP_Death of type 231): told as an OP_Damage is, but
	// by `told` (ReportDeath's distance) rather than msg_successful_hit's.
	void DeathBlow(const ZoneCast &cast, bool told, const ZoneSession &zone, std::vector<Line> &lines,
		std::vector<Animation> &animations) const;

	// HandleWorldMessage's 0x41d3 and 0x4241, and CEverQuest_StartCasting's line: whether the line
	// about `spawn`'s spell is told.
	// A bard's by the bard songs switch (DAT_0079965c: ours unless 3, a group member's at 0 or 2,
	// anyone else's at 0), anyone else's by the PC spells switch (DAT_00799654: ours always, a group
	// member's at 0 or 2, another player's at 0) or an NPC's by the NPC spells one (DAT_00799658 0).
	static bool SpellTextShown(const ZoneSpawn &spawn, bool ours, bool group_member, const ChatFilters &filters);
	static constexpr int SpellTextColor = 0x108;
	// ReportSuccessfulHit's spell branch (type 231): `who` is the caster, or the victim when there's
	// none. A bard's by Bard Songs: ours unless 3, anyone else's at 0 or 2 (in our group or not: the
	// group loop's answer goes unused). Anyone else's: shown when it's ours or lands on us; with the
	// caster or the victim in our group, PC Spells at 0 or 2; otherwise a player's at PC Spells 0,
	// an NPC's (or a corpse's) at NPC Spells 0.
	static bool DamageShown(const ZoneSpawn &who, bool ours, bool on_us, bool group_member, const ChatFilters &filters);

	// EQ_Character_IsRoguePoison (@004c4753): our own casting of these says nothing.
	static bool IsRoguePoison(int spell);

private:
	void BeginCast(const ZoneCast &cast, const ZoneSession &zone, std::vector<Line> &lines,
		std::vector<Animation> &animations, std::vector<SpellEffects::Request> &effects) const;
	void Action(const ZoneCast &cast, const ZoneSession &zone, std::vector<Animation> &animations,
		std::vector<SpellEffects::Request> &effects) const;
	// told: -1 msg_successful_hit's rule, else whether it's told.
	void Damaged(const ZoneCast &cast, const ZoneSession &zone, std::vector<Line> &lines,
		std::vector<Animation> &animations, int told = -1) const;
	std::string Format(uint32_t id, const std::string &fallback, const std::vector<std::string> &args) const;

	bool InGroup(uint16_t id) const;
	const ChatFilters &Filters() const;

	const SpellFile *m_spells;
	const EqStr *m_eqstr;
	const ChatFilters *m_filters;
	std::vector<uint16_t> m_group;
};
