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
// Slots are the zone's (CInvSlotMgr_MoveItem's): 0 the cursor, 1..21 worn, 22..29 general,
// 250 + (general - 22) * 10 + n in the bags, 330 + n in a bag on the cursor; the bank's 2000..2007
// and 2030 + bank * 10 + n in its bags; the trade window's 3000..3007 (ours, 3008..3015 the other
// side's) and 3030 + trade * 10 + n in their bags; a world container's (a forge) 4000 + n; a
// corpse's 5000..5029 and a merchant's 6000..6079. eqgame keeps a bag's items in it, so they go
// with it here too.

#include "zone_session.h"

#include <cstdint>
#include <map>
#include <string>

namespace Inventory
{
	constexpr int16_t Cursor = 0;
	constexpr int16_t FirstWorn = 1, Range = 11, Primary = 13, Secondary = 14, Ammo = 21;
	constexpr int16_t FirstGeneral = 22, LastGeneral = 29;
	constexpr int16_t FirstBank = 2000, LastBank = 2007;
	constexpr int16_t FirstTrade = 3000, LastTrade = 3007, FirstHisTrade = 3008, LastHisTrade = 3015;
	constexpr int16_t WorldContainer = 4000; // its first slot
	constexpr int16_t FirstLoot = 5000, LastLoot = 5029;
	constexpr int16_t FirstMerchant = 6000, LastMerchant = 6079;
	constexpr int BagSlots = 10;
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

	// The n'th slot of the bag in `slot` (a general, bank or trade slot, or the cursor), or -1;
	// and the slot of the bag a slot is in, or -1 when it isn't in one.
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
	// Coins (CEverQuest_MoveMoney @00530fd3): where they are, and which (0 platinum .. 3 copper).
	enum Place { CursorCoins, Purse, Bank, TradeCoins };
	int32_t &Coins(ZoneProfile &me, int place, int coin);
	int32_t Coins(const ZoneProfile &me, int place, int coin);
	// CDisplay's CursorMoneyType: 1 platinum .. 4 copper, the biggest coin on the cursor; 0 none.
	int CursorMoneyType(const ZoneProfile &me);
	// MoveMoney: `amount` of a coin from one place to another, as another coin only while the bank's
	// open (ten of one for one of the next up, the rest staying; one for ten of the next down), then
	// OP_MoveCoin. Onto the cursor only when it's free (IsOkToActivate) or `force`. False when it
	// can't (not that many there, the same place and coin).
	bool MoveMoney(ZoneSession &zone, int from, int to, int from_coin, int to_coin, int32_t amount, bool bank_open,
		bool force = false);

	// EQ_Item::ValueBuy (@004cda6f): what `count` of a merchant's item costs, in copper, at its rate
	// (its price times the rate, times SellRate when that's over 1 for a common item, rounded).
	int32_t ValueBuy(const ZoneItem &item, float rate, int count);
	// EQ_Item::ValueSell (@004cdac6): what a merchant gives for `count` of ours: the price over the
	// rate, rounded, times the count, and to the nearest 100 from 10,000,000 on.
	int32_t ValueSell(const ZoneItem &item, float rate, int count);
	// EQ_Character::TotalOnPerson: our purse in copper.
	int32_t TotalOnPerson(const ZoneProfile &me);
	// HandleMoney (@004f2e27): `copper` out of the purse, breaking the bigger coins for change;
	// false (and nothing taken) when there isn't that much.
	bool HandleMoney(ZoneProfile &me, int32_t copper);
	// EQ_Character::ItemSold_addCoinsFromItemSale: `copper` into the purse as the fewest coins.
	void AddCoins(ZoneProfile &me, int32_t copper);
	// CanDrop (@004ef773) with nothing to say (mode 0): not NO DROP, nor a bag with something NO DROP
	// in it; anything under the FV rules (but EQ_Item::IsSpecialNoDrop's ids, not yet).
	bool CanDrop(const std::map<int16_t, ZoneItem> &items, const ZoneItem &item, int16_t slot, bool fv_rules);
	// HasLoreItem (@004f1469): a lore item ('*') we already have one of, anywhere: worn, carried,
	// in a bag, in the bank.
	bool HasLoreItem(const std::map<int16_t, ZoneItem> &items, const ZoneItem &item);

	// BuildMoneyText (@004f8ee3), in English: "1 platinum, 2 gold and 3 copper", the coins there are.
	std::string MoneyText(int32_t platinum, int32_t gold, int32_t silver, int32_t copper);

	// AutoInventory: the cursor's item where it goes. With no room, AutoGenInventory drops it on the
	// ground where we stand, at once (no confirmation; "There was no place to put that!  The item has
	// dropped to the ground!", eqstr 12937, color 15), or keeps a NO DROP one on the cursor (CanDrop's
	// 12921 or 12922). False when it's still on the cursor; `message` the eqstr id to say, or 0.
	bool AutoInventory(ZoneSession &zone, uint32_t *message = nullptr, bool fv_rules = false);
}
