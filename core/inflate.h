#pragma once

// Minimal zlib (RFC 1950/1951) decompressor for PFS archive blocks.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Inflate
{
	// Decompresses one zlib stream and appends the output. Returns false on corrupt input.
	bool Zlib(const uint8_t *in, size_t in_size, std::vector<uint8_t> &out);
}
