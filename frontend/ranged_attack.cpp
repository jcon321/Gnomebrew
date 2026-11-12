// Our own archery and throwing: the Range button (CActionsWnd::CombatPageWndNotification, a hot
// button holding it) and EQPlayer::DoAttack(11) (@0050a0f8), with its ammunition (HandleAmmo
// @0050b0fe) and the shot to the zone (DoMissileHit @004f3228), and the attack timer that readies it
// (EQ_Character::DoPassageOfTime's __RangeAttackReady).

#include "frontend.h"
#include "abilities.h"
#include "game_values.h"
#include "inventory.h"
#include "opcodes.h"
#include "skills.h"

#include <cmath>
#include <cstring>

namespace
{
	// DoPassageOfTime's and DoAttack's "a weapon": an item type under 6, 13 or 22 (hand to hand's).
	bool TimedWeapon(const ZoneItem &item)
	{
		return item.item_type < 6 || item.item_type == 13 || item.item_type == 22;
	}
}

uint32_t FrontEnd::ModifyAttackSpeed(uint32_t delay, bool bow) const
{
	// EQPlayer::ModifyAttackSpeed (@0050a039): haste, then for a bow the first quiver in the general
	// slots with any weight reduction takes WR * 0.25% of the delay and 1 more off, unless that
	// leaves 1000 ms or less.
	delay = Abilities::ModifyAttackSpeed(delay, m_buffs.AttackSpeed());
	if (delay == 0 || !bow)
		return delay;
	const auto &items = m_zone.Items();
	for (int16_t slot = 22; slot <= 29; ++slot) {
		auto it = items.find(slot);
		if (it == items.end() || it->second.item_class != 1 || it->second.bag_type != 2 || it->second.bag_wr == 0)
			continue;
		uint32_t cut = static_cast<uint32_t>(static_cast<int>(it->second.bag_wr * 0.0025f * static_cast<float>(delay))) + 1;
		uint32_t less = delay - cut;
		return less < 1001 ? delay : less;
	}
	return delay;
}

uint32_t FrontEnd::WeaponDelay(int16_t slot) const
{
	// DoPassageOfTime: the slot's weapon (one with a delay), or for the primary hand to hand (the
	// dummy item, type 22, skill 28: its reuse time); 0 when it isn't a weapon.
	const auto &items = m_zone.Items();
	auto it = items.find(slot);
	const ZoneItem *item = it != items.end() && it->second.delay != 0 ? &it->second : nullptr;
	if (!item)
		return slot == Inventory::Primary ? ModifyAttackSpeed(Skills::Get(28).reuse_ms, false) : 0;
	if (!TimedWeapon(*item))
		return 0;
	uint32_t delay = item->item_type == 22 ? Skills::Get(item->delay).reuse_ms : item->delay * 100u;
	return ModifyAttackSpeed(delay, item->item_type == 5);
}

void FrontEnd::RangedPassageOfTime(uint64_t now_ms)
{
	// A ready swing ProcessControls didn't take (we had no control): both hands start over.
	if (m_attack && m_range_ready) {
		m_range_ready = false;
		m_secondary_ready = false;
		m_attack_timer_ms = m_secondary_timer_ms = now_ms;
	}
	uint32_t delay = WeaponDelay(Inventory::Primary);
	uint32_t range = WeaponDelay(Inventory::Range);
	if (m_last_attack == 11 && range != 0)
		delay = range;
	if (delay <= now_ms - m_attack_timer_ms)
		m_range_ready = true;
	MeleePassageOfTime(delay);
}

void FrontEnd::RangeAttackButton()
{
	// CombatPageWndNotification: only when it's ready.
	if (!m_range_ready)
		return;
	m_range_ready = false;
	RangedAttack(Target());
}

void FrontEnd::RangedAttack(uint16_t target_id)
{
	// EQPlayer::DoAttack(11, 0, target).
	auto text = [this](uint32_t id, const char *fallback) { return EqString(id, fallback); };
	if (m_spell_cast.Casting() && m_zone.Profile().class_id != 8)
		return;
	AttackBegins();
	auto &items = m_zone.EditItems();
	auto range_it = items.find(Inventory::Range);
	// The bow (or what's thrown) in the hand while shooting (UpdateItemSlot 7 and 8).
	if (m_last_attack != 11) {
		m_last_attack = 11;
		m_ranged_look = true;
	}
	if (range_it == items.end())
		return;
	ZoneItem &weapon = range_it->second;
	if (!GameValues::IsWeapon(weapon) && weapon.item_type != 13 && weapon.item_type != 22)
		return;
	uint32_t delay = ModifyAttackSpeed(weapon.item_type == 22 ? Skills::Get(weapon.delay).reuse_ms : weapon.delay * 100u,
		weapon.item_type == 5);
	uint64_t was = m_attack_timer_ms;
	if (m_now_ms - was < delay)
		return;
	m_attack_timer_ms = m_now_ms;
	uint8_t stance = m_zone.Self().stance;
	if (stance != 100) {
		ChatOut(stance == 102 ? text(12368, "You do not have control of yourself right now.")
		                      : text(12369, "You must be standing to attack!"),
			ChatType::Red);
		return;
	}
	// Not closer than 30, nor more than 40 above or below (but in the Plane of Sky... zone 71).
	const ZoneSpawn *target = nullptr;
	if (auto it = m_zone.Spawns().find(target_id); it != m_zone.Spawns().end())
		target = &it->second;
	float height = 0;
	bool blocked = false;
	if (target && m_zone_view) {
		const ZoneProfile &me = m_zone.Profile();
		float dx = target->x - me.x, dy = target->y - me.y;
		if (std::sqrt(dx * dx + dy * dy) < 30.0f) {
			ChatOut(text(12698, "Your target is too close to use a ranged weapon!!"), ChatType::Yellow);
			m_attack_timer_ms = was;
			return;
		}
		height = std::fabs((target->z - m_zone_view->CenterHeight(target_id)) - (me.z - m_zone_view->CenterHeight(m_zone.MySpawnId())));
	}
	if (height > 39.99f && m_zone.Profile().zone_id != 71) {
		ChatOut(text(12699, "Your target is too far above/below you."), ChatType::Default);
		blocked = true;
		m_attack_timer_ms = was;
	}
	// The reach: a bow's and its arrow's, else the ammo's, else what's thrown.
	auto ammo_it = items.find(Inventory::Ammo);
	float reach = 0;
	if (weapon.item_type == 5) {
		reach = weapon.range;
		if (ammo_it != items.end() && ammo_it->second.item_type == 27)
			reach += ammo_it->second.range;
	}
	else if (ammo_it == items.end())
		reach = weapon.range;
	else
		reach = ammo_it->second.range;
	if (!CanIHitTarget(target_id, reach) || blocked) {
		m_attack_timer_ms = was;
		return;
	}
	AttackFatigue(&weapon);
	// HandleAmmo, and what's used up.
	int how = HandleAmmo();
	range_it = items.find(Inventory::Range);
	if (range_it == items.end())
		return;
	if (how == 0) {
		// The weapon itself, thrown and gone.
		if (target_id == 0)
			return;
		DoMissileHit(target_id, range_it->second);
		items.erase(range_it);
		m_zone.MoveItem(Inventory::Range, -1, 0);
		return;
	}
	if (how == 1) {
		ammo_it = items.find(Inventory::Ammo);
		if (ammo_it == items.end() || target_id == 0)
			return;
		DoMissileHit(target_id, ammo_it->second);
		// Endless Quiver (GetAbility 0x54) keeps it. Else one off the ammo; a lone one is taken from
		// the same item in the general slots or their bags instead, the last found.
		if (m_zone.Profile().aa_ranks[0x54] != 0)
			return;
		int16_t from = Inventory::Ammo;
		if (ammo_it->second.charges < 2) {
			uint16_t id = ammo_it->second.id;
			for (int16_t slot = 22; slot < 30; ++slot) {
				auto it = items.find(slot);
				if (it == items.end())
					continue;
				if (it->second.id == id) {
					from = slot;
					continue;
				}
				if (it->second.item_class == 1)
					for (int n = 0; n < it->second.bag_slots; ++n) {
						int16_t inside = Inventory::BagSlot(slot, n);
						auto in = items.find(inside);
						if (in != items.end() && in->second.id == id)
							from = inside;
					}
			}
		}
		m_zone.DeleteCharge(from);
		return;
	}
	// 2: no ammunition to speak of; a throwing weapon (19) goes itself.
	if (range_it->second.item_type != 19 || target_id == 0)
		return;
	DoMissileHit(target_id, range_it->second);
	if (range_it->second.no_drop == 0 || range_it->second.charges == 0) { // CanDropItem
		items.erase(range_it);
		m_zone.MoveItem(Inventory::Range, -1, 0);
	}
	else
		m_zone.DeleteCharge(Inventory::Range);
}

int FrontEnd::HandleAmmo()
{
	// EQPlayer::HandleAmmo: 0 throw the weapon itself, 1 use the ammo slot's, 2 nothing to shoot.
	auto &items = m_zone.EditItems();
	auto range_it = items.find(Inventory::Range);
	if (range_it == items.end())
		return 2;
	const ZoneItem &weapon = range_it->second;
	uint8_t type = weapon.item_type;
	if (type != 19 && type != 5 && type != 6)
		return weapon.range != 0 ? 0 : 2;
	auto ammo_it = items.find(Inventory::Ammo);
	auto none = [&] {
		ChatOut(EqString(12875, "You have run out of ammo!!"), ChatType::Default);
		return 2;
	};
	if (ammo_it == items.end()) {
		// A stack of throwing weapons in the range slot: all but one go to the ammo slot.
		if (type != 19 || weapon.charges < 2)
			return none();
		ZoneItem rest = weapon;
		rest.slot = Inventory::Ammo;
		rest.charges = static_cast<int8_t>(weapon.charges - 1);
		m_zone.MoveItem(Inventory::Range, Inventory::Ammo, weapon.charges - 1);
		range_it->second.charges = 1;
		items[Inventory::Ammo] = rest;
		return 1;
	}
	const ZoneItem &ammo = ammo_it->second;
	bool fits = (type == 19 && ammo.id == weapon.id) || (type == 5 && ammo.item_type == 27 && ammo.charges != 0) ||
	            (type == 6 && ammo.item_type == 28 && ammo.charges != 0) || (type == 19 && ammo.item_type == 19 && ammo.charges != 0);
	return fits ? 1 : none();
}

void FrontEnd::DoMissileHit(uint16_t target_id, const ZoneItem &item)
{
	// DoMissileHit(us, target, item, 11): we turn to face them (AimAtTarget: within 64 of our
	// heading, square on; the pitch isn't ours), and OP_CombatAbility {target, 11, the skill: the
	// item's (SkillUsed), a type 22's own, 231 for type 9}. The zone does the rest.
	auto it = m_zone.Spawns().find(target_id);
	if (it == m_zone.Spawns().end() || it->second.npc > 1)
		return;
	if (m_zone_view) {
		ZoneMovement me = m_zone_view->Movement();
		const ZoneSpawn &them = it->second;
		float dx = them.x + them.dx * 2 - me.x, dy = them.y + them.dy * 2 - me.y;
		float bearing = std::atan2(dx, dy) * 180.0f / 3.14159265f;
		if (bearing < 0)
			bearing += 360;
		bearing = bearing * 511.5f / 360.0f;
		float diff = std::fabs(bearing - me.heading);
		if (diff > 256)
			diff = 512 - diff;
		if (diff < 64)
			m_zone_view->Turn(bearing - me.heading);
	}
	uint32_t skill = item.item_type == 9 ? 231 : item.item_type == 22 ? item.delay : static_cast<uint32_t>(GameValues::SkillUsed(item.item_type));
	uint8_t body[12] = {};
	uint32_t attack = 11;
	std::memcpy(body, &target_id, 2);
	std::memcpy(body + 4, &attack, 4);
	std::memcpy(body + 8, &skill, 4);
	m_zone.SendSkill(ZoneOp::CombatAbility, body, sizeof(body));
}
