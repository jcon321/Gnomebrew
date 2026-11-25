#include "grass.h"

#include "dds.h"
#include "pfs.h"
#include "textures.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>

namespace render
{
	namespace
	{
		constexpr float Radius = 200; // _DAT_007cf270, t3dSetRadialFloraRadius
		constexpr int Most = 3000;
		constexpr int NearestFade = 20;

		// eqgfx's PTR_s_LWEED1_DDS_100f5f90: a record's texture, by number.
		constexpr const char *Names[] = {
			"lweed1", "lweed2", "lgrass1", "lgrass2", "lgrass3", "lgrass4", "lflowr1", "lflowr2", "lcorn1", "lcorn2",
			"lgrass1", "lgrass2", "lgrass3", "lgrass4", "lplant1", "lplant2", "rplant1", "rplant2", "rplant3", "rplant4",
			"tallplt1", "tallplt2", "bramble1", "bramble2", "bramble3", "bramble4", "bramble5", "bramble6", "lplant1", "lplant2",
			"rplant1", "rplant2", "rplant3", "rplant4", "birch1", "birch2", "birch3", "lgrass1", "lgrass2", "lgrass3",
			"lgrass4", "rplant1", "rplant2", "rplant3", "rplant4", "pinkflower", "pinkflower1", "pinkflower2", "pinkflower3", "tallplt1",
			"tallplt2", "lplant1", "lplant2", "rplant1", "rplant2", "rplant3", "rplant4", "vine2", "lflowr1", "lflowr2",
			"birch1", "birch2", "birch3", "bramble1", "bramble2", "bramble3", "bramble4", "bramble5", "bramble6", "rplant1",
			"rplant2", "rplant3", "rplant4", "birch1", "birch2", "birch3", "tallplt1", "tallplt2", "thgrass1", "thgrass2",
			"thgrass3", "thgrass4", "bramble1", "bramble2", "bramble3", "bramble4", "bramble5", "bramble6", "vine2", "lcorn1",
			"lcorn2", "thgrass1", "thgrass2", "thgrass3", "thgrass4", "lgrass1", "lgrass2", "lgrass3", "lgrass4", "rplant1",
			"rplant2", "rplant3", "rplant4", "birch1", "birch2", "birch3", "thgrass1", "thgrass2", "thgrass3", "thgrass4",
			"bramble1", "bramble2", "bramble3", "bramble4", "bramble5", "bramble6", "lplant1", "lplant2", "birch1", "birch2",
			"birch3", "tallplt1", "tallplt2", "lweed2",
		};
		static_assert(std::size(Names) == 124);
	}

	bool Grass::Load(const std::string &eq_dir, const std::string &zone)
	{
		m_blades.clear();
		m_textures.clear();
		std::ifstream in(eq_dir + "/" + zone + ".dat", std::ios::binary);
		if (!in)
			return false;
		std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		PfsArchive archive;
		if (!archive.Open(eq_dir + "/grass.s3d", true))
			return false;
		std::map<std::string, const Texture *> textures;
		auto texture = [&](const std::string &name) {
			auto [it, added] = textures.emplace(name, nullptr);
			std::vector<uint8_t> dds;
			Image image;
			if (added && archive.Read(name + ".dds", dds) && DecodeDds(dds, image) && image.width > 0 && image.height > 0) {
				m_textures.push_back(MakeTexture(image, true));
				it->second = m_textures.back().get();
			}
			return it->second;
		};
		for (size_t at = 0; at + 32 <= file.size(); at += 32) {
			float f[7];
			int32_t number;
			std::memcpy(f, file.data() + at, sizeof(f));
			std::memcpy(&number, file.data() + at + 28, 4);
			// t3dSetRadialFloraObjectList: a number out of the table has no texture, and isn't drawn.
			const Texture *t = number >= 0 && number < static_cast<int32_t>(std::size(Names)) ? texture(Names[number]) : nullptr;
			m_blades.push_back({{f[0], f[1], f[2]}, {f[3], f[4], f[5]}, f[6], t});
		}
		return !m_blades.empty();
	}

	void Grass::Draw(Renderer &renderer, const Vec3 &camera, const Vec3 &ambient) const
	{
		m_drawn = 0;
		auto level = [](float light) { return static_cast<uint32_t>(std::clamp(static_cast<int>((light + 0.15f) * 255), 0, 255)); };
		const uint32_t color = level(ambient.x) << 16 | level(ambient.y) << 8 | level(ambient.z);
		static const uint32_t Quad[6] = {0, 1, 2, 1, 2, 3};
		const float reach = Radius * Radius;
		int counted = 0;
		for (size_t i = 0; i < m_blades.size(); ++i) {
			const Blade &blade = m_blades[i];
			if (!blade.texture)
				continue;
			float dx = blade.a.x - camera.x, dy = blade.a.y - camera.y;
			float d2 = dx * dx + dy * dy;
			if (d2 < reach) {
				int fade = static_cast<int>(d2 / (reach / 255));
				// Behind the camera it's counted all the same (eqgfx leaves that to the clipping).
				Vec3 to = blade.a - camera;
				bool behind = Dot(to, renderer.Forward()) < -20;
				if (fade < 255 && !behind) {
					fade = std::max(fade, NearestFade);
					Vertex v[4];
					v[0].position = blade.a;
					v[1].position = blade.b;
					v[2].position = blade.a + Vec3{0, 0, blade.height};
					v[3].position = blade.b + Vec3{0, 0, blade.height};
					// V is clamped, not wrapped (D3DTADDRESS_CLAMP): half a texel in from each end.
					float edge = 0.5f / blade.texture->height;
					v[0].u = v[2].u = 0;
					v[1].u = v[3].u = 1;
					v[0].v = v[1].v = 1 - edge;
					v[2].v = v[3].v = edge;
					for (Vertex &vertex : v)
						vertex.color = color;
					Material material;
					material.texture = blade.texture;
					material.blend = Blend::Translucent;
					material.texel_alpha = true;
					material.opacity = static_cast<uint8_t>(255 - fade);
					material.two_sided = true;
					renderer.Draw(v, 4, Quad, 6, material);
					++m_drawn;
				}
				++counted;
			}
			// The records go down the zone by y, in several passes: far from it, eqgfx skips 64.
			if (dy * dy > reach)
				i += 64;
			if (counted >= Most)
				return;
		}
	}
}
