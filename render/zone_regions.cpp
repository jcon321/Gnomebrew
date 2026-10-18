#include "zone_regions.h"

#include <algorithm>
#include <cstring>

namespace render
{
	namespace
	{
		// The prefixes GetEnvironment checks, in its order.
		bool Known(const std::string &type, Environment &environment)
		{
			static const struct
			{
				const char *prefix;
				Environment environment;
			} prefixes[] = {
				{"DR", Environment::Dry}, {"WT", Environment::Water}, {"LA", Environment::Lava}, {"SL", Environment::Slime},
				{"VW", Environment::VWater}, {"W2", Environment::Water2}, {"W3", Environment::Water3},
			};
			for (const auto &p : prefixes) {
				if (std::strncmp(type.c_str(), p.prefix, 2) == 0) {
					environment = p.environment;
					return true;
				}
			}
			return false;
		}
	}

	void ZoneRegions::Load(const WldRegionMap &map)
	{
		m_nodes = map.nodes;
		m_types.assign(map.region_count, {});
		for (const auto &group : map.groups) {
			const std::string &type = group.name.size() < 20 && !group.user_data.empty() ? group.user_data : group.name;
			for (uint32_t region : group.regions)
				if (region < m_types.size())
					m_types[region].push_back(type);
		}
	}

	int ZoneRegions::RegionAt(const Vec3 &point) const
	{
		// t3dGetRegionNumberFromWorldAndXYZ: down the tree from the first node.
		int32_t index = m_nodes.empty() ? 0 : 1;
		while (index >= 1 && index <= static_cast<int32_t>(m_nodes.size())) {
			const WldRegionMap::Node &node = m_nodes[index - 1];
			if (node.region != 0)
				return node.region - 1;
			float side = point.x * node.normal.x + point.y * node.normal.y + point.z * node.normal.z;
			index = side < -node.distance ? node.back : node.front;
		}
		return -1;
	}

	std::vector<int> ZoneRegions::RegionsTouched(const Vec3 &center, float radius) const
	{
		// Down the front of each split, and the back too when the sphere crosses it.
		std::vector<int> regions;
		std::vector<int32_t> pending;
		if (!m_nodes.empty())
			pending.push_back(1);
		while (!pending.empty()) {
			int32_t index = pending.back();
			pending.pop_back();
			if (index < 1 || index > static_cast<int32_t>(m_nodes.size()))
				continue;
			const WldRegionMap::Node &node = m_nodes[index - 1];
			if (node.region != 0) {
				regions.push_back(node.region - 1);
				continue;
			}
			float side = center.x * node.normal.x + center.y * node.normal.y + center.z * node.normal.z + node.distance;
			if (side < -radius) {
				pending.push_back(node.back);
				continue;
			}
			if (side <= radius)
				pending.push_back(node.back);
			pending.push_back(node.front);
		}
		std::sort(regions.begin(), regions.end());
		regions.erase(std::unique(regions.begin(), regions.end()), regions.end());
		return regions;
	}

	const std::vector<std::string> &ZoneRegions::Types(int region) const
	{
		static const std::vector<std::string> none;
		return region >= 0 && region < static_cast<int>(m_types.size()) ? m_types[region] : none;
	}

	Environment ZoneRegions::EnvironmentAt(const Vec3 &point) const
	{
		for (const std::string &type : Types(RegionAt(point))) {
			Environment environment;
			if (Known(type, environment))
				return environment;
		}
		return Environment::Dry;
	}

	ZoneRegions::Feet ZoneRegions::FeetAt(const Vec3 &point) const
	{
		Feet feet;
		for (const std::string &type : Types(RegionAt(point))) {
			Known(type, feet.environment);
			if (type.size() > 4 && type[3] == 'T' && type[4] == 'P')
				feet.zone_line = type;
			if (type.size() > 32 && type[32] == 'S')
				feet.slippery = true;
		}
		return feet;
	}
}
