#pragma once

// RIFF WAVE, PCM only: what the sound packs (snd1..14.pfs) hold. Every one of their 2000-odd
// waves is mono 8- or 16-bit PCM at 8000 to 44100 Hz.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace audio
{
	struct Clip
	{
		int rate = 22050;
		int channels = 1;
		std::vector<int16_t> samples; // interleaved
		size_t Frames() const { return channels > 0 ? samples.size() / static_cast<size_t>(channels) : 0; }
	};

	bool DecodeWav(const uint8_t *data, size_t size, Clip &out);
}
