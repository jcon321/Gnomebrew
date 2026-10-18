#include "charselect_view.h"

#include <cmath>
#include <cstdio>

namespace
{
	struct ClassSpot
	{
		float server_y, server_x, z;
	};

	// CCharacterSelect_SetLocationByClass (@0040fa3a): EQPlayer Position.y/.x/.z per class.
	bool SpotForClass(uint8_t class_id, ClassSpot &spot)
	{
		static const ClassSpot spots[16] = {
			{0, 0, 0},
			{600, 1475, -180},   // warrior
			{603.5f, -328, -85}, // cleric
			{-847, -250, -180},  // paladin
			{-45, 1415, 0},      // ranger
			{0, 680, 135},       // shadow knight
			{60, -950, 515},     // druid
			{0, -1120, 385},     // monk
			{60, -230, -870},    // bard
			{-790, 640, -20},    // rogue
			{2, 490, -981},      // shaman
			{-850, -1275, -330}, // necromancer
			{600, 680, -25},     // wizard
			{-840, -1274, -205}, // magician
			{-885, 685, 160},    // enchanter
			{0, -260, -720},     // beastlord
		};
		if (class_id < 1 || class_id > 15)
			return false;
		spot = spots[class_id];
		return true;
	}
}

CharacterSelectView::CharacterSelectView(std::string eq_dir, std::unique_ptr<render::Renderer> renderer)
	: m_eq_dir(std::move(eq_dir)), m_renderer(renderer ? std::move(renderer) : std::make_unique<render::Rasterizer>())
{
}

CharacterSelectView::~CharacterSelectView()
{
	if (m_loader.joinable())
		m_loader.join();
}

void CharacterSelectView::StartLoading()
{
	if (m_loader.joinable() || m_loaded)
		return;
	m_loader = std::thread([this] { Load(); });
}

void CharacterSelectView::Load()
{
	FILE *probe = fopen((m_eq_dir + "/load2.s3d").c_str(), "rb");
	m_alternate = probe != nullptr;
	if (probe)
		fclose(probe);
	m_zone_name = m_alternate ? "load2" : "load";

	std::string error;
	if (!m_zone.Load(m_eq_dir, m_zone_name, error))
		m_error = error;
	else if (!m_models.Open(m_eq_dir, error))
		m_error = error;
	else if (!m_sky.Load(m_eq_dir, 1, error)) // no sky is fine; the rooms are mostly indoors
		fprintf(stderr, "[charselect] %s\n", error.c_str());
	m_loaded = true;
}

void CharacterSelectView::SetCharacter(const render::CharacterAppearance *appearance, uint8_t class_id)
{
	auto same = [](const render::CharacterAppearance &a, const render::CharacterAppearance &b) {
		if (a.race != b.race || a.gender != b.gender || a.face != b.face)
			return false;
		for (int k = 0; k < render::EquipSlots; ++k)
			if (a.equip[k] != b.equip[k] || a.tint[k] != b.tint[k])
				return false;
		return true;
	};
	if (class_id == m_class && (appearance ? m_appearance && same(*appearance, *m_appearance) : !m_appearance))
		return;
	if (appearance)
		m_appearance = *appearance;
	else
		m_appearance.reset();
	m_class = class_id;
	m_dirty = true;
}

void CharacterSelectView::PlaceCharacter()
{
	m_dirty = false;
	m_model.reset();
	if (m_appearance) {
		std::string error;
		m_model = m_models.Build(*m_appearance, error);
		if (!m_model)
			fprintf(stderr, "[charselect] %s\n", error.c_str());
	}

	// CCharacterSelect::SetCharacterPositionInSelectScreen: in load2 the class picks the
	// room; the old load zone uses the origin. Heading 0 faces north (+x).
	ClassSpot spot{0, 0, 7};
	if (m_alternate && !SpotForClass(m_class, spot))
		SpotForClass(1, spot);
	m_position = {spot.server_y, spot.server_x, spot.z}; // WLD x/y are the server's y/x
	m_heading = 0;

	// EQPlayer::PositionOnFloor: drop to the surface below.
	float floor_z;
	if (m_zone.FloorBelow({m_position.x, m_position.y, m_position.z + 5}, 200, floor_z))
		m_position.z = floor_z;
	if (!m_rotate)
		m_camera_heading = m_heading;
}

void CharacterSelectView::AdvanceCamera(uint64_t now_ms)
{
	// DoCharacterSelection: once the 50 ms step is due, the camera heading follows the
	// player, or turns 3 units when Rotate is checked (a full turn in about 8.5 seconds).
	// At most one step per frame, and the next is due 50 ms after this frame.
	if (m_camera_override) {
		m_camera_heading = *m_camera_override;
		return;
	}
	if (m_close_up) {
		m_camera_heading = m_heading;
		return;
	}
	if (m_next_step_ms >= now_ms)
		return;
	m_next_step_ms = now_ms + 50;
	if (m_rotate)
		m_camera_heading = std::fmod(m_camera_heading + 3, 512.0f);
	else
		m_camera_heading = m_heading;
}

void CharacterSelectView::Render(Canvas &canvas, uint64_t now_ms)
{
	if (!m_loaded || !m_error.empty()) {
		canvas.Fill({0, 0, canvas.Width(), canvas.Height()}, 0x000000);
		return;
	}
	if (m_loader.joinable())
		m_loader.join();
	if (m_dirty)
		PlaceCharacter();
	AdvanceCamera(now_ms);

	// Camera view 5 (DoCamAi): zoom units out along the camera heading, at about two
	// thirds of the character's eye height, looking level at the character.
	float zoom = 6;
	float eye = 5;
	float camera_height = 0.67f; // of the eye height
	if (m_model) {
		uint16_t race = m_appearance->race;
		uint8_t gender = m_appearance->gender;
		if (race == 9 || race == 10 || race == 128) // troll, ogre, iksar (CDisplay_SetCCreateCamera)
			zoom = 7;
		eye = (m_model->HeadZ() - m_model->MinZ()) * 1.05f;
		if (m_close_up) {
			// CDisplay_SetCCreateCamera(100) plus CFacePick::Activate's 1.5, level with the face.
			zoom = gender == 0 || race == 3 || race == 5 ? 1.5f : 1.3f;
			if (race == 8 || race == 11 || race == 12)
				zoom = gender == 0 ? 2.5f : 1.5f;
			if (race == 9 || race == 10)
				zoom = gender == 0 || race == 10 ? 4.0f : 3.0f;
			if (race == 128)
				zoom = 2.5f;
			if (race == 130)
				zoom = 2.4f;
			zoom += 1.5f;
			camera_height = 1.0f;
		}
	}
	float angle = render::HeadingToRadians(m_camera_heading);
	render::Vec3 offset{std::cos(angle) * zoom, std::sin(angle) * zoom, 0};
	render::Camera camera;
	camera.position = m_position + offset + render::Vec3{0, 0, eye * camera_height};
	camera.forward = offset * -1.0f;
	camera.horizontal_fov = render::Pi / 2; // SetViewAngle(90)
	camera.near_plane = 0.5f;
	camera.far_plane = 5000; // CCharacterSelect::SetLoadZoneWeather: SetYon(5000)

	m_renderer->Begin(canvas.Width(), canvas.Height(), camera);
	m_zone.Draw(*m_renderer, now_ms);
	if (m_model) {
		render::Transform placement;
		placement.rotation = render::AxisAngle({0, 0, 1}, render::HeadingToRadians(m_heading));
		placement.translation = m_position + render::Vec3{0, 0, m_model->OriginOverFeet()}; // PositionOnFloor
		// Light from over the camera's shoulder.
		render::ModelLighting lighting;
		lighting.ambient = {0.45f, 0.45f, 0.45f};
		lighting.sun_direction = (render::Normalize(offset) + render::Vec3{0, 0, 0.8f}) * -1.0f;
		lighting.sun_color = {0.65f, 0.65f, 0.65f};
		m_model->Draw(*m_renderer, placement, lighting, now_ms);
	}
	// The sky, around the camera, wherever the zone leaves the screen open.
	bool sky = m_sky.Loaded();
	if (sky) {
		m_renderer->BeginBackground();
		m_sky.Draw(*m_renderer, now_ms);
		m_renderer->EndBackground();
	}
	m_triangles_drawn = m_renderer->TriangleCount();
	m_renderer->Finish(canvas.Row(0), canvas.Stride(), sky ? m_sky.HorizonColor() : 0x000000);
}

bool CharacterSelectView::NamePosition(int &x, int &y) const
{
	if (!m_model || !m_loaded || !m_error.empty())
		return false;
	float sx, sy;
	render::Vec3 top = m_position + render::Vec3{0, 0, m_model->MaxZ() - m_model->MinZ() + 0.6f};
	if (!m_renderer->Project(top, sx, sy))
		return false;
	x = static_cast<int>(sx);
	y = static_cast<int>(sy);
	return true;
}
