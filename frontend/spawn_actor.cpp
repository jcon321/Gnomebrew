#include "spawn_actor.h"

#include "animations.h"
#include "character_model.h"
#include "zone_scene.h"

#include <algorithm>
#include <cmath>

using namespace render;

namespace
{
	constexpr float GroundFriction = 0.625f; // MovementFriction1
	constexpr float GroundControl = 0.8f;    // MovementFriction2
	constexpr float AirFriction = 0.99f;
	constexpr float JumpHeight = 2.5f * 1.35f; // EQPlayer::SetHeights: ActorInfo+0xb4, 2.5 x JumpHeightModifier
	// As in PlayerMovement: process_physics damps once a frame, played here as at 60 frames a second.
	constexpr float FrameTicks = 1.0f / 3;
	constexpr float BodyHeight = 0.625f; // ModelHeightOffset over the feet

	float WrapHeading(float heading)
	{
		heading = std::fmod(heading, 512.0f);
		return heading < 0 ? heading + 512 : heading;
	}

	Vec3 Along(float heading, float amount)
	{
		float a = HeadingToRadians(heading);
		return {std::cos(a) * amount, std::sin(a) * amount, 0};
	}

	bool Thick(Environment environment)
	{
		return environment == Environment::Slime || environment == Environment::Lava || environment == Environment::VWater;
	}
}

void SpawnActor::Update(const ZoneSpawn &spawn, float height, float gravity, uint64_t now_ms)
{
	m_height = height > 0 ? height : 6;
	m_gravity = gravity;
	m_npc = spawn.npc == 1;
	// EQPlayer: gravity unless LevitationState says otherwise (3 turns it off in water, in Step);
	// a few races never fall.
	m_fly_mode = spawn.fly_mode;
	m_boat = IsBoatRace(spawn.race);
	m_gravity_on = (spawn.fly_mode == 0 || spawn.fly_mode == 3) && !m_boat;
	if (m_placed && spawn.updated_ms == m_updated_ms) {
		if (spawn.npc >= 2) // a corpse stops where it fell
			m_speed = m_heading_speed = m_velocity.x = m_velocity.y = 0;
		return;
	}
	// unpack_physics: the update's position, heading, speed, turn and velocity, as they come.
	m_updated_ms = spawn.updated_ms;
	m_taken_ms = now_ms;
	Vec3 feet{spawn.y, spawn.x, spawn.z - m_height / 5 * 3.125f}; // the server's z is ModelHeightOffset over the feet
	if (m_placed && spawn.fly_mode == 2 && std::fabs(feet.z - m_feet.z) <= 5)
		feet.z = m_feet.z; // levitating: small corrections in z are ignored
	if (!m_placed)
		m_random = 0x9e3779b9u ^ (spawn.id * 2654435761u);
	m_feet = feet;
	m_heading = WrapHeading(spawn.heading);
	m_speed = spawn.npc >= 2 ? 0 : spawn.speed;
	m_heading_speed = spawn.npc >= 2 ? 0 : spawn.heading_speed;
	m_velocity = {spawn.dy, spawn.dx, spawn.dz};
	m_placed = true;
	m_settled = false;
}

void SpawnActor::Step(const ZoneScene &scene, float ms, bool physics, uint64_t now_ms)
{
	if (now_ms > m_taken_ms + 5000)
		m_speed = m_heading_speed = 0;
	Vec3 before = m_feet;
	float heading = m_heading;
	if (!m_settled) {
		// process_physics takes at most 3 ticks a frame.
		float ticks = std::min(ms * 0.02f, 3.0f);
		while (ticks > 0) {
			float step = std::min(ticks, FrameTicks);
			Physics(step, scene, physics);
			ticks -= step;
		}
		if (physics && m_on_ground && m_speed == 0 && m_heading_speed == 0 && std::fabs(m_velocity.x) < 0.01f &&
			std::fabs(m_velocity.y) < 0.01f && std::fabs(m_velocity.z) < 0.01f) {
			m_velocity = {};
			m_settled = true;
		}
	}
	// CDisplay::MovePlayer: standing still is moving under 0.2 on every axis without a speed;
	// turning is any change of heading.
	Vec3 moved = m_feet - before;
	m_stationary = m_speed == 0 && std::fabs(moved.x) < 0.2f && std::fabs(moved.y) < 0.2f && std::fabs(moved.z) < 0.2f;
	m_turning = m_heading_speed != 0 || m_heading != heading;
}

void SpawnActor::Carry(const VehicleMove &move)
{
	m_feet = move.Carry(m_feet);
	m_heading = move.CarryHeading(m_heading);
	m_velocity = move.Rotate(m_velocity);
}

void SpawnActor::Ride(const Vec3 &feet, float heading, int animation)
{
	m_feet = feet;
	m_heading = WrapHeading(heading);
	m_speed = m_heading_speed = 0;
	m_velocity = {};
	m_stationary = true;
	m_turning = false;
	m_on_ground = true;
	m_settled = false; // off the horse, it drops to the floor
	m_placed = true;
	m_riding = animation;
}

void SpawnActor::Drive(const Vec3 &feet, float heading, float speed, float heading_speed, const Vec3 &velocity,
	bool on_ground, Environment body, Environment feet_environment)
{
	Vec3 moved = feet - m_feet;
	float was = m_heading;
	m_feet = feet;
	m_heading = WrapHeading(heading);
	m_speed = speed;
	m_heading_speed = heading_speed;
	m_velocity = velocity;
	m_on_ground = on_ground;
	m_body_environment = body;
	m_feet_environment = feet_environment;
	m_stationary = !m_placed || (m_speed == 0 && std::fabs(moved.x) < 0.2f && std::fabs(moved.y) < 0.2f &&
	                                std::fabs(moved.z) < 0.2f);
	m_turning = m_heading_speed != 0 || m_heading != was;
	m_placed = true;
	m_settled = false;
}

void SpawnActor::Physics(float t, const ZoneScene &scene, bool floor)
{
	m_heading = WrapHeading(m_heading + t * m_heading_speed);

	// EQPlayer::bodyEnvironmentChange: LevitationState 3 turns gravity off in water.
	bool water = Swimming(m_body_environment);
	bool gravity = floor && m_has_floor && m_gravity_on && !(m_fly_mode == 3 && water);
	float friction = GroundFriction, control = GroundControl, rise_friction = GroundFriction;
	if (gravity) {
		m_velocity.z -= m_gravity * t;
		rise_friction = AirFriction;
		if (water) {
			bool thick = Thick(m_body_environment);
			friction = thick ? 0.94f : 0.95f;
			control = thick ? 0.02f : 0.025f;
			rise_friction = thick ? 0.15f : 0.2f;
		}
		else if (!m_on_ground) {
			friction = AirFriction;
			control = 0;
		}
	}
	// A moving NPC is damped twice as hard.
	if (m_npc && (m_velocity.x != 0 || m_velocity.y != 0))
		friction *= 0.5f;
	float frames = t / FrameTicks;
	float keep = std::pow(friction, frames);
	Vec3 push = Along(m_heading, m_speed * control * (1 - keep) / (1 - friction));
	m_velocity.x = m_velocity.x * keep + push.x;
	m_velocity.y = m_velocity.y * keep + push.y;
	m_velocity.z *= std::pow(rise_friction, frames);

	Vec3 start = m_feet;
	m_feet += m_velocity * t;
	if (!floor)
		return;
	// MovePlayer keeps the feet on the floor; CheckForUnderFloor brings them back up onto it.
	float step_up = std::max(1.0f, m_height * 0.4f);
	float floor_z;
	bool found = scene.FloorBelow({m_feet.x, m_feet.y, m_feet.z + step_up}, 1000, floor_z, nullptr, &m_vehicle, !m_boat);
	if (!found) {
		// Nothing below at all (off the zone's mesh): the server's height stands.
		if (gravity)
			m_feet.z = start.z;
		m_velocity.z = std::max(m_velocity.z, 0.0f);
		m_has_floor = m_on_ground = false;
	}
	else if (!gravity) {
		m_has_floor = true;
		if (m_feet.z < floor_z)
			m_feet.z = floor_z;
		m_on_ground = m_feet.z <= floor_z + 0.001f;
	}
	else {
		// Down a slope or a step stays on the ground instead of falling off each edge.
		if (m_on_ground && !water && m_velocity.z <= 0 && floor_z >= start.z - step_up)
			m_feet.z = floor_z;
		m_on_ground = m_feet.z <= floor_z + 0.001f;
		if (m_on_ground) {
			if (m_velocity.z < m_landing) { // CheckForUnderFloor: the feet under the floor, going down
				m_landing = m_velocity.z;
				m_landing_wet = water;
			}
			m_feet.z = floor_z;
			m_velocity.z = std::max(m_velocity.z, 0.0f);
		}
	}
	UpdateEnvironment(scene);
}

void SpawnActor::UpdateEnvironment(const ZoneScene &scene)
{
	// MovePlayer looks again when the body's (or feet's) region changes.
	const ZoneRegions &regions = scene.Regions();
	if (regions.Empty())
		return;
	Vec3 body = m_feet + Vec3{0, 0, m_height * BodyHeight};
	int region = regions.RegionAt(body);
	if (region >= 0 && region != m_body_region) {
		m_body_region = region;
		m_body_environment = regions.EnvironmentAt(body);
	}
	Vec3 feet = m_feet + Vec3{0, 0, 0.25f};
	region = regions.RegionAt(feet);
	if (region >= 0 && region != m_feet_region) {
		m_feet_region = region;
		m_feet_environment = regions.EnvironmentAt(feet);
	}
}

float SpawnActor::TakeLanding(bool &in_water)
{
	float landing = m_landing;
	in_water = m_landing_wet;
	m_landing = 0;
	return landing;
}

void SpawnActor::Animate(const ZoneSpawn &spawn, const CharacterModel &model, float ms, uint64_t now_ms)
{
	m_played.clear();
	bool dead = spawn.npc == 2 || spawn.npc == 3;
	if (!m_animated) {
		// Seen for the first time: already lying, sitting or ducking, at the end of getting there.
		m_animated = true;
		m_stance = spawn.stance;
		m_dead = dead;
		m_action_serial = spawn.action_serial;
		int id = dead || spawn.stance == 115 ? Anim::Death
		       : spawn.stance == 110        ? Anim::SitDown
		       : spawn.stance == 111        ? Anim::Crouch
		       : spawn.stance == 105        ? Anim::Kneel
		                                    : 0;
		if (id && Play(spawn, model, id, dead ? Kind::Dead : Kind::Transition, End::Hold, false, 1, true))
			m_ms = model.AnimationMs(AnimationCode(m_animation));
	}
	if (dead != m_dead) {
		// OP_Death: they fall over (EQPlayer::IDied); a corpse that isn't one any more gets up.
		m_dead = dead;
		if (dead)
			Play(spawn, model, Anim::Death, Kind::Dead, End::Hold, false, 1, true);
		else
			m_kind = Kind::Base;
	}
	if (spawn.stance != m_stance) {
		// EQPlayer::ChangeStance: standing up plays getting down backwards.
		uint8_t from = m_stance;
		m_stance = spawn.stance;
		switch (spawn.stance) {
		case 100:
			if (from == 111)
				Play(spawn, model, Anim::Crouch, Kind::Transition, End::Once, true, 1, true);
			else if (from == 105 || m_animation == Anim::Kneel)
				Play(spawn, model, Anim::Kneel, Kind::Transition, End::Once, true, 1, true);
			else if (from != 102)
				Play(spawn, model, Anim::SitDown, Kind::Transition, End::Once, true, 1, true);
			break;
		case 105: Play(spawn, model, Anim::Kneel, Kind::Transition, End::Hold, false, 1, true); break;
		case 110: Play(spawn, model, Anim::SitDown, Kind::Transition, End::Hold, false, 1, true); break;
		case 111:
			if (m_animation != Anim::Crouch && m_animation != Anim::CrouchWalk)
				Play(spawn, model, Anim::Crouch, Kind::Transition, End::Hold, false, 1, true);
			break;
		case 115:
			if (m_animation != Anim::Death)
				Play(spawn, model, Anim::Death, Kind::Transition, End::Hold, false, 1, true);
			break;
		}
	}
	if (spawn.action_serial != m_action_serial) {
		// OP_Animation: PlayAnimationTrack(spawn, id, ..., speed).
		m_action_serial = spawn.action_serial;
		if (!m_dead)
			Play(spawn, model, spawn.action, Kind::Action, End::Once, false, spawn.action_speed > 0 ? spawn.action_speed : 1, true);
	}

	ProcessAnimation(spawn, model, now_ms);

	m_ms += ms * m_rate;
	if (m_end == End::Once && m_kind != Kind::Base && Finished(model))
		m_kind = Kind::Base; // done: the next pick replaces it
}

void SpawnActor::ProcessAnimation(const ZoneSpawn &spawn, const CharacterModel &model, uint64_t now_ms)
{
	if (m_kind == Kind::Dead || (m_kind == Kind::Action && !Finished(model)))
		return;
	if (m_riding) {
		Play(spawn, model, m_riding, Kind::Base, End::Loop, false, 1, false);
		return;
	}
	bool body_wet = Swimming(m_body_environment);
	bool feet_wet = Swimming(m_feet_environment);
	bool crouching = spawn.stance == 111;
	float across = std::sqrt(m_velocity.x * m_velocity.x + m_velocity.y * m_velocity.y);
	int id = -1;
	Kind kind = Kind::Base;
	End end = End::Loop;
	bool reversed = false;
	float rate = 1;
	if (m_stationary) {
		if (body_wet && spawn.race != 24 && spawn.race != 61 && spawn.race != 74)
			id = Anim::TreadWater;
		else if (m_turning) {
			// Turning on the spot shuffles, one way or the other.
			if (spawn.race == 49) {
				id = Anim::RunJump;
				rate = 1.5f;
			}
			else {
				if (m_heading_speed == 0)
					return;
				reversed = m_heading_speed > 0;
				id = crouching ? Anim::CrouchWalk : Anim::Shuffle;
			}
		}
		else if (now_ms - m_fidget_ms > 1000) {
			// Once a second, standing about: a fidget one time in four (pets one in a hundred).
			if (crouching)
				return;
			m_random = m_random * 1664525u + 1013904223u;
			int roll = static_cast<int>((m_random >> 8) % 100) + 1;
			m_fidget_ms = now_ms;
			if (!IsStandingAnimation(m_animation))
				return;
			if (roll <= (spawn.pet_owner != 0 ? 99 : 75))
				id = Anim::Idle;
			else {
				id = Anim::Fidget;
				kind = Kind::Fidget;
				end = End::Once;
				if (m_npc && (spawn.race == 49 || spawn.race == 158 || spawn.race == 196))
					rate = 1.5f;
			}
		}
		else {
			if (crouching || m_animation == Anim::Swim || m_animation == Anim::TreadWater)
				return;
			id = Anim::Idle;
		}
	}
	else {
		m_fidget_ms = 0;
		if (across >= 0.001f) {
			rate = across * 0.5f + 1;
			if (!body_wet) {
				// Walking, or running when faster than the walk; jumping and falling in the air.
				float walk = spawn.race == 216 ? (spawn.walk_speed + spawn.run_speed) * 0.5f : spawn.walk_speed;
				if (!feet_wet) {
					id = crouching ? Anim::CrouchWalk : Anim::Walk;
					if (m_velocity.z >= JumpHeight) {
						id = Anim::RunJump;
						end = End::Hold;
						rate = 1;
					}
					else if (m_velocity.z < -3.2f) {
						id = Anim::Fall;
						end = End::Hold;
					}
					else if (walk < m_speed)
						id = Anim::Run;
				}
				else if (m_animation != Anim::TreadWater && m_animation != Anim::Swim)
					id = walk < m_speed ? Anim::Run : Anim::Walk;
				else
					return;
				if (m_speed < 0)
					reversed = true; // backing up
			}
			else
				id = spawn.race == 49 && m_turning ? Anim::RunJump : Anim::Swim;
		}
		else if (!body_wet) {
			if (feet_wet)
				return;
			if (m_velocity.z >= JumpHeight * 0.5f) {
				id = Anim::StandJump;
				end = End::Hold;
			}
			else if (m_velocity.z < -2.25f) {
				id = Anim::Fall;
				end = End::Hold;
			}
			else
				id = spawn.race == 49 && m_turning ? Anim::RunJump : Anim::Idle;
		}
		else
			id = Anim::TreadWater;
	}
	// A fidget or a stance's transition plays out while standing about.
	if ((m_kind == Kind::Fidget || m_kind == Kind::Transition) && !Finished(model) && IsStandingAnimation(id))
		return;
	Play(spawn, model, id, kind, end, reversed, rate, false);
}

bool SpawnActor::Play(const ZoneSpawn &spawn, const CharacterModel &model, int id, Kind kind, End end, bool reversed,
	float rate, bool restart)
{
	// The dead, the feigning and the unconscious only fall over.
	if ((spawn.hp < 1 || spawn.stance == 115 || spawn.stance == 120) && id != Anim::Death && spawn.hp < 10)
		return false;
	// Sitting: the sitting idle and fidget; no casting gestures.
	if (spawn.stance == 110) {
		if ((id == Anim::Fidget || id == Anim::Fidget2) && spawn.race != 60)
			id = Anim::SitFidget;
		else if ((id == Anim::Idle || id == Anim::Stand2) && spawn.race != 60)
			id = Anim::Sitting;
		else if (id == Anim::Fidget || id == Anim::Idle)
			return false;
		else if (id >= 39 && id <= 41)
			return false;
	}
	auto has = [&](int a) { return model.HasAnimation(AnimationCode(a)); };
	if (!has(id)) {
		if (id == Anim::Sitting || id == Anim::SitFidget)
			return false; // sitting down stays where it ended
		id = FallbackAnimation(id, spawn.race);
		if (!has(id))
			id = Anim::Idle;
	}
	if (spawn.stance != 100 && (id == Anim::Idle || id == Anim::Shuffle || id == Anim::Stand2 || id == Anim::Fidget ||
								   id == Anim::Fidget2 || (id > 11 && id < 16)))
		return false;
	// A few models play slower.
	if (spawn.race == 216)
		rate = id == Anim::Run ? rate * 0.4f : 0.5f;
	else if ((spawn.race == 42 || spawn.race == 120) && spawn.gender == 2 && id != Anim::Walk)
		rate = 0.5f;
	m_played.push_back(id); // CDisplay_PlayAnimationSound, for every track played
	if (!restart && id == m_animation && reversed == m_reversed && !(m_end == End::Once && Finished(model))) {
		m_rate = rate;
		m_kind = kind;
		return true;
	}
	m_animation = id;
	m_kind = kind;
	m_end = end;
	m_reversed = reversed;
	m_rate = rate;
	m_ms = 0;
	return true;
}

bool SpawnActor::Finished(const CharacterModel &model) const
{
	return m_end != End::Loop && m_ms >= model.AnimationMs(AnimationCode(m_animation));
}

uint64_t SpawnActor::AnimationTime(const CharacterModel &model) const
{
	double length = model.AnimationMs(AnimationCode(m_animation));
	if (length <= 0)
		return 0;
	// One that doesn't loop stops on its last frame (and a reversed one on its first).
	double t = m_end == End::Loop ? std::fmod(m_ms, length) : std::min(m_ms, length);
	if (m_reversed)
		t = std::max(length - (m_end == End::Loop ? 1 : 0) - t, 0.0);
	return static_cast<uint64_t>(t);
}
