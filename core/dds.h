#pragma once

// DirectDraw Surface textures (the Luclin-era zones use them in place of BMPs):
// DXT1/DXT3/DXT5 and uncompressed 16/24/32-bit, top mip level only.

#include "bitmap.h"

#include <vector>

bool DecodeDds(const std::vector<uint8_t> &file, Image &image);
