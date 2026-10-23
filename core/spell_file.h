#pragma once

// spells_en.txt: eqgame's spell data, a line per spell of '^'-separated fields, the spell's id
// first. Of its 147 fields we keep what the item display shows (CItemDisplayWnd::SetItem): the
// name (1), the mana (19), the skill (88) and the level each class gets it at (92.., WAR first;
// 255 they don't). eqgame's IsValidSpellIndex takes 1..3999.

#include <cstdint>
#include <string>
#include <unordered_map>

struct SpellInfo
{
	std::string name;
	int mana = 0;
	int skill = 0;
	uint8_t class_levels[15] = {};
};

class SpellFile
{
public:
	bool Load(const std::string &path);
	const SpellInfo *Find(int id) const; // null outside 1..3999 or not in the file

private:
	std::unordered_map<int, SpellInfo> m_spells;
};
