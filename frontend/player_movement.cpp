#include "player_movement.h"

#include "zone_scene.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float GroundFriction = 0.625f; // MovementFriction1
	constexpr float GroundControl = 0.8f;    // MovementFriction2
	constexpr float AirFriction = 0.99f;
	constexpr float JumpHeightModifier = 1.35f; // EQPlayer::SetHeights
	constexpr float MaxTurnRate = 12;           // heading units per tick
	// process_physics applies the friction once a frame, not once a tick, so the real client
	// stops (and gets up to speed) quicker the faster it draws, and jumps a little lower. We
	// play like it does at 60 frames a second, a third of a tick each; the top speed doesn't
	// change.
	constexpr float FrameTicks = 1.0f / 3;

	float WrapHeading(float heading)
	{
		heading = std::fmod(heading, 512.0f);
		return heading < 0 ? heading + 512 : heading;
	}

	render::Vec3 Along(float heading, float amount)
	{
		float a = render::HeadingToRadians(heading);
		return {std::cos(a) * amount, std::sin(a) * amount, 0};
	}
}

void PlayerMovement::Place(const render::Vec3 &feet, float heading, float height, float walk_speed, float run_speed, float gravity)
{
	m_feet = feet;
	m_heading = WrapHeading(heading);
	m_height = height > 0 ? height : 6;
	if (walk_speed > 0)
		m_walk_speed = walk_speed;
	if (run_speed > 0)
		m_run_speed = run_speed;
	m_gravity = gravity;
	m_speed = m_heading_speed = m_turn_ramp = 0;
	m_velocity = {};
	m_on_ground = true;
}

void PlayerMovement::Turn(float delta)
{
	m_heading = WrapHeading(m_heading + delta);
}

bool PlayerMovement::Step(const MoveControls &controls, float ms, const render::ZoneScene &scene)
{
	// ProcessControls: forward runs or walks, back walks; SetAccel takes effect at once.
	if (controls.forward && !controls.back)
		m_speed = controls.run ? m_run_speed : m_walk_speed;
	else if (controls.back && !controls.forward)
		m_speed = -m_walk_speed;
	else
		m_speed = 0;

	float ticks = std::min(ms * 0.02f, 3.0f);
	int direction = (controls.left ? 1 : 0) - (controls.right ? 1 : 0);
	if (direction != 0) {
		// The real client adds one per frame; at its usual 30 frames a second that's 1.5 a tick.
		m_turn_ramp = std::min(m_turn_ramp + ticks * 1.5f, MaxTurnRate);
		m_heading_speed = direction * std::max(1.0f, std::floor(m_turn_ramp));
	}
	else
		m_turn_ramp = m_heading_speed = 0;

	bool jumped = false;
	if (controls.jump && m_on_ground) {
		// DoMainLoop: the jump, and push_along_heading(1).
		m_velocity.z += 1.65f * JumpHeightModifier;
		m_velocity += Along(m_heading, 1.0f);
		m_on_ground = false;
		jumped = true;
	}

	// One tick at most per step, so collision sees every stretch of the move.
	while (ticks > 0) {
		float step = std::min(ticks, 1.0f);
		Tick(step, scene);
		ticks -= step;
	}
	return jumped;
}

void PlayerMovement::Tick(float t, const render::ZoneScene &scene)
{
	m_heading = WrapHeading(m_heading + t * m_heading_speed);

	// process_physics: gravity, then damping and the push along the heading.
	m_velocity.z -= m_gravity * t;
	float friction = m_on_ground ? GroundFriction : AirFriction;
	float control = m_on_ground ? GroundControl : 0;
	float frames = t / FrameTicks;
	float keep = std::pow(friction, frames);
	render::Vec3 push = Along(m_heading, m_speed * control * (1 - keep) / (1 - friction));
	m_velocity.x = m_velocity.x * keep + push.x;
	m_velocity.y = m_velocity.y * keep + push.y;
	m_velocity.z *= std::pow(AirFriction, frames);
	if (m_on_ground && m_speed == 0) {
		if (std::fabs(m_velocity.x) < 0.01f)
			m_velocity.x = 0;
		if (std::fabs(m_velocity.y) < 0.01f)
			m_velocity.y = 0;
	}

	// Across, stopping at walls, then onto the floor (CheckForUnderFloor keeps the feet on it).
	render::Vec3 before = m_feet;
	render::Vec3 feet = MoveHorizontally(m_feet, {m_velocity.x * t, m_velocity.y * t, 0}, scene);
	// A wall that stopped or turned the move takes that much of the velocity with it.
	m_velocity.x = (feet.x - before.x) / t;
	m_velocity.y = (feet.y - before.y) / t;
	float step_up = std::max(1.0f, m_height * 0.4f);
	float floor_z;
	if (!scene.FloorBelow({feet.x, feet.y, feet.z + step_up}, 1000, floor_z)) {
		// Nothing below at all: off the edge of the world. Stay put.
		feet = before;
		m_velocity.x = m_velocity.y = 0;
		if (!scene.FloorBelow({feet.x, feet.y, feet.z + step_up}, 1000, floor_z))
			floor_z = -1e30f;
	}
	float z = feet.z + m_velocity.z * t;
	// Walking down a slope or a step keeps us on the ground instead of falling off each edge.
	if (m_on_ground && m_velocity.z <= 0 && floor_z >= feet.z - step_up)
		z = floor_z;
	if (z <= floor_z + 0.001f) {
		z = floor_z;
		if (m_velocity.z < 0)
			m_velocity.z = 0;
		m_on_ground = true;
	}
	else
		m_on_ground = false;
	feet.z = z;
	m_feet = feet;
}

render::Vec3 PlayerMovement::MoveHorizontally(const render::Vec3 &start, render::Vec3 move, const render::ZoneScene &scene) const
{
	const float radius = std::clamp(m_height * 0.15f, 0.5f, 2.0f);
	const float heights[2] = {std::max(1.0f, m_height * 0.4f) + 0.2f, m_height * 0.8f};
	render::Vec3 feet = start;
	for (int attempt = 0; attempt < 3; ++attempt) {
		float length = render::Length(move);
		if (length < 1e-4f)
			break;
		render::Vec3 direction = move * (1 / length);
		float nearest = 1;
		render::Vec3 wall;
		for (float height : heights) {
			render::Vec3 from = feet + render::Vec3{0, 0, height};
			float fraction;
			render::Vec3 normal;
			// Walkable slopes (normals pointing mostly up) aren't walls; the floor check climbs them.
			if (scene.SegmentHit(from, from + direction * (length + radius), fraction, &normal) && normal.z < 0.7f &&
				fraction < nearest) {
				nearest = fraction;
				wall = normal;
			}
		}
		float allowed = nearest < 1 ? std::clamp(nearest * (length + radius) - radius, 0.0f, length) : length;
		feet += direction * allowed;
		if (nearest >= 1)
			break;
		// Slide along the wall with what's left.
		render::Vec3 rest = move * (1 - allowed / length);
		wall.z = 0;
		wall = render::Normalize(wall);
		if (render::Length(wall) == 0)
			break;
		move = rest - wall * render::Dot(rest, wall);
	}
	return feet;
}
