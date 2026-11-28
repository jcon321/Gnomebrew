#pragma once

// BC1 and BC3 (DXT1 and DXT5) block compression, for keeping the textures that came as DXT the
// same on the GPU: eqgfx hands a DDS to Direct3D as it is and has D3DX make its mips, ours makes
// the mips (render's Build) and compresses each level here. One 4x4 block at a time, along the
// colors' principal axis (BC1's four-color mode; BC3's alpha in its eight-value mode).

#include <cstdint>
#include <vector>

namespace block_compress
{
	enum class Format : uint8_t { BC1 = 1, BC3 = 3 };

	constexpr int BlockBytes(Format format) { return format == Format::BC1 ? 8 : 16; }

	// A width x height level of 0xAARRGGBB texels, row by row, in blocks row by row: (width + 3) / 4
	// across and (height + 3) / 4 down, the texels past an edge copies of the last ones (a mip
	// smaller than a block). BC1 is opaque: its alpha is dropped.
	std::vector<uint8_t> Compress(const uint32_t *texels, int width, int height, Format format);
}
