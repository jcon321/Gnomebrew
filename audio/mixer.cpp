#include "mixer.h"

#define MINIMP3_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "../third_party/minimp3/minimp3.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <cmath>
#include <iterator>

namespace audio
{
	namespace
	{
		constexpr int SequenceStep = 64; // frames the music moves on at a time, inside a block
	}

	// An MP3 decoded a frame at a time as it plays, into stereo.
	struct Mixer::Mp3Stream
	{
		std::shared_ptr<const std::vector<uint8_t>> data;
		mp3dec_t decoder;
		size_t offset = 0;
		int rate = 0;
		std::vector<float> frames; // stereo, decoded and not yet played past
		bool ended = false;
		bool decoded = false; // anything since the start (a file with nothing to decode doesn't loop)

		// Decodes until there's at least `needed` frames buffered, or the data runs out.
		void Fill(size_t needed, int &loops_left, bool forever)
		{
			mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
			while (frames.size() / 2 < needed && !ended) {
				if (offset >= data->size()) {
					if (decoded && (forever || --loops_left > 0)) {
						offset = 0;
						mp3dec_init(&decoder);
						continue;
					}
					ended = true;
					break;
				}
				mp3dec_frame_info_t info{};
				int samples = mp3dec_decode_frame(&decoder, data->data() + offset, static_cast<int>(data->size() - offset), pcm, &info);
				if (info.frame_bytes <= 0) {
					offset = data->size(); // nothing more to decode
					continue;
				}
				offset += static_cast<size_t>(info.frame_bytes);
				if (samples <= 0)
					continue;
				rate = info.hz;
				decoded = true;
				for (int i = 0; i < samples; ++i) {
					float l = pcm[i * info.channels] / 32768.0f;
					float r = info.channels > 1 ? pcm[i * info.channels + 1] / 32768.0f : l;
					frames.push_back(l);
					frames.push_back(r);
				}
			}
		}
	};

	struct Mixer::VoiceState
	{
		enum class Kind { Clip, Mp3, Sequence } kind = Kind::Clip;
		std::shared_ptr<const Clip> clip;
		std::unique_ptr<Mp3Stream> mp3;
		XmiPlayer sequence;
		bool sequence_started = false;
		std::shared_ptr<const XmiSequence> sequence_data;

		int loops_left = 1;
		bool forever = false;
		double position = 0; // frames into the clip or the MP3's buffer
		bool finished = false;

		float volume = 0, target = 0, step = 0; // step: per output frame
		Placement placement;
		float left = -1, right = -1; // the positional gains the last block ended on
	};

	Mixer::Mixer() = default;
	Mixer::~Mixer() = default;

	Mixer::Voice Mixer::PlayClip(std::shared_ptr<const Clip> clip, int loops, float volume, int fade_ms, const Placement &placement)
	{
		if (!clip || clip->Frames() == 0)
			return 0;
		auto voice = std::make_unique<VoiceState>();
		voice->kind = VoiceState::Kind::Clip;
		voice->clip = std::move(clip);
		voice->forever = loops <= 0;
		voice->loops_left = loops;
		voice->placement = placement;
		voice->target = volume;
		if (fade_ms <= 0)
			voice->volume = volume;
		else
			voice->step = volume / (fade_ms * (Rate / 1000.0f));
		std::lock_guard<std::mutex> lock(m_mutex);
		Voice id = m_next++;
		m_voices[id] = std::move(voice);
		return id;
	}

	Mixer::Voice Mixer::PlayMp3(std::shared_ptr<const std::vector<uint8_t>> mp3, int loops, float volume, int fade_ms)
	{
		if (!mp3 || mp3->empty())
			return 0;
		auto voice = std::make_unique<VoiceState>();
		voice->kind = VoiceState::Kind::Mp3;
		voice->mp3 = std::make_unique<Mp3Stream>();
		voice->mp3->data = std::move(mp3);
		mp3dec_init(&voice->mp3->decoder);
		voice->forever = loops <= 0;
		voice->loops_left = loops;
		voice->target = volume;
		if (fade_ms <= 0)
			voice->volume = volume;
		else
			voice->step = volume / (fade_ms * (Rate / 1000.0f));
		std::lock_guard<std::mutex> lock(m_mutex);
		Voice id = m_next++;
		m_voices[id] = std::move(voice);
		return id;
	}

	Mixer::Voice Mixer::PlaySequence(std::shared_ptr<const XmiSequence> sequence, int loops, float volume, int fade_ms)
	{
		if (!sequence)
			return 0;
		std::lock_guard<std::mutex> lock(m_mutex);
		for (auto &entry : m_voices) {
			if (entry.second->kind == VoiceState::Kind::Sequence && !entry.second->finished) {
				if (m_synth)
					entry.second->sequence.Stop([this](uint32_t message) { m_synth->Send(message); });
				entry.second->finished = true;
			}
		}
		auto voice = std::make_unique<VoiceState>();
		voice->kind = VoiceState::Kind::Sequence;
		voice->sequence_data = std::move(sequence);
		voice->forever = loops <= 0;
		voice->loops_left = loops;
		voice->target = volume;
		if (fade_ms <= 0)
			voice->volume = volume;
		else
			voice->step = volume / (fade_ms * (Rate / 1000.0f));
		Voice id = m_next++;
		m_voices[id] = std::move(voice);
		return id;
	}

	void Mixer::Fade(Voice id, float volume, int ms)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = m_voices.find(id);
		if (it == m_voices.end())
			return;
		VoiceState &voice = *it->second;
		if (ms > 0 && volume == voice.target)
			return; // already on its way there
		voice.target = volume;
		if (ms <= 0) {
			voice.volume = volume;
			voice.step = 0;
		}
		else
			voice.step = std::fabs(volume - voice.volume) / (ms * (Rate / 1000.0f));
	}

	void Mixer::Move(Voice id, const Vec3 &position)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = m_voices.find(id);
		if (it != m_voices.end())
			it->second->placement.position = position;
	}

	void Mixer::Stop(Voice id)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = m_voices.find(id);
		if (it == m_voices.end())
			return;
		if (it->second->kind == VoiceState::Kind::Sequence && m_synth)
			it->second->sequence.Stop([this](uint32_t message) { m_synth->Send(message); });
		m_voices.erase(it);
	}

	bool Mixer::Active(Voice id) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		auto it = m_voices.find(id);
		return it != m_voices.end() && !it->second->finished;
	}

	size_t Mixer::ActiveVoices() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		size_t count = 0;
		for (const auto &entry : m_voices)
			count += entry.second->finished ? 0 : 1;
		return count;
	}

	void Mixer::SetListener(const Vec3 &position, const Vec3 &forward)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_listener = position;
		float length = std::sqrt(forward.x * forward.x + forward.y * forward.y);
		if (length > 0)
			m_forward = {forward.x / length, forward.y / length, 0};
	}

	void Mixer::SetSynth(std::shared_ptr<MidiSynth> synth)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_synth = std::move(synth);
	}

	void Mixer::SetRoomType(int type)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_reverb.SetRoomType(type);
	}

	int Mixer::RoomType() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_reverb.RoomType();
	}

	bool Mixer::SynthRenders() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_synth && m_synth->Renders();
	}

	bool Mixer::StepVolume(VoiceState &voice, int samples)
	{
		if (voice.volume != voice.target) {
			float change = voice.step * samples;
			if (voice.step <= 0 || std::fabs(voice.target - voice.volume) <= change)
				voice.volume = voice.target;
			else
				voice.volume += voice.target > voice.volume ? change : -change;
		}
		return !(voice.volume <= 0 && voice.target <= 0);
	}

	void Mixer::PositionalGains(const VoiceState &voice, float &left, float &right) const
	{
		const Placement &p = voice.placement;
		float dx = p.position.x - m_listener.x, dy = p.position.y - m_listener.y, dz = p.position.z - m_listener.z;
		float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
		if (distance > p.max_distance) {
			left = right = 0;
			return;
		}
		float gain = distance <= p.min_distance || distance <= 0 ? 1 : p.min_distance / distance;
		// The right is the way the renderer's is: forward x up.
		float pan = 0;
		if (distance > 0.001f) {
			float rx = m_forward.y, ry = -m_forward.x;
			pan = std::clamp((dx * rx + dy * ry) / distance, -1.0f, 1.0f);
		}
		// Equal power, raised so the middle is full volume on both sides.
		float angle = (pan + 1) * 0.25f * 3.14159265f;
		left = gain * std::min(1.0f, std::cos(angle) * 1.41421356f);
		right = gain * std::min(1.0f, std::sin(angle) * 1.41421356f);
	}

	namespace
	{
		// mss32's sample volume from its left and right levels (FUN_211146a0).
		float SampleVolume(float left, float right)
		{
			if (left <= 0.0001f)
				return right;
			float p = std::pow(right / left, 3.3333333f);
			p = p / (1 + p);
			return p < 0.0001f ? left : std::pow(p, -0.3f) * right;
		}
	}

	void Mixer::RenderVoice(VoiceState &voice, float *out, int frames)
	{
		float left = 1, right = 1;
		if (voice.placement.positional)
			PositionalGains(voice, left, right);
		float start_left = voice.left < 0 ? left : voice.left, start_right = voice.right < 0 ? right : voice.right;
		voice.left = left;
		voice.right = right;
		// The reverb's share (FUN_21116190: master wet * the sample's wet * its volume), its volume
		// from the gains the block starts and ends on.
		float send = voice.placement.positional && m_reverb.On() ? voice.placement.effects * m_reverb.Wet() : 0;
		float send_start = 0, send_end = 0;
		if (send > 0) {
			send_start = send * SampleVolume(start_left, start_right);
			send_end = send * SampleVolume(left, right);
		}

		for (int i = 0; i < frames && !voice.finished; ++i) {
			float a = 0, b = 0;
			if (voice.kind == VoiceState::Kind::Clip) {
				const Clip &clip = *voice.clip;
				size_t count = clip.Frames();
				size_t index = static_cast<size_t>(voice.position);
				float t = static_cast<float>(voice.position - index);
				size_t next = index + 1 < count ? index + 1 : (voice.forever || voice.loops_left > 1 ? 0 : index);
				int channels = clip.channels;
				a = (clip.samples[index * channels] * (1 - t) + clip.samples[next * channels] * t) / 32768.0f;
				b = channels > 1 ? (clip.samples[index * 2 + 1] * (1 - t) + clip.samples[next * 2 + 1] * t) / 32768.0f : a;
				voice.position += static_cast<double>(clip.rate) / Rate;
				if (voice.position >= count) {
					if (voice.forever || --voice.loops_left > 0)
						voice.position -= count;
					else
						voice.finished = true;
				}
			}
			else if (voice.kind == VoiceState::Kind::Mp3) {
				Mp3Stream &mp3 = *voice.mp3;
				size_t index = static_cast<size_t>(voice.position);
				mp3.Fill(index + 2, voice.loops_left, voice.forever);
				if (mp3.frames.size() / 2 <= index) {
					voice.finished = true;
					break;
				}
				float t = static_cast<float>(voice.position - index);
				bool have_next = mp3.frames.size() / 2 > index + 1;
				a = mp3.frames[index * 2] * (1 - t) + (have_next ? mp3.frames[index * 2 + 2] : 0) * t;
				b = mp3.frames[index * 2 + 1] * (1 - t) + (have_next ? mp3.frames[index * 2 + 3] : 0) * t;
				voice.position += mp3.rate > 0 ? static_cast<double>(mp3.rate) / Rate : 1;
				// Let go of what's been played, now and then.
				if (voice.position > 8192) {
					size_t drop = static_cast<size_t>(voice.position) - 1;
					mp3.frames.erase(mp3.frames.begin(), mp3.frames.begin() + static_cast<long>(drop * 2));
					voice.position -= static_cast<double>(drop);
				}
			}
			if (!StepVolume(voice, 1)) {
				voice.finished = true;
				break;
			}
			float f = frames > 1 ? static_cast<float>(i) / (frames - 1) : 1;
			float gl = (start_left + (left - start_left) * f) * voice.volume;
			float gr = (start_right + (right - start_right) * f) * voice.volume;
			out[i * 2] += a * gl;
			out[i * 2 + 1] += b * gr;
			if (send > 0)
				m_wet[i] += a * std::min(1.0f, (send_start + (send_end - send_start) * f) * voice.volume);
		}
	}

	void Mixer::RunSequence(VoiceState &voice, double seconds)
	{
		auto send = [this](uint32_t message) { m_synth->Send(message); };
		if (!voice.sequence_started) {
			voice.sequence_started = true;
			voice.sequence.Start(voice.sequence_data, voice.forever ? 0 : voice.loops_left, voice.volume, send);
		}
		if (!StepVolume(voice, static_cast<int>(seconds * Rate + 0.5))) {
			voice.sequence.Stop(send);
			voice.finished = true;
			return;
		}
		voice.sequence.SetVolume(voice.volume, send);
		if (!voice.sequence.Advance(seconds, send))
			voice.finished = true;
	}

	void Mixer::Render(float *out, int frames)
	{
		std::fill(out, out + static_cast<size_t>(frames) * 2, 0.0f);
		std::lock_guard<std::mutex> lock(m_mutex);
		m_wet.assign(static_cast<size_t>(frames), 0.0f);
		for (auto &entry : m_voices) {
			VoiceState &voice = *entry.second;
			if (!voice.finished && voice.kind != VoiceState::Kind::Sequence)
				RenderVoice(voice, out, frames);
		}
		// The reverb on what the voices sent it (FUN_21115f60's FUN_21114cb0, after the mix).
		if (m_reverb.On())
			m_reverb.Process(m_wet.data(), out, frames);
		// The music, a step at a time so its notes land close to where they should.
		if (m_synth && m_synth->Renders()) {
			m_scratch.assign(static_cast<size_t>(frames) * 2, 0.0f);
			for (int done = 0; done < frames;) {
				int chunk = std::min(SequenceStep, frames - done);
				for (auto &entry : m_voices) {
					if (entry.second->kind == VoiceState::Kind::Sequence && !entry.second->finished)
						RunSequence(*entry.second, static_cast<double>(chunk) / Rate);
				}
				m_synth->Render(m_scratch.data() + done * 2, chunk);
				done += chunk;
			}
			for (size_t i = 0; i < m_scratch.size(); ++i)
				out[i] += m_scratch[i];
		}
		for (int i = 0; i < frames * 2; ++i)
			out[i] = std::clamp(out[i], -1.0f, 1.0f);
		for (auto it = m_voices.begin(); it != m_voices.end();)
			it = it->second->finished ? m_voices.erase(it) : std::next(it);
	}

	void Mixer::AdvanceSequences(double seconds)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_synth || m_synth->Renders())
			return;
		for (auto &entry : m_voices) {
			if (entry.second->kind == VoiceState::Kind::Sequence && !entry.second->finished)
				RunSequence(*entry.second, seconds);
		}
	}
}
