#pragma once

// What eqgame makes of a spell's packets (ZoneCast), whoever casts it:
//   OP_BeginCast  CEverQuest_StartCasting (@0052cbe7): "%1 begins to cast a spell." (eqstr 12206, in
//                 color 0x120) for someone else, "You begin casting %1." (12205, 0x108) for us, a
//                 bard's song by its texts instead ("%1%2", 12207, with other_casts); our rogue's
//                 poisons say nothing. A GM sees the spell's name after it, and the cast time after
//                 their own. A dragon breath (spells.eff effects 61..64) plays the caster's animation
//                 at once and says nothing. Then the casting effect (DoSpellEffect stage 0, for the
//                 cast time and the casting animation's length).
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
// The chat filters for spells (PC spells, NPC spells, bard songs) aren't there yet: everything shows.
// Not done: being hit standing us up, the hit's sounds, a tap's amount (target type 13), HitBySpell
// (our buffs: flag 4), the fishing line (spell 0xffff), and the spells.eff effects (spells without a
// SpellsNew.eff one).

#include "spell_effects.h"
#include "zone_session.h"

#include <string>
#include <vector>

class EqStr;
class SpellFile;

class SpellCasting
{
public:
	SpellCasting(const SpellFile *spells, const EqStr *eqstr) : m_spells(spells), m_eqstr(eqstr) {}

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

	// EQ_Character_IsRoguePoison (@004c4753): our own casting of these says nothing.
	static bool IsRoguePoison(int spell);

private:
	void BeginCast(const ZoneCast &cast, const ZoneSession &zone, std::vector<Line> &lines,
		std::vector<Animation> &animations, std::vector<SpellEffects::Request> &effects) const;
	void Action(const ZoneCast &cast, const ZoneSession &zone, std::vector<Animation> &animations,
		std::vector<SpellEffects::Request> &effects) const;
	void Damaged(const ZoneCast &cast, const ZoneSession &zone, std::vector<Line> &lines,
		std::vector<Animation> &animations) const;
	std::string Format(uint32_t id, const std::string &fallback, const std::vector<std::string> &args) const;

	const SpellFile *m_spells;
	const EqStr *m_eqstr;
};
