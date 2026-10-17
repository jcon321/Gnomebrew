#include "bitmap.h"

#include <cstdlib>

namespace
{
	uint32_t U32(const std::vector<uint8_t> &d, size_t o)
	{
		return static_cast<uint32_t>(d[o]) | (static_cast<uint32_t>(d[o + 1]) << 8) | (static_cast<uint32_t>(d[o + 2]) << 16) |
		       (static_cast<uint32_t>(d[o + 3]) << 24);
	}

	uint16_t U16(const std::vector<uint8_t> &d, size_t o)
	{
		return static_cast<uint16_t>(d[o] | (d[o + 1] << 8));
	}
}

bool DecodeBmp(const std::vector<uint8_t> &d, Image &image, bool palette_key)
{
	if (d.size() < 54 || d[0] != 'B' || d[1] != 'M')
		return false;

	uint32_t data_offset = U32(d, 10);
	uint32_t header_size = U32(d, 14);
	int32_t width = static_cast<int32_t>(U32(d, 18));
	int32_t height = static_cast<int32_t>(U32(d, 22));
	uint16_t bpp = U16(d, 28);
	uint32_t compression = U32(d, 30);
	if (width <= 0 || height == 0 || width > 8192 || std::abs(height) > 8192)
		return false;
	if (compression != 0 && !(compression == 3 && (bpp == 16 || bpp == 32)))
		return false; // BI_RGB, or BI_BITFIELDS treated as the default masks

	bool bottom_up = height > 0;
	int h = std::abs(height);
	size_t stride = ((static_cast<size_t>(width) * bpp + 31) / 32) * 4;
	if (data_offset + stride * h > d.size())
		return false;

	std::vector<uint32_t> palette;
	if (bpp <= 8) {
		uint32_t colors = U32(d, 46);
		if (colors == 0)
			colors = 1u << bpp;
		size_t pal = 14 + header_size;
		for (uint32_t i = 0; i < colors && pal + i * 4 + 3 < d.size(); ++i)
			palette.push_back((d[pal + i * 4 + 2] << 16) | (d[pal + i * 4 + 1] << 8) | d[pal + i * 4]);
		palette.resize(1u << bpp, 0);
	}

	image.width = width;
	image.height = h;
	image.has_alpha = false;
	image.pixels.assign(static_cast<size_t>(width) * h, 0);
	for (int y = 0; y < h; ++y) {
		const uint8_t *row = &d[data_offset + stride * (bottom_up ? h - 1 - y : y)];
		uint32_t *dst = &image.pixels[static_cast<size_t>(y) * width];
		for (int x = 0; x < width; ++x) {
			switch (bpp) {
			case 1:
				dst[x] = palette[(row[x / 8] >> (7 - x % 8)) & 1];
				break;
			case 4:
				dst[x] = palette[(x & 1) ? (row[x / 2] & 0x0f) : (row[x / 2] >> 4)];
				break;
			case 8:
				dst[x] = palette[row[x]];
				if (palette_key && row[x] == 0) {
					dst[x] &= 0xffffff; // alpha stays 0
					image.has_alpha = true;
					continue;
				}
				break;
			case 16: {
				uint16_t v = static_cast<uint16_t>(row[x * 2] | (row[x * 2 + 1] << 8)); // X1R5G5B5
				dst[x] = (((v >> 10) & 31) << 19) | (((v >> 5) & 31) << 11) | ((v & 31) << 3);
				break;
			}
			case 24:
				dst[x] = (row[x * 3 + 2] << 16) | (row[x * 3 + 1] << 8) | row[x * 3];
				break;
			case 32:
				dst[x] = (row[x * 4 + 2] << 16) | (row[x * 4 + 1] << 8) | row[x * 4];
				break;
			default:
				return false;
			}
			dst[x] |= 0xff000000;
		}
	}
	return true;
}
