#pragma once

// Player race models (the classic ones the TAKP client uses: global_chr.s3d,
// global4_chr.s3d for Iksar, global7_chr.s3d for Vah Shir) and the creatures,
// dressed from the equipment list (or an NPC's body texture and helm), holding
// their weapons and shield (the IT models in gequip*.s3d) and playing any of their
// animations by track code (see animations.h).

#include "particles.h"
#include "point_lights.h"
#include "renderer.h"
#include "skeleton.h"
#include "textures.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace render
{
	// Equipment slots as the server sends them: head, chest, arms, wrists, hands, legs, feet, primary, secondary.
	constexpr int EquipSlots = 9;

	struct CharacterAppearance
	{
		uint16_t race = 1;
		uint8_t gender = 0;
		uint8_t face = 0;
		uint32_t equip[EquipSlots] = {}; // material per armor slot (0 none, 1 leather, 2 chain, 3 plate, 10-16 robes);
		                                 // the IT model number for primary and secondary (0 empty)
		uint32_t tint[EquipSlots] = {};  // 0xFFRRGGBB when tinted
		float height = 0;                // the spawn's size; 0 = the race's default
		uint8_t texture = 0xff;          // 0xFF: dressed per slot; else an NPC's body texture (Spawn_Struct bodytexture)
		uint8_t helm = 0;                // an NPC's head (Spawn_Struct helm)
	};

	// Three-letter model code for a playable race ("HUM", "IKF", ...), or empty.
	std::string RaceModelCode(uint16_t race, uint8_t gender);
	float RaceHeight(uint16_t race); // a player race's default height

	struct ModelLighting
	{
		Vec3 ambient{0.5f, 0.5f, 0.5f};
		Vec3 sun_direction{0, 0, -1}; // the way it shines
		Vec3 sun_color;
		const std::vector<PointLight> *lights = nullptr;
	};

	class CharacterModel
	{
	public:
		// placement: model space -> world. Lit as calculateMeshLighting (@1004da40) lights an actor:
		// the ambient, the sun (color x the cosine) and `lights` (color x 0.05 radius^2 x cos /
		// distance^2 each), up to full. `time_ms` runs the animation (looping, or stopping on its
		// last frame); a missing one plays the idle.
		void Draw(Renderer &renderer, const Transform &placement, const ModelLighting &lighting, uint64_t time_ms,
			const std::string &animation = "P01", bool loop = true) const;
		bool HasAnimation(const std::string &animation) const { return m_animations->count(animation) != 0; }
		uint32_t AnimationMs(const std::string &animation) const; // one loop

		// Extent of the idle's first frame, scaled (model space).
		float MinZ() const { return m_min_z; }
		float MaxZ() const { return m_max_z; }
		float HeadZ() const { return m_head_z; } // the head bone
		// How far the model's origin sits over its feet: placement = feet + (0, 0, OriginOverFeet()).
		float OriginOverFeet() const { return m_origin_over_feet; }
		const std::string &Code() const { return m_code; }
		float Scale() const { return m_scale; } // model units -> world
		size_t TriangleCount() const;

		// A held item's particle cloud (a torch's flame and smoke): on the model's bone the item
		// hangs from, `local` its own bone's place there (IT36_P1_DAG in the item's base pose).
		struct Emitter
		{
			uint16_t bone = 0;
			Transform local;
			ParticleCloudDef def;
		};
		const std::vector<Emitter> &Emitters() const { return m_emitters; }
		// Where each emitter is (world space) for Draw's arguments.
		void PlaceEmitters(const Transform &placement, uint64_t time_ms, const std::string &animation, bool loop,
			std::vector<Transform> &out) const;

	private:
		friend class CharacterModels;
		struct Batch
		{
			Material material;
			uint32_t tint = 0xffffff;
			std::vector<uint32_t> indices;
		};

		std::string m_code;
		WldSkeleton m_skeleton;
		std::shared_ptr<const std::map<std::string, SkeletonAnimation>> m_animations; // by code ("L01"), shared
		std::vector<Vec3> m_positions, m_normals; // bone space
		std::vector<uint16_t> m_bones;           // per vertex
		std::vector<float> m_uvs;
		std::vector<Batch> m_batches;
		std::vector<Emitter> m_emitters;
		float m_min_z = 0, m_max_z = 0, m_head_z = 0;
		float m_scale = 1;
		float m_origin_over_feet = 0;
		// Draw's scratch: the posed bones as matrices, and the vertices' light.
		struct BoneMatrix
		{
			float m[3][3]; // rotation times scale
			float inverse_scale;
			Vec3 translation;
			Vec3 light; // toward the sun, in the bone's space
		};
		mutable std::vector<BoneMatrix> m_bone_matrices;
		mutable std::vector<Vec3> m_intensity;
		mutable std::vector<Vertex> m_vertices;
	};

	// The particle clouds of one actor's held items, made for the model it's drawn with (t3dCreateActorEx
	// -> t3dCreateParticleCloud, one set per actor), following their bones.
	class HeldParticles
	{
	public:
		// After the model's Draw, with its arguments: the clouds move on and are drawn.
		void Draw(Renderer &renderer, const CharacterModel &model, const Transform &placement, uint64_t time_ms,
			const std::string &animation, bool loop, uint64_t now_ms, const Vec3 &ambient);
		void Clear();

	private:
		const CharacterModel *m_model = nullptr;
		std::vector<std::unique_ptr<ParticleCloud>> m_clouds;
		std::vector<Transform> m_emitters;
	};

	class CharacterModels
	{
	public:
		// global_chr.s3d (required), global4_chr.s3d (Iksar) and global7_chr.s3d (Vah Shir); with
		// `creatures` all of global2..7_chr.s3d. And the weapons and shields, gequip*.s3d.
		bool Open(const std::string &eq_dir, std::string &error, bool creatures = false);
		// A zone's own models, <zone>_chr.s3d and friends, searched first.
		bool AddZone(const std::string &eq_dir, const std::string &zone);
		std::unique_ptr<CharacterModel> Build(const CharacterAppearance &appearance, std::string &error);

	private:
		struct Source
		{
			PfsArchive archive;
			WldFile wld;
			TextureCache textures;
		};
		bool AddSource(std::vector<std::unique_ptr<Source>> &to, const std::string &eq_dir, const std::string &name);
		std::vector<std::unique_ptr<Source>> m_sources;
		std::vector<std::unique_ptr<Source>> m_items; // gequip*.s3d, searched after the models
		std::map<std::string, std::shared_ptr<const std::map<std::string, SkeletonAnimation>>> m_animation_sets; // by model code
	};
}
