#pragma once

// Our character's movement through the zone, ported from eqgame.exe:
//   CEverQuest::ProcessControls (@0053f337)  keys -> MovementSpeed / MovementSpeedHeading
//     forward runs at MovementForwardSpeedMultiplier (the zone entry's runspeed, 0.7) or walks
//     at MovementBackwardSpeedMultiplier (walkspeed, 0.46); back is always the walk speed.
//     Turning speeds up by one heading unit per frame, to 12 per tick.
//   process_physics (@0054d964)  time in 50 ms ticks; the speed pushes along the heading,
//     and the velocity is damped: on the ground v = v * 0.625 + push * 0.8 (StartWorldDisplay's
//     MovementFriction1/2), in the air there's no push and v *= 0.99; gravity is the zone's.
//   DoMainLoop's jump: MovementSpeedZ += 1.65 * JumpHeightModifier (1.35), pushed 1 along the heading.
// The real client applies the damping once per frame; here it's scaled to the frame's length,
// which gives the same speeds (about 30 units a second running) at any frame rate.
//
// Collision is ours: the feet follow the floor below (stepping up to 40% of the character's
// height), walls are found with segments at knee and chest height, and the move slides along them.
// World space is the WLD frame (x = the server's y, y = the server's x).

#include "math3d.h"

namespace render
{
	class ZoneScene;
}

struct MoveControls
{
	bool forward = false, back = false, left = false, right = false;
	bool run = true;
	bool jump = false; // start a jump if we're on the ground
};

class PlayerMovement
{
public:
	void Place(const render::Vec3 &feet, float heading, float height, float walk_speed, float run_speed, float gravity);
	// Advances `ms` milliseconds. Returns true when a jump started.
	bool Step(const MoveControls &controls, float ms, const render::ZoneScene &scene);

	const render::Vec3 &Feet() const { return m_feet; }
	float Heading() const { return m_heading; }
	float Speed() const { return m_speed; }               // MovementSpeed
	float HeadingSpeed() const { return m_heading_speed; } // MovementSpeedHeading
	const render::Vec3 &Velocity() const { return m_velocity; } // per tick, world space
	bool OnGround() const { return m_on_ground; }
	float RunSpeed() const { return m_run_speed; }
	float Height() const { return m_height; }

private:
	void Tick(float ticks, const render::ZoneScene &scene);
	render::Vec3 MoveHorizontally(const render::Vec3 &feet, render::Vec3 move, const render::ZoneScene &scene) const;

	render::Vec3 m_feet;
	float m_heading = 0;
	float m_height = 6;
	float m_walk_speed = 0.46f, m_run_speed = 0.7f;
	float m_gravity = 0.4f;

	float m_speed = 0;
	float m_heading_speed = 0;
	float m_turn_ramp = 0;
	render::Vec3 m_velocity;
	bool m_on_ground = true;
};
