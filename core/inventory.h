#pragma once

// eqgame's own inventory: where it lets an item go and how it moves its items, which it does
// before telling the zone (OP_MoveItem; the zone answers only when it disagrees):
//   - CanDropItem (@004f0bd4), which is whether an item stacks: a common item with MaxCharges of
//     one of the stacking types (food, drink, combinables, bandages, throwing weapons, arrows...);
//     a stack holds ((charges - 1) / MaxCharges + 1) of it, and 20 at most (SwapCombine's 0x14);
//   - CanItemEquipInSlot (@004f0db4), can_use_item (@004bb8e8) and CanGoInBag (@004f11a3);
//   - CInvSlotMgr_MoveItem (@00422b1c) with SwapCombine (@004f0acd): a stack put on the same item
//     tops that up (OP_MoveItem with how many went), anything else changes places (with 0);
//   - CInvSlot::SliderComplete (@00421d1e): some of a stack onto the cursor (with how many);
//   - AutoInventory and AutoGenInventory (@004f0eeb, @004f15cb): the cursor's item onto the same
//     item in a worn slot, then into the first worn slot it can go in, then onto the same item in
//     the general slots and bags, then into the first empty general slot, then a bag.
// Slots are the zone's: 0 the cursor, 1..21 worn, 22..29 general, 250 + (general - 22) * 10 + n
// in the bags, 330 + n in a bag on the cursor. eqgame keeps a bag's items in it, so they go
// with it here too.

#include "zone_session.h"

#include <cstdint>
#include <map>

namespace Inventory
{
	constexpr int16_t Cursor = 0;
	constexpr int16_t FirstWorn = 1, Range = 11, Primary = 13, Secondary = 14, Ammo = 21;
	constexpr int16_t FirstGeneral = 22, LastGeneral = 29;
	constexpr int MaxStack = 20;

	// The messages these give, eqstr ids.
	constexpr uint32_t CantEquip = 6611;         // "Your class, deity and/or race may not equip that item."
	constexpr uint32_t NoBagsInBags = 12933;     // "You cannot place containers in containers."
	constexpr uint32_t TooBigForBag = 12934;     // "That item is too big to fit in that container."
	constexpr uint32_t QuiverArrowsOnly = 12935; // "Only arrows may go in a quiver."
	constexpr uint32_t BandolierAmmoOnly = 12936; // "Only ammo may go in a bandolier."
	const char *MessageText(uint32_t id); // their English, for when eqstr doesn't have them

	bool Stacks(const ZoneItem &item); // CanDropItem
	int Count(const ZoneItem &item);   // how many in its stack; 0 for an item that doesn't stack

	// The n'th slot of the bag in `slot` (a general slot or the cursor), or -1; and the slot of
	// the bag a slot is in, or -1 when it isn't in one.
	int16_t BagSlot(int16_t slot, int n);
	int16_t BagOf(int16_t slot);

	bool CanEquip(const ZoneProfile &me, const std::map<int16_t, ZoneItem> &items, const ZoneItem &item, int16_t slot);
	bool CanUse(const ZoneProfile &me, const ZoneItem &item); // our class, deity, race and level
	uint32_t BagRefusal(const ZoneItem &item, const ZoneItem &bag); // why it can't go in, or 0

	// What's in two slots changes places (either may be empty), with a bag's items; Remove takes
	// an item and a bag's items out.
	void Swap(std::map<int16_t, ZoneItem> &items, int16_t a, int16_t b);
	void Remove(std::map<int16_t, ZoneItem> &items, int16_t slot);

	// CInvSlotMgr_MoveItem: what's in `from` to `to`; false if it can't go (message: why, or 0).
	bool Move(ZoneSession &zone, int16_t from, int16_t to, uint32_t &message);
	// SliderComplete: `count` of the stack in `from` onto the (empty) cursor; all of it moves it.
	void TakeFromStack(ZoneSession &zone, int16_t from, int count);
	// AutoInventory: the cursor's item where it goes. False when there's no room for it: eqgame
	// drops it on the ground then ("There was no place to put that!"), and we leave it on the cursor.
	bool AutoInventory(ZoneSession &zone);
}
