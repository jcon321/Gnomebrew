#pragma once

// Textures for the 3D view, decoded from .s3d archives (BMP or DDS) and turned
// into power-of-two mip chains, plus the mapping from WLD render methods to
// rasterizer materials.

#include "bitmap.h"
#include "pfs.h"
#include "renderer.h"
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
		// The alpha a masked texture's texel needs to be drawn (the rasterizer's is 128). eqgfx's alpha
		// test is 192, but 16 for the textures of a file whose name has "_chr" or "equip" in it
		// (t3dLoadWorldFile's flag 0x80, flushSpecialEffects): the Luclin models' DXT5 skirts and
		// robes are mostly alpha 96..127. Their masked textures are loaded with alpha 0 or 255 by it.
		void SetMaskReference(uint8_t alpha) { m_mask_reference = alpha; }

		// masked: 8-bit BMPs treat palette index 0 as transparent.
		const Texture *Get(const std::string &name, bool masked);
		// A Luclin material's two textures as one (WldMaterial::layer): `layer` over `name` by the
		// layer's alpha, as eqgfx draws the skin and then the layer blended over it with the same
		// vertex light (setRenderEffectState's mode 2). Just `name` when the layer is missing.
		const Texture *GetLayered(const std::string &name, const std::string &layer, bool masked);
		bool Exists(const std::string &name) const;
		const std::vector<std::string> &Missing() const { return m_missing; }

	private:
		bool Load(const std::string &name, bool masked, Image &image) const; // as drawn: v = 0 the bottom row
		std::vector<const PfsArchive *> m_archives;
		std::map<std::string, std::unique_ptr<Texture>> m_cache; // lowercase name + mask flag
		std::vector<std::string> m_missing;
		uint8_t m_mask_reference = 0x80;
	};

	// A texture of a decoded picture (rows top first, as v = 0); `soft` as Build's.
	std::unique_ptr<Texture> MakeTexture(const Image &image, bool soft = false);

	struct MaterialInfo
	{
		bool visible = true;
		bool masked = false;
		Blend blend = Blend::Opaque;
		uint8_t opacity = 255;
		bool unlit = false; // full bright: no vertex light, no ambient (fire, the sky)
	};

	MaterialInfo DescribeRenderMethod(uint32_t render_method);
}
