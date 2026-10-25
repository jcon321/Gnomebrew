#pragma once

// What the in-game windows' gauges and labels show (eqgame's fn_GetGaugeLabelFromEQ @0043600d and
// fn_GetLabelFromEQ @00436680), from the zone session and our target. The EQTypes so far:
//   gauges  1 HP (with our name), 2 mana, 3 stamina, 6 the target's HP (with its name), 16 our pet's
//   labels  1 our name, 2 our level, 3 our class (its title from 51), 4 our deity, 5..11 STR, STA,
//           DEX, AGI, WIS, INT, CHA (base and worn items), 12..16 the poison, disease, fire, cold
//           and magic resists, 17 and 18 our HP and max HP, 22 AC, 23 ATK, 24 and 25 our weight
//           and how much we can carry, 28 the target's name, 29 the target's HP percent, 45..59
//           our buffs' names (none yet)
// Others draw an empty gauge and keep a label's own text.
// What none of these count yet: spell effects (buffs, and items' worn effects), AAs, being drunk
// or berserk, and the skill caps (GetSkillLimit: the zone keeps our skills to them).

#include "sidl.h"

#include <cstdint>
#include <map>

class EqStr;
class SpellFile;
class ZoneSession;
struct ZoneItem;
struct ZoneProfile;
struct ZoneSpawn;

class GameValues : public SidlValues
{
public:
	const ZoneSession *zone = nullptr;
	const EqStr *eqstr = nullptr; // for the class and deity names; null: English
	const SpellFile *spells = nullptr; // for the item display's spells; null: none known
	uint16_t target = 0; // spawn id; 0 none
	bool running = true; // run mode (eqgame's @0079856d), not walking
	float heading = 0;   // ours, 0..512 (a compass item points by it)
	bool fv_rules = false, auto_identify = false; // OP_LogServer's (WorldSession)

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

	// EQPlayer_SkillUsed (@0050ba7e): the skill an ItemType is used with; 28 (hand to hand) for
	// what isn't a weapon.
	static int SkillUsed(int item_type);
	// EQ_Equipment::IsWeapon (@004cd9c2): not arrows or throwing items (27, 28), nor 43 and 44,
	// and with damage.
	static bool IsWeapon(const ZoneItem &item);

	// The rest are ours, from the zone session (0 without one).
	// EQ_Character::Skill (@004b8efd): ours (0 for one we don't have, 254 or 255), and the most a
	// worn item's skill mod adds to it, in percent, to 252. Not the racial innates (forage, hide,
	// sneak, safe fall, sense heading, swimming, tinkering at 50; an iksar's swimming at 100).
	int Skill(int skill) const;
	// EQ_Character::AntiTwinkAdj (@004bdbab): an item's value, scaled down by our level below its
	// recommended level, or else by our skill (its SkillUsed) below its recommended skill; never
	// under 1.
	int AntiTwinkAdj(const ZoneItem &item, int value) const;
	// A stat (5..11, as the labels number them) with what worn items add (AntiTwinkAdj) and the
	// food and drink we'd eat next (FoodAndDrink), 1..255, and its base. AGI (EQ_Character::Agi
	// @004bb5d8) is first brought down by encum_factor.
	int Stat(int eq_type, int &base) const;
	// EQ_Character::save_poison, save_disease, save_fire, save_cold and save_magic (@004bae34,
	// @004bac4f, @004ba8c1, @004baa95, @004ba710): a resist (12..16, as the labels number them),
	// to 500, and its base (basesave_*: by race, and a monk's disease and poison by level from 51).
	// What worn items add (AntiTwinkAdj), the food and drink we'd eat next, a warrior's half level
	// to magic, and the classes' bonuses: a monk's fire, a ranger's fire and cold, a beastlord's
	// cold and disease, a paladin's and a shadow knight's disease, a rogue's and a shadow knight's
	// poison, more each level from 50. Not the Resistant Discipline's (an active discipline of 30,
	// which we don't keep yet).
	int Resist(int eq_type, int &base) const;
	// EQ_Character::cur_encumbrance (@004bbb78): what the cursor, worn and general slots and the
	// bags in them weigh (less a bag's weight reduction), and the coins, a fortieth of a pound
	// each (less what the best of items 17201..17230 in a general slot takes off them).
	int Weight() const;
	// EQ_Character::encum_factor (@004bb9c7): what AGI is multiplied by. A half under 20% of our
	// HP and 50 HP, three quarters under 33% and 100 (from level 4); less half of what we carry
	// over our STR, over our STR; never under 0, and all of it in the Bazaar (zone 151).
	double EncumFactor() const;
	// EQ_Main_CalcDefense (@004712bc): the inventory window's AC, (EQ_Character::ac + compute_defense)
	// * 1000 / 847. ac (old_ac @004b98a7, without the soft caps): the worn items' AC (AntiTwinkAdj),
	// four thirds of it for all but the casters (11..14), with the class's bonus (GetClassACBonus
	// @004b961b: a monk's by weight, a rogue's and a beastlord's by AGI, an iksar's by level), a
	// third of defense (half for the casters) and AGI over 70 / 20. compute_defense (@004b9d19):
	// defense * 400 / 225 and AGI's bonus (defense_agility_bonus @004b9c7d), at least 1.
	int Ac() const;
	// CalcATKValue (@0043655a): the inventory window's ATK, (offense + compute_tohit) * 1000 / 744,
	// with the primary's skill (hand to hand without a weapon there). compute_tohit (@004b9b96):
	// offense (the skill) + 7 and the weapon's skill. offense (@004b9dda): the weapon's skill,
	// (STR * 2 - 150) / 3 for STR over 74 (DEX for archery and throwing), and a ranger's level * 4
	// - 216 above 54.
	int Atk() const;

	// EQ_Character::KunarkClass (@004bc06a): a class's name, or its title from level 51.
	static std::string ClassTitle(const EqStr *eqstr, int class_id, int level);
	// CEverQuest's deity description (@0052ea51): Agnostic for anything but 201..216.
	static std::string DeityName(const EqStr *eqstr, int deity);
};
