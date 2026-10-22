#include "game_values.h"
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

bool GameValues::Label(int eq_type, std::string &text)
{
	if (!zone)
		return false;
	const ZoneSpawn *spawn = Target();
	switch (eq_type) {
	case 1:
		text = zone->Profile().name;
		return true;
	case 2:
		text = std::to_string(zone->Profile().level);
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
