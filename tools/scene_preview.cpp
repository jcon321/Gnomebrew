// scene_preview (Linux): renders the 3D view to PNG files without a window, to
// check the zone and model loaders and the rasterizer.
//
//   scene_preview EQDIR OUT.png [--zone NAME] [--at X Y Z] [--heading H] [--pitch P] [--frames N]
//   scene_preview EQDIR OUT.png --char RACE GENDER CLASS [--face F] [--equip M,M,...] [--camera H] [--frames N]
//
// The first form flies a free camera through a zone: X Y Z are server
// coordinates (/loc order is Y X Z), heading is 0..512. The second renders the
// character select scene for a race/gender/class, with the camera at heading H.
// --frames renders N times and prints the average frame time.

#include "charselect_view.h"
#include "png_writer.h"
#include "rasterizer.h"
#include "zone_scene.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

namespace
{
	int CharacterSelect(const std::string &eq_dir, const std::string &out, const render::CharacterAppearance &appearance,
		uint8_t class_id, float camera_heading, int frames)
	{
		auto t0 = std::chrono::steady_clock::now();
		CharacterSelectView view(eq_dir);
		view.StartLoading();
		while (!view.Loaded())
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		if (!view.Error().empty()) {
			fprintf(stderr, "%s\n", view.Error().c_str());
			return 1;
		}
		printf("loaded %s in %.0f ms\n", view.ZoneName().c_str(),
			std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());

		constexpr int Width = 640, Height = 480;
		std::vector<uint32_t> pixels(Width * Height);
		Canvas canvas(pixels.data(), Width, Height, Width);
		view.SetRotate(false);
		view.SetCharacter(&appearance, class_id);
		double total = 0;
		for (int f = 0; f < frames; ++f) {
			view.SetCameraHeading(camera_heading);
			auto start = std::chrono::steady_clock::now();
			view.Render(canvas, 1000 + f);
			total += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
			if (f == 0 && view.Model())
				printf("model %s: %zu triangles, z %.2f..%.2f, head %.2f\n", view.Model()->Code().c_str(),
					view.Model()->TriangleCount(), view.Model()->MinZ(), view.Model()->MaxZ(), view.Model()->HeadZ());
		}
		printf("%zu triangles drawn, %.1f ms per frame\n", view.TrianglesDrawn(), total / frames);
		if (!WritePng(out, pixels, Width, Height)) {
			fprintf(stderr, "couldn't write %s\n", out.c_str());
			return 1;
		}
		printf("wrote %s\n", out.c_str());
		return 0;
	}
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: %s EQDIR OUT.png [--zone NAME] [--at X Y Z] [--heading H] [--pitch P] [--frames N]\n", argv[0]);
		return 2;
	}
	std::string eq_dir = argv[1], out = argv[2], zone = "load2";
	float server_x = 1475, server_y = 600, server_z = -175, heading = 256, pitch = 0;
	int frames = 1;
	bool character = false;
	render::CharacterAppearance appearance;
	int class_id = 1;
	float camera_heading = 0;
	for (int i = 3; i < argc; ++i) {
		std::string arg = argv[i];
		if (arg == "--char" && i + 3 < argc) {
			character = true;
			appearance.race = static_cast<uint16_t>(atoi(argv[++i]));
			appearance.gender = static_cast<uint8_t>(atoi(argv[++i]));
			class_id = atoi(argv[++i]);
		}
		else if (arg == "--face" && i + 1 < argc)
			appearance.face = static_cast<uint8_t>(atoi(argv[++i]));
		else if (arg == "--equip" && i + 1 < argc) {
			std::string list = argv[++i];
			for (int slot = 0; slot < render::EquipSlots && !list.empty(); ++slot) {
				appearance.equip[slot] = static_cast<uint32_t>(atoi(list.c_str()));
				size_t comma = list.find(',');
				list = comma == std::string::npos ? "" : list.substr(comma + 1);
			}
		}
		else if (arg == "--camera" && i + 1 < argc)
			camera_heading = static_cast<float>(atof(argv[++i]));
		else if (arg == "--zone" && i + 1 < argc)
			zone = argv[++i];
		else if (arg == "--at" && i + 3 < argc) {
			server_x = static_cast<float>(atof(argv[++i]));
			server_y = static_cast<float>(atof(argv[++i]));
			server_z = static_cast<float>(atof(argv[++i]));
		}
		else if (arg == "--heading" && i + 1 < argc)
			heading = static_cast<float>(atof(argv[++i]));
		else if (arg == "--pitch" && i + 1 < argc)
			pitch = static_cast<float>(atof(argv[++i]));
		else if (arg == "--frames" && i + 1 < argc)
			frames = atoi(argv[++i]);
	}

	if (character)
		return CharacterSelect(eq_dir, out, appearance, static_cast<uint8_t>(class_id), camera_heading, frames);

	auto t0 = std::chrono::steady_clock::now();
	render::ZoneScene scene;
	std::string error;
	if (!scene.Load(eq_dir, zone, error)) {
		fprintf(stderr, "%s\n", error.c_str());
		return 1;
	}
	auto t1 = std::chrono::steady_clock::now();
	printf("loaded %s: %zu triangles, %zu objects in %.0f ms\n", zone.c_str(), scene.TriangleCount(), scene.ObjectCount(),
		std::chrono::duration<double, std::milli>(t1 - t0).count());
	for (const auto &warning : scene.Warnings())
		printf("  %s\n", warning.c_str());

	constexpr int Width = 640, Height = 480;
	render::Camera camera;
	camera.position = {server_y, server_x, server_z}; // WLD x/y are the server's y/x
	float yaw = render::HeadingToRadians(heading), tilt = pitch * render::Pi / 180;
	camera.forward = {std::cos(yaw) * std::cos(tilt), std::sin(yaw) * std::cos(tilt), std::sin(tilt)};

	render::Rasterizer rasterizer;
	std::vector<uint32_t> pixels(Width * Height);
	double total = 0;
	for (int f = 0; f < frames; ++f) {
		auto start = std::chrono::steady_clock::now();
		rasterizer.Begin(Width, Height, camera);
		scene.Draw(rasterizer, 0);
		rasterizer.Finish(pixels.data(), Width, 0x203040);
		total += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
	}
	printf("%zu triangles drawn, %.1f ms per frame\n", rasterizer.TriangleCount(), total / frames);
	if (!WritePng(out, pixels, Width, Height)) {
		fprintf(stderr, "couldn't write %s\n", out.c_str());
		return 1;
	}
	printf("wrote %s\n", out.c_str());
	return 0;
}
