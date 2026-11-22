#pragma once

// Doors and the other switches (levers, lifts, traps...), EQSwitch in the client:
//   EQSwitch_DoorLoadingCode (@00520f91): one for each of OP_SpawnDoor's, with the actor
//     "<name>_ACTORDEF" scaled to its size / 100. Types 50 (pressure plates), 54, and 57 with an
//     incline of 1 (teleporters) are made invisible; 53 (a particle effect) has no actor.
//     ResetSwitchPosition: one that comes open starts opening.
//   EQSwitch::ChangeState (@00521815): the state, 0 closed, 1 open, 2 opening, 3 closing, 4 a trap
//     that went off. OP_MoveDoor sets it, for everyone's clicks.
//   EQSwitch_UpdateDoorStates (@004a3237): every frame, in 50 ms ticks, each moves by its type:
//       0-8      swing a quarter turn (128) about their hinge, 8 a tick; 5-7 the other way
//       10-27    slide sideways (EQSwitch_UpdateDoorPosition)
//       30, 35, 40, 130   tip over (the incline): trapdoors, drawbridges
//       59-81, 120, 140-146   rise or sink (lifts, portcullises, traps); 78-81 slide as well
//       100-107, 115, 116     spin up while open and wind down after
//       45, 109, 115, 116, 125, 150-152   move along their heading
//     Left open, one closes by itself after GetSwitchStateChangeTime: 10 s for types 0 and 5, 20 s
//     for most. Traps (115 and up) spring back as soon as they're out.
//   EQSwitch::UseSwitch (@00521d91): a click asks the zone (OP_ClickDoor), unless the door is
//     moving or out of reach; the zone answers everyone with OP_MoveDoor.
//   Sounds: each door type loads its waves as it spawns (EQSwitch::LoadDoorSoundEffects), and
//     ChangeState plays its type's for opening, closing or (some) shutting, where the door is.
//     Closing by itself (ToggleDoorState) goes round ChangeState, so it's quiet until it shuts.
// Keys, lockpicks, the combination switches and the particle effects aren't done.
//
// Positions are WLD space (x = the server's y, y = the server's x), headings 0..512.

#include "math3d.h"
#include "zone_session.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace render
{
	class ZoneScene;
}

class ZoneDoors
{
public:
	void Spawn(const std::vector<ZoneDoor> &doors, uint64_t now_ms);
	// OP_MoveDoor.
	void ChangeState(uint8_t id, uint8_t state, uint64_t now_ms);
	// Moves them on to now_ms and puts their models there in the scene (adding them the first time).
	// `hour`: the world's (pinstWorldData->Hour), for the particle switches (53) that go by it.
	// `emit`: DoParticleEffect would work now (pinstLocalPlayer's actor is in); else the particle
	// switches wait, as eqgame's EQSwitch+0x80 stays 0 and tries again.
	void Update(render::ZoneScene &scene, uint64_t now_ms, int hour = 12, bool emit = true);
	// EQSwitch_UpdateDoorStates for a particle switch (53, no actor): open with a parameter of 1..255
	// and none made yet, DoParticleEffect(2, spells.eff record `param`, pinstLocalPlayer, at the
	// switch, 43200000 ms); shut with one, its emitters destroyed (1000 ms). Its state goes by the
	// hour first (EQSwitch::UpdateDoorStateBasedOnWorldTime @004a3023): an incline of 1 open from 5
	// to 18, of 2 shut then and open the rest of the day.
	struct Particles
	{
		bool start = true; // else stop
		int tag = 0;       // which switch's
		int effect = 0;
		render::Vec3 at;
	};
	std::vector<Particles> TakeParticles() { return std::exchange(m_particles, {}); }

	enum class Use
	{
		Nothing, // not a switch we can use (or it's moving)
		TooFar,  // "You can't reach that, get closer."
		Ask,     // send OP_ClickDoor for `id`, with `skill`
	};
	// What CEverQuest_ClickedSwitch sends with a click: our Disarm Traps skill (HasSkill) for a trap,
	// else the item on the cursor (0xffff none) and the skill that goes with it.
	struct Held
	{
		int16_t disarm = 0;
		uint16_t item = 0xffff;
		int16_t skill = 0;
	};
	// UseSwitch for our click on the scene's door `scene_door` (ZoneScene::DoorHit). `center` is our
	// position (the model's center), `height` our size. A trap gets `held.disarm` and no item.
	Use Click(int scene_door, const render::Vec3 &center, float height, const Held &held, uint8_t &id, uint16_t &item,
		int16_t &skill) const;

	// EQPlayer::TouchingSwitch (@0050b7ac), each frame for us (CDisplay::MovePlayer), with the switch
	// our floor check met (ActorInfo+0x1c4, CDisplay_IsCollisionValid): its scene door, or -1.
	//   - type 35: 1 s after the last touch, 49 times in 100 UseSwitch(0, 0, 0) (no one, nothing held)
	//   - type 57 (a teleporter): 2 s after, as a player, UseSwitch(us, the cursor's item) when its
	//     parameter's table entry (`<zone>_tp.txt`, msg_teleport_index) names a zone we have
	//   - type 50 (a pressure plate), as a player: shut, UseSwitch(us, none); open longer than its
	//     GetSwitchStateChangeTime since the last touch, shut here (nothing sent)
	//   - 115 and up (a trap): 1 s (0.4 s for one that closes in under 1 s) after the last touch of
	//     the same trap, IWasHit (`Trap`)
	//   - a trap that's gone off (state 4), anything else: nothing.
	struct Touch
	{
		enum class Kind
		{
			Nothing,
			Ask,    // OP_ClickDoor for `id` with `item`, `skill` and `spawn` (UseSwitch's)
			TooFar, // "You can't reach that, get closer." (1462)
			Trap,   // EQPlayer::IWasHit on us
		};
		Kind kind = Kind::Nothing;
		uint8_t id = 0;
		uint16_t item = 0xffff;
		int16_t skill = 0;
		uint16_t spawn = 0;
	};
	Touch Touching(int scene_door, uint16_t me, uint16_t cursor_item, const render::Vec3 &center, float height,
		uint64_t now_ms, int percent);
	// Which switch CDisplay_IsCollisionValid lets our floor check touch, of the doors under us
	// (ZoneScene::DoorsUnder from `from`, the floor at `floor_z`): one of type 8..59 or 78 and up (70
	// only in zone 59) whose surface is under Height + 2 below `from`, at or over the floor; the last
	// the sweep meets, the lowest. -1 none.
	int TouchedSwitch(const std::vector<std::pair<int, float>> &under, const render::Vec3 &from, float floor_z, float height,
		uint32_t zone_id) const;
	// msg_teleport_index (@004f1e5b), as the zone starts: `<eq_dir>/<zone>_tp.txt`, a header line,
	// then id^y^x^z^heading^zone lines (GetTeleportTableEntryByID's). TAKP's folder has none.
	void LoadTeleportTable(const std::string &eq_dir, const std::string &zone);

	size_t Count() const { return m_doors.size(); }
	// The sounds the doors made since the last call (EqSoundManager slots, where they are), and the
	// types spawned since, for their waves.
	struct Sound
	{
		int slot;
		render::Vec3 position;
	};
	std::vector<Sound> TakeSounds();
	std::vector<uint8_t> TakeSpawnedTypes();
	size_t Shown() const; // with a model in the scene

private:
	struct Door
	{
		ZoneDoor spawn;
		render::Vec3 rest;                    // closed: where it was put
		float rest_heading = 0, rest_incline = 0;
		render::Vec3 at;                      // now
		float heading = 0, incline = 0;
		float scale = 1;
		uint8_t state = 0;
		float speed = 0;                      // a spinner's, per tick
		uint64_t changed_ms = 0;              // the state's last change
		bool visible = true;
		bool tried = false;                   // looked for its model
		int scene = -1;                       // ZoneScene's door
		float radius = 0;                     // the model's bounding radius, scaled
		bool emitting = false;                // a particle switch's effect is going (EQSwitch+0x80)
	};
	std::vector<Particles> m_particles;

	// EQSwitch::UseSwitch's checks: what a click or touch of `door` by `spawn` (0 none: no reach
	// check) holding `item` with `skill` does.
	Use UseSwitch(const Door &door, bool spawn, const render::Vec3 &center, float height, uint16_t item, int16_t skill,
		uint8_t &id) const;
	void SetState(Door &door, uint8_t state, uint64_t now_ms) const;
	void Toggle(Door &door) const; // ToggleDoorState
	void Step(Door &door, float ticks, uint64_t now_ms) const;
	bool Slide(Door &door, bool finish, float ticks, uint64_t now_ms) const; // EQSwitch_UpdateDoorPosition

	std::vector<Door> m_doors;
	struct TeleportEntry
	{
		int16_t id = 0;
		float y = 0, x = 0, z = 0, heading = 0;
		int32_t zone = 0;
	};
	std::vector<TeleportEntry> m_teleports;
	std::string m_eq_dir;
	uint64_t m_touch_ms = 0;   // ActorInfo+0x164+8: the last switch touched
	int m_last_trap = -1;      // DAT_007912a0
	mutable std::vector<Sound> m_sounds; // SetState runs in const steps
	std::vector<uint8_t> m_spawned_types;
	uint64_t m_last_ms = 0;
};
