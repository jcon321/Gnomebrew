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
	void Update(render::ZoneScene &scene, uint64_t now_ms);

	enum class Use
	{
		Nothing, // not a switch we can use (or it's moving)
		TooFar,  // "You can't reach that, get closer."
		Ask,     // send OP_ClickDoor for `id`, with `skill`
	};
	// UseSwitch for our click on the scene's door `scene_door` (ZoneScene::DoorHit). `center` is our
	// position (the model's center), `height` our size, `disarm` our disarm traps skill.
	Use Click(int scene_door, const render::Vec3 &center, float height, int16_t disarm, uint8_t &id, int16_t &skill) const;

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
	};

	void SetState(Door &door, uint8_t state, uint64_t now_ms) const;
	void Toggle(Door &door) const; // ToggleDoorState
	void Step(Door &door, float ticks, uint64_t now_ms) const;
	bool Slide(Door &door, bool finish, float ticks, uint64_t now_ms) const; // EQSwitch_UpdateDoorPosition

	std::vector<Door> m_doors;
	mutable std::vector<Sound> m_sounds; // SetState runs in const steps
	std::vector<uint8_t> m_spawned_types;
	uint64_t m_last_ms = 0;
};
