#include "day_light.h"

#include <algorithm>
#include <cmath>

namespace
{
	// CDisplay::SetAmbientLight: the ambient light's red, green and blue for each hour 1..24
	// (eqgame.exe 0x621be8, 0x621c48, 0x621ca8), between which it moves minute by minute.
	const float AmbientRed[24] = {0, 0, 0, 0.1f, 0.35f, 0.6f, 0.6f, 0.6f, 0.6f, 0.7f, 0.8f, 0.9f, 0.9f, 0.8f, 0.7f, 0.6f, 0.6f,
		0.6f, 0.6f, 0.35f, 0.1f, 0, 0, 0};
	const float AmbientGreen[24] = {0, 0, 0, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 0.9f, 0.8f, 0.7f, 0.6f, 0.5f,
		0.4f, 0.3f, 0.2f, 0.1f, 0, 0, 0};
	const float AmbientBlue[24] = {0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 0.9f, 0.8f, 0.7f,
		0.6f, 0.5f, 0.4f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f};
	// The sky's brightness over its 60 day periods (0x621af8): 14 is noon, 44 the dead of night.
	const float SkyLight[60] = {0.5f, 0.58f, 0.58f, 0.62f, 0.62f, 0.74f, 0.74f, 0.78f, 0.78f, 0.84f, 0.84f, 0.92f, 0.92f, 1, 1,
		0.96f, 0.92f, 0.92f, 0.92f, 0.84f, 0.84f, 0.78f, 0.78f, 0.78f, 0.7f, 0.7f, 0.6f, 0.54f, 0.54f, 0.54f, 0.44f, 0.4f, 0.38f,
		0.38f, 0.3f, 0.28f, 0.24f, 0.2f, 0.18f, 0.14f, 0.1f, 0.08f, 0.04f, 0.02f, 0, 0.02f, 0.04f, 0.08f, 0.1f, 0.14f, 0.18f, 0.2f,
		0.28f, 0.28f, 0.38f, 0.38f, 0.38f, 0.44f, 0.44f, 0.5f};
}

DayLight TimeOfDayLight(int hour, float minute, int time_type)
{
	DayLight light;
	// CDisplay_GetGameTimeInfo: zones that don't follow the time of day are at midnight.
	bool outdoors = time_type == 1 || time_type == 2 || time_type == 5;
	hour = outdoors ? std::clamp(hour, 1, 24) : 0;
	minute = outdoors ? minute : 0;

	if (outdoors) {
		int a = hour - 1, b = hour % 24;
		float t = minute / 60;
		light.ambient = {AmbientRed[a] + (AmbientRed[b] - AmbientRed[a]) * t, AmbientGreen[a] + (AmbientGreen[b] - AmbientGreen[a]) * t,
			AmbientBlue[a] + (AmbientBlue[b] - AmbientBlue[a]) * t};
	}
	else {
		float level = time_type == 4 ? 0.5f : 0.1f;
		light.ambient = {level, level, level};
	}
	// SetSunLight: its angle (ProcessSky / SetSkyBackground) turns 128 every 6 hours: 320 at 3:00,
	// overhead (0) at noon, 256 at midnight; it shines along (0, sin(512 - angle), -cos(512 - angle)).
	if (outdoors) {
		light.sun_color = light.ambient;
		float angle = 320 + ((hour * 60 + minute) - 180) / 360 * 128;
		float a = render::HeadingToRadians(512 - angle);
		light.sun_direction = {0, std::sin(a), -std::cos(a)};
	}
	// SetAmbientLight keeps each channel between CDisplay's minimum ambient (0.18, set at 0x3c by
	// its constructor, which TAKP's list calls CEverQuest::loadOptions @0049ec7e) and 0.99.
	const float least = 0.18f;
	light.ambient = {std::clamp(light.ambient.x, least, 0.99f), std::clamp(light.ambient.y, least, 0.99f),
		std::clamp(light.ambient.z, least, 0.99f)};

	// SetSkyBackground / ProcessSky: the day period, in steps of 12 game minutes. Night until
	// 3:00, dawn to 9:00 (periods 44, 45 .. 59, 0 .. 13), day to 15:00 (14), dusk to 20:48 (14 .. 43).
	float u = (hour * 60 + minute) / 12;
	float period;
	if (u < 15 || u > 104)
		period = 44;
	else if (u < 45)
		period = std::fmod(u + 29, 60.0f);
	else if (u < 75)
		period = 14;
	else
		period = u - 61;
	int p = static_cast<int>(period);
	float f = period - p;
	light.sky = SkyLight[p % 60] + (SkyLight[(p + 1) % 60] - SkyLight[p % 60]) * f;
	return light;
}

Vision RaceVision(uint16_t race)
{
	switch (race) {
	case 1:  // human
	case 2:  // barbarian
	case 3:  // erudite
		return Vision::None;
	case 6:  // dark elf
		return Vision::Ultravision;
	default:
		return Vision::Infravision;
	}
}

void ApplyVision(DayLight &light, Vision vision, float exposure, bool glow, bool vexthal)
{
	if (vision == Vision::None)
		return;
	render::Vec3 color = vision == Vision::Ultravision ? (vexthal ? render::Vec3{25, 25, 50} : render::Vec3{82, 55, 164})
	                                                   : (vexthal ? render::Vec3{25, 13, 13} : render::Vec3{75, 38, 38});
	color = color * (1.0f / 255);
	light.ambient = {std::max(light.ambient.x, color.x), std::max(light.ambient.y, color.y), std::max(light.ambient.z, color.z)};
	light.special = color * (1 - exposure);
	if (vision == Vision::Infravision && glow)
		light.glow = {100 / 256.0f, 0, 0};
}
