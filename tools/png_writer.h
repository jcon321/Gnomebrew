#pragma once

// Minimal PNG writer for the Linux preview tools: stored (uncompressed) deflate
// blocks, so no zlib is needed. Pixels are 0x00RRGGBB.

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace png_writer_detail
{
	inline uint32_t Crc32(const uint8_t *data, size_t size, uint32_t crc = 0)
	{
		crc = ~crc;
		for (size_t i = 0; i < size; ++i) {
			crc ^= data[i];
			for (int k = 0; k < 8; ++k)
				crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
		}
		return ~crc;
	}

	inline void Put32(std::vector<uint8_t> &out, uint32_t v)
	{
		for (int shift = 24; shift >= 0; shift -= 8)
			out.push_back(static_cast<uint8_t>(v >> shift));
	}

	inline bool WritePng(const std::string &path, const std::vector<uint32_t> &pixels, int w, int h)
	{
		std::vector<uint8_t> raw;
		for (int y = 0; y < h; ++y) {
			raw.push_back(0);
			for (int x = 0; x < w; ++x) {
				uint32_t p = pixels[static_cast<size_t>(y) * w + x];
				raw.push_back(static_cast<uint8_t>(p >> 16));
				raw.push_back(static_cast<uint8_t>(p >> 8));
				raw.push_back(static_cast<uint8_t>(p));
			}
		}

		std::vector<uint8_t> z = {0x78, 0x01};
		uint32_t a = 1, b = 0;
		for (size_t pos = 0; pos < raw.size();) {
			size_t n = std::min<size_t>(65535, raw.size() - pos);
			z.push_back(pos + n == raw.size() ? 1 : 0);
			z.push_back(static_cast<uint8_t>(n));
			z.push_back(static_cast<uint8_t>(n >> 8));
			z.push_back(static_cast<uint8_t>(~n));
			z.push_back(static_cast<uint8_t>(~n >> 8));
			z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
			for (size_t i = pos; i < pos + n; ++i) {
				a = (a + raw[i]) % 65521;
				b = (b + a) % 65521;
			}
			pos += n;
		}
		Put32(z, (b << 16) | a);

		std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
		auto chunk = [&png](const char *type, const std::vector<uint8_t> &data) {
			Put32(png, static_cast<uint32_t>(data.size()));
			size_t start = png.size();
			png.insert(png.end(), type, type + 4);
			png.insert(png.end(), data.begin(), data.end());
			Put32(png, Crc32(&png[start], png.size() - start));
		};
		std::vector<uint8_t> ihdr;
		Put32(ihdr, static_cast<uint32_t>(w));
		Put32(ihdr, static_cast<uint32_t>(h));
		ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
		chunk("IHDR", ihdr);
		chunk("IDAT", z);
		chunk("IEND", {});

		std::ofstream file(path, std::ios::binary);
		file.write(reinterpret_cast<const char *>(png.data()), static_cast<std::streamsize>(png.size()));
		return static_cast<bool>(file);
	}
}

using png_writer_detail::WritePng;
