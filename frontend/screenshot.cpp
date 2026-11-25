#include "screenshot.h"

#include <cstddef>
#include <cstdio>
#include <vector>

std::string Screenshot::NextPath(const std::string &dir)
{
	for (int number = 0;; ++number) {
		char name[32];
		std::snprintf(name, sizeof name, "EQ%06d.bmp", number);
		std::string path = dir + "/" + name;
		FILE *file = std::fopen(path.c_str(), "rb");
		if (!file)
			return path;
		std::fclose(file);
	}
}

bool Screenshot::WriteBmp(const std::string &path, const uint32_t *pixels, int width, int height, int stride)
{
	if (!pixels || width <= 0 || height <= 0)
		return false;
	constexpr size_t HeaderSize = 0x36;
	size_t row = (static_cast<size_t>(width) * 3 + 3) & ~static_cast<size_t>(3);
	size_t size = (row * height + HeaderSize + 3) & ~static_cast<size_t>(3);
	std::vector<uint8_t> out(size, 0);
	auto put = [&](size_t at, size_t value, int bytes) {
		for (int i = 0; i < bytes; ++i)
			out[at + i] = static_cast<uint8_t>(value >> (8 * i));
	};
	put(0, 0x4d42, 2);       // BITMAPFILEHEADER: "BM"
	put(2, size, 4);         // bfSize
	put(10, HeaderSize, 4);  // bfOffBits
	put(14, 0x28, 4);        // BITMAPINFOHEADER
	put(18, static_cast<size_t>(width), 4);
	put(22, static_cast<size_t>(height), 4);
	put(26, 1, 2);           // biPlanes
	put(28, 24, 2);          // biBitCount
	put(38, 0xb12, 4);       // biXPelsPerMeter
	put(42, 0xb12, 4);       // biYPelsPerMeter
	for (int y = 0; y < height; ++y) {
		const uint32_t *from = pixels + static_cast<size_t>(stride) * (height - 1 - y);
		uint8_t *to = out.data() + HeaderSize + row * y;
		for (int x = 0; x < width; ++x) {
			*to++ = static_cast<uint8_t>(from[x]);
			*to++ = static_cast<uint8_t>(from[x] >> 8);
			*to++ = static_cast<uint8_t>(from[x] >> 16);
		}
	}
	FILE *file = std::fopen(path.c_str(), "wb");
	if (!file)
		return false;
	bool written = std::fwrite(out.data(), 1, out.size(), file) == out.size();
	return std::fclose(file) == 0 && written;
}
