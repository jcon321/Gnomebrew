#pragma once

// spells_en.txt: eqgame's spell data, a line per spell of '^'-separated fields, the spell's id
// first. Of its 147 fields (EQ_Spell::EQ_Spell @004d6b40 reads them in order) we keep what the
// item display (CItemDisplayWnd::SetItem), the spellbook, the spell gems and casting use: the name (1),
// the casting texts (4..8), the range (9), the cast time (13, ms), the recovery time (14, ms), the recast
// time (15, ms), the buff's duration formula and most ticks (16, 17), AEDuration (18), the mana (19), the
// effects' base values (20..31), their maximums (32..43) and their formulas (58..69), goodEffect (71),
// Activated (72), the effects' SPAs (74..85), the target
// type (86), the skill (88), the level each class gets it at (92..106, WAR first; 255 they don't), the
// caster's and the target's animations (107, 108), how its missile travels (109), the spells.eff
// effect (110, SpellAffectIndex), the icon the book and the gems show (131, new_icon), the SpellsNew.eff effect (132), whether it can be
// deleted from the book (136) and its recourse (137, RecourseLink). eqgame's IsValidSpellIndex
// takes 1..3999.

#include <cstdint>
#include <string>
#include <unordered_map>

struct SpellInfo
{
	std::string name;
	// What the chat says (EQ_Spell +0xdc..+0xec): a bard's own song starting, someone else's (after
	// their name), the spell landing on us, on someone else (after their name), and wearing off.
	std::string you_cast, other_casts, cast_on_you, cast_on_other, fades;
	float range = 0;
	int cast_ms = 0;
	int recovery_ms = 0; // after it goes off, before any gem can be cast (FizzleTimeout)
	int recast_ms = 0;
	int ae_duration = 0;
	int mana = 0;
	// A buff's (EQ_Spell +0x20, +0x24: EQ_Character::CalculateSpellDuration): the formula, 0 not a
	// buff, and the most ticks it lasts (0 no limit).
	int duration_formula = 0, duration = 0;
	// Each effect's value (EQ_Spell +0x2e, +0x46, +0x7a: EQ_Character::CalcAffectChange): the base,
	// the most it gets to (0 no limit) and how it grows with the caster's level.
	int16_t base[12] = {}, max[12] = {};
	uint8_t formula[12] = {};
	uint8_t activated = 0; // copied into its buffs
	uint8_t good_effect = 0; // 0 detrimental; the book's slot is red for those, blue for the rest
	uint8_t first_spa = 0;   // the first of its 12 effects (EQ_Spell +0x8a)
	uint8_t spas[12] = {};   // all of them
	uint8_t target_type = 0; // the gem's color (CSpellBookWnd::UpdateSpellGems)
	int skill = 0;
	uint8_t class_levels[15] = {};
	uint8_t casting_anim = 0;  // the caster's as it lands (EQPlayer::TriggerSpellEffect): 42..44, T04..T06
	uint8_t target_anim = 0;   // the target's as it lands (CEverQuest_ReportSuccessfulHit); 0 none
	uint8_t travel_type = 0;   // EQ_Spell +0xbe: 3 a bolt's missile (EQMissile::ProcessMissile), out 30 s and level
	int effect = -1;           // a spells.eff record, 0..255 (EQ_Spell +0xf4); -1 none
	int new_effect = 0;        // a SpellsNew.eff record (EQ_Spell +0xfc); 0 none, which falls back on `effect`
	int icon = -1;           // a cell of A_SpellIcons (the book) and A_SpellGems (the gems)
	bool deletable = false;
	int recourse = 0;        // the spell it gives the caster when it lands (EQ_Spell +0xc4); 0 none

	bool HasSpa(int spa) const; // EQ_Spell::SpellAffectIndex: any of its effects
	// The level a class (1 WAR .. 15 BST) gets it at; 255 never.
	int ClassLevel(int class_id) const { return class_id >= 1 && class_id <= 15 ? class_levels[class_id - 1] : 255; }
};

class SpellFile
{
public:
	bool Load(const std::string &path);
	const SpellInfo *Find(int id) const; // null outside 1..3999 or not in the file

private:
	std::unordered_map<int, SpellInfo> m_spells;
};
