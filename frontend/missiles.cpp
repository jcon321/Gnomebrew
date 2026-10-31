#include "missiles.h"

#include "spell_file.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float TurnStep = 6.2831855f / 512; // a 512th of a turn
	constexpr int FrostBolt = 54;

	// CDisplay_FixHeading: into 0..512.
	float FixHeading(float heading)
	{
		heading = std::fmod(heading, 512.0f);
		return heading < 0 ? heading + 512 : heading;
	}

	// CDisplay_HeadingDiff: how far apart, the short way round; `sign` is b - a that way (taken with
	// the larger first, as eqgame does).
	float HeadingDiff(float a, float b, float &sign)
	{
		float low = std::min(a, b), high = std::max(a, b);
		float diff = high - low;
		if (diff <= 256) {
			sign = diff;
			return diff;
		}
		sign = diff - 512;
		return -(diff - 512);
	}

	// get_bearing: the heading from one spot to another.
	float Bearing(const render::Vec3 &from, const render::Vec3 &to)
	{
		return FixHeading(std::atan2(to.y - from.y, to.x - from.x) / TurnStep);
	}

	float FlatDistance(const render::Vec3 &a, const render::Vec3 &b)
	{
		return std::hypot(a.x - b.x, a.y - b.y);
	}

	bool Still(const render::Vec3 &v)
	{
		return std::fabs(v.x) < 0.001f && std::fabs(v.y) < 0.001f && std::fabs(v.z) < 0.001f;
	}
}

int Missiles::CasterAnimation(const ZoneMissile &packet)
{
	// GetAnimationIdForAttack(skill, kind, aimed).
	if (packet.kind == 9)
		return 44;
	if (packet.aimed)
		return packet.kind == 17 || packet.kind == 27 ? 9 : packet.kind == 28 ? 10 : 5;
	switch (packet.skill) {
	case 0: case 1: return 5;
	case 2: return 4;
	case 3: return 3;
	case 10: return 7;
	case 21: return 46;
	case 23: return 47;
	case 26: return 45;
	case 28: return 8;
	case 30: case 71: return 1;
	case 38: return 11;
	case 52: return 46;
	default: return packet.kind == 35 ? 4 : 2;
	}
}

void Missiles::Launch(const ZoneMissile &packet)
{
	// EQMissile::EQMissile from the packet: its fields as they come, the speeds under 0.001 none.
	Missile m;
	m.id = m_next_id++;
	if (m_next_id > 0xffffff)
		m_next_id = 1;
	m.from = packet;
	m.at = {packet.y, packet.x, packet.z};
	m.velocity = {packet.velocity_y, packet.velocity_x, packet.velocity_z};
	auto tiny = [](float &v) {
		if (std::fabs(v) < 0.001f)
			v = 0;
	};
	tiny(m.velocity.x);
	tiny(m.velocity.y);
	tiny(m.velocity.z);
	m.heading = packet.heading;
	m.pitch = packet.pitch;
	m.climb = packet.climb;
	m.target = packet.target;
	m_missiles.push_back(m);
}

const Missiles::Missile *Missiles::Find(uint32_t id) const
{
	for (const Missile &m : m_missiles)
		if (m.id == id)
			return &m;
	return nullptr;
}

void Missiles::Update(uint64_t now_ms, const World &world)
{
	// RealRender_World: a new one made, the rest stepped once 32 ms have gone by.
	for (Missile &m : m_missiles) {
		if (!m.made)
			Make(m, now_ms, world);
		else if (now_ms - std::min(now_ms, m.physics_ms) > 32) {
			Aim(m, now_ms, world);
			render::Vec3 from = m.at;
			Physics(m, now_ms);
			Move(m, from, now_ms, world);
		}
		if (m.done)
			Destroy(m);
	}
	m_missiles.erase(std::remove_if(m_missiles.begin(), m_missiles.end(), [](const Missile &m) { return m.done; }),
		m_missiles.end());
}

void Missiles::Make(Missile &m, uint64_t now_ms, const World &world)
{
	// ProcessMissile: nothing while the caster isn't there.
	Spawn caster;
	if (!world.find || !world.find(m.from.source, caster)) {
		m.done = true;
		return;
	}
	m.expires_ms = now_ms + 120000;
	int stage = 0;
	const SpellInfo *spell = m.from.kind == 9 && m_spells ? m_spells->Find(static_cast<int>(m.from.object)) : nullptr;
	if (m.from.kind == 9 && spell) {
		m.spell = static_cast<int>(m.from.object);
		m.expires_ms = now_ms + (spell->ae_duration ? static_cast<uint64_t>(spell->ae_duration) : spell->travel_type == 3 ? 30000 : 50);
		uint8_t type = spell->target_type;
		if (type == 8 || (type > 20 && (type < 24 || type == 28))) {
			// Set down on its target, still.
			Spawn target;
			if (world.find(m.target, target))
				m.at = target.at;
			stage = 2;
			m.velocity = {};
			m.from.speed = 0;
		}
		else {
			stage = 1;
			if (spell->travel_type == 3)
				m.velocity.z = 0;
			else
				m.at = m.at + m.velocity * 5.5f;
		}
	}
	else if (m.from.kind == 9) {
		m.done = true; // a spell that isn't one: no actor
		return;
	}
	else
		m.at = m.at + m.velocity;
	// CreateActor: its model, else IT63's, else RED_BLOB's; neither, it's gone.
	for (const char *name : {m.from.model.c_str(), "IT63", "RED_BLOB"})
		if (world.has_model && world.has_model(name)) {
			m.model = name;
			break;
		}
	if (m.model.empty()) {
		m.done = true;
		return;
	}
	m.radius = m.from.aimed ? 1.0f : 1.5f;
	m.aim_heading = m.heading;
	m.aim_climb = m.climb;
	m.aim_distance = 2000;
	m.physics_ms = now_ms;
	m.aim_ms = now_ms + (m.from.kind == 9 ? 2000 : 500);
	m.made = true;
	if (spell && m_start_effect)
		m_start_effect(m, stage, nullptr); // on the actor just made
}

void Missiles::Aim(Missile &m, uint64_t now_ms, const World &world)
{
	// MissileAI.
	if (m.from.speed == 0 || !m.target)
		return;
	Spawn target;
	if (!world.find(m.target, target)) {
		m.target = 0; // EQPlayer's destructor forgets it
		return;
	}
	if (target.dead)
		return;
	if (m.target == m.from.source) {
		m.target = 0;
		return;
	}
	uint32_t every = 0;
	if (m.aim_distance >= 60)
		every = m.aim_distance < 150 ? 125 : m.aim_distance < 250 ? 250 : m.aim_distance < 350 ? 500 : 1000;
	else if (m.aim_distance < 2) {
		m.done = true; // HitActor
		return;
	}
	// Unsigned, as eqgame's: a first ask put off into the future wraps round, and is now.
	if (static_cast<uint32_t>(now_ms) - static_cast<uint32_t>(m.aim_ms) > every) {
		m.aim_ms = now_ms;
		float aim_z = target.at.z + target.height_offset * 0.5f;
		float dz = aim_z - m.at.z;
		m.aim_heading = Bearing(m.at, target.at);
		m.aim_distance = FlatDistance(m.at, target.at);
		if (std::fabs(dz) <= 0.5f)
			m.aim_climb = 0;
		else {
			m.aim_climb = 128 - std::atan2(m.aim_distance, std::fabs(dz)) / TurnStep;
			if (dz < 0)
				m.aim_climb = -m.aim_climb;
		}
	}
	if (m.aim_distance >= 32) {
		const float most = 16;
		float sign;
		float off = HeadingDiff(m.heading, m.aim_heading, sign);
		if (off >= 1) {
			float step = std::min(off, most);
			if ((m.heading < m.aim_heading && sign < 0) || (m.aim_heading < m.heading && sign > 0))
				m.heading -= step;
			else
				m.heading += step;
			m.heading = FixHeading(m.heading);
		}
		else
			m.heading = m.aim_heading;
		off = HeadingDiff(m.climb, m.aim_climb, sign);
		if (off >= 1) {
			// eqgame returns here under 16, leaving the climb off by that much until it's within 32
			// and snaps onto it; we step what's left, as the heading does.
			float step = std::min(off, most);
			m.climb = sign < 0 || m.aim_climb < m.climb ? m.climb - step : m.climb + step;
			m.climb = FixHeading(m.climb);
		}
		else
			m.climb = m.aim_climb;
	}
	else {
		m.heading = m.aim_heading;
		m.climb = m.aim_climb;
	}
}

void Missiles::Physics(Missile &m, uint64_t now_ms)
{
	// process_physics(0, missile, 0).
	float k = static_cast<float>(now_ms - std::min(now_ms, m.physics_ms)) * 0.02f;
	k = std::min(k, 3.0f);
	m.physics_ms = now_ms;
	m.heading = FixHeading(m.heading + k * m.from.heading_rate);
	m.pitch = std::clamp(m.pitch + k * m.from.pitch_rate, -128.0f, 128.0f);
	render::Vec3 push;
	if (m.from.speed != 0) {
		push.x = std::cos(m.heading * TurnStep) * m.from.speed;
		push.y = std::sin(m.heading * TurnStep) * m.from.speed;
		if (m.climb != 0)
			push.z = std::sin(m.climb * TurnStep) * m.from.speed;
	}
	float drag = m.from.aimed ? 0.625f : 0.85f;
	m.velocity = m.velocity * drag + push * 0.8f;
	if (!m.from.aimed && std::hypot(m.velocity.x, m.velocity.y) < 0.05f)
		m.velocity = {};
	m.at = m.at + m.velocity * k;
}

void Missiles::Move(Missile &m, const render::Vec3 &from, uint64_t now_ms, const World &world)
{
	// CDisplay::MoveMissile.
	if (m.expires_ms < now_ms) {
		m.done = true;
		return;
	}
	m.stuck = false;
	if (Still(m.velocity))
		return; // set down (a spell's on its target), until its time's up
	if (world.spawn_hit && world.spawn_hit(from, m.at, m.radius, m.from.source)) {
		m.done = true; // CollsnCallbackForMissile
		return;
	}
	if (world.blocked && world.blocked(from, m.at)) {
		m.at = from;
		m.stuck = true;
	}
}

void Missiles::Destroy(Missile &m)
{
	// ~EQMissile: Frost Bolt's landing where it was a step back.
	if (m.made && m.spell == FrostBolt && m_start_effect) {
		render::Vec3 at = m.at - m.velocity;
		m_start_effect(m, 2, &at);
	}
}

render::Vec3 Missiles::DrawnAt(const Missile &m, uint64_t now_ms) const
{
	if (!m.made || m.stuck)
		return m.at;
	float k = static_cast<float>(std::min<uint64_t>(now_ms - std::min(now_ms, m.physics_ms), 50)) * 0.02f;
	return m.at + m.velocity * k;
}
