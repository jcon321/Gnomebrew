#pragma once

// Truevision TGA decoding: types 2 (uncompressed) and 10 (RLE), 24/32 bpp.
// That covers every .tga in the EQ uifiles skins.

#include "bitmap.h"

#include <cstdint>
#include <vector>

bool DecodeTga(const std::vector<uint8_t> &file, Image &image);
