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
// SIZE X Y Z HEADING (repeatable) stands a spawn there; after it, --npc-look TEXTURE HELM gives it an
// NPC's body texture and helm (255 0: dressed per slot), --npc-equip M,M,... its equipment as the server sends it
// (head to feet materials, then the primary and secondary IT numbers), --npc-move SPEED TURN sets it walking (the
// update's speed, and turn per tick), --npc-stance S its stance (100, 110 sitting, 111, 115) and
// --npc-fly F its fly mode, --npc-light TYPE the light it carries (2 a torch, 4 a lantern...). --spawn-ms MS runs the spawns that long first, printing where they are
// and what they play each second. --time HOUR MINUTE sets Norrath's time (hour
// 1..24, 1 is midnight) for the zone's time type (--time-type, default 2: outdoors).
// --debug-draw wireframe,nofog,walls,floor,noculling,noclip turns on the debug menu's render toggles, and --vision
// own|none|infra|ultra picks what we see in at night (own: the race's).
// --door NAME TYPE X Y Z HEADING INCLINE SIZE PARAM (repeatable) puts a door or switch there as OP_SpawnDoor
// would (its id the count so far), --door-open MS opens them all (OP_MoveDoor 2) and runs that long first,
// printing where they are, and --click X Y clicks the window there after drawing (what EQSwitch::UseSwitch says).
// --weather rain|snow MS has it start raining or snowing (OP_Weather at intensity 3) and runs that
// long in 16 ms frames first, printing the weather's state and particles each second, and
// --debug-draw noweather draws none of it.
// With --char, --velious 0 shows the Velious armor (materials 17..23) as the older armor (VeliousTextures=0).
// --frames renders N times and prints the average frame time. --size picks the
// image size (default 640x480).

#include "charselect_view.h"
#include "eq_sound.h"
#include "mixer.h"
#include "zone_view.h"
#include "png_writer.h"
#include "rasterizer.h"
#include "sky.h"
#include "zone_scene.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <map>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

namespace
{
	// --sound: what the doors, we and the spawns sound like, mixed to a WAV as the frames run
	// (no zone music or ambience).
	struct SoundCapture
	{
		std::unique_ptr<audio::Mixer> mixer;
		std::unique_ptr<audio::EqSound> sound;
		std::vector<float> mix;
		double owed = 0; // frames of audio the clock has moved on by and we haven't mixed

		void Frame(ZoneView &view, uint64_t now, float ms)
		{
			if (!sound)
				return;
			ZoneView::Hearing hearing = view.SoundListener(false);
			sound->SetListener({hearing.position.x, hearing.position.y, hearing.position.z},
				{hearing.forward.x, hearing.forward.y, hearing.forward.z});
			sound->GiveTime(now);
			owed += ms * audio::Mixer::Rate / 1000.0;
			int frames = static_cast<int>(owed);
			owed -= frames;
			size_t at = mix.size();
			mix.resize(at + static_cast<size_t>(frames) * 2);
			mixer->Render(mix.data() + at, frames);
		}
	};
	SoundCapture g_sound;

	struct WeatherOption
	{
		int type = -1; // OP_Weather's: 0 rain, 1 snow
		int ms = 0;
	};
	WeatherOption g_weather;

	// --spell-effect STAGE EFFECT CASTER TARGET MS [CAST_MS ANIM]: a SpellsNew.eff record's stage on
	// the --npc spawns (1 on), run MS in 16 ms frames.
	struct EffectOption
	{
		int stage = -1, effect = 0, caster = 0, target = 0, ms = 0, cast_ms = 0, anim = 0;
	};
	EffectOption g_effect;

	void WriteWav(const std::string &path, const std::vector<float> &stereo)
	{
		FILE *file = fopen(path.c_str(), "wb");
		if (!file)
			return;
		auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, file); };
		auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, file); };
		uint32_t bytes = static_cast<uint32_t>(stereo.size() * 2);
		fwrite("RIFF", 1, 4, file);
		u32(36 + bytes);
		fwrite("WAVEfmt ", 1, 8, file);
		u32(16);
		u16(1);
		u16(2);
		u32(audio::Mixer::Rate);
		u32(audio::Mixer::Rate * 4);
		u16(4);
		u16(16);
		fwrite("data", 1, 4, file);
		u32(bytes);
		for (float sample : stereo)
			u16(static_cast<uint16_t>(static_cast<int16_t>(std::lround(std::clamp(sample, -1.0f, 1.0f) * 32767))));
		fclose(file);
	}

	int CharacterSelect(const std::string &eq_dir, const std::string &out, const render::CharacterAppearance &appearance,
		uint8_t class_id, float camera_heading, int frames, int Width, int Height, bool velious)
	{
		auto t0 = std::chrono::steady_clock::now();
		CharacterSelectView view(eq_dir);
		view.SetVeliousTextures(velious);
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
		int Width, int Height, const std::map<uint16_t, ZoneSpawn> &spawns, int game_hour, float game_minute,
		const ZoneView::DebugDraw &debug_draw, int spawn_ms, const std::vector<ZoneDoor> &doors, int door_open_ms, int click_x,
		int click_y, int jump_ms)
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
		view.SetSound(g_sound.sound.get());
		view.SetDebugDraw(debug_draw);
		view.SetPlayer(appearance, 0.46f, 0.7f);
		view.SetPlayerPosition(x, y, z, heading);
		if (game_hour > 0)
			view.SetGameTime(game_hour, game_minute);
		view.UpdateSpawns(spawns, 0, 1000);
		if (first_person)
			view.CycleCamera();
		view.Orbit(orbit);
		view.Pitch(pitch);
		uint64_t now = 1000; // Move's clock
		if (!doors.empty()) {
			// The doors move as Move runs, on its clock (before any walk, which they can be in the way of).
			view.SetDoors(doors, now);
			view.Move(MoveControls(), now, false);
			if (door_open_ms > 0) {
				for (const ZoneDoor &door : doors)
					view.MoveDoor(door.id, 2, now);
				for (int ms = 0; ms <= door_open_ms; ms += 16) {
					now += 16;
					view.Move(MoveControls(), now, false);
					g_sound.Frame(view, now, 16);
				}
			}
			printf("%zu doors, %zu with a model\n", view.DoorCount(), view.DoorsShown());
		}
		if (walk_ms > 0) {
			// Hold forward (and turn, with --turn) for walk_ms of 16 ms frames, then report where we got.
			MoveControls controls;
			controls.forward = true;
			controls.left = turn < 0;
			controls.right = turn > 0;
			view.Move(controls, now, false);
			double total = 0;
			int steps = 0;
			for (int ms = 0; ms < walk_ms; ms += 16, ++steps) {
				now += 16;
				auto start = std::chrono::steady_clock::now();
				controls.jump = jump_ms >= 0 && ms >= jump_ms && ms < jump_ms + 16;
				view.Move(controls, now, false);
				total += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
				g_sound.Frame(view, now, 16);
				std::string zone_line = view.TakeZoneLine();
				if (!zone_line.empty()) {
					ZoneMovement at = view.Movement();
					printf("  %5d ms: zone line %s at loc %.1f, %.1f, %.1f\n", ms, zone_line.c_str(), at.y, at.x, at.z);
				}
				if (ms % 1000 < 16) {
					ZoneMovement m = view.Movement();
					ZoneView::DebugStats st = view.Stats();
					printf("  %5d ms: loc %.1f, %.1f, %.1f heading %.0f speed %.2f velocity %.2f, %.2f, %.2f  body %d eyes %d %s\n", ms, m.y,
						m.x, m.z, m.heading, m.speed, m.dy, m.dx, m.dz, static_cast<int>(st.body_environment),
						static_cast<int>(st.head_environment), st.animation.c_str());
				}
			}
			ZoneMovement m = view.Movement();
			printf("walked %d ms: loc %.1f, %.1f, %.1f heading %.0f (%.2f ms per movement step)\n", walk_ms, m.y, m.x, m.z,
				m.heading, total / std::max(1, steps));
		}
		if (zoom) {
			view.Render(canvas, 0); // places the character, which sets the starting distance
			view.Zoom(zoom);
		}
		uint64_t spawn_clock = 1000;
		if (spawn_ms > 0) {
			// Run everyone else in 16 ms frames (Render first: it places the camera they're near).
			view.Render(canvas, spawn_clock);
			double spawn_total = 0;
			int spawn_steps = 0;
			for (int ms = 0; ms <= spawn_ms; ms += 16, ++spawn_steps) {
				spawn_clock += 16;
				auto start = std::chrono::steady_clock::now();
				view.UpdateSpawns(spawns, 0, spawn_clock);
				spawn_total += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
				g_sound.Frame(view, spawn_clock, 16);
				if ((ms % 1000 >= 16 && ms != spawn_ms) || spawns.size() > 12)
					continue;
				for (const auto &[id, spawn] : spawns) {
					ZoneView::SpawnDebug st;
					if (view.SpawnStats(id, st))
						printf("  %5d ms: spawn %u %s at loc %.1f, %.1f, %.1f (feet) playing %s x%.2f%s\n", ms, id, st.model.c_str(),
							st.feet.y, st.feet.x, st.feet.z, st.animation.c_str(), st.animation_rate,
							render::Swimming(st.body_environment) ? " in water" : "");
				}
			}
			printf("%.3f ms per UpdateSpawns (%zu spawns)\n", spawn_total / spawn_steps, spawns.size());
		}
		if (g_weather.type >= 0) {
			// --weather: as FrontEnd::WeatherFrame runs it, a frame each 16 ms.
			Weather weather;
			weather.Receive(static_cast<uint32_t>(g_weather.type), 3);
			for (int ms = 0; ms <= g_weather.ms; ms += 16) {
				spawn_clock += 16;
				Weather::Place place;
				place.outdoors = info.time_type == 1 || info.time_type == 2 || info.time_type == 5;
				view.ZoneFog(place.fog_start, place.fog_end);
				place.clip_plane = ZoneView::ClipPlane;
				if (int message = weather.Frame(nullptr, spawn_clock, place))
					printf("  %5d ms: eqstr %d\n", ms, message);
				view.SetWeather(weather.GetEffects());
				view.Render(canvas, spawn_clock);
				if (ms % 1000 >= 16 && ms + 16 <= g_weather.ms)
					continue;
				Weather::Effects effects = weather.GetEffects();
				ZoneView::WeatherCounts counts = view.WeatherParticles();
				printf("  %5d ms: weather %s, fog %.0f..%.0f%s, %zu falling, %zu on the ground, %zu on water\n", ms,
					effects.active ? "on" : "off", effects.fog_start, effects.fog_end, effects.flash ? ", flash" : "", counts.falling,
					counts.ground, counts.water);
			}
		}
		if (g_effect.stage >= 0) {
			view.UpdateSpawns(spawns, 0, spawn_clock);
			view.Render(canvas, spawn_clock);
			SpellEffects::Request request;
			request.stage = g_effect.stage;
			request.effect = g_effect.effect;
			request.caster = static_cast<uint16_t>(g_effect.caster);
			request.target = static_cast<uint16_t>(g_effect.target);
			request.duration_ms = g_effect.cast_ms;
			request.casting_anim = g_effect.anim;
			view.StartSpellEffect(request);
			double effect_total = 0;
			int effect_frames = 0;
			for (int ms = 0; ms <= g_effect.ms; ms += 16, ++effect_frames) {
				spawn_clock += 16;
				view.UpdateSpawns(spawns, 0, spawn_clock);
				auto start = std::chrono::steady_clock::now();
				view.Render(canvas, spawn_clock);
				effect_total += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
				if (ms % 500 < 16)
					printf("  %5d ms: %zu emitters, %zu particles\n", ms, view.Effects().EmitterCount(), view.Effects().ParticleCount());
			}
			printf("%.1f ms per frame with the effect\n", effect_total / std::max(1, effect_frames));
		}
		double total = 0;
		for (int f = 0; f < frames; ++f) {
			view.UpdateSpawns(spawns, 0, spawn_clock + f);
			auto start = std::chrono::steady_clock::now();
			view.Render(canvas, spawn_clock + f);
			total += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
			if (f == 0 && view.Model())
				printf("model %s: %zu triangles, height %.2f (z %.2f..%.2f, origin %.2f over the feet), feet at %.2f, %.2f, %.2f (WLD)\n",
					view.Model()->Code().c_str(), view.Model()->TriangleCount(), view.Model()->MaxZ() - view.Model()->MinZ(),
					view.Model()->MinZ(), view.Model()->MaxZ(), view.Model()->OriginOverFeet(), view.Feet().x, view.Feet().y,
					view.Feet().z);
		}
		printf("%zu triangles drawn, %.1f ms per frame\n", view.TrianglesDrawn(), total / frames);
		if (click_x >= 0) {
			uint8_t id = 0;
			int16_t skill = 0;
			static const char *const uses[] = {"nothing", "too far", "ask the zone"};
			ZoneDoors::Use use = view.ClickDoor(click_x, click_y, 0, id, skill);
			printf("click at %d, %d: %s", click_x, click_y, uses[static_cast<int>(use)]);
			if (use == ZoneDoors::Use::Ask)
				printf(" (door %u)", id);
			printf("\n");
		}
		ZoneView::DebugStats stats = view.Stats();
		printf("zone meshes %zu in view, %zu hidden\n", stats.meshes_in_view, stats.meshes_hidden);
		if (stats.have_floor)
			printf("feet at z %.2f, floor %.2f below them\n", stats.feet_z, stats.feet_z - stats.floor_z);
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
	std::string eq_dir = argv[1], out = argv[2], zone = "load2", sound_out;
	int jump_ms = -1;
	float server_x = 1475, server_y = 600, server_z = -175, heading = 256, pitch = 0;
	int frames = 1;
	int width = 640, height = 480;
	bool character = false;
	render::CharacterAppearance appearance;
	int class_id = 1;
	bool velious = true;
	float camera_heading = 0;
	bool player = false, first_person = false;
	float orbit = 0;
	int zoom = 0, walk_ms = 0, turn = 0, spawn_ms = 0;
	std::vector<ZoneDoor> doors;
	int door_open_ms = 0, click_x = -1, click_y = -1;
	std::map<uint16_t, ZoneSpawn> spawns;
	int game_hour = 0;
	float game_minute = 0;
	ZoneView::DebugDraw debug_draw;
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
			spawn.hp = 100;
			spawn.body_texture = 0; // an NPC's default texture (a player race's guard is 255 and dressed)
			spawn.updated_ms = 1000;
			spawns[spawn.id] = spawn;
		}
		else if (arg == "--door" && i + 9 < argc) {
			ZoneDoor door;
			door.id = static_cast<uint8_t>(doors.size() + 1);
			door.name = argv[++i];
			door.type = static_cast<uint8_t>(atoi(argv[++i]));
			door.x = static_cast<float>(atof(argv[++i]));
			door.y = static_cast<float>(atof(argv[++i]));
			door.z = static_cast<float>(atof(argv[++i]));
			door.heading = static_cast<float>(atof(argv[++i]));
			door.incline = static_cast<int16_t>(atoi(argv[++i]));
			door.size = static_cast<int16_t>(atoi(argv[++i]));
			door.param = static_cast<int16_t>(atoi(argv[++i]));
			doors.push_back(door);
		}
		else if (arg == "--door-open" && i + 1 < argc)
			door_open_ms = atoi(argv[++i]);
		else if (arg == "--click" && i + 2 < argc) {
			click_x = atoi(argv[++i]);
			click_y = atoi(argv[++i]);
		}
		else if (arg == "--debug-draw" && i + 1 < argc) {
			std::string list = argv[++i];
			debug_draw.wireframe = list.find("wireframe") != std::string::npos;
			debug_draw.no_fog = list.find("nofog") != std::string::npos;
			debug_draw.walls = list.find("walls") != std::string::npos;
			debug_draw.floor = list.find("floor") != std::string::npos;
			debug_draw.no_culling = list.find("noculling") != std::string::npos;
			debug_draw.no_clip = list.find("noclip") != std::string::npos;
			debug_draw.no_weather = list.find("noweather") != std::string::npos;
		}
		else if (arg == "--spell-effect" && i + 5 < argc) {
			g_effect.stage = atoi(argv[++i]);
			g_effect.effect = atoi(argv[++i]);
			g_effect.caster = atoi(argv[++i]);
			g_effect.target = atoi(argv[++i]);
			g_effect.ms = atoi(argv[++i]);
			if (i + 2 < argc && argv[i + 1][0] != '-') {
				g_effect.cast_ms = atoi(argv[++i]);
				g_effect.anim = atoi(argv[++i]);
			}
		}
		else if (arg == "--weather" && i + 2 < argc) {
			g_weather.type = std::string(argv[++i]) == "snow" ? 1 : 0;
			g_weather.ms = atoi(argv[++i]);
		}
		else if (arg == "--time" && i + 2 < argc) {
			game_hour = atoi(argv[++i]);
			game_minute = static_cast<float>(atof(argv[++i]));
		}
		else if (arg == "--vision" && i + 1 < argc) {
			std::string v = argv[++i];
			debug_draw.vision = v == "none" ? 0 : v == "infra" ? 1 : v == "ultra" ? 2 : -1;
		}
		else if (arg == "--time-type" && i + 1 < argc)
			info.time_type = static_cast<uint8_t>(atoi(argv[++i]));
		else if (arg == "--orbit" && i + 1 < argc)
			orbit = static_cast<float>(atof(argv[++i]));
		else if (arg == "--npc-look" && i + 2 < argc && !spawns.empty()) {
			spawns.rbegin()->second.body_texture = static_cast<uint8_t>(atoi(argv[++i]));
			spawns.rbegin()->second.helm = static_cast<uint8_t>(atoi(argv[++i]));
			spawns.rbegin()->second.equipment[0] = spawns.rbegin()->second.helm;
		}
		else if (arg == "--npc-equip" && i + 1 < argc && !spawns.empty()) {
			std::string list = argv[++i];
			for (int slot = 0; slot < render::EquipSlots && !list.empty(); ++slot) {
				spawns.rbegin()->second.equipment[slot] = static_cast<uint16_t>(atoi(list.c_str()));
				size_t comma = list.find(',');
				list = comma == std::string::npos ? "" : list.substr(comma + 1);
			}
		}
		else if (arg == "--npc-move" && i + 2 < argc && !spawns.empty()) {
			spawns.rbegin()->second.speed = static_cast<float>(atof(argv[++i]));
			spawns.rbegin()->second.heading_speed = static_cast<float>(atof(argv[++i]));
		}
		else if (arg == "--npc-stance" && i + 1 < argc && !spawns.empty())
			spawns.rbegin()->second.stance = static_cast<uint8_t>(atoi(argv[++i]));
		else if (arg == "--npc-light" && i + 1 < argc && !spawns.empty())
			spawns.rbegin()->second.light = static_cast<uint8_t>(atoi(argv[++i]));
		else if (arg == "--npc-fly" && i + 1 < argc && !spawns.empty())
			spawns.rbegin()->second.fly_mode = static_cast<uint8_t>(atoi(argv[++i]));
		else if (arg == "--spawn-ms" && i + 1 < argc)
			spawn_ms = atoi(argv[++i]);
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
			appearance.class_id = static_cast<uint8_t>(class_id);
		}
		else if (arg == "--velious" && i + 1 < argc)
			velious = atoi(argv[++i]) != 0;
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
		else if (arg == "--sound" && i + 1 < argc)
			sound_out = argv[++i];
		else if (arg == "--jump" && i + 1 < argc)
			jump_ms = atoi(argv[++i]);
	}

	if (player) {
		info.short_name = zone;
		bool have_heading = false;
		for (int i = 3; i < argc; ++i)
			have_heading = have_heading || std::string(argv[i]) == "--heading";
		if (!sound_out.empty()) {
			g_sound.mixer = std::make_unique<audio::Mixer>();
			audio::EqSound::Settings settings;
			settings.music_volume = 0;
			g_sound.sound = std::make_unique<audio::EqSound>(*g_sound.mixer, eq_dir, settings);
			g_sound.sound->SetWaveLogger([](const std::string &line) {
				printf("  [sound %.2fs] %s\n", g_sound.mix.size() / 2.0 / audio::Mixer::Rate, line.c_str());
			});
		}
		int result = InZone(eq_dir, out, info, appearance, server_x, server_y, server_z, have_heading ? heading : 0, orbit,
			pitch, first_person, zoom, walk_ms, turn, frames, width, height, spawns, game_hour, game_minute, debug_draw, spawn_ms,
			doors, door_open_ms, click_x, click_y, jump_ms);
		if (g_sound.sound) {
			WriteWav(sound_out, g_sound.mix);
			printf("wrote %s (%.1f s of sound)\n", sound_out.c_str(), g_sound.mix.size() / 2.0 / audio::Mixer::Rate);
		}
		return result;
	}
	if (character)
		return CharacterSelect(eq_dir, out, appearance, static_cast<uint8_t>(class_id), camera_heading, frames, width, height, velious);

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

	scene.SetOcclusionCulling(!debug_draw.no_culling);
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
	printf("zone meshes %zu in view, %zu hidden\n", scene.MeshesInView(), scene.MeshesHidden());
	if (!WritePng(out, pixels, Width, Height)) {
		fprintf(stderr, "couldn't write %s\n", out.c_str());
		return 1;
	}
	printf("wrote %s\n", out.c_str());
	return 0;
}
