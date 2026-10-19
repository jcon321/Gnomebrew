#include "win32_audio.h"

#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <chrono>
#include <vector>

namespace
{
	constexpr int BufferCount = 4;
	constexpr int BufferFrames = audio::Mixer::Rate / 60; // a sixtieth of a second each

	class WindowsMidiSynth : public audio::MidiSynth
	{
	public:
		explicit WindowsMidiSynth(HMIDIOUT device) : m_device(device) {}
		~WindowsMidiSynth() override
		{
			midiOutReset(m_device);
			midiOutClose(m_device);
		}
		void Send(uint32_t message) override { midiOutShortMsg(m_device, message); }

	private:
		HMIDIOUT m_device;
	};
}

std::unique_ptr<audio::MidiSynth> MakeWindowsMidiSynth(std::string &error)
{
	HMIDIOUT device = nullptr;
	MMRESULT result = midiOutOpen(&device, MIDI_MAPPER, 0, 0, CALLBACK_NULL);
	if (result != MMSYSERR_NOERROR) {
		error = "no MIDI out (midiOutOpen " + std::to_string(result) + ")";
		return nullptr;
	}
	return std::make_unique<WindowsMidiSynth>(device);
}

WaveOutDevice::~WaveOutDevice()
{
	Stop();
}

bool WaveOutDevice::Start(audio::Mixer &mixer, std::string &error)
{
	m_mixer = &mixer;
	WAVEFORMATEX format{};
	format.wFormatTag = WAVE_FORMAT_PCM;
	format.nChannels = 2;
	format.nSamplesPerSec = audio::Mixer::Rate;
	format.wBitsPerSample = 16;
	format.nBlockAlign = 4;
	format.nAvgBytesPerSec = audio::Mixer::Rate * 4;
	m_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	HWAVEOUT device = nullptr;
	MMRESULT result = waveOutOpen(&device, WAVE_MAPPER, &format, reinterpret_cast<DWORD_PTR>(m_event), 0, CALLBACK_EVENT);
	if (result != MMSYSERR_NOERROR) {
		error = "no sound device (waveOutOpen " + std::to_string(result) + ")";
		CloseHandle(m_event);
		m_event = nullptr;
		return false;
	}
	m_device = device;
	m_running = true;
	m_thread = std::thread([this] { Run(); });
	if (!mixer.SynthRenders())
		m_sequence_thread = std::thread([this] { Sequences(); });
	return true;
}

void WaveOutDevice::Stop()
{
	if (!m_running)
		return;
	m_running = false;
	SetEvent(m_event);
	if (m_thread.joinable())
		m_thread.join();
	if (m_sequence_thread.joinable())
		m_sequence_thread.join();
	waveOutClose(static_cast<HWAVEOUT>(m_device));
	CloseHandle(m_event);
	m_device = m_event = nullptr;
}

void WaveOutDevice::Run()
{
	HWAVEOUT device = static_cast<HWAVEOUT>(m_device);
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
	std::vector<std::vector<int16_t>> buffers(BufferCount, std::vector<int16_t>(BufferFrames * 2));
	std::vector<WAVEHDR> headers(BufferCount);
	std::vector<float> mix(BufferFrames * 2);
	for (int i = 0; i < BufferCount; ++i) {
		headers[i].lpData = reinterpret_cast<LPSTR>(buffers[i].data());
		headers[i].dwBufferLength = BufferFrames * 4;
		waveOutPrepareHeader(device, &headers[i], sizeof(WAVEHDR));
		headers[i].dwFlags |= WHDR_DONE; // free to fill
	}
	while (m_running) {
		for (int i = 0; i < BufferCount && m_running; ++i) {
			if (!(headers[i].dwFlags & WHDR_DONE))
				continue;
			m_mixer->Render(mix.data(), BufferFrames);
			for (int s = 0; s < BufferFrames * 2; ++s)
				buffers[i][s] = static_cast<int16_t>(std::clamp(mix[s], -1.0f, 1.0f) * 32767);
			headers[i].dwFlags &= ~WHDR_DONE;
			waveOutWrite(device, &headers[i], sizeof(WAVEHDR));
		}
		WaitForSingleObject(m_event, 100);
	}
	waveOutReset(device);
	for (auto &header : headers)
		waveOutUnprepareHeader(device, &header, sizeof(WAVEHDR));
}

// The music on a MIDI synth that makes its own sound: Miles serviced its sequences on a timer.
void WaveOutDevice::Sequences()
{
	timeBeginPeriod(1);
	auto last = std::chrono::steady_clock::now();
	while (m_running) {
		Sleep(2);
		auto now = std::chrono::steady_clock::now();
		m_mixer->AdvanceSequences(std::chrono::duration<double>(now - last).count());
		last = now;
	}
	timeEndPeriod(1);
}
