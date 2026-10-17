#pragma once

// Character creation rules, as eqgame.exe applies them in CCharacterCreation:
// which classes a race can be (EQ_Character_CheckClass @004bc730), starting
// stats and class bonus points (EQ_Character_ClassMin @004bcdf1,
// EQ_Character::BaseAttr @004bc677), deities (EQ_Character::CanWorship
// @004bc786), starting cities (EQ_Character_CityCanStart @004bcbe9), the start
// zone (EQ_PC::InitPlayerStart @004cea24) and random names (CreateName @0050585c).
// The tables are built in (creation_rules.cpp), taken from TAKP's eqgame.exe.

#include <cstdint>
#include <random>
#include <string>
#include <vector>

class CreationRules
{
public:
	// Button order on the creation screens (CCharacterCreation's constructor): alphabetical.
	static constexpr int RaceCount = 14, ClassCount = 15, DeityCount = 17, CityCount = 14, StatCount = 7;
	static const uint16_t RaceButtons[RaceCount]; // race ids
	static const uint8_t ClassButtons[ClassCount]; // class ids
	static const uint16_t Deities[DeityCount];     // deity id per CC1_Deity button (SetNewPCDeityFromBtnIndex)

	// The stat rows on the screen (STRENGTH, STAMINA, AGILITY, DEXTERITY, WISDOM, INTELLIGENCE, CHARISMA).
	enum Stat { Str, Sta, Agi, Dex, Wis, Int, Cha };

	// Like the exe, Erudite shadow knights, necromancers and Cazic Thule need paineel.s3d installed.
	void CheckInstall(const std::string &eq_dir);

	// OP_LogServer: the ruleset (u32 at 4; 2 and 4 are the team PvP servers) and whether names
	// can't have four consonants in a row (u32 at 12).
	void SetServerType(uint32_t type) { m_server_type = type; }
	void SetStrictNames(bool strict) { m_strict_names = strict; }

	bool CanBeClass(uint16_t race, uint8_t class_id) const;
	int RaceBase(uint16_t race, Stat stat) const;     // the race's starting value
	int ClassBonus(uint8_t class_id, Stat stat) const; // points the class adds (taken from the 50 to spend)
	bool CanWorship(uint16_t race, uint8_t class_id, int deity_button) const;
	bool CityCanStart(int city, uint16_t race, int deity_button, uint8_t class_id) const;
	uint32_t StartZone(uint16_t race, uint8_t class_id, int city, uint16_t deity) const;
	std::string RandomName(uint16_t race, uint8_t gender, std::mt19937 &random) const;
	// CCharacterCreation_GetCharCreateText @0040ccf2: the eqstr_en.txt strings that make up the
	// last screen's welcome text, joined in order (the first '@' becomes "<BR><BR>").
	static std::vector<uint32_t> WelcomeText(uint16_t race, uint8_t class_id, int deity_button, int city);
	// CEverQuest_IsValidName: 4 to 15 characters, no spaces.
	static bool IsValidName(const std::string &name);

private:
	bool m_paineel = false;
	uint32_t m_server_type = 0;
	bool m_strict_names = false;
};
