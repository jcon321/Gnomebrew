// scene_preview (Linux): renders the 3D view to PNG files without a window, to
// check the zone and model loaders and the rasterizer.
//
//   scene_preview EQDIR OUT.png [--zone NAME] [--at X Y Z] [--heading H] [--pitch P] [--frames N] [--size WxH]
//   scene_preview EQDIR OUT.png --char RACE GENDER CLASS [--face F] [--equip M,M,...] [--camera H] [--frames N] [--size WxH]
//   scene_preview EQDIR OUT.png --player RACE GENDER --zone NAME --at X Y Z [--heading H] [--face F] [--equip M,M,...]
//                 [--orbit H] [--pitch P] [--first-person] [--zoom NOTCHES] [--walk MS [--turn -1|1]]
//                 [--fog START END RRGGBB] [--sky N] [--frames N] [--size WxH]
//
// The first form flies a free camera through a zone: X Y Z are server
// coordinates (/loc order is Y X Z), heading is 0..512. The second renders the
// character select scene for a race/gender/class, with the camera at heading H.
// The third is the in-zone view (ZoneView): that character standing at X Y Z
// (the server's position, model center height) with the chase camera turned
// --orbit from behind them, or first person. --fog and --sky stand in for
// OP_NewZone (default: the client's fallback fog, 500 to 600 in grey, sky 1).
// --walk runs forward that long first (turning left -1 or right 1 with --turn). --npc RACE GENDER
// SIZE X Y Z HEADING (repeatable) stands a spawn there. --time HOUR MINUTE sets Norrath's time (hour
// 1..24, 1 is midnight) for the zone's time type (--time-type, default 2: outdoors).
// --frames renders N times and prints the average frame time. --size picks the
// image size (default 640x480).

#include "charselect_view.h"
#include "zone_view.h"
#include "png_writer.h"
#include "rasterizer.h"
#include "sky.h"
#include "zone_scene.h"

#include <chrono>
#include <map>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

namespace
{
	int CharacterSelect(const std::string &eq_dir, const std::string &out, const render::CharacterAppearance &appearance,
		uint8_t class_id, float camera_heading, int frames, int Width, int Height)
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

		std::vector<uint32_t> pixels(static_cast<size_t>(Width) * Height);
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

	int InZone(const std::string &eq_dir, const std::string &out, const ZoneInfo &info, const render::CharacterAppearance &appearance,
		float x, float y, float z, float heading, float orbit, float pitch, bool first_person, int zoom, int walk_ms, int turn, int frames,
		int Width, int Height, const std::map<uint16_t, ZoneSpawn> &spawns, int game_hour, float game_minute)
	{
		auto t0 = std::chrono::steady_clock::now();
		ZoneView view(eq_dir);
		view.StartLoading(info);
		while (!view.Loaded())
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		if (!view.Error().empty()) {
			fprintf(stderr, "%s\n", view.Error().c_str());
			return 1;
		}
		printf("loaded %s in %.0f ms\n", view.ZoneName().c_str(),
			std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());

		std::vector<uint32_t> pixels(static_cast<size_t>(Width) * Height);
		Canvas canvas(pixels.data(), Width, Height, Width);
		view.SetPlayer(appearance, 0.46f, 0.7f);
		view.SetPlayerPosition(x, y, z, heading);
		if (game_hour > 0)
			view.SetGameTime(game_hour, game_minute);
		view.UpdateSpawns(spawns, 0, 1000);
		if (walk_ms > 0) {
			// Hold forward (and turn, with --turn) for walk_ms of 16 ms frames, then report where we got.
			MoveControls controls;
			controls.forward = true;
			controls.left = turn < 0;
			controls.right = turn > 0;
			uint64_t now = 1000;
			view.Move(controls, now, false);
			double total = 0;
			int steps = 0;
			for (int ms = 0; ms < walk_ms; ms += 16, ++steps) {
				now += 16;
				auto start = std::chrono::steady_clock::now();
				view.Move(controls, now, false);
				total += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
				if (ms % 1000 < 16) {
					ZoneMovement m = view.Movement();
					printf("  %5d ms: loc %.1f, %.1f, %.1f heading %.0f speed %.2f velocity %.2f, %.2f, %.2f\n", ms, m.y, m.x, m.z,
						m.heading, m.speed, m.dy, m.dx, m.dz);
				}
			}
			ZoneMovement m = view.Movement();
			printf("walked %d ms: loc %.1f, %.1f, %.1f heading %.0f (%.2f ms per movement step)\n", walk_ms, m.y, m.x, m.z,
				m.heading, total / std::max(1, steps));
		}
		if (first_person)
			view.CycleCamera();
		view.Orbit(orbit);
		view.Pitch(pitch);
		if (zoom) {
			view.Render(canvas, 0); // places the character, which sets the starting distance
			view.Zoom(zoom);
		}
		double total = 0;
		for (int f = 0; f < frames; ++f) {
			view.UpdateSpawns(spawns, 0, 1000 + f);
			auto start = std::chrono::steady_clock::now();
			view.Render(canvas, 1000 + f);
			total += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
			if (f == 0 && view.Model())
				printf("model %s: %zu triangles, height %.2f, feet at %.2f, %.2f, %.2f (WLD)\n", view.Model()->Code().c_str(),
					view.Model()->TriangleCount(), view.Model()->MaxZ() - view.Model()->MinZ(), view.Feet().x, view.Feet().y,
					view.Feet().z);
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
	int width = 640, height = 480;
	bool character = false;
	render::CharacterAppearance appearance;
	int class_id = 1;
	float camera_heading = 0;
	bool player = false, first_person = false;
	float orbit = 0;
	int zoom = 0, walk_ms = 0, turn = 0;
	std::map<uint16_t, ZoneSpawn> spawns;
	int game_hour = 0;
	float game_minute = 0;
	ZoneInfo info;
	info.sky = 1;
	info.time_type = 2;
	for (int i = 3; i < argc; ++i) {
		std::string arg = argv[i];
		if (arg == "--player" && i + 2 < argc) {
			player = true;
			appearance.race = static_cast<uint16_t>(atoi(argv[++i]));
			appearance.gender = static_cast<uint8_t>(atoi(argv[++i]));
		}
		else if (arg == "--npc" && i + 7 < argc) {
			ZoneSpawn spawn;
			spawn.id = static_cast<uint16_t>(spawns.size() + 1);
			spawn.npc = 1;
			spawn.race = static_cast<uint16_t>(atoi(argv[++i]));
			spawn.gender = static_cast<uint8_t>(atoi(argv[++i]));
			spawn.size = static_cast<float>(atof(argv[++i]));
			spawn.x = static_cast<float>(atof(argv[++i]));
			spawn.y = static_cast<float>(atof(argv[++i]));
			spawn.z = static_cast<float>(atof(argv[++i]));
			spawn.heading = static_cast<float>(atof(argv[++i]));
			spawns[spawn.id] = spawn;
		}
		else if (arg == "--time" && i + 2 < argc) {
			game_hour = atoi(argv[++i]);
			game_minute = static_cast<float>(atof(argv[++i]));
		}
		else if (arg == "--time-type" && i + 1 < argc)
			info.time_type = static_cast<uint8_t>(atoi(argv[++i]));
		else if (arg == "--orbit" && i + 1 < argc)
			orbit = static_cast<float>(atof(argv[++i]));
		else if (arg == "--walk" && i + 1 < argc)
			walk_ms = atoi(argv[++i]);
		else if (arg == "--turn" && i + 1 < argc)
			turn = atoi(argv[++i]);
		else if (arg == "--zoom" && i + 1 < argc)
			zoom = atoi(argv[++i]);
		else if (arg == "--first-person")
			first_person = true;
		else if (arg == "--fog" && i + 3 < argc) {
			info.fog_start = static_cast<float>(atof(argv[++i]));
			info.fog_end = static_cast<float>(atof(argv[++i]));
			info.fog_color = static_cast<uint32_t>(strtoul(argv[++i], nullptr, 16));
		}
		else if (arg == "--sky" && i + 1 < argc)
			info.sky = static_cast<uint8_t>(atoi(argv[++i]));
		else if (arg == "--char" && i + 3 < argc) {
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
		else if (arg == "--size" && i + 1 < argc)
			sscanf(argv[++i], "%dx%d", &width, &height);
	}

	if (player) {
		info.short_name = zone;
		bool have_heading = false;
		for (int i = 3; i < argc; ++i)
			have_heading = have_heading || std::string(argv[i]) == "--heading";
		return InZone(eq_dir, out, info, appearance, server_x, server_y, server_z, have_heading ? heading : 0, orbit, pitch,
			first_person, zoom, walk_ms, turn, frames, width, height, spawns, game_hour, game_minute);
	}
	if (character)
		return CharacterSelect(eq_dir, out, appearance, static_cast<uint8_t>(class_id), camera_heading, frames, width, height);

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

	const int Width = width, Height = height;
	render::Camera camera;
	camera.position = {server_y, server_x, server_z}; // WLD x/y are the server's y/x
	float yaw = render::HeadingToRadians(heading), tilt = pitch * render::Pi / 180;
	camera.forward = {std::cos(yaw) * std::cos(tilt), std::sin(yaw) * std::cos(tilt), std::sin(tilt)};

	render::Sky sky;
	if (!sky.Load(eq_dir, 1, error))
		printf("  no sky: %s\n", error.c_str());

	render::Rasterizer rasterizer;
	std::vector<uint32_t> pixels(static_cast<size_t>(Width) * Height);
	double total = 0;
	for (int f = 0; f < frames; ++f) {
		auto start = std::chrono::steady_clock::now();
		rasterizer.Begin(Width, Height, camera);
		scene.Draw(rasterizer, 0);
		if (sky.Loaded()) {
			rasterizer.BeginBackground();
			sky.Draw(rasterizer, 0);
			rasterizer.EndBackground();
		}
		rasterizer.Finish(pixels.data(), Width, sky.Loaded() ? sky.HorizonColor() : 0x203040);
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
