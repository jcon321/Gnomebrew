#include "wav.h"

#include <cstring>

namespace audio
{
	namespace
	{
		uint32_t U32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }
		uint16_t U16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
	}

	bool DecodeWav(const uint8_t *data, size_t size, Clip &out)
	{
		if (size < 12 || memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WAVE", 4) != 0)
			return false;
		int format = 0, bits = 0;
		bool have_format = false;
		size_t pos = 12;
		while (pos + 8 <= size) {
			const uint8_t *chunk = data + pos;
			size_t length = U32(chunk + 4);
			const uint8_t *body = chunk + 8;
			size_t available = size - pos - 8;
			if (memcmp(chunk, "fmt ", 4) == 0 && length >= 16 && available >= 16) {
				format = U16(body);
				out.channels = U16(body + 2);
				out.rate = static_cast<int>(U32(body + 4));
				bits = U16(body + 14);
				have_format = true;
			}
			else if (memcmp(chunk, "data", 4) == 0 && have_format) {
				if (format != 1 || (bits != 8 && bits != 16) || out.channels < 1 || out.channels > 2 || out.rate <= 0)
					return false;
				size_t bytes = length < available ? length : available; // a few are cut short
				size_t count = bytes / (bits / 8);
				out.samples.resize(count);
				for (size_t i = 0; i < count; ++i)
					out.samples[i] = bits == 8 ? static_cast<int16_t>((body[i] - 128) << 8) : static_cast<int16_t>(U16(body + i * 2));
				return true;
			}
			pos += 8 + length + (length & 1);
		}
		return false;
	}
}
