#pragma once

// Everyone else in the zone, moved and animated the way the client does between the server's updates:
//   unpack_physics (@004f2405): an update puts the spawn where the server says, with its speed
//     (MovementSpeed, the update's animation byte / 10), its turn per tick and its velocity.
//     NPCs walk with a velocity of 0: the speed alone moves them, along the heading.
//   process_physics (@0054d964): every frame (in 50 ms ticks) the turn, then the velocity damped
//     (0.625 a frame on the ground, half that while an NPC moves) plus 0.8 x the speed along the
//     heading, the water's damping in water, and gravity unless the spawn flies or levitates
//     (LevitationState 1 and 2; 3 floats only in water). CDisplay::MovePlayer and CheckForUnderFloor
//     keep the feet on the floor. A moving NPC settles at 0.8 / (1 - 0.3125) = 1.16 x its speed a
//     tick: 23.3 x speed units a second, which is how far the server moves it (speed / 0.025 x 0.4 x 1.45).
//   CDisplay::ProcessAnimation (@004dad79) picks the animation from that each frame: idle (with
//     a fidget 1 time in 4, each second), shuffling on the spot while turning, walking or running
//     (faster with the velocity), swimming or treading water, jumping and falling. The stance
//     plays its transition (EQPlayer::ChangeStance: sitting down, standing back up, ducking,
//     lying down), corpses lie dead, and the server's OP_Animation plays attacks, casting and
//     emotes once. PlayAnimationTrack2 (@004d938f) makes do with what the model has.
//
// World space is the WLD frame (x = the server's y, y = the server's x).

#include "math3d.h"
#include "vehicle.h"
#include "zone_regions.h"
#include "zone_session.h"

#include <cstdint>
#include <vector>

namespace render
{
	class CharacterModel;
	class ZoneScene;
}

class SpawnActor
{
public:
	// Every frame, with the spawn as the server last sent it, before Step. `height` is its size,
	// `gravity` the zone's (per tick, squared).
	void Update(const ZoneSpawn &spawn, float height, float gravity, uint64_t now_ms);
	// Advances to `now_ms`, `ms` since the last step. With `physics`, the floor and the water as
	// process_physics has them; without (far away), the spawn just carries on along its heading.
	// Ours: a spawn stops walking 5 s after its last update. The server updates a moving NPC at
	// least every 2 s, but only for clients in its update range, so one that walked out of range
	// would otherwise walk on for ever.
	void Step(const render::ZoneScene &scene, float ms, bool physics, uint64_t now_ms);
	// Picks and advances the animation for the model (after Step). The model may change.
	void Animate(const ZoneSpawn &spawn, const render::CharacterModel &model, float ms, uint64_t now_ms);
	// A rider on its horse, every frame in place of Step (process_physics): its feet where the
	// horse's RIDER_DAG puts it, the horse's heading, no motion of its own, and the riding animation
	// in place of standing about (PlayAnimationTrack2: P09, or L10 while the horse goes as fast as it
	// runs). Getting off, Step takes over from there (it falls to the floor).
	void Ride(const render::Vec3 &feet, float heading, int animation);
	void StopRiding() { m_riding = 0; }
	// The horse we ride (pinstCharSpawn), every frame in place of Update and Step: where our movement
	// has it.
	void Drive(const render::Vec3 &feet, float heading, float speed, float heading_speed, const render::Vec3 &velocity,
		bool on_ground, render::Environment body, render::Environment feet_environment);

	// Its AttachedEntity: the boat its last floor check came down on (0 none); a boat never has one.
	uint16_t Vehicle() const { return m_vehicle; }
	// vehicle_premove and vehicle_postmove: its boat moved, and it goes along (before its own Step).
	void Carry(const VehicleMove &move);
	bool Placed() const { return m_placed; }

	const render::Vec3 &Feet() const { return m_feet; }
	float Heading() const { return m_heading; }
	int Animation() const { return m_animation; } // the animation number (render::Anim)
	float AnimationRate() const { return m_rate; }
	// Where the track is, for CharacterModel::Draw: backwards for a reversed one, held at the end.
	uint64_t AnimationTime(const render::CharacterModel &model) const;
	bool AnimationLoops() const { return m_end == End::Loop; }
	render::Environment BodyEnvironment() const { return m_body_environment; }
	render::Environment FeetEnvironment() const { return m_feet_environment; }
	const render::Vec3 &Velocity() const { return m_velocity; } // per tick
	// For the sounds: the animations PlayAnimationTrack2 played in the last Animate, in order (it
	// plays the moving and idling ones every frame), and the hardest the feet met the floor going
	// down since the last call (per tick, negative; 0 for none), with whether the body was in water.
	const std::vector<int> &Played() const { return m_played; }
	float TakeLanding(bool &in_water);

private:
	enum class Kind
	{
		Base,       // ProcessAnimation's: idle, moving, swimming... replaced by its next pick
		Fidget,     // once, until standing still picks again; moving cuts it short
		Transition, // the stance's: plays out before an idle replaces it
		Action,     // the server's OP_Animation: plays out
		Dead,       // lying there
	};
	enum class End
	{
		Loop,
		Once, // then done: the next pick replaces it
		Hold, // stays on its last frame
	};

	void Physics(float ticks, const render::ZoneScene &scene, bool floor);
	void UpdateEnvironment(const render::ZoneScene &scene);
	void ProcessAnimation(const ZoneSpawn &spawn, const render::CharacterModel &model, uint64_t now_ms);
	// PlayAnimationTrack2: false when the spawn's state or the model doesn't allow it.
	bool Play(const ZoneSpawn &spawn, const render::CharacterModel &model, int id, Kind kind, End end, bool reversed,
		float rate, bool restart);
	bool Finished(const render::CharacterModel &model) const;

	// Motion.
	uint64_t m_updated_ms = 0; // the update we last took (ZoneSpawn::updated_ms)
	uint64_t m_taken_ms = 0;   // when we took it, on our clock
	bool m_placed = false;
	bool m_npc = false;
	render::Vec3 m_feet;
	float m_heading = 0;
	float m_height = 6;
	float m_speed = 0, m_heading_speed = 0;
	render::Vec3 m_velocity; // per tick
	float m_gravity = 0.4f;
	bool m_gravity_on = true;
	uint8_t m_fly_mode = 0;
	bool m_has_floor = true; // anything below at all: without, the server's height stands
	bool m_on_ground = false;
	bool m_settled = false; // on the floor and not moving: nothing to do until the next update
	int m_body_region = -2, m_feet_region = -2;
	render::Environment m_body_environment = render::Environment::Dry;
	render::Environment m_feet_environment = render::Environment::Dry;
	// MovePlayer's flags for ProcessAnimation, from the last Step.
	bool m_stationary = true, m_turning = false;
	int m_riding = 0; // Ride's animation; 0 not riding
	bool m_boat = false;     // a vehicle itself (IsBoatRace): no boat floors under it
	uint16_t m_vehicle = 0;

	// Animation.
	bool m_animated = false;
	int m_animation = 32;
	Kind m_kind = Kind::Base;
	End m_end = End::Loop;
	bool m_reversed = false;
	float m_rate = 1;
	double m_ms = 0; // into the track, before reversing
	uint64_t m_fidget_ms = 0; // the last fidget roll (0: moving)
	uint32_t m_random = 0;
	uint8_t m_stance = 100;
	bool m_dead = false;
	uint32_t m_action_serial = 0;
	std::vector<int> m_played;
	float m_landing = 0;
	bool m_landing_wet = false;
};
