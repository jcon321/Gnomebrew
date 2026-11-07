#include "item_display.h"
#include "chat_text.h"
#include "game_values.h"
#include "game_windows.h"
#include "spell_file.h"
#include "world_client.h"
#include "zone_session.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

namespace
{
	const char *const Br = "<BR>";

	// The slot names SetItem goes through (@005e5a40), a bit each; the second ear, wrist and
	// finger (4, 10, 16) are left out.
	constexpr uint32_t SlotNames[22] = {558, 559, 560, 561, 559, 562, 563, 564, 565, 566, 566, 567, 568, 569, 570,
		571, 571, 572, 573, 574, 575, 576};
	const char *const SlotEnglish[22] = {"Held", "Ear", "Head", "Face", "Ear", "Neck", "Shoulders", "Arms", "Back",
		"Wrist", "Wrist", "Range", "Hands", "Primary", "Secondary", "Fingers", "Fingers", "Chest", "Legs", "Feet",
		"Waist", "Ammo"};

	// GetClassThreeLetterCode's (by class bit, WAR first; rangers' and shadow knights' the other
	// way round from their numbers) and GetRaceCompatibilityString's (HUM first).
	constexpr uint32_t ClassCodes[15] = {3065, 3066, 3067, 3069, 3068, 3070, 3071, 3072, 3073, 3074, 3075, 3076, 3077,
		3078, 3079};
	const char *const ClassEnglish[15] = {"WAR", "CLR", "PAL", "RNG", "SHD", "DRU", "MNK", "BRD", "ROG", "SHM", "NEC",
		"WIZ", "MAG", "ENC", "BST"};
	constexpr uint32_t RaceCodes[14] = {3050, 3051, 3052, 3053, 3054, 3055, 3056, 3057, 3058, 3059, 3060, 3061, 3062,
		3063};
	const char *const RaceEnglish[14] = {"HUM", "BAR", "ERU", "ELF", "HIE", "DEF", "HEF", "DWF", "TRL", "OGR", "HFL",
		"GNM", "IKS", "VAH"};

	// CEverQuest_GetRaceDesc2 (@0052db3a): races 1..244's names (0: "UNKNOWN RACE").
	constexpr uint16_t RaceDescIds[244] = {
		1470, 1471, 1472, 1473, 1474, 1475, 1476, 1477, 1478, 1479, 1480, 1481,
		2469, 2523, 2471, 2472, 2473, 2474, 2475, 2476, 2477, 2478, 2479, 2480,
		2481, 2482, 2483, 2484, 2485, 2486, 2487, 2488, 2489, 2490, 2491, 2492,
		2493, 2494, 2495, 2496, 2497, 2498, 2499, 2500, 2501, 2502, 2503, 2504,
		2505, 2506, 2507, 2508, 2509, 2510, 2511, 2512, 2513, 2514, 2515, 2516,
		2517, 2518, 2519, 2520, 2521, 2522, 2470, 2524, 2525, 2526, 2527, 2528,
		2529, 2530, 2531, 2533, 2534, 2535, 2536, 2537, 2538, 2539, 2540, 2541,
		2542, 2543, 2544, 2545, 2546, 2547, 2548, 2549, 2550, 2551, 2552, 2553,
		2554, 2555, 2556, 2557, 2558, 2559, 2560, 2561, 2562, 2563, 2564, 2565,
		2566, 2567, 2568, 2569, 2570, 2571, 2572, 2573, 2574, 2575, 2576, 2577,
		2578, 2579, 2580, 2581, 2582, 2583, 2584, 1482, 2586, 1483, 2587, 2588,
		2589, 2590, 2591, 2592, 2593, 2594, 2595, 2596, 2532, 0, 0, 2597,
		2598, 2599, 2600, 2601, 2602, 2603, 2604, 2605, 2606, 2607, 2608, 2609,
		2610, 2611, 2612, 2613, 2614, 2615, 2616, 2617, 2618, 2619, 2620, 2621,
		2622, 2623, 2624, 2625, 2626, 2627, 2628, 2629, 2630, 2631, 0, 0,
		2632, 2633, 2634, 2635, 2636, 2637, 2638, 2639, 2640, 2641, 2642, 2643,
		2644, 2645, 2646, 2647, 2648, 2649, 2650, 2651, 2652, 2653, 2655, 2656,
		2657, 2658, 2659, 2660, 2661, 2662, 2663, 2664, 2665, 2666, 2667, 2668,
		2669, 2670, 2671, 2672, 2673, 2674, 2675, 2676, 2677, 2678, 2679, 2680,
		2681, 2682, 2683, 2684, 2685, 2687, 2688, 2689, 2690, 2691, 2692, 2686,
		2693, 2694, 2695, 2696,
	};

	// CEverQuest_GetBodyTypeDesc (@0052d856): body types with their own name; the rest 2455.
	struct BodyTypeDesc
	{
		int body;
		uint32_t id;
	};
	constexpr BodyTypeDesc BodyTypeDescs[] = {{1, 2443}, {2, 2445}, {3, 2449}, {4, 2442}, {5, 2438}, {6, 2441},
		{7, 2446}, {12, 2453}, {13, 2450}, {14, 2451}, {15, 2452}, {19, 5754}, {21, 2437}, {22, 2444}, {23, 2447},
		{24, 2440}, {25, 2448}, {26, 2439}, {28, 2454}};

	// CEverQuest_GetItemClassDesc (@0052d7f2) for an instrument's BardType.
	struct InstrumentDesc
	{
		int type;
		uint32_t id;
		const char *english;
	};
	constexpr InstrumentDesc InstrumentDescs[] = {{23, 13923, "Wind Instrument"}, {24, 13916, "Stringed Instrument"},
		{25, 13872, "Brass Instrument"}, {26, 13898, "Percussion Instrument"}, {50, 13908, "Singing"},
		{51, 5750, "All Instruments"}};

	// The eqstr string with %1.. filled in, then orderedStringExpansion's "%%" to "%".
	std::string Text(const GameValues &values, uint32_t id, const std::string &fallback,
		const std::vector<std::string> &args = {})
	{
		std::string s = FormatEqString(values.eqstr, id, fallback, args);
		for (size_t i = s.find("%%"); i != std::string::npos; i = s.find("%%", i + 1))
			s.erase(i, 1);
		return s;
	}

	// The eqstr strings SetItem sprintf's with a casting time: its "%2.1f" filled in.
	std::string Seconds(const GameValues &values, uint32_t id, const char *fallback, double seconds)
	{
		std::string s = Text(values, id, fallback);
		char number[32];
		snprintf(number, sizeof(number), "%2.1f", seconds);
		size_t at = s.find("%2.1f");
		if (at != std::string::npos)
			s.replace(at, 5, number);
		return s;
	}

	std::string Number(int value)
	{
		return std::to_string(value);
	}

	// DisplayPCBonus2HDam (@004f313b): the damage bonus warriors, paladins, rangers, shadow knights,
	// monks, bards, rogues and beastlords get from 28, more with a two-handed weapon by its delay.
	int DamageBonus(const ZoneProfile &me, const ZoneItem &item)
	{
		int level = me.level, c = me.class_id;
		if (level < 28)
			return 0;
		if (!(c == 1 || (c >= 3 && c <= 5) || (c >= 7 && c <= 9) || c == 15))
			return 0;
		int base = (level - 28) / 3;
		int bonus = base + 1;
		int t = item.item_type;
		if (t == 1 || t == 4 || t == 35) {
			int delay = item.delay;
			if (delay < 28)
				bonus = base + 2;
			else if (level > 29) {
				int more = (level - 30) / 5 + 1;
				if (level > 50) {
					more = (level - 30) / 5 + 2;
					int over = level - 50;
					if (level > 54)
						over = level - 49;
					if (level > 56)
						++over;
					if (level > 58)
						++over;
					if (level > 59)
						++over;
					more += delay * over / 40;
				}
				bonus += more;
			}
			if (delay > 39) {
				int more = (delay - 40) / 3 + 1;
				if (delay > 42)
					more = (delay - 40) / 3 + 2;
				if (delay > 44)
					++more;
				bonus += more;
			}
		}
		return bonus;
	}

	// CanDrop(item, 0x29a) (@004ef773), for the display: a no drop item, or a bag with one in it,
	// can't be. On FV's rules all can but for EQ_Item::IsSpecialNoDrop's ids (not here).
	bool CanDrop(const GameValues &values, const ZoneItem &item)
	{
		if (values.fv_rules)
			return true;
		if (item.no_drop == 0)
			return false;
		if (item.item_class == 1) {
			// A bag's own slots: 250.. for the inventory's 22..29, 330.. on the cursor (30).
			int first = item.slot >= 22 && item.slot <= 29 ? 250 + (item.slot - 22) * 10 : item.slot == 30 ? 330 : -1;
			const auto &items = values.zone->Items();
			for (int i = 0; first >= 0 && i < item.bag_slots; ++i) {
				auto it = items.find(static_cast<int16_t>(first + i));
				if (it != items.end() && it->second.no_drop == 0)
					return false;
			}
		}
		return true;
	}

	// CreateEquipmentStatusString (@004257ce).
	std::string StatusString(const GameValues &values, const ZoneItem &item)
	{
		std::string s;
		if (item.magic & 1)
			s += Text(values, 3094, "MAGIC ITEM");
		size_t next = 0;
		char first = item.lore.empty() ? '\0' : item.lore[0];
		if (first == '*' || first == '~') {
			s += "  " + (first == '*' ? Text(values, 3095, "LORE ITEM") : Text(values, 3096, "PENDING LORE"));
			next = 1;
		}
		char mark = next < item.lore.size() ? item.lore[next] : '\0';
		if (mark == '#')
			s += "  " + Text(values, 3097, "ARTIFACT");
		else if (mark == '&')
			s += "  " + Text(values, 3098, "SUMMONED");
		if (!CanDrop(values, item))
			s += "  " + Text(values, 3099, "NO DROP");
		if (item.no_rent == 0)
			s += "  " + Text(values, 3100, "NO RENT");
		return s;
	}

	// GetItemSizeDescription (@00425130): TINY to LARGE, GIANT for the rest.
	std::string SizeText(const GameValues &values, int size)
	{
		static const char *const english[] = {"TINY", "SMALL", "MEDIUM", "LARGE", "GIANT"};
		int i = size >= 0 && size < 4 ? size : 4;
		return Text(values, 3128 + i, english[i]);
	}

	// sprintf "%4.1f" of the weight in tenths.
	std::string WeightText(const ZoneItem &item)
	{
		char weight[32];
		snprintf(weight, sizeof(weight), "%4.1f", static_cast<float>(item.weight) * 0.1f);
		return weight;
	}

	std::string Upper(std::string s)
	{
		for (char &c : s)
			c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		return s;
	}

	std::string SpellName(const GameValues &values, int id)
	{
		const SpellInfo *spell = values.spells ? values.spells->Find(id) : nullptr;
		return spell ? spell->name : "";
	}

	std::string RaceDesc(const GameValues &values, int race)
	{
		uint32_t id = race >= 1 && race <= static_cast<int>(std::size(RaceDescIds)) ? RaceDescIds[race - 1] : 0;
		if (id == 0)
			return "UNKNOWN RACE";
		return Text(values, id, RaceName(static_cast<uint16_t>(race)));
	}

	std::string BodyTypeDesc(const GameValues &values, int body)
	{
		for (const auto &desc : BodyTypeDescs)
			if (desc.body == body)
				return Text(values, desc.id, "Body Type " + Number(body));
		return Text(values, 2455, "Unknown");
	}

	// CreateModString: "STR: +5    " and its two spaces, counted, or nothing for 0. A value above 0
	// brought down by AntiTwinkAdj shows red, the full value after it.
	std::string ModString(const GameValues &values, const ZoneItem &item, int value, uint32_t id, const char *fallback,
		int &count)
	{
		if (value == 0)
			return "";
		char text[64];
		int adjusted = value > 0 ? values.AntiTwinkAdj(item, value) : value;
		if (adjusted == value)
			snprintf(text, sizeof(text), "%+d", value);
		else
			snprintf(text, sizeof(text), "<c \"#FF4040\"> %+d </C> (%+d)", adjusted, value);
		++count;
		return Text(values, id, fallback, {text}) + "  ";
	}

	// GetClassThreeLetterCode and GetRaceCompatibilityString: "Class: " and the codes of the bits
	// set, each with a space after; None for none, ALL for all of them.
	std::string CodeList(const GameValues &values, uint32_t label, const char *label_english, int32_t bits,
		const uint32_t *ids, const char *const *english, int count)
	{
		std::string s = Text(values, label, label_english);
		int set = 0;
		for (int i = 0; i < count; ++i)
			if (bits & (1 << i)) {
				s += Text(values, ids[i], english[i]) + " ";
				++set;
			}
		if (set == 0)
			s += Text(values, 1216, "None");
		else if (set >= count)
			s = Text(values, label, label_english) + "  " + Text(values, 13205, "ALL");
		return s;
	}

	void CommonItem(const GameValues &values, const ZoneItem &item, std::string &out)
	{
		const ZoneProfile &me = values.zone->Profile();
		auto line = [&out](const std::string &text) { out += text + Br; };
		int lines = 0; // param_3: the lines so far, for where the saves break
		if (values.auto_identify) {
			line(Text(values, 3048, "LORE: ") + item.lore);
			lines = 1;
		}
		line(StatusString(values, item));
		++lines;

		std::string slots;
		for (int i = 0; i < 22; ++i)
			if (i != 4 && i != 10 && i != 16 && (item.slots & (1 << i)))
				slots += " " + Upper(Text(values, SlotNames[i], SlotEnglish[i]));
		if (!slots.empty()) {
			line(Text(values, 3000, "Slot: %1", {slots}));
			++lines;
		}

		uint8_t charges = static_cast<uint8_t>(item.charges);
		if (item.effect_type == 3 || item.item_type == 21) {
			line(Text(values, 3001, "EXPENDABLE     Charges: %1", {Number(charges)}));
			++lines;
		}
		else if (item.stackable > 1 && item.stackable < 5 && static_cast<int8_t>(item.max_charges) > 0) {
			line(charges ? Text(values, 3003, "Charges: %1", {Number(charges)}) : Text(values, 3002, "Charges: None"));
			++lines;
		}

		if (item.item_type == 20) { // a scroll: its spell (Effect1) for our class
			const SpellInfo *spell = values.spells ? values.spells->Find(item.effect1) : nullptr;
			bool usable = me.class_id >= 1 && me.class_id <= 15 && (item.classes & (1 << (me.class_id - 1)));
			if (!usable)
				line(Text(values, 3005, "Level Needed: CANNOT USE"));
			else
				line(Text(values, 3004, "Level Needed: %1", {Number(spell ? spell->class_levels[me.class_id - 1] : 0)}));
			line(Text(values, 12453, "Skill: %1", {SkillName(values.eqstr, spell ? spell->skill : 0)}));
			line(Text(values, 3006, "Mana Cost: %1", {Number(spell ? spell->mana : 0)}));
			lines += 3;
		}

		if (GameValues::IsWeapon(item) || item.item_type == 27 || item.item_type == 28) {
			int skill = GameValues::SkillUsed(item.item_type);
			if (item.item_type == 27 || item.item_type == 28)
				skill = 7;
			if (item.item_type == 19)
				skill = 51;
			if (skill != 28 || item.item_type == 45) {
				if (item.item_type == 35)
					line(Text(values, 3007, "Skill: 2H Piercing   Atk Delay: %1", {Number(item.delay)}));
				else
					line(Text(values, 3008, "Skill: %1   Atk Delay: %2", {SkillName(values.eqstr, skill), Number(item.delay)}));
				++lines;
			}
		}

		if (item.item_type == 40) {
			// A compass points by our heading (counterclockwise from north), eighths of 512.
			static const char *const english[] = {"North", "NorthEast", "East", "SouthEast", "South", "SouthWest",
				"West", "NorthWest"};
			int16_t h = static_cast<int16_t>(values.heading);
			int dir = h >= 480 ? 0 : h > 416 ? 1 : h > 352 ? 2 : h > 288 ? 3 : h > 224 ? 4 : h > 160 ? 5 : h > 96 ? 6
			        : h > 32   ? 7
			        : h >= 0   ? 0
			                   : -1;
			std::string where = dir >= 0 ? Text(values, 12427 + dir, english[dir]) : "";
			line(Text(values, 3009, "The compass points %1.", {where}));
			++lines;
		}
		else if (item.bard_type != 0) {
			for (const auto &desc : InstrumentDescs)
				if (desc.type == item.bard_type)
					out += Text(values, desc.id, desc.english);
			out += Br;
			++lines;
		}

		int skill_used = GameValues::SkillUsed(item.item_type);
		int ac = item.ac ? values.AntiTwinkAdj(item, item.ac) : 0;
		if (item.damage == 0) {
			if (item.ac != 0) {
				line(ac < item.ac ? Text(values, 3015, "AC: <c \"#FF4040\">%1</c> (%2)", {Number(ac), Number(item.ac)})
				                  : Text(values, 3016, "AC: %1", {Number(ac)}));
				++lines;
			}
		}
		else {
			int bonus = DamageBonus(me, item);
			std::string bonus_text = bonus > 0 ? Text(values, 3010, "Dmg Bonus: %1", {Number(bonus)}) : "";
			int damage = values.AntiTwinkAdj(item, item.damage);
			bool reduced = damage < item.damage;
			if (item.ac == 0)
				line(reduced ? Text(values, 3013, "DMG: <c \"#FF4040\">%1</c> (%2)  %3",
				                   {Number(damage), Number(item.damage), bonus_text})
				             : Text(values, 3014, "DMG: %1  %2", {Number(damage), bonus_text}));
			else
				line(reduced ? Text(values, 3011, "DMG: <c \"#FF4040\">%1</c> (%2)  %3 AC: %4",
				                   {Number(damage), Number(item.damage), bonus_text, Number(ac)})
				             : Text(values, 3012, "DMG: %1  %2 AC: %3", {Number(damage), bonus_text, Number(ac)}));
			++lines;
		}

		if (item.elem_type >= 1 && item.elem_type <= 5 && item.elem_amount != 0) {
			static const char *const english[] = {"Magic DMG: %1", "Fire DMG: %1", "Cold DMG: %1", "Poison DMG: %1",
				"Disease DMG: %1"};
			line(Text(values, 3017 + item.elem_type - 1, english[item.elem_type - 1], {Number(item.elem_amount)}));
			++lines;
		}
		if ((item.bane_body != 0 || item.bane_race != 0) && item.bane_amount != 0) {
			std::string amount = Number(item.bane_amount);
			if (item.bane_body == 0)
				line(Text(values, 3022, "Bane DMG: %1 %2", {RaceDesc(values, item.bane_race), amount}));
			else if (item.bane_race == 0)
				line(Text(values, 3023, "Bane DMG: %1 +%2", {BodyTypeDesc(values, item.bane_body), amount}));
			else
				line(Text(values, 3024, "Bane DMG: %1, %2 %3",
					{BodyTypeDesc(values, item.bane_body), RaceDesc(values, item.bane_race), amount}));
			++lines;
		}
		if ((item.skill_mod_type & 0xff) != 0 && item.skill_mod_value != 0) {
			line(Text(values, 3025, "Skill Mod: %1  +%2 %%",
				{SkillName(values.eqstr, item.skill_mod_type & 0xff), Number(item.skill_mod_value)}));
			++lines;
		}
		bool faction = false;
		for (int i = 0; i < 4; ++i)
			faction = faction || (item.faction_mod[i] != 0 && item.faction_amount[i] > 0);
		if (faction) {
			line(Text(values, 3026, "This item has a faction modifier."));
			++lines;
		}

		// The stats, a line of them broken after more than five; HP and mana; the saves, broken
		// after four while there have been fewer than four lines.
		int count = 0;
		out += ModString(values, item, item.str, 12457, "STR: %1    ", count);
		out += ModString(values, item, item.dex, 12458, "DEX: %1   ", count);
		out += ModString(values, item, item.sta, 12459, "STA: %1    ", count);
		out += ModString(values, item, item.cha, 12460, "CHA: %1    ", count);
		out += ModString(values, item, item.wis, 12461, "WIS: %1    ", count);
		out += ModString(values, item, item.int_, 12462, "INT: %1    ", count);
		out += ModString(values, item, item.agi, 12463, "AGI: %1    ", count);
		if (count > 5) {
			out += Br;
			++lines;
			count = 0;
		}
		out += ModString(values, item, item.hp, 12464, "HP: %1    ", count);
		out += ModString(values, item, item.mana, 12465, "MANA: %1    ", count);
		if (count > 0) {
			out += Br;
			++lines;
			count = 0;
		}
		out += ModString(values, item, item.fr, 12466, "SV FIRE: %1    ", count);
		out += ModString(values, item, item.dr, 12467, "SV DISEASE: %1    ", count);
		out += ModString(values, item, item.cr, 12468, "SV COLD: %1    ", count);
		out += ModString(values, item, item.mr, 12469, "SV MAGIC: %1    ", count);
		if (lines < 4 && count > 3)
			out += Br;
		out += ModString(values, item, item.pr, 12470, "SV POISON: %1    ", count);
		if (count > 0)
			out += Br;

		if (item.rec_skill != 0 || item.rec_level != 0 || item.req_level != 0) {
			std::string req = Number(item.req_level);
			if (item.rec_skill == 0) {
				if (item.req_level != 0 && item.rec_level != 0)
					line(Text(values, 557, "Recommended level of %1. Required level of %2.", {Number(item.rec_level), req}));
				else if (item.rec_level != 0)
					line(Text(values, 3028, "Recommended level of %1.", {Number(item.rec_level)}));
				else
					line(Text(values, 5775, "Required level of %1.", {req}));
			}
			else {
				std::string skill = SkillName(values.eqstr, skill_used);
				if (item.req_level == 0)
					line(Text(values, 3027, "Recommended skill of %1 %2.", {Number(item.rec_skill), skill}));
				else
					line(Text(values, 556, "Recommended skill of %1 %2. Required level of %3.",
						{Number(item.rec_skill), skill, req}));
			}
		}

		if (item.item_type != 20) {
			if (item.stackable > 1 && item.stackable < 5 && item.effect2 >= 1 && item.effect2 <= 3999) {
				std::string how;
				double seconds = item.cast_time * 0.001;
				switch (item.effect_type) {
				case 0: how = Text(values, 3029, "(Combat)"); break;
				case 1: case 3: case 5:
					how = item.cast_time > 0 ? Seconds(values, 3030, "(Casting Time: %2.1f)", seconds)
					                         : Text(values, 3031, "(Casting Time: Instant)");
					break;
				case 2: how = Text(values, 3032, "(Worn)"); break;
				case 4:
					how = item.cast_time > 0 ? Seconds(values, 3033, "(Must Equip.  Casting Time: %2.1f)", seconds)
					                         : Text(values, 3034, "(Must Equip.  Casting Time: Instant)");
					break;
				default: how = Text(values, 3035, "(Unknown Activation)"); break;
				}
				line(Text(values, 3036, "Effect: %1 %2", {SpellName(values, item.effect2), how}));
			}
			if (item.focus_effect != 0)
				line(Text(values, 5530, "Focus Effect: %1", {SpellName(values, item.focus_effect)}));
		}

		// GetItemLevelDescription: how big a food (14) or drink (15) is, by its duration.
		if (item.item_type == 14 || item.item_type == 15) {
			int t = item.cast_time;
			int step = t < 6 ? 0 : t < 21 ? 1 : t < 31 ? 2 : t < 41 ? 3 : t < 51 ? 4 : t < 61 ? 5 : 6;
			static const char *const food[] = {"This is a snack.", "This is a meal.", "This is a hearty meal.",
				"This is a banquet size meal.", "This meal is a feast!", "This is an enduring meal!",
				"This is a miraculous meal!"};
			static const char *const drink[] = {"This is a whistle wetter.", "This is a drink.",
				"This is a refreshing drink.", "This is a lasting drink.", "This is a flowing drink!",
				"This is an enduring drink!", "This is a miraculous drink!"};
			bool is_food = item.item_type == 14;
			line(Text(values, (is_food ? 3080 : 3087) + step, is_food ? food[step] : drink[step]));
		}

		if (item.range == 0)
			line(Text(values, 3038, "WT: %1   Size: %2", {WeightText(item), SizeText(values, item.size)}));
		else
			line(Text(values, 3037, "WT: %1    Range: %2    Size: %3",
				{WeightText(item), Number(item.range), SizeText(values, item.size)}));
		line(CodeList(values, 3064, "Class: ", item.classes, ClassCodes, ClassEnglish, 15));
		line(CodeList(values, 3049, "Race: ", item.races, RaceCodes, RaceEnglish, 14));

		if (item.deity != 0) {
			// Five to a line, from bit 1 (201).
			out += Text(values, 3039, "Deity:");
			int on_line = 0;
			for (int bit = 1; bit < 17; ++bit) {
				if (item.deity & (1 << bit)) {
					out += "  " + GameValues::DeityName(values.eqstr, 200 + bit);
					++on_line;
				}
				if (on_line == 5) {
					out += Br;
					on_line = 0;
				}
			}
			if (on_line != 0)
				out += Br;
		}
	}
}

std::string ItemDisplayText(const GameValues &values, const ZoneItem &item)
{
	if (!values.zone)
		return "";
	std::string out;
	auto lore = [&]() {
		if (values.auto_identify)
			out += Text(values, 3048, "LORE: ") + item.lore + Br;
	};
	switch (item.item_class) {
	case 0:
		CommonItem(values, item, out);
		break;
	case 1: {
		out += " ";
		lore();
		const bool open = false; // CContainerMgr's: no bag windows yet
		out += (open ? Text(values, 3040, "Container: OPEN.") : Text(values, 3041, "Container: CLOSED.")) + Br;
		out += StatusString(values, item) + Br;
		out += Text(values, 3042, "WT: %1    Weight Reduction: %2", {WeightText(item), Number(item.bag_wr) + "%"}) + Br;
		out += Text(values, 3043, "Capacity: %1    Size Capacity: %2",
			{Number(item.bag_slots), SizeText(values, item.bag_size)}) + Br;
		break;
	}
	case 2:
		out += " ";
		lore();
		if (item.book_type != 0)
			out += item.book_open ? Text(values, 3044, "The Book is open.") : Text(values, 3045, "The Book is closed.");
		else
			out += item.book_open ? Text(values, 3046, "The Note is unrolled.") : Text(values, 3047, "The Note is rolled up.");
		out += Br;
		out += StatusString(values, item) + Br;
		out += Text(values, 3038, "WT: %1   Size: %2", {WeightText(item), SizeText(values, item.size)}) + Br;
		break;
	default:
		break;
	}
	return out;
}

std::string SpellDisplayTitle(const GameValues &values, const SpellInfo &spell)
{
	return Text(values, 3101, "Spell:") + "  " + spell.name;
}

std::string SpellDisplayText(const GameValues &values, const SpellInfo &spell, bool details)
{
	if (!details || !values.zone)
		return "";
	const ZoneProfile &me = values.zone->Profile();
	const int level = spell.ClassLevel(me.class_id);
	if (level == 255) // and SetSpell's third argument -1, as every caller of ours passes it
		return Text(values, 3104, "CANNOT USE") + Br;
	std::string out = Text(values, 506, "Level: %1", {Number(level)}) + Br;
	out += Text(values, 3105, "Skill: %1", {SkillName(values.eqstr, spell.skill)}) + Br;
	// The casting skills (abjuration, alteration, channeling, conjuration, divination, evocation and
	// their specializations, 43..47) say the mana; a song's skills don't.
	const int s = spell.skill;
	if (s == 4 || s == 5 || s == 13 || s == 14 || s == 18 || s == 24 || (s >= 43 && s <= 47))
		out += Text(values, 3006, "Mana Cost: %1", {Number(spell.mana)}) + Br;
	if (me.class_id == 8) // a bard's songs: no casting time
		return out;
	// GetAACastingTimeModifier and GetFocusCastingTimeModifier are both 0 here (spell_cast.h), so
	// it's always "Cast Time"; with either, "Normal Cast Time: %1    Your Cast Time: %2" (3106), the
	// second no less than half the first.
	char seconds[32];
	snprintf(seconds, sizeof(seconds), "%4.1f", spell.cast_ms * 0.001);
	out += Text(values, 3107, "Cast Time: %1", {seconds}) + Br;
	return out;
}

std::string RaceDescription(const GameValues &values, int race)
{
	return RaceDesc(values, race);
}
