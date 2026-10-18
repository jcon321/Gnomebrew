#pragma once

// The lights that light characters, as the client has them.
//
// The zone's own lights (lights.wld) don't: they come with flags 0x100 and no scene influence,
// and EQGfx's processPhysicsEffects (@10050b50) only takes the lights whose flags share a bit
// with 0x30000000 (their light is baked into the zone's and the objects' vertex colors instead).
// What does is the light a spawn carries: its LightType (Spawn_Struct byte 94, an equipped
// torch, lantern or lightstone), which CDisplay::ChangeLight (@004af2a3) makes with
// CDisplay::CreateLight (@004af3d5) from the lights CDisplay::InitCommonLights (@004a3d65)
// defines, 2 units in front of the spawn and 4 over its center, and CDisplay::MoveLight keeps there.
//
// A vertex's light is then (calculateMeshLighting, @1004da40) its ambient plus, for each of the 3
// lights brightest at the actor's center (0.05 radius^2 / (distance^2 + 1)) among those whose
// sphere reaches its region, the light's color x 0.05 radius^2 x cos / distance^2.

#include "math3d.h"

#include <cstdint>
#include <vector>

namespace render
{
	class ZoneRegions;

	struct PointLight
	{
		Vec3 position;
		Vec3 color;        // this frame's
		float radius = 0;  // of influence
		Vec3 Strength() const { return color * (radius * radius * 0.05f); } // x cos / distance^2
	};

	// The light a LightType carries at `now_ms` (the torches flicker through 10 colors, 200 ms
	// each), its radius capped as CEverQuest_GetMaxLightRadius caps it in `zone`; false for none.
	bool CarriedLight(uint8_t light_type, const char *zone, uint64_t now_ms, Vec3 &color, float &radius);
	// ChangeLight: in water only the magic lights shine (types 3, 5, 7 and over 8).
	bool CarriedLightShinesInWater(uint8_t light_type);

	// The lights that reach something at `center` in `region` (-1: unknown, by distance alone),
	// brightest first, at most `count` (processPhysicsEffects).
	void SelectLights(const std::vector<PointLight> &lights, const std::vector<std::vector<int>> &light_regions,
		const Vec3 &center, int region, size_t count, std::vector<PointLight> &out);
}
