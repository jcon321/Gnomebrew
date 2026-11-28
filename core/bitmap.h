#pragma once

// Windows BMP decoding (uncompressed 1/4/8/16/24/32 bpp) into 0xAARRGGBB pixels (alpha 0xff).

#include <cstdint>
#include <vector>

struct Image
{
	int width = 0;
	int height = 0;
	std::vector<uint32_t> pixels; // row-major, top row first, 0xAARRGGBB
	bool has_alpha = false;       // true when any pixel is not fully opaque
	uint8_t dxt = 0;              // decoded from DXT1, 3 or 5 (DecodeDds); 0 otherwise

	uint32_t At(int x, int y) const { return pixels[static_cast<size_t>(y) * width + x]; }
};

// palette_key: for 8-bit images, palette index 0 becomes transparent (alpha 0), which is
// how the 3D engine masks textures.
bool DecodeBmp(const std::vector<uint8_t> &file, Image &image, bool palette_key = false);

// An uncompressed 8-bit BMP's palette indices, its rows as they are in the file.
bool ReadBmpIndices(const std::vector<uint8_t> &file, int &width, int &height, std::vector<uint8_t> &indices);
