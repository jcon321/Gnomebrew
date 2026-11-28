#pragma once

// DirectDraw Surface textures (the Luclin-era zones use them in place of BMPs):
// DXT1/DXT3/DXT5 and uncompressed 16/24/32-bit, top mip level only.

#include "bitmap.h"

#include <cstdint>
#include <vector>

bool DecodeDds(const std::vector<uint8_t> &file, Image &image);
// One 4x4 block of DXT1, 3 or 5 (`kind`) as 0xAARRGGBB texels, row by row.
void DecodeDxtBlock(int kind, const uint8_t *block, uint32_t texels[16]);
