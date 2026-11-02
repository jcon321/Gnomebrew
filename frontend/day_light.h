#pragma once

// The light of the time of day, as eqgame's CDisplay sets it for the world (the zone and character
// select alike):
//   - CDisplay::SetAmbientLight (@004b234b): the ambient light's red, green and blue for the hour,
//     moving toward the next hour's minute by minute; 0.1 where time doesn't pass (0.5 for time
//     type 4); each channel then between CDisplay's minimum ambient (0.18) and 0.99.
//   - CDisplay::SetSunLight (@004b18b1): the sun, a directional light the color of the hour's
//     ambient (unclamped), none where time doesn't pass.
//   - SetSkyBackground / ProcessSky: the sky's brightness by the day period.
// Time passes (CDisplay_GetGameTimeInfo) for zone time types 1, 2 and 5; CEverQuest's constructor
// starts it at 1, which is what character select has.

#include "math3d.h"

#include <cstdint>

struct DayLight
{
	render::Vec3 ambient{0.6f, 0.6f, 0.6f}; // added to the zone's baked lighting
	float sky = 1;                           // the sky's brightness
	render::Vec3 sun_direction{0, 0, -1};    // the way the sun shines (SetSunLight)
	render::Vec3 sun_color;                  // 0 where the time of day doesn't pass
	// Infravision and ultravision (SetAmbientLight): the world's special ambient, added to
	// everything, and the glow infravision gives warm-blooded spawns at night.
	render::Vec3 special, glow;
};

// The light at `hour` (1..24) and `minute` in a zone of `time_type` (EQZoneInfo's), before vision
// and weather.
DayLight TimeOfDayLight(int hour, float minute, int time_type);

// EQ_Character's Vision1 (infravision) and Vision2 (ultravision), which win over Vision1.
enum class Vision : uint8_t { None, Infravision, Ultravision };
// EQ_Character::InitInnates: the dark elf sees in ultravision; the human, barbarian and erudite in
// neither; everyone else in infravision.
Vision RaceVision(uint16_t race);
// SetAmbientLight's vision: the ambient raised to its color (SetDefaultGameValues' and
// CEverQuest_EnterZone2's, dimmer in Vex Thal), which is also added as the world's special ambient
// the less `exposure` (the camera's actor's, followed) there is. Infravision's glow (`glow`: on while
// the sky's light is under 0.53, off over 0.7) is a constant red on the warm-blooded (CDisplay's 0x2e10).
void ApplyVision(DayLight &light, Vision vision, float exposure, bool glow, bool vexthal = false);
