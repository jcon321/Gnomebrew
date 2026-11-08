#pragma once

// The digital mix: what Miles' samples, 3D samples, MP3s and sequence handle did for the client.
// The game thread starts voices and changes them; the device thread pulls the mix (Render).
//
// A voice's volume moves in straight lines, as Miles' were moved every frame
// (updateSampleVolume): Fade sets where it's going and how long it takes from where it is, and a
// voice faded to 0 ends there. Fading again to the same volume while it's on its way (the client
// asks every frame) leaves it alone. Positional voices are "Miles Fast 2D Positional Audio", the
// provider the client picks (SoundManager::InitializeSoundSystem): full volume within the minimum
// distance, falling off as min / distance beyond it, silent past the maximum, and panned by where
// they are relative to the listener's facing. A positional voice's effects level sends a share of
// it to Miles' software reverb (reverb.h): AIL_set_sample_reverb_levels with msssoft's wet level, the
// effects level (FUN_22401a50), times the room's wet level and the sample's volume as mss32 has it
// from its left and right levels (FUN_211146a0: r = right / left, p = r^3.333 / (1 + r^3.333),
// p^-0.3 * right; left alone when p is under 0.0001, right when left is), at most 1 (FUN_21116190's
// 2048).

#include "reverb.h"
#include "synth.h"
#include "wav.h"
#include "xmi.h"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace audio
{
	struct Vec3
	{
		float x = 0, y = 0, z = 0;
	};

	// Where a voice plays from: anywhere (plain), or a place in the world (positional).
	struct Placement
	{
		bool positional = false;
		Vec3 position;
		float min_distance = 1, max_distance = 100;
		float effects = 0; // AIL_set_3D_sample_effects_level: its share sent to the reverb, 0..1
	};

	class Mixer
	{
	public:
		static constexpr int Rate = 44100;
		using Voice = uint32_t; // 0: none
		using Placement = audio::Placement;

		Mixer();
		~Mixer();

		// loops: 0 plays forever, else that many times. The voice starts silent and fades to
		// `volume` over `fade_ms`.
		Voice PlayClip(std::shared_ptr<const Clip> clip, int loops, float volume, int fade_ms, const Placement &placement = {});
		Voice PlayMp3(std::shared_ptr<const std::vector<uint8_t>> mp3, int loops, float volume, int fade_ms);
		// One sequence plays at a time, on the synth (SetSynth); a new one ends the last.
		Voice PlaySequence(std::shared_ptr<const XmiSequence> sequence, int loops, float volume, int fade_ms);
		void Fade(Voice voice, float volume, int ms);
		void Move(Voice voice, const Vec3 &position);
		void Stop(Voice voice);
		bool Active(Voice voice) const;
		size_t ActiveVoices() const;

		// Where we hear from, and the way we face (level).
		void SetListener(const Vec3 &position, const Vec3 &forward);
		void SetSynth(std::shared_ptr<MidiSynth> synth);
		// AIL_set_3D_room_type (msssoft's M3D_set_3D_room_type: AIL_set_digital_master_room_type).
		void SetRoomType(int type);
		int RoomType() const;
		bool SynthRenders() const;

		// The device thread: `frames` stereo frames, interleaved, written over `out`.
		void Render(float *out, int frames);
		// For a synth that doesn't render: plays the music on `seconds` (its host's timer calls this).
		void AdvanceSequences(double seconds);

	private:
		struct VoiceState;
		struct Mp3Stream;

		void RenderVoice(VoiceState &voice, float *out, int frames);
		void RunSequence(VoiceState &voice, double seconds);
		bool StepVolume(VoiceState &voice, int samples); // false once faded out
		void PositionalGains(const VoiceState &voice, float &left, float &right) const;

		mutable std::mutex m_mutex;
		std::map<Voice, std::unique_ptr<VoiceState>> m_voices;
		Voice m_next = 1;
		Vec3 m_listener, m_forward{1, 0, 0};
		std::shared_ptr<MidiSynth> m_synth;
		std::vector<float> m_scratch;
		Reverb m_reverb{Rate};
		std::vector<float> m_wet; // the voices' sends to the reverb, a block's
	};
}
