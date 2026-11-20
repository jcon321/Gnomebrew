#include "spell_focus.h"

#include "buffs.h"
#include "spell_file.h"
#include "zone_session.h"

#include <cstdlib>

namespace
{
	bool NoFocus(int class_id)
	{
		return class_id == 1 || class_id == 7 || class_id == 8 || class_id == 9; // warrior, monk, bard, rogue
	}

	// DoesSpellMatchFocusFilters' table (@00606c64, set on first use): an effect a limit of 137
	// counts as; any not set is 0.
	int SameEffect(int spa)
	{
		switch (spa) {
		case 0: return 79;
		case 33: return 71;
		case 71: return 33;
		default: return 0; // 79's set to 0 too
		}
	}
}

bool SpellFocus::Matches(const SpellInfo &cast, const SpellInfo &focus, int class_id, int level)
{
	for (int i = 1; i < 12; ++i) {
		int spa = focus.spas[i];
		if (spa == 0xfe)
			return true;
		int base = focus.base[i];
		bool ok = true;
		switch (spa) {
		case 134: ok = cast.ClassLevel(class_id) <= base; break;
		case 135: ok = cast.resist_type == base; break;
		case 136: ok = base > 0 ? cast.target_type == base : cast.target_type != -base; break;
		case 137: {
			bool exclude = base < 0;
			int want = std::abs(base);
			bool found = false;
			for (int e = 0; e < 12 && cast.spas[e] != 0xfe; ++e)
				if (cast.spas[e] == want || SameEffect(cast.spas[e]) == want) {
					found = true;
					break;
				}
			ok = found != exclude;
			break;
		}
		case 138: ok = cast.good_effect == base; break;
		case 139: ok = base > 0 ? cast.id == base : cast.id != -base; break;
		case 140: ok = Buffs::Duration(cast, level) >= base; break;
		case 141: ok = cast.duration_formula == 0; break;
		case 142: ok = cast.ClassLevel(class_id) >= base; break;
		case 143: ok = cast.cast_ms >= base; break;
		case 144: ok = cast.cast_ms <= base; break;
		default: break;
		}
		if (!ok)
			return false;
	}
	return true;
}

const ZoneItem *SpellFocus::Item(const ZoneSession &zone, const SpellFile &spells, const SpellInfo &cast, int kind)
{
	const ZoneProfile &me = zone.Profile();
	if (NoFocus(me.class_id))
		return nullptr;
	const ZoneItem *best = nullptr;
	int best_base = 0;
	for (int16_t slot = 1; slot <= 20; ++slot) {
		auto it = zone.Items().find(slot);
		if (it == zone.Items().end() || it->second.item_class != 0)
			continue;
		const SpellInfo *focus = spells.Find(it->second.focus_effect);
		if (!focus || focus->spas[0] != kind || (best && focus->base[0] <= best_base) ||
		    !Matches(cast, *focus, me.class_id, me.level))
			continue;
		best = &it->second;
		best_base = focus->base[0];
	}
	return best;
}

const SpellInfo *SpellFocus::Effect(const ZoneSession &zone, const Buffs &buffs, const SpellFile &spells,
	const SpellInfo &cast, int kind)
{
	const ZoneProfile &me = zone.Profile();
	if (NoFocus(me.class_id))
		return nullptr;
	const SpellInfo *lowest = nullptr, *highest = nullptr;
	for (int slot = 0; slot < Buffs::Slots; ++slot) {
		const ZoneBuff &b = buffs.Slot(slot);
		const SpellInfo *focus = b.type != 0 ? spells.Find(b.spell) : nullptr;
		if (!focus || focus->spas[0] != kind || !Matches(cast, *focus, me.class_id, me.level))
			continue;
		if (!lowest || focus->base[0] < lowest->base[0])
			lowest = focus;
		if (!highest || focus->base[0] > highest->base[0])
			highest = focus;
	}
	if (lowest && lowest->base[0] >= 0)
		return highest;
	return lowest;
}

int SpellFocus::CastingTimeModifier(const ZoneSession &zone, const Buffs *buffs, const SpellFile &spells,
	const SpellInfo &cast, const ZoneItem **item)
{
	int modifier = 0;
	if (const ZoneItem *focus_item = Item(zone, spells, cast, 0x7f)) {
		if (item)
			*item = focus_item;
		if (const SpellInfo *focus = spells.Find(focus_item->focus_effect))
			modifier = -(focus->base[0] * cast.cast_ms / 100);
	}
	if (buffs)
		if (const SpellInfo *focus = Effect(zone, *buffs, spells, cast, 0x7f))
			modifier -= focus->base[0] * cast.cast_ms / 100;
	return modifier;
}

int SpellFocus::RangeModifier(const ZoneSession &zone, const Buffs *buffs, const SpellFile &spells, const SpellInfo &cast)
{
	int modifier = 0;
	if (const ZoneItem *focus_item = Item(zone, spells, cast, 0x81))
		if (const SpellInfo *focus = spells.Find(focus_item->focus_effect))
			modifier = static_cast<int>(focus->base[0] * cast.range * 0.01f);
	if (buffs)
		if (const SpellInfo *focus = Effect(zone, *buffs, spells, cast, 0x81))
			modifier += static_cast<int>(focus->base[0] * cast.range * 0.01f);
	return modifier;
}
