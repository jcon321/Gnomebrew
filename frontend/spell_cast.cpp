#include "spell_cast.h"
#include "chat_text.h"
#include "inventory.h"
#include "spell_file.h"
#include "window_manager.h"
#include "zone_session.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr int GemCount = ZoneProfile::Gems;
	constexpr int Bard = 8;
	constexpr uint64_t AnswerMs = 10000; // the gems' wait for the zone's answer
	constexpr uint8_t Standing = 100, Feigning = 102, Lying = 115, Dead = 120;
	constexpr int GoneOff = 0xff; // CastingSpellGemNumber once msg_launchspell_info's 3 came

	bool Valid(int spell) { return spell >= 1 && spell <= 3999; } // IsValidSpellIndex

	// The target types CastSpell wants to see the target for (CastRay).
	bool NeedsSight(uint8_t type)
	{
		return type == 5 || (type > 6 && (type < 24 || (type > 26 && (type < 29 || type == 30))));
	}
}

void SpellCast::Attach(WindowManager &windows, ZoneSession &zone, const SpellFile *spells, const EqStr *eqstr, Host host)
{
	m_windows = &windows;
	m_zone = &zone;
	m_spells = spells;
	m_eqstr = eqstr;
	m_host = std::move(host);
}

void SpellCast::Reset()
{
	m_spell = -1;
	m_gem = 0;
	m_timeout = 0;
	m_cast_ms = 0;
	std::fill(std::begin(m_recast_ms), std::end(m_recast_ms), 0);
	m_fizzle_ms = 0;
	m_waiting = m_casts = 0;
}

const SpellInfo *SpellCast::Spell(int id) const
{
	return m_spells && Valid(id) ? m_spells->Find(id) : nullptr;
}

void SpellCast::Say(uint32_t id, const std::string &fallback, int type)
{
	if (m_host.chat)
		m_host.chat(FormatEqString(m_eqstr, id, fallback, {}), type);
}

bool SpellCast::BardSong(const SpellInfo *spell) const
{
	if (!spell || m_zone->Profile().class_id != Bard)
		return false;
	int level = spell->ClassLevel(Bard);
	return level != 0 && level != 255;
}

bool SpellCast::Casting() const
{
	return Valid(m_spell);
}

bool SpellCast::GemReady(int gem, uint64_t now_ms) const
{
	if (gem < 0 || gem >= GemCount || Casting() || !Valid(m_zone->Profile().mem_spells[gem]))
		return false;
	return now_ms >= m_recast_ms[gem] && now_ms > m_fizzle_ms;
}

void SpellCast::GemClicked(int gem, uint64_t now_ms)
{
	if (!m_zone || gem < 0 || gem >= GemCount)
		return;
	if (!Valid(m_zone->Profile().mem_spells[gem]))
		return;
	if (m_recast_ms[gem] < now_ms && m_fizzle_ms < now_ms && !Casting()) {
		if (m_waiting == 0 && m_casts == 0)
			CastSpell(gem, now_ms);
	}
	else if (BardSong(Spell(m_spell))) // EQ_Character_IsBardCastingSpell_or_StopSpellCast
		StopSpellCast(0, m_spell);
	else
		Say(12029, "You haven't recovered yet...", ChatType::Spells);
}

void SpellCast::StopSong()
{
	if (m_zone && m_zone->Profile().class_id == Bard)
		StopSpellCast(0, m_spell);
}

void SpellCast::Interrupted()
{
	if (m_zone)
		StopSpellCast(1, m_spell);
}

bool SpellCast::CastSpell(int gem, uint64_t now_ms)
{
	const ZoneProfile &me = m_zone->Profile();
	uint8_t stance = m_zone->Self().stance;
	if (stance != Standing) {
		Say(12441, "You must be standing to cast a spell.", ChatType::SpellFailure);
		if (stance == Feigning)
			Say(12368, "You do not have control of yourself right now.", ChatType::SpellFailure);
		return false;
	}
	if (m_spell != -1 && me.class_id != Bard) {
		Say(12442, "You are already casting a spell!", ChatType::SpellFailure);
		return false;
	}
	int spell = me.mem_spells[gem];
	const SpellInfo *info = Spell(spell);
	if (!info)
		return false;
	if (me.level < info->ClassLevel(me.class_id)) {
		Say(12443, "You are not high enough level to cast this spell!", ChatType::SpellFailure);
		return false;
	}
	return Cast(gem, spell, *info, nullptr, -1, now_ms);
}

bool SpellCast::CastItem(const ZoneItem &item, int16_t slot, uint64_t now_ms)
{
	if (!m_zone)
		return false;
	const ZoneProfile &me = m_zone->Profile();
	uint8_t stance = m_zone->Self().stance;
	if (stance != Standing) {
		Say(12441, "You must be standing to cast a spell.", ChatType::SpellFailure);
		if (stance == Feigning)
			Say(12368, "You do not have control of yourself right now.", ChatType::SpellFailure);
		return false;
	}
	if (item.item_class != 0 || item.item_type == 20)
		return false;
	if (Casting() && !BardSong(Spell(m_spell)))
		return false;
	if (item.effect_type == 4 || item.effect_type == 5) {
		if (item.effect_type == 4 && (slot < 1 || slot > 21)) {
			Say(179, "You cannot use this item unless it is equipped.", ChatType::Red);
			return false;
		}
		if (item.effect_type == 5 && !Inventory::CanUse(me, item)) {
			Say(181, "Your race, class, or deity cannot use this item.", ChatType::Red);
			return false;
		}
		if (me.level < item.effect_level) {
			Say(136, "You are not sufficient level to use this item.", ChatType::Red);
			return false;
		}
	}
	if (item.stackable < 2)
		return false;
	const SpellInfo *info = Spell(item.effect2);
	if (!info)
		return false;
	if (item.charges == 0) {
		Say(182, "Item is out of charges.", ChatType::Default);
		return false;
	}
	return Cast(10, item.effect2, *info, &item, slot, now_ms);
}

bool SpellCast::Cast(int gem, int spell, const SpellInfo &spell_info, const ZoneItem *item, int16_t slot, uint64_t now_ms)
{
	const ZoneProfile &me = m_zone->Profile();
	uint8_t stance = m_zone->Self().stance;
	const SpellInfo *info = &spell_info;
	uint16_t target = m_host.target ? m_host.target() : 0;
	if (target != 0 && !m_zone->Spawns().count(target) && target != m_zone->MySpawnId())
		target = 0;
	if (NeedsSight(info->target_type) && target != 0 && (info->good_effect != 1 || info->HasSpa(27)) &&
		m_host.can_see && !m_host.can_see(target)) {
		Say(108, "You cannot see your target.", ChatType::SpellFailure);
		return false;
	}
	bool song = BardSong(info);
	if (!song) {
		if (stance != Standing) {
			Say(213, "You must stand upright and still in order to cast!", ChatType::SpellFailure);
			return false;
		}
	}
	else if (stance == Lying || stance == Dead) {
		Say(212, "You need to at least be conscious to sing!", ChatType::SpellFailure);
		return false;
	}
	if (info->recourse != 0 && target == m_zone->MySpawnId()) {
		Say(210, "That spell can not affect this target PC.", ChatType::SpellFailure);
		return false;
	}
	if (!item && me.mana < info->mana) {
		Say(199, "Insufficient Mana to cast this spell!", ChatType::SpellFailure);
		return false;
	}

	// The target type: what it needs of the target.
	uint8_t type = info->target_type;
	uint32_t problem = 0;
	switch (type) {
	case 6: // ourselves; summoning a corpse wants a player targeted, not us (no group to be in yet)
		if (info->HasSpa(91)) {
			bool ourselves = target != 0 && target == m_zone->MySpawnId();
			auto it = m_zone->Spawns().find(target);
			const ZoneSpawn *on = ourselves ? &m_zone->Self() : it != m_zone->Spawns().end() ? &it->second : nullptr;
			if (!on || on->npc != 0 || (ourselves && m_zone->Self().gm == 0))
				problem = 215;
		}
		break;
	case 7: // an item enchanted: one held (never from an item)
		if (item)
			return false;
		if (!m_zone->Items().count(Inventory::Cursor))
			problem = 233;
		break;
	case 14: { // our pet: it's the target now (pinstTarget = the pet)
		target = 0;
		uint16_t mine = m_zone->MySpawnId();
		for (const auto &[id, spawn] : m_zone->Spawns())
			if (spawn.pet_owner == mine && mine != 0) {
				target = id;
				break;
			}
		if (m_host.set_target)
			m_host.set_target(target);
		problem = CheckTarget(*info, target, false);
		break;
	}
	case 5: case 9: case 10: case 11: case 12: case 13: case 15: case 16: case 17: case 18: case 20: case 27:
		// A potion's spell lands on us (pinstTarget = us).
		if (item && item->item_class == 0 && item->item_type == 21 && item->stackable > 1) {
			target = m_zone->MySpawnId();
			if (m_host.set_target)
				m_host.set_target(target);
		}
		problem = CheckTarget(*info, target, false);
		break;
	case 8: case 21: case 22: case 23: case 28:
		problem = CheckTarget(*info, target, true);
		break;
	default: // the rest need no target (19, the coldain's, included: eqgame skips it)
		break;
	}
	if (problem != 0) {
		static const struct
		{
			uint32_t id;
			const char *text;
		} Messages[] = {
			{100, "Your target is out of range, get closer!"},
			{214, "You must first select a target for this spell!"},
			{215, "You must first target a living group member whose corpse you wish to summon."},
			{216, "This spell only works on summoned beings."},
			{217, "This spell will only work on your pet."},
			{218, "This spell only works on things that are flying."},
			{219, "This spell only works on the undead."},
			{220, "This spell only works on the plants."},
			{221, "This spell only works on corpses."},
			{222, "This spell only works on animals."},
			{223, "This spell only works on insects."},
			{224, "You can't drain yourself!"},
			{225, "This spell only works on specific giants."},
			{226, "This spell only works on specific dragons."},
			{233, "You must hold an item to cast the spell upon."},
		};
		const char *text = "";
		for (const auto &message : Messages)
			if (message.id == problem)
				text = message.text;
		Say(problem, text, ChatType::SpellFailure);
		return false;
	}

	// Away it goes: a gem's cast time (no AA or focus modifiers here), and the zone's to say the rest.
	if (!item) {
		m_cast_ms = 0;
		m_timeout = now_ms + static_cast<uint64_t>(std::max(info->cast_ms, 0));
		m_spell = spell;
		m_gem = gem;
	}
	m_zone->CastSpell(gem, spell, target, item ? static_cast<uint16_t>(slot) : 0xffff);
	if (gem < GemCount) {
		m_recast_ms[gem] = now_ms + AnswerMs;
		m_fizzle_ms = now_ms + AnswerMs;
	}
	if (!song) {
		++m_casts;
		++m_waiting;
	}
	return true;
}

uint32_t SpellCast::CheckTarget(const SpellInfo &spell, uint16_t target, bool area) const
{
	if (target == 0)
		return 214;
	uint16_t mine = m_zone->MySpawnId();
	const ZoneSpawn *spawn = nullptr;
	if (target == mine)
		spawn = &m_zone->Self();
	else if (auto it = m_zone->Spawns().find(target); it != m_zone->Spawns().end())
		spawn = &it->second;
	if (!spawn)
		return 214;
	// CDisplay::PlayerDistance(target, us, 0): across the ground. Ours is where we last moved.
	const ZoneProfile &me = m_zone->Profile();
	float tx = target == mine ? me.x : spawn->x, ty = target == mine ? me.y : spawn->y;
	float tz = target == mine ? me.z : spawn->z;
	float distance = std::sqrt((tx - me.x) * (tx - me.x) + (ty - me.y) * (ty - me.y));
	if (distance > spell.range)
		return 100;
	if (area) // a targeted AE: within 40 of our height too (ModelHeightOffset aside)
		return std::fabs(tz - me.z) <= 39.99f ? 0 : 100;
	int16_t body = spawn->body_type;
	switch (spell.target_type) {
	case 9: case 23: // animals
		return body == 21 ? 0 : 222;
	case 10: case 21: // the undead
		return body == 3 || body == 8 || body == 12 ? 0 : 219;
	case 11: case 22: // summoned
		return body == 24 || body == 27 || body == 8 || body == 28 ? 0 : 216;
	case 12: { // flying (EQPlayer::IsEntityFlying: these races, or not falling)
		uint16_t race = spawn->race;
		if (race == 79 || race == 25 || race == 34 || race == 47 || race == 56 || race == 69 || race == 109 ||
			race == 111 || race == 113)
			return 0;
		if (race == 72 || race == 73 || race == 114 || race == 141)
			return 218;
		return spawn->fly_mode == 1 ? 0 : 218;
	}
	case 13: case 20: // taps
		return target == mine ? 224 : 0;
	case 15: // corpses
		return spawn->npc < 2 ? 221 : 0;
	case 16: // plants
		return body == 25 ? 0 : 220;
	case 17: // giants
		return body == 9 ? 0 : 225;
	case 18: // dragons
		return body == 30 ? 0 : 226;
	case 27: case 28: // insects
		return body == 22 ? 0 : 223;
	default:
		return 0;
	}
}

void SpellCast::StopSpellCast(int how, int spell)
{
	const SpellInfo *info = Spell(spell);
	bool song = BardSong(info);
	if (!song) {
		m_casts = std::max(m_casts - 1, 0);
		m_waiting = std::max(m_waiting - 1, 0);
		if (m_zone->Profile().class_id == Bard) {
			if (m_cast_ms != 0) {
				m_spell = -1;
				m_gem = 0;
			}
			m_timeout = 0;
			m_cast_ms = 0;
			return;
		}
	}
	if (!info)
		return;
	if (how == 0)
		m_zone->StopCasting(); // an empty OP_ManaChange
	m_spell = -1;
	m_gem = 0;
	m_timeout = 0;
	if (!song) {
		// An interrupt of ours: we're told, and the zone passes "%1's casting is interrupted!" on
		// (CEverQuest::stripName of our name).
		if (how == 1) {
			Say(439, "Your spell is interrupted.", ChatType::SpellFailure);
			m_zone->SendSpellText(FormatEqString(m_eqstr, 12478, "%1's casting is interrupted!", {m_zone->Profile().name}));
		}
		return;
	}
	// A song ends: gems without a recast time are ready again.
	const ZoneProfile &me = m_zone->Profile();
	for (int gem = 0; gem < GemCount; ++gem) {
		const SpellInfo *on = Spell(me.mem_spells[gem]);
		if (!on || on->recast_ms == 0)
			m_recast_ms[gem] = 0;
	}
	m_fizzle_ms = 0;
	if (how == 1)
		Say(12686, "Your song ends abruptly.", ChatType::Spells);
	else
		Say(12687, "Your song ends.", ChatType::Spells);
}

void SpellCast::ZoneEvent(int type, int id, int value, uint64_t now_ms)
{
	if (!m_zone)
		return;
	using Type = ZoneSession::Event::Type;
	const SpellInfo *info = Spell(value);
	switch (static_cast<Type>(type)) {
	case Type::SpellMemorized: // FinishMemorizing: castable a second and its recast after
		if (id < GemCount)
			m_recast_ms[id] = now_ms + 1000 + (info ? info->recast_ms : 0);
		break;
	case Type::SpellForgotten: // ForgetMemorizedSpell: a song from it stops
		if (BardSong(Spell(m_spell)))
			StopSpellCast(0, m_spell);
		break;
	case Type::SpellRecast:
		if (!info)
			break;
		if (id < GemCount) { // (9, an ability's, adjusted by AltAdvManager: no abilities here)
			m_recast_ms[id] = now_ms + info->recast_ms;
			m_gem = GoneOff;
			m_fizzle_ms = now_ms + (m_zone->Profile().class_id == Bard ? 0 : info->recovery_ms);
		}
		break;
	case Type::ManaChanged:
		// Still on a gem: the zone stopped it before it went off; that gem's ready again.
		if (m_gem >= 0 && m_gem < GemCount) {
			m_recast_ms[m_gem] = now_ms;
			m_fizzle_ms = now_ms;
		}
		StopSpellCast(3, value != -1 ? value : m_spell);
		break;
	default:
		break;
	}
}

void SpellCast::CastBegan(const ZoneCast &cast, uint64_t now_ms)
{
	// StartCasting: ours, when we weren't casting already (the zone casting for us).
	if (!m_zone || cast.caster != m_zone->MySpawnId() || m_timeout != 0 || m_spell != -1 || !Spell(cast.spell))
		return;
	m_spell = cast.spell;
	m_timeout = now_ms + static_cast<uint64_t>(std::max(cast.cast_ms, 0));
	m_cast_ms = cast.cast_ms;
}

int SpellCast::Gauge() const
{
	const SpellInfo *info = Spell(m_spell);
	if (!info || !m_zone)
		return -1;
	if (m_zone->Profile().class_id == Bard && m_timeout == 0)
		return -1;
	if (m_now_ms > m_timeout || (info->cast_ms == 0 && m_cast_ms == 0))
		return -1;
	int64_t cast_ms = m_cast_ms != 0 ? m_cast_ms : info->cast_ms;
	return static_cast<int>(std::clamp<int64_t>(static_cast<int64_t>(m_timeout - m_now_ms) * 1000 / cast_ms, 0, 1000));
}

void SpellCast::Frame(uint64_t now_ms)
{
	m_now_ms = now_ms;
	if (!m_windows)
		return;
	bool casting = Gauge() >= 0;
	if (casting != m_windows->Shown("CastingWindow"))
		m_windows->Show("CastingWindow", casting);
}
