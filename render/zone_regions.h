#pragma once

// What a place in the zone is (water, lava, a zone line...), from <zone>.wld's BSP
// tree and region groups (WldRegionMap), read the way eqgame.exe does:
//   CDisplay::GetEnvironment (@004b3eeb)  a point's environment: the first of its region's
//     groups whose type starts with a known prefix (DR dry, WT water, SL slime, LA lava,
//     VW, W2 and W3 more water).
//   CDisplay::MovePlayer (@00504765)       the feet's region: the last known prefix, a zone
//     line when the type has TP at 3 ("DRNTP...", the destination or an OP_SendZonepoints
//     index), and slippery ground with an S at 32.
// A group's type is its name, or its user data when the name is under 20 characters (classic
// zones name them "Z0002_ZONE" and put "WTN__01258000..." in the user data).

#include "math3d.h"
#include "wld.h"

#include <cstdint>
#include <string>
#include <vector>

namespace render
{
	// ActorInfo::SwimmingWaterType's values.
	enum class Environment : uint8_t
	{
		Dry = 0,
		Water = 5,
		Slime = 6,
		Lava = 7,
		VWater = 8,
		Water2 = 9,
		Water3 = 10,
	};
	inline bool Swimming(Environment environment) { return static_cast<uint8_t>(environment) > 4; }

	class ZoneRegions
	{
	public:
		void Load(const WldRegionMap &map);
		bool Empty() const { return m_nodes.empty(); }
		int RegionAt(const Vec3 &point) const; // 0-based; -1 outside every region
		// The regions a sphere reaches (traverseBspTree, as a light is put in them), sorted.
		std::vector<int> RegionsTouched(const Vec3 &center, float radius) const;
		Environment EnvironmentAt(const Vec3 &point) const;

		struct Feet
		{
			Environment environment = Environment::Dry;
			std::string zone_line; // the "DRNTP..." type, empty when none
			bool slippery = false;
		};
		Feet FeetAt(const Vec3 &point) const;
		// A region's group types, for the debug menu.
		const std::vector<std::string> &Types(int region) const;

	private:
		std::vector<WldRegionMap::Node> m_nodes;
		std::vector<std::vector<std::string>> m_types; // per region
	};
}
