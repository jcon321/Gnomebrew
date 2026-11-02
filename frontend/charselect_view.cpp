#include "charselect_view.h"

#include "animations.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
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
	// The zone's own lighting only; the hour's ambient light is added as it draws, as in a zone.
	render::ZoneLighting lighting;
	lighting.ambient = 0;
	lighting.point_lights = false; // already baked into the vertex colors
	if (!m_zone.Load(m_eq_dir, m_zone_name, error, lighting))
		m_error = error;
	else if (!m_models.Open(m_eq_dir, error, false, m_velious_textures, m_luclin))
		m_error = error;
	else if (!m_sky.Load(m_eq_dir, 1, error)) // no sky is fine; the rooms are mostly indoors
		fprintf(stderr, "[charselect] %s\n", error.c_str());
	if (m_error.empty())
		LoadSwitches();
	m_loaded = true;
}

void CharacterSelectView::LoadSwitches()
{
	// CCharacterSelect::Activate loads load2's switches (CEverQuest_LoadSwitchesNonAvatar @005472a3):
	// <zone>_switches.dat, 0x90 bytes each as EQSwitch_DoorLoadingCode reads them: the id at 0xc,
	// the name at 0xd, the type at 0x1c and state at 0x1d, the y, x, z, heading and incline at 0x34
	// (floats), inverted at 0x48, the size at 0x60 and the parameter at 0x62. It keeps those with a
	// model, and the particle effects (type 53).
	if (!m_alternate)
		return;
	FILE *file = fopen((m_eq_dir + "/" + m_zone_name + "_switches.dat").c_str(), "rb");
	if (!file)
		return;
	std::vector<ZoneDoor> doors;
	uint8_t record[0x90];
	while (fread(record, sizeof(record), 1, file) == 1) {
		ZoneDoor door;
		door.id = record[0x0c];
		char name[16] = {};
		memcpy(name, record + 0x0d, 15);
		door.name = name;
		door.type = record[0x1c];
		door.open = record[0x1d];
		float f[5];
		memcpy(f, record + 0x34, sizeof(f));
		door.y = f[0];
		door.x = f[1];
		door.z = f[2];
		door.heading = f[3];
		door.incline = static_cast<int16_t>(f[4]);
		door.inverted = record[0x48];
		memcpy(&door.size, record + 0x60, 2);
		memcpy(&door.param, record + 0x62, 2);
		doors.push_back(door);
	}
	fclose(file);
	m_doors.Spawn(doors, 0);
}

void CharacterSelectView::SetCharacter(const render::CharacterAppearance *appearance, uint8_t class_id, uint16_t deity)
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
	if (class_id == m_class && deity == m_deity && (appearance ? m_appearance && same(*appearance, *m_appearance) : !m_appearance))
		return;
	m_deity = deity;
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

	// CCharacterSelect::SetCharacterPositionInSelectScreen: in load2 the class picks the room
	// (SetLocationByClass); in the old load zone (IsSpecialLocationRequired) the evil races,
	// classes and deities stand at 0, -315, 6 with the yon at 400, the rest at 0, 0, 7 with it at
	// 140. Heading 0 faces north (+x).
	ClassSpot spot{0, 0, 7};
	m_yon = 0;
	if (m_alternate) {
		if (!SpotForClass(m_class, spot))
			SpotForClass(1, spot);
	}
	else {
		uint16_t race = m_appearance ? m_appearance->race : 1;
		uint8_t c = m_class;
		int d = m_deity;
		bool special = race == 9 || race == 10 || race == 6 || race == 128 || c == 11 || c == 5 || d == 1 || d == 3 ||
		               d == 4 || d == 201 || d == 203 || d == 206;
		spot = special ? ClassSpot{0, -315, 6} : ClassSpot{0, 0, 7};
		m_yon = special ? 400.0f : 140.0f;
	}
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
	float floor_z, exposure = 1;
	if (m_zone.FloorBelow({m_position.x, m_position.y, m_position.z + 5}, 200, floor_z, &exposure))
		m_position.z = floor_z;
	else
		exposure = 1;
	// SetDayPeriod and SetSunLight at that hour, minute 0, time passing (time type 1). SetAmbientLight
	// gives the character's race's vision (SwitchModel's InitInnates) with SetDefaultGameValues'
	// colors: the ambient raised to its color, and that color times 1 - the exposure it follows, the
	// first time taken as it is and after that 0.01 closer each call (ours: our floor's). Infravision's
	// glow is on in the dark rooms' night (the sky's light under 0.53), off by day (over 0.7).
	m_daylight = TimeOfDayLight(m_hour, 0, 1);
	if (m_vision_exposure < 0)
		m_vision_exposure = exposure;
	else if (exposure > m_vision_exposure)
		m_vision_exposure = std::min(m_vision_exposure + 0.01f, exposure);
	else if (exposure < m_vision_exposure)
		m_vision_exposure = std::max(m_vision_exposure - 0.01f, exposure);
	Vision vision = RaceVision(m_appearance ? m_appearance->race : 1);
	if (vision == Vision::Infravision && m_daylight.sky < 0.53f)
		m_infravision_glow = true;
	else if (vision != Vision::Infravision || m_daylight.sky > 0.7f)
		m_infravision_glow = false;
	ApplyVision(m_daylight, vision, m_vision_exposure, m_infravision_glow);
	if (!m_rotate)
		m_camera_heading = m_heading;
	if (m_exploring) {
		// Back in place (LeaveExplorationMode re-selects, SelectCharacter re-enters).
		m_movement.SetRace(m_appearance ? m_appearance->race : 1);
		float height = m_appearance ? render::RaceHeight(m_appearance->race) : 6;
		m_movement.Place(m_position, m_heading, height, 0.46f, 0.7f, 0.4f);
		m_movement.SetEyeHeight(m_model ? (m_model->HeadZ() - m_model->MinZ()) * 1.05f : height * 0.9f);
		m_explore_heading = m_heading;
		m_others_dirty = true;
	}
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
	if (m_exploring) {
		// Render_MinWorld: exploring, our character moves as it walks (no random picks).
		ChooseExploreAnimation(now_ms);
		return;
	}
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
			id = PickAnimation(*m_model, m_appearance->race, m_class);
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

int CharacterSelectView::PickAnimation(const render::CharacterModel &model, uint16_t race, uint8_t class_id)
{
	auto has = [&model](int id) { return model.HasAnimation(render::AnimationCode(id)); };
	// Ours (CharSelectEmotes): half the picks are a social's animation, of those the model has.
	std::vector<int> socials;
	for (int id : SocialAnimations)
		if (m_emotes && has(id))
			socials.push_back(id);
	if (!socials.empty() && m_random() % 2 == 0)
		return socials[m_random() % socials.size()];

	const std::vector<int> &set = ClassAnimations(class_id);
	int id = set[m_random() % set.size()];
	if (!has(id)) {
		id = render::FallbackAnimation(id, race);
		if (!has(id))
			id = render::Anim::Idle;
	}
	return id;
}

// ---------------------------------------------------------------- explore mode

void CharacterSelectView::EnterExplore(const std::vector<ExploreCharacter> &others)
{
	// CCharacterSelect::EnterExplorationMode: the default camera (CDisplay_SwitchToDefaultCameraMode)
	// and the other characters round the selected one (CreateExplorationModePlayers).
	m_exploring = true;
	m_pending_others = others;
	m_others.clear();
	m_pitch = 0;
	m_last_move_ms = 0;
	m_dirty = true; // placed afresh, the movement with it
}

void CharacterSelectView::LeaveExplore()
{
	// LeaveExplorationMode: the others go (RemoveNonSelectedCharacters), the character stops and
	// is put back in its place (SelectCharacter), the character select camera again.
	m_exploring = false;
	m_others.clear();
	m_pending_others.clear();
	m_dirty = true;
}

void CharacterSelectView::PlaceOthers()
{
	// CreateExplorationModePlayers: each other character in turn (SetRoomLocationByIndex): our
	// class's room spot (SetLocationByClass, not dropped to the floor) 4 up, then round us by its
	// index, 20 out (15 for a magician, 12 for a necromancer first), facing us
	// (EQPlayer::FacePlayer), then on the floor (PositionOnFloor). In the server's y and x.
	m_others_dirty = false;
	m_others.clear();
	if (!m_loaded || !m_error.empty())
		return;
	ClassSpot room{0, 0, 7};
	if (!SpotForClass(m_class, room))
		room = {0, 0, 7};
	int index = 0;
	for (const ExploreCharacter &character : m_pending_others) {
		float y = room.server_y, x = room.server_x, z = room.z + 4;
		float d = m_class == 13 ? 15.0f : 20.0f;
		switch (index) {
		case 0: {
			float first = m_class == 13 ? 15.0f : m_class == 11 ? 12.0f : 20.0f;
			y += first;
			x += first;
			break;
		}
		case 1: y += 20; break;
		case 2: y += d; x -= d; break;
		case 3: x -= 20; break;
		case 4: y -= d; x -= d; break;
		case 5: y -= 20; break;
		case 6: y -= d; x += d; break;
		default: break;
		}
		++index;
		Other other;
		other.character = character;
		std::string error;
		other.model = m_models.Build(character.appearance, error);
		if (!other.model)
			fprintf(stderr, "[charselect] %s\n", error.c_str());
		other.position = {y, x, z}; // WLD x/y are the server's y/x
		render::Vec3 to = m_movement.Feet() - other.position;
		float heading = std::atan2(to.y, to.x) * 256 / render::Pi;
		other.heading = heading < 0 ? heading + 512 : heading;
		float floor_z;
		if (m_zone.FloorBelow({other.position.x, other.position.y, other.position.z + 5}, 200, floor_z))
			other.position.z = floor_z;
		other.next_animation_ms = 0;
		m_others.push_back(std::move(other));
	}
}

void CharacterSelectView::Explore(const MoveControls &controls, uint64_t now_ms)
{
	// ProcessControls and MovePlayer, as in a zone (PlayerMovement), through load2's floors and walls.
	if (!m_exploring || !m_loaded || !m_error.empty() || m_dirty)
		return;
	float ms = m_last_move_ms && now_ms > m_last_move_ms ? static_cast<float>(now_ms - m_last_move_ms) : 0;
	m_last_move_ms = now_ms;
	if (ms <= 0)
		return;
	MoveControls swim = controls;
	swim.pitch = m_pitch * 512 / 360;
	m_movement.Step(swim, ms, m_zone);
	m_position = m_movement.Feet();
	m_explore_heading = m_movement.Heading();
	m_heading = m_explore_heading;
}

void CharacterSelectView::Turn(float delta)
{
	if (m_exploring) {
		m_movement.Turn(delta);
		m_heading = m_explore_heading = m_movement.Heading();
	}
}

void CharacterSelectView::Pitch(float degrees)
{
	if (m_exploring)
		m_pitch = std::clamp(m_pitch + degrees, -80.0f, 60.0f);
}

void CharacterSelectView::ToggleCamera()
{
	// CDisplay_ToggleView, which also makes it the default camera (DAT_0063b8ec).
	if (m_exploring) {
		m_first_person = !m_first_person;
		m_pitch = 0;
	}
}

void CharacterSelectView::Zoom(int notches)
{
	constexpr float MinDistance = 3, MaxDistance = 200; // ZoneView's
	if (!m_exploring || notches == 0)
		return;
	if (m_explore_distance <= 0) {
		float height = m_model ? m_model->MaxZ() - m_model->MinZ() : 6;
		m_explore_distance = height * 2 + 6;
	}
	if (m_first_person) {
		if (notches >= 0)
			return;
		m_first_person = false;
		m_pitch = 0;
		m_explore_distance = MinDistance;
		++notches;
	}
	float distance = m_explore_distance * std::pow(0.85f, static_cast<float>(notches));
	if (distance < MinDistance) {
		m_first_person = true;
		m_pitch = 0;
		m_explore_distance = MinDistance;
		return;
	}
	m_explore_distance = std::min(distance, MaxDistance);
}

void CharacterSelectView::ChooseExploreAnimation(uint64_t now_ms)
{
	// What the character is doing picks the animation, as EQPlayer's animation tracks do: L01 walk
	// (backwards for back), L02 run, L03/L04 jumping, L05 falling, P01 standing.
	int id = render::Anim::Idle;
	const render::Vec3 &velocity = m_movement.Velocity();
	bool moving = std::fabs(velocity.x) >= 0.001f || std::fabs(velocity.y) >= 0.001f;
	if (!m_movement.OnGround())
		id = velocity.z < (moving ? -3.2f : -2.25f) ? render::Anim::Fall : moving ? render::Anim::RunJump : render::Anim::StandJump;
	else if (m_movement.Speed() != 0)
		id = std::fabs(m_movement.Speed()) > m_movement.RunSpeed() * 0.75f ? render::Anim::Run : render::Anim::Walk;
	if (!m_model->HasAnimation(render::AnimationCode(id)))
		id = render::Anim::Idle;
	if (id != m_animation) {
		m_animation = id;
		m_animation_start_ms = now_ms;
	}
	m_walk_animation = id;
}

render::Camera CharacterSelectView::ExploreCamera(float aspect) const
{
	// The default camera: first person at the eyes, or the chase camera behind and above, pulled in
	// where a wall is in the way.
	float height = m_model ? m_model->MaxZ() - m_model->MinZ() : 6;
	float eye = m_model ? (m_model->HeadZ() - m_model->MinZ()) * 1.05f : height * 0.9f;
	float yaw = render::HeadingToRadians(m_explore_heading);
	float pitch = (m_pitch + (m_first_person ? 0.0f : -12.0f)) * render::Pi / 180;
	render::Vec3 look{std::cos(yaw) * std::cos(pitch), std::sin(yaw) * std::cos(pitch), std::sin(pitch)};
	render::Camera camera;
	camera.horizontal_fov = LensFov(aspect);
	camera.near_plane = 0.3f;
	render::Vec3 target = m_position + render::Vec3{0, 0, eye};
	if (m_first_person)
		camera.position = target;
	else {
		float reach = m_explore_distance > 0 ? m_explore_distance : height * 2 + 6;
		float fraction = 1;
		if (m_zone.SegmentHit(target, target - look * (reach + 1), fraction))
			reach = std::max(0.5f, fraction * (reach + 1) - 1);
		camera.position = target - look * reach;
	}
	camera.forward = look;
	return camera;
}

int CharacterSelectView::PickCharacter(int x, int y) const
{
	// s3dPickObjectOnScreen: the nearest model under the cursor.
	if (!m_exploring || !m_loaded || !m_error.empty())
		return -1;
	render::Vec3 from = m_renderer->CurrentCamera().position;
	render::Vec3 direction = m_renderer->ViewRay(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
	float best = 1000, distance;
	int slot = -1;
	for (const Other &other : m_others)
		if (other.model && other.model->RayHit(other.placement, 0, render::AnimationCode(other.animation), false, from, direction,
				best, distance)) {
			best = distance;
			slot = other.character.slot;
		}
	return slot;
}

std::vector<CharacterSelectView::NameTag> CharacterSelectView::NameTags() const
{
	std::vector<NameTag> tags;
	if (!m_loaded || !m_error.empty())
		return tags;
	auto add = [&](int slot, const render::CharacterModel &model, const render::Vec3 &feet) {
		NameTag tag;
		tag.slot = slot;
		float sx, sy;
		render::Vec3 top = feet + render::Vec3{0, 0, model.MaxZ() - model.MinZ() + 0.6f};
		if (!m_renderer->Project(top, sx, sy, tag.depth))
			return;
		tag.x = static_cast<int>(sx);
		tag.y = static_cast<int>(sy);
		tag.focal = m_renderer->Focal();
		tags.push_back(tag);
	};
	if (m_model && !(m_exploring && m_first_person))
		add(-1, *m_model, m_position);
	for (const Other &other : m_others)
		if (other.model)
			add(other.character.slot, *other.model, other.position);
	return tags;
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
	if (m_others_dirty)
		PlaceOthers();
	AdvanceCamera(now_ms);
	Animate(now_ms);
	// Render_MinWorld: the doors move (EQSwitch_UpdateDoorStates), and everyone else plays
	// DoClassRandomAnimation's picks.
	m_doors.Update(m_zone, now_ms);
	for (Other &other : m_others) {
		if (!other.model)
			continue;
		if (now_ms >= other.next_animation_ms) {
			if (other.next_animation_ms != 0) {
				other.animation = PickAnimation(*other.model, other.character.appearance.race, other.character.class_id);
				other.animation_start_ms = now_ms;
			}
			other.next_animation_ms = now_ms + (m_random() % 11 + 5) * 1000;
		}
		else if (other.animation != render::Anim::Idle &&
		         now_ms - other.animation_start_ms >= other.model->AnimationMs(render::AnimationCode(other.animation))) {
			other.animation = render::Anim::Idle;
			other.animation_start_ms = now_ms;
		}
	}

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
	if (m_exploring)
		camera = ExploreCamera(static_cast<float>(canvas.Width()) / canvas.Height());
	// SetFog: the yon is the fog's end (over SetLoadZoneWeather's 5000); past it, the sky. The old
	// load zone's is SetCharacterPositionInSelectScreen's.
	camera.fog = true;
	FogRange(camera.fog_start, camera.fog_end);
	camera.far_plane = m_yon > 0 ? m_yon : std::max(camera.fog_end, 50.0f);
	// The fog darkens with the hour's ambient light (SetAmbientLight), as in a zone.
	const DayLight &light = m_daylight;
	float level = std::clamp((light.ambient.x + light.ambient.y + light.ambient.z) / 3 / 0.9f, 0.12f, 1.0f);
	auto scaled = static_cast<uint32_t>((FogColor & 0xff) * level);
	camera.fog_color = scaled << 16 | scaled << 8 | scaled;

	m_renderer->Begin(canvas.Width(), canvas.Height(), camera);
	// SelectCharacter's CreateUserLight: smlantern's color out to 75, where MovePlayer's MoveLight
	// keeps it, 2 in front of the character and 4 over its center. It lights the room too.
	float center = m_model ? m_model->OriginOverFeet() : 0;
	render::PointLight user;
	std::vector<render::PointLight> scene_lights;
	if (m_user_light) {
		float radius;
		render::CarriedLight(4, m_zone_name.c_str(), now_ms, user.color, radius);
		user.radius = 75;
		float a = render::HeadingToRadians(m_heading);
		user.position = m_position + render::Vec3{std::cos(a) * 2, std::sin(a) * 2, center + 4};
		scene_lights.push_back(user);
	}
	// The zone: its baked light, the ambient over its vertices' exposure, the sun and the light.
	m_renderer->SetSceneLights(light.sun_direction, light.sun_color, scene_lights);
	m_renderer->SetVertexLight({1, 1, 1}, light.ambient, light.special);
	m_zone.Draw(*m_renderer, now_ms, scene_lights, true);
	m_renderer->SetVertexLight({1, 1, 1}, {0, 0, 0});
	for (Other &other : m_others) {
		if (!other.model)
			continue;
		other.placement.rotation = render::AxisAngle({0, 0, 1}, render::HeadingToRadians(other.heading));
		other.placement.translation = other.position + render::Vec3{0, 0, other.model->OriginOverFeet()};
		render::ModelLighting lighting = ModelLightingAt(other.placement.translation, scene_lights.empty() ? nullptr : &user);
		std::string animation = render::AnimationCode(other.animation);
		bool loop = other.animation == render::Anim::Idle;
		other.model->Draw(*m_renderer, other.placement, lighting, now_ms - other.animation_start_ms, animation, loop);
		other.held.Draw(*m_renderer, *other.model, other.placement, now_ms - other.animation_start_ms, animation, loop, now_ms,
			lighting.ambient);
	}
	// In first person our own model isn't drawn.
	if (m_model && !(m_exploring && m_first_person)) {
		render::Transform placement;
		placement.rotation = render::AxisAngle({0, 0, 1}, render::HeadingToRadians(m_heading));
		placement.translation = m_position + render::Vec3{0, 0, m_model->OriginOverFeet()}; // PositionOnFloor
		render::ModelLighting lighting = ModelLightingAt(placement.translation, scene_lights.empty() ? nullptr : &user);
		// The idle loops; a pick stops on its last frame until Animate puts the idle back. Exploring,
		// walking and running loop too.
		std::string animation = render::AnimationCode(m_animation);
		bool loop = m_animation == render::Anim::Idle ||
		            (m_exploring && (m_animation == render::Anim::Walk || m_animation == render::Anim::Run));
		uint64_t time_ms = now_ms - m_animation_start_ms;
		m_model->Draw(*m_renderer, placement, lighting, time_ms, animation, loop);
		m_held.Draw(*m_renderer, *m_model, placement, time_ms, animation, loop, now_ms, lighting.ambient);
	}
	// The sky, around the camera, wherever the zone leaves the screen open, dimmed by the day
	// period as in a zone (ProcessSky).
	bool sky = m_sky.Loaded();
	render::Vec3 sky_tint{light.sky + 0.03f * (1 - light.sky), light.sky + 0.04f * (1 - light.sky), light.sky + 0.10f * (1 - light.sky)};
	if (sky) {
		m_renderer->BeginBackground();
		m_renderer->SetVertexLight(sky_tint, {0, 0, 0});
		m_sky.Draw(*m_renderer, now_ms);
		m_renderer->EndBackground();
	}
	m_triangles_drawn = m_renderer->TriangleCount();
	uint32_t horizon = 0;
	if (sky) {
		uint32_t c = m_sky.HorizonColor();
		auto channel = [](uint32_t v, float f) { return static_cast<uint32_t>(std::clamp(v * f, 0.0f, 255.0f)); };
		horizon = channel(c >> 16 & 0xff, sky_tint.x) << 16 | channel(c >> 8 & 0xff, sky_tint.y) << 8 | channel(c & 0xff, sky_tint.z);
	}
	m_renderer->Finish(canvas.Row(0), canvas.Stride(), horizon);
}

render::ModelLighting CharacterSelectView::ModelLightingAt(const render::Vec3 &center, const render::PointLight *user)
{
	// As in a zone (s3dGetActorScaledAmbient): the ambient and the sun times the exposure of the
	// floor under the character, the world's constant and special ambient, infravision's glow (all
	// players are warm-blooded; CreatePlayerActor gives it the others too), and its 3 brightest lights: the
	// zone's own (lights.wld) and the user light.
	float floor_z, exposure = 1;
	if (!m_zone.FloorBelow(center, 400, floor_z, &exposure))
		exposure = 1;
	render::ModelLighting lighting;
	lighting.ambient = m_daylight.ambient * exposure + m_zone.ConstantAmbient() + m_daylight.special + m_daylight.glow;
	lighting.sun_direction = m_daylight.sun_direction;
	lighting.sun_color = m_daylight.sun_color * exposure;
	int region = m_zone.Regions().Empty() ? -1 : m_zone.Regions().RegionAt(center);
	render::SelectLights(m_zone.StaticLights(), m_zone.StaticLightRegions(), center, region, 3, m_lights_for);
	if (user)
		m_lights_for.push_back(*user);
	auto score = [&](const render::PointLight &light) {
		render::Vec3 d = light.position - center;
		return light.radius * light.radius * 0.05f / (render::Dot(d, d) + 1);
	};
	std::sort(m_lights_for.begin(), m_lights_for.end(),
		[&](const render::PointLight &a, const render::PointLight &b) { return score(a) > score(b); });
	if (m_lights_for.size() > 3)
		m_lights_for.resize(3);
	lighting.lights = m_lights_for.empty() ? nullptr : &m_lights_for;
	return lighting;
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
