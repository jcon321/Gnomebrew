#pragma once

// Our character's movement through the zone, ported from eqgame.exe:
//   CEverQuest::ProcessControls (@0053f337)  keys -> MovementSpeed / MovementSpeedHeading
//     forward runs at MovementForwardSpeedMultiplier (the zone entry's runspeed, 0.7) or walks
//     at MovementBackwardSpeedMultiplier (walkspeed, 0.46); back is always the walk speed.
//     Turning speeds up by one heading unit per frame, to 12 per tick. In mouse look, left and
//     right strafe instead (Zeal_strafe_direction / Zeal_strafe_speed, 2 x encum_factor).
//   process_physics (@0054d964)  time in 50 ms ticks; the speed pushes along the heading,
//     and the velocity is damped: on the ground v = v * 0.625 + push * 0.8 (StartWorldDisplay's
//     MovementFriction1/2), in the air there's no push and v *= 0.99; gravity is the zone's.
//     A strafe pushes a quarter turn to the side at half the run (or walk) speed, on top of
//     the forward push, or at strafe speed x that standing still: running and strafing is
//     about 12% faster than running.
//   The jump: ProcessControls arms it on a press, on the ground, 1.5 s after the last jump.
//     For the next half second DoMainLoop jumps at once when we move forward (MovementSpeedZ
//     += 1.65 * JumpHeightModifier (1.35), pushed 1 along the heading); standing still,
//     ChangeStance crouches (stance 111, L08), and at the half second stands back up and
//     jumps straight up; backing up calls it off.
// In water (the body's region, ActorInfo::SwimmingWaterType 5 to 10) process_physics keeps
// gravity but damps far harder: across v = v * 0.95 + push * 0.025 (lava, slime and VW: 0.94,
// 0.02), up and down v = v * 0.2 + speed * sin(pitch) * 0.8 (0.15): we swim where we look,
// at about half the push. ProcessControls adds a hundredth of the swimming skill to the
// forward speed, gives no speed backwards, and swimming forward looking up does
// CDisplay::DoSwimJump: at most every 1.5 s, when what's ahead isn't water, MovementSpeedZ
// += 3 x 2.5 x JumpHeightModifier and a push of 0.5 along the heading, to climb out.
// CDisplay::MovePlayer samples the regions at the feet (+0.25: zone lines, slippery ground,
// where friction is 0.99 and control 0.02), the body (ModelHeightOffset, 5/8 of the height:
// the physics) and the eyes (underwater). See render/zone_regions.h.
// The real client applies the damping once per frame; here it's scaled to the frame's length,
// which gives the same speeds (about 30 units a second running) at any frame rate.
//
// CDisplay::MovePlayer's collision: a sphere (half the ModelHeightOffset, wider for the short
// races) lifted three quarters of the ModelHeightOffset over the body sweeps along the move and
// slides on what it meets, so we walk onto anything under its bottom (about 3/4 of the height);
// the feet then go onto the floor under it. Walls don't take the velocity away.
// World space is the WLD frame (x = the server's y, y = the server's x).

#include "math3d.h"
#include "vehicle.h"
#include "zone_regions.h"

#include <string>

namespace render
{
	class ZoneScene;
}

struct MoveControls
{
	bool forward = false, back = false, left = false, right = false;
	bool strafe = false; // mouse look: left and right strafe instead of turning
	bool run = true;
	bool jump = false; // the jump key went down
	float pitch = 0;   // where we look, 512ths of a turn, + up: the way we swim
};

class PlayerMovement
{
public:
	void Place(const render::Vec3 &feet, float heading, float height, float walk_speed, float run_speed, float gravity);
	void SetEyeHeight(float eye) { m_eye = eye; } // above the feet (CameraHeightOffset)
	void SetSwimSkill(int skill) { m_swim_skill = skill; }
	void SetRace(int race) { m_race = race; } // the collision sphere's size and lift
	// ActorInfo's MovementSpeedModifier (our buffs'): added to the speed forward, taken from it
	// back; below -1000 we're rooted.
	void SetSpeedModifier(float modifier) { m_speed_modifier = modifier; }
	// Sneaking (EQPlayer::InitSneakMod's: 0.5, a rogue's 0.1 more each level past 50, 1.5 at most);
	// 0 not.
	void SetSneakModifier(float modifier) { m_sneak_modifier = modifier; }
	// On a horse (pinstCharSpawn the horse, which has a Rider): the movement is the horse's, with its
	// walk and run speeds (Place's) and race (SetRace 216: twice the sphere). EQPlayer::SetAccel
	// (@00520074) gets to the speed the keys ask for 0.02 a tick faster or 0.03 slower instead of at
	// once; ProcessControls turns it 8 a tick at most (not 12), and a strafe standing still goes at a
	// quarter (the strafe speed's 0.25 with a Rider); DoMainLoop never jumps it; process_physics
	// gives it only our snares (MovementSpeedModifier under 0), no speed buff.
	void SetMounted(bool on) { m_mounted = on; }
	bool Mounted() const { return m_mounted; }
	// Advances `ms` milliseconds. Returns true when a jump started.
	bool Step(const MoveControls &controls, float ms, const render::ZoneScene &scene);
	// Turns by `delta` 512ths at once (mouse look).
	void Turn(float delta);

	const render::Vec3 &Feet() const { return m_feet; }
	float Heading() const { return m_heading; }
	float Speed() const { return m_speed; }               // MovementSpeed
	float HeadingSpeed() const { return m_heading_speed; } // MovementSpeedHeading
	float Strafe() const { return m_strafe; }              // sideways push: + left, - right
	const render::Vec3 &Velocity() const { return m_velocity; } // per tick, world space
	// EQPlayer::IWasHit's push: added to our motion (per tick, world space).
	void Push(const render::Vec3 &push) { m_velocity = m_velocity + push; }
	bool OnGround() const { return m_on_ground; }
	const render::Vec3 &FloorNormal() const { return m_floor_normal; } // where the last floor drop met it
	// Stance 111: a standing jump winding up, or ducking (ExecuteCmd 10, or a wind-up called off by
	// backing up, which DoMainLoop leaves crouched).
	bool Crouching() const { return m_crouching || m_ducking; }
	bool Ducking() const { return m_ducking; }
	bool JumpWindingUp() const { return m_jump_armed; } // DAT_007985f8
	void Duck() { m_ducking = true; }
	// EQPlayer::ChangeStance from 111 to 100: only where CanIFitHere standing, with the ceiling at
	// least the ModelHeightOffset over the body's center; false (still ducking) otherwise.
	bool StandUp(const render::ZoneScene &scene);
	void StopDucking() { m_ducking = false; } // any other stance
	float RunSpeed() const { return m_run_speed; }
	float Height() const { return m_height; }
	float Radius() const; // the actor's collision sphere, and where it is
	render::Vec3 SphereCenter() const { return m_feet + render::Vec3{0, 0, m_height * 0.625f + StepHeight()}; }
	render::Environment FeetEnvironment() const { return m_feet_environment; }
	render::Environment BodyEnvironment() const { return m_body_environment; } // SwimmingWaterType
	render::Environment HeadEnvironment() const { return m_head_environment; } // IsSwimmingUnderwater
	bool Swimming() const { return render::Swimming(m_body_environment); }
	// The zone line ("DRNTP..." region type) the feet last stepped into, once; empty for none.
	std::string TakeZoneLine();
	// The hardest the feet met the floor going down since the last call (per tick, negative; 0 for
	// none), and whether the body was in water (CheckForUnderFloor's landing sounds).
	float TakeLanding(bool &in_water);
	// ActorInfo's AttachedEntity: the boat (a solid spawn, ZoneScene::SetActor) our last floor check
	// came down on, 0 none (DoFloorCheck clears it and GetPlayerFloorHeight's drop sets it).
	uint16_t Vehicle() const { return m_vehicle; }
	void ClearVehicle() { m_vehicle = 0; }
	// vehicle_premove and vehicle_postmove: our boat moved, and we go along, turned with it (our
	// heading and motion too).
	void Carry(const VehicleMove &move);
	// Put somewhere at once with no motion (msg_vehicle_reset, IFoundMyVehicle's places), the rest of
	// what we have kept; true when there's a floor under it, which the feet are put on.
	bool MoveTo(const render::Vec3 &feet, float heading, const render::ZoneScene &scene);

private:
	void Tick(float ticks, const render::ZoneScene &scene);
	void UpdateEnvironment(const render::ZoneScene &scene);
	bool CanSwimJump(const render::ZoneScene &scene) const;
	// CDisplay::CanIFitHere (@0050453a) for the body over `feet`: the floor under it
	// (GetPlayerFloorHeight from half a unit under the body's center) and the ceiling over it
	// (GetCeilingHeight: a 0.5 sphere swept up from the center to 32767, ZCeiling) leave the body
	// room (twice the ModelHeightOffset; once ducking unless `standing`) and 0.5 more. No room
	// latches a stand-up (DAT_00798572) for 2 s on (DAT_00798574); room standing with it latched
	// and past then stands a duck up.
	bool CanIFitHere(const render::Vec3 &feet, const render::ZoneScene &scene, bool standing);
	float StepHeight() const; // how far over the body MovePlayer lifts the sphere
	render::Vec3 Sweep(const render::Vec3 &center, render::Vec3 move, const render::ZoneScene &scene) const;
	// `vehicle`: the solid spawn the floor is, 0 the zone's.
	bool FloorUnder(const render::Vec3 &center, const render::ZoneScene &scene, float &floor_z, uint16_t *vehicle = nullptr,
		render::Vec3 *normal = nullptr) const;

	render::Vec3 m_feet;
	float m_heading = 0;
	float m_height = 6;
	float m_walk_speed = 0.46f, m_run_speed = 0.7f;
	float m_gravity = 0.4f;

	float m_speed = 0;
	float m_heading_speed = 0;
	float m_strafe = 0;
	float m_turn_ramp = 0;
	render::Vec3 m_velocity;
	bool m_on_ground = true;

	float m_eye = 5.4f;
	int m_race = 1;
	int m_swim_skill = 0;
	float m_speed_modifier = 0;
	float m_sneak_modifier = 0;
	bool m_mounted = false;
	float m_pitch = 0;
	float m_clock_ms = 0, m_swim_jump_ms = 0; // DoSwimJump's 1.5 s
	float m_last_jump_ms = 0;  // when the last jump went up
	float m_jump_armed_ms = 0; // when the press armed one
	bool m_jump_armed = false;
	bool m_crouching = false;
	bool m_ducking = false;
	render::Vec3 m_floor_normal{0, 0, 1};
	float m_ceiling = 0;               // ActorInfo's ZCeiling, CanIFitHere's last
	bool m_stand_latched = false;      // DAT_00798572
	float m_stand_latch_ms = 0;        // DAT_00798574
	// The regions last seen at the feet, body and eyes (MovePlayer redoes them when they change).
	int m_feet_region = -2, m_body_region = -2, m_head_region = -2;
	render::Environment m_feet_environment = render::Environment::Dry;
	render::Environment m_body_environment = render::Environment::Dry;
	render::Environment m_head_environment = render::Environment::Dry;
	bool m_slippery = false;
	bool m_was_swimming = false;
	std::string m_zone_line;
	float m_landing = 0;
	bool m_landing_wet = false;
	uint16_t m_vehicle = 0;
};
