#pragma once

// The 3D scene behind character select, as CEverQuest::DoCharacterSelection
// sets it up: zone 185 "load2" (or 184 "load" when load2.s3d is missing, per
// CCharacterSelect::AlternateBackgroundAvailable), the selected character
// standing in its class's room (CCharacterSelect_SetLocationByClass), and
// camera view 5 orbiting it (CEverQuest_SetupCharacterSelectCamera,
// CDisplay_SetCCreateCamera, CDisplay::DoCamAi), in the fog DoCharacterSelection sets, the
// character now and then playing one of its class's animations (CDisplay::DoClassRandomAnimation)
// or, our own, a social's.
//
// Explore mode (CCharacterSelect::EnterExplorationMode) lets the character walk the room under the
// default camera (first person, or the chase camera; F9), with the account's other characters
// standing round it (CreateExplorationModePlayers, SetRoomLocationByIndex).
//
// The zone loads on a background thread; Render draws nothing until it's ready.

#include "canvas.h"
#include "character_model.h"
#include "day_light.h"
#include "player_movement.h"
#include "zone_doors.h"
#include "point_lights.h"
#include "rasterizer.h"
#include "sky.h"
#include "zone_scene.h"

#include <atomic>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

class CharacterSelectView
{
public:
	// renderer: the GPU one, or nullptr for the software Rasterizer.
	CharacterSelectView(std::string eq_dir, std::unique_ptr<render::Renderer> renderer = nullptr);
	~CharacterSelectView();

	void StartLoading();
	bool Loaded() const { return m_loaded; }
	const std::string &Error() const { return m_error; } // valid once loaded
	const std::string &ZoneName() const { return m_zone_name; }

	// The models, once loaded (character creation's face picker asks them what they have).
	render::CharacterModels *Models() { return m_loaded && m_error.empty() ? &m_models : nullptr; }
	// What's shown (null: nobody).
	const render::CharacterAppearance *Shown() const { return m_appearance ? &*m_appearance : nullptr; }
	// nullptr shows the empty default room. `deity` places a character in the old load zone
	// (IsSpecialLocationRequired).
	void SetCharacter(const render::CharacterAppearance *appearance, uint8_t class_id, uint16_t deity = 0);

	// Explore mode. `others`: the account's other characters, by slot.
	struct ExploreCharacter
	{
		int slot = -1;
		render::CharacterAppearance appearance;
		uint8_t class_id = 1;
	};
	void EnterExplore(const std::vector<ExploreCharacter> &others);
	void LeaveExplore();
	bool Exploring() const { return m_exploring; }
	// ProcessControls: the keys move the character this frame.
	void Explore(const MoveControls &controls, uint64_t now_ms);
	void Turn(float delta);      // mouse look: 512ths of a turn
	void Pitch(float degrees);   // the view up (+) or down
	void ToggleCamera();         // CDisplay_ToggleView between first person and the chase camera
	// The mouse wheel (ours, as in a zone): out of first person into the chase camera, then 0.85 of
	// the distance a notch in, out to 200; in past 3 back to first person.
	void Zoom(int notches);
	bool FirstPerson() const { return m_first_person; }
	// LeftClickedOnPlayer in explore mode: the slot of the other character under (x, y), or -1.
	int PickCharacter(int x, int y) const;
	// The names over heads after Render (UpdatePlayerVisibility): the selected character's (slot
	// -1; not in first person) and, exploring, the others'.
	struct NameTag
	{
		int slot = -1;
		int x = 0, y = 0;
		float depth = 0, focal = 0;
	};
	std::vector<NameTag> NameTags() const;
	// The Velious armor textures (VeliousTextures); before StartLoading.
	void SetVeliousTextures(bool on) { m_velious_textures = on; }
	// The Luclin player models to use (my-client.ini's UseLuclin*); before StartLoading.
	void SetLuclinModels(const render::LuclinModels &models) { m_luclin = models; }
	// Socials' animations among the random ones (CharSelectEmotes); otherwise only the class's.
	void SetEmotes(bool on) { m_emotes = on; }
	// The animations the character's model has (render::Anim numbers, in order), and a number that
	// changes whenever the model's rebuilt (another character, armor), for whoever lists them.
	std::vector<int> Animations() const;
	uint32_t ModelSerial() const { return m_model_serial; }
	int Animation() const { return m_animation; } // what's playing
	// Plays one of them once, from the next frame, the random picks waiting until it's done.
	void PlayAnimation(int id) { m_requested = id; }
	void SetRotate(bool rotate) { m_rotate = rotate; }
	// CFacePick::Activate: the camera close in front of the face, not turning.
	void SetCloseUp(bool close_up) { m_close_up = close_up; }
	// Pins the camera heading (preview tool); otherwise it follows Rotate.
	void SetCameraHeading(float heading) { m_camera_override = heading; }
	void Render(Canvas &canvas, uint64_t now_ms);
	// SelectCharacter's CDisplay::CreateUserLight, which CCharacterSelect::EnterWorld takes away.
	void SetUserLight(bool on) { m_user_light = on; }
	// The ambient light of the room's hour (SetDayPeriod, SetSunLight), which lights the name.
	const render::Vec3 &Ambient() const { return m_daylight.ambient; }
	// Where to draw the name over the character's head, after Render: the point, how far it is in
	// front of the camera, and the pixels a unit covers at depth 1 (for the name sprite's size).
	bool NamePosition(int &x, int &y, float &depth, float &focal) const;

	const render::Vec3 &Position() const { return m_position; } // the character's feet (preview tool)
	size_t DoorCount() const { return m_doors.Count(); }
	// For the preview tool: the model actually shown and the triangles drawn last frame.
	const render::CharacterModel *Model() const { return m_model.get(); }
	size_t TrianglesDrawn() const { return m_triangles_drawn; }

private:
	void Load();
	void PlaceCharacter();
	void AdvanceCamera(uint64_t now_ms);
	void Animate(uint64_t now_ms);
	// DoClassRandomAnimation's pick for a model of a race and class (ours: socials too).
	int PickAnimation(const render::CharacterModel &model, uint16_t race, uint8_t class_id);
	void LoadSwitches(); // CEverQuest_LoadSwitchesNonAvatar: <zone>_switches.dat
	void PlaceOthers();  // CreateExplorationModePlayers
	render::Camera ExploreCamera(float aspect) const;
	void ChooseExploreAnimation(uint64_t now_ms);
	void FogRange(float &start, float &end) const;
	render::ModelLighting ModelLightingAt(const render::Vec3 &center, const render::PointLight *user);

	std::string m_eq_dir;
	std::thread m_loader;
	std::atomic<bool> m_loaded{false};
	std::string m_error;
	std::string m_zone_name;
	bool m_alternate = false; // load2: rooms by class

	render::ZoneScene m_zone;
	render::Sky m_sky;
	render::CharacterModels m_models;
	bool m_velious_textures = true;
	render::LuclinModels m_luclin;
	std::unique_ptr<render::Renderer> m_renderer;

	// What's selected, applied once loading finishes.
	std::optional<render::CharacterAppearance> m_appearance;
	uint8_t m_class = 1;
	uint16_t m_deity = 0;
	float m_yon = 0; // SetCharacterPositionInSelectScreen's SetYon in the old load zone (0: the fog's end)
	ZoneDoors m_doors;
	bool m_dirty = true;
	std::unique_ptr<render::CharacterModel> m_model;
	render::HeldParticles m_held; // a torch's flame and smoke
	render::Vec3 m_position; // the model's feet, world space
	float m_heading = 0;

	// What the character plays: the idle, or one of DoClassRandomAnimation's picks played once.
	int m_animation = 32; // render::Anim::Idle
	uint64_t m_animation_start_ms = 0;
	uint64_t m_next_animation_ms = 0; // the next pick (EQPlayer's actor + 100)
	std::mt19937 m_random{std::random_device{}()};
	bool m_emotes = true;
	int m_requested = -1; // PlayAnimation's, for the next frame
	uint32_t m_model_serial = 0;
	// CCharacterSelect_SetLocationByClass: the hour (SetDayPeriod) and the clip plane in the room.
	int m_hour = 24;
	float m_clip_plane = 1;
	DayLight m_daylight = TimeOfDayLight(24, 0, 1); // the hour's (CEverQuest's time type 1)
	float m_vision_exposure = -1; // SetAmbientLight's followed exposure (-1: not yet)
	bool m_infravision_glow = false;
	bool m_user_light = true;
	std::vector<render::PointLight> m_lights_for; // the model's, this frame

	bool m_rotate = true;
	bool m_close_up = false;
	float m_camera_heading = 456; // CEverQuest_SetupCharacterSelectCamera
	std::optional<float> m_camera_override;
	uint64_t m_next_step_ms = 0;
	size_t m_triangles_drawn = 0;

	// Explore mode: the character moving, the camera, and the others.
	bool m_exploring = false;
	bool m_first_person = true; // CDisplay's default camera (DAT_0063b8ec), as F9 last left it
	PlayerMovement m_movement;
	float m_pitch = 0;
	float m_explore_distance = 0; // the chase camera's (0: from the character's height)
	float m_explore_heading = 0;
	uint64_t m_last_move_ms = 0;
	int m_walk_animation = -1; // what the moving character plays (-1: the random picks)
	struct Other
	{
		ExploreCharacter character;
		std::unique_ptr<render::CharacterModel> model;
		render::Vec3 position; // feet
		float heading = 0;
		int animation = 32;
		uint64_t animation_start_ms = 0, next_animation_ms = 0;
		render::Transform placement; // last drawn
		render::HeldParticles held;  // its torch's flame, a weapon's fire
	};
	std::vector<Other> m_others;
	std::vector<ExploreCharacter> m_pending_others; // EnterExplore's, placed on the next frame
	bool m_others_dirty = false;
};
