#include "synth.h"

#define TSF_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wnull-pointer-subtraction"
#endif
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "../third_party/tinysoundfont/tsf.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace audio
{
	namespace
	{
		class SoundFontSynth : public MidiSynth
		{
		public:
			explicit SoundFontSynth(tsf *font) : m_font(font) {}
			~SoundFontSynth() override { tsf_close(m_font); }

			void Send(uint32_t message) override
			{
				int status = message & 0xff, channel = status & 0x0f;
				int data1 = (message >> 8) & 0x7f, data2 = (message >> 16) & 0x7f;
				switch (status & 0xf0) {
				case 0x80: tsf_channel_note_off(m_font, channel, data1); break;
				case 0x90:
					if (data2 == 0)
						tsf_channel_note_off(m_font, channel, data1);
					else
						tsf_channel_note_on(m_font, channel, data1, data2 / 127.0f);
					break;
				case 0xb0: tsf_channel_midi_control(m_font, channel, data1, data2); break;
				case 0xc0: tsf_channel_set_presetnumber(m_font, channel, data1, channel == 9); break;
				case 0xe0: tsf_channel_set_pitchwheel(m_font, channel, data1 | (data2 << 7)); break;
				default: break;
				}
			}
			bool Renders() const override { return true; }
			void Render(float *out, int frames) override { tsf_render_float(m_font, out, frames, 1); }

		private:
			tsf *m_font;
		};
	}

	std::unique_ptr<MidiSynth> MakeSoundFontSynth(const std::string &path, int sample_rate, std::string &error)
	{
		tsf *font = tsf_load_filename(path.c_str());
		if (!font) {
			error = "couldn't load the SoundFont " + path;
			return nullptr;
		}
		tsf_set_output(font, TSF_STEREO_INTERLEAVED, sample_rate, 0);
		tsf_set_max_voices(font, 64);
		for (int channel = 0; channel < 16; ++channel)
			tsf_channel_set_presetnumber(font, channel, 0, channel == 9); // General MIDI: channel 10 is the drums
		return std::make_unique<SoundFontSynth>(font);
	}
}
