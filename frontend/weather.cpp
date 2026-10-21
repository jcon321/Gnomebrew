#include "weather.h"

#include "eq_sound.h"

#include <algorithm>
#include <ctime>

namespace
{
	constexpr int Thunder1 = 0x8f, Thunder2 = 0x90; // thunder1.wav, thunder2.wav
	constexpr uint32_t PlaneOfFear = 72;
}

Weather::Weather() : m_random(static_cast<uint32_t>(time(nullptr))) {}

int Weather::Rand()
{
	m_random = m_random * 214013u + 2531011u;
	return static_cast<int>((m_random >> 16) & 0x7fff);
}

void Weather::Receive(uint32_t type, int32_t intensity)
{
	m_pending = true;
	m_pending_type = type;
	m_pending_intensity = intensity;
	m_received = true;
	m_last_type = type;
	m_last_intensity = intensity;
}

int Weather::Frame(audio::EqSound *sound, uint64_t now_ms, const Place &place)
{
	m_sound = sound;
	m_now_ms = now_ms;
	m_place = place;
	m_message = 0;
	m_flash = false; // the frame after
	if (m_pending) {
		m_pending = false;
		if (m_pending_type == 1) {
			Snow(m_pending_intensity);
			Rain(0);
		}
		else {
			Rain(m_pending_intensity);
			Snow(0);
		}
	}
	if (place.outdoors && m_active) {
		StartWeather2();
		// SetTimedFog: no nearer than 30, and out to 105 at least.
		SetFog(std::max(m_fog_start, 30.0f), std::max(m_fog_end, 105.0f));
	}
	return m_message;
}

void Weather::SetFog(float start, float end)
{
	if (start < 0.1f && end < 0.1f) {
		start = m_fog_end - 50;
		end = m_fog_end - 1;
	}
	start = std::max(start, 0.0f);
	if (end < start)
		end = start + 50;
	m_fog_start = start;
	m_fog_end = end;
}

Weather::Effects Weather::GetEffects() const
{
	Effects effects;
	effects.active = m_active && m_place.outdoors;
	effects.fog_start = m_fog_start;
	effects.fog_end = m_fog_end;
	effects.clouds = m_clouds;
	effects.blood = m_blood;
	effects.flash = m_flash;
	return effects;
}

void Weather::Kill(audio::EqSound *sound)
{
	m_sound = sound;
	m_clouds = Kind::None; // t3dDestroyParticleCloud: they go with the zone
	if (m_snowing) {
		StopWeather(Kind::Snow, false);
		m_snowing = false;
	}
	if (m_raining) {
		StopWeather(Kind::Rain, false);
		m_raining = false;
	}
}

void Weather::Rain(int32_t intensity)
{
	if (intensity == 0) {
		if (m_raining)
			StopWeather(Kind::Rain, true);
	}
	else if (!m_raining)
		StartWeather(Kind::Rain, true);
}

void Weather::Snow(int32_t intensity)
{
	if (intensity == 0) {
		if (m_snowing)
			StopWeather(Kind::Snow, true);
	}
	else if (!m_snowing)
		StartWeather(Kind::Snow, true);
}

void Weather::StartWeather(Kind kind, bool fade)
{
	if (!m_active) { // the fog it moves is the zone's as it is
		m_fog_start = m_place.fog_start;
		m_fog_end = m_place.fog_end;
	}
	m_kind = kind;
	m_active = m_changing = m_arriving = true;
	m_cloud = 0;
	m_cloud_ms = m_strike_ms = m_now_ms;
	m_follow = 0;
	if (!fade) {
		m_cloud = 256;
		StartWeather2();
	}
}

void Weather::StopWeather(Kind kind, bool fade)
{
	m_kind = kind;
	m_changing = true;
	m_arriving = false;
	m_cloud = m_cloud < 0 || m_cloud == 256 ? 0 : 256 - m_cloud; // going from as far as it came
	if (!fade) {
		SetRaining(false);
		SetWind(false);
		m_cloud = 256;
		StartWeather2();
	}
}

void Weather::StartWeather2()
{
	if (m_changing)
		ProcessCloud();
	else
		LightningStrike();
}

void Weather::ProcessCloud()
{
	if (m_cloud < 256) {
		// A step a frame (at most one a millisecond), as the fog closes in or opens out.
		if (m_now_ms < m_cloud_ms)
			return;
		int step;
		if (m_arriving) {
			m_cloud_ms = m_now_ms + 1;
			step = 2;
			if (m_cloud > 255)
				m_cloud = 255;
		}
		else {
			m_cloud_ms = m_now_ms - 1;
			step = 3;
			if (m_cloud > 256)
				m_cloud = 256;
		}
		m_cloud += step;
		// The fog closes in, or opens out again.
		float start, end;
		if (m_arriving) {
			float steps = 256.0f / static_cast<float>(step);
			if (256 - m_cloud == 254) { // the first step: a 128th of the way each, times the clip plane
				m_fog_step_start = m_fog_start / steps * m_place.clip_plane;
				m_fog_step_end = m_fog_end > 550 ? (m_fog_end - 550) / steps * m_place.clip_plane : 0;
			}
			start = 0;
			if (m_fog_start > 0) {
				start = m_fog_start - m_fog_step_start;
				if (start < 100)
					start = 0;
			}
			else
				m_fog_start = 0;
			end = m_fog_end > 550 ? m_fog_end - m_fog_step_end : m_fog_end;
		}
		else {
			if (m_cloud == 1) {
				m_fog_step_start *= 1.5f;
				m_fog_step_end *= 1.5f;
			}
			start = m_fog_start + m_fog_step_start;
			end = m_fog_end + m_fog_step_end;
		}
		SetFog(start, end);
		return;
	}
	if (!m_arriving) {
		m_active = false;
		if (m_kind == Kind::Rain) {
			m_raining = false;
			SetRaining(false);
			if (m_clouds == Kind::Rain)
				m_clouds = Kind::None; // t3dStopPCloudAndAutoDestroy
		}
		else if (m_kind == Kind::Snow) {
			m_snowing = false;
			SetWind(false);
			if (m_clouds == Kind::Snow)
				m_clouds = Kind::None;
		}
	}
	else if (m_kind == Kind::Rain) {
		m_blood = m_place.zone_id == PlaneOfFear;
		m_message = m_blood ? 540 : 541; // "It begins to rain blood." / "It begins to rain."
		m_raining = true;
		SetRaining(true);
		m_clouds = Kind::Rain; // t3dCreateParticleCloud
	}
	else if (m_kind == Kind::Snow) {
		m_message = 542; // "It begins to snow."
		m_snowing = true;
		SetWind(true);
		m_clouds = Kind::Snow;
	}
	m_changing = false;
}

void Weather::LightningStrike()
{
	if (m_now_ms < m_strike_ms)
		return;
	if (m_kind == Kind::Rain) {
		// Either one, or the other of the one before (and the sky flashes).
		bool second = m_follow == 0 ? Rand() % 100 + 1 > 49 : m_follow == Thunder1;
		m_last_thunder = second ? Thunder2 : Thunder1;
		if (m_sound)
			m_sound->PlayWeatherWave(m_last_thunder);
		m_flash = true;
	}
	else if (m_kind == Kind::Snow)
		SetWind(true);
	if (m_follow == 0)
		m_follow = Rand() % 100 > 29 ? 0 : m_last_thunder & 0xff;
	else
		m_follow = 0;
	int delay = m_follow == 0 ? Rand() % 10000 + 10000 : Rand() % 1000;
	m_strike_ms = m_now_ms + static_cast<uint64_t>(delay);
}

void Weather::SetRaining(bool on)
{
	if (m_sound)
		m_sound->SetRaining(on);
}

void Weather::SetWind(bool on)
{
	if (m_sound)
		m_sound->SetWind(on);
}

Weather::Stats Weather::GetStats() const
{
	Stats stats;
	stats.kind = m_kind;
	stats.active = m_active;
	stats.changing = m_changing;
	stats.arriving = m_arriving;
	stats.cloud = m_cloud;
	stats.next_strike_ms = m_strike_ms > m_now_ms ? static_cast<int>(m_strike_ms - m_now_ms) : 0;
	stats.received = m_received;
	stats.type = m_last_type;
	stats.intensity = m_last_intensity;
	return stats;
}
