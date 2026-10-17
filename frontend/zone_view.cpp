#include "zone_view.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

ZoneView::ZoneView(std::string eq_dir) : m_eq_dir(std::move(eq_dir)) {}

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
	if (!m_zone.Load(m_eq_dir, m_info.short_name, error))
		m_error = error;
	else if (!m_models.Open(m_eq_dir, error))
		m_error = error;
	else if (m_info.sky != 0 && m_info.fog_end >= 101 && !m_sky.Load(m_eq_dir, m_info.sky, error))
		fprintf(stderr, "[zone view] %s\n", error.c_str()); // the zone still draws, over the fog color
	m_loaded = true;
}

void ZoneView::SetPlayer(const render::CharacterAppearance &appearance)
{
	m_appearance = appearance;
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
	m_feet = {m_server_position.y, m_server_position.x, m_server_position.z - height / 5 * 3.125f}; // WLD x/y are the server's y/x
	float floor_z;
	if (m_zone.FloorBelow({m_feet.x, m_feet.y, m_feet.z + 2}, 10, floor_z))
		m_feet.z = floor_z;
	if (m_distance <= 0)
		m_distance = height * 2 + 6;
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
	camera.fog_color = m_info.fog_color;
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
	m_zone.Draw(m_rasterizer, now_ms);
	// Our own model isn't drawn in first person, or with the camera pushed into our head.
	m_model_shown = m_model && m_view == CameraView::Chase && distance >= 1.5f;
	if (m_model_shown) {
		render::Transform placement;
		placement.rotation = render::AxisAngle({0, 0, 1}, render::HeadingToRadians(m_heading));
		placement.translation = m_feet - render::Vec3{0, 0, m_model->MinZ()};
		// Lit from high over the camera's shoulder.
		float yaw = render::HeadingToRadians(m_heading + m_orbit);
		render::Vec3 light{-std::cos(yaw), -std::sin(yaw), 1.2f};
		m_model->Draw(m_rasterizer, placement, light, 0.5f, 0.6f, now_ms);
	}
	bool sky = m_sky.Loaded();
	if (sky) {
		m_rasterizer.BeginBackground();
		m_sky.Draw(m_rasterizer, now_ms);
		m_rasterizer.EndBackground();
	}
	m_triangles_drawn = m_rasterizer.TriangleCount();
	m_rasterizer.Finish(canvas.Row(0), canvas.Stride(), sky ? m_sky.HorizonColor() : m_info.fog_color);
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
