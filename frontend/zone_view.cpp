#include "zone_view.h"

#include "actor_tags.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

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

	uint32_t ScaleColor(uint32_t color, const render::Vec3 &scale)
	{
		auto channel = [](uint32_t c, float s) { return static_cast<uint32_t>(std::clamp(c * s, 0.0f, 255.0f)); };
		return (channel((color >> 16) & 0xff, scale.x) << 16) | (channel((color >> 8) & 0xff, scale.y) << 8) |
		       channel(color & 0xff, scale.z);
	}
}

ZoneView::ZoneView(std::string eq_dir) : m_eq_dir(std::move(eq_dir)) {}

void ZoneView::SetGameTime(int hour, float minute)
{
	m_have_time = true;
	m_hour = hour;
	m_minute = minute;
}

ZoneView::DayLight ZoneView::Lighting() const
{
	DayLight light;
	if (!m_have_time)
		return light;
	// CDisplay_GetGameTimeInfo: zones that don't follow the time of day are at midnight.
	bool outdoors = m_info.time_type == 1 || m_info.time_type == 2 || m_info.time_type == 5;
	int hour = outdoors ? std::clamp(m_hour, 1, 24) : 0;
	float minute = outdoors ? m_minute : 0;

	// SetAmbientLight: the hour's color toward the next hour's; 0.1 where time doesn't pass
	// (0.5 for time type 4).
	if (outdoors) {
		int a = hour - 1, b = hour % 24;
		float t = minute / 60;
		light.ambient = {AmbientRed[a] + (AmbientRed[b] - AmbientRed[a]) * t, AmbientGreen[a] + (AmbientGreen[b] - AmbientGreen[a]) * t,
			AmbientBlue[a] + (AmbientBlue[b] - AmbientBlue[a]) * t};
	}
	else {
		float level = m_info.time_type == 4 ? 0.5f : 0.1f;
		light.ambient = {level, level, level};
	}
	light.ambient = {std::min(light.ambient.x, 0.99f), std::min(light.ambient.y, 0.99f), std::min(light.ambient.z, 0.99f)};

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

ZoneView::~ZoneView()
{
	if (m_loader.joinable())
		m_loader.join();
}

void ZoneView::StartLoading(const ZoneInfo &info)
{
	if (m_loader.joinable() || m_loaded)
		return;
	m_info = info;
	m_loader = std::thread([this] { Load(); });
}

void ZoneView::Load()
{
	std::string error;
	// The zone's own lighting only; the time of day's ambient light is added as it draws.
	render::ZoneLighting lighting;
	lighting.ambient = 0;
	lighting.point_lights = false; // already baked into the vertex colors
	if (!m_zone.Load(m_eq_dir, m_info.short_name, error, lighting))
		m_error = error;
	else if (!m_models.Open(m_eq_dir, error, true))
		m_error = error;
	else if (m_info.sky != 0 && m_info.fog_end >= 101 && !m_sky.Load(m_eq_dir, m_info.sky, error))
		fprintf(stderr, "[zone view] %s\n", error.c_str()); // the zone still draws, over the fog color
	if (m_error.empty())
		m_models.AddZone(m_eq_dir, m_info.short_name);
	m_loaded = true;
}

void ZoneView::SetPlayer(const render::CharacterAppearance &appearance, float walk_speed, float run_speed)
{
	m_appearance = appearance;
	m_walk_speed = walk_speed;
	m_run_speed = run_speed;
	m_model_dirty = true;
}

void ZoneView::SetPlayerPosition(float x, float y, float z, float heading)
{
	render::Vec3 position{x, y, z};
	if (position.x == m_server_position.x && position.y == m_server_position.y && position.z == m_server_position.z &&
		heading == m_heading && !m_position_dirty)
		return;
	m_server_position = position;
	m_heading = heading;
	m_position_dirty = true;
}

void ZoneView::CycleCamera()
{
	m_view = m_view == CameraView::Chase ? CameraView::FirstPerson : CameraView::Chase;
	m_orbit = 0;
	m_pitch = m_view == CameraView::Chase ? -12.0f : 0.0f;
}

void ZoneView::Turn(float delta)
{
	if (!m_placed)
		return;
	m_movement.Turn(delta);
	m_heading = m_movement.Heading();
}

void ZoneView::FaceCamera()
{
	Turn(m_orbit);
	m_orbit = 0;
}

void ZoneView::Pitch(float degrees)
{
	m_pitch = std::clamp(m_pitch + degrees, -80.0f, 60.0f);
}

void ZoneView::Zoom(int notches)
{
	if (m_distance <= 0)
		return; // not placed yet
	// Zooming out of first person steps back into the chase camera, close behind the head;
	// zooming in past the closest distance goes back to first person.
	if (m_view == CameraView::FirstPerson) {
		if (notches >= 0)
			return;
		m_view = CameraView::Chase;
		m_distance = MinDistance;
		++notches;
	}
	float distance = m_distance * std::pow(0.85f, static_cast<float>(notches));
	if (distance < MinDistance) {
		m_view = CameraView::FirstPerson;
		m_distance = MinDistance;
		FaceCamera(); // first person looks where the character faces
		return;
	}
	m_distance = std::min(distance, MaxDistance);
}

void ZoneView::PlacePlayer()
{
	if (m_model_dirty) {
		m_model_dirty = false;
		m_model.reset();
		if (m_appearance) {
			std::string error;
			m_model = m_models.Build(*m_appearance, error);
			if (!m_model)
				fprintf(stderr, "[zone view] %s\n", error.c_str());
		}
		m_position_dirty = true;
	}
	if (!m_position_dirty)
		return;
	m_position_dirty = false;

	// The server's z is the model's center height, size / 5 * 3.125 over the feet
	// (Mob::CalcZOffset). Settle the feet onto the floor right below, like
	// EQPlayer::PositionOnFloor, in case the zone's floor and the server's map differ a little.
	float height = m_model ? m_model->MaxZ() - m_model->MinZ() : 6;
	if (m_appearance && m_appearance->height > 0)
		height = m_appearance->height;
	m_height = height;
	m_feet = {m_server_position.y, m_server_position.x, m_server_position.z - height / 5 * 3.125f}; // WLD x/y are the server's y/x
	float floor_z;
	if (m_zone.FloorBelow({m_feet.x, m_feet.y, m_feet.z + 2}, 10, floor_z))
		m_feet.z = floor_z;
	if (m_distance <= 0)
		m_distance = height * 2 + 6;
	m_movement.Place(m_feet, m_heading, height, m_walk_speed, m_run_speed, m_info.gravity);
	m_placed = true;
}

bool ZoneView::Move(const MoveControls &controls, uint64_t now_ms, bool looking)
{
	if (!m_loaded || !m_error.empty())
		return false;
	if (m_loader.joinable())
		m_loader.join();
	PlacePlayer();
	float ms = m_last_move_ms ? static_cast<float>(now_ms - m_last_move_ms) : 0;
	m_last_move_ms = now_ms;
	if (!m_placed || ms <= 0)
		return false;

	bool jumped = m_movement.Step(controls, ms, m_zone);
	m_feet = m_movement.Feet();
	m_heading = m_movement.Heading();
	m_server_position = {m_feet.y, m_feet.x, m_feet.z + m_height / 5 * 3.125f};
	// The chase camera drifts back behind us as we move, unless the mouse is holding it.
	if (m_movement.Speed() != 0 && !looking && m_view == CameraView::Chase) {
		m_orbit = std::fmod(m_orbit, 512.0f);
		if (m_orbit > 256)
			m_orbit -= 512;
		if (m_orbit < -256)
			m_orbit += 512;
		m_orbit *= std::pow(0.9f, ms * 0.02f);
	}
	ChooseAnimation(ms);
	return jumped;
}

void ZoneView::ChooseAnimation(float ms)
{
	// What the character is doing picks the animation, as EQPlayer's animation tracks do:
	// L01 walk (backwards for back), L02 run, L03/L04 jump running/standing, L05 falling, P01 idle.
	std::string animation = "P01";
	bool reversed = false;
	float rate = 1;
	float speed = m_movement.Speed();
	if (!m_movement.OnGround()) {
		if (m_movement.Velocity().z > 0)
			animation = speed != 0 ? "L03" : "L04";
		else
			animation = "L05";
	}
	else if (speed > 0)
		animation = speed >= m_movement.RunSpeed() ? "L02" : "L01";
	else if (speed < 0) {
		animation = "L01";
		reversed = true;
	}
	if (m_model && !m_model->HasAnimation(animation))
		animation = animation == "L03" || animation == "L04" || animation == "L05" ? "P01" : animation;
	if (animation != m_animation) {
		m_animation = animation;
		m_animation_ms = 0;
	}
	m_animation_reversed = reversed;
	m_animation_ms += ms * rate;
	// Jumps and falls hold their last frame instead of looping.
	if (m_model && (animation == "L03" || animation == "L04" || animation == "L05")) {
		double length = m_model->AnimationMs(animation);
		if (length > 0)
			m_animation_ms = std::min(m_animation_ms, length - 1);
	}
}

ZoneMovement ZoneView::Movement() const
{
	ZoneMovement m;
	m.x = m_server_position.x; // m_server_position is (x, y, z) in the server's terms
	m.y = m_server_position.y;
	m.z = m_server_position.z;
	m.heading = m_heading;
	m.speed = m_movement.Speed();
	m.heading_speed = m_movement.HeadingSpeed();
	m.dy = m_movement.Velocity().x; // WLD x is the server's y
	m.dx = m_movement.Velocity().y;
	m.dz = m_movement.Velocity().z;
	return m;
}

render::Camera ZoneView::MakeCamera(float &distance) const
{
	float height = m_model ? m_model->MaxZ() - m_model->MinZ() : 6;
	float eye = m_model ? (m_model->HeadZ() - m_model->MinZ()) * 1.05f : height * 0.9f;
	float yaw = render::HeadingToRadians(m_heading + m_orbit);
	float pitch = m_pitch * render::Pi / 180;
	render::Vec3 look{std::cos(yaw) * std::cos(pitch), std::sin(yaw) * std::cos(pitch), std::sin(pitch)};

	render::Camera camera;
	camera.horizontal_fov = render::Pi / 2;
	camera.near_plane = 0.3f;
	// CDisplay::SetFog: the yon is the fog's end; everything past it is the sky or the fog color.
	camera.far_plane = std::max(m_info.fog_end, 50.0f);
	camera.fog = true;
	camera.fog_start = m_info.fog_start;
	camera.fog_end = camera.far_plane;
	// The fog darkens with the light (at night it fades into the dark, not into daylight grey).
	DayLight light = Lighting();
	float level = std::clamp((light.ambient.x + light.ambient.y + light.ambient.z) / 3 / 0.9f, 0.12f, 1.0f);
	camera.fog_color = ScaleColor(m_info.fog_color, {level, level, level});
	distance = 0;
	if (m_view == CameraView::FirstPerson) {
		camera.position = m_feet + render::Vec3{0, 0, eye};
		camera.forward = look;
	}
	else {
		// Behind the character and a little above, looking at their head. Like the real
		// client's camera, it comes in closer when a wall or the floor is in the way. The
		// test covers a box around the camera, not just its center, so a slope or wall it
		// backs into at a shallow angle stays clear of the near plane.
		render::Vec3 target = m_feet + render::Vec3{0, 0, eye};
		render::Vec3 right = render::Normalize(render::Cross(look, {0, 0, 1}));
		if (render::Length(right) == 0)
			right = {0, 1, 0};
		render::Vec3 up = render::Cross(right, look);
		const float margin = 1, half_width = 0.8f, half_height = 0.6f;
		float reach = m_distance + margin;
		const render::Vec3 corners[5] = {{0, 0, 0}, right * half_width + up * half_height, right * half_width - up * half_height,
			right * -half_width + up * half_height, right * -half_width - up * half_height};
		float nearest = 1;
		for (const auto &corner : corners) {
			float fraction;
			if (m_zone.SegmentHit(target, target - look * reach + corner, fraction))
				nearest = std::min(nearest, fraction);
		}
		distance = std::min(m_distance, std::max(0.5f, nearest * reach - margin));
		camera.position = target - look * distance;
		camera.forward = look;
	}
	return camera;
}

void ZoneView::Render(Canvas &canvas, uint64_t now_ms)
{
	if (!m_loaded || !m_error.empty()) {
		canvas.Fill({0, 0, canvas.Width(), canvas.Height()}, 0x000000);
		return;
	}
	if (m_loader.joinable())
		m_loader.join();
	PlacePlayer();

	float distance;
	render::Camera camera = MakeCamera(distance);
	m_rasterizer.Begin(canvas.Width(), canvas.Height(), camera);
	DayLight light = Lighting();
	m_rasterizer.SetVertexLight({1, 1, 1}, light.ambient);
	m_zone.Draw(m_rasterizer, now_ms);
	m_rasterizer.SetVertexLight({1, 1, 1}, {0, 0, 0});
	// Characters: the ambient light, and a directional light that weakens with the daylight.
	float level = (light.ambient.x + light.ambient.y + light.ambient.z) / 3;
	m_model_ambient = std::max(0.08f, level * 0.6f);
	m_model_diffuse = 0.1f + 0.55f * std::clamp(level / 0.9f, 0.0f, 1.0f);
	// Our own model isn't drawn in first person, or with the camera pushed into our head.
	m_model_shown = m_model && m_view == CameraView::Chase && distance >= 1.5f;
	if (m_model_shown) {
		render::Transform placement;
		placement.rotation = render::AxisAngle({0, 0, 1}, render::HeadingToRadians(m_heading));
		placement.translation = m_feet - render::Vec3{0, 0, m_model->MinZ()};
		// Lit from high over the camera's shoulder.
		float yaw = render::HeadingToRadians(m_heading + m_orbit);
		render::Vec3 light_direction{-std::cos(yaw), -std::sin(yaw), 1.2f};
		// The animation's own clock once we're moving (the preview tool never moves: real time).
		uint64_t time = m_last_move_ms ? static_cast<uint64_t>(m_animation_ms) : now_ms;
		if (m_animation_reversed) {
			uint32_t length = m_model->AnimationMs(m_animation);
			if (length > 0)
				time = length - 1 - time % length;
		}
		m_model->Draw(m_rasterizer, placement, light_direction, m_model_ambient, m_model_diffuse, time, m_animation);
	}
	m_camera_position = camera.position;
	DrawSpawns();
	// The sky dims with the day period, toward a deep blue at night.
	bool sky = m_sky.Loaded();
	render::Vec3 sky_tint{light.sky + 0.03f * (1 - light.sky), light.sky + 0.04f * (1 - light.sky), light.sky + 0.10f * (1 - light.sky)};
	if (sky) {
		m_rasterizer.BeginBackground();
		m_rasterizer.SetVertexLight(sky_tint, {0, 0, 0});
		m_sky.Draw(m_rasterizer, now_ms);
		m_rasterizer.EndBackground();
	}
	m_triangles_drawn = m_rasterizer.TriangleCount();
	m_rasterizer.Finish(canvas.Row(0), canvas.Stride(), sky ? ScaleColor(m_sky.HorizonColor(), sky_tint) : camera.fog_color);
}

// ---------------------------------------------------------------- other spawns

std::shared_ptr<render::CharacterModel> ZoneView::SpawnModel(const ZoneSpawn &spawn)
{
	render::CharacterAppearance appearance;
	appearance.race = spawn.race;
	appearance.gender = spawn.gender;
	appearance.face = spawn.face;
	appearance.height = spawn.size;
	char key[512];
	int length = snprintf(key, sizeof(key), "%u/%u/%u/%.2f", spawn.race, spawn.gender, spawn.face, spawn.size);
	for (int k = 0; k < render::EquipSlots; ++k) {
		appearance.equip[k] = spawn.equipment[k];
		appearance.tint[k] = spawn.colors[k];
		length += snprintf(key + length, sizeof(key) - static_cast<size_t>(length), "/%u:%08x", spawn.equipment[k], spawn.colors[k]);
	}
	auto cached = m_model_cache.find(key);
	if (cached != m_model_cache.end())
		return cached->second;
	std::string error;
	std::shared_ptr<render::CharacterModel> model = m_models.Build(appearance, error);
	if (!model && m_missing_models.insert(render::ActorTag(spawn.race, spawn.gender)).second)
		fprintf(stderr, "[zone view] %s (race %u)\n", error.c_str(), spawn.race);
	m_model_cache[key] = model;
	return model;
}

void ZoneView::UpdateSpawns(const std::map<uint16_t, ZoneSpawn> &spawns, uint16_t my_id, uint64_t now_ms)
{
	if (!m_loaded || !m_error.empty())
		return;
	if (m_loader.joinable())
		m_loader.join();
	for (auto it = m_actors.begin(); it != m_actors.end();)
		it = spawns.count(it->first) ? std::next(it) : m_actors.erase(it);
	for (const auto &[id, spawn] : spawns) {
		if (id == my_id || id == 0)
			continue;
		PoseSpawn(m_actors[id], spawn, now_ms);
	}
}

void ZoneView::PoseSpawn(SpawnActor &actor, const ZoneSpawn &spawn, uint64_t now_ms)
{
	// Carry on from the last update the way process_physics moves everyone: the velocity per
	// tick (and the turn) since the update came in, for up to two seconds.
	float ticks = spawn.updated_ms && now_ms > spawn.updated_ms ? std::min((now_ms - spawn.updated_ms) * 0.02f, 40.0f) : 0;
	float y = spawn.y + spawn.dy * ticks, x = spawn.x + spawn.dx * ticks, z = spawn.z + spawn.dz * ticks;
	actor.heading = std::fmod(spawn.heading + spawn.heading_speed * std::min(ticks, 10.0f) + 512, 512.0f);

	float size = spawn.size > 0 ? spawn.size : (render::IsPlayerRace(spawn.race) ? render::RaceHeight(spawn.race) : 6);
	actor.feet = {y, x, z - size / 5 * 3.125f}; // WLD x/y are the server's y/x; z is the model's center

	// Build the model once they're close enough to be seen.
	float distance = render::Length(actor.feet - m_camera_position);
	if (!actor.resolved && distance < std::max(m_info.fog_end, 50.0f) + 50) {
		actor.model = SpawnModel(spawn);
		actor.resolved = true;
	}

	// The animation: dead, sitting, running, walking or standing.
	std::string animation = "P01";
	bool hold = false;
	float moving = std::sqrt(spawn.dx * spawn.dx + spawn.dy * spawn.dy);
	if (spawn.npc == 2 || spawn.npc == 3 || spawn.stance == 115) {
		animation = "D05";
		hold = true;
	}
	else if (spawn.speed != 0 || moving > 0.05f) {
		float speed = spawn.speed != 0 ? std::fabs(spawn.speed) : moving / 2.13f; // velocity -> speed at steady state
		animation = speed >= spawn.run_speed * 0.9f ? "L02" : "L01";
	}
	if (actor.model && !actor.model->HasAnimation(animation))
		animation = "P01";
	float ms = actor.last_ms && now_ms > actor.last_ms ? static_cast<float>(now_ms - actor.last_ms) : 0;
	actor.last_ms = now_ms;
	if (animation != actor.animation) {
		actor.animation = animation;
		actor.animation_ms = 0;
	}
	actor.hold_last_frame = hold;
	actor.animation_ms += ms;
	if (hold && actor.model) {
		double length = actor.model->AnimationMs(animation);
		if (length > 0)
			actor.animation_ms = std::min(actor.animation_ms, length - 1);
	}
}

void ZoneView::DrawSpawns()
{
	float far_plane = std::max(m_info.fog_end, 50.0f);
	for (auto &[id, actor] : m_actors) {
		actor.drawn = false;
		if (!actor.model)
			continue;
		const render::CharacterModel &model = *actor.model;
		float height = model.MaxZ() - model.MinZ();
		render::Vec3 center = actor.feet + render::Vec3{0, 0, height * 0.5f};
		actor.distance = render::Length(center - m_camera_position);
		if (actor.distance > far_plane + height || !m_rasterizer.SphereVisible(center, height * 0.75f))
			continue;
		render::Transform placement;
		placement.rotation = render::AxisAngle({0, 0, 1}, render::HeadingToRadians(actor.heading));
		placement.translation = actor.feet + render::Vec3{0, 0, model.OriginOverFeet()};
		render::Vec3 to_camera = render::Normalize(m_camera_position - center);
		render::Vec3 light{to_camera.x, to_camera.y, 1.2f};
		model.Draw(m_rasterizer, placement, light, m_model_ambient, m_model_diffuse, static_cast<uint64_t>(actor.animation_ms),
			actor.animation);
		actor.drawn = true;
	}
}

std::vector<ZoneView::NameTag> ZoneView::NameTags(float range) const
{
	std::vector<NameTag> tags;
	int x, y;
	if (NamePosition(x, y))
		tags.push_back({0, x, y, 0});
	for (const auto &[id, actor] : m_actors) {
		if (!actor.drawn || actor.distance > range)
			continue;
		float sx, sy;
		render::Vec3 top = actor.feet + render::Vec3{0, 0, actor.model->MaxZ() - actor.model->MinZ() + 0.6f};
		if (m_rasterizer.Project(top, sx, sy) && !m_rasterizer.Occluded(top))
			tags.push_back({id, static_cast<int>(sx), static_cast<int>(sy), actor.distance});
	}
	std::sort(tags.begin(), tags.end(), [](const NameTag &a, const NameTag &b) { return a.distance < b.distance; });
	return tags;
}

bool ZoneView::NamePosition(int &x, int &y) const
{
	if (!m_model_shown || !m_loaded || !m_error.empty())
		return false;
	float sx, sy;
	render::Vec3 top = m_feet + render::Vec3{0, 0, m_model->MaxZ() - m_model->MinZ() + 0.6f};
	if (!m_rasterizer.Project(top, sx, sy))
		return false;
	x = static_cast<int>(sx);
	y = static_cast<int>(sy);
	return true;
}
