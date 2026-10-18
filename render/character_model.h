#pragma once

// Player race models (the classic ones the TAKP client uses: global_chr.s3d,
// global4_chr.s3d for Iksar, global7_chr.s3d for Vah Shir) and the creatures,
// dressed from the equipment list (or an NPC's body texture and helm) and playing
// any of their animations by track code (see animations.h).

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
		uint32_t equip[EquipSlots] = {}; // material per slot (0 none, 1 leather, 2 chain, 3 plate, ...)
		uint32_t tint[EquipSlots] = {};  // 0xFFRRGGBB when tinted
		float height = 0;                // the spawn's size; 0 = the race's default
		uint8_t texture = 0xff;          // 0xFF: dressed per slot; else an NPC's body texture (Spawn_Struct bodytexture)
		uint8_t helm = 0;                // an NPC's head (Spawn_Struct helm)
	};

	// Three-letter model code for a playable race ("HUM", "IKF", ...), or empty.
	std::string RaceModelCode(uint16_t race, uint8_t gender);
	float RaceHeight(uint16_t race); // a player race's default height

	class CharacterModel
	{
	public:
		// placement: model space -> world. Lit by a directional light (toward `light_direction`) over ambient.
		// `time_ms` runs the animation (looping, or stopping on its last frame); a missing one plays the idle.
		void Draw(Renderer &renderer, const Transform &placement, const Vec3 &light_direction, float ambient,
			float diffuse, uint64_t time_ms, const std::string &animation = "P01", bool loop = true) const;
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
		float m_min_z = 0, m_max_z = 0, m_head_z = 0;
		float m_scale = 1;
		float m_origin_over_feet = 0;
		// Draw's scratch: the posed bones as matrices, and the vertices' light.
		struct BoneMatrix
		{
			float m[3][3]; // rotation times scale
			Vec3 translation;
			Vec3 light; // the light direction in the bone's space
		};
		mutable std::vector<BoneMatrix> m_bone_matrices;
		mutable std::vector<float> m_intensity;
		mutable std::vector<Vertex> m_vertices;
	};

	class CharacterModels
	{
	public:
		// global_chr.s3d (required), global4_chr.s3d (Iksar) and global7_chr.s3d (Vah Shir); with
		// `creatures` all of global2..7_chr.s3d.
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
		bool AddSource(const std::string &eq_dir, const std::string &name);
		std::vector<std::unique_ptr<Source>> m_sources;
		std::map<std::string, std::shared_ptr<const std::map<std::string, SkeletonAnimation>>> m_animation_sets; // by model code
	};
}
