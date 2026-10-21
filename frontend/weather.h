#pragma once

// CDisplay's weather (StartWeather @004abf73 on): its sound, and what it tells the 3D view to draw.
//
// OP_Weather is kept until RealRender_World next draws the world, which hands it to do_snow and
// do_rain: type 1 snows and stops any rain, anything else rains and stops any snow, and intensity
// 0 stops it. They StartWeather or StopWeather with a fade, ProcessCloud's, a step a frame in an
// outdoor zone (time type 1, 2 or 5) only: the cloud comes in 2/256 a frame and goes 3/256 a frame.
// Once it's in, rain starts its loop (EqSoundManager::EmitterSetRaining) with "It begins to
// rain.", or snow the wind (EmitterSetWind) with "It begins to snow."; once it's gone they stop.
// Rain that's still coming in can't be stopped, nor snow. While it rains, LightningStrike: thunder1
// or thunder2.wav as it comes in, then every 10 to 20 seconds, and three times in ten the other one
// within a second after. StopWorldDisplay (leaving the zone) kills it: no fade, the loops off at once.
//
// What it draws (Effects): from StartWeather until it's gone the world is drawn without the sky
// (RealRender_World), and the fog is the weather's (SetTimedFog). ProcessCloud's steps move it: in,
// the fog's start by its start / 128 times the Clip Plane a step (to nothing once under 100) and its
// end by what's past 550 / 128 times the Clip Plane while it's past 550; out, back by as much a
// step for the fewer steps going takes (so not all the way: the zone's own fog comes back once
// it's gone). SetTimedFog keeps the start at 30 or more and the end at 105 or more. Once the rain
// is in, its particle clouds (t3dCreateParticleCloud, red in the Plane of Fear: t3dSetRainColor)
// until it's gone (t3dStopPCloudAndAutoDestroy: those falling finish); likewise the snow's. A
// thunder clap flashes the frame it sounds (SetAmbientLight: the ambient light at 0.99, the fog no
// darker than our floor's exposure).

#include <cstdint>

namespace audio
{
	class EqSound;
}

class Weather
{
public:
	Weather();

	// OP_Weather.
	void Receive(uint32_t type, int32_t intensity);
	struct Place
	{
		bool outdoors = false; // time type 1, 2 or 5
		uint32_t zone_id = 0;  // ours (the Plane of Fear rains blood)
		float fog_start = 0, fog_end = 0; // the zone's fog as it is without weather
		float clip_plane = 1;  // the options' Clip Plane slider
	};
	// RealRender_World, each frame in the world: the packet that came, and the weather's fade or
	// thunder. Returns the eqstr id of what to say in the chat (it begins to rain or snow), or 0.
	int Frame(audio::EqSound *sound, uint64_t now_ms, const Place &place);
	// StopWorldDisplay: "kill" to do_snow and do_rain.
	void Kill(audio::EqSound *sound);

	enum class Kind { None = 0, Rain = 1, Snow = 2 };
	// What the 3D view draws of it this frame.
	struct Effects
	{
		bool active = false;              // no sky, and the weather's fog
		float fog_start = 0, fog_end = 0; // when active
		Kind clouds = Kind::None;         // the particle clouds to have (going on falling once they're stopped)
		bool blood = false;               // the rain's color red (t3dSetRainColor)
		bool flash = false;               // lightning
	};
	Effects GetEffects() const;

	// For the debug menu.
	struct Stats
	{
		Kind kind = Kind::None;  // what's coming, here or going
		bool active = false;     // coming or here (RealRender_World runs it)
		bool changing = false;   // coming in or going (ProcessCloud) rather than here (LightningStrike)
		bool arriving = false;   // coming rather than going
		int cloud = 0;           // the fade, of 256
		int next_strike_ms = 0;  // when LightningStrike comes round again
		bool received = false;   // any OP_Weather yet
		uint32_t type = 0;       // the last one's
		int32_t intensity = 0;
	};
	Stats GetStats() const;

private:
	void Rain(int32_t intensity); // do_rain
	void Snow(int32_t intensity); // do_snow
	void StartWeather(Kind kind, bool fade);
	void StopWeather(Kind kind, bool fade);
	void StartWeather2();
	void ProcessCloud();
	void LightningStrike();
	void SetRaining(bool on);
	void SetWind(bool on);
	void SetFog(float start, float end); // CDisplay::SetFog, while the weather's on
	int Rand(); // the MSVC runtime's rand(), standing in for RandNbr too

	audio::EqSound *m_sound = nullptr; // the frame's
	uint64_t m_now_ms = 0;
	Place m_place;
	int m_message = 0;

	bool m_pending = false; // OP_Weather, waiting for RealRender_World
	uint32_t m_pending_type = 0;
	int32_t m_pending_intensity = 0;
	bool m_received = false;
	uint32_t m_last_type = 0;
	int32_t m_last_intensity = 0;

	Kind m_kind = Kind::None;
	bool m_active = false, m_changing = false, m_arriving = false;
	int m_cloud = 0;
	uint64_t m_cloud_ms = 0, m_strike_ms = 0;
	bool m_raining = false, m_snowing = false; // come in, and not yet gone
	int m_follow = 0;       // the thunder to answer within a second, or 0
	int m_last_thunder = 0; // the wave slot LightningStrike played last
	float m_fog_start = 0, m_fog_end = 0;           // CDisplay's (0xac, 0xb0) while the weather's on
	float m_fog_step_start = 0, m_fog_step_end = 0; // ProcessCloud's steps
	Kind m_clouds = Kind::None;
	bool m_blood = false, m_flash = false;
	uint32_t m_random;
};
