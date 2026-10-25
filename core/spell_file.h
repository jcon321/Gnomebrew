#pragma once

// spells_en.txt: eqgame's spell data, a line per spell of '^'-separated fields, the spell's id
// first. Of its 147 fields (EQ_Spell::EQ_Spell @004d6b40 reads them in order) we keep what the
// item display (CItemDisplayWnd::SetItem), the spellbook and the spell gems use: the name (1), the
// recast time (15, ms), AEDuration (18), the mana (19), goodEffect (71), the target type (86), the
// skill (88), the level each class gets it at (92.., WAR first; 255 they don't), the icon the book
// and the gems show (131, new_icon) and whether it can be deleted from the book (136). eqgame's
// IsValidSpellIndex takes 1..3999.

#include <cstdint>
#include <string>
#include <unordered_map>

struct SpellInfo
{
	std::string name;
	int recast_ms = 0;
	int ae_duration = 0;
	int mana = 0;
	uint8_t good_effect = 0; // 0 detrimental; the book's slot is red for those, blue for the rest
	uint8_t target_type = 0; // the gem's color (CSpellBookWnd::UpdateSpellGems)
	int skill = 0;
	uint8_t class_levels[15] = {};
	int icon = -1;           // a cell of A_SpellIcons (the book) and A_SpellGems (the gems)
	bool deletable = false;

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
