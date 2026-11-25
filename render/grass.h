#pragma once

#include "renderer.h"

#include <memory>
#include <string>
#include <vector>

// The grass round the camera (eqgame's CDisplay::loadGrassObjects @004b2043, eqgfx's radial flora:
// t3dSetRadialFloraObjectList, renderLightSources @10041250, which is mislabeled).
//   - <zone>.dat: 32 byte records {float a[3], b[3], height, i32 texture}, a blade's foot from a to b.
//     Twelve zones of Luclin have one. Its textures are grass.s3d's, by eqgfx's table of 124 names.
//   - Drawn each frame within the radius (200) of the camera across the ground, by the distance to a:
//     a quad standing on the foot, lit by the ambient light + 0.15 alone, blended by the texture's
//     alpha times 255 - k, k being 255 times the squared distance over the squared radius (20 at
//     least), from both sides, no depth written. At most 3000.
namespace render
{
	class Grass
	{
	public:
		// False when the zone has none (or no grass.s3d).
		bool Load(const std::string &eq_dir, const std::string &zone);
		void Draw(Renderer &renderer, const Vec3 &camera, const Vec3 &ambient) const;
		size_t Count() const { return m_blades.size(); }
		size_t Drawn() const { return m_drawn; } // the last Draw's

	private:
		struct Blade
		{
			Vec3 a, b;
			float height;
			const Texture *texture;
		};
		std::vector<Blade> m_blades;
		std::vector<std::unique_ptr<Texture>> m_textures;
		mutable size_t m_drawn = 0;
	};
}
