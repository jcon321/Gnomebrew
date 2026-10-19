#pragma once

// Where XMI music goes. The real client opened Miles' XMIDI driver on the Windows MIDI mapper
// (AIL_open_XMIDI_driver(0)), so its music came out of whatever MIDI synth Windows had: usually the
// Microsoft GS Wavetable Synth. win32_audio.cpp's synth does the same. The SoundFont synth plays
// it through one of the General MIDI banks EverQuest ships (synthusr.sf2, synthus2.sf2) instead,
// into our own mix: on any machine, and for the Linux tools.

#include <cstdint>
#include <memory>
#include <string>

namespace audio
{
	class MidiSynth
	{
	public:
		virtual ~MidiSynth() = default;
		// A channel message: status | data1 << 8 | data2 << 16.
		virtual void Send(uint32_t message) = 0;
		// Synths that make their own sound (MIDI out) leave the mix alone; the mixer then needs its
		// host to run the music on a timer (Mixer::AdvanceSequences).
		virtual bool Renders() const { return false; }
		// Adds `frames` stereo frames to `out`.
		virtual void Render(float *out, int frames) { (void)out; (void)frames; }
	};

	std::unique_ptr<MidiSynth> MakeSoundFontSynth(const std::string &path, int sample_rate, std::string &error);
}
