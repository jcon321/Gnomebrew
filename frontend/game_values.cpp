#include "game_values.h"
#include "eqstr.h"
#include "world_client.h"
#include "zone_session.h"

#include <algorithm>

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

int GameValues::Stat(const ZoneProfile &p, const std::map<int16_t, ZoneItem> &items, int eq_type, int &base)
{
	int worn = 0;
	switch (eq_type) {
	case 5: base = p.str; worn = WornSum(items, &ZoneItem::str); break;
	case 6: base = p.sta; worn = WornSum(items, &ZoneItem::sta); break;
	case 7: base = p.dex; worn = WornSum(items, &ZoneItem::dex); break;
	case 8: base = p.agi; worn = WornSum(items, &ZoneItem::agi); break;
	case 9: base = p.wis; worn = WornSum(items, &ZoneItem::wis); break;
	case 10: base = p.int_; worn = WornSum(items, &ZoneItem::int_); break;
	case 11: base = p.cha; worn = WornSum(items, &ZoneItem::cha); break;
	default: base = 0; return 0;
	}
	// CalculateMaxStatAA2 without AAs: no more than 255.
	return std::min(base + worn, 255);
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
		int value = Stat(me, zone->Items(), eq_type, base);
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
	case 12: case 13: case 14: case 15: case 16: case 22: case 23: case 24: case 25:
		text.clear();
		return true;
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
