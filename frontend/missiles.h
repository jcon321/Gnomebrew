#pragma once

// eqgame's missiles (EQMissile, the list at DAT_007f94a4): arrows, thrown weapons and bolt spells in
// flight, each an actor that steers itself at its target. They're only something to see: the zone
// says what they hit and for how much.
//
//   EQMissile::EQMissile (@00502540)  from OP_Projectile (HandleWorldMessage's 0x4045): the packet is
//                 the missile's own first 0x70 bytes. The caster plays GetAnimationIdForAttack's
//                 animation (@004f29e9; a spell's 44, the server's bolts, kind 28 skill 0, a 5).
//   ProcessMissile (@0050315e)  the first frame (RealRender_World): made where it starts (a spell's,
//                 kind 9: on its target for the target types 8, 21..23 and 28; a bolt, travel type
//                 3, level), out 2 minutes (a spell its AEDuration, else 30 s for a bolt, else 50 ms);
//                 CreateActor of its model (else IT63, else RED_BLOB; none, it's gone) with a
//                 collision radius of 1.5 (1 aimed), its light (CreateLight), and for a spell the
//                 effect's missile stage (DoSpellEffect 1; 2 for those on the target).
//   Then each frame, once 32 ms have gone by since the last step:
//   MissileAI (@00503582)  turned toward its target (none, or dead, or the caster: straight on).
//                 Where the target is, is asked again at most every 125 ms at 60..150 units (flat
//                 distance), 250 out to 250, 500 out to 350, then a second; under 60 every step,
//                 and under 2 it's hit. Further than 32 it turns 16 a step at most, its climb too
//                 (eqgame leaves a climb under 16 off as it is; we step that too, so it glides onto
//                 its target rather than jumping to it at the end); within 32 it points right at it.
//                 It aims at the target's center + half its ModelHeightOffset. The 500 ms (2 s for
//                 a spell) ProcessMissile puts on the first ask wraps (it's unsigned): asked at once.
//   process_physics (@0054d964, a missile's)  in 50 ms steps (at most 3): turned and tilted by its
//                 rates, pushed along its heading at its speed (up at its climb's sine) times 0.8,
//                 onto 0.625 (0.85 not aimed: a bolt) of its last push, and moved that far. No
//                 gravity. Not aimed, under 0.05 across it stops.
//   CDisplay::MoveMissile (@00503b12)  gone when its time's up; a spawn in its way (not the caster,
//                 CollsnCallbackForMissile) and it's gone; the zone in its way and it stays put.
//   ~EQMissile (@00502dfa)  Frost Bolt (54) lands its effect where it went.
//
// Between the 32 ms steps it's drawn on along its push to where the next would put it, so it moves
// each frame rather than 30 times a second (eqgame's sits still between them).

#include "zone_session.h"

#include "math3d.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class SpellFile;

class Missiles
{
public:
	// Positions are world space (WLD: x the server's y, y its x), z the server's.
	struct Spawn
	{
		render::Vec3 at;          // its center (the server's z)
		float height_offset = 0;  // ModelHeightOffset
		bool dead = false;        // StandingState 'x'
	};
	struct World
	{
		std::function<bool(uint16_t id, Spawn &spawn)> find;
		// The first spawn but `ignore` within `radius` of the way from `from` to `to`; 0 none.
		std::function<uint16_t(const render::Vec3 &from, const render::Vec3 &to, float radius, uint16_t ignore)> spawn_hit;
		std::function<bool(const render::Vec3 &from, const render::Vec3 &to)> blocked; // by the zone
		std::function<bool(const std::string &model)> has_model; // CreateActor finds it
	};

	struct Missile
	{
		uint32_t id = 0;
		ZoneMissile from;          // the packet
		render::Vec3 at, velocity; // world space
		float heading = 0, pitch = 0, climb = 0;
		std::string model;         // what CreateActor found
		float radius = 1;          // its collision radius
		int spell = 0;             // kind 9: its spell, 0 none
		uint16_t target = 0;
		bool made = false, stuck = false, done = false;
		bool bolt_gun = false, hit = false; // the debug menu's bolt gun: one of ours, and it hit something
		uint64_t physics_ms = 0, aim_ms = 0, expires_ms = 0;
		float aim_heading = 0, aim_climb = 0, aim_distance = 2000;
	};

	// A missile's spell effect (DoSpellEffect): a stage on the missile (its emitters on whom 2), or
	// with `point`, on that spot (on whom 0).
	using StartEffect = std::function<void(const Missile &missile, int stage, const render::Vec3 *point)>;

	void SetSpells(const SpellFile *spells) { m_spells = spells; }
	const SpellFile *Spells() const { return m_spells; }
	void SetEffects(StartEffect start) { m_start_effect = std::move(start); }

	// OP_Projectile: on its way the next Update. `bolt_gun`: the debug menu's, which no packet
	// sends; it bursts on a wall as well as a spawn (its spell's landing at where it hit).
	void Launch(const ZoneMissile &packet, bool bolt_gun = false);
	void Update(uint64_t now_ms, const World &world);
	void Clear() { m_missiles.clear(); }

	const std::vector<Missile> &All() const { return m_missiles; }
	const Missile *Find(uint32_t id) const;
	// Where it's drawn this frame: on along its push since the last step.
	render::Vec3 DrawnAt(const Missile &missile, uint64_t now_ms) const;

	// GetAnimationIdForAttack: what the caster plays as it lets one go (0xfe none).
	static int CasterAnimation(const ZoneMissile &packet);

private:
	void Make(Missile &m, uint64_t now_ms, const World &world);
	void Aim(Missile &m, uint64_t now_ms, const World &world);
	void Physics(Missile &m, uint64_t now_ms);
	void Move(Missile &m, const render::Vec3 &from, uint64_t now_ms, const World &world);
	void Destroy(Missile &m);

	std::vector<Missile> m_missiles;
	const SpellFile *m_spells = nullptr;
	StartEffect m_start_effect;
	uint32_t m_next_id = 1;
};
