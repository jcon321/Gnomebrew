#include "spell_casting.h"

#include "chat_text.h"
#include "spell_file.h"

#include <cctype>
#include <cmath>

namespace
{
	constexpr uint8_t SpellType = 231;
	constexpr int ClassBard = 8;
	constexpr int OwnSpellColor = 0x108;   // ChatType::Spells
	constexpr int OtherSpellColor = 0x120; // someone else beginning to cast

	// Where a spawn is, in the world's (render) space, and its heading: ours as we last moved.
	bool Where(const ZoneSession &zone, uint16_t id, render::Vec3 &at, float &heading)
	{
		if (id != 0 && id == zone.MySpawnId()) {
			const ZoneProfile &me = zone.Profile();
			at = {me.y, me.x, me.z};
			heading = me.heading;
			return true;
		}
		auto it = zone.Spawns().find(id);
		if (it == zone.Spawns().end())
			return false;
		at = {it->second.y, it->second.x, it->second.z};
		heading = it->second.heading;
		return true;
	}

	const ZoneSpawn *FindSpawn(const ZoneSession &zone, uint16_t id)
	{
		if (id == 0)
			return nullptr;
		if (id == zone.MySpawnId())
			return &zone.Self();
		auto it = zone.Spawns().find(id);
		return it != zone.Spawns().end() ? &it->second : nullptr;
	}

	// SpellUsesDragonBreathEffect (@004d6b29): spells.eff effects 61..64.
	bool DragonBreath(const SpellInfo &spell)
	{
		return spell.effect > 0x3c && spell.effect < 0x41;
	}

	// orderedStringExpansion of "%1" and a spell's text: the name stands in for %1.
	std::string Expand(const std::string &text, const std::string &name)
	{
		std::string out;
		for (size_t i = 0; i < text.size(); ++i) {
			if (text[i] == '%' && i + 1 < text.size() && text[i + 1] == '1') {
				out += name;
				++i;
			}
			else
				out += text[i];
		}
		return out;
	}
}

bool SpellCasting::IsRoguePoison(int spell)
{
	return spell == 87 || spell == 88 || spell == 476 || (spell >= 760 && spell <= 768) || (spell >= 821 && spell <= 823) ||
	       (spell >= 1465 && spell <= 1467) || spell == 1471 || (spell >= 1832 && spell <= 1883) || spell == 1965 ||
	       spell == 1966 || spell == 2710 || spell == 2711 || spell == 2717 || spell == 2718 || (spell >= 2720 && spell <= 2726) ||
	       spell == 2729 || spell == 2774 || spell == 2784 || spell == 2834 || spell == 2835 || spell == 2876 ||
	       spell == 2889 || spell == 2891;
}

std::string SpellCasting::Format(uint32_t id, const std::string &fallback, const std::vector<std::string> &args) const
{
	return FormatEqString(m_eqstr, id, fallback, args);
}

void SpellCasting::Handle(const ZoneSession::Event &event, const ZoneSession &zone, std::vector<Line> &lines,
	std::vector<Animation> &animations, std::vector<SpellEffects::Request> &effects) const
{
	if (!m_spells)
		return;
	switch (event.type) {
	case ZoneSession::Event::Type::CastBegan: BeginCast(event.cast, zone, lines, animations, effects); break;
	case ZoneSession::Event::Type::SpellAction: Action(event.cast, zone, animations, effects); break;
	case ZoneSession::Event::Type::Damaged: Damaged(event.cast, zone, lines, animations); break;
	default: break;
	}
}

void SpellCasting::BeginCast(const ZoneCast &cast, const ZoneSession &zone, std::vector<Line> &lines,
	std::vector<Animation> &animations, std::vector<SpellEffects::Request> &effects) const
{
	const ZoneSpawn *caster = FindSpawn(zone, cast.caster);
	const SpellInfo *spell = m_spells->Find(cast.spell);
	if (!caster || !spell)
		return;
	if (DragonBreath(*spell)) {
		// The breath goes off a second on (process_physics, the Horse timer), for 1.25 seconds.
		animations.push_back({cast.caster, spell->casting_anim});
		SpellEffects::Request effect;
		effect.stage = 0;
		effect.effect = spell->new_effect;
		effect.old_effect = spell->effect;
		effect.spell = cast.spell;
		effect.caster = effect.target = cast.caster;
		effect.duration_ms = 1250;
		effect.delay_ms = 1000;
		effects.push_back(effect);
		return;
	}
	bool mine = cast.caster == zone.MySpawnId();
	std::string text;
	if (caster->class_id == ClassBard) {
		if (mine)
			text = spell->you_cast;
		else if (!spell->other_casts.empty())
			text = Format(12207, "%1%2", {caster->DisplayName(), spell->other_casts});
	}
	else if (mine)
		text = Format(12205, "You begin casting %1.", {spell->name});
	else
		text = Format(12206, "%1 begins to cast a spell.", {caster->DisplayName()});
	bool gm = zone.Self().gm != 0;
	if (mine && gm)
		text += Format(12208, "Item Cast Time (%1)", {std::to_string(cast.cast_ms)});
	if (gm)
		text += " (" + spell->name + ")";
	if (!text.empty() && !(mine && caster->class_id != ClassBard && IsRoguePoison(cast.spell)))
		lines.push_back({text, mine ? OwnSpellColor : OtherSpellColor});
	SpellEffects::Request effect;
	effect.stage = 0;
	effect.effect = spell->new_effect;
	effect.old_effect = spell->effect;
	effect.spell = cast.spell;
	effect.caster = effect.target = cast.caster;
	effect.duration_ms = cast.cast_ms;
	effect.casting_anim = spell->casting_anim;
	effects.push_back(effect);
}

void SpellCasting::Action(const ZoneCast &cast, const ZoneSession &zone, std::vector<Animation> &animations,
	std::vector<SpellEffects::Request> &effects) const
{
	if (cast.type != SpellType || !FindSpawn(zone, cast.target))
		return;
	const SpellInfo *spell = m_spells->Find(cast.spell);
	if (!spell || spell->target_type == 0)
		return;
	const ZoneSpawn *caster = FindSpawn(zone, cast.caster);
	if (caster && !DragonBreath(*spell) && (caster->class_id != ClassBard || cast.instrument_mod > 10))
		animations.push_back({cast.caster, spell->casting_anim});
	SpellEffects::Request effect;
	effect.stage = 2;
	effect.effect = spell->new_effect;
	effect.old_effect = spell->effect;
	effect.spell = cast.spell;
	effect.caster = caster ? cast.caster : 0;
	effect.target = cast.target;
	// A summoning's spot: at the caster (71), or `range` in front of the target (33, 106, 108).
	render::Vec3 at;
	float heading;
	uint8_t spa = spell->first_spa;
	if (spa == 71 && caster && Where(zone, cast.caster, at, heading)) {
		effect.at_point = true;
		effect.point = at;
	}
	else if ((spa == 33 || spa == 71 || spa == 106 || spa == 108) && Where(zone, cast.target, at, heading)) {
		float a = render::HeadingToRadians(heading);
		effect.at_point = true;
		effect.point = at + render::Vec3{std::cos(a), std::sin(a), 0} * spell->range;
	}
	effects.push_back(effect);
}

void SpellCasting::DeathBlow(const ZoneCast &cast, bool told, const ZoneSession &zone, std::vector<Line> &lines,
	std::vector<Animation> &animations) const
{
	if (m_spells)
		Damaged(cast, zone, lines, animations, told ? 1 : 0);
}

void SpellCasting::Damaged(const ZoneCast &cast, const ZoneSession &zone, std::vector<Line> &lines,
	std::vector<Animation> &animations, int told_override) const
{
	if (cast.type != SpellType)
		return; // melee: Combat's
	const ZoneSpawn *victim = FindSpawn(zone, cast.target);
	const ZoneSpawn *attacker = FindSpawn(zone, cast.caster);
	const SpellInfo *spell = m_spells->Find(cast.spell);
	if (!victim || (cast.caster != 0 && !attacker) || !spell)
		return;
	uint16_t me = zone.MySpawnId();
	// msg_successful_hit: someone else's on someone else is told only near the target.
	bool told = true;
	if (cast.target != me && cast.caster != me) {
		const ZoneProfile &self = zone.Profile(); // where we are
		float dx = victim->x - self.x, dy = victim->y - self.y, dz = victim->z - self.z;
		told = std::sqrt(dx * dx + dy * dy + dz * dz) <= 75;
	}
	if (told_override >= 0)
		told = told_override != 0;
	bool on_me = cast.target == me;
	std::string text;
	if (on_me) {
		if (cast.damage < 1)
			text = spell->cast_on_you;
		else if (cast.damage < 2)
			text = Format(12204, "%1  You have taken %2 point of damage.", {spell->cast_on_you, std::to_string(cast.damage)});
		else
			text = Format(12203, "%1  You have taken %2 points of damage.", {spell->cast_on_you, std::to_string(cast.damage)});
	}
	else if (!spell->cast_on_other.empty())
		text = Expand("%1" + spell->cast_on_other, victim->DisplayName());
	if (told && !text.empty()) {
		text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
		lines.push_back({text, OwnSpellColor});
	}

	if (victim->npc == 2 || victim->npc == 3)
		return; // a corpse
	int animation = -1;
	if (spell->target_anim != 0 && spell->target_anim < 0x4f && (victim->stance == 100 || victim->stance == 102))
		animation = spell->target_anim;
	if (animation < 0 && cast.damage > 0 && spell->good_effect == 0)
		animation = 13; // D02, hit
	if (animation >= 0)
		animations.push_back({cast.target, animation});
}
