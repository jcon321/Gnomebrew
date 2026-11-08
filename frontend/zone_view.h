#pragma once

// The 3D view in a zone, as CDisplay::StartWorldDisplay sets it up: <zone>.s3d
// with the objects from <zone>_obj.s3d and the lights.wld point lights, the
// fog and far plane from OP_NewZone (CDisplay::SetFog: the yon is the fog's
// end), the sky when the zone has one, and our own character dressed from
// OP_ZoneEntry, standing where the server put them. The weather (weather.h) draws over it: its
// particle clouds, and its fog in place of the zone's and the sky.
//
// The zone loads on a background thread; Render draws nothing until it's ready.
// Positions are the server's (x, y, z; heading 0..512 counterclockwise from north).

#include "actor_sound.h"
#include "canvas.h"
#include "character_model.h"
#include "day_light.h"
#include "missiles.h"
#include "particles.h"
#include "player_movement.h"
#include "rasterizer.h"
#include "sky.h"
#include "spawn_actor.h"
#include "spell_effects.h"
#include "weather.h"
#include "zone_doors.h"
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
	using Vision = ::Vision; // day_light.h
	static Vision RaceVision(uint16_t race) { return ::RaceVision(race); }

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
	// The Velious armor textures (VeliousTextures); before StartLoading.
	void SetVeliousTextures(bool on) { m_velious_textures = on; }
	// The Luclin player models to use (my-client.ini's UseLuclin*); before StartLoading.
	void SetLuclinModels(const render::LuclinModels &models) { m_luclin = models; }
	void SetPlayerPosition(float x, float y, float z, float heading);
	void SetSwimSkill(int skill) { m_movement.SetSwimSkill(skill); } // the profile's swimming skill
	void SetSafeFallSkill(int skill) { m_safe_fall = skill; }        // for the landing sounds
	// What our buffs add to our speed (eqspa_movement_rate's MovementSpeedModifier).
	void SetSpeedModifier(float modifier) { m_movement.SetSpeedModifier(modifier); }
	// The sound the doors, we and the spawns make (none when unset).
	void SetSound(audio::EqSound *sound) { m_sound = sound; }
	// The zone line (a "DRNTP..." region type) our feet stepped into since the last call, once.
	std::string TakeZoneLine() { return m_movement.TakeZoneLine(); }
	// Runs our movement up to now. Returns true when a jump started. `looking` is a mouse
	// drag being held, which keeps the chase camera where it's put while moving.
	bool Move(const MoveControls &controls, uint64_t now_ms, bool looking);
	bool Placed() const { return m_placed; }
	bool Crouching() const { return m_movement.Crouching(); } // a standing jump winding up
	// Our stance 110 (EQPlayer::ChangeStance): we sit down (P02, held), and stand back up (P02
	// backwards, all of it, even as we move off) when it ends. In first person the view goes down
	// and up with our head.
	void SetSitting(bool sitting);
	bool StandingUp() const { return m_standing_up; } // getting up: P02 backwards hasn't finished
	// EQPlayer::IWasHit: a hit's push on us, `force` along `heading` (512ths) and up at `pitch`.
	void Push(float force, float heading, float pitch);
	// EQPlayer::IDied for us: we fall (D05) and lie there, nothing else played.
	void SetDead(bool dead) { m_dead = dead; }
	// One of the client's own one-shot animations on us (a spell's casting or its hit, a swing the
	// zone sends; PlayAnimationTrack's id), played once while we stand or move on the ground, with
	// its sound; a jump, a fall, the water, crouching or sitting cut it short.
	void PlayAnimation(int id);
	float EyeDrop() const { return m_eye_drop; }
	ZoneMovement Movement() const; // for OP_ClientUpdate

	// The doors and switches (OP_SpawnDoor), and one's new state (OP_MoveDoor). They move as Move runs.
	void SetDoors(const std::vector<ZoneDoor> &doors, uint64_t now_ms) { m_doors.Spawn(doors, now_ms); }
	void MoveDoor(uint8_t id, uint8_t state, uint64_t now_ms) { m_doors.ChangeState(id, state, now_ms); }
	size_t DoorCount() const { return m_doors.Count(); }
	size_t DoorsShown() const { return m_doors.Shown(); }
	// A left click on the window at (x, y), after Render: the door it's on, if any, and whether we
	// can use it (EQSwitch::UseSwitch). `disarm` is our disarm traps skill; `id` and `skill` are for OP_ClickDoor.
	ZoneDoors::Use ClickDoor(int x, int y, int16_t disarm, uint8_t &id, int16_t &skill) const;
	// CastRay: nothing of the zone's between two points (render space); true until it's loaded.
	bool CanSee(const render::Vec3 &from, const render::Vec3 &to) const;

	// Everyone else, every frame: their models, carried on from their last update.
	void UpdateSpawns(const std::map<uint16_t, ZoneSpawn> &spawns, uint16_t my_id, uint64_t now_ms);
	// The light we carry (our ZoneSpawn::light; we're not among the spawns).
	void SetMyLight(uint8_t light) { m_my_light = light; }
	void SetMyLevel(uint8_t level) { m_my_level = level; } // which of a spell effect's emitters our casts get
	// A spell's effect (DoSpellEffect's stage) on the spawns it names, our own id for us; once
	// loaded. They follow the spawns from then on, drawn with the rest.
	void StartSpellEffect(const SpellEffects::Request &request);
	// EQPlayer::PlaySoundA: wave `slot` from a spawn's center, or ours (our own id); false when it's
	// not drawn.
	bool PlayActorSound(uint16_t on, int slot);
	const SpellEffects &Effects() const { return m_spell_effects; }
	// OP_Projectile: a missile (Missiles) on its way from the next Render, drawn with its model and
	// light, a spell's effect riding on it. The spells are for a bolt's (kind 9).
	void SetSpellFile(const SpellFile *spells) { m_missiles.SetSpells(spells); }
	void LaunchMissile(const ZoneMissile &missile);
	const Missiles &Flying() const { return m_missiles; }
	// The spells' stage sounds lately (for the debug menu), newest last: played from the spawn on
	// the game sounds' switch, as EQPlayer::PlaySoundA plays them.
	struct SpellSound
	{
		uint64_t at_ms = 0; // on Effects().Now()'s clock
		int slot = 0, spell = 0, stage = 0;
		uint16_t on = 0;
	};
	const std::vector<SpellSound> &SpellSounds() const { return m_spell_sounds; }
	struct NameTag
	{
		uint16_t id;  // 0: us
		int x, y;     // over the head, after Render
		float depth;  // in front of the camera
		float focal;  // pixels a unit covers at depth 1
	};
	// eqgfx's name sprites (CDisplay::SetNameSpriteState's string sprites, renderNameTag), drawn
	// by the next Render in the 3D pass: a line or two of text over a spawn's head point (id 0
	// us, in the chase view), each line a texture of its text's coverage. They face the camera
	// at the head point's depth, tested against the depth buffer and not written to it, so what's
	// in front hides them pixel by pixel, as eqgfx draws its deferred name text with ZENABLE on.
	struct NameLine
	{
		const render::Texture *texture = nullptr; // white, its alpha the coverage; null: no line
		int width = 0, height = 0; // the coverage's texels (the texture may be bigger)
		int text_width = 0;        // the text's (Measure's), from `left` on
		int left = 0;
	};
	struct NameSprite
	{
		uint16_t id = 0;
		NameLine lines[2];         // the name, then the guild (0.85 of its size)
		uint32_t color = 0xffffff; // the tint before the light (NameSpriteColor, the target's pulse)
	};
	void SetNameSprites(std::vector<NameSprite> sprites, float yon)
	{
		m_name_sprites = std::move(sprites);
		m_name_yon = yon;
	}
	// The spawns drawn last frame (in the view and the zone's visible regions).
	std::vector<uint16_t> DrawnSpawns() const;
	// The name sprites the last Render drew: their head point's screen position, nearest first.
	std::vector<NameTag> NameTags(float yon) const;
	// s3dGetWorldVisibleActorList (flags 0x11): the spawns drawn last frame (in the view and the
	// zone's visible regions) no farther than `range` from the camera.
	struct ViewedSpawn
	{
		uint16_t id;
		float from_camera;   // to its location (CDisplay_SimpleDistance)
		bool camera_clear;   // s3dCollideSphereWithWorld: nothing from 1 over the camera to 2.5 over it
		float from_me;       // feet to feet
		float from_me_2d;    // the same, level
		bool me_clear;       // Zeal's: a clear line between our head or feet and its head or feet
	};
	std::vector<ViewedSpawn> SpawnsInView(float range) const;
	// This frame's ambient light, the sky's (SetAmbientLight), which lights the names too.
	const render::Vec3 &Ambient() const { return m_daylight.ambient; }

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
	using DayLight = ::DayLight;
	DayLight Lighting() const;

	// For the sound: where we hear from (our character's center, where the client puts its 3D
	// listener) and the way the camera faces; and what's over us, for the zone's emitters
	// (CDisplay::SetupEmitterEnvironment and SetGenericEnvironment): whether the floor under us is
	// covered (its exposure under 0.48), and how high the ceiling is over the floor (none: -1).
	struct Hearing
	{
		render::Vec3 position, forward{1, 0, 0};
		bool covered = false;
		float ceiling = -1;
	};
	Hearing SoundListener(bool surroundings) const; // surroundings: work out covered and ceiling too

	void Render(Canvas &canvas, uint64_t now_ms);
	// Where to draw the name over our character's head, after Render (chase view only).
	// renderNameTag: the name sprites, after the models and effects.
	void DrawNameSprites(const render::Camera &camera);
	// UpdateDAGPointers' DagHeadPoint: <tag>HEAD_POINT_DAG, else <tag>HE_DAG; a Luclin model's
	// <tag>HEHEAD_DAG. -1: none, and no name. `lift` is renderSceneHierarchy's for its name:
	// 1.1 of the actor's scale on a DAG whose sixth letter is H (1.2 an ogre's), else 0.55.
	int HeadPoint(const render::CharacterModel &model, float &lift);

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
		render::Environment feet_environment = render::Environment::Dry, body_environment = render::Environment::Dry,
		                    head_environment = render::Environment::Dry;
		std::string region_types; // the body's region's group types, comma-separated
		size_t zone_triangles = 0, zone_objects = 0;
		size_t meshes_in_view = 0, meshes_hidden = 0; // zone meshes in the view, and hidden behind others
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
		render::Vec3 feet; // world space
		render::Environment body_environment = render::Environment::Dry;
	};
	bool SpawnStats(uint16_t id, SpawnDebug &out) const;
	// The spawn that's the nearest thing drawn under a window pixel (after Render; not a ground
	// item in front of it); 0 for none. In the chase view our own model is one of them, as `me`.
	uint16_t PickSpawn(int x, int y, uint16_t me) const;
	// A spawn's ModelHeightOffset (its center over its feet, server units); 0 before its model's in.
	// Ours for our own id.
	float CenterHeight(uint16_t id) const;
	// t3dGetScaledActorBoundingRadius of a spawn's model, or ours for our own id; -1 with no model.
	float BoundingRadius(uint16_t id) const;
	// Things on the ground (OP_GroundSpawn's EQItemList): CreateActor's "<tag>_ACTORDEF", set on
	// the floor under where the zone put it (GetFloorHeight from 1 over it, then 0.5 up), turned
	// to its heading. Server coordinates. Drawn from when the zone's models are in.
	void AddGroundItem(uint32_t drop_id, const std::string &model, float x, float y, float z, float heading);
	void RemoveGroundItem(uint32_t drop_id);
	// EQItemList_is_item_actor on GetClickedActor: the ground item that's the nearest thing drawn
	// under a window pixel (not a spawn in front of it); false for none.
	bool PickGroundItem(int x, int y, uint16_t me, uint32_t &drop_id) const;
	size_t GroundItemsShown() const; // with a model
	// Debug drawing: triangle outlines instead of the scene, no fog (out to the zone's max clip),
	// no clip plane (no far plane or fog at all), collision walls near us outlined, a marker on
	// the floor under our feet, none of the weather (its sound goes on), and the spells' old
	// effects (spells.eff) in place of their Luclin ones (as eqgame without SpellsNew.eff).
	struct DebugDraw
	{
		bool wireframe = false, no_fog = false, walls = false, floor = false;
		bool no_culling = false; // draw what the zone hides too (no Umbra)
		bool no_clip = false;
		bool no_weather = false; // no rain or snow, and the zone's own fog, sky and light
		bool new_spell_effects = true; // SpellsNew.eff's; off, every spell's spells.eff one
		int vision = -1; // a Vision, or -1: our own (our race's; spells and items to come)
	};
	// What we see in: the debug menu's choice, or our own (our race's; spells and items to come).
	Vision CurrentVision() const;
	Vision OwnVision() const;
	void SetDebugDraw(const DebugDraw &draw) { m_debug_draw = draw; }
	// The debug draws that put lines straight onto the canvas (DrawDebugOverlays).
	bool DrawsDebugLines() const { return m_debug_draw.wireframe || m_debug_draw.walls || m_debug_draw.floor; }
	const render::Vec3 &Feet() const { return m_feet; }

	// The options' Clip Plane slider (0.1 to 4 in steps of 0.2; 1, eqgame's default, is the zone's
	// own fog), at its maximum: the view reaches the zone's max clip.
	static constexpr float ClipPlane = 4;
	// CDisplay::SetFog: the zone's fog moved out by the Clip Plane slider (it keeps its depth),
	// without the weather's.
	void ZoneFog(float &start, float &end) const;
	// The weather's effects (Weather::GetEffects), each frame before Render.
	void SetWeather(const Weather::Effects &effects) { m_weather = effects; }
	// For the debug menu: the weather's particles falling, and those carried on into its clouds
	// on the ground and on the water.
	struct WeatherCounts
	{
		size_t falling = 0, ground = 0, water = 0;
	};
	WeatherCounts WeatherParticles() const;

private:
	static constexpr float MinDistance = 3, MaxDistance = 200;
	static constexpr float NoClipFarPlane = 1e7f; // the debug menu's no clip plane

	void Load();
	void PlacePlayer();
	struct SpawnView;
	void PoseSpawn(SpawnView &view, const ZoneSpawn &spawn, uint64_t now_ms);
	static std::string SpawnModelKey(const ZoneSpawn &spawn);
	std::shared_ptr<render::CharacterModel> SpawnModel(const ZoneSpawn &spawn, const std::string &key);
	void DrawSpawns(uint64_t now_ms);
	void DrawGroundItems(uint64_t now_ms);
	// The missiles stepped (once loaded), then drawn.
	void UpdateMissiles(uint64_t now_ms);
	void DrawMissiles(uint64_t now_ms);
	render::Transform MissilePlacement(const Missiles::Missile &missile, uint64_t now_ms) const;
	render::CharacterModel *MissileModel(const std::string &name);
	// s3dPickObjectOnScreen: what's drawn nearest along the ray through a window pixel, a spawn
	// (our own model in the chase view, as `me`) or a ground item.
	struct Picked
	{
		uint16_t spawn = 0;
		bool ground = false;
		uint32_t drop_id = 0;
	};
	Picked Pick(int x, int y, uint16_t me) const;
	struct GroundItemView;
	static render::Transform GroundItemPlacement(const GroundItemView &view);
	// The lights spawns carry (point_lights.h), placed for this frame, and the regions each reaches.
	void PlaceCarriedLights(uint64_t now_ms);
	// The (up to 3) carried lights that light something centered at `center`.
	const std::vector<render::PointLight> *LightsFor(const render::Vec3 &center);
	// How a character centered at `center` is lit (calculateMeshLighting).
	// `warm`: infravision makes it glow at night (not undead, constructs or vampires).
	render::ModelLighting ModelLightingAt(const render::Vec3 &center, bool warm = true);
	// The floor's exposure under `center` (s3dGetActorScaledAmbient), 1 where there's none.
	float ExposureAt(const render::Vec3 &center) const;
	// The fog drawn: the weather's while it's on, else the zone's.
	void FogRange(float &start, float &end) const;
	bool WeatherShown() const { return !m_debug_draw.no_weather; }
	// The weather's particle clouds (CDisplay's do_rain and do_snow), moved on and drawn.
	void DrawWeather(uint64_t now_ms);
	float FarPlane() const; // the fog's end, at least the zone's max clip with no fog, unbounded with no clip
	void DrawDebugOverlays(Canvas &canvas) const;
	void ChooseAnimation(float ms);
	// The sounds: the doors' (EQSwitch::ChangeState), ours and a spawn's (actor_sound.h).
	void DoorSounds();
	ActorSound::Actor MyActor() const;
	int SoundAnimation(bool jumped) const; // what ProcessAnimation would ask PlayAnimationTrack for, for us
	void SpawnSounds(SpawnView &view, const ZoneSpawn &spawn, uint16_t id, uint64_t now_ms);
	render::Camera MakeCamera(float &distance) const; // distance: the chase camera's, after walls pull it in

	std::string m_eq_dir;
	ZoneInfo m_info;
	std::thread m_loader;
	std::atomic<bool> m_loaded{false};
	std::string m_error;

	render::ZoneScene m_zone;
	render::Sky m_sky;
	render::CharacterModels m_models;
	bool m_velious_textures = true;
	render::LuclinModels m_luclin;
	std::unique_ptr<render::Renderer> m_renderer;

	std::optional<render::CharacterAppearance> m_appearance;
	bool m_model_dirty = false;
	std::unique_ptr<render::CharacterModel> m_model;
	render::HeldParticles m_held; // our torch's flame and smoke
	bool m_model_shown = false;
	render::Vec3 m_server_position; // x, y, z as the server sends them
	float m_heading = 0;
	bool m_position_dirty = false;
	render::Vec3 m_feet; // world space
	float m_height = 6;  // the model's
	float m_walk_speed = 0.46f, m_run_speed = 0.7f;
	PlayerMovement m_movement;
	ZoneDoors m_doors;
	bool m_placed = false;
	uint64_t m_last_move_ms = 0;
	bool m_jump_pressed = false; // a press that came on a frame with no time to move in
	std::string m_animation = "P01";
	double m_animation_ms = 0;
	float m_animation_rate = 1;
	DebugDraw m_debug_draw;
	bool m_animation_reversed = false;
	std::string m_action; // PlayAnimation's, while it plays
	bool m_sitting = false, m_standing_up = false;
	float m_eye_drop = 0; // how far below standing our head is now (sitting down, getting up)
	bool m_dead = false;

	// Other spawns: models shared by everyone who looks the same, built once they're in range.
	struct SpawnView
	{
		std::shared_ptr<render::CharacterModel> model;
		std::string model_key; // the look it was built for
		uint32_t look_serial = 0; // ZoneSpawn::look_serial then: an OP_WearChange since builds again
		bool resolved = false; // model looked up (it may still be null: no model for the race)
		SpawnActor actor;      // how it moves and what it's playing
		uint64_t last_ms = 0;
		float distance = 0; // from the camera, last frame
		bool drawn = false;
		// How it was last drawn, for clicks on it.
		render::Transform placement;
		uint64_t time_ms = 0;
		std::string animation;
		bool loops = true;
		uint8_t light = 0;         // ZoneSpawn::light
		bool warm = true;          // body type not 3, 5, 8 or 12 (SetPlayerConstantAmbient2)
		float center_height = 0;   // ModelHeightOffset: the server's z over the feet
		float size = 6;
		ActorSound sound;
		render::HeldParticles held; // a torch's flame and smoke
		render::Environment sound_feet = render::Environment::Dry; // the feet's, as the sound last saw them
		uint16_t race = 0;   // for the spell effects
		bool player = false;
		uint8_t level = 1;
		bool dead = false;   // a corpse: missiles stop steering at it
		const render::Vec3 &Feet() const { return actor.Feet(); }
	};
	std::vector<NameSprite> m_name_sprites;
	float m_name_yon = 0;
	std::vector<NameTag> m_name_tags; // drawn last Render
	std::map<const render::CharacterModel *, std::pair<int, float>> m_head_points; // HeadPoint's, by model
	std::vector<render::Vertex> m_name_vertices;
	std::map<uint16_t, SpawnView> m_actors;
	struct GroundItemView
	{
		std::string model_name;
		render::Vec3 at;     // the server's position, in world space
		float heading = 0;
		bool placed = false; // its model looked for and set on the floor
		std::unique_ptr<render::CharacterModel> model;
		render::Vec3 feet;   // where it's drawn
		float scale = 1;     // CreateActor's for tradeskill containers
		float distance = 0;  // from the camera, last frame
		render::HeldParticles particles; // its own clouds (a torch's flame and smoke)
	};
	std::map<uint32_t, GroundItemView> m_ground_items;
	std::map<std::string, std::shared_ptr<render::CharacterModel>> m_model_cache;
	std::set<std::string> m_missing_models;
	render::Vec3 m_camera_position;
	bool m_have_time = false;
	DayLight m_daylight; // this frame's
	// SetAmbientLight: our floor's exposure, followed 0.01 a frame, and whether infravision's glow
	// is on (from sky light under 0.53 until over 0.7).
	float m_vision_exposure = 1;
	bool m_infravision_glow = false;
	uint64_t m_vision_ms = 0;
	uint8_t m_my_light = 0; // our ZoneSpawn::light
	uint16_t m_my_id = 0;
	uint8_t m_my_level = 1;
	// Our model as Render last posed it, for the spell effects on us.
	uint64_t m_player_time_ms = 0;
	bool m_player_loops = true;
	SpellEffects m_spell_effects;
	bool FindActor(uint16_t id, SpellEffects::Actor &actor) const;
	void PlaySpellSound(const SpellEffects::Request &request, uint16_t on, int slot);
	std::vector<SpellSound> m_spell_sounds;
	Missiles m_missiles;
	std::map<std::string, std::unique_ptr<render::CharacterModel>> m_missile_models; // by name; null not there
	uint64_t m_missiles_ms = 0; // the last UpdateMissiles
	std::vector<render::PointLight> m_carried_lights;
	std::vector<std::vector<int>> m_carried_light_regions;
	std::vector<render::PointLight> m_lights_for; // LightsFor's
	int m_hour = 12;
	float m_minute = 0;
	audio::EqSound *m_sound = nullptr;
	ActorSound m_my_sound;
	render::Environment m_sound_feet = render::Environment::Dry, m_sound_head = render::Environment::Dry;
	int m_safe_fall = 0;

	CameraView m_view = CameraView::Chase;
	float m_orbit = 0;      // chase camera heading relative to the character's
	float m_pitch = -12;    // degrees, negative looks down
	float m_distance = 0;   // chase camera distance; 0 = from the character's height
	size_t m_triangles_drawn = 0;
	RenderTimes m_times;

	Weather::Effects m_weather;
	// A rain's or snow's clouds: those falling carry on into the others where they hit. The
	// newest is last; any before it are stopped, their last particles still falling.
	struct WeatherClouds
	{
		std::unique_ptr<render::ParticleCloud> ground, water, falling;
	};
	std::vector<WeatherClouds> m_weather_clouds;
	Weather::Kind m_cloud_kind = Weather::Kind::None;
	PfsArchive m_equipment; // gequip.s3d: the rain's ring and the snowflake
	render::TextureCache m_effect_textures;
	const render::Texture *m_rain_ring = nullptr, *m_snowflake = nullptr;
};
