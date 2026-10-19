// sound_preview (Linux): runs the client's sound (EqSound) without a window or a sound card and
// writes what it would play to a WAV file, to check the music, the zone emitters and the mix.
//
//   sound_preview EQDIR OUT.wav [--zone NAME] [--at X Y Z] [--heading H] [--walk X Y Z] [--seconds S]
//                 [--music opening|charselect|combat|death|ID]... [--night] [--environment 1|2|3]
//                 [--soundfont FILE] [--music-volume V] [--sound-volume V]
//   sound_preview EQDIR --emitters ZONE
//   sound_preview EQDIR --xmi FILE INDEX OUT.mid
//
// The first form stands us at X Y Z in the zone (the server's position, /loc order Y X Z) facing
// heading H (0..512) for S seconds (default 10), at 30 frames a second, with the music selections
// given on, and writes the mix. --walk walks us from there to X Y Z over the time instead. The XMI
// music plays through a SoundFont (default synthus2.sf2 from EQDIR, else synthusr.sf2), as
// eqclient_gui's Music=sf2 does. Each second it prints the music track and the voices playing, and
// at the end the loudness of each second. --night and --environment set the emitters' time of day
// and ceiling (default day, outside). The second form lists a zone's .emt emitters; the third
// writes one of an .xmi's sequences out as a standard MIDI file.

#include "eq_sound.h"
#include "mixer.h"
#include "xmi.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace
{
	void WriteWav(const std::string &path, const std::vector<float> &stereo)
	{
		std::ofstream file(path, std::ios::binary);
		auto u32 = [&](uint32_t v) { file.write(reinterpret_cast<const char *>(&v), 4); };
		auto u16 = [&](uint16_t v) { file.write(reinterpret_cast<const char *>(&v), 2); };
		uint32_t bytes = static_cast<uint32_t>(stereo.size() * 2);
		file.write("RIFF", 4);
		u32(36 + bytes);
		file.write("WAVEfmt ", 8);
		u32(16);
		u16(1);
		u16(2);
		u32(audio::Mixer::Rate);
		u32(audio::Mixer::Rate * 4);
		u16(4);
		u16(16);
		file.write("data", 4);
		u32(bytes);
		for (float sample : stereo)
			u16(static_cast<uint16_t>(static_cast<int16_t>(std::lround(sample * 32767))));
	}

	bool ReadFile(const std::string &path, std::vector<uint8_t> &out)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
			return false;
		out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
		return true;
	}

	int ExportXmi(const std::string &path, int index, const std::string &out)
	{
		std::vector<uint8_t> data;
		std::vector<audio::XmiSequence> sequences;
		if (!ReadFile(path, data) || !audio::ParseXmi(data.data(), data.size(), sequences)) {
			fprintf(stderr, "couldn't read %s\n", path.c_str());
			return 1;
		}
		printf("%s: %zu sequences\n", path.c_str(), sequences.size());
		for (size_t i = 0; i < sequences.size(); ++i)
			printf("  #%zu: %zu events, %.1f s\n", i, sequences[i].events.size(),
				sequences[i].end_tick / static_cast<double>(audio::XmiTicksPerSecond));
		if (index < 0 || index >= static_cast<int>(sequences.size()))
			return 1;
		std::vector<uint8_t> midi = audio::ToStandardMidi(sequences[index]);
		std::ofstream(out, std::ios::binary).write(reinterpret_cast<const char *>(midi.data()), static_cast<long>(midi.size()));
		printf("wrote %s\n", out.c_str());
		return 0;
	}

	int ListEmitters(const std::string &eq_dir, const std::string &zone)
	{
		std::vector<uint8_t> data;
		if (!ReadFile(eq_dir + "/" + zone + ".emt", data)) {
			fprintf(stderr, "no %s.emt\n", zone.c_str());
			return 1;
		}
		printf("%s", std::string(data.begin(), data.end()).c_str());
		return 0;
	}
}

int main(int argc, char **argv)
{
	if (argc >= 3 && std::string(argv[2]) == "--emitters" && argc >= 4)
		return ListEmitters(argv[1], argv[3]);
	if (argc >= 3 && std::string(argv[2]) == "--xmi" && argc >= 6)
		return ExportXmi(argv[3], atoi(argv[4]), argv[5]);
	if (argc < 3) {
		fprintf(stderr, "usage: %s EQDIR OUT.wav [--zone NAME] [--at X Y Z] [--heading H] [--walk X Y Z] [--seconds S]\n"
		                "       [--music opening|charselect|combat|death|ID]... [--night] [--environment 1|2|3]\n"
		                "       [--soundfont FILE] [--music-volume V] [--sound-volume V]\n"
		                "       %s EQDIR --emitters ZONE\n       %s EQDIR --xmi FILE INDEX OUT.mid\n",
			argv[0], argv[0], argv[0]);
		return 2;
	}
	std::string eq_dir = argv[1], out = argv[2], zone, soundfont;
	float x = 0, y = 0, z = 0, heading = 0, seconds = 10;
	bool walk = false, night = false;
	float walk_x = 0, walk_y = 0, walk_z = 0;
	int environment = 1;
	std::vector<int> music;
	audio::EqSound::Settings settings;
	for (int i = 3; i < argc; ++i) {
		std::string arg = argv[i];
		if (arg == "--zone" && i + 1 < argc)
			zone = argv[++i];
		else if (arg == "--at" && i + 3 < argc) {
			x = static_cast<float>(atof(argv[++i]));
			y = static_cast<float>(atof(argv[++i]));
			z = static_cast<float>(atof(argv[++i]));
		}
		else if (arg == "--walk" && i + 3 < argc) {
			walk = true;
			walk_x = static_cast<float>(atof(argv[++i]));
			walk_y = static_cast<float>(atof(argv[++i]));
			walk_z = static_cast<float>(atof(argv[++i]));
		}
		else if (arg == "--heading" && i + 1 < argc)
			heading = static_cast<float>(atof(argv[++i]));
		else if (arg == "--seconds" && i + 1 < argc)
			seconds = static_cast<float>(atof(argv[++i]));
		else if (arg == "--music" && i + 1 < argc) {
			std::string name = argv[++i];
			music.push_back(name == "opening"      ? audio::EqSound::Opening
			                : name == "charselect" ? audio::EqSound::CharacterSelect
			                : name == "combat"     ? audio::EqSound::Combat
			                : name == "death"      ? audio::EqSound::Death
			                                       : atoi(name.c_str()));
		}
		else if (arg == "--night")
			night = true;
		else if (arg == "--environment" && i + 1 < argc)
			environment = atoi(argv[++i]);
		else if (arg == "--soundfont" && i + 1 < argc)
			soundfont = argv[++i];
		else if (arg == "--music-volume" && i + 1 < argc)
			settings.music_volume = static_cast<float>(atof(argv[++i]));
		else if (arg == "--sound-volume" && i + 1 < argc)
			settings.sound_volume = static_cast<float>(atof(argv[++i]));
		else {
			fprintf(stderr, "unknown option %s\n", arg.c_str());
			return 2;
		}
	}

	audio::Mixer mixer;
	std::string error;
	std::shared_ptr<audio::MidiSynth> synth;
	for (const std::string &name : soundfont.empty() ? std::vector<std::string>{eq_dir + "/synthus2.sf2", eq_dir + "/synthusr.sf2"}
	                                                 : std::vector<std::string>{soundfont}) {
		if ((synth = audio::MakeSoundFontSynth(name, audio::Mixer::Rate, error))) {
			printf("music through %s\n", name.c_str());
			break;
		}
	}
	if (!synth)
		printf("%s: no XMI music\n", error.c_str());
	mixer.SetSynth(synth);

	audio::EqSound sound(mixer, eq_dir, settings);
	if (!zone.empty())
		sound.SetCurrentZone(zone);
	for (int id : music)
		sound.SetMusicSelection(id, true);

	const int fps = 30, frames_per_tick = audio::Mixer::Rate / fps;
	int ticks = static_cast<int>(seconds * fps);
	std::vector<float> mix(static_cast<size_t>(ticks) * frames_per_tick * 2);
	uint64_t now = 1000;
	float yaw = heading * 2 * 3.14159265f / 512;
	for (int tick = 0; tick < ticks; ++tick) {
		float t = ticks > 1 ? static_cast<float>(tick) / (ticks - 1) : 0;
		float sx = walk ? x + (walk_x - x) * t : x, sy = walk ? y + (walk_y - y) * t : y, sz = walk ? z + (walk_z - z) * t : z;
		// World space is the server's (y, x, z), as the zone's WLD has it.
		sound.SetListener({sy, sx, sz}, {std::cos(yaw), std::sin(yaw), 0});
		sound.SetNight(night);
		sound.SetEnvironment(static_cast<audio::EqSound::Environment>(environment));
		sound.GiveTime(now);
		mixer.Render(&mix[static_cast<size_t>(tick) * frames_per_tick * 2], frames_per_tick);
		now += 1000 / fps;
		if (tick % fps == 0) {
			auto stats = sound.GetStats();
			printf("%5.1fs at %.0f %.0f %.0f: music %s, %zu voices (%zu ambient), %zu of %zu emitters in range\n",
				tick / static_cast<float>(fps), sx, sy, sz, stats.music.empty() ? "-" : stats.music.c_str(), stats.voices,
				stats.ambient_voices, stats.emitters_in_range, stats.emitters);
		}
	}
	for (const auto &name : sound.GetStats().missing)
		printf("missing: %s\n", name.c_str());

	printf("loudness each second (RMS dBFS, peak):");
	size_t second = audio::Mixer::Rate * 2;
	for (size_t start = 0; start < mix.size(); start += second) {
		double sum = 0;
		float peak = 0;
		size_t end = std::min(mix.size(), start + second);
		for (size_t i = start; i < end; ++i) {
			sum += mix[i] * mix[i];
			peak = std::max(peak, std::fabs(mix[i]));
		}
		double rms = std::sqrt(sum / static_cast<double>(end - start));
		printf(" %.0f/%.2f", rms > 0 ? 20 * std::log10(rms) : -99.0, peak);
	}
	printf("\n");
	WriteWav(out, mix);
	printf("wrote %s (%.1f s)\n", out.c_str(), mix.size() / 2.0 / audio::Mixer::Rate);
	return 0;
}
