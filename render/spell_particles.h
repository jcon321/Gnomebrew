#pragma once

// eqgfx's particle system (CParticleSystem), which draws the Luclin spell effects:
//   loadParticleEffectData (@10058960)   spellsnew.edd: "EDD\0", "105\0", then 0x138-byte emitter
//                                        definitions (EmitterDef); textures from SpellEffects\.
//   createParticleEmitter (@10043840)    an emitter of a definition: how many particles it keeps
//                                        (at the density), for how long, made in a frame (where it
//                                        is and its three axes) it may follow.
//   renderParticleSystem (@10055ef0)     each frame, each emitter: its frame follows what it's bound
//                                        to; the first burst, then `per_emit` every 1 / rate seconds
//                                        (fewer beyond lod_distance from the camera, never under 16);
//                                        each made on its shape in the frame, moving out along it and
//                                        the axes, falling and drifting; drawn as camera-facing
//                                        squares (or streaks along axis 0), growing and fading in and
//                                        out, their color going from `start` to `end`, a 2x2 or 4x4
//                                        sheet's cells stepping on. It goes once its time is up and
//                                        its particles are gone.
//   renderParticleEffects (@100584b0)    SRCALPHA over INVSRCALPHA (ONE for `additive`), the texel's
//                                        alpha times the particle's, alpha tested at 8, no fog.
// eqgfx projects the particles itself; here they're quads in the world, which comes to the same.
// Angles are 512ths of a turn, times seconds, lengths world units.

#include "renderer.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace render
{
	struct EmitterDef
	{
		std::string name, texture;
		bool bone_axes = false;  // 0x60: a bone's frame (turned round) rather than its actor's
		bool no_depth_write = false; // 0x64
		bool additive = false;   // 0x68
		bool streak = false;     // 0x6c: drawn along axis 0, `size` each way
		bool follow = false;     // 0x70: the particles move with the frame
		float duration = 0;      // 0x74: the emitter's; < 0 always that long, else the caller's if it gives one
		float life = 0;          // 0x78: a particle's; < 0 the emitter's time left
		int initial = 0;         // 0x7c: made at once
		int per_emit = 0;        // 0x80: made each time
		float rate = 0;          // 0x84: times a second
		float stop_early = 0;    // 0x88: none made this long before the end
		float fade_in = 0, fade_out = 0; // 0x8c, 0x90: the alpha
		float grow = 0, shrink = 0;      // 0x94, 0x98: the size
		float lod_distance = 0;  // 0x9c
		float max_alpha = 1;     // 0xa0
		int shape = 0;           // 0xa4: 0 a point, 1 a ring in turn, 2 rings stacked, 3 a cylinder,
		                         //       4 a disk, 5 a sphere, 6 a box, 7 a cone, 8 a torus, 9 a ring
		float radius = 0, radius2 = 0, height = 0; // 0xa8, 0xac (an ellipse's other radius), 0xb0
		float offset[3] = {};    // 0xb4: the frame moved along its axes
		float tilt[2] = {};      // 0xc0, 0xc4: axis 0 turned toward axis 1, then axis 2
		float size = 1;          // 0xc8
		float depth_bias = 0;    // 0xcc: toward the camera
		int start[3] = {255, 255, 255}, end[3] = {255, 255, 255}; // 0xd0, 0xdc: RGB
		// 0xe8..0x110: velocity ranges (each a low and a high) and accelerations: along axis 0 (up),
		// axis 1, axis 2, and out from the shape's center.
		float up_velocity[2] = {}, up_accel = 0;
		float axis1_velocity[2] = {}, axis1_accel = 0;
		float axis2_velocity[2] = {}, axis2_accel = 0;
		float out_velocity[2] = {}, out_accel = 0;
		float spin_velocity[2] = {}, spin_accel = 0; // 0x118..0x120: around axis 0
		float gravity = 0;       // 0x124: down, world z
		float drift = 0;         // 0x128: along world x
		int cells = 1;           // 0x12c: 16 a 4x4 sheet, 4 a 2x2, else the whole texture
		float cell_rate = 0;     // 0x130: cells a second
		float sprite_spin = 0;   // 0x134
	};

	class SpellParticles
	{
	public:
		bool Load(const std::string &eq_dir, std::string &error);
		size_t DefCount() const { return m_defs.size(); }
		const EmitterDef *Def(int index) const;

		// An emitter's frame: where it is, and its axes (0 up, then two across).
		struct Frame
		{
			Vec3 position;
			Vec3 axes[3] = {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}};
			bool bone = false;  // bone_axes is there
			Vec3 bone_axes[3];
		};
		// Where a bound emitter's frame is now; false when what it's bound to is gone, and its
		// emitters go too (t3dParticleSystemDestroyBoundEmitters).
		using Locate = std::function<bool(uint32_t binding, Frame &frame)>;

		// t3dParticleSystemCreateEmitter: binding 0 stays at `frame`. Density is the options' 0..1
		// (none made at 0), opacity their 0..1. Returns how long it shows (ms); 0 when there's nothing.
		int Create(int def, uint32_t tag, int duration_ms, float density, float opacity, const Frame &frame, uint32_t binding);
		void DestroyTagged(uint32_t tag);
		void DestroyBound(uint32_t binding);
		// t3dParticleSystemReplaceEmitterBinding (replaceParticleEmitterBinding): the emitters bound
		// to `from` are bound to `to`.
		void Rebind(uint32_t from, uint32_t to);
		void Clear() { m_emitters.clear(); }

		// renderParticleSystem: `camera` for the level of detail; then Draw under the renderer's camera.
		void Update(uint64_t now_ms, const Vec3 &camera, const Locate &locate);
		// t3dParticleSystemSetNearClipPlane: particles nearer the camera than this (less their
		// depth bias) aren't drawn (renderParticleSystem); the options' 2, 4, 6 or 8.
		void SetNearClip(float distance) { m_near_clip = distance; }
		void Draw(Renderer &renderer);
		size_t EmitterCount() const { return m_emitters.size(); }
		size_t ParticleCount() const;

	private:
		float m_near_clip = 2;
		struct Particle
		{
			Vec3 origin;
			Vec3 axes[3];
			float angle = 0, radius = 0, height = 0;
			float out[2] = {1, 0}; // the way `out` moves radius and height
			float up_v = 0, axis1_v = 0, axis2_v = 0, out_v = 0, spin_v = 0, fall_v = 0;
			float life = 0, left = 0;
		};
		struct Emitter
		{
			int def = 0;
			uint32_t tag = 0, binding = 0;
			int capacity = 0, initial = 0, per_emit = 0;
			float rate = 0, duration = 0, left = 0, opacity = 1;
			float lod_initial = 1, lod_emit = 1; // the least of the level of detail
			int ticks = -1;   // emissions so far; -1 before the first burst
			int made = 0;
			Frame frame;
			std::vector<Particle> particles; // oldest first
		};
		void Place(const EmitterDef &def, const Frame &frame, Particle &p) const;
		void Make(const EmitterDef &def, Emitter &e, int count);
		const Texture *TextureFor(const EmitterDef &def);
		int Rand(); // the MSVC runtime's rand()
		float Rand01() { return static_cast<float>(Rand()) * 3.051851e-05f; }

		std::string m_dir; // SpellEffects
		std::vector<EmitterDef> m_defs;
		std::map<std::string, std::unique_ptr<Texture>> m_textures; // by lowercase name; null: missing
		std::vector<Emitter> m_emitters;
		uint64_t m_last_ms = 0;
		uint32_t m_random = 1;
		// renderParticleSystem's rings: how many go round one, and how many shape 2 stacks; carried from
		// emitter to emitter, the first starting over each frame.
		int m_ring = 1, m_rings = 8;
		std::vector<Vertex> m_vertices;
		std::vector<uint32_t> m_indices;
	};
}
