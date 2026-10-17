#pragma once

// Textures for the 3D view, decoded from .s3d archives (BMP or DDS) and turned
// into power-of-two mip chains, plus the mapping from WLD render methods to
// rasterizer materials.

#include "pfs.h"
#include "rasterizer.h"
#include "wld.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace render
{
	class TextureCache
	{
	public:
		// Archives are searched in the order added; the cache doesn't own them.
		void AddArchive(const PfsArchive *archive) { m_archives.push_back(archive); }

		// masked: 8-bit BMPs treat palette index 0 as transparent.
		const Texture *Get(const std::string &name, bool masked);
		bool Exists(const std::string &name) const;
		const std::vector<std::string> &Missing() const { return m_missing; }

	private:
		std::vector<const PfsArchive *> m_archives;
		std::map<std::string, std::unique_ptr<Texture>> m_cache; // lowercase name + mask flag
		std::vector<std::string> m_missing;
	};

	struct MaterialInfo
	{
		bool visible = true;
		bool masked = false;
		Blend blend = Blend::Opaque;
		uint8_t opacity = 255;
	};

	MaterialInfo DescribeRenderMethod(uint32_t render_method);
}
