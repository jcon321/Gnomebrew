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
// The zone loads on a background thread; Render draws nothing until it's ready.

#include "canvas.h"
#include "character_model.h"
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

	// nullptr shows the empty default room.
	void SetCharacter(const render::CharacterAppearance *appearance, uint8_t class_id);
	// The Velious armor textures (VeliousTextures); before StartLoading.
	void SetVeliousTextures(bool on) { m_velious_textures = on; }
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
	// Where to draw the name over the character's head, after Render: the point, how far it is in
	// front of the camera, and the pixels a unit covers at depth 1 (for the name sprite's size).
	bool NamePosition(int &x, int &y, float &depth, float &focal) const;

	// For the preview tool: the model actually shown and the triangles drawn last frame.
	const render::CharacterModel *Model() const { return m_model.get(); }
	size_t TrianglesDrawn() const { return m_triangles_drawn; }

private:
	void Load();
	void PlaceCharacter();
	void AdvanceCamera(uint64_t now_ms);
	void Animate(uint64_t now_ms);
	int PickAnimation();
	void FogRange(float &start, float &end) const;

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
	std::unique_ptr<render::Renderer> m_renderer;

	// What's selected, applied once loading finishes.
	std::optional<render::CharacterAppearance> m_appearance;
	uint8_t m_class = 1;
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

	bool m_rotate = true;
	bool m_close_up = false;
	float m_camera_heading = 456; // CEverQuest_SetupCharacterSelectCamera
	std::optional<float> m_camera_override;
	uint64_t m_next_step_ms = 0;
	size_t m_triangles_drawn = 0;
};
