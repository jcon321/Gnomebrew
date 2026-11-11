#pragma once

// Our own casting from the spell gems: eqgame's EQ_Character::CastSpell (@004c483b) for a gem,
// StopSpellCast (@004cb510), what the zone's answers do (msg_launchspell_info's 3 @004dd19d, the
// OP_ManaChange handler @004e4509, CEverQuest_StartCasting @0052cbe7 for us) and the casting
// window (CCastingWnd @0040a10f, its gauge GetSpellCastingTime @00435f28). The state is the local
// player's ActorInfo's (CastingSpellId, CastingSpellGemNumber, CastingTimeout, CastingSpellCastTime,
// RecastTimeout[8], FizzleTimeout) and the spell bar's two counters: the zone's answers it waits for
// (DAT_007d0254, which the book's requests share) and the casts (DAT_007d0258).
//   - A gem's left click (CCastSpellWnd::HandleSpellLeftClick @0040a4f5, /cast n): with its recast
//     and the last spell's recovery past, and nothing being cast, CastSpell if the bar isn't waiting;
//     otherwise a bard's song stops (StopSpellCast2), or "You haven't recovered yet..." (12029).
//   - CastSpell's checks, each with its message: standing (12441), not casting already (12442), the
//     level (12443), seeing the target for a detrimental spell or a dispel (108, CastRay), standing
//     again (213, a bard's song: conscious, 212), a recourse spell not on ourselves (210), the mana
//     (199), then by the target type: a target (214), in range (100; spell range, 2D), within 40 of
//     our height for a targeted AE, and of the body type the spell needs (216..226); a pet spell
//     targets our pet (214 without one), an enchantment needs an item held (233).
//   - Then: CastingTimeout = now + the cast time, the spell and the gem; OP_CastSpell with the
//     target; every gem's recast and the recovery 10 seconds off (until the zone answers), and the
//     bar waits (both counters, not for a bard's song).
//   - The zone's answers: OP_BeginCast starts nothing of ours (it's already going) unless we
//     weren't casting; OP_MemorizeSpell 3 (it went off) sets the gem's recast (the spell's recast
//     time) and the recovery (the spell's recovery time, none for a bard), and takes the gem off the
//     cast; OP_ManaChange ends the cast (StopSpellCast 3: the counters down, the spell, gem and timeout
//     cleared), and if the gem is still on it (an interrupt or a fizzle), that gem and the recovery
//     are ready at once. The zone's OP_TokenText says why.
//   - A bard's song keeps going until a gem click stops it (StopSpellCast 0: an empty
//     OP_ManaChange, "Your song ends."), and gems without a recast time come back at once.
//   - The casting window shows while GetSpellCastingTime has something: the time left of the cast,
//     0..1000, emptying (EQType 7).
// Not done: CEverQuest_IsOkToTransact (trading and the like), divine aura (SPA 40) and stuns
// (StunnedState, OP_Stun), our invisibility and sneaking ending as we cast, the AA and focus cast
// time modifiers (GetAACastingTimeModifier, GetFocusCastingTimeModifier: none of either here) and
// the focus's range, Modulation and Mystical Transvergance's checks (items and abilities, not gems),
// PvP deities, being hit interrupting a bard (StopSpellCast 1), abilities (gem 9) and the spell_crc
// (the zone ignores it).
//   - An item's click effect (gem 10): not while something's cast (but a bard's song); a
//     must-equip effect only worn (179), a class-checked one only by those who can use it (181),
//     either from its EffectLevel1 (136); a stackable item (IsStackable 1) never; out of charges
//     (182). No mana; a potion's targeted spell lands on us; no cast time of ours (the zone's
//     OP_BeginCast starts it); OP_CastSpell with the item's slot.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class EqStr;
class SpellFile;
class WindowManager;
class ZoneSession;
struct SpellInfo;
struct ZoneCast;
struct ZoneItem;
struct ZoneSpawn;

class SpellCast
{
public:
	struct Host
	{
		std::function<uint16_t()> target;           // our target's spawn id; 0 none
		std::function<void(uint16_t)> set_target;   // a pet spell targets our pet (pinstTarget)
		std::function<bool(uint16_t)> can_see;      // CastRay from us to a spawn
		std::function<void(const std::string &, int)> chat;
	};

	void Attach(WindowManager &windows, ZoneSession &zone, const SpellFile *spells, const EqStr *eqstr, Host host);
	void Reset(); // a new zone: nothing cast, nothing waited for

	// HandleSpellLeftClick, without a book spell on the cursor.
	void GemClicked(int gem, uint64_t now_ms);
	// /stopsong (@004f5c74): a bard's StopSpellCast2 (StopSpellCast 0 of whatever's cast, a song
	// or not, or nothing); anyone else, nothing.
	void StopSong();
	// CastSpell for an item's click effect (gem 10, CInvSlot_DoDrinkEatPoison): from the slot it's in.
	bool CastItem(const ZoneItem &item, int16_t slot, uint64_t now_ms);
	// The zone's: ZoneSession::Event's SpellMemorized, SpellForgotten, SpellRecast, ManaChanged.
	void ZoneEvent(int type, int id, int value, uint64_t now_ms);
	void CastBegan(const ZoneCast &cast, uint64_t now_ms); // OP_BeginCast, anyone's
	void Frame(uint64_t now_ms); // CCastingWnd::OnProcessFrame: the casting window up while casting

	bool Casting() const; // IsValidSpellIndex(CastingSpellId)
	// RefreshSpellGemButtons: the gem can be cast (its spell's there, its recast and the recovery
	// past, nothing being cast).
	bool GemReady(int gem, uint64_t now_ms) const;
	int Gauge() const; // GetSpellCastingTime, at the last Frame: 0..1000 left, -1 not casting

	// The spell bar's answers to wait for (DAT_007d0254): the book's requests count too.
	bool Waiting() const { return m_waiting != 0; }
	void Wait() { ++m_waiting; }
	void Answered() { m_waiting = m_waiting > 0 ? m_waiting - 1 : 0; }

private:
	const SpellInfo *Spell(int id) const;
	void Say(uint32_t id, const std::string &fallback, int type);
	bool BardSong(const SpellInfo *spell) const; // EQ_Character_BardCastBard: ours, a bard's
	bool CastSpell(int gem, uint64_t now_ms);
	// What CastSpell does for a gem and an item alike, from the line of sight on; `item` null for a gem.
	bool Cast(int gem, int spell, const SpellInfo &info, const ZoneItem *item, int16_t slot, uint64_t now_ms);
	// What's in the way at `target` for target types that need one (LAB_004c50ba, LAB_004c5141):
	// 0 nothing, or the message.
	uint32_t CheckTarget(const SpellInfo &spell, uint16_t target, bool area) const;
	void StopSpellCast(int how, int spell);

	WindowManager *m_windows = nullptr;
	ZoneSession *m_zone = nullptr;
	const SpellFile *m_spells = nullptr;
	const EqStr *m_eqstr = nullptr;
	Host m_host;

	int m_spell = -1;       // CastingSpellId
	int m_gem = 0;          // CastingSpellGemNumber; 0xff once the zone says it went off
	uint64_t m_timeout = 0; // CastingTimeout: when the cast time runs out; 0 none
	int m_cast_ms = 0;      // CastingSpellCastTime: 0, the spell's own
	uint64_t m_recast_ms[8] = {}; // RecastTimeout
	uint64_t m_fizzle_ms = 0;     // FizzleTimeout: the recovery
	int m_waiting = 0;      // DAT_007d0254
	int m_casts = 0;        // DAT_007d0258
	uint64_t m_now_ms = 0;  // the last Frame's
};
