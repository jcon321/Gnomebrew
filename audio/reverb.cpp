#include "reverb.h"

#include <algorithm>
#include <cmath>

namespace audio
{
	namespace
	{
		// The table at mss32.dll 0x2114d070 (16 bytes a room): the master wet level, the reverb
		// time, the damping and the predelay, in seconds where they're times.
		struct Room
		{
			float wet, time, damping, predelay;
		};
		constexpr Room Rooms[] = {
			{0.000f, 1.493f, 0.500f, 0.011f},  // 0 generic
			{0.250f, 0.100f, 0.000f, 0.002f},  // 1 padded cell
			{0.417f, 0.400f, 0.666f, 0.003f},  // 2 room
			{0.750f, 1.499f, 0.166f, 0.011f},  // 3 bathroom
			{0.208f, 0.478f, 0.000f, 0.004f},  // 4 living room
			{0.300f, 2.309f, 0.680f, 0.017f},  // 5 stone room
			{0.403f, 4.279f, 0.500f, 0.030f},  // 6 auditorium
			{0.500f, 3.961f, 0.500f, 0.029f},  // 7 concert hall
			{0.500f, 2.886f, 1.304f, 0.022f},  // 8 cave
			{0.361f, 7.284f, 0.332f, 0.030f},  // 9 arena
			{0.500f, 10.000f, 0.300f, 0.030f}, // 10 hangar
			{0.153f, 0.259f, 0.550f, 0.030f},  // 11 carpeted hallway
			{0.361f, 1.493f, 0.500f, 0.011f},  // 12 hallway
			{0.444f, 2.697f, 0.638f, 0.020f},  // 13 stone corridor
			{0.250f, 1.752f, 0.776f, 0.011f},  // 14 alley
			{0.111f, 3.145f, 0.472f, 0.088f},  // 15 forest
			{0.111f, 2.767f, 0.224f, 0.011f},  // 16 city
			{0.200f, 0.800f, 0.200f, 0.300f},  // 17 mountains
			{0.550f, 3.000f, 0.550f, 0.043f},  // 18 quarry
			{0.120f, 0.800f, 0.180f, 0.100f},  // 19 plain
			{0.208f, 1.652f, 1.500f, 0.012f},  // 20 parking lot
			{0.652f, 2.886f, 0.250f, 0.021f},  // 21 sewer pipe
			{1.000f, 1.499f, 0.100f, 0.011f},  // 22 underwater
			{0.875f, 8.392f, 1.388f, 0.011f},  // 23 drugged
			{0.139f, 10.000f, 1.500f, 0.011f}, // 24 dizzy
			{0.486f, 7.563f, 4.000f, 0.011f},  // 25 psychotic
		};
		constexpr int RoomCount = static_cast<int>(sizeof(Rooms) / sizeof(Rooms[0]));
		constexpr float CombSeconds[4] = {0.0297f, 0.0371f, 0.0411f, 0.0437f};
		constexpr float AllpassSeconds[2] = {0.005f, 0.0017f};
		constexpr float AllpassDecay[2] = {0.09685350f, 0.032924f};
		constexpr float MaxPredelay = 0.3f; // the table's longest (mountains)
		constexpr float MilesRate = 22050;   // the digital driver's (Sound44k=0)
		constexpr float Denormal = 1e-10f / 32768; // Miles' 1e-10, on its 16 bit scale
	}

	Reverb::Reverb(int rate) : m_rate(rate)
	{
		auto frames = [rate](float seconds) { return static_cast<size_t>(std::lround(rate * seconds)); };
		for (int i = 0; i < 4; ++i)
			m_combs[i].buffer.assign(std::max<size_t>(frames(CombSeconds[i]), 1), 0.0f);
		for (int i = 0; i < 2; ++i) {
			m_allpasses[i].buffer.assign(std::max<size_t>(frames(AllpassSeconds[i]), 1), 0.0f);
			m_allpass_gains[i] = static_cast<float>(std::pow(0.001, AllpassSeconds[i] / AllpassDecay[i]));
		}
		m_delay.buffer.assign(frames(MaxPredelay) + 1, 0.0f);
		SetRoomType(0);
	}

	void Reverb::SetRoomType(int type)
	{
		if (type < 0 || type >= RoomCount)
			type = 0;
		if (type == m_room)
			return;
		m_room = type;
		const Room &room = Rooms[type];
		m_wet = room.wet;
		m_time = room.time;
		m_damping = room.damping;
		m_predelay = room.predelay;
		m_delay_frames = std::min(static_cast<size_t>(std::lround(m_rate * m_predelay)), m_delay.buffer.size() - 1);
		// FUN_211150c0: the combs' feedback for the reverb time, and the low pass.
		for (int i = 0; i < 4; ++i)
			m_comb_gains[i] = m_time > 0.0001f ? static_cast<float>(std::pow(0.001, CombSeconds[i] / m_time)) : 0.0f;
		double angle = m_damping * (3.14159265358979 / 8) * (MilesRate / m_rate); // the same cutoff in Hz
		if (m_damping <= 0) {
			m_b0 = m_a1 = m_a2 = 0; // Miles' 1 / tan(0): nothing through
		}
		else {
			double c = angle < 3.14159265358979 / 2 ? 1 / std::tan(angle) : 0;
			double b0 = 1 / ((std::sqrt(2.0) + c) * c + 1);
			m_b0 = static_cast<float>(b0);
			m_a1 = static_cast<float>(2 * (1 - c * c) * b0);
			m_a2 = static_cast<float>((1 - std::sqrt(2.0) * c + c * c) * b0);
		}
	}

	void Reverb::Process(const float *in, float *out, int frames)
	{
		size_t delay_size = m_delay.buffer.size();
		for (int i = 0; i < frames; ++i) {
			// The predelay.
			m_delay.buffer[m_delay.at] = in[i];
			float x = m_delay.buffer[(m_delay.at + delay_size - m_delay_frames) % delay_size];
			m_delay.at = (m_delay.at + 1) % delay_size;

			// The low pass.
			float y = m_x1 + m_x1 + m_x2 + x * m_b0 - m_y1 * m_a1 - m_y2 * m_a2;
			m_x2 = m_x1;
			m_x1 = x * m_b0;
			m_y2 = m_y1;
			m_y1 = y + Denormal;

			// The combs.
			float sum = 0;
			for (int c = 0; c < 4; ++c) {
				Line &line = m_combs[c];
				float old = line.buffer[line.at];
				line.buffer[line.at] = m_comb_gains[c] * old + y;
				line.at = (line.at + 1) % line.buffer.size();
				sum += old;
			}

			// The allpasses.
			Line &first = m_allpasses[0];
			float a = first.buffer[first.at];
			float w = m_allpass_gains[0] * a + sum;
			first.buffer[first.at] = w;
			first.at = (first.at + 1) % first.buffer.size();
			float through = a - m_allpass_gains[0] * w;

			Line &second = m_allpasses[1];
			float b = second.buffer[second.at];
			float g = m_allpass_gains[1];
			float w2 = through * 0.25f + g * b;
			second.buffer[second.at] = w2;
			second.at = (second.at + 1) % second.buffer.size();

			out[i * 2] += b - w2 * g;
			out[i * 2 + 1] += b - (through * 0.15f + g * b) * g;
		}
	}
}
