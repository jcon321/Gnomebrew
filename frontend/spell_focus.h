#pragma once

// Focus effects on our spells, as eqgame's EQ_Character works them out (@004c3fe3 on): an item's
// focus spell (EQ_Equipment's FocusEffect, +0x166) on a worn item, or a buff, whose first effect is
// the kind of focus asked for and whose limits the spell meets (DoesSpellMatchFocusFilters):
//   - GetFocusItem (@004c41b5): of the worn items (slots 1..20), the one with the highest base
//     (its spell's base 1) among those that match; none for warriors, monks, bards and rogues.
//   - GetFocusEffect (@004c42cb): of the buffs that match, the one with the most negative base if
//     any is negative, else the highest; none for those classes either.
//   - DoesSpellMatchFocusFilters (@004c4432): each of the focus spell's effects after the first, in
//     order, until one fails or a 0xfe: 134 the most level (the spell's for our class), 135 the
//     resist type, 136 the target type (negative: not it), 137 an effect the spell has (negative:
//     hasn't; 0 and 79, 33 and 71 count as each other, and any other effect counts as 0), 138
//     beneficial or not, 139 the spell (negative: not it), 140 the least duration, 141 no duration,
//     142 the least level, 143 and 144 the least and most cast time.
// The kinds ours uses: 127 (0x7f) casting time, 129 (0x81) range.

#include <cstdint>
#include <string>

class Buffs;
class SpellFile;
class ZoneSession;
struct SpellInfo;
struct ZoneItem;

namespace SpellFocus
{
	bool Matches(const SpellInfo &cast, const SpellInfo &focus, int class_id, int level);
	// GetFocusItem: the worn item, null for none.
	const ZoneItem *Item(const ZoneSession &zone, const SpellFile &spells, const SpellInfo &cast, int kind);
	// GetFocusEffect: a buff's spell, null for none.
	const SpellInfo *Effect(const ZoneSession &zone, const Buffs &buffs, const SpellFile &spells, const SpellInfo &cast,
		int kind);
	// GetFocusCastingTimeModifier (@004c4071): the ms the item's and the buff's focus take off the
	// cast time (a base percent of it each), negative; `item` the focus item when it's there.
	int CastingTimeModifier(const ZoneSession &zone, const Buffs *buffs, const SpellFile &spells, const SpellInfo &cast,
		const ZoneItem **item = nullptr);
	// GetFocusRangeModifier (@004c4105): what the item's and the buff's focus add to the range (a
	// base percent of it each, truncated).
	int RangeModifier(const ZoneSession &zone, const Buffs *buffs, const SpellFile &spells, const SpellInfo &cast);
}
