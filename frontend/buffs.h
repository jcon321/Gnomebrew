#pragma once

// Our buffs, as eqgame keeps them (EQ_Character's Buff[15] and BuffCasterId, of pinstPCData):
//   - EQ_Character::HitBySpell (@004c8492), from EQPlayer::IWasHit on a spell's OP_Action (231)
//     with flag 4 (it landed) on us: a spell with a duration formula goes in a slot
//     (FindAffectSlot @004c7a3e), {type 2 (4 for a tap we cast), the caster's level (a potion's,
//     spells 1252..1266, from the packet), a bard's instrument, its Activated, the spell, its
//     duration (CalculateSpellDuration @004ca746)}, unless Divine Aura's on us (but for 982).
//   - FindAffectSlot, for us: not when a buff of ours blocks it (IsStackBlocked @004c830b, effect
//     148), or a bard's beneficial movement song meets a detrimental one of a non song; the same
//     spell again takes its slot, but not from a higher level caster, nor for an Eye of Zomm,
//     Donal's (101) or a horse. Otherwise each buff is held against it effect by effect (the same
//     effect in the same place, not one isAttributeStackable @004d7201 lets stack, nor a bard's
//     regen song under a non song, nor a DoT or debuff's HP or AC, nor CHA used as a spacer): an
//     illusion, Donal's and a list of spells can't be overwritten; values of opposite signs
//     overwrite (but a snare can't be by a speed buff, nor a DoT by a heal); otherwise the new one
//     must be as strong (CalcAffectChange @004c657d, at each caster's level; attack speed by its own
//     rules), and a beneficial one takes the first slot it beats, or the first empty one, while
//     two detrimental ones overwrite at once. No slot: a detrimental one takes the first
//     beneficial buff's, a beneficial one doesn't land. What it overwrites fades (with its text)
//     without telling the zone, which has done the same.
//   - ProcessAffects (@004c7448) every 6 s (DoPassageOfTime): a counting buff (type 2 and up) at
//     0 fades and tells the zone (RemovePCAffect); the rest count down (-1 never), and a
//     levitation (not a song's) or an illusion warns as it gets to its last tick.
//   - OP_Buff from the zone (HandleWorldMessage's 0x4132): how 3 puts the buff in its slot;
//     otherwise the buffs with its type and spell take it, or with how 1 fade (with their text,
//     not telling the zone).
//   - RemoveMyAffect (@004d0337), a buff clicked in the buff window (CBuffWindow::WndNotification):
//     only a beneficial one, not Donal's nor one decaying by formula 107, 108, 120 or 122 ("You
//     cannot remove this effect."); it fades and the zone is told twice (RemovePCAffect, then
//     NotifyPCAffectChange with how 1).
//   - TotalSpellAffects (@004c6b6d): an effect's total over the buffs, each at its caster's level
//     and a song's instrument, turned round in a type 4 buff; movement (3) is the worst snare, or
//     else the best speed.
//   - eqspa_movement_rate (@004caaba), which HitBySpell and RemovePCAffectex call: a root (99) or
//     else the movement total, in percent of the run speed, -65 at the least (below -999 rooted);
//     ProcessControls adds it to the run or walk speed, at most 1.092006 (more with Fleet of Foot);
//     running with none, Innate Run Speed's;
// Not done: the effects that act as a buff lands or fades (levitation, invisibility, seeing
// invisible, infravision, illusions, the Eye of Zomm, mounts, binding, gating...), the mana and
// fatigue a buff gives each tick (the zone's updates carry them), the AAs (a bard's instrument
// mastery, Jam Fest), the item effects TotalSpellAffects adds, and a buff's caster's
// level for its stacking when it came with the profile (it has the level it was cast at).

#include "zone_session.h"

#include <cstdint>
#include <functional>
#include <string>

class EqStr;
class SpellFile;
struct SpellInfo;

class Buffs
{
public:
	static constexpr int Slots = ZoneProfile::BuffSlots;
	// What the chat says (a buff's fading text, a warning) and in what color (a ChatType).
	using Chat = std::function<void(const std::string &text, int type)>;

	void Attach(ZoneSession &zone, const SpellFile *spells, const EqStr *eqstr, Chat chat);
	// A new zone: the buffs in the profile, the tick starting now.
	void Reset(uint64_t now_ms);
	// The zone's: SpellAction (one landing on us) and BuffChanged.
	void ZoneEvent(const ZoneSession::Event &event);
	bool Frame(uint64_t now_ms); // ProcessAffects, every 6 s: true when it ticked
	// RemoveMyAffect: false (and why in the chat) when it can't be.
	bool ClickOff(int slot);
	// EQ_Character::GetPCSpellAffect then RemovePCAffect: the first buff with the effect goes (its
	// fading text, the zone told); false for none.
	bool RemoveAffect(int spa);
	// EQ_Character::GetPCSpellAffect: whether a buff has the effect.
	bool HasAffect(int spa) const;

	const ZoneBuff &Slot(int slot) const;
	// IsValidAffect: a buff there of a spell we know.
	const SpellInfo *SlotSpell(int slot) const;
	int Total(int spa) const; // TotalSpellAffects
	// TotalSpellAffects' own branch for attack speed (SPA 11, with the worn items): the worst slow
	// (a value under 100, the buffs' or the items') if there's one; else the best spell haste and
	// the best worn haste (110 at most under level 26) together, less the 100 counted twice, plus
	// overhaste (SPA 98, over level 49: 110 at most) and bard haste (SPA 119: its value over 100,
	// 10 at most under level 51, 25 from it), up to level + 125 (+ the bard haste; 185 over level
	// 50, 200 over 59). 0 none; else a percentage of the normal speed.
	int AttackSpeed() const;
	// eqspa_movement_rate as ProcessControls adds it to our speed (per tick); below -1000 rooted.
	// Running (run mode, DAT_0079856d) with no speed buff or snare, Innate Run Speed is the modifier;
	// then at most ProcessControls' cap, more with Fleet of Foot (AltAdv::RunSpeedCap). eqgame works
	// the rate out each second (DoPassageOfTime) and as a buff lands or fades; this every frame.
	float SpeedModifier(float run_speed, bool running = false) const;
	uint32_t Serial() const { return m_serial; } // counts the changes

	// EQ_Character::CalculateSpellDuration: ticks at a caster's level; -1 for ever. On us with
	// Permanent Illusion (AA 0x37, `permanent_illusion` its rank), a beneficial illusion (SPA 0x3a)
	// with a duration lasts 10000.
	static int Duration(const SpellInfo &spell, int level, int permanent_illusion = 0);
	// EQ_Character::CalcAffectChange: an effect's value at a caster's level (a decaying formula's
	// from how long `buff` has had, if there's one).
	static int Value(const SpellInfo &spell, int level, int effect, const ZoneBuff *buff);

private:
	struct Caster
	{
		uint16_t id = 0;
		int level = 1;
		int class_id = 0;
		bool npc = false;
	};

	const SpellInfo *Spell(int id) const;
	bool FindCaster(uint16_t id, Caster &caster) const;
	void Land(const ZoneCast &cast);
	int FindAffectSlot(int id, const SpellInfo &spell, const Caster &caster);
	bool StackBlocked(int id, const SpellInfo &spell) const;
	// RemovePCAffectex: its fading text, then the slot's empty; `tell` the zone which spell went.
	void Remove(int slot, bool tell);
	void Say(uint32_t id, const std::string &fallback, int type) const;

	ZoneSession *m_zone = nullptr;
	const SpellFile *m_spells = nullptr;
	const EqStr *m_eqstr = nullptr;
	Chat m_chat;
	ZoneBuff m_slots[Slots];
	uint16_t m_casters[Slots] = {}; // BuffCasterId (UpdateBuffSlotIndicator)
	uint64_t m_tick_ms = 0;         // ActorInfo's Last6000msTick
	uint32_t m_serial = 0;
};
