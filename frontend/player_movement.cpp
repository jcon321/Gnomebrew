#include "player_movement.h"

#include "zone_scene.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
	constexpr float GroundFriction = 0.625f; // MovementFriction1
	constexpr float GroundControl = 0.8f;    // MovementFriction2
	constexpr float AirFriction = 0.99f;
	constexpr float JumpHeightModifier = 1.35f; // EQPlayer::SetHeights
	constexpr float MaxTurnRate = 12;           // heading units per tick
	constexpr float JumpCooldownMs = 1500;      // ProcessControls: since the last jump
	constexpr float JumpWindUpMs = 500;         // DoMainLoop: how long a press stays armed
	// process_physics applies the friction once a frame, not once a tick, so the real client
	// stops (and gets up to speed) quicker the faster it draws, and jumps a little lower. We
	// play like it does at 60 frames a second, a third of a tick each; the top speed doesn't
	// change.
	constexpr float FrameTicks = 1.0f / 3;
	constexpr float BodyHeight = 0.625f; // ModelHeightOffset: 5/8 of the height above the feet, where the server's z is

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
	m_speed = m_heading_speed = m_turn_ramp = m_strafe = 0;
	m_velocity = {};
	m_on_ground = true;
	m_eye = m_height * 0.9f;
	m_clock_ms = m_swim_jump_ms = 0;
	m_last_jump_ms = -JumpCooldownMs;
	m_jump_armed = m_crouching = false;
	m_feet_region = m_body_region = m_head_region = -2;
	m_feet_environment = m_body_environment = m_head_environment = render::Environment::Dry;
	m_slippery = m_was_swimming = false;
	m_zone_line.clear();
}

std::string PlayerMovement::TakeZoneLine()
{
	return std::exchange(m_zone_line, {});
}

void PlayerMovement::Turn(float delta)
{
	m_heading = WrapHeading(m_heading + delta);
}

bool PlayerMovement::Step(const MoveControls &controls, float ms, const render::ZoneScene &scene)
{
	// ProcessControls: forward runs or walks (swimming adds a hundredth of the skill), back walks
	// (or stays put in water); SetAccel takes effect at once.
	m_clock_ms += ms;
	m_pitch = controls.pitch;
	bool swimming = Swimming();
	// What our buffs add (MovementSpeedModifier): a snare slows us to a crawl at most; rooted, nothing.
	bool rooted = m_speed_modifier < -1000;
	// Sneaking (IsSneaking) forward goes at the sneak modifier of the walk speed, back and beside at
	// half of it, water or not.
	bool sneaking = m_sneak_modifier > 0;
	if (controls.forward && !controls.back) {
		if (sneaking)
			m_speed = m_sneak_modifier * m_walk_speed + m_speed_modifier;
		else
			m_speed = (controls.run ? m_run_speed : m_walk_speed) + (swimming ? m_swim_skill * 0.01f : 0) + m_speed_modifier;
		m_speed = rooted ? 0 : m_speed < 0 ? 0.01f : m_speed;
	}
	else if (controls.back && !controls.forward) {
		m_speed = (sneaking ? -m_walk_speed * 0.5f : swimming ? 0 : -m_walk_speed) - m_speed_modifier;
		m_speed = rooted ? 0 : m_speed > 0 ? -0.01f : m_speed;
	}
	else
		m_speed = 0;

	float ticks = std::min(ms * 0.02f, 3.0f);
	int direction = (controls.left ? 1 : 0) - (controls.right ? 1 : 0);
	m_strafe = 0;
	if (controls.strafe && direction != 0) {
		// process_physics: half the run (or walk) speed beside the forward push, or the strafe
		// speed (2, unencumbered) times that when standing still.
		m_strafe = direction * (controls.run && !sneaking ? m_run_speed : m_walk_speed) * 0.5f;
		if (m_speed == 0)
			m_strafe *= 2;
		if (sneaking)
			m_strafe *= 0.5f;
		if (rooted)
			m_strafe = 0;
		direction = 0;
	}
	if (direction != 0) {
		// The real client adds one per frame: at 60 frames a second, three a tick.
		m_turn_ramp = std::min(m_turn_ramp + ticks / FrameTicks, MaxTurnRate);
		m_heading_speed = direction * std::max(1.0f, std::floor(m_turn_ramp));
	}
	else
		m_turn_ramp = m_heading_speed = 0;

	// DoSwimJump: swimming forward and looking up, every 1.5 s, climbs out where it isn't water ahead.
	if (swimming && m_speed > 0 && m_pitch > 0 && m_clock_ms > m_swim_jump_ms) {
		m_swim_jump_ms = m_clock_ms + 1500;
		if (CanSwimJump(scene)) {
			m_velocity.z += 3 * 2.5f * JumpHeightModifier;
			m_velocity += Along(m_heading, 0.5f);
		}
	}

	// ProcessControls: a press on the ground, out of the water, 1.5 s after the last jump arms one.
	if (controls.jump && !swimming && !m_jump_armed && m_on_ground && m_clock_ms - m_last_jump_ms > JumpCooldownMs) {
		m_jump_armed = true;
		m_jump_armed_ms = m_clock_ms;
	}
	// DoMainLoop: for half a second, moving forward jumps at once, pushed along the heading, and
	// standing still crouches; then the crouch stands back up and jumps straight up. Backing up
	// calls it off (the real client leaves us crouched; we stand, having no duck key yet).
	bool jumped = false;
	if (m_jump_armed) {
		bool push = false;
		if (m_clock_ms - m_jump_armed_ms < JumpWindUpMs) {
			if (m_speed > 0) {
				m_jump_armed = m_crouching = false;
				jumped = m_on_ground;
				push = true;
			}
			else if (m_speed < 0)
				m_jump_armed = m_crouching = false;
			else
				m_crouching = true;
		}
		else {
			m_jump_armed = false;
			jumped = m_crouching && m_on_ground;
			m_crouching = false;
		}
		if (jumped) {
			m_velocity.z += 1.65f * JumpHeightModifier;
			if (push)
				m_velocity += Along(m_heading, 1.0f); // push_along_heading(1)
			m_on_ground = false;
			m_last_jump_ms = m_clock_ms;
		}
	}

	// A frame (at 60 a second) at most per step: water damps a lot each frame, and collision
	// sees every stretch of the move.
	while (ticks > 0) {
		float step = std::min(ticks, FrameTicks);
		Tick(step, scene);
		ticks -= step;
	}
	return jumped;
}

void PlayerMovement::Tick(float t, const render::ZoneScene &scene)
{
	m_heading = WrapHeading(m_heading + t * m_heading_speed);

	// process_physics: gravity, then damping and the push along the heading (and in water, up
	// or down the way we look).
	bool swimming = Swimming();
	m_velocity.z -= m_gravity * t;
	float friction = AirFriction, control = 0, rise_friction = AirFriction, rise_control = 0;
	if (swimming) {
		bool thick = m_body_environment == render::Environment::Slime || m_body_environment == render::Environment::Lava ||
		             m_body_environment == render::Environment::VWater;
		friction = thick ? 0.94f : 0.95f;
		control = thick ? 0.02f : 0.025f;
		rise_friction = thick ? 0.15f : 0.2f;
		rise_control = 0.8f;
	}
	else if (m_on_ground) {
		friction = m_slippery ? 0.99f : GroundFriction;
		control = m_slippery ? 0.02f : GroundControl;
	}
	float frames = t / FrameTicks;
	float keep = std::pow(friction, frames);
	float scale = control * (1 - keep) / (1 - friction);
	render::Vec3 push = Along(m_heading, m_speed * scale) + Along(m_heading + 128, m_strafe * scale);
	m_velocity.x = m_velocity.x * keep + push.x;
	m_velocity.y = m_velocity.y * keep + push.y;
	float rise_keep = std::pow(rise_friction, frames);
	float rise = m_speed != 0 && m_pitch != 0 ? m_speed * std::sin(render::HeadingToRadians(m_pitch)) : 0;
	m_velocity.z = m_velocity.z * rise_keep + rise * rise_control * (1 - rise_keep) / (1 - rise_friction);
	// Out of the water but still swimming's animation: a slow rise stops (no popping up at the surface).
	if (!swimming && m_was_swimming && m_velocity.z > 0 && m_velocity.z < 1)
		m_velocity.z = 0;
	if (m_on_ground && m_speed == 0 && m_strafe == 0 && !swimming) {
		if (std::fabs(m_velocity.x) < 0.01f)
			m_velocity.x = 0;
		if (std::fabs(m_velocity.y) < 0.01f)
			m_velocity.y = 0;
	}

	// CDisplay::MovePlayer: the actor's sphere, lifted by the step height, sweeps from where we
	// were to where the velocity takes us, sliding along what it meets; then the feet go onto
	// the floor below the lifted center (DoFloorCheck, PutPlayerOnFloor). So anything lower than
	// the sphere's bottom is walked onto, and a wall doesn't take the velocity: pressed against a
	// ledge we keep pushing at it, and a jump or a swim jump carries us over once we clear it.
	render::Vec3 before = m_feet;
	render::Vec3 lift{0, 0, m_height * BodyHeight + StepHeight()};
	render::Vec3 center = Sweep(m_feet + lift, m_velocity * t, scene);
	render::Vec3 feet = center - lift;
	float floor_z;
	if (!FloorUnder(center, scene, floor_z)) {
		// Nothing below at all: off the edge of the world. Stay put.
		feet = before;
		m_velocity.x = m_velocity.y = 0;
		if (!FloorUnder(before + lift, scene, floor_z))
			floor_z = -1e30f;
	}
	float z = feet.z;
	// Walking down a slope or a step keeps us on the ground instead of falling off each edge.
	float step_down = std::max(1.0f, m_height * 0.4f);
	if (m_on_ground && !swimming && m_velocity.z <= 0 && floor_z >= z - step_down)
		z = floor_z;
	if (z <= floor_z + 0.001f) {
		z = floor_z;
		if (m_velocity.z < m_landing) {
			m_landing = m_velocity.z;
			m_landing_wet = swimming;
		}
		if (m_velocity.z < 0)
			m_velocity.z = 0;
		m_on_ground = true;
	}
	else
		m_on_ground = false;
	feet.z = z;
	m_feet = feet;
	m_was_swimming = swimming;
	UpdateEnvironment(scene);
}

void PlayerMovement::UpdateEnvironment(const render::ZoneScene &scene)
{
	// MovePlayer looks again when a sample point's region changes, and keeps what it had
	// outside every region.
	const render::ZoneRegions &regions = scene.Regions();
	if (regions.Empty())
		return;
	render::Vec3 feet = m_feet + render::Vec3{0, 0, 0.25f};
	int region = regions.RegionAt(feet);
	if (region >= 0 && region != m_feet_region) {
		// Not where we were put: a zone-in point inside a zone line mustn't send us straight back.
		bool placed_here = m_feet_region == -2;
		m_feet_region = region;
		render::ZoneRegions::Feet info = regions.FeetAt(feet);
		m_feet_environment = info.environment;
		m_slippery = info.slippery;
		if (!info.zone_line.empty() && !placed_here)
			m_zone_line = info.zone_line;
	}
	render::Vec3 body = m_feet + render::Vec3{0, 0, m_height * BodyHeight};
	region = regions.RegionAt(body);
	if (region >= 0 && region != m_body_region) {
		m_body_region = region;
		m_body_environment = regions.EnvironmentAt(body);
	}
	render::Vec3 eyes = m_feet + render::Vec3{0, 0, m_eye};
	region = regions.RegionAt(eyes);
	if (region >= 0 && region != m_head_region) {
		m_head_region = region;
		m_head_environment = regions.EnvironmentAt(eyes);
	}
}

bool PlayerMovement::CanSwimJump(const render::ZoneScene &scene) const
{
	// CDisplay::GetEnvironment(player): ModelHeightOffset ahead, a unit under the body; no
	// region or a dry one there means the edge.
	float body = m_height * BodyHeight;
	render::Vec3 ahead = m_feet + Along(m_heading, body) + render::Vec3{0, 0, body - 1};
	int region = scene.Regions().RegionAt(ahead);
	return region < 0 || scene.Regions().EnvironmentAt(ahead) == render::Environment::Dry;
}

float PlayerMovement::StepHeight() const
{
	// MovePlayer's lift: none ducking (a standing jump's wind-up too); barbarians, trolls and
	// ogres half their ModelHeightOffset, the rest three quarters, and 5.4375 at most.
	if (m_crouching)
		return 0;
	float offset = m_height * BodyHeight;
	if (m_race == 2 || m_race == 9 || m_race == 10)
		return offset * 0.5f;
	return offset < 7.25f ? offset * 0.75f : 5.4375f;
}

float PlayerMovement::Radius() const
{
	// CreatePlayerActor: a player's sphere is half its ModelHeightOffset, wider for the short races.
	float radius = m_height * BodyHeight * 0.5f;
	switch (m_race) {
	case 8:  // dwarf
	case 11: // halfling
		return radius * 1.5f;
	case 9: // troll
		return radius * 0.9f;
	case 10: // ogre
		return radius * 0.8f;
	case 12:  // gnome
	case 216: // horse
		return radius * 2;
	default:
		return radius;
	}
}

render::Vec3 PlayerMovement::Sweep(const render::Vec3 &start, render::Vec3 move, const render::ZoneScene &scene) const
{
	// s3dCollideActorWithWorld, then ...AlongUnitDirection with what's left along the surface
	// (once in the client; a corner can take a second).
	const float radius = Radius();
	render::Vec3 center = start;
	for (int attempt = 0; attempt < 3; ++attempt) {
		if (render::Length(move) < 1e-5f)
			break;
		float fraction;
		render::Vec3 normal;
		if (!scene.SphereHit(center, center + move, radius, fraction, &normal)) {
			center += move;
			break;
		}
		// Up to the touch, just off it (the client nudges by 0.001), and slide with the rest.
		center += move * fraction + normal * 0.01f;
		render::Vec3 rest = move * (1 - fraction);
		move = rest - normal * render::Dot(rest, normal);
	}
	return center;
}

bool PlayerMovement::FloorUnder(const render::Vec3 &center, const render::ZoneScene &scene, float &floor_z) const
{
	// GetPlayerFloorHeight: a sphere a quarter of the ModelHeightOffset across, dropped from half a
	// unit over the lifted center; the floor is where its bottom comes to rest.
	float radius = std::max(m_height * BodyHeight * 0.25f, 0.1f);
	render::Vec3 from = center + render::Vec3{0, 0, 0.5f};
	render::Vec3 to = from - render::Vec3{0, 0, 1000};
	float fraction;
	if (!scene.SphereHit(from, to, radius, fraction))
		return false;
	floor_z = from.z - 1000 * fraction - radius + 0.001f;
	return true;
}

float PlayerMovement::TakeLanding(bool &in_water)
{
	float landing = m_landing;
	in_water = m_landing_wet;
	m_landing = 0;
	return landing;
}
