#pragma once

// The 3D view in a zone, as CDisplay::StartWorldDisplay sets it up: <zone>.s3d
// with the objects from <zone>_obj.s3d and the lights.wld point lights, the
// fog and far plane from OP_NewZone (CDisplay::SetFog: the yon is the fog's
// end), the sky when the zone has one, and our own character dressed from
// OP_ZoneEntry, standing where the server put them.
//
// The zone loads on a background thread; Render draws nothing until it's ready.
// Positions are the server's (x, y, z; heading 0..512 counterclockwise from north).

#include "canvas.h"
#include "character_model.h"
#include "rasterizer.h"
#include "sky.h"
#include "zone_scene.h"
#include "zone_session.h"

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <thread>

class ZoneView
{
public:
	enum class CameraView { FirstPerson, Chase };

	explicit ZoneView(std::string eq_dir);
	~ZoneView();

	void StartLoading(const ZoneInfo &info);
	bool Loaded() const { return m_loaded; }
	const std::string &Error() const { return m_error; } // valid once loaded
	const std::string &ZoneName() const { return m_info.short_name; }

	// Our own character: appearance (and size) from OP_ZoneEntry, position from the server.
	void SetPlayer(const render::CharacterAppearance &appearance);
	void SetPlayerPosition(float x, float y, float z, float heading);

	// F9 toggles first person and the chase camera. Orbit turns the view (in 512ths; the chase
	// camera circles the character), Pitch tilts it (degrees), Zoom moves the chase camera in or
	// out, from first person out to 200 units (CDisplay::DoCamAi's 2..200).
	void CycleCamera();
	CameraView View() const { return m_view; }
	void Orbit(float delta) { m_orbit += delta; }
	void Pitch(float degrees);
	void Zoom(int notches);

	void Render(Canvas &canvas, uint64_t now_ms);
	// Where to draw the name over our character's head, after Render (chase view only).
	bool NamePosition(int &x, int &y) const;

	// For the preview tool.
	const render::CharacterModel *Model() const { return m_model.get(); }
	size_t TrianglesDrawn() const { return m_triangles_drawn; }
	const render::Vec3 &Feet() const { return m_feet; }

private:
	static constexpr float MinDistance = 3, MaxDistance = 200;

	void Load();
	void PlacePlayer();
	render::Camera MakeCamera(float &distance) const; // distance: the chase camera's, after walls pull it in

	std::string m_eq_dir;
	ZoneInfo m_info;
	std::thread m_loader;
	std::atomic<bool> m_loaded{false};
	std::string m_error;

	render::ZoneScene m_zone;
	render::Sky m_sky;
	render::CharacterModels m_models;
	render::Rasterizer m_rasterizer;

	std::optional<render::CharacterAppearance> m_appearance;
	bool m_model_dirty = false;
	std::unique_ptr<render::CharacterModel> m_model;
	bool m_model_shown = false;
	render::Vec3 m_server_position; // x, y, z as the server sends them
	float m_heading = 0;
	bool m_position_dirty = false;
	render::Vec3 m_feet; // world space

	CameraView m_view = CameraView::Chase;
	float m_orbit = 0;      // chase camera heading relative to the character's
	float m_pitch = -12;    // degrees, negative looks down
	float m_distance = 0;   // chase camera distance; 0 = from the character's height
	size_t m_triangles_drawn = 0;
};
