#include "buffs.h"
#include "../core/alt_adv.h"
#include "chat_text.h"
#include "spell_file.h"

#include <algorithm>
#include <cstdlib>

namespace
{
	constexpr int Bard = 8;
	constexpr uint8_t NoEffect = 254;
	constexpr int TickMs = 6000;

	// EQ_Character_BardCastBard: a bard casting one of their songs.
	bool BardCastBard(const SpellInfo &spell, int class_id)
	{
		int level = spell.ClassLevel(Bard);
		return class_id == Bard && level != 0 && level != 255;
	}

	bool NotASong(const SpellInfo &spell) { return spell.ClassLevel(Bard) == 255; }

	// isAttributeStackable: effects that never block one another.
	bool Stackable(int spa)
	{
		switch (spa) {
		case 13: case 24: case 35: case 36: case 57: case 65: case 66: case 79: case 116:
		case 148: case 149: case NoEffect:
			return true;
		}
		return spa >= 124 && spa <= 144;
	}

	// The spells FindAffectSlot never lets anything overwrite.
	bool Unoverwritable(int id)
	{
		return (id >= 775 && id <= 785) || (id >= 1200 && id <= 1250) || (id >= 1900 && id <= 1924) || id == 2079 ||
		       id == 2751 || id == 756 || id == 757 || id == 836;
	}

	// The spells whose value FindAffectSlot takes as -1.
	bool CountsNegative(int id) { return id == 1620 || id == 1816 || id == 833 || id == 1814; }
}

void Buffs::Attach(ZoneSession &zone, const SpellFile *spells, const EqStr *eqstr, Chat chat)
{
	m_zone = &zone;
	m_spells = spells;
	m_eqstr = eqstr;
	m_chat = std::move(chat);
}

void Buffs::Reset(uint64_t now_ms)
{
	for (int i = 0; i < Slots; ++i) {
		m_slots[i] = m_zone ? m_zone->Profile().buffs[i] : ZoneBuff{};
		m_casters[i] = 0;
	}
	m_tick_ms = now_ms;
	++m_serial;
}

void Buffs::Clear()
{
	for (int i = 0; i < Slots; ++i) {
		m_slots[i] = ZoneBuff{};
		m_casters[i] = 0;
	}
	++m_serial;
}

void Buffs::Put(int slot, const ZoneBuff &buff)
{
	if (slot < 0 || slot >= Slots)
		return;
	m_slots[slot] = buff;
	m_casters[slot] = 0;
	++m_serial;
}

const SpellInfo *Buffs::Spell(int id) const
{
	return m_spells ? m_spells->Find(id) : nullptr;
}

const ZoneBuff &Buffs::Slot(int slot) const
{
	static const ZoneBuff none;
	return slot >= 0 && slot < Slots ? m_slots[slot] : none;
}

const SpellInfo *Buffs::SlotSpell(int slot) const
{
	if (slot < 0 || slot >= Slots || m_slots[slot].type == 0)
		return nullptr;
	return Spell(m_slots[slot].spell);
}

void Buffs::Say(uint32_t id, const std::string &fallback, int type) const
{
	if (m_chat)
		m_chat(FormatEqString(m_eqstr, id, fallback, {}), type);
}

bool Buffs::FindCaster(uint16_t id, Caster &caster) const
{
	caster.id = id;
	if (id == m_zone->MySpawnId()) {
		caster.level = m_zone->Profile().level;
		caster.class_id = m_zone->Profile().class_id;
		caster.npc = false;
		return true;
	}
	auto it = m_zone->Spawns().find(id);
	if (it == m_zone->Spawns().end())
		return false;
	caster.level = it->second.level;
	caster.class_id = it->second.class_id;
	caster.npc = it->second.npc == 1;
	return true;
}

void Buffs::ZoneEvent(const ZoneSession::Event &event)
{
	if (!m_zone)
		return;
	using Type = ZoneSession::Event::Type;
	if (event.type == Type::SpellAction) {
		if (event.cast.type == 231 && (event.cast.flags & 4) && event.cast.target == m_zone->MySpawnId())
			Land(event.cast);
		return;
	}
	if (event.type != Type::BuffChanged)
		return;
	if (event.value == 3) {
		if (event.id < Slots) {
			m_slots[event.id] = event.buff;
			++m_serial;
		}
		return;
	}
	for (int i = 0; i < Slots; ++i) {
		ZoneBuff &b = m_slots[i];
		if (b.type == 0 || b.type != event.buff.type || b.spell != event.buff.spell)
			continue;
		if (event.value == 1)
			Remove(i, false);
		else
			b = event.buff;
		++m_serial;
	}
}

void Buffs::Land(const ZoneCast &cast)
{
	const SpellInfo *spell = Spell(cast.spell);
	Caster caster;
	if (!spell || !FindCaster(cast.caster, caster))
		return;
	if (Total(40) > 0 && cast.spell != 982) // Divine Aura
		return;
	int level = caster.level;
	if (cast.spell >= 1252 && cast.spell <= 1266 && cast.level > 0 && cast.level < 61)
		level = cast.level;
	if (spell->duration_formula == 0)
		return; // no buff: its effects at once (none of them here)
	int slot = FindAffectSlot(cast.spell, *spell, caster);
	if (slot < 0)
		return;
	ZoneBuff &b = m_slots[slot];
	b.counters = 0;
	m_casters[slot] = caster.id;
	b.type = (spell->target_type == 13 || spell->target_type == 20) && caster.id == m_zone->MySpawnId() ? 4 : 2;
	b.bard_modifier = 10;
	if (caster.class_id == Bard && cast.instrument_mod > 10 && cast.instrument_mod < 40)
		b.bard_modifier = static_cast<uint8_t>(cast.instrument_mod);
	b.spell = static_cast<int16_t>(cast.spell);
	b.level = static_cast<uint8_t>(level);
	b.duration = static_cast<int16_t>(Duration(*spell, level, m_zone->Profile().aa_ranks[0x37]));
	b.activated = spell->activated;
	++m_serial;
}

bool Buffs::StackBlocked(int id, const SpellInfo &spell) const
{
	// Effect 148 in a buff we have: the new spell's effect in the place it names (the formula, less
	// 201 over 200) blocked when it's no more than the max, unless the new spell has the same 148
	// with a greater max (the same line, overwriting).
	if (BardCastBard(spell, m_zone->Profile().class_id))
		return false;
	for (const ZoneBuff &b : m_slots) {
		const SpellInfo *old = Spell(b.spell);
		if (!old || (b.spell == id && id != 2751))
			continue;
		for (int i = 0; i < 12 && old->spas[i] != NoEffect; ++i) {
			if (old->spas[i] != 148)
				continue;
			int blocked = old->base[i];
			int place = old->formula[i] > 200 ? old->formula[i] - 201 : old->formula[i];
			int below = std::abs(old->max[i]);
			if (place >= 12 || spell.spas[place] != blocked)
				continue;
			int value = spell.max[place] != 0 ? spell.max[place] : spell.base[place];
			if (value > below)
				continue;
			for (int j = 0; j < 12; ++j)
				if (spell.spas[j] == 148 && spell.base[j] == blocked && spell.formula[j] - 201 == place)
					value = spell.max[j];
			if (value <= below)
				return true;
		}
	}
	return false;
}

int Buffs::FindAffectSlot(int id, const SpellInfo &spell, const Caster &caster)
{
	bool good = spell.good_effect != 0;
	if (!caster.npc && StackBlocked(id, spell))
		return -1;
	if (BardCastBard(spell, caster.class_id))
		for (const ZoneBuff &b : m_slots) {
			const SpellInfo *old = b.type != 0 ? Spell(b.spell) : nullptr;
			if (old && NotASong(*old) && old->good_effect == 0 && good && spell.HasSpa(3) && (old->HasSpa(3) || old->HasSpa(99)))
				return -1;
		}

	// The same spell again, in its slot.
	for (int slot = 0; slot < Slots; ++slot) {
		const ZoneBuff &b = m_slots[slot];
		if (b.type == 0 || b.spell != id)
			continue;
		if (caster.level < b.level || spell.HasSpa(67) || spell.HasSpa(101) || spell.HasSpa(113))
			return -1;
		return slot;
	}

	int found = -1;
	for (int slot = 0; slot < Slots; ++slot) {
		ZoneBuff &b = m_slots[slot];
		const SpellInfo *old = b.type != 0 ? Spell(b.spell) : nullptr;
		if (!old) {
			if (b.type != 0)
				b = ZoneBuff{}; // a spell we don't know: cleared
			if (found == -1)
				found = slot;
			continue;
		}
		int old_id = b.spell;
		bool old_good = old->good_effect != 0;
		if (caster.class_id == Bard && NotASong(*old)) {
			if (good && spell.HasSpa(3) && (old->HasSpa(3) || old->HasSpa(99)))
				return -1;
			if (BardCastBard(spell, caster.class_id))
				continue; // a song stacks with what isn't one
		}
		int old_bard = old->ClassLevel(Bard);
		if (good && old_good && spell.HasSpa(3) && old->HasSpa(3) && old_bard != 0 && old_bard != 255 && NotASong(spell))
			return -1; // a movement song isn't overwritten by a spell

		for (int i = 0; i < 12; ++i) {
			int old_spa = old->spas[i], spa = spell.spas[i];
			if (old_spa == NoEffect || spa == NoEffect)
				break;
			if (spa == 44 || spa == 45) // lycanthropy, vampirism
				return -1;
			bool regen_song = (old_spa == 15 || old_spa == 0 || old_spa == 100) && !NotASong(*old) && NotASong(spell);
			if (regen_song || old_spa != spa || Stackable(spa))
				continue;
			if (spa < 2) { // HP, AC: a DoT or a debuff doesn't count
				if (spell.base[i] < 0)
					continue;
			}
			else if (spa == 10 && (spell.base[i] == 0 || old->base[i] == 0)) // CHA as a spacer
				continue;

			if ((good && (!old_good || old->HasSpa(58))) || old_spa == 101 || Unoverwritable(old_id))
				return -1;
			int old_value = Value(*old, b.level, i, nullptr);
			int value = Value(spell, caster.level, i, nullptr);
			if (CountsNegative(id) || old_id == 1814)
				value = -1;
			if (CountsNegative(old_id))
				old_value = -1;

			if ((old_value < 0 && value > 0) || (old_value > 0 && value < 0)) {
				if (spa == 3) {
					if (value >= 0)
						return -1; // a snare stays
				}
				else if (spa == 0 && old_value < 0 && value > 0)
					return -1; // a DoT stays
				if (old_id != id)
					Remove(slot, false);
				return slot;
			}
			bool ok = id == 836;
			if (spa == 11) { // attack speed: under 100 a slow
				if (value > 99 || old_value < value) {
					if (old_value >= 101) {
						if (value <= 99 || old_value <= value)
							ok = true;
					}
				}
				else
					ok = true;
			}
			else if ((old_value < 0 && value <= old_value) || (old_value > 0 && old_value <= value))
				ok = true;
			if (!ok)
				return -1;
			if (good || old_good) {
				if (found == -1 || m_slots[found].type == 0)
					found = slot;
				break;
			}
			if (old_id != id)
				Remove(slot, false);
			return slot;
		}
	}

	if (found == -1) {
		// No room: a detrimental spell takes the first beneficial buff's.
		if (good || m_zone->Self().gm)
			return -1;
		for (int slot = 0; slot < Slots; ++slot) {
			const SpellInfo *old = Spell(m_slots[slot].spell);
			if (old && old->good_effect != 0) {
				Remove(slot, false);
				return slot;
			}
		}
		return -1;
	}
	if (m_slots[found].type != 0 && m_slots[found].spell != id)
		Remove(found, false);
	return found;
}

bool Buffs::HasAffect(int spa) const
{
	for (int slot = 0; slot < Slots; ++slot) {
		const ZoneBuff &b = m_slots[slot];
		const SpellInfo *spell = b.type != 0 ? Spell(b.spell) : nullptr;
		if (spell && spell->HasSpa(spa))
			return true;
	}
	return false;
}

bool Buffs::RemoveAffect(int spa, bool notify)
{
	for (int slot = 0; slot < Slots; ++slot) {
		const ZoneBuff &b = m_slots[slot];
		const SpellInfo *spell = b.type != 0 ? Spell(b.spell) : nullptr;
		if (!spell)
			continue;
		for (int i = 0; i < 12; ++i)
			if (spell->spas[i] == spa) {
				Remove(slot, true);
				if (notify && m_zone)
					m_zone->SendBuff(slot, m_slots[slot], 1);
				return true;
			}
	}
	return false;
}

void Buffs::Remove(int slot, bool tell)
{
	ZoneBuff old = m_slots[slot];
	if (const SpellInfo *spell = Spell(old.spell); spell && !spell->fades.empty() && m_chat)
		m_chat(spell->fades, ChatType::Spells);
	m_slots[slot] = ZoneBuff{};
	m_casters[slot] = 0;
	++m_serial;
	if (tell) {
		ZoneBuff gone = m_slots[slot];
		gone.spell = old.spell;
		m_zone->SendBuff(slot, gone, 0);
	}
}

bool Buffs::Frame(uint64_t now_ms)
{
	if (!m_zone || now_ms - m_tick_ms <= TickMs)
		return false;
	m_tick_ms = now_ms;
	for (int slot = 0; slot < Slots; ++slot) {
		ZoneBuff &b = m_slots[slot];
		if (b.type < 2)
			continue;
		if (b.duration == 0) {
			Remove(slot, true);
			continue;
		}
		if (b.duration != -1) {
			--b.duration;
			++m_serial;
		}
		const SpellInfo *spell = Spell(b.spell);
		if (!spell || b.duration == -1 || b.duration >= 2)
			continue;
		if (spell->HasSpa(58))
			Say(12483, "You feel as if you are about to look like yourself again.", ChatType::SpellFailure);
		if (spell->HasSpa(57) && NotASong(*spell))
			Say(12482, "You feel as if you are about to fall.", ChatType::SpellFailure);
	}
	return true;
}

bool Buffs::ClickOff(int slot)
{
	const SpellInfo *spell = SlotSpell(slot);
	if (!spell || !m_zone)
		return false;
	bool decays = false;
	for (uint8_t formula : spell->formula)
		decays |= formula == 107 || formula == 108 || formula == 120 || formula == 122;
	if (spell->good_effect == 0 || spell->HasSpa(101) || decays) {
		Say(137, "You cannot remove this effect.", ChatType::Red);
		return false;
	}
	Remove(slot, true);
	m_zone->SendBuff(slot, m_slots[slot], 1);
	return true;
}

int Buffs::Total(int spa) const
{
	int sum = 0, snare = 0, speed = 0;
	for (const ZoneBuff &b : m_slots) {
		const SpellInfo *spell = b.type != 0 ? Spell(b.spell) : nullptr;
		if (!spell)
			continue;
		for (int i = 0; i < 12; ++i) {
			if (spell->spas[i] != spa)
				continue;
			int value = Value(*spell, b.level, i, &b);
			if (b.bard_modifier > 10)
				value = value * b.bard_modifier / 10;
			if (value < 0)
				snare = std::min(snare, value);
			else
				speed = std::max(speed, value);
			sum += b.type == 4 ? -value : value;
		}
	}
	if (spa == 3)
		return snare != 0 ? snare : speed;
	return sum;
}

int Buffs::AttackSpeed() const
{
	int slow = 999, spell_haste = 0, over = 0, bard = 0;
	for (const ZoneBuff &b : m_slots) {
		const SpellInfo *spell = b.type != 0 ? Spell(b.spell) : nullptr;
		if (!spell)
			continue;
		for (int i = 0; i < 12; ++i) {
			int spa = spell->spas[i];
			if (spa == 98)
				over = std::max(over, Value(*spell, b.level, i, &b));
			else if (spa == 119)
				bard = std::max(bard, Value(*spell, b.level, i, &b));
			else if (spa == 11) {
				int value = static_cast<int16_t>(Value(*spell, b.level, i, &b));
				if (value < 100) {
					if (value < slow && value > 0)
						slow = value;
				}
				else
					spell_haste = std::max(spell_haste, value);
			}
		}
	}
	// The worn items (slots 1..21): not a bag, not stackable (Stackable over 1), a worn effect
	// (EffectType1 2) of a buff spell, at its EffectLevel1.
	int level = m_zone ? m_zone->Profile().level : 1;
	int item_slow = 999, item_haste = 0;
	if (m_zone)
		for (int16_t slot = 1; slot <= 21; ++slot) {
			auto it = m_zone->Items().find(slot);
			if (it == m_zone->Items().end())
				continue;
			const ZoneItem &item = it->second;
			if (item.item_class != 0 || item.stackable < 2 || item.effect_type != 2)
				continue;
			const SpellInfo *spell = Spell(item.effect1);
			if (!spell || spell->duration_formula == 0)
				continue;
			for (int i = 0; i < 12; ++i) {
				if (spell->spas[i] != 11)
					continue;
				int value = static_cast<int16_t>(Value(*spell, item.effect_level, i, nullptr));
				if (value < 100) {
					if (value < item_slow && value > 0)
						item_slow = value;
				}
				else {
					if (level < 26 && value > 110)
						value = 110;
					item_haste = std::max(item_haste, value);
				}
			}
		}
	if (item_slow < slow && item_slow != 999)
		slow = item_slow;
	if (slow < 101)
		return slow;
	int haste = item_haste + spell_haste;
	int cap = level + 125;
	if (bard > 0) {
		if (bard > 100)
			bard -= 100;
		bard = std::min(bard, level < 51 ? 10 : 25);
		cap += bard;
	}
	if (spell_haste > 99 && item_haste > 99)
		haste -= 100;
	if (over > 100 && level > 49)
		haste += std::min(over, 110) - 100;
	if (level > 50)
		cap = bard + 185;
	if (level > 59)
		cap = bard + 200;
	if (haste == 0)
		haste = bard != 0 ? bard + 100 : 0;
	else
		haste += bard;
	return std::min(haste, cap);
}

float Buffs::SpeedModifier(float run_speed, bool running) const
{
	int root = Total(99);
	int rate = root < 0 ? root : Total(3);
	if (rate < -999)
		return static_cast<float>(rate);
	rate = std::max(rate, -65);
	float modifier = std::abs(rate) * 0.01f * run_speed;
	if (rate < 0)
		modifier = -modifier;
	AltAdv::Character ch;
	if (m_zone) {
		const ZoneProfile &me = m_zone->Profile();
		ch = {me.class_id, me.level, me.expansions, me.aa_unspent, me.aa_ranks};
	}
	if (running && rate == 0 && AltAdv::InnateRunSpeed(ch) != 0)
		modifier = AltAdv::InnateRunSpeed(ch);
	return std::min(modifier, AltAdv::RunSpeedCap(ch));
}

int Buffs::Duration(const SpellInfo &spell, int level, int permanent_illusion)
{
	unsigned l = static_cast<uint8_t>(level);
	unsigned formula = static_cast<unsigned>(spell.duration_formula);
	unsigned ticks = 0;
	if (formula == 0)
		return 0;
	if (formula == 50)
		return -1;
	if (formula > 50) {
		if (formula < 200)
			return 0;
		ticks = formula;
	}
	else
		switch (formula) {
		case 1: ticks = l < 2 ? 1 : l / 2; break;
		case 2: ticks = (l < 2 ? 1 : l / 2) + 5; break;
		case 3: ticks = l * 30; break;
		case 4: ticks = 50; break;
		case 5: ticks = 2; break;
		case 6: ticks = l / 2 + 2; break;
		case 7: ticks = l; break;
		case 8: ticks = l + 10; break;
		case 9: ticks = l * 2 + 10; break;
		case 10: ticks = l * 3 + 10; break;
		case 11: ticks = (l + 3) * 30; break;
		case 12: ticks = l / 4 == 0 ? 1 : l / 4; break;
		default: return 0;
		}
	if (spell.duration != 0 && static_cast<unsigned>(spell.duration) < ticks)
		ticks = static_cast<unsigned>(spell.duration);
	if (spell.good_effect != 0 && permanent_illusion != 0 && spell.HasSpa(0x3a))
		ticks = 10000;
	return static_cast<int16_t>(ticks);
}

int Buffs::Value(const SpellInfo &spell, int level, int effect, const ZoneBuff *buff)
{
	int base = spell.base[effect], max = spell.max[effect];
	int magnitude = std::abs(base);
	int calc = static_cast<int8_t>(spell.formula[effect]);
	int l = static_cast<uint8_t>(level);
	// So much a level over `from`.
	auto over = [l](int from, int times) { return l - from > 0 ? (l - from) * times : 0; };
	// How far a decaying formula has come: the ticks the buff has had.
	auto decay = [&](int times) {
		if (spell.duration_formula == 0 || !buff)
			return 0;
		return -static_cast<int>(static_cast<uint16_t>(Duration(spell, l) - buff->duration)) * times;
	};
	int add = 0;
	switch (calc) {
	case 100: add = 0; break;
	case 101: add = l / 2; break;
	case 102: add = l; break;
	case 103: add = l * 2; break;
	case 104: add = l * 3; break;
	case 105: add = l * 4; break;
	case 107: add = decay(1); break;
	case 108: add = decay(2); break;
	case 109: add = l / 4; break;
	case 110: add = l / 6; break;
	case 111: add = over(16, 6); break;
	case 112: add = over(24, 8); break;
	case 113: add = over(34, 10); break;
	case 114: add = over(44, 15); break;
	case 115: add = over(15, 7); break;
	case 116: add = over(24, 10); break;
	case 117: add = over(34, 13); break;
	case 118: add = over(44, 20); break;
	case 119: add = l / 8; break;
	case 120: add = decay(5); break;
	case 121: add = l / 3; break;
	case 122: add = decay(12); break;
	case 123: { // RandNbr between the base and the max
		int range = max - magnitude;
		add = range > 1 ? std::rand() % range + 1 : 1;
		break;
	}
	case 124: add = over(50, 1); break;
	case 125: add = over(50, 2); break;
	case 126: add = over(50, 3); break;
	case 127: add = over(50, 4); break;
	case -128: add = over(50, 5); break;  // 128
	case -127: add = over(50, 10); break; // 129
	case -126: add = over(50, 15); break; // 130
	case -125: add = over(50, 20); break; // 131
	case -124: add = over(50, 25); break; // 132
	default: add = static_cast<int16_t>(calc * l); break;
	}
	int value;
	if (spell.spas[effect] == 11 && magnitude < 100) { // a slow: down from the base, to the max
		value = static_cast<int16_t>(magnitude - add);
		if (max != 0 && value < max)
			value = max;
	}
	else
		value = static_cast<int16_t>(magnitude + add);
	if (max != 0 && std::abs(max) < value)
		value = std::abs(max);
	if (base < 0 || (calc < 0 && calc >= -123))
		value = -value;
	return static_cast<int16_t>(value);
}
