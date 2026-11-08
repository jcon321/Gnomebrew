#pragma once

// The client's sound, as EqSoundManager (@004d482a) and what's under it run it, on our Mixer.
//
// Sounds come from the EQ folder, or else from snd1.pfs..snd14.pfs (EqSoundManager mounts those
// fourteen and no others): .wav waves, .mp3 music and .xmi music, which holds several sequences.
//
// Music is MusicManager's: a list of tracks, each an asset (a sequence of an .xmi, or an .mp3) with
// a priority, a loop count and fades. Whatever turns a track on or off (a music selection, or a zone
// music emitter we're in range of) leaves MusicManager::GiveTime to pick the one that plays: the
// active track with the lowest priority number, the one playing keeping a tie. The one it replaces
// fades out over the track's switch fade, or its stop fade when nothing follows it. A track whose
// asset is still sounding (fading out) fades back up rather than starting again, so moving between
// two areas of one .xmi keeps the tune going. LoadGlobalMusic's tracks: 1 the opening and 4
// character select (both eqtheme.mp3, else opener4.xmi), 2 combat (combattheme1 or 2.mp3, picked
// at random, else gl.xmi), 8 death (deaththeme.mp3, played once, else gl.xmi), and the rest gl.xmi's
// sequences for the merchant, bank and guild windows.
//
// A zone's sounds are its <zone>.emt (EqSoundManager::EmitterLoad), one emitter a line; zones
// without one would fall back to <zone>_sounds.eff, but every zone with sounds has one. An .xmi or
// .mp3 emitter is a music track, priority 10000 + its activation range, on while we're within that
// range. A .wav emitter plays at its place (or, randomized, somewhere within that distance around
// us) while we're within range: looping, or once each time its random repeat delay comes round.
// Emitters can be for the day or the night only, and for outside, under a high ceiling or a low
// one only (CDisplay::SetupEmitterEnvironment, SetGenericEnvironment).
//
// The weather's are two global emitters (LoadGlobalEmitters), rainloop.wav and wind_lp1.wav, at half
// volume everywhere, fading in over 2 seconds and out over 3: rain on while it rains, wind while
// it snows (EmitterSetRaining and EmitterSetWind, from CDisplay's weather), and neither while the
// floor under us is covered (SetupEmitterEnvironment's exposure under 0.48). The thunder is a
// numbered wave, thunder1 or thunder2.wav, that CDisplay::LightningStrike plays.
//
// Game sounds are EqSoundManager's numbered waves (4000 slots, WaveLoad and WavePlay): the global
// ones (LoadGlobalWaves: jumps, footsteps, the wood door...), the doors' (EQSwitch::
// LoadDoorSoundEffects, by door type, as doors spawn), and the creatures' (EQPlayer::SetSounds'
// table, actor_sound_table.h). Doors play theirs where they are (CDisplay_PlaySoundAtLocation:
// full volume within 5, silent past 80), actors from themselves (EQPlayer::PlaySoundA: within
// half their height, out to 24 times that; ours on a channel of our own at 0.8), and an actor's
// looping sound (walking, idling) follows it about on its EqMobileEmitter, as far out as that.
//
// The client's Miles voices come from pools, and a sound that finds its pool empty doesn't play:
// six plain samples and six 3D ones for the game's sounds, one plain sample for MP3 music, one
// sequence for XMI music, two 3D samples for the other channel, and four 3D samples for the zone's
// emitters. So at most four ambient sounds play at once, and a new XMI or MP3 waits for the last to
// finish fading out.

#include "mixer.h"
#include "pfs.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace audio
{
	class EqSound
	{
	public:
		// MusicManager track ids for SetMusicSelection.
		enum Music
		{
			Opening = 1,         // CDisplay::InitDDraw, until the main loop starts
			Combat = 2,          // auto attack
			Bank = 3,            // CBankWnd::Activate
			CharacterSelect = 4, // character select
			Merchant = 6,        // CMerchantWnd::Activate
			Trainer = 7,         // msg_req_guildmaster, CTrainWnd::Activate
			Death = 8,           // EQPlayer::IDied
		};
		// SetGenericEnvironment's (and an emitter's "environment" column).
		enum class Environment { Unset = 0, Outside = 1, HighCeiling = 2, LowCeiling = 3 };

		struct Settings
		{
			// The options' levels, 0..20 in eqclient.ini, as fractions; 10 by default.
			float music_volume = 0.5f;
			float sound_volume = 0.5f;
		};

		EqSound(Mixer &mixer, const std::string &eq_dir, const Settings &settings);
		~EqSound();

		void SetMusicSelection(int id, bool on);
		// SetCurrentZone: the zone's emitters (and its .xmi) in place of the last's. Empty: none.
		void SetCurrentZone(const std::string &zone);
		const std::string &CurrentZone() const { return m_zone; }

		// Where we hear from (our character, in world space) and the way the camera faces.
		void SetListener(const Vec3 &position, const Vec3 &forward);
		// SetSoundEnvironmentMode: night is an hour (1..24, 1 midnight) under 5 or over 18.
		void SetNight(bool night);
		void SetEnvironment(Environment environment);
		// EmitterSetRaining and EmitterSetWind: the weather's loops on or off. SetCovered is
		// setSoundEmitterUnderwater (as it's named): the floor under us covered, which silences them.
		void SetRaining(bool on);
		void SetWind(bool on);
		void SetCovered(bool covered);

		// EqSoundManager::GiveTime, once a frame: the emitters, the music, and the voices that finished.
		void GiveTime(uint64_t now_ms);

		void SetVolumes(const Settings &settings);

		// What plays (the debug menu's switches; all on). Music off is MusicManager's own switch
		// (/music): what's playing fades out as if another track took over. Ambience is the zone's
		// wave emitters (its music emitters are music), game sounds the numbered waves (doors,
		// steps, jumps, splashes, creatures) and the creatures' loops, and weather the rain and
		// wind loops and the thunder. Off, they fade out as when we leave their range, and don't
		// start.
		struct Switches
		{
			bool sound = true; // all of it
			bool music = true, ambience = true, game = true, weather = true;
		};
		void SetSwitches(const Switches &switches);
		const Switches &GetSwitches() const { return m_switches; }
		// Told each numbered wave that's asked to play, and whether it did (for tools).
		void SetWaveLogger(std::function<void(const std::string &)> log) { m_wave_log = std::move(log); }

		// EQSwitch::LoadDoorSoundEffects: the waves a door of `type` uses, loaded as it spawns.
		void LoadDoorSounds(int type);
		// The wave EQSwitch::ChangeState plays for a door of `type` going to `state` (0 closed,
		// 2 opening, 3 closing), or 0.
		static int DoorSound(int type, int state);
		// CDisplay_PlaySoundAtLocation.
		void PlaySoundAt(int slot, const Vec3 &position);
		// EQPlayer::PlaySoundA: `position` is the actor's (its model's center), `height` its size.
		void PlayActorSound(int slot, const Vec3 &position, float height, bool ours);
		// SoundManager::IsSoundPlaying.
		bool WavePlaying(int slot);
		// The file a numbered wave plays (for the debug menu); empty for none.
		std::string WaveFile(int slot) const;
		// WavePlay with no control: right where we are (full volume within 1000), once.
		void PlayWave(int slot);
		// The same for the weather's thunder (CDisplay::LightningStrike): on the weather's switch.
		void PlayWeatherWave(int slot);
		// WaveStop: the wave's voices end at once.
		void StopWave(int slot);
		// EqMobileEmitter: actor `key`'s looping sound (an ActorSounds loop number, 0 none),
		// moved to where it is.
		void SetActorLoop(uint32_t key, const Vec3 &position, float height, int loop);
		void RemoveActorLoop(uint32_t key);

		// For the debug menu.
		struct Stats
		{
			std::string music;      // the track picked: its file, and the sequence sounding for an .xmi
			int music_track = 0;    // its id
			size_t emitters = 0, emitters_in_range = 0;
			size_t actor_loops = 0;        // creatures' looping sounds playing
			size_t voices = 0;      // ours playing, of all kinds
			size_t ambient_voices = 0; // emitter voices playing (of four)
			struct Playing
			{
				std::string name;
				float distance;
			};
			std::vector<Playing> ambient; // what they are, and how far
			int environment = 0, time_of_day = 0;
			bool raining = false, wind = false, covered = false;
			std::vector<std::string> weather; // the weather's sounds playing: its loops, and thunder
			std::vector<std::string> missing; // assets that weren't found
		};
		Stats GetStats() const;

	private:
		enum class AssetType { None, Wave, Mp3, Xmi };
		struct Instance;
		struct Asset
		{
			std::string name;
			AssetType type = AssetType::None;
			std::shared_ptr<const Clip> clip;
			std::shared_ptr<const std::vector<uint8_t>> mp3;
			std::vector<std::shared_ptr<const XmiSequence>> sequences;
			std::vector<Instance *> playing; // SoundAsset's list of sound objects
		};
		// SoundControl: how to play one.
		struct Control
		{
			float volume = 1;
			int fade_ms = 0;
			int loops = 1; // 0 forever
			int sequence = 0;
			bool positional = false;
			Vec3 position;
			float min_distance = 0, max_distance = 0;
			int channel = 0;
			bool weather = false; // on the weather's switch rather than the game sounds'
		};
		struct Instance
		{
			Mixer::Voice voice = 0;
			int pool = 0;
			std::shared_ptr<Asset> asset;
			int sequence = 0; // an .xmi's
			bool positional = false;
			Vec3 position;
		};
		struct MusicTrack
		{
			int id = 0;
			std::shared_ptr<Asset> asset;
			int sequence = 0, loops = 0, priority = 0;
			int fade_in = 0, fade_out_switch = 0, fade_out_stop = 0;
			int channel = 0;
			bool start = false;  // play it (again) when it's picked
			bool active = false; // wants to be heard
		};
		struct Emitter
		{
			std::shared_ptr<Asset> asset;
			int music_id = 0; // a music emitter's track
			float volume = 1;
			int fade_in = 0, fade_out = 0, loop_type = 0;
			Vec3 position;
			float full_volume_radius = 0, max_distance = 0, randomize = 0, activation_range = 0;
			int min_delay = 0, max_delay = 0;
			int environment = 0, when = 0;
			int channel = 2; // the zone's; an actor's is 0
			bool weather = false; // the rain's or the wind's
			bool enabled = true;
			uint64_t last_ms = 0;
			int delay_ms = 0;
			Mixer::Voice playing = 0; // a looping wave's, held until it's let go
		};

		std::shared_ptr<Asset> GetAsset(const std::string &name);
		bool ReadFile(const std::string &name, std::vector<uint8_t> &out) const;
		Instance *Play(const std::shared_ptr<Asset> &asset, const Control &control);
		void FadeAsset(Asset &asset, float volume, int ms);
		void ReleaseFinished();

		// MusicManager
		void SetTrack(int id, const std::shared_ptr<Asset> &asset, int sequence, int loops, int priority, int fade_in,
			int fade_out_switch, int fade_out_stop, int channel);
		void RemoveTrack(int id);
		void PlayTrack(int id);
		void StopTrack(int id);
		void MusicGiveTime();
		void LoadGlobalMusic();

		void LoadEmitters(const std::string &path);
		void LoadGlobalWaves();
		void LoadGlobalEmitters();
		// WaveLoad: `name` into `slot`, unless it's there already (what was there stops).
		void WaveLoad(int slot, const std::string &name);
		std::shared_ptr<Asset> Wave(int slot); // loading a creature's on first use
		void WavePlay(int slot, Control control);
		void EmitterGiveTime(Emitter &emitter, uint64_t now_ms);
		void ResetDelay(Emitter &emitter, uint64_t now_ms);
		void UpdateEnables(); // EqSoundManager's Enable2
		void ClearZone();

		Mixer &m_mixer;
		std::string m_eq_dir;
		std::map<std::string, std::string> m_loose; // lowercase -> the file's name, in the EQ folder
		std::vector<std::unique_ptr<PfsArchive>> m_packs;
		std::map<std::string, std::shared_ptr<Asset>> m_assets; // lowercase name
		std::vector<std::string> m_missing;

		struct Pool
		{
			int capacity;
			int used = 0;
		};
		std::vector<Pool> m_pools;
		std::vector<std::unique_ptr<Instance>> m_instances;

		std::vector<MusicTrack> m_tracks; // newest first, as MusicManager::Set links them
		int m_current_track = 0;          // its id; 0 none
		float m_music_volume = 0.5f, m_sound_volume = 0.5f;

		std::string m_zone;
		std::shared_ptr<Asset> m_zone_xmi;
		std::vector<Emitter> m_emitters;
		struct WaveSlot
		{
			std::string name;
			std::shared_ptr<Asset> asset;
		};
		std::vector<WaveSlot> m_waves;               // EqSoundManager's 4000
		std::map<int, const char *> m_actor_waves;   // SetSounds' slots, loaded when first played
		std::map<uint32_t, Emitter> m_actor_loops;
		int m_emitter_counter = 16; // music emitters' track ids count up from here
		int m_environment = 0, m_time_of_day = 0;
		Emitter m_rain, m_wind; // LoadGlobalEmitters'
		bool m_raining = false, m_windy = false, m_covered = false;
		Vec3 m_listener, m_forward{1, 0, 0};
		std::function<void(const std::string &)> m_wave_log;
		Switches m_switches;
		bool MusicOn() const { return m_switches.sound && m_switches.music; }
		bool AmbienceOn() const { return m_switches.sound && m_switches.ambience; }
		bool GameOn() const { return m_switches.sound && m_switches.game; }
		bool WeatherOn() const { return m_switches.sound && m_switches.weather; }
		uint64_t m_now_ms = 0;
	};
}
