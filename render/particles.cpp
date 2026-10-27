#include "particles.h"

#include <algorithm>
#include <cmath>
#include <ctime>

namespace render
{
	namespace
	{
		// t3dInitParticleCloudTables' wobble: 32 steps around a circle of 0.5, a sixteenth of a turn each.
		Vec3 WobbleAt(int step)
		{
			float a = static_cast<float>(step & 31) * 0.3926991f;
			return {std::sin(a) * 0.5f, std::cos(a) * 0.5f, 0};
		}

		// A color's channels times alpha / 256: the additive blends add what the alpha leaves.
		uint32_t Scale(uint32_t rgb, int alpha)
		{
			uint32_t r = (((rgb >> 16) & 0xff) * static_cast<uint32_t>(alpha)) >> 8;
			uint32_t g = (((rgb >> 8) & 0xff) * static_cast<uint32_t>(alpha)) >> 8;
			uint32_t b = ((rgb & 0xff) * static_cast<uint32_t>(alpha)) >> 8;
			return (r << 16) | (g << 8) | b;
		}

		uint32_t Lerp(uint64_t from, uint64_t to, uint64_t now) // 255 at `from` down to 0 at `to`
		{
			if (to <= from)
				return 0;
			uint64_t age = std::min(now - std::min(now, from), to - from);
			return static_cast<uint32_t>(255 - age * 255 / (to - from));
		}
	}

	ParticleCloud::ParticleCloud(const ParticleCloudDef &def, float density, ParticleCloud *ground, ParticleCloud *water)
		: m_def(def), m_ground(ground), m_water(water), m_random(static_cast<uint32_t>(time(nullptr)))
	{
		// t3dInitPCloudInstBase: room for the definition's count times the density, and one made
		// every interval over the density (none at density 0).
		m_max = static_cast<uint32_t>(static_cast<float>(def.max_particles) * density);
		m_makes = density > 0 && def.interval_ms != 0xffffffff;
		if (m_makes)
			m_interval = static_cast<uint64_t>(static_cast<double>(def.interval_ms) * (1.0 / density));
		m_particles.reserve(m_max);
		// t3dSetPCloudBaseSpawnNormal: one axis square to the normal on the ground (or, for a
		// normal straight up or down, square to it on x), the second square to both.
		if (def.spawn != ParticleCloudDef::Spawn::Box && def.spawn != ParticleCloudDef::Spawn::Sphere) {
			const Vec3 &n = def.normal;
			Vec3 v{n.y, -n.x, 0};
			if (std::fabs(v.x) < 0.0001f && std::fabs(v.y) < 0.0001f)
				v = {0, n.z, -n.y};
			m_axes[0] = Cross(v, n);
			m_axes[1] = v;
			m_axes[2] = n;
		}
	}

	int ParticleCloud::Rand()
	{
		m_random = m_random * 214013u + 2531011u;
		return static_cast<int>((m_random >> 16) & 0x7fff);
	}

	void ParticleCloud::Stop(uint64_t now_ms)
	{
		if (!m_stop_ms)
			m_stop_ms = std::max<uint64_t>(now_ms, 1);
	}

	void ParticleCloud::Make(Particle &p, const Vec3 &position, const Vec3 &velocity, float age_s, uint64_t now_ms,
		const Collide *collide)
	{
		p.position = position;
		if (m_def.flags & ParticleCloudDef::FollowX)
			p.position.x += m_camera.x;
		if (m_def.flags & ParticleCloudDef::FollowY)
			p.position.y += m_camera.y;
		if (m_def.flags & ParticleCloudDef::FollowZ)
			p.position.z += m_camera.z;
		p.velocity = velocity;
		uint64_t age_ms = static_cast<uint64_t>(std::lround(age_s * 1000));
		p.birth_ms = now_ms - std::min(now_ms, age_ms);
		p.hit_flags = 0;
		p.death_ms = p.birth_ms + m_def.lifespan_ms;
		if (collide && *collide && (m_def.flags & ParticleCloudDef::Collide)) {
			// updateParticleCollision: the whole path over its life; it dies just short of what it meets.
			Vec3 to = p.position + p.velocity * (static_cast<float>(m_def.lifespan_ms) * 0.001f);
			float fraction;
			uint32_t flags;
			if ((*collide)(p.position, to, fraction, flags)) {
				fraction = std::max(fraction - 0.001f, 0.0f);
				p.hit = p.position + (to - p.position) * fraction;
				p.hit_flags = flags ? flags : 1;
				p.death_ms = p.birth_ms + static_cast<uint64_t>(static_cast<float>(m_def.lifespan_ms) * fraction);
			}
		}
		p.wobble = Rand() & 31;
		Move(p, age_s); // as old as it is
	}

	void ParticleCloud::Move(Particle &p, float seconds) const
	{
		// On along its velocity, then the gravity's pull: half its square taken back off the place
		// (as eqgfx has it) and the speed it adds on.
		p.position = p.position + p.velocity * seconds;
		if (m_def.gravity != 0) {
			Vec3 pull = m_def.gravity_direction * m_def.gravity;
			p.position = p.position - pull * (seconds * seconds * 0.5f);
			p.velocity = p.velocity + pull * seconds;
		}
	}

	void ParticleCloud::Carry(const Particle &parent, uint64_t now_ms)
	{
		if (m_particles.size() >= m_max)
			return;
		m_particles.emplace_back();
		float age_s = static_cast<float>(now_ms - std::min(now_ms, parent.death_ms)) * 0.001f;
		Make(m_particles.back(), parent.hit, m_def.normal * m_def.speed, age_s, now_ms, nullptr);
	}

	void ParticleCloud::Update(uint64_t now_ms, const Vec3 &camera, const Collide &collide)
	{
		if (m_last_ms == 0) {
			m_last_ms = now_ms;
			if (m_def.duration_ms)
				m_end_ms = now_ms + m_def.duration_ms; // t3dSetPCloudDuration
		}
		uint64_t elapsed = now_ms - std::min(now_ms, m_last_ms);
		m_last_ms = now_ms;
		float elapsed_s = static_cast<float>(elapsed) * 0.001f;
		m_camera = camera;

		// The living move on; those whose time has come go, into the ground's or the water's
		// cloud where they hit.
		for (size_t i = 0; i < m_particles.size();) {
			Particle &p = m_particles[i];
			if (p.death_ms > now_ms) {
				Move(p, elapsed_s);
				++i;
				continue;
			}
			if (p.hit_flags) {
				ParticleCloud *into = m_ground && p.hit_flags != WaterRegion ? m_ground : m_water;
				if (into)
					into->Carry(p, now_ms);
			}
			p = m_particles.back();
			m_particles.pop_back();
		}

		// New ones, each a random part of the frame old, until Stop.
		if (!m_makes || (m_stop_ms && now_ms >= m_stop_ms) || (m_end_ms && now_ms >= m_end_ms))
			return;
		uint64_t count = 1; // an interval of 0: one a frame
		if (m_interval) {
			m_carry_ms += elapsed;
			count = m_carry_ms / m_interval;
			m_carry_ms -= count * m_interval;
		}
		count = std::min<uint64_t>(count, m_max - std::min<size_t>(m_max, m_particles.size()));
		// getParticleEmitterMatrix: a bound cloud is made from its bone, along the way it points;
		// a Local one in the bone's own space.
		bool world = m_bound && !(m_def.flags & ParticleCloudDef::Local);
		Vec3 origin = world ? m_emitter.translation : Vec3{};
		Vec3 axes[3];
		for (int k = 0; k < 3; ++k)
			axes[k] = world ? m_emitter.ApplyDirection(m_axes[k]) : m_axes[k];
		const Vec3 span = m_def.high - m_def.low;
		constexpr float TurnStep = 6.2831855f / 512; // t3dFloatFastSine's table
		for (uint64_t n = 0; n < count; ++n) {
			Vec3 at = origin, velocity = m_def.normal * m_def.speed;
			switch (m_def.spawn) {
			case ParticleCloudDef::Spawn::Box:
				at = at + Vec3{span.x * Rand() * 3.051851e-05f + m_def.low.x, span.y * Rand() * 3.051851e-05f + m_def.low.y,
					span.z * Rand() * 3.051851e-05f + m_def.low.z};
				break;
			case ParticleCloudDef::Spawn::Sphere: {
				// Straight out from the emitter, in the world's axes.
				float around = static_cast<float>(Rand() & 0x1ff) * TurnStep;
				float up = static_cast<float>(Rand() & 0x1ff) * TurnStep;
				Vec3 out{std::cos(around) * std::sin(up), std::sin(around) * std::sin(up), std::cos(up)};
				at = at + out * m_def.radius;
				velocity = out * m_def.speed;
				break;
			}
			case ParticleCloudDef::Spawn::Ring:
			case ParticleCloudDef::Spawn::Circle: {
				// `radius` out across the normal; a ring's head straight out, a circle's along the
				// (unturned) normal.
				float around = static_cast<float>(Rand() & 0x1ff) * TurnStep;
				Vec3 out = axes[0] * std::cos(around) + axes[1] * std::sin(around);
				at = at + out * m_def.radius;
				if (m_def.spawn == ParticleCloudDef::Spawn::Ring)
					velocity = out * m_def.speed;
				break;
			}
			case ParticleCloudDef::Spawn::Cone: {
				float spread = std::sin(static_cast<float>(Rand()) * m_def.angle * 3.051851e-05f * TurnStep);
				float around = static_cast<float>(Rand() & 0x1ff) * TurnStep;
				velocity = (axes[0] * (std::cos(around) * spread) + axes[1] * (std::sin(around) * spread) + axes[2]) * m_def.speed;
				break;
			}
			case ParticleCloudDef::Spawn::Disk: {
				float r = static_cast<float>(Rand()) * m_def.radius * 3.051851e-05f;
				float around = static_cast<float>(Rand() & 0x1ff) * TurnStep;
				at = at + axes[0] * (std::cos(around) * r) + axes[1] * (std::sin(around) * r);
				break;
			}
			}
			float age_s = elapsed_s * static_cast<float>(Rand() & 0x3ff) * 0.0009765625f;
			m_particles.emplace_back();
			Make(m_particles.back(), at, velocity, age_s, now_ms, &collide);
		}
	}

	void ParticleCloud::Draw(Renderer &renderer, uint64_t now_ms, uint32_t rain_color, const Vec3 &ambient)
	{
		if (m_particles.empty())
			return;
		const Camera &camera = renderer.CurrentCamera();
		Vec3 forward = Normalize(camera.forward);
		Vec3 right = Normalize(Cross(forward, camera.up));
		Vec3 up = Cross(right, forward);
		// A pixel's width at a depth: the points and lines are one wide.
		float pixel = std::tan(camera.horizontal_fov * 0.5f) / (renderer.Width() * 0.5f);
		auto depth = [&](const Vec3 &p) { return std::max(Dot(p - camera.position, forward), camera.near_plane); };

		m_vertices.clear();
		m_indices.clear();
		auto quad = [&](const Vec3 &a, const Vec3 &b, const Vec3 &c, const Vec3 &d, uint32_t ca, uint32_t cb, uint32_t cc,
			uint32_t cd) {
			uint32_t base = static_cast<uint32_t>(m_vertices.size());
			const Vec3 *corners[4] = {&a, &b, &c, &d};
			const uint32_t colors[4] = {ca, cb, cc, cd};
			const float uv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
			for (int k = 0; k < 4; ++k) {
				Vertex v;
				v.position = *corners[k];
				v.u = uv[k][0];
				v.v = uv[k][1];
				v.color = colors[k]; // AA 0: no ambient added
				m_vertices.push_back(v);
			}
			for (uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u})
				m_indices.push_back(base + k);
		};
		bool fade = m_def.flags & ParticleCloudDef::Fade;
		bool wobble = m_def.flags & ParticleCloudDef::Wobble;
		// t3dPaintParticleCloud: a Local cloud's particles placed by its bone, and the sprites
		// sized with it (the actor's scale times the bone's).
		bool local = m_bound && (m_def.flags & ParticleCloudDef::Local);
		float size = m_def.size * (m_bound && (m_def.flags & (ParticleCloudDef::Local | ParticleCloudDef::ActorScaled)) ? m_emitter.scale : 1);
		// renderPointLightParticle steps the sprite's frames on with the time.
		const Texture *sprite = m_def.frames.empty() ? m_def.sprite
		                        : m_def.frames[(now_ms / std::max<uint32_t>(m_def.frame_ms, 1)) % m_def.frames.size()];
		Material material;
		material.blend = Blend::Additive;
		material.two_sided = true;
		if (!m_def.fog) {
			material.fog = false;
			material.texel_alpha = true;
		}

		switch (m_def.kind) {
		case ParticleCloudDef::Kind::Points:
			// renderParticlePoint: the rain's color at the particle's alpha (fading, or half), a pixel.
			for (const Particle &p : m_particles) {
				Vec3 at = wobble ? p.position + WobbleAt(p.wobble) : p.position;
				float half = depth(at) * pixel * 0.5f;
				uint32_t color = Scale(rain_color, fade ? static_cast<int>(Lerp(p.birth_ms, p.death_ms, now_ms)) : 0x80);
				Vec3 r = right * half, u = up * half;
				quad(at - r - u, at + r - u, at + r + u, at - r + u, color, color, color, color);
			}
			material.fog = false;
			break;
		case ParticleCloudDef::Kind::Lines: {
			// renderLineSegment: from the particle back along a fortieth of a second of its path,
			// the rain's color at 3/4 fading to nothing, a pixel wide.
			uint32_t head = Scale(rain_color, 191);
			for (const Particle &p : m_particles) {
				Vec3 a = wobble ? p.position + WobbleAt(p.wobble) : p.position;
				Vec3 b = a - p.velocity * 0.025f;
				Vec3 side = Cross(b - a, a - camera.position);
				side = Length(side) > 1e-6f ? Normalize(side) : right;
				Vec3 sa = side * (depth(a) * pixel * 0.5f), sb = side * (depth(b) * pixel * 0.5f);
				quad(a - sa, a + sa, b + sb, b - sb, head, head, 0, 0);
			}
			material.fog = false;
			break;
		}
		case ParticleCloudDef::Kind::Sprites:
			// renderPointLightParticle: the sprite facing the camera, its corners `size` out at 45
			// degrees (so 0.707 of it either side) with the texture's v = 1 along the top, in the
			// cloud's color at the particle's alpha; the wobble steps on a frame each time.
			if (!sprite)
				return;
			for (Particle &p : m_particles) {
				Vec3 at = local ? m_emitter.Apply(p.position) : p.position;
				if (wobble) {
					at = at + WobbleAt(p.wobble);
					p.wobble = (p.wobble + 1) & 31;
				}
				uint32_t color = Scale(m_def.color, fade ? static_cast<int>(Lerp(p.birth_ms, p.death_ms, now_ms)) + 1 : 0x80);
				Vec3 r = right * (size * 0.7071068f), u = up * (size * 0.7071068f);
				quad(at - r + u, at + r + u, at + r - u, at - r - u, color, color, color, color);
			}
			material.texture = sprite;
			break;
		case ParticleCloudDef::Kind::Rings: {
			// The sprite flat on the ground, from nothing to `size` either side over its life, lit
			// by the world's ambient light and fading out.
			if (!sprite)
				return;
			uint32_t lit = (static_cast<uint32_t>(std::clamp(ambient.x, 0.0f, 1.0f) * 255) << 16) |
			               (static_cast<uint32_t>(std::clamp(ambient.y, 0.0f, 1.0f) * 255) << 8) |
			               static_cast<uint32_t>(std::clamp(ambient.z, 0.0f, 1.0f) * 255);
			for (const Particle &p : m_particles) {
				float life = p.death_ms > p.birth_ms
				                 ? static_cast<float>(now_ms - std::min(now_ms, p.birth_ms)) / static_cast<float>(p.death_ms - p.birth_ms)
				                 : 1;
				life = std::min(life, 1.0f);
				float half = life * m_def.size;
				uint32_t color = Scale(lit, fade ? static_cast<int>((1 - life) * 255) + 1 : 0x80);
				const Vec3 &c = p.position;
				quad({c.x - half, c.y - half, c.z}, {c.x + half, c.y - half, c.z}, {c.x + half, c.y + half, c.z},
					{c.x - half, c.y + half, c.z}, color, color, color, color);
			}
			material.texture = sprite;
			break;
		}
		}
		renderer.SetVertexLight({1, 1, 1}, {0, 0, 0});
		renderer.Draw(m_vertices.data(), m_vertices.size(), m_indices.data(), m_indices.size(), material);
	}
}
