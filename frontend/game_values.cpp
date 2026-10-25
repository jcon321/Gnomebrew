#include "game_values.h"
#include "eqstr.h"
#include "inventory.h"
#include "spell_book.h"
#include "world_client.h"
#include "zone_session.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr int FirstWornSlot = 1, LastWornSlot = 20; // ear .. waist; not the ammo slot (21)

	// The worn, common items' values of one stat.
	template <typename Field>
	int WornSum(const std::map<int16_t, ZoneItem> &items, Field field)
	{
		int sum = 0;
		for (const auto &[slot, item] : items)
			if (slot >= FirstWornSlot && slot <= LastWornSlot && item.item_class == 0)
				sum += item.*field;
		return sum;
	}

	// EQ_Character::GetAttributeModifierFromItems (@004ba538)'s, for food (14) and drink (15): a
	// value of the first one of item_type in the general slots, in order, each bag's contents
	// after it, even a 0; what we'd eat or drink next.
	int FirstOfType(const std::map<int16_t, ZoneItem> &items, int item_type, int8_t ZoneItem::*field)
	{
		for (int16_t slot = Inventory::FirstGeneral; slot <= Inventory::LastGeneral; ++slot) {
			auto it = items.find(slot);
			if (it == items.end())
				continue;
			const ZoneItem &item = it->second;
			if (item.item_class == 0 && item.item_type == item_type)
				return item.*field;
			if (item.item_class != 1)
				continue;
			for (int n = 0; n < item.bag_slots; ++n) {
				auto in = items.find(Inventory::BagSlot(slot, n));
				if (in != items.end() && in->second.item_class == 0 && in->second.item_type == item_type)
					return in->second.*field;
			}
		}
		return 0;
	}

	int FoodAndDrink(const std::map<int16_t, ZoneItem> &items, int8_t ZoneItem::*field)
	{
		return FirstOfType(items, 14, field) + FirstOfType(items, 15, field);
	}

	// gauge values are 0..1000
	int Permille(int64_t value, int64_t max)
	{
		return max > 0 ? static_cast<int>(std::clamp<int64_t>(value * 1000 / max, 0, 1000)) : 0;
	}

	// getComparisonColor (@004365e9): a value above what it's compared with green, below it red,
	// the same grey.
	uint32_t ComparisonColor(int value, int base)
	{
		return value == base ? 0xc0c0c0 : value > base ? 0x00ff00 : 0xff0000;
	}

	std::string EqText(const EqStr *eqstr, uint32_t id, const std::string &fallback)
	{
		return eqstr && eqstr->Has(id) ? eqstr->Format(id, {}) : fallback;
	}

	// KunarkClass's eqstr ids for classes 1..15: the class, its titles at 51, 55 and 60 (the next
	// three), and at 65. Rangers' and shadow knights' are the other way round from their numbers.
	constexpr uint32_t ClassNameIds[15] = {1502, 1506, 1510, 1518, 1514, 1522, 1526, 1530, 1534, 1538, 1542, 1546, 1550,
		1554, 1558};
	constexpr uint32_t ClassTitle65Ids[15] = {5755, 5756, 5757, 5759, 5758, 5760, 5761, 5762, 5763, 5764, 5765, 5766,
		5767, 5768, 5769};
	// Merchant and the GMs' classes, 16..32 (32 is 1485, the rest 1486 on, but 20 and 21 swapped).
	constexpr uint32_t OtherClassIds[17] = {1486, 1487, 1488, 1489, 1491, 1490, 1492, 1493, 1494, 1495, 1496, 1497, 1498,
		1499, 1500, 1501, 1485};

	// The deities, 201..216 (Bertoxxulous .. Veeshan).
	constexpr uint32_t DeityNameIds[16] = {3251, 3262, 3253, 3256, 3252, 3254, 3255, 3257, 3259, 3260, 3266, 3258, 3261,
		3263, 3264, 3265};
}

std::string GameValues::ClassTitle(const EqStr *eqstr, int class_id, int level)
{
	if (class_id >= 1 && class_id <= 15) {
		std::string fallback = ClassName(static_cast<uint8_t>(class_id));
		uint32_t id = ClassNameIds[class_id - 1];
		if (level > 50) {
			if (level >= 65)
				return EqText(eqstr, ClassTitle65Ids[class_id - 1], fallback);
			id += level < 55 ? 1 : level < 60 ? 2 : 3;
		}
		return EqText(eqstr, id, fallback);
	}
	// Above 50, only the player classes have a name.
	if (class_id >= 16 && class_id <= 32 && level <= 50)
		return EqText(eqstr, OtherClassIds[class_id - 16], "Unknown");
	return EqText(eqstr, 1484, "Unknown");
}

std::string GameValues::DeityName(const EqStr *eqstr, int deity)
{
	if (deity >= 201 && deity <= 216)
		return EqText(eqstr, DeityNameIds[deity - 201], "");
	return EqText(eqstr, 3250, "Agnostic");
}

int GameValues::SkillUsed(int item_type)
{
	switch (item_type) {
	case 0: return 1;
	case 1: return 3;
	case 2: return 36;
	case 3: return 0;
	case 4: return 2;
	case 5: case 6: case 17: case 27: case 28: return 7;
	case 7: case 19: return 51;
	case 9: return 231;
	case 10: return 15;
	case 35: return 36;
	default: return 28;
	}
}

bool GameValues::IsWeapon(const ZoneItem &item)
{
	int t = item.item_type;
	return t != 27 && t != 28 && t != 43 && t != 44 && item.damage != 0;
}

int GameValues::Skill(int skill) const
{
	if (!zone || skill < 0 || skill >= static_cast<int>(std::size(zone->Profile().skills)))
		return 0;
	int value = zone->Profile().skills[skill];
	if (value < 0 || value >= 254)
		return 0;
	if (value < 252) {
		// the ammo slot's counts too
		int mod = 0;
		for (const auto &[slot, item] : zone->Items())
			if (slot >= FirstWornSlot && slot <= Inventory::Ammo && item.item_class == 0 && item.skill_mod_type == skill)
				mod = std::max<int>(mod, item.skill_mod_value);
		value = std::min(value + mod * value / 100, 252);
	}
	return value;
}

int GameValues::AntiTwinkAdj(const ZoneItem &item, int value) const
{
	if (!zone || value == 0)
		return value;
	const ZoneProfile &me = zone->Profile();
	double scaled;
	if (item.rec_level != 0 && me.level < item.rec_level)
		scaled = std::floor(value * (static_cast<double>(me.level) / item.rec_level) + 0.5);
	else {
		if (item.rec_skill == 0)
			return value;
		int skill = SkillUsed(item.item_type);
		if (Skill(skill) >= item.rec_skill)
			return value;
		// by the skill as it is, not as Skill has it
		int have = skill < static_cast<int>(std::size(me.skills)) ? me.skills[skill] : 0;
		scaled = std::floor(value * (static_cast<double>(std::max(have, 1)) / item.rec_skill) + 0.5);
	}
	return std::max(static_cast<int>(scaled), 1);
}

int GameValues::Stat(int eq_type, int &base) const
{
	base = 0;
	if (!zone)
		return 0;
	const ZoneProfile &p = zone->Profile();
	int8_t ZoneItem::*field;
	switch (eq_type) {
	case 5: base = p.str; field = &ZoneItem::str; break;
	case 6: base = p.sta; field = &ZoneItem::sta; break;
	case 7: base = p.dex; field = &ZoneItem::dex; break;
	case 8: base = p.agi; field = &ZoneItem::agi; break;
	case 9: base = p.wis; field = &ZoneItem::wis; break;
	case 10: base = p.int_; field = &ZoneItem::int_; break;
	case 11: base = p.cha; field = &ZoneItem::cha; break;
	default: return 0;
	}
	int value = base;
	for (const auto &[slot, item] : zone->Items())
		if (slot >= FirstWornSlot && slot <= LastWornSlot && item.item_class == 0)
			value += AntiTwinkAdj(item, item.*field);
	value += FoodAndDrink(zone->Items(), field);
	if (eq_type == 8)
		value = static_cast<int>(value * EncumFactor());
	// CalculateMaxStatAA2 without AAs: 1..255.
	return std::clamp(value, 1, 255);
}

int GameValues::Resist(int eq_type, int &base) const
{
	base = 0;
	if (!zone)
		return 0;
	const ZoneProfile &me = zone->Profile();
	int level = me.level, c = me.class_id, race = me.race;
	// how much a class's bonus grows from level 50: one more a level
	auto from50 = [level](int bonus, int at50) { return level > 49 ? level - 50 + at50 : bonus; };
	int8_t ZoneItem::*field;
	int bonus = 0;
	switch (eq_type) {
	case 12: // poison
		base = race == 11 || race == 8 ? 20 : 15;
		if (c == 7 && level > 50)
			base += level - 50;
		field = &ZoneItem::pr;
		if (c == 9) bonus = from50(8, 9);
		if (c == 5) bonus = from50(4, 5);
		break;
	case 13: // disease
		base = race == 11 ? 20 : race == 3 ? 10 : 15;
		if (c == 7 && level > 50)
			base += level - 50;
		field = &ZoneItem::dr;
		if (c == 3) bonus = from50(8, 9);
		if (c == 15 || c == 5) bonus = from50(4, 5);
		break;
	case 14: // fire
		base = race == 9 ? 5 : 25;
		if (race == 128)
			base += 5;
		field = &ZoneItem::fr;
		if (c == 7) bonus = from50(8, 9);
		if (c == 4) bonus = from50(4, 5);
		break;
	case 15: // cold
		base = race == 2 ? 35 : 25;
		if (race == 128)
			base -= 10;
		field = &ZoneItem::cr;
		if (c == 4 || c == 15) bonus = from50(4, 5);
		break;
	case 16: // magic
		base = race == 3 || race == 8 ? 30 : 25;
		field = &ZoneItem::mr;
		if (c == 1) bonus = level / 2;
		break;
	default: return 0;
	}
	int value = base;
	for (const auto &[slot, item] : zone->Items())
		if (slot >= FirstWornSlot && slot <= LastWornSlot && item.item_class == 0)
			value += AntiTwinkAdj(item, item.*field);
	value += FoodAndDrink(zone->Items(), field) + bonus;
	// CalculateMaxStatAA2: no more than 500; unlike the stats, no least
	return std::min(value, 500);
}

int GameValues::Weight() const
{
	if (!zone)
		return 0;
	const auto &items = zone->Items();
	// eqgame's float constants, added up in doubles
	const double tenth = 0.1f, hundredth = 0.01f;
	double weight = 0;
	auto add = [&](int16_t slot) {
		auto it = items.find(slot);
		if (it == items.end())
			return;
		const ZoneItem &item = it->second;
		weight += item.weight * tenth;
		if (item.item_class != 1)
			return;
		for (int n = 0; n < item.bag_slots; ++n) {
			auto in = items.find(Inventory::BagSlot(slot, n));
			if (in != items.end())
				weight += (100.0 - item.bag_wr) * hundredth * (in->second.weight * tenth);
		}
	};
	add(Inventory::Cursor);
	for (int16_t slot = FirstWornSlot; slot <= Inventory::Ammo; ++slot)
		add(slot);
	int reduction = 0;
	for (int16_t slot = Inventory::FirstGeneral; slot <= Inventory::LastGeneral; ++slot) {
		add(slot);
		// 17201..17210, 17211..17220 and 17221..17230: 10% off the coins' weight .. all of it
		auto it = items.find(slot);
		if (it != items.end() && it->second.id >= 17201 && it->second.id <= 17230)
			reduction = std::max(reduction, ((it->second.id - 17201) % 10 + 1) * 10);
	}
	// what's on the person; coins on the cursor count too, but we don't keep those
	const ZoneProfile &p = zone->Profile();
	double coins = static_cast<double>(p.platinum + p.gold + p.silver + p.copper) * 0.025f;
	if (reduction > 0)
		coins = (100 - reduction) * coins * hundredth;
	return static_cast<int>(std::clamp(weight + coins, 0.0, 32766.0));
}

double GameValues::EncumFactor() const
{
	if (!zone)
		return 1;
	const ZoneProfile &me = zone->Profile();
	float factor = 1;
	float hp = static_cast<float>(zone->BaseHp() + WornHp(zone->Items()));
	float max = static_cast<float>(zone->MaxHp());
	if (hp < max && me.level > 3) {
		float part = hp / max;
		if (part < 0.2 && hp < 50.0)
			factor = 0.5f;
		else if (part < 0.33 && hp < 100.0)
			factor = 0.75f;
	}
	int base = 0;
	int str = Stat(5, base), weight = Weight();
	if (str >= weight || str == 0)
		return factor;
	if (me.zone_id == 151) // the Bazaar
		return 1;
	return std::max(factor - static_cast<double>(weight - str) / (static_cast<double>(str) + str), 0.0);
}

namespace
{
	// GetClassACBonus (@004b961b)'s multiplier for a rogue's or beastlord's AGI from 80.
	int AgiTimes(int agi)
	{
		return agi < 80 ? 1 : agi < 85 ? 2 : agi < 90 ? 3 : agi < 100 ? 4 : 5;
	}
}

int GameValues::Ac() const
{
	if (!zone)
		return 0;
	const ZoneProfile &me = zone->Profile();
	int level = me.level, c = me.class_id;
	bool caster = c >= 11 && c <= 14;
	int base = 0;
	int agi = Stat(8, base);

	// old_ac
	int ac = 0;
	for (const auto &[slot, item] : zone->Items())
		if (slot >= FirstWornSlot && slot <= LastWornSlot && item.item_class == 0)
			ac += AntiTwinkAdj(item, item.ac);
	if (!caster)
		ac = ac * 4 / 3;
	int bonus = 0;
	if (c == 7) { // monk: by what we carry, from a weight that goes up with our level
		int cap = 30, soft = 14;
		if (level > 14) cap = 32, soft = 15;
		if (level > 29) cap = 34, soft = 16;
		if (level > 44) cap = 36, soft = 17;
		if (level > 50) cap = 38, soft = 18;
		if (level > 54) cap = 40, soft = 20;
		if (level > 59) cap = 45, soft = 24;
		if (level > 61) cap = 47;
		if (level > 63) cap = 50;
		if (level > 64) cap = 53;
		int weight = Weight();
		double full = level + 5;
		if (weight < cap - 1) {
			double value = full;
			if (weight > soft) {
				double part = std::min((weight - soft) * 6.66667, 100.0);
				value = std::max((100.0 - part) * 0.01 * full, 0.0);
			}
			bonus = static_cast<int>(value * 4.0 * (1.0 / 3));
		}
		else if (weight > cap + 1) {
			double part = std::min((weight - (cap - 10.0)) * 0.01, 1.0);
			bonus -= static_cast<int>(full * 4.0 * (1.0 / 3) * part);
		}
	}
	else if (c == 9) { // rogue
		if (agi > 75 && level > 29)
			bonus = std::min((level - 26) * AgiTimes(agi) / 4, 12);
	}
	else if (c == 15) { // beastlord
		if (level > 10)
			bonus = std::min((level - 6) * AgiTimes(agi) / 5, 16);
	}
	if (me.race == 128) // iksar
		bonus += std::clamp(level, 10, 35);
	ac = std::max(ac + static_cast<int16_t>(bonus), 0);
	int defense = Skill(15);
	ac += caster ? defense / 2 : defense / 3;
	if (agi > 70)
		ac += agi / 20;
	ac = static_cast<int16_t>(std::max(ac, 0));

	// compute_defense, with defense_agility_bonus
	int agi_bonus = 0;
	if (agi < 40)
		agi_bonus = (agi - 40) * 25 / 40;
	else if (agi >= 61 && agi < 75)
		agi_bonus = (28 - static_cast<int16_t>(200 - agi) / 5) * 2 / 3;
	else if (agi >= 75) {
		int most = level < 7 ? 35 : level < 20 ? 55 : level < 40 ? 70 : 80;
		agi_bonus = (most - std::max(200 - agi, 0) / 5) * 2 / 3;
	}
	int compute_defense = std::max(agi_bonus + static_cast<int16_t>(std::max(defense, 0) * 400 / 225), 1);

	return (ac + compute_defense) * 1000 / 847;
}

int GameValues::Atk() const
{
	if (!zone)
		return 0;
	const ZoneProfile &me = zone->Profile();
	int skill = 28; // hand to hand
	auto primary = zone->Items().find(Inventory::Primary);
	if (primary != zone->Items().end() && primary->second.item_class == 0 && IsWeapon(primary->second))
		skill = SkillUsed(primary->second.item_type);

	int tohit = std::max(Skill(33) + 7 + Skill(skill), 1);

	int base = 0;
	int stat = skill == 7 || skill == 51 ? Stat(7, base) : Stat(5, base);
	int offense = Skill(skill);
	if (stat > 74)
		offense += (stat * 2 - 150) / 3;
	if (me.class_id == 4 && me.level > 54) // ranger
		offense += me.level * 4 - 216;
	offense = std::max(offense, 1);

	return (offense + tohit) * 1000 / 744;
}

int GameValues::WornHp(const std::map<int16_t, ZoneItem> &items)
{
	return WornSum(items, &ZoneItem::hp);
}

int GameValues::MaxMana(const ZoneProfile &p, const std::map<int16_t, ZoneItem> &items)
{
	// Warriors, monks, rogues, berserkers and merchants have none; paladins, rangers, shadow
	// knights and beastlords none of their own before level 9.
	int level = p.level;
	switch (p.class_id) {
	case 1: case 7: case 9: case 16: case 32:
		return 0;
	case 3: case 4: case 5: case 15:
		if (level - 8 < 1)
			level = 0;
		break;
	default:
		break;
	}
	// Clerics, paladins, rangers, druids, shamans and beastlords cast from WIS; the rest from INT.
	bool wisdom = p.class_id == 2 || p.class_id == 3 || p.class_id == 4 || p.class_id == 6 || p.class_id == 10 || p.class_id == 15;
	int stat = wisdom ? p.wis + WornSum(items, &ZoneItem::wis) : p.int_ + WornSum(items, &ZoneItem::int_);
	stat = std::clamp(stat, 0, 255);
	if (stat > 200)
		stat = (stat - 200) / 2 + 200;
	if (stat - 100 > 0)
		stat += (stat - 100) * 3 / 2;
	int mana = level * 15 + static_cast<int16_t>(level * 15) * stat / 200 + WornSum(items, &ZoneItem::mana);
	return std::max(static_cast<int16_t>(mana), static_cast<int16_t>(0));
}

const ZoneSpawn *GameValues::Target() const
{
	if (!zone || !target)
		return nullptr;
	auto it = zone->Spawns().find(target);
	return it != zone->Spawns().end() ? &it->second : nullptr;
}

const ZoneSpawn *GameValues::Pet() const
{
	uint16_t mine = zone ? zone->MySpawnId() : 0;
	if (mine == 0)
		return nullptr;
	for (const auto &[id, spawn] : zone->Spawns())
		if (spawn.pet_owner == mine && spawn.npc < 2)
			return &spawn;
	return nullptr;
}

int GameValues::Gauge(int eq_type, std::string &text)
{
	if (!zone)
		return -2;
	const ZoneProfile &me = zone->Profile();
	switch (eq_type) {
	case 1: { // HP, "Name Surname" on it
		text = me.last_name.empty() ? me.name : me.name + " " + me.last_name;
		int max = zone->MaxHp();
		if (max <= 0)
			return 1000; // not heard yet
		return Permille(zone->BaseHp() + WornHp(zone->Items()), max);
	}
	case 2: { // mana; none for those who never have any
		int max = MaxMana(me, zone->Items());
		if (max > 0)
			return Permille(std::clamp<int>(me.mana, 0, max), max);
		uint16_t c = me.class_id;
		bool caster = c >= 2 && (c <= 6 || c == 8 || (c >= 10 && c <= 15));
		return caster ? 0 : -1;
	}
	case 3: // stamina: fatigue 0..100 counts down from full
		return me.fatigue <= 0 ? 1000 : 1000 - std::min<int>(me.fatigue, 100) * 10;
	case 6: { // the target's HP and name; a corpse has an empty bar
		const ZoneSpawn *spawn = Target();
		if (!spawn)
			return -1;
		text = spawn->DisplayName();
		return spawn->npc < 2 ? Permille(spawn->hp, 100) : -3;
	}
	case 16: { // our pet's HP and name
		const ZoneSpawn *pet = Pet();
		if (!pet)
			return -3;
		text = pet->DisplayName();
		return Permille(pet->hp, 100);
	}
	case 9: // GetSpellMemTicksLeft
		return spell_book ? spell_book->MemorizeGauge() : 0;
	case 10: // GetSpellScribeTicksLeft
		return spell_book ? spell_book->ScribeGauge() : 0;
	case 11: case 12: case 13: case 14: case 15: // group members (no group yet)
		return -1;
	default:
		return -3;
	}
}

bool GameValues::Label(int eq_type, std::string &text, uint32_t &color)
{
	if (!zone)
		return false;
	const ZoneSpawn *spawn = Target();
	const ZoneProfile &me = zone->Profile();
	switch (eq_type) {
	case 1:
		text = me.name;
		return true;
	case 2:
		text = std::to_string(me.level);
		return true;
	case 3:
		text = ClassTitle(eqstr, me.class_id, me.level);
		return true;
	case 4:
		text = DeityName(eqstr, me.deity);
		return true;
	case 5: case 6: case 7: case 8: case 9: case 10: case 11: { // colored against the base
		int base = 0;
		int value = Stat(eq_type, base);
		text = std::to_string(value);
		color = ComparisonColor(value, base);
		return true;
	}
	case 17: // our HP, with what worn items add (the zone leaves that out)
		text = std::to_string(zone->BaseHp() + WornHp(zone->Items()));
		return true;
	case 18: // our max HP, against itself less spell effects (none yet): grey
		text = std::to_string(zone->MaxHp());
		color = ComparisonColor(0, 0);
		return true;
	case 22: case 23: { // AC and ATK, against themselves less spell effects (none yet): grey
		int value = eq_type == 22 ? Ac() : Atk();
		text = std::to_string(value);
		color = ComparisonColor(value, value);
		return true;
	}
	case 24: { // what we carry, grey, red over our STR
		int base = 0;
		int weight = Weight();
		text = std::to_string(weight);
		color = weight > Stat(5, base) ? 0xff0000 : 0xc0c0c0;
		return true;
	}
	case 25: { // what we can carry: our STR
		int base = 0;
		text = std::to_string(Stat(5, base));
		return true;
	}
	case 12: case 13: case 14: case 15: case 16: { // colored against the base, like the stats
		int base = 0;
		int value = Resist(eq_type, base);
		text = std::to_string(value);
		color = ComparisonColor(value, base);
		return true;
	}
	case 28:
		text = spawn ? spawn->DisplayName() : "";
		return true;
	case 29: // the target's HP percent; 0 without a living target, as eqgame prints it
		text = std::to_string(spawn && spawn->npc < 2 ? std::clamp<int>(spawn->hp, 0, 100) : 0);
		return true;
	default:
		if (eq_type >= 45 && eq_type <= 59) { // the buff in that slot's spell name: no buffs yet
			text.clear();
			return true;
		}
		return false;
	}
}
