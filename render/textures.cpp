#include "textures.h"

#include "bitmap.h"
#include "dds.h"

#include <algorithm>

namespace render
{
	namespace
	{
		int CeilShift(int size)
		{
			int shift = 0;
			while ((1 << shift) < size && shift < 13)
				++shift;
			return shift;
		}

		// Nearest-neighbour resample to a power of two, then box-filtered mips down to 1x1.
		std::unique_ptr<Texture> Build(const Image &image)
		{
			auto texture = std::make_unique<Texture>();
			texture->width_shift = CeilShift(image.width);
			texture->height_shift = CeilShift(image.height);
			texture->width = 1 << texture->width_shift;
			texture->height = 1 << texture->height_shift;
			texture->has_alpha = image.has_alpha;

			std::vector<uint32_t> level(static_cast<size_t>(texture->width) * texture->height);
			for (int y = 0; y < texture->height; ++y) {
				int sy = y * image.height / texture->height;
				for (int x = 0; x < texture->width; ++x)
					level[static_cast<size_t>(y) * texture->width + x] = image.At(x * image.width / texture->width, sy);
			}
			texture->levels.push_back(std::move(level));

			int w = texture->width, h = texture->height;
			while (w > 1 || h > 1) {
				int nw = std::max(1, w / 2), nh = std::max(1, h / 2);
				const auto &src = texture->levels.back();
				std::vector<uint32_t> next(static_cast<size_t>(nw) * nh);
				for (int y = 0; y < nh; ++y) {
					for (int x = 0; x < nw; ++x) {
						uint32_t sum[4] = {0, 0, 0, 0};
						int samples = 0, opaque = 0;
						for (int dy = 0; dy < (h > 1 ? 2 : 1); ++dy) {
							for (int dx = 0; dx < (w > 1 ? 2 : 1); ++dx) {
								uint32_t p = src[static_cast<size_t>(y * (h > 1 ? 2 : 1) + dy) * w + x * (w > 1 ? 2 : 1) + dx];
								++samples;
								sum[3] += p >> 24;
								if ((p >> 24) >= 0x80) {
									// Average color over the opaque texels so masked edges don't darken.
									++opaque;
									sum[0] += (p >> 16) & 0xff;
									sum[1] += (p >> 8) & 0xff;
									sum[2] += p & 0xff;
								}
							}
						}
						uint32_t a = sum[3] / samples;
						uint32_t r = opaque ? sum[0] / opaque : 0, g = opaque ? sum[1] / opaque : 0, b = opaque ? sum[2] / opaque : 0;
						next[static_cast<size_t>(y) * nw + x] = (a << 24) | (r << 16) | (g << 8) | b;
					}
				}
				texture->levels.push_back(std::move(next));
				w = nw;
				h = nh;
			}
			return texture;
		}
	}

	const Texture *TextureCache::Get(const std::string &name, bool masked)
	{
		std::string key = ToLower(name) + (masked ? "|m" : "");
		auto it = m_cache.find(key);
		if (it != m_cache.end())
			return it->second.get();

		std::unique_ptr<Texture> texture;
		std::vector<uint8_t> file;
		for (const PfsArchive *archive : m_archives) {
			if (!archive->Read(name, file))
				continue;
			Image image;
			bool dds = file.size() >= 4 && file[0] == 'D' && file[1] == 'D' && file[2] == 'S';
			bool ok = dds ? DecodeDds(file, image) : DecodeBmp(file, image, masked);
			if (ok && image.width > 0 && image.height > 0) {
				// The engine uses BMP rows in file order (bottom-up), so v = 0 is the bottom of
				// the picture; DDS rows are already stored that way round.
				if (!dds)
					for (int y = 0; y < image.height / 2; ++y)
						std::swap_ranges(image.pixels.begin() + static_cast<size_t>(y) * image.width,
							image.pixels.begin() + static_cast<size_t>(y + 1) * image.width,
							image.pixels.begin() + static_cast<size_t>(image.height - 1 - y) * image.width);
				texture = Build(image);
			}
			break;
		}
		if (!texture)
			m_missing.push_back(name);
		const Texture *result = texture.get();
		m_cache.emplace(key, std::move(texture));
		return result;
	}

	bool TextureCache::Exists(const std::string &name) const
	{
		for (const PfsArchive *archive : m_archives)
			if (archive->Contains(name))
				return true;
		return false;
	}

	// User-defined render methods (bit 31) are the engine's fixed material table;
	// the rest are t3d bit fields, which the files only use for solid textured surfaces.
	MaterialInfo DescribeRenderMethod(uint32_t method)
	{
		MaterialInfo info;
		if (method == 0) {
			info.visible = false; // collision boundaries
			return info;
		}
		if (!(method & 0x80000000)) {
			if ((method & 3) == 0 || method == 0x53)
				info.visible = false;
			return info;
		}
		switch (method & 0x7fffffff) {
		case 0x00:
		case 0x03:
		case 0x4b:
			info.visible = false;
			break;
		case 0x05: // 50% transparent
			info.blend = Blend::Translucent;
			info.opacity = 128;
			break;
		case 0x09: // 25%
			info.blend = Blend::Translucent;
			info.opacity = 64;
			break;
		case 0x0a: // 75%
			info.blend = Blend::Translucent;
			info.opacity = 192;
			break;
		case 0x07: // masked, passable (foliage)
		case 0x13: // masked
			info.blend = Blend::Masked;
			info.masked = true;
			break;
		case 0x0b: // additive, unlit (fire)
		case 0x17: // additive, unlit (sky)
			info.blend = Blend::Additive;
			info.masked = true;
			info.unlit = true;
			break;
		case 0x10: // additive
			info.blend = Blend::Additive;
			info.masked = true;
			break;
		default: // 0x01, 0x02, 0x12, 0x14, 0x15, 0x19, 0x31, 0x553 ... diffuse
			break;
		}
		return info;
	}
}
