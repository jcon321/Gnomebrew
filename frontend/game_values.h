#pragma once

// What the in-game windows' gauges and labels show (eqgame's fn_GetGaugeLabelFromEQ @0043600d and
// fn_GetLabelFromEQ @00436680), from the zone session and our target. The EQTypes so far:
//   gauges  1 HP (with our name), 2 mana, 3 stamina, 6 the target's HP (with its name), 16 our pet's
//   labels  1 our name, 2 our level, 28 the target's name, 29 the target's HP percent, 45..59 our
//           buffs' names (none yet)
// Others draw an empty gauge and keep a label's own text.

#include "sidl.h"

#include <cstdint>
#include <map>

class ZoneSession;
struct ZoneItem;
struct ZoneProfile;
struct ZoneSpawn;

class GameValues : public SidlValues
{
public:
	const ZoneSession *zone = nullptr;
	uint16_t target = 0; // spawn id; 0 none

	int Gauge(int eq_type, std::string &text) override;
	bool Label(int eq_type, std::string &text) override;

	const ZoneSpawn *Target() const; // null without one
	const ZoneSpawn *Pet() const;    // our pet: a living spawn we own

	// EQ_Character::Max_Mana (@004b9483): level and INT or WIS (worn items' included), plus the
	// worn items' mana. Spell effects (buffs) aren't counted yet.
	static int MaxMana(const ZoneProfile &profile, const std::map<int16_t, ZoneItem> &items);
	// The hit points worn items add (slots 1..20, Client::CalcItemBonuses), which the zone leaves
	// out of our current hit points.
	static int WornHp(const std::map<int16_t, ZoneItem> &items);
};
