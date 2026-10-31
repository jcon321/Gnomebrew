#include "charselect_view.h"

#include "animations.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

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

	// DoCharacterSelection (the new UI): the zone's fog, 150 to 250 in (200, 200, 200), and
	// CEverQuest's starting min and max clip, toward which SetFog moves the end by the clip plane.
	constexpr float FogStart = 150;
	constexpr float FogEnd = 250;
	constexpr float MinClip = 100;
	constexpr float MaxClip = 1560;
	constexpr uint32_t FogColor = 0xc8c8c8;

	// DoClassRandomAnimation's picks for each class (number_from_set's first number, 26, the O01
	// fidget, is among them): combat (C01..C11), casting (T01..T09) and a few socials.
	const std::vector<int> &ClassAnimations(uint8_t class_id)
	{
		static const std::vector<int> warrior = {26, 1, 2, 3, 4, 5, 6, 7, 8, 27};
		static const std::vector<int> priest = {26, 42, 2, 3, 5, 43, 44, 8};             // cleric, druid, shaman
		static const std::vector<int> knight = {26, 2, 3, 5, 6, 7, 8, 60, 42, 43, 44};   // paladin, shadow knight
		static const std::vector<int> ranger = {26, 1, 2, 3, 5, 6, 8, 9, 42, 43, 44};
		static const std::vector<int> monk = {26, 1, 11, 46, 5, 6, 45, 8, 47};
		static const std::vector<int> bard = {26, 41, 2, 3, 5, 6, 39, 40};
		static const std::vector<int> rogue = {26, 2, 5, 6, 8, 4};
		static const std::vector<int> caster = {26, 42, 2, 43, 5, 69, 44, 8, 69};        // necromancer .. enchanter
		static const std::vector<int> beastlord = {26, 1, 2, 3, 5, 6, 8, 42, 43, 44};
		static const std::vector<int> other = {26, 1, 2, 3, 5, 6, 7, 8};
		switch (class_id) {
		case 1: return warrior;
		case 2: case 6: case 10: return priest;
		case 3: case 5: return knight;
		case 4: return ranger;
		case 7: return monk;
		case 8: return bard;
		case 9: return rogue;
		case 11: case 12: case 13: case 14: return caster;
		case 15: return beastlord;
		default: return other;
		}
	}

	// The animations CEverQuest_send_social plays for /dance, /wave, /bow and the rest: S01..S29.
	const int SocialAnimations[] = {27, 28, 29, 30, 31, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
		64, 65, 66, 67, 68, 69, 70, 77};

	// The horizontal field of view of SetViewAngle(90) (DoCharacterSelection, CDisplay_SetCCreateCamera(0)),
	// `aspect` the screen's width over its height. ChangeVideoMode passes it to t3dSetCameraLens,
	// which keeps half; eqgfx (setupCameraTransformations) takes the tangent of that in 512ths of
	// a turn across the screen's width, the near plane at 1 (CDisplay's constructor). So 90 is
	// 63.3 degrees.
	// Zeal's SetCameraLens hook changes the angle in character select (GameState 1, on by default
	// with [Zeal] MouseSmoothing): it reads it as degrees across a 4:3 screen and widens it to keep
	// that height on a wider one, more again past 4:3. The same at 4:3; 88 degrees at 16:9.
	// Character creation (GameState 2) keeps the lens character select left, Zeal's included; the
	// face pick's SetCCreateCamera(100) doesn't set the angle.
	float LensFov(float aspect)
	{
		constexpr float Classic = 4.0f / 3;
		float tangent = std::tan(90.0f / 2 * render::Pi / 180) / Classic;
		float ratio = aspect + std::max(0.0f, (aspect - Classic) * aspect);
		float angle = 2 * std::atan(tangent * ratio) * 180 / render::Pi;
		return angle * render::Pi / 256;
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
	else if (!m_models.Open(m_eq_dir, error, false, m_velious_textures, m_luclin))
		m_error = error;
	else if (!m_sky.Load(m_eq_dir, 1, error)) // no sky is fine; the rooms are mostly indoors
		fprintf(stderr, "[charselect] %s\n", error.c_str());
	m_loaded = true;
}

void CharacterSelectView::SetCharacter(const render::CharacterAppearance *appearance, uint8_t class_id)
{
	auto same = [](const render::CharacterAppearance &a, const render::CharacterAppearance &b) {
		if (a.race != b.race || a.gender != b.gender || a.face != b.face || a.hair_style != b.hair_style ||
			a.hair_color != b.hair_color || a.beard != b.beard || a.beard_color != b.beard_color ||
			a.eye_color1 != b.eye_color1 || a.eye_color2 != b.eye_color2)
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
	m_held.Clear();
	++m_model_serial;
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
	// SetLocationByClass: 16:00 with the clip plane at 1.2 (2.2 for a druid), 23:00 in the darker
	// rooms; SetCharacterPositionInSelectScreen leaves the old load zone at midnight.
	m_hour = 24;
	m_clip_plane = 1;
	if (m_alternate) {
		m_hour = m_class == 7 || m_class == 8 || m_class == 9 || m_class == 11 || m_class == 12 ? 23 : 16;
		m_clip_plane = m_class == 6 ? 2.2f : 1.2f;
	}
	m_animation = render::Anim::Idle;

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

void CharacterSelectView::FogRange(float &start, float &end) const
{
	// CDisplay::SetFog: a clip plane over 1 takes the end out toward the max clip (all the way at 4),
	// the start keeping its distance from it.
	start = FogStart;
	end = FogEnd;
	if (m_clip_plane > 1) {
		end = FogEnd + (m_clip_plane - 1) * (MaxClip - FogEnd) / 3;
		start = std::max(end - (FogEnd - FogStart), 0.0f);
	}
	else if (m_clip_plane < 1) {
		end = MinClip + (m_clip_plane - 0.1f) * (FogEnd - MinClip);
		start = std::max(end - (FogEnd - FogStart), 0.0f);
	}
}

void CharacterSelectView::Animate(uint64_t now_ms)
{
	// DoClassRandomAnimation, every frame: when the next pick is due (5 to 15 seconds on), one of the
	// class's plays once, cutting off whatever was playing; otherwise the idle, once that's done.
	// Picking a face, the pick is the idle.
	if (!m_model)
		return;
	if (m_requested >= 0) {
		// Ours: one asked for plays now, and the next pick comes 5 to 15 seconds after it ends.
		m_animation = m_model->HasAnimation(render::AnimationCode(m_requested)) ? m_requested : render::Anim::Idle;
		m_animation_start_ms = now_ms;
		m_next_animation_ms = now_ms + m_model->AnimationMs(render::AnimationCode(m_animation)) + (m_random() % 11 + 5) * 1000;
		m_requested = -1;
		return;
	}
	int id = render::Anim::Idle;
	bool pick = false;
	if (now_ms >= m_next_animation_ms) {
		m_next_animation_ms = now_ms + (m_random() % 11 + 5) * 1000;
		if (!m_close_up) {
			id = PickAnimation();
			pick = true;
		}
	}
	if (id == m_animation)
		return;
	bool done = now_ms - m_animation_start_ms >= m_model->AnimationMs(render::AnimationCode(m_animation));
	if (!pick && !done)
		return;
	m_animation = id;
	m_animation_start_ms = now_ms;
}

std::vector<int> CharacterSelectView::Animations() const
{
	std::vector<int> ids;
	if (!m_model || !m_loaded)
		return ids;
	for (int id = 1; id < render::Anim::Count; ++id)
		if (m_model->HasAnimation(render::AnimationCode(id)))
			ids.push_back(id);
	return ids;
}

int CharacterSelectView::PickAnimation()
{
	auto has = [this](int id) { return m_model->HasAnimation(render::AnimationCode(id)); };
	// Ours (CharSelectEmotes): half the picks are a social's animation, of those the model has.
	std::vector<int> socials;
	for (int id : SocialAnimations)
		if (m_emotes && has(id))
			socials.push_back(id);
	if (!socials.empty() && m_random() % 2 == 0)
		return socials[m_random() % socials.size()];

	const std::vector<int> &set = ClassAnimations(m_class);
	int id = set[m_random() % set.size()];
	if (!has(id)) {
		id = render::FallbackAnimation(id, m_appearance->race);
		if (!has(id))
			id = render::Anim::Idle;
	}
	return id;
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
	Animate(now_ms);

	// Camera view 5 (DoCamAi): zoom units out along the camera heading, at about two
	// thirds of the character's eye height, looking level at the character.
	float zoom = 6;
	float eye = 5;
	float camera_height = 0.67f; // of the eye height
	if (m_model) {
		uint16_t race = m_appearance->race;
		uint8_t gender = m_appearance->gender;
		// CDisplay_SetCCreateCamera(0): 6 out (7 for a troll, ogre or iksar), and with the new UI
		// the height + 2 more. The eye is SetHeights' CameraHeightOffset, 6.2 x the scale
		// (the origin is 3.125 x it over the feet).
		float scale = m_model->OriginOverFeet() / 3.125f;
		if (race == 9 || race == 10 || race == 128)
			zoom = 7;
		zoom += scale * 5 + 2;
		eye = 6.2f * scale;
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
	camera.horizontal_fov = LensFov(static_cast<float>(canvas.Width()) / canvas.Height());
	camera.near_plane = 0.5f;
	// SetFog: the yon is the fog's end (over SetLoadZoneWeather's 5000); past it, the sky.
	camera.fog = true;
	FogRange(camera.fog_start, camera.fog_end);
	camera.far_plane = std::max(camera.fog_end, 50.0f);
	// The fog darkens with the hour's ambient light (SetAmbientLight), as in a zone: 0.6 at 16:00,
	// (0.18, 0.18, 0.3) at night.
	float level = m_hour == 16 ? 0.6f / 0.9f : (0.18f + 0.18f + 0.3f) / 3 / 0.9f;
	auto scaled = static_cast<uint32_t>((FogColor & 0xff) * level);
	camera.fog_color = scaled << 16 | scaled << 8 | scaled;

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
		// The idle loops; a pick stops on its last frame until Animate puts the idle back.
		std::string animation = render::AnimationCode(m_animation);
		bool loop = m_animation == render::Anim::Idle;
		uint64_t time_ms = now_ms - m_animation_start_ms;
		m_model->Draw(*m_renderer, placement, lighting, time_ms, animation, loop);
		m_held.Draw(*m_renderer, *m_model, placement, time_ms, animation, loop, now_ms, lighting.ambient);
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

bool CharacterSelectView::NamePosition(int &x, int &y, float &depth, float &focal) const
{
	if (!m_model || !m_loaded || !m_error.empty())
		return false;
	float sx, sy;
	render::Vec3 top = m_position + render::Vec3{0, 0, m_model->MaxZ() - m_model->MinZ() + 0.6f};
	if (!m_renderer->Project(top, sx, sy, depth))
		return false;
	x = static_cast<int>(sx);
	y = static_cast<int>(sy);
	focal = m_renderer->Focal();
	return true;
}
