#pragma once

// Miles' Extended MIDI (.xmi), the zones' music before Luclin and gl.xmi's merchant, bank and
// guild tunes. FORM XDIR (INFO: the sequence count) then CAT XMID, one FORM XMID per sequence:
// TIMB (timbres), RBRN (branch points) and EVNT. EVNT is MIDI without running status: a delay
// is a run of bytes under 0x80 that add up, at Miles' fixed 120 ticks a second (the tempo events
// are left over from the conversion and don't change it), and a note-on carries its duration (a
// MIDI variable-length number) instead of a note-off following.
//
// XmiPlayer plays one as Miles' sequence handle does (AIL_init_sequence, _set_sequence_loop_count,
// _set_sequence_volume, _start_sequence), sending channel messages to a synth: midiOutShortMsg's
// packing, status | data1 << 8 | data2 << 16.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace audio
{
	constexpr int XmiTicksPerSecond = 120;

	struct XmiEvent
	{
		uint32_t tick;
		uint32_t message;      // a channel message
		uint32_t duration = 0; // a note-on's, in ticks
	};

	struct XmiSequence
	{
		std::vector<XmiEvent> events;
		uint32_t end_tick = 0; // the end of track
	};

	bool ParseXmi(const uint8_t *data, size_t size, std::vector<XmiSequence> &out);
	// As a type 0 standard MIDI file (60 ticks a quarter at 500000 us a quarter: 120 a second), for tools.
	std::vector<uint8_t> ToStandardMidi(const XmiSequence &sequence);

	class XmiPlayer
	{
	public:
		using Send = std::function<void(uint32_t message)>;

		// loops: 0 plays forever, else that many times.
		void Start(std::shared_ptr<const XmiSequence> sequence, int loops, float volume, const Send &send);
		// Plays `seconds` more. False once the sequence has ended (its notes are off).
		bool Advance(double seconds, const Send &send);
		// AIL_set_sequence_volume: scales every channel's volume controller (7) by `volume`, 0..1.
		void SetVolume(float volume, const Send &send);
		// AIL_end_sequence: everything off now.
		void Stop(const Send &send);
		bool Playing() const { return m_sequence != nullptr; }

	private:
		struct PendingOff
		{
			uint64_t tick; // m_elapsed's
			uint8_t channel, key;
		};
		struct ForLoop
		{
			size_t index;   // the event after the FOR
			uint32_t tick;  // its tick
			int remaining;  // 0: forever
		};

		void Dispatch(const XmiEvent &event, const Send &send);
		void SendVolume(int channel, const Send &send);
		void NotesOff(const Send &send);

		std::shared_ptr<const XmiSequence> m_sequence;
		size_t m_index = 0;
		uint32_t m_position = 0; // ticks into the sequence
		uint64_t m_elapsed = 0; // ticks played, for the note durations
		double m_fraction = 0;
		int m_loops_left = 0;
		bool m_forever = false;
		float m_volume = 1;
		uint8_t m_channel_volume[16] = {};
		uint8_t m_sent_volume[16] = {};
		std::vector<PendingOff> m_offs;
		std::vector<ForLoop> m_for;
	};
}
