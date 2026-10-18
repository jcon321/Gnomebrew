#include "zone_view.h"

#include "actor_tags.h"
#include "animations.h"

#include <algorithm>
#include <chrono>
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

	// CDisplay::ProcessAnimation: walking and running play faster the faster you go, at
	// 1 + half the horizontal velocity per tick (about 1.75x at a normal run).
	float MoveAnimationRate(float velocity)
	{
		return 1 + velocity * 0.5f;
	}
}

ZoneView::ZoneView(std::string eq_dir, std::unique_ptr<render::Renderer> renderer)
	: m_eq_dir(std::move(eq_dir)), m_renderer(renderer ? std::move(renderer) : std::make_unique<render::Rasterizer>())
{
}

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
	float height = 6; // the size, which the model is drawn 1.25 x as tall as
	if (m_appearance)
		height = m_appearance->height > 0 ? m_appearance->height : render::RaceHeight(m_appearance->race);
	m_height = height;
	m_feet = {m_server_position.y, m_server_position.x, m_server_position.z - height / 5 * 3.125f}; // WLD x/y are the server's y/x
	float floor_z;
	if (m_zone.FloorBelow({m_feet.x, m_feet.y, m_feet.z + 2}, 10, floor_z))
		m_feet.z = floor_z;
	if (m_distance <= 0)
		m_distance = height * 2 + 6;
	m_movement.Place(m_feet, m_heading, height, m_walk_speed, m_run_speed, m_info.gravity);
	m_movement.SetEyeHeight(m_model ? (m_model->HeadZ() - m_model->MinZ()) * 1.05f : height * 0.9f);
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

	// We swim where we look: the view's pitch from where each camera starts out.
	MoveControls swim = controls;
	swim.pitch = (m_pitch - (m_view == CameraView::Chase ? -12.0f : 0.0f)) * 512 / 360;
	bool jumped = m_movement.Step(swim, ms, m_zone);
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
	std::string animation;
	bool reversed = false;
	float rate = 1;
	float speed = m_movement.Speed();
	const render::Vec3 &velocity = m_movement.Velocity();
	if (!m_movement.OnGround()) {
		// ProcessAnimation only falls once we're dropping fast: below -3.2 a tick while moving,
		// -2.25 standing still, more than a jump ever gets to. Until then a jump plays on, and
		// stepping off a ledge keeps walking or running.
		bool moving = std::fabs(velocity.x) >= 0.001f || std::fabs(velocity.y) >= 0.001f;
		if (velocity.z < (moving ? -3.2f : -2.25f))
			animation = "L05";
		else if (m_animation == "L03" || m_animation == "L04")
			animation = m_animation;
		else if (velocity.z > 0)
			animation = speed != 0 ? "L03" : "L04";
	}
	// ProcessAnimation in water: P06 swimming while we move (faster with the speed), L09 treading water.
	float across = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
	if (m_movement.Swimming()) {
		bool moving = std::fabs(velocity.x) >= 0.001f || std::fabs(velocity.y) >= 0.001f;
		animation = moving ? "P06" : "L09";
		rate = moving ? across * 0.5f + 1 : 1;
	}
	if (animation.empty()) {
		if (speed > 0)
			animation = speed >= m_movement.RunSpeed() ? "L02" : "L01";
		else if (speed < 0) {
			animation = "L01";
			reversed = true;
		}
		else if (m_movement.Strafe() != 0)
			animation = "L01"; // ProcessAnimation walks while we're moving but not running
		else
			animation = "P01";
	}
	if (animation == "L01" || animation == "L02")
		rate = MoveAnimationRate(across);
	if (m_model && !m_model->HasAnimation(animation))
		animation = animation == "L03" || animation == "L04" || animation == "L05" || animation == "P06" || animation == "L09" ? "P01" : animation;
	if (animation != m_animation) {
		m_animation = animation;
		m_animation_ms = 0;
	}
	m_animation_reversed = reversed;
	m_animation_rate = rate;
	m_animation_ms += ms * rate;
	// Jumps and falls hold their last frame instead of looping.
	if (m_model && (animation == "L03" || animation == "L04" || animation == "L05")) {
		double length = m_model->AnimationMs(animation);
		if (length > 0)
			m_animation_ms = std::min(m_animation_ms, length);
	}
}

ZoneView::DebugStats ZoneView::Stats() const
{
	DebugStats stats;
	stats.triangles = m_triangles_drawn;
	stats.renderer = m_renderer->Name();
	stats.feet_environment = m_movement.FeetEnvironment();
	stats.body_environment = m_movement.BodyEnvironment();
	stats.head_environment = m_movement.HeadEnvironment();
	for (const std::string &type : m_zone.Regions().Types(m_zone.Regions().RegionAt(m_feet + render::Vec3{0, 0, m_height * 0.625f})))
		stats.region_types += (stats.region_types.empty() ? "" : ", ") + type;
	stats.spawns = m_actors.size();
	for (const auto &[id, actor] : m_actors) {
		stats.spawns_built += actor.model ? 1 : 0;
		stats.spawns_drawn += actor.drawn ? 1 : 0;
	}
	stats.models_cached = m_model_cache.size();
	stats.heading = m_heading;
	stats.velocity = m_movement.Velocity();
	stats.on_ground = m_movement.OnGround();
	stats.animation = m_animation;
	stats.animation_rate = m_animation_rate;
	stats.view = m_view;
	stats.camera_distance = m_distance;
	stats.camera_pitch = m_pitch;
	stats.feet_z = m_feet.z;
	if (m_placed)
		stats.have_floor = m_zone.FloorBelow(m_feet + render::Vec3{0, 0, 2}, 1000, stats.floor_z);
	stats.zone_triangles = m_zone.TriangleCount();
	stats.zone_objects = m_zone.ObjectCount();
	stats.light = Lighting();
	return stats;
}

bool ZoneView::SpawnStats(uint16_t id, SpawnDebug &out) const
{
	auto it = m_actors.find(id);
	if (it == m_actors.end())
		return false;
	const SpawnView &view = it->second;
	out = SpawnDebug{};
	if (view.model)
		out.model = view.model->Code();
	out.animation = render::AnimationCode(view.actor.Animation());
	out.animation_rate = view.actor.AnimationRate();
	out.drawn = view.drawn;
	out.camera_distance = view.distance;
	out.feet = view.Feet();
	out.body_environment = view.actor.BodyEnvironment();
	return true;
}

uint16_t ZoneView::PickSpawn(int x, int y) const
{
	// A box over each drawn model on screen, from its feet to the top of its head and a third of
	// its height either side.
	uint16_t best = 0;
	float best_distance = 0;
	for (const auto &[id, actor] : m_actors) {
		if (!actor.drawn || !actor.model)
			continue;
		float height = actor.model->MaxZ() - actor.model->MinZ();
		float fx, fy, tx, ty;
		if (!m_renderer->Project(actor.Feet(), fx, fy) || !m_renderer->Project(actor.Feet() + render::Vec3{0, 0, height}, tx, ty))
			continue;
		float half = std::max(6.0f, (fy - ty) / 3);
		float center = (fx + tx) / 2;
		if (x < center - half || x > center + half || y < ty - 4 || y > fy + 4)
			continue;
		if (best == 0 || actor.distance < best_distance) {
			best = id;
			best_distance = actor.distance;
		}
	}
	return best;
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
	camera.far_plane = FarPlane();
	camera.fog = !m_debug_draw.no_fog;
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

	using Clock = std::chrono::steady_clock;
	auto since = [](Clock::time_point start) { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); };
	Clock::time_point start = Clock::now();
	float distance;
	render::Camera camera = MakeCamera(distance);
	m_renderer->Begin(canvas.Width(), canvas.Height(), camera);
	DayLight light = Lighting();
	m_renderer->SetVertexLight({1, 1, 1}, light.ambient);
	m_zone.Draw(*m_renderer, now_ms);
	m_renderer->SetVertexLight({1, 1, 1}, {0, 0, 0});
	m_times.zone = since(start);
	start = Clock::now();
	// Characters: the ambient light, and a directional light that weakens with the daylight.
	float level = (light.ambient.x + light.ambient.y + light.ambient.z) / 3;
	m_model_ambient = std::max(0.08f, level * 0.6f);
	m_model_diffuse = 0.1f + 0.55f * std::clamp(level / 0.9f, 0.0f, 1.0f);
	// Our own model isn't drawn in first person, or with the camera pushed into our head.
	m_model_shown = m_model && m_view == CameraView::Chase && distance >= 1.5f;
	PlaceCarriedLights(now_ms);
	if (m_model_shown) {
		render::Transform placement;
		placement.rotation = render::AxisAngle({0, 0, 1}, render::HeadingToRadians(m_heading));
		placement.translation = m_feet + render::Vec3{0, 0, m_model->OriginOverFeet()};
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
		bool held = m_animation == "L03" || m_animation == "L04" || m_animation == "L05"; // jumps and falls stop at the end
		m_model->Draw(*m_renderer, placement, light_direction, m_model_ambient, m_model_diffuse, time, m_animation, !held,
			LightsFor(m_feet + render::Vec3{0, 0, m_height / 5 * 3.125f}));
	}
	m_camera_position = camera.position;
	DrawSpawns();
	m_times.models = since(start);
	start = Clock::now();
	// The sky dims with the day period, toward a deep blue at night.
	bool sky = m_sky.Loaded();
	render::Vec3 sky_tint{light.sky + 0.03f * (1 - light.sky), light.sky + 0.04f * (1 - light.sky), light.sky + 0.10f * (1 - light.sky)};
	if (sky) {
		m_renderer->BeginBackground();
		m_renderer->SetVertexLight(sky_tint, {0, 0, 0});
		m_sky.Draw(*m_renderer, now_ms);
		m_renderer->EndBackground();
	}
	m_triangles_drawn = m_renderer->TriangleCount();
	m_renderer->Finish(canvas.Row(0), canvas.Stride(), sky ? ScaleColor(m_sky.HorizonColor(), sky_tint) : camera.fog_color);
	DrawDebugOverlays(canvas);
	m_times.submit = since(start);
}

float ZoneView::FarPlane() const
{
	float far_plane = std::max(m_info.fog_end, 50.0f);
	return m_debug_draw.no_fog ? std::max(far_plane, m_info.max_clip) : far_plane;
}

void ZoneView::DrawDebugOverlays(Canvas &canvas) const
{
	uint32_t *pixels = canvas.Row(0);
	int stride = canvas.Stride(), width = canvas.Width(), height = canvas.Height();
	auto line = [&](const render::Vec3 &a, const render::Vec3 &b, uint32_t color) {
		float ax, ay, bx, by;
		if (m_renderer->Project(a, ax, ay) && m_renderer->Project(b, bx, by))
			render::DrawLine(pixels, stride, width, height, ax, ay, bx, by, color);
	};
	if (m_debug_draw.wireframe) {
		// Everything drawn (after Finish, so name tags still hide behind walls), as outlines on black.
		canvas.Fill({0, 0, width, height}, 0x000000);
		m_renderer->DrawEdges(pixels, stride, 0x40a060);
	}
	if (m_debug_draw.walls && m_placed) {
		// What MoveHorizontally stops at: solid triangles steeper than a walkable slope, within reach.
		const float reach = 40;
		const auto &solid = m_zone.SolidTriangles();
		for (size_t i = 0; i + 2 < solid.size(); i += 3) {
			const render::Vec3 &a = solid[i], &b = solid[i + 1], &c = solid[i + 2];
			render::Vec3 center = (a + b + c) * (1.0f / 3);
			if (std::fabs(center.x - m_feet.x) > reach || std::fabs(center.y - m_feet.y) > reach ||
				std::fabs(center.z - m_feet.z) > reach)
				continue;
			render::Vec3 normal = render::Normalize(render::Cross(b - a, c - a));
			if (std::fabs(normal.z) >= 0.7f)
				continue;
			line(a, b, 0xff4040);
			line(b, c, 0xff4040);
			line(c, a, 0xff4040);
		}
	}
	if (m_debug_draw.floor && m_placed) {
		// A cross on the floor right under us, and a post from it up to our feet.
		float floor_z;
		if (m_zone.FloorBelow(m_feet + render::Vec3{0, 0, 2}, 1000, floor_z)) {
			render::Vec3 floor{m_feet.x, m_feet.y, floor_z + 0.05f};
			line(floor - render::Vec3{2, 0, 0}, floor + render::Vec3{2, 0, 0}, 0xffe040);
			line(floor - render::Vec3{0, 2, 0}, floor + render::Vec3{0, 2, 0}, 0xffe040);
			line(floor, m_feet, 0xffe040);
		}
	}
}

// ---------------------------------------------------------------- other spawns

std::string ZoneView::SpawnModelKey(const ZoneSpawn &spawn)
{
	char key[512];
	int length = snprintf(key, sizeof(key), "%u/%u/%u/%.2f/%u/%u", spawn.race, spawn.gender, spawn.face, spawn.size,
		spawn.body_texture, spawn.helm);
	for (int k = 0; k < render::EquipSlots; ++k)
		length += snprintf(key + length, sizeof(key) - static_cast<size_t>(length), "/%u:%08x", spawn.equipment[k], spawn.colors[k]);
	return key;
}

std::shared_ptr<render::CharacterModel> ZoneView::SpawnModel(const ZoneSpawn &spawn, const std::string &key)
{
	auto cached = m_model_cache.find(key);
	if (cached != m_model_cache.end())
		return cached->second;
	render::CharacterAppearance appearance;
	appearance.race = spawn.race;
	appearance.gender = spawn.gender;
	appearance.face = spawn.face;
	appearance.height = spawn.size;
	appearance.texture = spawn.body_texture;
	appearance.helm = spawn.helm;
	for (int k = 0; k < render::EquipSlots; ++k) {
		appearance.equip[k] = spawn.equipment[k];
		appearance.tint[k] = spawn.colors[k];
	}
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
	auto me = spawns.find(my_id);
	m_my_light = me != spawns.end() ? me->second.light : 0;
	for (const auto &[id, spawn] : spawns) {
		if (id == my_id || id == 0)
			continue;
		PoseSpawn(m_actors[id], spawn, now_ms);
	}
}

void ZoneView::PlaceCarriedLights(uint64_t now_ms)
{
	// ChangeLight / MoveLight: 2 in front of the spawn and 4 over its center, none in water but
	// the magic ones.
	m_carried_lights.clear();
	m_carried_light_regions.clear();
	auto add = [&](uint8_t type, const render::Vec3 &feet, float center_height, float heading, render::Environment body) {
		render::PointLight light;
		if (!type || (render::Swimming(body) && !render::CarriedLightShinesInWater(type)) ||
			!render::CarriedLight(type, m_info.short_name.c_str(), now_ms, light.color, light.radius))
			return;
		float a = render::HeadingToRadians(heading);
		light.position = feet + render::Vec3{std::cos(a) * 2, std::sin(a) * 2, center_height + 4};
		m_carried_lights.push_back(light);
		m_carried_light_regions.push_back(m_zone.Regions().RegionsTouched(light.position, light.radius));
	};
	if (m_placed)
		add(m_my_light, m_feet, m_height / 5 * 3.125f, m_heading, m_movement.BodyEnvironment());
	for (const auto &[id, view] : m_actors)
		add(view.light, view.Feet(), view.center_height, view.actor.Heading(), view.actor.BodyEnvironment());
}

const std::vector<render::PointLight> *ZoneView::LightsFor(const render::Vec3 &center)
{
	if (m_carried_lights.empty())
		return nullptr;
	int region = m_zone.Regions().Empty() ? -1 : m_zone.Regions().RegionAt(center);
	render::SelectLights(m_carried_lights, m_carried_light_regions, center, region, 3, m_lights_for);
	return &m_lights_for;
}

void ZoneView::PoseSpawn(SpawnView &view, const ZoneSpawn &spawn, uint64_t now_ms)
{
	float ms = view.last_ms && now_ms > view.last_ms ? static_cast<float>(now_ms - view.last_ms) : 0;
	view.last_ms = now_ms;
	float size = spawn.size > 0 ? spawn.size : (render::IsPlayerRace(spawn.race) ? render::RaceHeight(spawn.race) : 6);
	view.actor.Update(spawn, size, m_info.gravity, now_ms);
	view.light = spawn.light;
	view.center_height = size / 5 * 3.125f;

	// Build the model once they're close enough to be seen (and again when their look changes).
	// Up close they move with the floor and the water under them; far off they just carry on.
	float range = std::max(m_info.fog_end, 50.0f) + 50;
	bool near = render::Length(view.Feet() - m_camera_position) < range;
	view.actor.Step(m_zone, ms, near, now_ms);
	if ((near && !view.resolved) || (view.resolved && spawn.look_serial != view.look_serial)) {
		std::string key = SpawnModelKey(spawn);
		if (!view.resolved || key != view.model_key)
			view.model = SpawnModel(spawn, key);
		view.model_key = key;
		view.look_serial = spawn.look_serial;
		view.resolved = true;
	}
	if (view.model)
		view.actor.Animate(spawn, *view.model, ms, now_ms);
}

void ZoneView::DrawSpawns()
{
	float far_plane = FarPlane();
	for (auto &[id, view] : m_actors) {
		view.drawn = false;
		if (!view.model)
			continue;
		const render::CharacterModel &model = *view.model;
		float height = model.MaxZ() - model.MinZ();
		render::Vec3 center = view.Feet() + render::Vec3{0, 0, height * 0.5f};
		view.distance = render::Length(center - m_camera_position);
		if (view.distance > far_plane + height || !m_renderer->SphereVisible(center, height * 0.75f))
			continue;
		render::Transform placement;
		placement.rotation = render::AxisAngle({0, 0, 1}, render::HeadingToRadians(view.actor.Heading()));
		placement.translation = view.Feet() + render::Vec3{0, 0, model.OriginOverFeet()};
		render::Vec3 to_camera = render::Normalize(m_camera_position - center);
		render::Vec3 light{to_camera.x, to_camera.y, 1.2f};
		model.Draw(*m_renderer, placement, light, m_model_ambient, m_model_diffuse, view.actor.AnimationTime(model),
			render::AnimationCode(view.actor.Animation()), view.actor.AnimationLoops(),
			LightsFor(view.Feet() + render::Vec3{0, 0, view.center_height}));
		view.drawn = true;
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
		render::Vec3 top = actor.Feet() + render::Vec3{0, 0, actor.model->MaxZ() - actor.model->MinZ() + 0.6f};
		if (m_renderer->Project(top, sx, sy) && !m_renderer->Occluded(top))
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
	if (!m_renderer->Project(top, sx, sy))
		return false;
	x = static_cast<int>(sx);
	y = static_cast<int>(sy);
	return true;
}
