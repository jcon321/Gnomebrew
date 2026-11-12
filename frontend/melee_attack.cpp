// Our own melee swings while auto attack is on: CEverQuest::ProcessControls (@0053f337) swings the
// primary hand on __RangeAttackReady and the secondary on DAT_007cd845, through
// EQPlayer::DoAttack(13 / 14) (@0050a0f8): the hand's timer, the stance and CanIHitTarget's checks
// with their messages, and the fatigue the swing costs. The zone swings for itself (its own timer)
// and says what hits; what DoMissileHit works out for 13 and 14 (to-hit, the double attack and dual
// wield rolls) goes nowhere.

#include "frontend.h"
#include "game_values.h"
#include "inventory.h"
#include "skills.h"

#include <algorithm>

namespace
{
	constexpr uint8_t Standing = 100, NoControl = 102, Crouching = 111;
	constexpr int HandToHand = 28;

	bool TimedWeapon(const ZoneItem &item)
	{
		return item.item_type < 6 || item.item_type == 13 || item.item_type == 22;
	}
}

void FrontEnd::MeleeControls()
{
	// ProcessControls' local_10: we have control standing or crouching (not stunned, nor under
	// another's control, stance 102), and not dead.
	uint8_t stance = m_zone.Self().stance;
	bool control = !m_dead && !m_stunned && (stance == Standing || stance == Crouching);
	if (!m_attack || !control)
		return;
	if (m_range_ready) {
		m_range_ready = false;
		MeleeAttack(Inventory::Primary, Target());
	}
	if (m_attack && m_secondary_ready) {
		m_secondary_ready = false;
		MeleeAttack(Inventory::Secondary, Target());
	}
}

void FrontEnd::AttackBegins()
{
	// DoAttack, before anything else: attacking ends our invisibility (SPAs 12, 28, 29, 94) and
	// hiding (with eqstr 12682).
	for (int spa : {12, 28, 29, 94})
		m_buffs.RemoveAffect(spa);
	m_abilities.AttackEndsHiding();
}

void FrontEnd::AttackFatigue(const ZoneItem *weapon)
{
	// DoAttack: a swing or shot standing tires us by the weapon's weight (hand to hand's and under
	// 2.6 a third of the time, under 5.1 half of it, under 10.1 always, heavier 2 points).
	int weight = weapon ? weapon->weight : 0;
	int more = 0;
	if (weight < 26)
		more = Rand() % 3 == 0 ? 1 : 0;
	else if (weight < 51)
		more = Rand() % 2 == 0 ? 1 : 0;
	else if (weight < 101)
		more = 1;
	else
		more = 2;
	if (more == 0)
		return;
	// EQ_PC::SetFatigue: 0..100.
	ZoneProfile &me = m_zone.EditProfile();
	me.fatigue = static_cast<int8_t>(std::clamp(static_cast<int>(me.fatigue) + more, 0, 100));
}

void FrontEnd::MeleeAttack(int16_t hand, uint16_t target_id)
{
	// EQPlayer::DoAttack(13 primary, 14 secondary, 0, target). Not while casting, but for a bard.
	const ZoneProfile &me = m_zone.Profile();
	if (m_spell_cast.Casting() && me.class_id != 8)
		return;
	if (AttackStunned())
		return;
	AttackBegins();
	// The weapons back in our hands after shooting (UpdateItemSlot 7 and 8).
	if (m_last_attack == 11)
		m_ranged_look = true;
	m_last_attack = hand;
	// The hand's weapon, or hand to hand (the dummy item: type 22, skill 28) with it empty. Anything
	// but a weapon (a shield, a lantern) doesn't swing.
	const auto &items = m_zone.Items();
	auto it = items.find(hand);
	const ZoneItem *weapon = it != items.end() ? &it->second : nullptr;
	if (weapon && !GameValues::IsWeapon(*weapon))
		return;
	uint32_t delay;
	bool fists = !weapon || weapon->item_type == 22;
	if (fists) {
		int skill = weapon ? weapon->delay : HandToHand;
		delay = Skills::Get(skill).reuse_ms;
		// A monk's and a beastlord's fists quicken with level: 100 ms each 4 levels past 24 (an
		// Iksar monk's each 6), 1 s at most, a monk's set over 60 (2.4, 2.2, 2.0 s; an Iksar's 0.1
		// more) or 1.6 s over 50 in the Celestial Fists (10652, on the hands).
		int level = me.level;
		if (me.class_id == 7 && level > 26) {
			bool iksar = me.race == 128;
			int cut = std::min(((level - 24) / (iksar ? 6 : 4)) * 100, 1000);
			delay -= cut;
			if (level > 60) {
				delay = level > 62 ? 2000 : level > 61 ? 2200 : 2400;
				if (iksar)
					delay += 100;
			}
			auto hands = items.find(12); // the hands' slot
			if (hands != items.end() && hands->second.id == 10652 && level > 50)
				delay = 1600;
		}
		if (me.class_id == 15 && level > 26)
			delay -= std::min(((level - 24) / 5) * 100, 1000);
	}
	else
		delay = weapon->delay * 100u;
	delay = ModifyAttackSpeed(delay, weapon && weapon->item_type == 5);
	uint64_t &timer = hand == Inventory::Primary ? m_attack_timer_ms : m_secondary_timer_ms;
	if (m_now_ms - timer < delay)
		return;
	timer = m_now_ms;
	uint8_t stance = m_zone.Self().stance;
	if (stance != Standing) {
		ChatOut(stance == NoControl ? EqString(12368, "You do not have control of yourself right now.")
		                            : EqString(12369, "You must be standing to attack!"),
			ChatType::Red);
		return;
	}
	if (!CanIHitTarget(target_id, 0))
		return;
	AttackFatigue(weapon);
}

int FrontEnd::Rand()
{
	// RandNbr.
	m_random = m_random * 214013u + 2531011u;
	return static_cast<int>((m_random >> 16) & 0x7fff);
}

void FrontEnd::MeleePassageOfTime(uint32_t primary_delay)
{
	// DoPassageOfTime: the secondary hand's ready once its weapon's delay has gone by (hand to hand
	// with it empty; with anything there but a weapon, the primary's delay).
	const auto &items = m_zone.Items();
	auto it = items.find(Inventory::Secondary);
	const ZoneItem *item = it != items.end() && it->second.delay != 0 ? &it->second : nullptr;
	uint32_t delay = primary_delay;
	if (!item)
		delay = ModifyAttackSpeed(Skills::Get(HandToHand).reuse_ms, false);
	else if (TimedWeapon(*item))
		delay = ModifyAttackSpeed(item->item_type == 22 ? Skills::Get(item->delay).reuse_ms : item->delay * 100u, false);
	if (delay <= m_now_ms - m_secondary_timer_ms)
		m_secondary_ready = true;
}
