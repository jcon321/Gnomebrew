#pragma once

// Miles 6.5b's software reverb (mss32.dll), which "Miles Fast 2D Positional Audio" (msssoft.m3d,
// the 3D provider the client picks) uses for its room types and effects levels.
//
// AIL_set_digital_master_room_type (@21115210) picks a preset from the table at 0x2114d070 (the
// EAX environments' order): the master wet level (AIL_set_digital_master_reverb_levels, dry 1) and
// the reverb time, predelay and damping (AIL_set_digital_master_reverb, FUN_21114fd0). Each
// sample's share goes in mono, delayed by the predelay (the ring at driver+0x33c, read
// predelay * rate behind where it's written), then FUN_21114d60 runs it a frame at a time:
//   - a 2nd order Butterworth low pass (FUN_211150c0: c = 1 / tan(damping * pi / 8), b0 =
//     1 / (1 + sqrt2 c + c^2), a1 = 2 (1 - c^2) b0, a2 = (1 - sqrt2 c + c^2) b0), so its cutoff is
//     damping * rate / 8 at Miles' rate: 22050 with eqclient.ini's Sound44k=0, as TAKP has it;
//   - four comb filters 29.7, 37.1, 41.1 and 43.7 ms long, each fed back by 0.001^(length / reverb
//     time), summed;
//   - two allpasses, 5 ms (gain 0.001^(0.005 / 0.0968535)) and 1.7 ms (0.001^(0.0017 / 0.032924)),
//     the second taking a quarter of the first's output;
//   - out on the left; the right takes the second allpass again with 0.15 in place of 0.25 (not
//     kept in its line).
// Miles stops running it 100 ms past the reverb time and predelay after the last sample sent to it;
// it runs here all the time, its tail long gone by then.

#include <vector>

namespace audio
{
	class Reverb
	{
	public:
		explicit Reverb(int rate);

		// AIL_set_digital_master_room_type: 0 generic .. 25 psychotic (out of range: 0).
		void SetRoomType(int type);
		int RoomType() const { return m_room; }
		float Wet() const { return m_wet; } // the master wet level (driver+0x104)
		bool On() const { return m_wet > 0.0001f && m_time > 0.0001f; }

		// Adds the reverb of `in` (mono: what the samples send it, `frames` long) to `out` (stereo,
		// interleaved).
		void Process(const float *in, float *out, int frames);

	private:
		struct Line
		{
			std::vector<float> buffer;
			size_t at = 0;
		};

		int m_rate;
		int m_room = -1;
		float m_wet = 0, m_time = 0, m_damping = 0, m_predelay = 0;
		Line m_delay; // the predelay
		size_t m_delay_frames = 0;
		Line m_combs[4], m_allpasses[2];
		float m_comb_gains[4] = {}, m_allpass_gains[2] = {};
		float m_b0 = 0, m_a1 = 0, m_a2 = 0;
		float m_x1 = 0, m_x2 = 0, m_y1 = 0, m_y2 = 0; // the low pass's (x scaled by b0, as Miles keeps them)
	};
}
