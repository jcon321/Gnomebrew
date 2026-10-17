#include "inflate.h"

namespace
{
	constexpr int MaxBits = 15;

	struct BitReader
	{
		const uint8_t *data;
		size_t size;
		size_t pos = 0;
		uint32_t bits = 0;
		int count = 0;
		bool overrun = false;

		uint32_t Get(int n)
		{
			while (count < n) {
				if (pos >= size) {
					overrun = true;
					return 0;
				}
				bits |= static_cast<uint32_t>(data[pos++]) << count;
				count += 8;
			}
			uint32_t v = bits & ((1u << n) - 1);
			bits >>= n;
			count -= n;
			return v;
		}

		void AlignToByte()
		{
			bits = 0;
			count = 0;
		}
	};

	// Canonical Huffman table: symbol counts per length plus symbols sorted by code.
	struct Huffman
	{
		uint16_t counts[MaxBits + 1] = {};
		uint16_t symbols[288] = {};

		bool Build(const uint8_t *lengths, int n)
		{
			for (int i = 0; i < n; ++i)
				counts[lengths[i]]++;
			counts[0] = 0;

			int left = 1;
			for (int len = 1; len <= MaxBits; ++len) {
				left = (left << 1) - counts[len];
				if (left < 0)
					return false; // over-subscribed
			}

			uint16_t offsets[MaxBits + 1] = {};
			for (int len = 1; len < MaxBits; ++len)
				offsets[len + 1] = offsets[len] + counts[len];
			for (int i = 0; i < n; ++i)
				if (lengths[i])
					symbols[offsets[lengths[i]]++] = static_cast<uint16_t>(i);
			return true;
		}

		int Decode(BitReader &br) const
		{
			int code = 0, first = 0, index = 0;
			for (int len = 1; len <= MaxBits; ++len) {
				code |= static_cast<int>(br.Get(1));
				if (br.overrun)
					return -1;
				int count = counts[len];
				if (code - count < first)
					return symbols[index + (code - first)];
				index += count;
				first = (first + count) << 1;
				code <<= 1;
			}
			return -1;
		}
	};

	const uint16_t LengthBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
		35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
	const uint8_t LengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
		3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
	const uint16_t DistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
		257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
	const uint8_t DistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
		7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

	bool Codes(BitReader &br, std::vector<uint8_t> &out, size_t start, const Huffman &lit, const Huffman &dist)
	{
		for (;;) {
			int sym = lit.Decode(br);
			if (sym < 0)
				return false;
			if (sym < 256) {
				out.push_back(static_cast<uint8_t>(sym));
				continue;
			}
			if (sym == 256)
				return true;

			sym -= 257;
			if (sym >= 29)
				return false;
			size_t length = LengthBase[sym] + br.Get(LengthExtra[sym]);

			int dsym = dist.Decode(br);
			if (dsym < 0 || dsym >= 30)
				return false;
			size_t distance = DistBase[dsym] + br.Get(DistExtra[dsym]);
			if (br.overrun || distance > out.size() - start)
				return false;

			size_t from = out.size() - distance;
			for (size_t i = 0; i < length; ++i)
				out.push_back(out[from + i]);
		}
	}

	bool Stored(BitReader &br, std::vector<uint8_t> &out)
	{
		br.AlignToByte();
		if (br.pos + 4 > br.size)
			return false;
		uint16_t len = static_cast<uint16_t>(br.data[br.pos] | (br.data[br.pos + 1] << 8));
		uint16_t nlen = static_cast<uint16_t>(br.data[br.pos + 2] | (br.data[br.pos + 3] << 8));
		br.pos += 4;
		if (len != static_cast<uint16_t>(~nlen) || br.pos + len > br.size)
			return false;
		out.insert(out.end(), br.data + br.pos, br.data + br.pos + len);
		br.pos += len;
		return true;
	}

	bool Fixed(BitReader &br, std::vector<uint8_t> &out, size_t start)
	{
		static Huffman lit, dist;
		static bool built = false;
		if (!built) {
			uint8_t lengths[288];
			int i = 0;
			for (; i < 144; ++i) lengths[i] = 8;
			for (; i < 256; ++i) lengths[i] = 9;
			for (; i < 280; ++i) lengths[i] = 7;
			for (; i < 288; ++i) lengths[i] = 8;
			lit.Build(lengths, 288);
			for (i = 0; i < 30; ++i) lengths[i] = 5;
			dist.Build(lengths, 30);
			built = true;
		}
		return Codes(br, out, start, lit, dist);
	}

	bool Dynamic(BitReader &br, std::vector<uint8_t> &out, size_t start)
	{
		static const uint8_t Order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

		int nlen = static_cast<int>(br.Get(5)) + 257;
		int ndist = static_cast<int>(br.Get(5)) + 1;
		int ncode = static_cast<int>(br.Get(4)) + 4;
		if (br.overrun || nlen > 286 || ndist > 30)
			return false;

		uint8_t lengths[320] = {};
		for (int i = 0; i < ncode; ++i)
			lengths[Order[i]] = static_cast<uint8_t>(br.Get(3));
		Huffman code_table;
		if (!code_table.Build(lengths, 19))
			return false;

		int index = 0;
		while (index < nlen + ndist) {
			int sym = code_table.Decode(br);
			if (sym < 0)
				return false;
			if (sym < 16) {
				lengths[index++] = static_cast<uint8_t>(sym);
				continue;
			}
			uint8_t value = 0;
			int repeat;
			if (sym == 16) {
				if (index == 0)
					return false;
				value = lengths[index - 1];
				repeat = 3 + static_cast<int>(br.Get(2));
			}
			else if (sym == 17)
				repeat = 3 + static_cast<int>(br.Get(3));
			else
				repeat = 11 + static_cast<int>(br.Get(7));
			if (index + repeat > nlen + ndist)
				return false;
			while (repeat--)
				lengths[index++] = value;
		}
		if (lengths[256] == 0)
			return false;

		Huffman lit, dist;
		if (!lit.Build(lengths, nlen) || !dist.Build(lengths + nlen, ndist))
			return false;
		return Codes(br, out, start, lit, dist);
	}
}

bool Inflate::Zlib(const uint8_t *in, size_t in_size, std::vector<uint8_t> &out)
{
	if (in_size < 2)
		return false;
	uint8_t cmf = in[0], flg = in[1];
	if ((cmf & 0x0f) != 8 || ((cmf << 8) | flg) % 31 != 0 || (flg & 0x20))
		return false;

	BitReader br{in + 2, in_size - 2};
	size_t start = out.size();
	for (;;) {
		uint32_t last = br.Get(1);
		uint32_t type = br.Get(2);
		if (br.overrun)
			return false;

		bool ok = false;
		if (type == 0)
			ok = Stored(br, out);
		else if (type == 1)
			ok = Fixed(br, out, start);
		else if (type == 2)
			ok = Dynamic(br, out, start);
		if (!ok)
			return false;
		if (last)
			return true; // adler32 trailer ignored; PFS also records the expected size
	}
}
