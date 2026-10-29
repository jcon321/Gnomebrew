#include "inventory.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace Inventory
{
	namespace
	{
		constexpr int16_t FirstBagSlot = 250, CursorBagSlot = 330;
		constexpr int16_t FirstBankBagSlot = 2030, FirstTradeBagSlot = 3030;
		constexpr int Agnostic = 396;

		// Item types: what stacks (CanDropItem), and what the range, ammo and hand slots check.
		enum ItemType : uint8_t
		{
			TwoHandSlash = 1, TwoHandBlunt = 4, Food = 14, Drink = 15, Combinable = 17, Bandage = 18,
			Throwing = 19, Martial = 20, FirstInstrument = 23, LastInstrument = 26, Arrow = 27, Type28 = 28,
			TwoHandPierce = 35, FishingBait = 37, Alcohol = 38, Type43 = 43, Type44 = 44,
		};
		constexpr uint8_t Quiver = 2, Bandolier = 8; // bag types
		constexpr int DualWield = 22;               // skill

		bool TwoHanded(const ZoneItem &item)
		{
			return item.item_type == TwoHandSlash || item.item_type == TwoHandBlunt || item.item_type == TwoHandPierce;
		}

		bool Instrument(const ZoneItem &item)
		{
			return item.item_type >= FirstInstrument && item.item_type <= LastInstrument;
		}

		// EQ_Equipment::IsWeapon (@004cd9c2).
		bool IsWeapon(const ZoneItem &item)
		{
			uint8_t t = item.item_type;
			return t != Arrow && t != Type28 && t != Type43 && t != Type44 && item.damage != 0;
		}

		// EQ_Character::HasSkill, from what the zone sends: 254 we'll have it at a later level, 255 never.
		bool HasSkill(const ZoneProfile &me, int skill)
		{
			return me.skills[skill] < 254;
		}

		uint8_t Charges(const ZoneItem &item)
		{
			return static_cast<uint8_t>(item.charges);
		}

		const ZoneItem *Find(const std::map<int16_t, ZoneItem> &items, int16_t slot)
		{
			auto it = items.find(slot);
			return it != items.end() ? &it->second : nullptr;
		}

		// SwapCombine: as much of the stack in `from` as fits onto the same item in `to`, told to
		// the zone in whole items; the stack's gone once it's all moved. How many went (0: none).
		int Combine(ZoneSession &zone, int16_t from, int16_t to)
		{
			auto &items = zone.EditItems();
			auto src = items.find(from), dst = items.find(to);
			if (src == items.end() || dst == items.end() || !Stacks(src->second) || src->second.id != dst->second.id)
				return 0;
			int room = std::max(dst->second.max_charges * MaxStack - Charges(dst->second), 0);
			int have = Charges(src->second);
			int moved;
			if (room < have) {
				src->second.charges = static_cast<int8_t>(have - room);
				dst->second.charges = static_cast<int8_t>(Charges(dst->second) + room);
				moved = room / src->second.max_charges;
			}
			else {
				dst->second.charges = static_cast<int8_t>(Charges(dst->second) + have);
				moved = have / src->second.max_charges;
				items.erase(src);
			}
			if (from >= 0 && moved > 0)
				zone.MoveItem(from, to, moved);
			return moved;
		}

		// The cursor's item into an empty slot, told to the zone.
		void Put(ZoneSession &zone, int16_t to)
		{
			Swap(zone.EditItems(), Cursor, to);
			zone.MoveItem(Cursor, to, 0);
		}
	}

	const char *MessageText(uint32_t id)
	{
		switch (id) {
		case CantEquip: return "Your class, deity and/or race may not equip that item.";
		case NoBagsInBags: return "You cannot place containers in containers.";
		case TooBigForBag: return "That item is too big to fit in that container.";
		case QuiverArrowsOnly: return "Only arrows may go in a quiver.";
		case BandolierAmmoOnly: return "Only ammo may go in a bandolier.";
		default: return "";
		}
	}

	bool Stacks(const ZoneItem &item)
	{
		if (item.item_class != 0 || item.max_charges == 0 || item.max_charges > 127)
			return false;
		switch (item.item_type) {
		case Food: case Drink: case Combinable: case Bandage: case Throwing: case Arrow: case Type28:
		case FishingBait: case Alcohol:
			return true;
		default:
			return false;
		}
	}

	int Count(const ZoneItem &item)
	{
		return Stacks(item) ? (Charges(item) - 1) / item.max_charges + 1 : 0;
	}

	int16_t BagSlot(int16_t slot, int n)
	{
		if (n < 0 || n >= BagSlots)
			return -1;
		if (slot == Cursor)
			return static_cast<int16_t>(CursorBagSlot + n);
		if (slot >= FirstGeneral && slot <= LastGeneral)
			return static_cast<int16_t>(FirstBagSlot + (slot - FirstGeneral) * BagSlots + n);
		if (slot >= FirstBank && slot <= LastBank)
			return static_cast<int16_t>(FirstBankBagSlot + (slot - FirstBank) * BagSlots + n);
		if (slot >= FirstTrade && slot <= LastHisTrade)
			return static_cast<int16_t>(FirstTradeBagSlot + (slot - FirstTrade) * BagSlots + n);
		return -1;
	}

	int16_t BagOf(int16_t slot)
	{
		if (slot >= CursorBagSlot && slot < CursorBagSlot + BagSlots)
			return Cursor;
		if (slot >= FirstBagSlot && slot < CursorBagSlot)
			return static_cast<int16_t>(FirstGeneral + (slot - FirstBagSlot) / BagSlots);
		if (slot >= FirstBankBagSlot && slot < FirstBankBagSlot + (LastBank - FirstBank + 1) * BagSlots)
			return static_cast<int16_t>(FirstBank + (slot - FirstBankBagSlot) / BagSlots);
		if (slot >= FirstTradeBagSlot && slot < FirstTradeBagSlot + (LastHisTrade - FirstTrade + 1) * BagSlots)
			return static_cast<int16_t>(FirstTrade + (slot - FirstTradeBagSlot) / BagSlots);
		return -1;
	}

	bool CanEquip(const ZoneProfile &me, const std::map<int16_t, ZoneItem> &items, const ZoneItem &item, int16_t slot)
	{
		if (slot >= FirstGeneral && slot <= LastGeneral)
			return true;
		if (item.item_class != 0) // containers and books go in general slots only
			return false;
		if (slot == Range) {
			if (item.item_type == Arrow || item.item_type == Type28)
				return false;
			if (item.range != 0)
				return true;
		}
		else if (slot == Ammo) {
			if (item.item_type == Throwing || item.item_type == Arrow || item.item_type == Type28)
				return true;
		}
		if (slot < 0 || slot > 31 || (item.slots & (1 << slot)) == 0)
			return false;
		const ZoneItem *primary = Find(items, Primary), *secondary = Find(items, Secondary);
		if (slot == Primary) {
			// Not a two-hander with something in the other hand, nor anything with an instrument there.
			if (!secondary)
				return true;
			if (TwoHanded(item))
				return false;
			return !Instrument(*secondary);
		}
		if (slot != Secondary)
			return true;
		if (TwoHanded(item) || (IsWeapon(item) && !HasSkill(me, DualWield)))
			return false;
		if (!primary)
			return true;
		return !Instrument(item) && !TwoHanded(*primary);
	}

	bool CanUse(const ZoneProfile &me, const ZoneItem &item)
	{
		if (item.item_class != 0)
			return true;
		int ok = (item.classes & (1u << ((me.class_id - 1) & 31))) != 0;
		if (item.deity == 0 || (me.deity != Agnostic && (item.deity & (1u << ((me.deity + 56) & 31))) != 0))
			++ok;
		// The races' bits: Iksar's is 13 and Vah Shir's 14, after the first twelve.
		int race = me.race == 128 ? 13 : me.race == 130 ? 14 : me.race;
		if ((item.races & (1u << ((race - 1) & 31))) != 0 || item.item_type == Martial)
			++ok;
		if (item.effect_level > 50 && item.effect_level < 128 && me.level < item.effect_level)
			ok = 0;
		if (me.level < item.req_level)
			ok = 0;
		return ok == 3;
	}

	uint32_t BagRefusal(const ZoneItem &item, const ZoneItem &bag)
	{
		if (item.item_class == 1)
			return NoBagsInBags;
		if (bag.bag_size < item.size)
			return TooBigForBag;
		if (bag.bag_type == Quiver && (item.item_class != 0 || (item.item_type != Arrow && item.item_type != Type43)))
			return QuiverArrowsOnly;
		if (bag.bag_type == Bandolier && (item.item_class != 0 || item.item_type != Throwing))
			return BandolierAmmoOnly;
		return 0;
	}

	void Swap(std::map<int16_t, ZoneItem> &items, int16_t a, int16_t b)
	{
		if (a == b)
			return;
		// Each side's item and what's in it, put back on the other side.
		struct Side
		{
			bool has = false;
			ZoneItem item;
			std::vector<std::pair<int, ZoneItem>> contents;
		};
		auto take = [&](int16_t slot) {
			Side side;
			auto it = items.find(slot);
			if (it != items.end()) {
				side.has = true;
				side.item = it->second;
				items.erase(it);
			}
			for (int n = 0; n < BagSlots; ++n) {
				int16_t inside = BagSlot(slot, n);
				auto content = inside >= 0 ? items.find(inside) : items.end();
				if (content != items.end()) {
					side.contents.push_back({n, content->second});
					items.erase(content);
				}
			}
			return side;
		};
		auto put = [&](Side &side, int16_t slot) {
			if (!side.has)
				return;
			side.item.slot = slot;
			items[slot] = side.item;
			for (auto &[n, content] : side.contents) {
				int16_t inside = BagSlot(slot, n);
				if (inside < 0)
					continue;
				content.slot = inside;
				items[inside] = content;
			}
		};
		Side from_a = take(a), from_b = take(b);
		put(from_a, b);
		put(from_b, a);
	}

	void Remove(std::map<int16_t, ZoneItem> &items, int16_t slot)
	{
		items.erase(slot);
		for (int n = 0; n < BagSlots; ++n)
			if (int16_t inside = BagSlot(slot, n); inside >= 0)
				items.erase(inside);
	}

	bool Move(ZoneSession &zone, int16_t from, int16_t to, uint32_t &message)
	{
		message = 0;
		auto &items = zone.EditItems();
		const ZoneItem *a = Find(items, from), *b = Find(items, to);
		// A slot in a bag needs the bag; one end in a bag, the other not (it's the cursor).
		const ZoneItem *from_bag = nullptr, *to_bag = nullptr;
		if (int16_t bag = BagOf(from); bag >= 0) {
			from_bag = Find(items, bag);
			if (!from_bag || from_bag->item_class != 1)
				return false;
		}
		if (int16_t bag = BagOf(to); bag >= 0) {
			to_bag = Find(items, bag);
			if (!to_bag || to_bag->item_class != 1)
				return false;
		}
		if (!a && !b)
			return false;
		if (from_bag && to_bag)
			return false;
		const ZoneItem *entering = from_bag ? b : a;
		const ZoneItem *bag = from_bag ? from_bag : to_bag;
		if (bag && entering) {
			message = BagRefusal(*entering, *bag);
			if (message)
				return false;
		}
		if (Combine(zone, from, to) > 0)
			return true;
		// The same stacking item, full: it stays where it is.
		if (a && b && Stacks(*a) && a->id == b->id)
			return false;
		Swap(items, from, to);
		zone.MoveItem(from, to, 0);
		return true;
	}

	void TakeFromStack(ZoneSession &zone, int16_t from, int count)
	{
		auto &items = zone.EditItems();
		auto it = items.find(from);
		if (it == items.end() || count <= 0 || items.count(Cursor))
			return;
		if (count >= Count(it->second)) {
			uint32_t message;
			Move(zone, from, Cursor, message);
			return;
		}
		ZoneItem taken = it->second;
		taken.slot = Cursor;
		taken.charges = static_cast<int8_t>(taken.max_charges * count);
		it->second.charges = static_cast<int8_t>(Charges(it->second) - Charges(taken));
		items[Cursor] = taken;
		zone.MoveItem(from, Cursor, count);
	}

	int32_t &Coins(ZoneProfile &me, int place, int coin)
	{
		int32_t *coins[4][4] = {
			{&me.cursor_platinum, &me.cursor_gold, &me.cursor_silver, &me.cursor_copper},
			{&me.platinum, &me.gold, &me.silver, &me.copper},
			{&me.bank_platinum, &me.bank_gold, &me.bank_silver, &me.bank_copper},
			{&me.trade_coins[0], &me.trade_coins[1], &me.trade_coins[2], &me.trade_coins[3]},
		};
		return *coins[place & 3][coin & 3];
	}

	int32_t Coins(const ZoneProfile &me, int place, int coin)
	{
		return Coins(const_cast<ZoneProfile &>(me), place, coin);
	}

	int CursorMoneyType(const ZoneProfile &me)
	{
		for (int coin = 0; coin < 4; ++coin)
			if (Coins(me, CursorCoins, coin) > 0)
				return coin + 1;
		return 0;
	}

	bool MoveMoney(ZoneSession &zone, int from, int to, int from_coin, int to_coin, int32_t amount, bool bank_open,
		bool force)
	{
		if (amount <= 0 || from < CursorCoins || from > TradeCoins || to < CursorCoins || to > TradeCoins ||
		    from_coin < 0 || from_coin > 3 || to_coin < 0 || to_coin > 3)
			return false;
		int steps = to_coin - from_coin;
		int factor = 1;
		for (int i = 0; i < std::abs(steps); ++i)
			factor *= 10;
		if (factor != 1 && !bank_open)
			return false;
		ZoneProfile &me = zone.EditProfile();
		if (to == CursorCoins && !force && (zone.Items().count(Cursor) || CursorMoneyType(me) != 0))
			return false;
		int32_t &source = Coins(me, from, from_coin), &dest = Coins(me, to, to_coin);
		if (&source == &dest || source < amount)
			return false;
		if (steps < 0) { // to a bigger coin: what doesn't make a whole one stays
			int32_t moved = amount - amount % factor;
			source -= moved;
			dest += moved / factor;
		}
		else {
			source -= amount;
			dest += amount * factor;
		}
		// MoveCoin_Struct's coins run the other way: 0 copper .. 3 platinum.
		zone.MoveCoin(from, to, 3 - from_coin, 3 - to_coin, amount);
		return true;
	}

	int32_t ValueBuy(const ZoneItem &item, float rate, int count)
	{
		if (item.price < 0)
			return 0;
		float each = static_cast<float>(item.price) * rate;
		if (item.item_class == 0 && item.sell_rate > 1.0f)
			each *= item.sell_rate;
		return static_cast<int32_t>(static_cast<float>(count) * each + 0.5f);
	}

	int32_t ValueSell(const ZoneItem &item, float rate, int count)
	{
		if (item.price < 1 || rate == 0)
			return 0;
		int32_t total = static_cast<int32_t>(static_cast<double>(item.price) / rate + 0.5) * count;
		if (total >= 10000000)
			total = (total + 50) - (total + 50) % 100;
		return total;
	}

	int32_t TotalOnPerson(const ZoneProfile &me)
	{
		return me.copper + (me.silver + (me.gold + me.platinum * 10) * 10) * 10;
	}

	bool HandleMoney(ZoneProfile &me, int32_t copper)
	{
		if (copper < 0 || TotalOnPerson(me) < copper)
			return false;
		while (me.copper < copper) {
			if (me.silver != 0) {
				--me.silver;
				me.copper += 10;
			}
			else if (me.gold != 0) {
				--me.gold;
				me.silver = 10;
			}
			else if (me.platinum != 0) {
				--me.platinum;
				me.gold = 10;
			}
		}
		me.copper -= copper;
		return true;
	}

	void AddCoins(ZoneProfile &me, int32_t copper)
	{
		me.platinum += copper / 1000;
		me.gold += copper % 1000 / 100;
		me.silver += copper % 100 / 10;
		me.copper += copper % 10;
	}

	bool CanDrop(const std::map<int16_t, ZoneItem> &items, const ZoneItem &item, int16_t slot, bool fv_rules)
	{
		if (fv_rules)
			return true;
		if (item.no_drop == 0)
			return false;
		if (item.item_class == 1)
			for (int n = 0; n < item.bag_slots; ++n)
				if (const ZoneItem *inside = Find(items, BagSlot(slot, n)); inside && inside->no_drop == 0)
					return false;
		return true;
	}

	bool HasLoreItem(const std::map<int16_t, ZoneItem> &items, const ZoneItem &item)
	{
		if (item.lore.empty() || item.lore[0] != '*')
			return false;
		for (const auto &[slot, have] : items) {
			bool ours = slot <= CursorBagSlot + BagSlots || (slot >= FirstBank && slot < FirstTrade);
			if (ours && have.id == item.id)
				return true;
		}
		return false;
	}

	bool AutoInventory(ZoneSession &zone)
	{
		auto &items = zone.EditItems();
		const ZoneProfile &me = zone.Profile();
		auto held = [&]() { return Find(items, Cursor); };
		if (!held())
			return true;
		// Onto the same in a worn slot it could go in (arrows, throwing weapons), then into the
		// first empty worn slot it could, from the ammo slot back.
		if (Stacks(*held()))
			for (int16_t slot = Ammo; slot >= FirstWorn && held(); --slot) {
				const ZoneItem *there = Find(items, slot);
				if (there && there->id == held()->id && CanEquip(me, items, *held(), slot) && CanUse(me, *held()))
					Combine(zone, Cursor, slot);
			}
		if (held() && held()->item_class == 0)
			for (int16_t slot = Ammo; slot >= FirstWorn && held(); --slot)
				if (!Find(items, slot) && CanEquip(me, items, *held(), slot) && CanUse(me, *held()))
					Put(zone, slot);
		// AutoGenInventory: onto the same in the general slots, then in the bags.
		if (held() && Stacks(*held())) {
			for (int16_t slot = FirstGeneral; slot <= LastGeneral && held(); ++slot)
				if (const ZoneItem *there = Find(items, slot); there && Stacks(*there))
					Combine(zone, Cursor, slot);
			for (int16_t general = FirstGeneral; general <= LastGeneral && held() && held()->item_class != 1; ++general) {
				const ZoneItem *bag = Find(items, general);
				if (!bag || bag->item_class != 1)
					continue;
				for (int n = 0; n < bag->bag_slots && held(); ++n) {
					const ZoneItem *there = Find(items, BagSlot(general, n));
					if (there && Stacks(*there) && BagRefusal(*held(), *bag) == 0)
						Combine(zone, Cursor, BagSlot(general, n));
				}
			}
		}
		// Into the first empty general slot, then the first empty bag slot it can go in.
		for (int16_t slot = FirstGeneral; slot <= LastGeneral && held(); ++slot)
			if (!Find(items, slot))
				Put(zone, slot);
		for (int16_t general = FirstGeneral; general <= LastGeneral && held() && held()->item_class != 1; ++general) {
			const ZoneItem *bag = Find(items, general);
			if (!bag || bag->item_class != 1)
				continue;
			for (int n = 0; n < bag->bag_slots && held(); ++n)
				if (!Find(items, BagSlot(general, n)) && BagRefusal(*held(), *bag) == 0)
					Put(zone, BagSlot(general, n));
		}
		return !held();
	}
}
