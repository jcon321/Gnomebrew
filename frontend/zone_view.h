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
#include "player_movement.h"
#include "rasterizer.h"
#include "sky.h"
#include "zone_scene.h"
#include "zone_session.h"

#include <atomic>
#include <map>
#include <memory>
#include <set>
#include <vector>
#include <optional>
#include <string>
#include <thread>

class ZoneView
{
public:
	enum class CameraView { FirstPerson, Chase };

	// renderer: the GPU one, or nullptr for the software Rasterizer.
	ZoneView(std::string eq_dir, std::unique_ptr<render::Renderer> renderer = nullptr);
	~ZoneView();

	void StartLoading(const ZoneInfo &info);
	bool Loaded() const { return m_loaded; }
	const std::string &Error() const { return m_error; } // valid once loaded
	const std::string &ZoneName() const { return m_info.short_name; }

	// Our own character: appearance (and size) and speeds from OP_ZoneEntry, the starting
	// position from the server. From then on Move() moves them.
	void SetPlayer(const render::CharacterAppearance &appearance, float walk_speed, float run_speed);
	void SetPlayerPosition(float x, float y, float z, float heading);
	// Runs our movement up to now. Returns true when a jump started. `looking` is a mouse
	// drag being held, which keeps the chase camera where it's put while moving.
	bool Move(const MoveControls &controls, uint64_t now_ms, bool looking);
	bool Placed() const { return m_placed; }
	ZoneMovement Movement() const; // for OP_ClientUpdate

	// Everyone else, every frame: their models, carried on from their last update.
	void UpdateSpawns(const std::map<uint16_t, ZoneSpawn> &spawns, uint16_t my_id, uint64_t now_ms);
	struct NameTag
	{
		uint16_t id; // 0: us
		int x, y;    // over the head, after Render
		float distance;
	};
	// Names to draw over heads (CDisplay's name sprites): us in the chase view, and spawns in view
	// within `range`, nearest first.
	std::vector<NameTag> NameTags(float range) const;

	// F9 toggles first person and the chase camera. Orbit circles the chase camera around the
	// character (in 512ths), Turn turns the character (and the camera with them), FaceCamera
	// turns the character to where the chase camera looks, Pitch tilts the view (degrees), Zoom
	// moves the chase camera in or out, from first person out to 200 units (CDisplay::DoCamAi's
	// 2..200).
	void CycleCamera();
	CameraView View() const { return m_view; }
	void Orbit(float delta) { m_orbit += delta; }
	void Turn(float delta);
	void FaceCamera();
	void Pitch(float degrees);
	void Zoom(int notches);

	// Norrath's time (ZoneSession::GameTime): the ambient light, the sky and the fog follow it.
	void SetGameTime(int hour, float minute);
	struct DayLight
	{
		render::Vec3 ambient{0.6f, 0.6f, 0.6f}; // added to the zone's baked lighting
		float sky = 1;                           // the sky's brightness
	};
	DayLight Lighting() const;

	void Render(Canvas &canvas, uint64_t now_ms);
	// Where to draw the name over our character's head, after Render (chase view only).
	bool NamePosition(int &x, int &y) const;

	// For the preview tool.
	const render::CharacterModel *Model() const { return m_model.get(); }
	size_t TrianglesDrawn() const { return m_triangles_drawn; }
	// CPU time of the last Render's parts, ms (the debug menu's performance rows).
	struct RenderTimes
	{
		double zone = 0, models = 0, submit = 0;
	};
	const RenderTimes &Times() const { return m_times; }
	// For the debug menu: what the last frame drew, and how we're moving and looking.
	struct DebugStats
	{
		size_t triangles = 0;
		const char *renderer = ""; // Renderer::Name
		size_t spawns_drawn = 0, spawns_built = 0, spawns = 0; // models drawn, built, spawns tracked
		size_t models_cached = 0;                               // distinct spawn models loaded
		float heading = 0;                                      // 0..512, 0 north, 128 west
		render::Vec3 velocity;                                  // per tick, world space
		bool on_ground = true;
		std::string animation;
		float animation_rate = 1;
		CameraView view = CameraView::Chase;
		float camera_distance = 0, camera_pitch = 0;
		bool have_floor = false;
		float feet_z = 0, floor_z = 0;                          // world z of our feet and the floor under them
		size_t zone_triangles = 0, zone_objects = 0;
		DayLight light;
	};
	DebugStats Stats() const;
	// One spawn as we show it; false when we aren't tracking it.
	struct SpawnDebug
	{
		std::string model; // actor tag, empty without a model
		std::string animation;
		float animation_rate = 1;
		bool drawn = false;
		float camera_distance = 0;
	};
	bool SpawnStats(uint16_t id, SpawnDebug &out) const;
	// The spawn drawn under a window pixel, nearest first (after Render); 0 for none.
	uint16_t PickSpawn(int x, int y) const;
	// Debug drawing: triangle outlines instead of the scene, no fog (out to the zone's max clip),
	// collision walls near us outlined, and a marker on the floor under our feet.
	struct DebugDraw
	{
		bool wireframe = false, no_fog = false, walls = false, floor = false;
	};
	void SetDebugDraw(const DebugDraw &draw) { m_debug_draw = draw; }
	const render::Vec3 &Feet() const { return m_feet; }

private:
	static constexpr float MinDistance = 3, MaxDistance = 200;

	void Load();
	void PlacePlayer();
	struct SpawnActor;
	void PoseSpawn(SpawnActor &actor, const ZoneSpawn &spawn, uint64_t now_ms);
	std::shared_ptr<render::CharacterModel> SpawnModel(const ZoneSpawn &spawn);
	void DrawSpawns();
	float FarPlane() const; // the fog's end, or the zone's max clip with the debug menu's no fog
	void DrawDebugOverlays(Canvas &canvas) const;
	void ChooseAnimation(float ms);
	render::Camera MakeCamera(float &distance) const; // distance: the chase camera's, after walls pull it in

	std::string m_eq_dir;
	ZoneInfo m_info;
	std::thread m_loader;
	std::atomic<bool> m_loaded{false};
	std::string m_error;

	render::ZoneScene m_zone;
	render::Sky m_sky;
	render::CharacterModels m_models;
	std::unique_ptr<render::Renderer> m_renderer;

	std::optional<render::CharacterAppearance> m_appearance;
	bool m_model_dirty = false;
	std::unique_ptr<render::CharacterModel> m_model;
	bool m_model_shown = false;
	render::Vec3 m_server_position; // x, y, z as the server sends them
	float m_heading = 0;
	bool m_position_dirty = false;
	render::Vec3 m_feet; // world space
	float m_height = 6;  // the model's
	float m_walk_speed = 0.46f, m_run_speed = 0.7f;
	PlayerMovement m_movement;
	bool m_placed = false;
	uint64_t m_last_move_ms = 0;
	std::string m_animation = "P01";
	double m_animation_ms = 0;
	float m_animation_rate = 1;
	DebugDraw m_debug_draw;
	bool m_animation_reversed = false;

	// Other spawns: models shared by everyone who looks the same, built once they're in range.
	struct SpawnActor
	{
		std::shared_ptr<render::CharacterModel> model;
		bool resolved = false; // model looked up (it may still be null: no model for the race)
		render::Vec3 feet;
		float heading = 0;
		std::string animation = "P01";
		float animation_rate = 1;
		bool hold_last_frame = false;
		double animation_ms = 0;
		uint64_t last_ms = 0;
		float distance = 0; // from the camera, last frame
		bool drawn = false;
	};
	std::map<uint16_t, SpawnActor> m_actors;
	std::map<std::string, std::shared_ptr<render::CharacterModel>> m_model_cache;
	std::set<std::string> m_missing_models;
	render::Vec3 m_camera_position;
	bool m_have_time = false;
	float m_model_ambient = 0.5f, m_model_diffuse = 0.6f;
	int m_hour = 12;
	float m_minute = 0;

	CameraView m_view = CameraView::Chase;
	float m_orbit = 0;      // chase camera heading relative to the character's
	float m_pitch = -12;    // degrees, negative looks down
	float m_distance = 0;   // chase camera distance; 0 = from the character's height
	size_t m_triangles_drawn = 0;
	RenderTimes m_times;
};
