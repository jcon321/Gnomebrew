#pragma once

// What eqgame knows of each skill (__SkillDict, filled by InitializeSkillData @004d0e88): the exe's
// own table, extracted by running the decompiled function.

#include <cstdint>

struct ZoneProfile;

namespace Skills
{
	constexpr int Count = 74;
	// The skill's kind (its byte at +10): a passive one, an ability used from the abilities or combat
	// page (bind wound, disarm, feign death, mend, intimidation, berserking, taunt), or a combat skill
	// on the combat timer (backstab, bash, the kicks and the monk strikes).
	enum Type : uint8_t { Passive = 0, Ability = 1, Combat = 2 };
	constexpr int Classes = 15;

	struct Info
	{
		uint32_t name;      // eqstr id
		uint32_t reuse_ms;  // its reuse timer (+4)
		uint8_t activated;  // used from a button (+9): the skill select window lists it
		uint8_t type;       // Type (+10)
		int16_t combat;     // +0x8c: DoAttack's "a combat skill" (1 and up); its base damage
		uint8_t level[Classes]; // the level each class (1..15) gets it at (+0xd..)
		uint16_t cap[Classes];  // each class's cap (+0x4e..); 0 the class never has it
	};
	const Info &Get(int skill); // out of range: a zeroed one

	// EQ_Character::GetInherentRacialSkillLevel_or_IHaveSkill (@004b9fac): a race's own skill, 50
	// (an Iksar's swimming 100), else 0.
	int RacialLevel(uint16_t race, int skill);
	// EQ_Character::HasSkill (@004b9f08): our value of a skill we have (1 for one at 0), the race's
	// own level for one we haven't (254 and 255 are "can't"), or 0.
	int HasSkill(const ZoneProfile &me, int skill);

	// The cap each class (1..15) reaches with an expansion on (__SkillDict +0x98..): where
	// InitializeSkillData sets none, the same as Info::cap.
	int ExpansionCap(int skill, int cls);
	// GetSkillLimit2 (@004ef9e6): our class's cap, the expansion one with any of the profile's
	// expansion bits (0x1f) set.
	int SkillLimit(const ZoneProfile &me, int skill);
	// setskill (@004d0808): our value of a skill is at the most our level lets it be. The trade
	// skills (+0xb clear) stop at 250, or 200 with more of the others over 200 than Trade Skill
	// Mastery allows; the rest by a level formula each, capped as EQ_PC_GetKunarkThreshold does
	// past level 50 with an expansion, else by our class's cap.
	bool AtLevelCap(const ZoneProfile &me, int skill);
	// EQ_PC_AtSkillLimit? (@004ef2aa): the trade skills a guildmaster teaches no further than 20.
	bool FieldTradeSkill(int skill);
	// EQ_PC_checkLang (@004ef555): our race may learn a language (0..24) at our level, with the
	// class exceptions (shadow knights and necromancers the dark languages, and the rest).
	bool CanLearnLanguage(const ZoneProfile &me, int language);
}
