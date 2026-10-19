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
// Collision is ours: the feet follow the floor below (stepping up to 40% of the character's
// height), walls are found with segments at knee and chest height, and the move slides along them.
// World space is the WLD frame (x = the server's y, y = the server's x).

#include "math3d.h"
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
	bool OnGround() const { return m_on_ground; }
	bool Crouching() const { return m_crouching; } // a standing jump winding up (stance 111)
	float RunSpeed() const { return m_run_speed; }
	float Height() const { return m_height; }
	render::Environment FeetEnvironment() const { return m_feet_environment; }
	render::Environment BodyEnvironment() const { return m_body_environment; } // SwimmingWaterType
	render::Environment HeadEnvironment() const { return m_head_environment; } // IsSwimmingUnderwater
	bool Swimming() const { return render::Swimming(m_body_environment); }
	// The zone line ("DRNTP..." region type) the feet last stepped into, once; empty for none.
	std::string TakeZoneLine();

private:
	void Tick(float ticks, const render::ZoneScene &scene);
	void UpdateEnvironment(const render::ZoneScene &scene);
	bool CanSwimJump(const render::ZoneScene &scene) const;
	render::Vec3 MoveHorizontally(const render::Vec3 &feet, render::Vec3 move, const render::ZoneScene &scene) const;

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
	int m_swim_skill = 0;
	float m_pitch = 0;
	float m_clock_ms = 0, m_swim_jump_ms = 0; // DoSwimJump's 1.5 s
	float m_last_jump_ms = 0;  // when the last jump went up
	float m_jump_armed_ms = 0; // when the press armed one
	bool m_jump_armed = false;
	bool m_crouching = false;
	// The regions last seen at the feet, body and eyes (MovePlayer redoes them when they change).
	int m_feet_region = -2, m_body_region = -2, m_head_region = -2;
	render::Environment m_feet_environment = render::Environment::Dry;
	render::Environment m_body_environment = render::Environment::Dry;
	render::Environment m_head_environment = render::Environment::Dry;
	bool m_slippery = false;
	bool m_was_swimming = false;
	std::string m_zone_line;
};
