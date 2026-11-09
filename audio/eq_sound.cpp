#include "eq_sound.h"
#include "actor_sound_table.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace audio
{
	namespace
	{
		// SoundManager's pools (InitializeSoundSystem and the EqSoundManager constructor): plain
		// samples on channels 0-2, 3D samples on channels 0-2, and the one sequence.
		enum PoolIndex { Plain0, Plain1, Plain2, Positional0, Positional1, Positional2, SequencePool, PoolCount };
		constexpr int PoolCapacity[PoolCount] = {6, 1, 0, 6, 2, 4, 1};
		constexpr int SwitchFadeMs = 1500; // a zone music track's fade out to another
		constexpr int WaveSlots = 4000;
		constexpr int ActorChannel = 0, OurChannel = 1;

		// LoadGlobalWaves' lists: each from its slot on.
		const char *const MaleWaves[] = {"JumpM_1.WAV", "GetHit1M.WAV", "GetHit2M.WAV", "GetHit3M.WAV", "GetHit4M.WAV",
			"Gasp1M.WAV", "Gasp2M.WAV", "Death_M.WAV", "Drown_M.WAV", "Drown_M.WAV"};
		const char *const FemaleWaves[] = {"JumpF_1.WAV", "GetHit1F.WAV", "GetHit2F.WAV", "GetHit3F.WAV", "GetHit4F.WAV",
			"Gasp1F.WAV", "Gasp2F.WAV", "Death_F.WAV", "Drown_F.WAV", "Drown_F.WAV"};
		const char *const MaleLargeWaves[] = {"JumpM_1L.WAV", "GetHit1ML.WAV", "GetHit2ML.WAV", "GetHit3ML.WAV",
			"GetHit4ML.WAV", "Gasp1ML.WAV", "Gasp2ML.WAV", "Death_ML.WAV", "Drown_ML.WAV", "Drown_ML.WAV"};
		const char *const FemaleLargeWaves[] = {"JumpF_1L.WAV", "GetHit1FL.WAV", "GetHit2FL.WAV", "GetHit3FL.WAV",
			"GetHit4FL.WAV", "Gasp1FL.WAV", "Gasp2FL.WAV", "Death_FL.WAV", "Drown_FL.WAV", "Drown_FL.WAV"};
		const char *const MaleBigWaves[] = {"JumpM_1B.WAV", "GetHit1MB.WAV", "GetHit2MB.WAV", "GetHit3MB.WAV",
			"GetHit4MB.WAV", "Gasp1MB.WAV", "Gasp2MB.WAV", "Death_MB.WAV", "Drown_MB.WAV", "Drown_MB.WAV"};
		const char *const FemaleBigWaves[] = {"JumpF_1B.WAV", "GetHit1FB.WAV", "GetHit2FB.WAV", "GetHit3FB.WAV",
			"GetHit4FB.WAV", "Gasp1FB.WAV", "Gasp2FB.WAV", "Death_FB.WAV", "Drown_FB.WAV", "Drown_FB.WAV"};
		const char *const GameWaves[] = {"StepWlkL.WAV", "StepWlkR.WAV", "StepRunL.WAV", "StepRunR.WAV", "StepLadR.WAV",
			"StepLadL.WAV", "StepCrch.WAV", "WaterOut.WAV", "WaterIn.WAV", "WatTrd_1.WAV", "WatTrd_2.WAV", "Spell_1.wav",
			"Spell_2.wav", "Spell_3.wav", "Spell_4.wav", "Spell_5.wav", "SpelCast.WAV", "SpelGdHt.WAV", "SpelHit1.WAV",
			"SpelHit2.WAV", "SpelHit3.WAV", "SpelHit4.WAV", "jumpland.WAV", "fall_hit.WAV", "BowDraw.WAV", "ArrowHit.WAV",
			"Swing.WAV", "SwingBig.WAV", "SwordBlk.WAV", "Impale.WAV", "Stab.WAV", "Club.WAV", "BashShld.WAV",
			"ShldBlk.WAV", "Kick1.WAV", "KickHit.WAV", "RndKick.WAV", "LeftAtk.WAV", "Punch1.WAV", "PunchHit.WAV",
			"StaffBlk.WAV", "Chest_Cl.WAV", "Chest_Op.WAV", "DoorWd_O.WAV", "DoorWd_C.WAV", "Teleport.WAV", "BuyItem.WAV",
			"LevelUp.WAV", "LevDn.WAV", "EndQuest.WAV", "Button_1.WAV", "thunder1.wav", "thunder2.wav", "itemclth.WAV",
			"wearclth.WAV", "FlshHit1.WAV", "FlshHit2.WAV", "Drink.WAV", "Whip.WAV", "StepWlk2.WAV", "StepWlk3.WAV",
			"StepRun2.WAV", "StepRun3.WAV", "StepRun3.WAV", "StepRun3.WAV", "StepRun3.WAV", "wind_lp1.wav", "rainloop.wav",
			"Torch_Lp.WAV", "WatUndLp.WAV"};

		// The MSVC runtime's rand(), which the client's random delays and placements use.
		uint32_t g_seed = 1;
		int Rand()
		{
			g_seed = g_seed * 214013 + 2531011;
			return static_cast<int>((g_seed >> 16) & 0x7fff);
		}

		std::string Lower(std::string s) { return ToLower(std::move(s)); }

		bool HasExtension(const std::string &lower, const char *extension)
		{
			return lower.find(extension) != std::string::npos; // Util_Stristr, as EmitterAdd checks
		}

		// The client's comma-separated fields: atoi/atof of each up to the next comma.
		struct Fields
		{
			const char *at;
			std::string Next()
			{
				const char *comma = strchr(at, ',');
				std::string field = comma ? std::string(at, comma) : std::string(at);
				at = comma ? comma + 1 : at + strlen(at);
				return field;
			}
			int Int() { return atoi(Next().c_str()); }
			float Float() { return static_cast<float>(atof(Next().c_str())); }
		};

		std::string Trim(const std::string &s)
		{
			size_t start = s.find_first_not_of(" \t\r\n"), end = s.find_last_not_of(" \t\r\n");
			return start == std::string::npos ? std::string() : s.substr(start, end - start + 1);
		}
	}

	EqSound::EqSound(Mixer &mixer, const std::string &eq_dir, const Settings &settings)
		: m_mixer(mixer), m_eq_dir(eq_dir), m_music_volume(settings.music_volume), m_sound_volume(settings.sound_volume),
		  m_effects_level(settings.effects_level)
	{
		g_seed = static_cast<uint32_t>(time(nullptr)); // LoadGlobalMusic's srand(time(0))
		for (int i = 0; i < PoolCount; ++i)
			m_pools.push_back({PoolCapacity[i]});
		m_waves.resize(WaveSlots);
		size_t count;
		const WaveName *waves = ActorWaves(count);
		for (size_t i = 0; i < count; ++i)
			m_actor_waves[waves[i].slot] = waves[i].name;

		std::error_code ec;
		for (const auto &entry : std::filesystem::directory_iterator(eq_dir, ec)) {
			std::string name = entry.path().filename().string();
			std::string lower = Lower(name);
			if (HasExtension(lower, ".wav") || HasExtension(lower, ".mp3") || HasExtension(lower, ".xmi") ||
				HasExtension(lower, ".emt"))
				m_loose[lower] = name;
		}
		for (int i = 1; i <= 14; ++i) {
			auto pack = std::make_unique<PfsArchive>();
			if (pack->Open(eq_dir + "/snd" + std::to_string(i) + ".pfs", true))
				m_packs.push_back(std::move(pack));
		}
		LoadGlobalMusic();
		LoadGlobalWaves();
		LoadGlobalEmitters();
	}

	EqSound::~EqSound()
	{
		for (auto &instance : m_instances)
			m_mixer.Stop(instance->voice);
	}

	void EqSound::SetVolumes(const Settings &settings)
	{
		m_effects_level = settings.effects_level; // SetEffectsLevel: sounds started from now on
		m_sound_volume = settings.sound_volume;
		if (settings.music_volume == m_music_volume)
			return;
		m_music_volume = settings.music_volume;
		// SoundManager::SetDigitalVolume: the track playing changes at once.
		for (auto &track : m_tracks) {
			if (track.id == m_current_track && track.asset)
				FadeAsset(*track.asset, m_music_volume, 0);
		}
	}

	// ---------------------------------------------------------------- assets

	bool EqSound::ReadFile(const std::string &name, std::vector<uint8_t> &out) const
	{
		auto loose = m_loose.find(Lower(name));
		if (loose != m_loose.end()) {
			std::ifstream file(m_eq_dir + "/" + loose->second, std::ios::binary);
			if (file) {
				out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
				return true;
			}
		}
		for (const auto &pack : m_packs) {
			if (pack->Contains(name))
				return pack->Read(name, out);
		}
		return false;
	}

	std::shared_ptr<EqSound::Asset> EqSound::GetAsset(const std::string &name)
	{
		std::string key = Lower(name);
		auto cached = m_assets.find(key);
		if (cached != m_assets.end())
			return cached->second;

		std::vector<uint8_t> data;
		if (key.empty() || !ReadFile(key, data)) {
			if (!key.empty() && std::find(m_missing.begin(), m_missing.end(), key) == m_missing.end())
				m_missing.push_back(key);
			return nullptr;
		}
		auto asset = std::make_shared<Asset>();
		asset->name = key;
		size_t dot = key.rfind('.');
		std::string extension = dot == std::string::npos ? "" : key.substr(dot);
		if (extension == ".wav") {
			auto clip = std::make_shared<Clip>();
			if (DecodeWav(data.data(), data.size(), *clip)) {
				asset->type = AssetType::Wave;
				asset->clip = std::move(clip);
			}
		}
		else if (extension == ".mp3") {
			asset->type = AssetType::Mp3;
			asset->mp3 = std::make_shared<const std::vector<uint8_t>>(std::move(data));
		}
		else if (extension == ".xmi") {
			std::vector<XmiSequence> sequences;
			if (ParseXmi(data.data(), data.size(), sequences)) {
				asset->type = AssetType::Xmi;
				for (auto &sequence : sequences)
					asset->sequences.push_back(std::make_shared<const XmiSequence>(std::move(sequence)));
			}
		}
		if (asset->type == AssetType::None) {
			m_missing.push_back(key + " (unreadable)");
			return nullptr;
		}
		m_assets[key] = asset;
		return asset;
	}

	// SoundAsset::Play: a voice from the pool its kind and channel draw on, or none.
	EqSound::Instance *EqSound::Play(const std::shared_ptr<Asset> &asset, const Control &control)
	{
		if (!asset || control.volume <= 0)
			return nullptr;
		int pool;
		if (asset->type == AssetType::Xmi)
			pool = SequencePool;
		else if (!control.positional || asset->type == AssetType::Mp3)
			pool = control.channel >= 0 && control.channel <= 2 ? Plain0 + control.channel : -1;
		else {
			float dx = m_listener.x - control.position.x, dy = m_listener.y - control.position.y,
			      dz = m_listener.z - control.position.z;
			if (control.max_distance * control.max_distance <= dx * dx + dy * dy + dz * dz)
				return nullptr; // out of earshot: not started
			pool = control.channel >= 0 && control.channel <= 2 ? Positional0 + control.channel : -1;
		}
		if (pool < 0 || m_pools[pool].used >= m_pools[pool].capacity)
			return nullptr;

		Mixer::Voice voice = 0;
		switch (asset->type) {
		case AssetType::Wave: {
			Mixer::Placement placement;
			placement.positional = control.positional;
			placement.position = control.position;
			placement.min_distance = control.min_distance;
			placement.max_distance = control.max_distance;
			placement.effects = std::clamp(control.effects, 0.0f, 1.0f);
			voice = m_mixer.PlayClip(asset->clip, control.loops, control.volume, control.fade_ms, placement);
			break;
		}
		case AssetType::Mp3: voice = m_mixer.PlayMp3(asset->mp3, control.loops, control.volume, control.fade_ms); break;
		case AssetType::Xmi:
			if (control.sequence >= 0 && control.sequence < static_cast<int>(asset->sequences.size()))
				voice = m_mixer.PlaySequence(asset->sequences[control.sequence], control.loops, control.volume, control.fade_ms);
			break;
		default: break;
		}
		if (!voice)
			return nullptr;
		auto instance = std::make_unique<Instance>();
		instance->voice = voice;
		instance->pool = pool;
		instance->asset = asset;
		instance->sequence = control.sequence;
		instance->positional = control.positional;
		instance->position = control.position;
		++m_pools[pool].used;
		asset->playing.push_back(instance.get());
		m_instances.push_back(std::move(instance));
		return m_instances.back().get();
	}

	// SoundAsset::AdjustVolume: every voice of the asset.
	void EqSound::FadeAsset(Asset &asset, float volume, int ms)
	{
		for (Instance *instance : asset.playing)
			m_mixer.Fade(instance->voice, volume, ms);
	}

	// SoundManager::ProcessSoundObjects: finished voices go back to their pools.
	void EqSound::ReleaseFinished()
	{
		for (size_t i = 0; i < m_instances.size();) {
			Instance &instance = *m_instances[i];
			if (m_mixer.Active(instance.voice)) {
				++i;
				continue;
			}
			auto &playing = instance.asset->playing;
			playing.erase(std::remove(playing.begin(), playing.end(), &instance), playing.end());
			--m_pools[instance.pool].used;
			m_instances.erase(m_instances.begin() + static_cast<long>(i));
		}
	}

	// ---------------------------------------------------------------- music

	void EqSound::SetTrack(int id, const std::shared_ptr<Asset> &asset, int sequence, int loops, int priority, int fade_in,
		int fade_out_switch, int fade_out_stop, int channel)
	{
		RemoveTrack(id);
		if (!asset)
			return;
		MusicTrack track;
		track.id = id;
		track.asset = asset;
		track.sequence = sequence;
		track.loops = loops;
		track.priority = priority;
		track.fade_in = fade_in;
		track.fade_out_switch = fade_out_switch;
		track.fade_out_stop = fade_out_stop;
		track.channel = channel;
		m_tracks.insert(m_tracks.begin(), track);
	}

	void EqSound::RemoveTrack(int id)
	{
		for (auto it = m_tracks.begin(); it != m_tracks.end(); ++it) {
			if (it->id == id) {
				m_tracks.erase(it);
				if (m_current_track == id)
					m_current_track = 0;
				return;
			}
		}
	}

	void EqSound::PlayTrack(int id)
	{
		for (auto &track : m_tracks) {
			if (track.id == id) {
				track.start = track.active = true;
				MusicGiveTime();
				return;
			}
		}
	}

	void EqSound::StopTrack(int id)
	{
		for (auto &track : m_tracks) {
			if (track.id == id) {
				track.active = false;
				break;
			}
		}
		MusicGiveTime();
	}

	void EqSound::SetMusicSelection(int id, bool on)
	{
		if (on)
			PlayTrack(id);
		else
			StopTrack(id);
	}

	// MusicManager::GiveTime.
	void EqSound::MusicGiveTime()
	{
		MusicTrack *current = nullptr;
		for (auto &track : m_tracks) {
			if (track.id == m_current_track && m_current_track != 0)
				current = &track;
		}
		MusicTrack *best = current && current->active ? current : nullptr;
		for (auto &track : m_tracks) {
			if (track.active && (!best || track.priority < best->priority))
				best = &track;
		}
		if (best != current) {
			if (current) // SoundObject_AdjustSoundVolume: its stop fade when nothing follows
				FadeAsset(*current->asset, 0, best ? current->fade_out_switch : current->fade_out_stop);
			m_current_track = best ? best->id : 0;
		}
		if (!best)
			return;
		if (!MusicOn()) {
			FadeAsset(*best->asset, 0, best->fade_out_switch); // MusicManager switched off
			return;
		}
		if (!best->start) {
			best->active = false;
			return;
		}
		if (m_music_volume > 0) {
			// SoundObject_PlayOrAdjustSoundVolume: up to the music volume if it's still sounding.
			if (!best->asset->playing.empty())
				FadeAsset(*best->asset, m_music_volume, best->fade_in);
			else {
				Control control;
				control.volume = m_music_volume;
				control.fade_ms = best->fade_in;
				control.loops = best->loops;
				control.sequence = best->sequence;
				control.channel = best->channel;
				Play(best->asset, control);
			}
		}
		if (best->loops != 0)
			best->start = false; // played once: it lets go next time round
	}

	void EqSound::LoadGlobalMusic()
	{
		auto gl = GetAsset("gl.xmi");
		auto opener = GetAsset("opener4.xmi");
		auto theme = GetAsset("eqtheme.mp3");
		auto death = GetAsset("deaththeme.mp3");
		auto combat = GetAsset(Rand() % 2 == 0 ? "combattheme2.mp3" : "combattheme1.mp3");
		if (theme) {
			SetTrack(Opening, theme, 0, 0, 10, 0, 1500, 5000, 1);
			SetTrack(CharacterSelect, theme, 0, 0, 10, 0, 1500, 5000, 1);
		}
		else {
			SetTrack(Opening, opener, 0, 0, 10, 0, 1500, 5000, 0);
			SetTrack(CharacterSelect, opener, 1, 0, 9, 0, 1500, 5000, 0);
		}
		if (combat)
			SetTrack(Combat, combat, 0, 0, 3, 1000, 3000, 4000, 1);
		else
			SetTrack(Combat, gl, 0, 0, 3, 1500, 3000, 4000, 0);
		if (death)
			SetTrack(Death, death, 0, 1, 2, 1500, 1500, 2500, 1);
		else
			SetTrack(Death, gl, 3, 1, 2, 1500, 1500, 2500, 0);
		// gl.xmi's sequences, for the merchant, bank, trainer and guild windows.
		SetTrack(Bank, gl, 21, 0, 5, 1500, 2500, 3500, 0);
		SetTrack(Merchant, gl, 21, 0, 5, 1500, 2500, 3500, 0);
		SetTrack(Trainer, gl, 23, 0, 5, 1500, 2500, 3500, 0);
		SetTrack(15, gl, 19, 0, 5, 1500, 2500, 3500, 0);
		SetTrack(9, gl, 20, 0, 5, 1500, 2500, 3500, 0);
		SetTrack(10, gl, 7, 0, 6, 1500, 2500, 3500, 0);
		SetTrack(11, gl, 8, 0, 5, 1500, 1500, 2500, 0);
		SetTrack(12, gl, 9, 0, 5, 1500, 1500, 2500, 0);
		SetTrack(13, gl, 10, 0, 5, 1500, 1500, 2500, 0);
		SetTrack(14, gl, 11, 0, 5, 1500, 1500, 2500, 0);
		RemoveTrack(5);
	}

	// ---------------------------------------------------------------- zones and emitters

	void EqSound::SetCurrentZone(const std::string &zone)
	{
		std::string lower = Lower(zone);
		if (!lower.empty() && lower == m_zone)
			return;
		ClearZone();
		m_zone = lower;
		if (!m_zone.empty()) {
			m_zone_xmi = GetAsset(m_zone + ".xmi");
			LoadEmitters(m_zone + ".emt");
		}
		m_environment = 0;
		m_time_of_day = 0;
	}

	void EqSound::ClearZone()
	{
		// SetCurrentZone3: the emitters go, music ones taking their tracks with them
		// (EqSoundManager::cleanupSoundManager), wave ones fading out what they hold.
		for (auto &emitter : m_emitters) {
			if (emitter.music_id) {
				StopTrack(emitter.music_id);
				RemoveTrack(emitter.music_id);
			}
			else if (emitter.playing) {
				m_mixer.Fade(emitter.playing, 0, emitter.fade_out);
				emitter.playing = 0;
			}
		}
		m_emitters.clear();
		for (auto &entry : m_actor_loops) // the zone's actors go with it
			if (entry.second.playing)
				m_mixer.Fade(entry.second.playing, 0, 0);
		m_actor_loops.clear();
		// SetCurrentZone4: the zone's .xmi stops at once, and its tracks go.
		if (m_zone_xmi) {
			for (auto it = m_tracks.begin(); it != m_tracks.end();) {
				if (it->asset == m_zone_xmi) {
					if (it->id == m_current_track)
						m_current_track = 0;
					it = m_tracks.erase(it);
				}
				else
					++it;
			}
			FadeAsset(*m_zone_xmi, 0, 0);
			m_zone_xmi.reset();
		}
		// Assets nothing holds or plays any more.
		for (auto it = m_assets.begin(); it != m_assets.end();) {
			if (it->second.use_count() == 1 && it->second->playing.empty())
				it = m_assets.erase(it);
			else
				++it;
		}
	}

	// EqSoundManager::EmitterLoad and parseEmitterDataFromCSV: a line is a leading number, the
	// file, then the columns the .emt's header names.
	void EqSound::LoadEmitters(const std::string &name)
	{
		std::vector<uint8_t> data;
		if (!ReadFile(name, data))
			return;
		std::string text(data.begin(), data.end());
		size_t start = 0;
		while (start < text.size()) {
			size_t end = text.find('\n', start);
			std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
			start = end == std::string::npos ? text.size() : end + 1;
			if (line.size() <= 10 || line[0] == ';')
				continue;

			Fields fields{line.c_str()};
			fields.Int();
			std::string file = Trim(fields.Next()).substr(0, 31);
			Emitter emitter;
			emitter.environment = fields.Int();
			emitter.when = fields.Int();
			emitter.volume = fields.Float();
			emitter.fade_in = fields.Int();
			emitter.fade_out = fields.Int();
			emitter.loop_type = fields.Int();
			emitter.position.x = fields.Float();
			emitter.position.y = fields.Float();
			emitter.position.z = fields.Float();
			emitter.full_volume_radius = fields.Float();
			emitter.max_distance = fields.Float();
			emitter.randomize = fields.Float();
			emitter.activation_range = fields.Float();
			emitter.min_delay = fields.Int();
			emitter.max_delay = fields.Int();
			int sequence = fields.Int();
			emitter.effects = fields.Float();

			// EqSoundManager::EmitterAdd.
			auto asset = GetAsset(file);
			if (!asset)
				continue;
			std::string lower = Lower(file);
			if (HasExtension(lower, ".xmi") || HasExtension(lower, ".mp3")) {
				emitter.music_id = ++m_emitter_counter;
				SetTrack(emitter.music_id, asset, sequence, emitter.loop_type,
					static_cast<int>(emitter.activation_range + 10000), emitter.fade_in, SwitchFadeMs, emitter.fade_out,
					asset->type == AssetType::Mp3 ? 1 : 0);
			}
			else
				emitter.asset = asset;
			m_emitters.push_back(emitter);
		}
	}

	void EqSound::ResetDelay(Emitter &emitter, uint64_t now_ms)
	{
		emitter.last_ms = now_ms;
		emitter.delay_ms = emitter.min_delay;
		if (emitter.max_delay > emitter.min_delay)
			emitter.delay_ms += Rand() % (emitter.max_delay - emitter.min_delay);
	}

	// SoundEmitter::GiveTime.
	void EqSound::EmitterGiveTime(Emitter &emitter, uint64_t now_ms)
	{
		if (emitter.last_ms == 0)
			ResetDelay(emitter, now_ms);
		bool wanted = false;
		bool on = emitter.music_id ? true
		          : emitter.weather ? WeatherOn()
		          : emitter.channel == ActorChannel ? GameOn()
		                                            : AmbienceOn();
		if (emitter.enabled && on) {
			float dx = m_listener.x - emitter.position.x, dy = m_listener.y - emitter.position.y,
			      dz = m_listener.z - emitter.position.z;
			float range = emitter.activation_range;
			if (range <= 0 || dx * dx + dy * dy + dz * dz < range * range) {
				int since = static_cast<int>(now_ms - emitter.last_ms);
				wanted = emitter.delay_ms == 0 || since > emitter.delay_ms;
			}
		}
		if (emitter.music_id) {
			if (wanted)
				PlayTrack(emitter.music_id);
			else
				StopTrack(emitter.music_id);
			return;
		}
		if (!emitter.asset)
			return;
		if (!wanted) {
			if (emitter.playing) { // StopAndReleaseSound
				m_mixer.Fade(emitter.playing, 0, emitter.fade_out);
				emitter.playing = 0;
			}
			return;
		}
		ResetDelay(emitter, now_ms);
		if (emitter.playing)
			return;
		Control control;
		control.channel = emitter.channel;
		control.positional = emitter.full_volume_radius != 0 || emitter.max_distance != 0;
		control.min_distance = emitter.full_volume_radius;
		control.max_distance = emitter.max_distance;
		control.volume = m_sound_volume * emitter.volume;
		control.effects = m_effects_level * emitter.effects; // SoundEmitter::GiveTime
		control.fade_ms = emitter.fade_in;
		control.loops = emitter.loop_type;
		if (emitter.randomize > 0) {
			// Somewhere around us, level with us.
			float angle = static_cast<float>(Rand() % 360) * 0.017453292f;
			float distance = static_cast<float>(Rand() % 10000) * emitter.randomize * 0.0001f;
			control.position = {std::sin(angle) * distance + m_listener.x, std::cos(angle) * distance + m_listener.y, m_listener.z};
		}
		else
			control.position = emitter.position;
		if ((emitter.min_delay == 0 && emitter.max_delay == 0) || emitter.loop_type == 0) {
			control.loops = 0; // loops, held until we leave
			if (Instance *instance = Play(emitter.asset, control))
				emitter.playing = instance->voice;
		}
		else
			Play(emitter.asset, control); // once, each time the delay comes round
	}

	// Enable2: an emitter for one environment or time of day is on only then.
	void EqSound::UpdateEnables()
	{
		for (auto &emitter : m_emitters) {
			bool off = (emitter.environment != 0 && emitter.environment != m_environment) ||
			           (emitter.when != 0 && emitter.when != m_time_of_day);
			emitter.enabled = !off;
		}
		// The weather's, while it rains or snows and the floor over us isn't covered. They take it
		// up now rather than at the next GiveTime, which doesn't come once we're out of the world.
		m_rain.enabled = m_raining && !m_covered;
		m_wind.enabled = m_windy && !m_covered;
		EmitterGiveTime(m_rain, m_now_ms);
		EmitterGiveTime(m_wind, m_now_ms);
	}

	// EqSoundManager::LoadGlobalEmitters: the rain (wave 0x9f) and the wind (0x9e), off until the
	// weather turns them on. Heard everywhere at half volume, not placed, looping.
	void EqSound::LoadGlobalEmitters()
	{
		auto make = [this](Emitter &emitter, int slot) {
			emitter.asset = Wave(slot);
			emitter.weather = true;
			emitter.channel = 0;
			emitter.volume = 0.5f;
			emitter.fade_in = 2000;
			emitter.fade_out = 3000;
			emitter.loop_type = 0;
			emitter.enabled = false;
		};
		make(m_rain, 0x9f);
		make(m_wind, 0x9e);
	}

	void EqSound::SetRaining(bool on)
	{
		m_raining = on;
		UpdateEnables();
	}

	void EqSound::SetWind(bool on)
	{
		m_windy = on;
		UpdateEnables();
	}

	void EqSound::SetCovered(bool covered)
	{
		if (covered == m_covered)
			return;
		m_covered = covered;
		UpdateEnables();
	}

	void EqSound::SetNight(bool night)
	{
		int value = night ? 2 : 1;
		if (value == m_time_of_day)
			return;
		m_time_of_day = value;
		UpdateEnables();
	}

	void EqSound::SetEnvironment(Environment environment)
	{
		int value = static_cast<int>(environment);
		if (value == m_environment)
			return;
		m_environment = value;
		ApplyRoom();
		UpdateEnables();
	}

	void EqSound::ApplyRoom()
	{
		// EqSoundManager's 0xfdc, 0xfe0 and 0xfe4.
		if (m_special_room)
			return;
		switch (static_cast<Environment>(m_environment)) {
		case Environment::HighCeiling: m_mixer.SetRoomType(2); break;
		case Environment::LowCeiling: m_mixer.SetRoomType(12); break;
		case Environment::Outside: m_mixer.SetRoomType(19); break;
		case Environment::Unset: break;
		}
	}

	void EqSound::SetSpecialEnvironment(int room)
	{
		m_special_room = true;
		m_mixer.SetRoomType(room);
	}

	void EqSound::SetGenericEnvironment()
	{
		m_special_room = false;
		ApplyRoom();
	}

	void EqSound::SetListener(const Vec3 &position, const Vec3 &forward)
	{
		m_listener = position;
		m_forward = forward;
		m_mixer.SetListener(position, forward);
	}

	void EqSound::GiveTime(uint64_t now_ms)
	{
		m_now_ms = now_ms;
		for (auto &emitter : m_emitters)
			EmitterGiveTime(emitter, now_ms);
		for (auto &entry : m_actor_loops)
			EmitterGiveTime(entry.second, now_ms);
		EmitterGiveTime(m_rain, now_ms);
		EmitterGiveTime(m_wind, now_ms);
		MusicGiveTime();
		ReleaseFinished();
	}

	EqSound::Stats EqSound::GetStats() const
	{
		Stats stats;
		for (const auto &track : m_tracks) {
			if (track.id != m_current_track || !m_current_track)
				continue;
			stats.music_track = track.id;
			stats.music = track.asset->name;
			// What's sounding can be another of the .xmi's sequences, fading back up.
			if (track.asset->type == AssetType::Xmi)
				stats.music += " #" + std::to_string(track.asset->playing.empty() ? track.sequence : track.asset->playing.front()->sequence);
			if (track.asset->playing.empty())
				stats.music += " (waiting)";
		}
		stats.emitters = m_emitters.size();
		for (const auto &emitter : m_emitters) {
			float dx = m_listener.x - emitter.position.x, dy = m_listener.y - emitter.position.y,
			      dz = m_listener.z - emitter.position.z;
			float range = emitter.activation_range;
			if (emitter.enabled && (range <= 0 || dx * dx + dy * dy + dz * dz < range * range))
				++stats.emitters_in_range;
		}
		for (const auto &entry : m_actor_loops)
			stats.actor_loops += entry.second.playing && m_mixer.Active(entry.second.playing) ? 1 : 0;
		stats.voices = m_instances.size();
		stats.ambient_voices = static_cast<size_t>(m_pools[Positional2].used);
		for (const auto &instance : m_instances) {
			if (instance->pool != Positional2)
				continue;
			float dx = m_listener.x - instance->position.x, dy = m_listener.y - instance->position.y,
			      dz = m_listener.z - instance->position.z;
			stats.ambient.push_back({instance->asset->name, std::sqrt(dx * dx + dy * dy + dz * dz)});
		}
		stats.environment = m_environment;
		stats.time_of_day = m_time_of_day;
		stats.raining = m_raining;
		stats.wind = m_windy;
		stats.covered = m_covered;
		for (const Emitter *emitter : {&m_rain, &m_wind}) {
			if (emitter->asset && !emitter->asset->playing.empty())
				stats.weather.push_back(emitter->asset->name + (emitter->playing ? "" : " (fading out)"));
		}
		for (int slot : {0x8f, 0x90}) { // LightningStrike's
			const auto &asset = m_waves[slot].asset;
			if (asset && !asset->playing.empty())
				stats.weather.push_back(asset->name);
		}
		stats.missing = m_missing;
		return stats;
	}

	// ---------------------------------------------------------------- waves

	void EqSound::LoadGlobalWaves()
	{
		auto list = [this](const char *const *names, size_t count, int first) {
			for (size_t i = 0; i < count; ++i)
				WaveLoad(first + static_cast<int>(i), names[i]);
		};
		list(MaleWaves, std::size(MaleWaves), 32);
		list(FemaleWaves, std::size(FemaleWaves), 42);
		list(MaleLargeWaves, std::size(MaleLargeWaves), 52);
		list(FemaleLargeWaves, std::size(FemaleLargeWaves), 62);
		list(MaleBigWaves, std::size(MaleBigWaves), 72);
		list(FemaleBigWaves, std::size(FemaleBigWaves), 82);
		list(GameWaves, std::size(GameWaves), 92);
	}

	void EqSound::WaveLoad(int slot, const std::string &name)
	{
		if (slot < 0 || slot >= WaveSlots)
			return;
		WaveSlot &wave = m_waves[slot];
		if (wave.asset && Lower(wave.name) == Lower(name))
			return;
		if (wave.asset)
			FadeAsset(*wave.asset, 0, 0);
		wave.name = name;
		wave.asset = GetAsset(name);
	}

	std::shared_ptr<EqSound::Asset> EqSound::Wave(int slot)
	{
		if (slot < 0 || slot >= WaveSlots)
			return nullptr;
		if (!m_waves[slot].asset) {
			auto known = m_actor_waves.find(slot);
			if (known != m_actor_waves.end() && m_waves[slot].name.empty())
				WaveLoad(slot, known->second); // SetSounds loads them as the creatures come
		}
		return m_waves[slot].asset;
	}

	// EqSoundManager::WavePlay with a control: its volume times the sound level.
	void EqSound::WavePlay(int slot, Control control)
	{
		if (control.weather ? !WeatherOn() : !GameOn())
			return;
		auto asset = Wave(slot);
		if (!asset) {
			if (m_wave_log)
				m_wave_log("wave " + std::to_string(slot) + " (none loaded)");
			return;
		}
		control.volume *= m_sound_volume;
		control.effects *= m_effects_level;
		Instance *instance = Play(asset, control);
		if (m_wave_log)
			m_wave_log("wave " + std::to_string(slot) + " " + asset->name + (instance ? "" : " (no voice, or out of earshot)"));
	}

	std::string EqSound::WaveFile(int slot) const
	{
		if (slot < 0 || slot >= WaveSlots)
			return {};
		if (!m_waves[slot].name.empty())
			return m_waves[slot].name;
		auto known = m_actor_waves.find(slot);
		return known != m_actor_waves.end() ? known->second : std::string();
	}

	bool EqSound::WavePlaying(int slot)
	{
		auto asset = slot >= 0 && slot < WaveSlots ? m_waves[slot].asset : nullptr;
		return asset && !asset->playing.empty();
	}

	void EqSound::PlayWave(int slot)
	{
		// WavePlay without a control: its own, where we are, with no effects.
		Control control;
		control.effects = 0;
		control.positional = true;
		control.position = m_listener;
		control.min_distance = 1000;
		control.max_distance = 10000;
		WavePlay(slot, control);
	}

	void EqSound::PlayWeatherWave(int slot)
	{
		Control control;
		control.effects = 0;
		control.positional = true;
		control.position = m_listener;
		control.min_distance = 1000;
		control.max_distance = 10000;
		control.weather = true;
		WavePlay(slot, control);
	}

	void EqSound::StopWave(int slot)
	{
		if (slot >= 0 && slot < WaveSlots && m_waves[slot].asset)
			FadeAsset(*m_waves[slot].asset, 0, 0);
	}

	void EqSound::SetSwitches(const Switches &switches)
	{
		m_switches = switches;
		MusicGiveTime(); // music fades now; the rest as their emitters next look
	}

	void EqSound::PlayWaveAt(int slot, const Vec3 &position, float min_distance, float max_distance)
	{
		Control control;
		control.positional = true;
		control.position = position;
		control.min_distance = min_distance;
		control.max_distance = max_distance;
		control.channel = 0;
		WavePlay(slot, control);
	}

	void EqSound::PlaySoundAt(int slot, const Vec3 &position)
	{
		Control control;
		control.positional = true;
		control.position = position;
		control.min_distance = 5;
		control.max_distance = 80;
		control.channel = 0;
		WavePlay(slot, control);
	}

	void EqSound::PlayActorSound(int slot, const Vec3 &position, float height, bool ours)
	{
		Control control;
		control.positional = true;
		control.position = position;
		control.min_distance = height * 0.5f;
		control.max_distance = control.min_distance * 24;
		control.channel = ours ? OurChannel : ActorChannel;
		control.volume = ours ? 0.8f : 1;
		WavePlay(slot, control);
	}

	void EqSound::SetActorLoop(uint32_t key, const Vec3 &position, float height, int loop)
	{
		auto found = m_actor_loops.find(key);
		if (found == m_actor_loops.end()) {
			if (loop == 0)
				return;
			// EqMobileEmitter: looping, no delays, on channel 0.
			Emitter emitter;
			emitter.channel = ActorChannel;
			emitter.loop_type = 0;
			found = m_actor_loops.emplace(key, emitter).first;
		}
		Emitter &emitter = found->second;
		// SetNpcHeight: full volume within half the height (2 at least), heard out to 24 times that.
		emitter.full_volume_radius = std::max(2.0f, height * 0.5f);
		emitter.max_distance = emitter.activation_range = emitter.full_volume_radius * 24;
		if (emitter.position.x != position.x || emitter.position.y != position.y || emitter.position.z != position.z) {
			emitter.position = position; // SoundEmitter::Move
			if (emitter.playing)
				m_mixer.Move(emitter.playing, position);
		}
		// SetWave: another sound stops this one (its fade out is 0).
		auto asset = loop > 0 ? Wave(3000 + loop) : nullptr;
		if (asset != emitter.asset) {
			if (emitter.playing) {
				m_mixer.Fade(emitter.playing, 0, emitter.fade_out);
				emitter.playing = 0;
			}
			emitter.asset = asset;
		}
		if (!emitter.asset)
			m_actor_loops.erase(found);
	}

	void EqSound::RemoveActorLoop(uint32_t key)
	{
		SetActorLoop(key, {}, 0, 0);
	}

	// ---------------------------------------------------------------- doors

	void EqSound::LoadDoorSounds(int type)
	{
		auto load = [this](int open_slot, const char *open, int close_slot, const char *close) {
			if (open)
				WaveLoad(open_slot, open);
			if (close)
				WaveLoad(close_slot, close);
		};
		switch (type) {
		case 1: case 6: load(0xb0, "DoorMt_O.WAV", 0xaf, "DoorMt_C.WAV"); break;
		case 2: case 7: case 8: case 140: case 145: case 146: case 150: case 151: case 152:
			load(0xb3, "DoorSt_O.WAV", 0xb2, "DoorSt_C.WAV");
			break;
		case 10: case 11: case 12: case 15: case 16: case 17: case 20: case 21: case 22: case 25: case 26: case 27:
		case 62: case 67: case 72: case 77: case 78: case 79: case 80: case 81:
			load(0xb8, "SlDorStO.WAV", 0xb7, "SlDorStC.WAV");
			break;
		case 30: load(0xad, "Dbrdg_Lp.WAV", 0xae, "DbrdgStp.WAV"); break;
		case 35: load(0xbd, "TrapDoor.WAV", 0, nullptr); break;
		case 40: load(0xb4, "Lever.WAV", 0, nullptr); break;
		case 59: load(0xbd, "ElevLoop.wav", 0, nullptr); break; // the trapdoor's slot too: the last loaded wins
		case 60: case 61: case 65: case 66: case 70: case 71: case 75: case 76:
			load(0xb5, "PortC_Lp.WAV", 0xb6, "PortcStp.WAV");
			break;
		case 110: load(0xac, "DartShot.WAV", 0, nullptr); break;
		case 120: load(0xbb, "SpearDn.WAV", 0, nullptr); break;
		case 125: load(0xbc, "SpearUp.WAV", 0, nullptr); break;
		default: break;
		}
	}

	int EqSound::DoorSound(int type, int state)
	{
		bool moving = state == 2 || state == 3;
		switch (type) {
		case 0: case 5: return state == 2 ? 0x87 : state == 3 ? 0x88 : 0; // DoorWd_O, DoorWd_C
		case 1: case 6: return state == 2 ? 0xb0 : state == 3 ? 0xaf : 0;
		case 2: case 7: case 8: case 140: case 145: case 146: case 150: case 151: case 152:
			return state == 2 ? 0xb3 : state == 3 ? 0xb2 : 0;
		case 10: case 11: case 12: case 15: case 16: case 17: case 20: case 21: case 22: case 25: case 26: case 27:
		case 78: case 79: case 80: case 81:
			return state == 2 ? 0xb8 : state == 3 ? 0xb7 : 0;
		case 62: case 67: case 72: case 77: // sliding, and the portcullis' stop once shut
			return state == 2 ? 0xb8 : state == 3 ? 0xb7 : state == 0 ? 0xb6 : 0;
		case 30: return moving ? 0xad : state == 0 ? 0xae : 0;
		case 35: return state == 2 ? 0x87 : state == 3 ? 0xbd : 0;
		case 40: return moving ? 0xb4 : 0;
		case 59: return moving ? 0xb9 : 0; // a slot nothing loads
		case 60: case 61: case 65: case 66: case 70: case 71: case 75: case 76:
			return moving ? 0xb5 : state == 0 ? 0xb6 : 0;
		case 110: return state == 2 ? 0xac : 0;
		case 115: case 116: case 130: case 135: return moving ? 0x77 : 0; // SwingBig
		case 120: return state == 2 ? 0xbb : 0;
		case 125: return state == 2 ? 0xbc : 0;
		default: return 0;
		}
	}
}
