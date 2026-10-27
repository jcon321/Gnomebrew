#pragma once

// The lights that light characters (and the carried ones the zone too), as the client has them.
//
// A light reaches what EQGfx's processPhysicsEffects (@10050b50) is drawing when its flags share
// a bit with the mask: 0x30000000 for characters and meshes without baked colors, 0x20000000 for
// meshes with them. The zone's own lights (lights.wld, flag 0x100) get 0x10000000 from
// createPointLightFromFileContext, so they light characters but not the zone, whose colors they're
// baked into. The light a spawn carries (its LightType, Spawn_Struct byte 94: an equipped torch,
// lantern or lightstone) gets both from CDisplay::CreateLight (@004af3d5), which CDisplay::ChangeLight
// (@004af2a3) calls with a light CDisplay::InitCommonLights (@004a3d65) defines, 2 units in front of
// the spawn and 4 over its center, where CDisplay::MoveLight keeps it.
//
// A character's vertex light is then its ambient plus, for each of the 3 lights brightest at its
// center (0.05 radius^2 / (distance^2 + 1)) among those whose sphere reaches its region, the light's
// color x 0.05 radius^2 x cos / distance^2 (applyPhysicsAndLightingEffects, calculateMeshLighting).

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
	// CEverQuest_GetMaxLightRadius: Crystal Caverns 25, Vex Thal 100, elsewhere no cap (10000).
	float MaxLightRadius(const char *zone);
	// ChangeLight: in water only the magic lights shine (types 3, 5, 7 and over 8).
	bool CarriedLightShinesInWater(uint8_t light_type);

	// The lights that reach something at `center` in `region` (-1: unknown, by distance alone),
	// brightest first, at most `count` (processPhysicsEffects).
	void SelectLights(const std::vector<PointLight> &lights, const std::vector<std::vector<int>> &light_regions,
		const Vec3 &center, int region, size_t count, std::vector<PointLight> &out);
}
