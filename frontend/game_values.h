#pragma once

// What the in-game windows' gauges and labels show (eqgame's fn_GetGaugeLabelFromEQ @0043600d and
// fn_GetLabelFromEQ @00436680), from the zone session and our target. The EQTypes so far:
//   gauges  1 HP (with our name), 2 mana, 3 stamina, 6 the target's HP (with its name), 16 our pet's
//   labels  1 our name, 2 our level, 3 our class (its title from 51), 4 our deity, 5..11 STR, STA,
//           DEX, AGI, WIS, INT, CHA (base and worn items), 17 and 18 our HP and max HP, 28 the
//           target's name, 29 the target's HP percent, 45..59 our buffs' names (none yet)
// The resists (12..16), AC (22), ATK (23) and weight (24, 25) are eqgame's own formulas, not done
// yet: they show nothing. Others draw an empty gauge and keep a label's own text.

#include "sidl.h"

#include <cstdint>
#include <map>

class EqStr;
class ZoneSession;
struct ZoneItem;
struct ZoneProfile;
struct ZoneSpawn;

class GameValues : public SidlValues
{
public:
	const ZoneSession *zone = nullptr;
	const EqStr *eqstr = nullptr; // for the class and deity names; null: English
	uint16_t target = 0; // spawn id; 0 none
	bool running = true; // run mode (eqgame's @0079856d), not walking

	int Gauge(int eq_type, std::string &text) override;
	bool Label(int eq_type, std::string &text, uint32_t &color) override;

	const ZoneSpawn *Target() const; // null without one
	const ZoneSpawn *Pet() const;    // our pet: a living spawn we own

	// EQ_Character::Max_Mana (@004b9483): level and INT or WIS (worn items' included), plus the
	// worn items' mana. Spell effects (buffs) aren't counted yet.
	static int MaxMana(const ZoneProfile &profile, const std::map<int16_t, ZoneItem> &items);
	// The hit points worn items add (slots 1..20, Client::CalcItemBonuses), which the zone leaves
	// out of our current hit points.
	static int WornHp(const std::map<int16_t, ZoneItem> &items);
	// A stat (5..11, as the labels number them) with what worn items add, to 255, and its base.
	// eqgame's EQ_Character::Str and the others also count spells, AAs and being drunk.
	static int Stat(const ZoneProfile &profile, const std::map<int16_t, ZoneItem> &items, int eq_type, int &base);

	// EQ_Character::KunarkClass (@004bc06a): a class's name, or its title from level 51.
	static std::string ClassTitle(const EqStr *eqstr, int class_id, int level);
	// CEverQuest's deity description (@0052ea51): Agnostic for anything but 201..216.
	static std::string DeityName(const EqStr *eqstr, int deity);
};
