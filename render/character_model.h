#pragma once

// Player race models (the classic ones the TAKP client uses: global_chr.s3d,
// global4_chr.s3d for Iksar, global7_chr.s3d for Vah Shir), playing the "P01"
// standing idle and dressed from the character select equipment list.

#include "rasterizer.h"
#include "skeleton.h"
#include "textures.h"

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
	};

	// Three-letter model code for a playable race ("HUM", "IKF", ...), or empty.
	std::string RaceModelCode(uint16_t race, uint8_t gender);

	class CharacterModel
	{
	public:
		// placement: model space -> world. Lit by a directional light (toward `light_direction`) over ambient.
		void Draw(Rasterizer &rasterizer, const Transform &placement, const Vec3 &light_direction, float ambient,
			float diffuse, uint64_t now_ms) const;

		// Model-space extent of the idle's first frame, after scaling to the race's height.
		float MinZ() const { return m_min_z; }
		float MaxZ() const { return m_max_z; }
		float HeadZ() const { return m_head_z; } // the head bone
		const std::string &Code() const { return m_code; }
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
		SkeletonAnimation m_idle;
		std::vector<Vec3> m_positions, m_normals; // bone space
		std::vector<uint16_t> m_bones;           // per vertex
		std::vector<float> m_uvs;
		std::vector<Batch> m_batches;
		float m_min_z = 0, m_max_z = 0, m_head_z = 0;
		float m_scale = 1;
		mutable std::vector<Vertex> m_vertices;
	};

	class CharacterModels
	{
	public:
		bool Open(const std::string &eq_dir, std::string &error);
		std::unique_ptr<CharacterModel> Build(const CharacterAppearance &appearance, std::string &error);

	private:
		struct Source
		{
			PfsArchive archive;
			WldFile wld;
			TextureCache textures;
		};
		std::vector<std::unique_ptr<Source>> m_sources;
	};
}
