#include "point_lights.h"

#include <algorithm>
#include <cstring>

namespace render
{
	namespace
	{
		// InitCommonLights' colors (red, green, blue), each frame shown for 200 ms.
		const Vec3 Torch[] = {{0.996f, 0.996f, 0}, {0.957f, 0.957f, 0}, {0.996f, 0.996f, 0}, {0.996f, 0.881f, 0},
			{0.996f, 0.996f, 0}, {0.996f, 0.996f, 0.075f}, {0.996f, 0.996f, 0}, {0.996f, 0.803f, 0}, {0.996f, 0.996f, 0},
			{0.996f, 0.996f, 0.075f}};
		const Vec3 Magic[] = {{0.4f, 0.996f, 0.996f}, {0.4f, 0.957f, 0.957f}, {0.4f, 0.996f, 0.996f},
			{0.4f, 0.901f, 0.996f}, {0.4f, 0.996f, 0.996f}, {0.4f, 0.996f, 0.996f}, {0.4f, 0.996f, 0.996f},
			{0.4f, 0.903f, 0.996f}, {0.4f, 0.996f, 0.996f}, {0.4f, 0.996f, 0.996f}};
		const Vec3 Lantern{0.996f, 0.996f, 0.4f};
		const Vec3 Red{0.5f, 0.26f, 0};
		const Vec3 Blue{0.16f, 0.2f, 0.5f};
	}

	bool CarriedLight(uint8_t light_type, const char *zone, uint64_t now_ms, Vec3 &color, float &radius)
	{
		// CreateLight: torch and candle, magtorch (one-frame smmagic and lgmagic), smlantern and
		// lglantern, redLight and blueLight, with their radii.
		size_t frame = static_cast<size_t>(now_ms / 200 % 10);
		switch (light_type) {
		case 1: color = Torch[frame]; radius = 25; break;   // candle
		case 2: color = Torch[frame]; radius = 100; break;  // torch
		case 3:
		case 9: color = Magic[frame]; radius = 100; break;  // magtorch
		case 4: color = Lantern; radius = 150; break;       // smlantern
		case 5:
		case 10: color = Magic[0]; radius = 150; break;     // smmagic
		case 6: color = Lantern; radius = 200; break;       // lglantern
		case 7:
		case 11: color = Magic[0]; radius = 200; break;     // lgmagic
		case 8: color = Lantern; radius = 400; break;       // lglantern, further
		case 12:
		case 14: color = Red; radius = 150; break;
		case 13:
		case 15: color = Blue; radius = 150; break;
		default: return false;
		}
		// CEverQuest_GetMaxLightRadius: Crystal Caverns (121) and Vex Thal (158).
		if (zone && std::strcmp(zone, "crystal") == 0)
			radius = std::min(radius, 25.0f);
		else if (zone && std::strcmp(zone, "vexthal") == 0)
			radius = std::min(radius, 100.0f);
		return true;
	}

	bool CarriedLightShinesInWater(uint8_t light_type)
	{
		return light_type == 3 || light_type == 5 || light_type == 7 || light_type > 8;
	}

	void SelectLights(const std::vector<PointLight> &lights, const std::vector<std::vector<int>> &light_regions,
		const Vec3 &center, int region, size_t count, std::vector<PointLight> &out)
	{
		out.clear();
		std::vector<std::pair<float, size_t>> ranked;
		for (size_t i = 0; i < lights.size(); ++i) {
			const PointLight &light = lights[i];
			Vec3 d = light.position - center;
			float distance2 = Dot(d, d);
			bool reaches = region >= 0 && i < light_regions.size()
			                   ? std::binary_search(light_regions[i].begin(), light_regions[i].end(), region)
			                   : distance2 <= light.radius * light.radius;
			if (reaches)
				ranked.push_back({light.radius * light.radius * 0.05f / (distance2 + 1), i});
		}
		std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
		for (size_t i = 0; i < ranked.size() && i < count; ++i)
			out.push_back(lights[ranked[i].second]);
	}
}
