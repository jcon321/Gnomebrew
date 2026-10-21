#pragma once

// eqgfx's particle clouds, as much of them as the weather's use (CDisplay's do_rain and do_snow):
//   t3dDefineParticleCloud (@1005a4a0)   a definition: how the particles are made, move and look
//   t3dCreateParticleCloud (@1005a7b0)   a cloud of them, with the clouds its particles carry on
//                                        into where they hit (the ground's and the water's)
//   t3dUpdateParticleCloud (@1005d190)   each frame: the living move on, those whose time is up go
//                                        (into the ground's or water's cloud where they hit), and new
//                                        ones are made every interval, at a random point of the box
//                                        (spawn type 0, the only one done here) around the cloud's
//                                        place or the camera, already that much of the frame old
//   updateParticleCollision (@1005c6f0)  a particle's whole path tried against the world as it's
//                                        made (s3dCollideRaySegmentWithWorld: solid surfaces, and
//                                        the water, lava and slime regions): it dies where it hits
//   t3dPaintParticleCloud (@1005b160)    drawing them: one-pixel points and lines (additive, in the
//                                        rain's color, not fogged), sprites facing the camera, or
//                                        rings growing on the ground (the sprite's additive method)
//   t3dStopPCloudAndAutoDestroy          no more made; the cloud goes once its particles have
// The rest (gravity, wrapping, the other spawn shapes, binding to an actor, sprite animation, the
// distance level of detail) the weather doesn't use and isn't done.

#include "renderer.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace render
{
	struct ParticleCloudDef
	{
		enum class Kind : uint8_t
		{
			Points = 1,  // renderParticlePoint
			Lines = 2,   // renderLineSegment: from each one back along a fortieth of a second of its path
			Sprites = 3, // renderPointLightParticle
			Rings = 4,   // flat on the ground, growing from nothing to `size` over their life
		};
		enum : uint32_t
		{
			Collide = 0x2,                                  // the path is tried against the world as it's made
			FollowX = 0x8, FollowY = 0x10, FollowZ = 0x20, // made around the camera, not the cloud's place
			Wobble = 0x80,                                  // drawn circling 0.5 about its place (a step a frame for sprites)
			Fade = 0x100,                                   // fades out over its life; otherwise half strength
		};
		Kind kind = Kind::Points;
		uint32_t flags = 0;
		uint32_t max_particles = 0; // times the density
		Vec3 low, high;             // the box they're made in
		Vec3 normal;                // their velocity is this times speed (not normalized)
		float speed = 0;
		uint32_t lifespan_ms = 0;
		uint32_t interval_ms = 0xffffffff; // between each one made (over the density); 0xffffffff: none
		float size = 1;                    // a sprite's half width; a ring's at the end of its life
		uint32_t color = 0xffffff;         // sprites'
		const Texture *sprite = nullptr;   // sprites' and rings'
	};

	class ParticleCloud
	{
	public:
		// The first thing on the segment, as a fraction of it, and the region's flags if that's a
		// region (WaterRegion, LavaRegion, SlimeRegion), 0 for a solid surface.
		using Collide = std::function<bool(const Vec3 &from, const Vec3 &to, float &fraction, uint32_t &flags)>;
		static constexpr uint32_t WaterRegion = 0x80000000, LavaRegion = 0x40000000, SlimeRegion = 0x20000000;

		// density: CDisplay::SetPCloudDensity's (0, 0.33, 0.66 or 1). A particle that hits water
		// carries on into `water`, and one that hits anything else into `ground` (or, with none,
		// into `water` too).
		ParticleCloud(const ParticleCloudDef &def, float density, ParticleCloud *ground, ParticleCloud *water);
		// t3dStopPCloudAndAutoDestroy: none made from now on.
		void Stop(uint64_t now_ms);
		bool Stopped() const { return m_stop_ms != 0; }
		// Stopped and all gone: t3dPaintParticleCloud destroys it then.
		bool Done(uint64_t now_ms) const { return m_stop_ms != 0 && m_stop_ms <= now_ms && m_particles.empty(); }
		size_t Count() const { return m_particles.size(); }

		// t3dUpdateParticleCloud. `camera` is where the camera is (FollowX and the rest).
		void Update(uint64_t now_ms, const Vec3 &camera, const Collide &collide);
		// t3dPaintParticleCloud, after Update, under the renderer's camera: `rain_color` is
		// t3dSetRainColor's (the points' and lines'), `ambient` the world's ambient light (the
		// rings'). Leaves the renderer's vertex light at (1, 0, 0).
		void Draw(Renderer &renderer, uint64_t now_ms, uint32_t rain_color, const Vec3 &ambient);

	private:
		struct Particle
		{
			Vec3 position, velocity;
			uint64_t birth_ms = 0, death_ms = 0;
			Vec3 hit;          // where its path meets the world, just short of it
			uint32_t hit_flags = 0; // 0 nothing; the region's flags, or 1 a solid surface
			int wobble = 0;    // into the wobble's 32 steps
		};
		void Make(Particle &p, const Vec3 &position, float age_s, uint64_t now_ms, const Collide *collide);
		// updateParticlePhysics with a parent's particle: one made where it hit, born as it died.
		void Carry(const Particle &parent, uint64_t now_ms);
		int Rand(); // the MSVC runtime's rand()

		ParticleCloudDef m_def;
		uint32_t m_max = 0;
		bool m_makes = false;    // has an interval (and a density)
		uint64_t m_interval = 0;
		ParticleCloud *m_ground, *m_water;
		std::vector<Particle> m_particles;
		uint64_t m_last_ms = 0, m_carry_ms = 0, m_stop_ms = 0;
		Vec3 m_camera;
		uint32_t m_random;
		std::vector<Vertex> m_vertices;
		std::vector<uint32_t> m_indices;
	};
}
