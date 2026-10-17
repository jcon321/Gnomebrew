#include "dds.h"

#include <cstring>

namespace
{
	uint32_t U32(const std::vector<uint8_t> &d, size_t o)
	{
		return static_cast<uint32_t>(d[o]) | (static_cast<uint32_t>(d[o + 1]) << 8) | (static_cast<uint32_t>(d[o + 2]) << 16) |
		       (static_cast<uint32_t>(d[o + 3]) << 24);
	}

	uint32_t Expand565(uint16_t c)
	{
		uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
		return ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
	}

	uint32_t Mix(uint32_t a, uint32_t b, int wa, int wb, int div)
	{
		uint32_t out = 0;
		for (int shift = 0; shift < 24; shift += 8) {
			uint32_t ca = (a >> shift) & 0xff, cb = (b >> shift) & 0xff;
			out |= ((ca * wa + cb * wb) / div) << shift;
		}
		return out;
	}

	// One 4x4 color block. DXT1 with c0 <= c1 has a transparent fourth color.
	void ColorBlock(const uint8_t *block, uint32_t out[16], bool dxt1)
	{
		uint16_t c0 = static_cast<uint16_t>(block[0] | block[1] << 8);
		uint16_t c1 = static_cast<uint16_t>(block[2] | block[3] << 8);
		uint32_t colors[4];
		colors[0] = Expand565(c0) | 0xff000000;
		colors[1] = Expand565(c1) | 0xff000000;
		if (c0 > c1 || !dxt1) {
			colors[2] = Mix(colors[0], colors[1], 2, 1, 3) | 0xff000000;
			colors[3] = Mix(colors[0], colors[1], 1, 2, 3) | 0xff000000;
		}
		else {
			colors[2] = Mix(colors[0], colors[1], 1, 1, 2) | 0xff000000;
			colors[3] = 0;
		}
		uint32_t bits = block[4] | block[5] << 8 | block[6] << 16 | static_cast<uint32_t>(block[7]) << 24;
		for (int i = 0; i < 16; ++i)
			out[i] = colors[(bits >> (i * 2)) & 3];
	}

	void AlphaDxt3(const uint8_t *block, uint32_t out[16])
	{
		for (int i = 0; i < 16; ++i) {
			uint32_t a = (block[i / 2] >> ((i & 1) * 4)) & 15;
			out[i] = (out[i] & 0xffffff) | ((a * 17) << 24);
		}
	}

	void AlphaDxt5(const uint8_t *block, uint32_t out[16])
	{
		uint32_t a[8];
		a[0] = block[0];
		a[1] = block[1];
		if (a[0] > a[1]) {
			for (int i = 1; i < 7; ++i)
				a[i + 1] = ((7 - i) * a[0] + i * a[1]) / 7;
		}
		else {
			for (int i = 1; i < 5; ++i)
				a[i + 1] = ((5 - i) * a[0] + i * a[1]) / 5;
			a[6] = 0;
			a[7] = 255;
		}
		uint64_t bits = 0;
		for (int i = 0; i < 6; ++i)
			bits |= static_cast<uint64_t>(block[2 + i]) << (8 * i);
		for (int i = 0; i < 16; ++i)
			out[i] = (out[i] & 0xffffff) | (a[(bits >> (3 * i)) & 7] << 24);
	}

	int MaskShift(uint32_t mask)
	{
		int shift = 0;
		while (mask && !(mask & 1)) {
			mask >>= 1;
			++shift;
		}
		return shift;
	}

	uint32_t Channel(uint32_t pixel, uint32_t mask)
	{
		if (!mask)
			return 0;
		uint32_t value = (pixel & mask) >> MaskShift(mask);
		uint32_t max = mask >> MaskShift(mask);
		return value * 255 / max;
	}
}

bool DecodeDds(const std::vector<uint8_t> &d, Image &image)
{
	if (d.size() < 128 || std::memcmp(d.data(), "DDS ", 4) != 0)
		return false;
	uint32_t height = U32(d, 12);
	uint32_t width = U32(d, 16);
	uint32_t pf_flags = U32(d, 80);
	uint32_t fourcc = U32(d, 84);
	if (width == 0 || height == 0 || width > 8192 || height > 8192)
		return false;

	image.width = static_cast<int>(width);
	image.height = static_cast<int>(height);
	image.pixels.assign(static_cast<size_t>(width) * height, 0);
	image.has_alpha = false;
	const uint8_t *data = d.data() + 128;
	size_t available = d.size() - 128;

	if (pf_flags & 0x4) { // DDPF_FOURCC
		int kind;
		if (fourcc == 0x31545844) // "DXT1"
			kind = 1;
		else if (fourcc == 0x33545844 || fourcc == 0x32545844) // DXT3 (and DXT2)
			kind = 3;
		else if (fourcc == 0x35545844 || fourcc == 0x34545844) // DXT5 (and DXT4)
			kind = 5;
		else
			return false;
		size_t block_size = kind == 1 ? 8 : 16;
		uint32_t bw = (width + 3) / 4, bh = (height + 3) / 4;
		if (static_cast<size_t>(bw) * bh * block_size > available)
			return false;
		for (uint32_t by = 0; by < bh; ++by) {
			for (uint32_t bx = 0; bx < bw; ++bx) {
				const uint8_t *block = data + (static_cast<size_t>(by) * bw + bx) * block_size;
				uint32_t texels[16];
				if (kind == 1)
					ColorBlock(block, texels, true);
				else {
					ColorBlock(block + 8, texels, false);
					if (kind == 3)
						AlphaDxt3(block, texels);
					else
						AlphaDxt5(block, texels);
				}
				for (int i = 0; i < 16; ++i) {
					uint32_t x = bx * 4 + i % 4, y = by * 4 + i / 4;
					if (x < width && y < height) {
						image.pixels[static_cast<size_t>(y) * width + x] = texels[i];
						if ((texels[i] >> 24) != 0xff)
							image.has_alpha = true;
					}
				}
			}
		}
		return true;
	}

	if (!(pf_flags & 0x40)) // DDPF_RGB
		return false;
	uint32_t bits = U32(d, 88);
	uint32_t rmask = U32(d, 92), gmask = U32(d, 96), bmask = U32(d, 100), amask = (pf_flags & 0x1) ? U32(d, 104) : 0;
	if (bits != 16 && bits != 24 && bits != 32)
		return false;
	size_t bytes = bits / 8;
	if (static_cast<size_t>(width) * height * bytes > available)
		return false;
	for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
		uint32_t pixel = 0;
		for (size_t k = 0; k < bytes; ++k)
			pixel |= static_cast<uint32_t>(data[i * bytes + k]) << (8 * k);
		uint32_t a = amask ? Channel(pixel, amask) : 255;
		image.pixels[i] = (a << 24) | (Channel(pixel, rmask) << 16) | (Channel(pixel, gmask) << 8) | Channel(pixel, bmask);
		if (a != 255)
			image.has_alpha = true;
	}
	return true;
}
