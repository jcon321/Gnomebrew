#pragma once

// The sky from sky.s3d: for sky type N, LAYERN1 is the dome and LAYERN3 the
// cloud layer. Both are small domes drawn around the camera before the zone,
// so only direction matters; the clouds drift.

#include "zone_scene.h"

namespace render
{
	class Sky
	{
	public:
		bool Load(const std::string &eq_dir, int type, std::string &error);
		bool Loaded() const { return !m_layers.empty(); }
		// Draws into a pass whose camera sits at the origin.
		void Draw(Renderer &renderer, uint64_t now_ms);
		uint32_t HorizonColor() const { return m_horizon; } // fills below the dome

	private:
		PfsArchive m_archive;
		TextureCache m_textures;
		std::vector<StaticMesh> m_layers;
		std::vector<std::vector<float>> m_base_uvs; // per layer, for the cloud drift
		uint32_t m_horizon = 0;
	};
}
