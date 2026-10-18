#pragma once

// The 3D scene behind character select, as CEverQuest::DoCharacterSelection
// sets it up: zone 185 "load2" (or 184 "load" when load2.s3d is missing, per
// CCharacterSelect::AlternateBackgroundAvailable), the selected character
// standing in its class's room (CCharacterSelect_SetLocationByClass), and
// camera view 5 orbiting it (CEverQuest_SetupCharacterSelectCamera,
// CDisplay_SetCCreateCamera, CDisplay::DoCamAi).
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
#include <string>
#include <thread>

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
	void SetRotate(bool rotate) { m_rotate = rotate; }
	// CFacePick::Activate: the camera close in front of the face, not turning.
	void SetCloseUp(bool close_up) { m_close_up = close_up; }
	// Pins the camera heading (preview tool); otherwise it follows Rotate.
	void SetCameraHeading(float heading) { m_camera_override = heading; }
	void Render(Canvas &canvas, uint64_t now_ms);
	// Where to draw the name over the character's head, after Render.
	bool NamePosition(int &x, int &y) const;

	// For the preview tool: the model actually shown and the triangles drawn last frame.
	const render::CharacterModel *Model() const { return m_model.get(); }
	size_t TrianglesDrawn() const { return m_triangles_drawn; }

private:
	void Load();
	void PlaceCharacter();
	void AdvanceCamera(uint64_t now_ms);

	std::string m_eq_dir;
	std::thread m_loader;
	std::atomic<bool> m_loaded{false};
	std::string m_error;
	std::string m_zone_name;
	bool m_alternate = false; // load2: rooms by class

	render::ZoneScene m_zone;
	render::Sky m_sky;
	render::CharacterModels m_models;
	std::unique_ptr<render::Renderer> m_renderer;

	// What's selected, applied once loading finishes.
	std::optional<render::CharacterAppearance> m_appearance;
	uint8_t m_class = 1;
	bool m_dirty = true;
	std::unique_ptr<render::CharacterModel> m_model;
	render::Vec3 m_position; // the model's feet, world space
	float m_heading = 0;

	bool m_rotate = true;
	bool m_close_up = false;
	float m_camera_heading = 456; // CEverQuest_SetupCharacterSelectCamera
	std::optional<float> m_camera_override;
	uint64_t m_next_step_ms = 0;
	size_t m_triangles_drawn = 0;
};
