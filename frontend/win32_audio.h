#pragma once

// Windows sound output for the Mixer: waveOut, 16-bit stereo at Mixer::Rate, kept a few
// buffers ahead by its own thread. And the MIDI synth the real client's music went to: Miles'
// XMIDI driver on the MIDI mapper (normally the Microsoft GS Wavetable Synth), with a timer thread
// to play the sequence on, since that synth makes its own sound.

#include "mixer.h"
#include "synth.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>

class WaveOutDevice
{
public:
	~WaveOutDevice();
	bool Start(audio::Mixer &mixer, std::string &error);
	void Stop();

private:
	void Run();
	void Sequences();

	audio::Mixer *m_mixer = nullptr;
	void *m_device = nullptr; // HWAVEOUT
	void *m_event = nullptr;  // HANDLE
	std::thread m_thread, m_sequence_thread;
	std::atomic<bool> m_running{false};
};

// midiOutOpen(MIDI_MAPPER). Null, with `error`, when there's no MIDI out.
std::unique_ptr<audio::MidiSynth> MakeWindowsMidiSynth(std::string &error);
