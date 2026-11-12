#include "zone_view.h"

#include "actor_tags.h"
#include "animations.h"
#include "name_sprites.h"
#include "spell_file.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <utility>

namespace
{
	uint32_t ScaleColor(uint32_t color, const render::Vec3 &scale)
	{
		auto channel = [](uint32_t c, float s) { return static_cast<uint32_t>(std::clamp(c * s, 0.0f, 255.0f)); };
		return (channel((color >> 16) & 0xff, scale.x) << 16) | (channel((color >> 8) & 0xff, scale.y) << 8) |
		       channel(color & 0xff, scale.z);
	}

	// CDisplay::ProcessAnimation: walking and running play faster the faster you go, at
	// 1 + half the horizontal velocity per tick (about 1.75x at a normal run).
	float MoveAnimationRate(float velocity)
	{
		return 1 + velocity * 0.5f;
	}
}

ZoneView::ZoneView(std::string eq_dir, std::unique_ptr<render::Renderer> renderer)
	: m_eq_dir(std::move(eq_dir)), m_renderer(renderer ? std::move(renderer) : std::make_unique<render::Rasterizer>())
{
}

void ZoneView::SetGameTime(int hour, float minute)
{
	m_have_time = true;
	m_hour = hour;
	m_minute = minute;
}

ZoneView::Vision ZoneView::CurrentVision() const
{
	return m_debug_draw.vision >= 0 ? static_cast<Vision>(m_debug_draw.vision) : OwnVision();
}

ZoneView::Vision ZoneView::OwnVision() const
{
	return m_appearance ? RaceVision(m_appearance->race) : Vision::None;
}

ZoneView::DayLight ZoneView::Lighting() const
{
	if (!m_have_time)
		return DayLight();
	DayLight light = TimeOfDayLight(m_hour, m_minute, m_info.time_type);

	// Infravision and ultravision (SetAmbientLight).
	ApplyVision(light, CurrentVision(), m_vision_exposure, m_infravision_glow, m_info.short_name == "vexthal");

	// A thunder clap's frame: the ambient light at 0.99, infravision off.
	if (WeatherShown() && m_weather.flash) {
		light.ambient = {0.99f, 0.99f, 0.99f};
		light.special = {};
		light.glow = {};
	}
	return light;
}

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
	// The zone's own lighting only; the time of day's ambient light is added as it draws.
	render::ZoneLighting lighting;
	lighting.ambient = 0;
	lighting.point_lights = false; // already baked into the vertex colors
	float fog_start, fog_end;
	ZoneFog(fog_start, fog_end); // CDisplay::SetFog: no sky when the fog ends within 101
	if (!m_zone.Load(m_eq_dir, m_info.short_name, error, lighting))
		m_error = error;
	else if (!m_models.Open(m_eq_dir, error, true, m_velious_textures, m_luclin))
		m_error = error;
	else if (m_info.sky != 0 && fog_end >= 101 && !m_sky.Load(m_eq_dir, m_info.sky, error))
		fprintf(stderr, "[zone view] %s\n", error.c_str()); // the zone still draws, over the fog color
	if (m_error.empty())
		m_models.AddZone(m_eq_dir, m_info.short_name);
	std::string effects_error;
	if (m_error.empty() && !m_spell_effects.Load(m_eq_dir, effects_error))
		fprintf(stderr, "[zone view] spell effects: %s\n", effects_error.c_str());
	m_spell_effects.SetFloor([this](const render::Vec3 &at) {
		float floor_z;
		return m_zone.FloorBelow(at + render::Vec3{0, 0, 1}, 1000, floor_z) ? floor_z : at.z;
	});
	m_spell_effects.SetSound(
		[this](const SpellEffects::Request &request, uint16_t on, int slot) { PlaySpellSound(request, on, slot); });
	m_spell_effects.SetMissiles([this](uint32_t id, render::Transform &placement) {
		const Missiles::Missile *missile = m_missiles.Find(id);
		if (!missile || !missile->made)
			return false;
		placement = MissilePlacement(*missile, m_missiles_ms);
		return true;
	});
	m_missiles.SetEffects([this](const Missiles::Missile &missile, int stage, const render::Vec3 *point) {
		const SpellInfo *spell = m_missiles.Spells() ? m_missiles.Spells()->Find(missile.spell) : nullptr;
		if (!spell)
			return;
		SpellEffects::Request request;
		request.stage = stage;
		request.effect = spell->new_effect;
		request.old_effect = spell->effect;
		request.spell = missile.spell;
		request.caster = missile.from.source;
		request.duration_ms = spell->ae_duration;
		if (point) {
			request.at_point = true;
			request.point = *point;
		}
		else
			request.missile = missile.id;
		StartSpellEffect(request);
	});
	// The weather's sprites, I_RAINRINGSPRITE and I_SNOWFLAKESPRITE (user method 0x17).
	if (m_equipment.Open(m_eq_dir + "/gequip.s3d", true)) {
		m_effect_textures.AddArchive(&m_equipment);
		m_rain_ring = m_effect_textures.Get("rainring.bmp", true);
		m_snowflake = m_effect_textures.Get("snowflake.bmp", true);
	}
	m_loaded = true;
}

void ZoneView::SetPlayer(const render::CharacterAppearance &appearance, float walk_speed, float run_speed)
{
	m_appearance = appearance;
	m_walk_speed = walk_speed;
	m_run_speed = run_speed;
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

void ZoneView::Turn(float delta)
{
	if (!m_placed)
		return;
	m_movement.Turn(delta);
	m_heading = m_movement.Heading();
}

void ZoneView::FaceCamera()
{
	Turn(m_orbit);
	m_orbit = 0;
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
		FaceCamera(); // first person looks where the character faces
		return;
	}
	m_distance = std::min(distance, MaxDistance);
}

void ZoneView::ZoomBy(float delta)
{
	if (m_distance <= 0 || m_view != CameraView::Chase || delta == 0)
		return;
	float distance = m_distance + delta;
	if (delta < 0)
		distance = std::max(distance, CenterHeight(m_my_id));
	else
		distance = std::min(distance, 400.0f);
	m_distance = std::clamp(distance, 2.0f, 200.0f);
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
	float height = 6; // the size, which the model is drawn 1.25 x as tall as
	if (m_appearance)
		height = m_appearance->height > 0 ? m_appearance->height : render::RaceHeight(m_appearance->race);
	m_height = height;
	m_feet = {m_server_position.y, m_server_position.x, m_server_position.z - height / 5 * 3.125f}; // WLD x/y are the server's y/x
	float floor_z;
	if (m_zone.FloorBelow({m_feet.x, m_feet.y, m_feet.z + 2}, 10, floor_z))
		m_feet.z = floor_z;
	if (m_distance <= 0)
		m_distance = height * 2 + 6;
	m_movement.SetRace(m_appearance ? m_appearance->race : 1);
	m_movement.Place(m_feet, m_heading, height, m_walk_speed, m_run_speed, m_info.gravity);
	m_movement.SetEyeHeight(m_model ? (m_model->HeadZ() - m_model->MinZ()) * 1.05f : height * 0.9f);
	m_placed = true;
	if (m_horse)
		ConfigureHorse(); // put somewhere else on it
}

void ZoneView::ConfigureHorse()
{
	auto it = m_actors.find(m_horse);
	float walk = it != m_actors.end() ? it->second.walk_speed : 0, run = it != m_actors.end() ? it->second.run_speed : 0;
	m_movement.SetRace(216);
	m_movement.Place(m_feet, m_heading, m_height, walk, run, m_info.gravity);
	m_movement.SetEyeHeight(m_model ? (m_model->HeadZ() - m_model->MinZ()) * 1.05f : m_height * 0.9f);
	m_movement.SetMounted(true);
}

void ZoneView::UpdateRiding()
{
	auto it = m_mounts.find(m_my_id);
	uint16_t horse = it != m_mounts.end() && m_actors.count(it->second) ? it->second : 0;
	if (horse == m_horse)
		return;
	if (m_horse)
		StopRiding();
	if (horse)
		StartRiding(horse);
}

void ZoneView::StartRiding(uint16_t horse)
{
	// MountEQPlayer: pinstCharSpawn is the horse, standing where it is, and our speeds are 0.
	SpawnView &view = m_actors[horse];
	m_horse = horse;
	m_feet = view.Feet();
	m_heading = view.actor.Heading();
	ConfigureHorse();
	m_pose.clear();
	m_standing_up = false;
	m_horse_head = m_movement.HeadEnvironment();
	m_horse_head_changed = false;
	DriveHorse(m_last_move_ms);
}

void ZoneView::StopRiding()
{
	// EQPlayer::Dismount: the horse stops, pinstCharSpawn is us again where the saddle was, and
	// SetHeights; we come down onto the floor under it.
	auto it = m_actors.find(m_horse);
	if (it != m_actors.end())
		it->second.actor.Drive(it->second.Feet(), it->second.actor.Heading(), 0, 0, {}, true,
			m_movement.BodyEnvironment(), m_movement.FeetEnvironment());
	render::Vec3 feet = MyFeet();
	m_horse = 0;
	m_rider_attached = false;
	float floor_z;
	if (m_zone.FloorBelow({feet.x, feet.y, feet.z + 2}, 30, floor_z))
		feet.z = floor_z;
	m_feet = feet;
	m_movement.SetMounted(false);
	m_movement.SetRace(m_appearance ? m_appearance->race : 1);
	m_movement.Place(m_feet, m_heading, m_height, m_walk_speed, m_run_speed, m_info.gravity);
	m_movement.SetEyeHeight(m_model ? (m_model->HeadZ() - m_model->MinZ()) * 1.05f : m_height * 0.9f);
	m_server_position = {m_feet.y, m_feet.x, m_feet.z + m_height / 5 * 3.125f};
}

void ZoneView::DriveHorse(uint64_t now_ms)
{
	auto it = m_actors.find(m_horse);
	if (it == m_actors.end())
		return;
	SpawnView &view = it->second;
	float ms = view.last_ms && now_ms > view.last_ms ? static_cast<float>(now_ms - view.last_ms) : 0;
	view.last_ms = now_ms;
	// As tall as we are (ChangeHeight).
	view.size = m_height;
	view.center_height = m_height / 5 * 3.125f;
	view.actor.Drive(m_feet, m_heading, m_movement.Speed(), m_movement.HeadingSpeed(), m_movement.Velocity(),
		m_movement.OnGround(), m_movement.BodyEnvironment(), m_movement.FeetEnvironment());
	ZoneSpawn spawn = m_horse_spawn;
	spawn.size = m_height;
	ResolveModel(view, spawn, true);
	if (view.model)
		view.actor.Animate(spawn, *view.model, ms, now_ms);
	render::Transform dag;
	m_rider_attached = RiderDag(view, dag);
	if (m_rider_attached)
		m_rider_placement = {dag.rotation, dag.translation, 1};
}

render::Transform ZoneView::MyPlacement() const
{
	if (m_rider_attached)
		return m_rider_placement;
	render::Transform placement;
	placement.rotation = render::AxisAngle({0, 0, 1}, render::HeadingToRadians(m_heading));
	placement.translation = m_feet + render::Vec3{0, 0, m_model ? m_model->OriginOverFeet() : 0};
	return placement;
}

render::Vec3 ZoneView::MyFeet() const
{
	if (!m_rider_attached)
		return m_feet;
	return m_rider_placement.translation - render::Vec3{0, 0, m_model ? m_model->OriginOverFeet() : 0};
}

bool ZoneView::RiderDag(const SpawnView &horse, render::Transform &out) const
{
	if (!horse.model)
		return false;
	int bone = horse.model->FindBone(horse.model->Code() + "RIDER_DAG");
	if (bone < 0)
		return false;
	render::Transform placement;
	placement.rotation = render::AxisAngle({0, 0, 1}, render::HeadingToRadians(horse.actor.Heading()));
	placement.translation = horse.Feet() + render::Vec3{0, 0, horse.model->OriginOverFeet()};
	std::vector<render::Transform> placed;
	horse.model->PlaceBones(placement, horse.actor.AnimationTime(*horse.model),
		render::AnimationCode(horse.actor.Animation()), horse.actor.AnimationLoops(), {bone}, placed);
	if (placed.empty())
		return false;
	out = placed[0];
	return true;
}

int ZoneView::RidingAnimation(float speed, float run)
{
	return run <= speed ? 78 : 74; // L10, P09
}

bool ZoneView::Move(const MoveControls &controls, uint64_t now_ms, bool looking)
{
	if (!m_loaded || !m_error.empty())
		return false;
	if (m_loader.joinable())
		m_loader.join();
	PlacePlayer();
	// EQSwitch_UpdateDoorStates: the doors move first, so a lift carries us.
	m_doors.Update(m_zone, now_ms);
	DoorSounds();
	float ms = m_last_move_ms ? static_cast<float>(now_ms - m_last_move_ms) : 0;
	m_last_move_ms = now_ms;
	// A jump pressed on a frame that's too short to move in waits for the next one.
	m_jump_pressed = m_jump_pressed || controls.jump;
	if (!m_placed || ms <= 0)
		return false;

	UpdateRiding();
	// We swim where we look: the view's pitch from where each camera starts out.
	MoveControls swim = controls;
	swim.jump = std::exchange(m_jump_pressed, false);
	swim.pitch = (m_pitch - (m_view == CameraView::Chase ? -12.0f : 0.0f)) * 512 / 360;
	bool jumped = m_movement.Step(swim, ms, m_zone);
	m_feet = m_movement.Feet();
	m_heading = m_movement.Heading();
	// Riding, the horse's x and y and its floor plus our ModelHeightOffset (DoMainLoop's update for a
	// pinstCharSpawn with a Rider): the same, the horse being as tall as we are.
	m_server_position = {m_feet.y, m_feet.x, m_feet.z + m_height / 5 * 3.125f};
	if (m_horse) {
		DriveHorse(now_ms);
		if (m_movement.HeadEnvironment() != m_horse_head) {
			m_horse_head = m_movement.HeadEnvironment();
			m_horse_head_changed = true;
		}
	}
	// The chase camera drifts back behind us as we move, unless the mouse is holding it.
	if (m_movement.Speed() != 0 && !looking && m_view == CameraView::Chase) {
		m_orbit = std::fmod(m_orbit, 512.0f);
		if (m_orbit > 256)
			m_orbit -= 512;
		if (m_orbit < -256)
			m_orbit += 512;
		m_orbit *= std::pow(0.9f, ms * 0.02f);
	}
	ChooseAnimation(ms);
	// CheckForUnderFloor: IHaveFallen at 2.5 down or faster, sound or not.
	bool wet = false;
	float landing = m_movement.TakeLanding(wet);
	int fall_damage = -1;
	if (landing <= -2.5f && m_fall_damage)
		fall_damage = m_fall_damage(std::fabs(landing + 2.5f));
	if (m_sound) {
		// Hear from where we are now (the front end sets it again for the frame).
		Hearing hearing = SoundListener(false);
		m_sound->SetListener({hearing.position.x, hearing.position.y, hearing.position.z},
			{hearing.forward.x, hearing.forward.y, hearing.forward.z});
		ActorSound::Actor me = MyActor();
		if (landing < 0)
			m_my_sound.Landed(*m_sound, me, landing, wet, m_safe_fall, fall_damage);
		render::Environment feet = m_movement.FeetEnvironment(), head = m_movement.HeadEnvironment();
		m_my_sound.FeetChanged(*m_sound, me, m_sound_feet, feet, m_movement.Velocity().z);
		ActorSound::HeadChanged(*m_sound, m_sound_head, head);
		m_sound_feet = feet;
		m_sound_head = head;
		m_my_sound.Animation(*m_sound, me, SoundAnimation(jumped), now_ms);
	}
	return jumped;
}

void ZoneView::DoorSounds()
{
	if (!m_sound)
		return;
	for (uint8_t type : m_doors.TakeSpawnedTypes())
		m_sound->LoadDoorSounds(type);
	for (const auto &sound : m_doors.TakeSounds())
		m_sound->PlaySoundAt(sound.slot, {sound.position.x, sound.position.y, sound.position.z});
}

ActorSound::Actor ZoneView::MyActor() const
{
	ActorSound::Actor me;
	me.ours = true;
	me.type = 0;
	if (m_appearance) {
		me.race = m_appearance->race;
		me.gender = m_appearance->gender;
		me.texture = m_appearance->texture;
		me.primary = static_cast<int>(m_appearance->equip[7]);
		me.secondary = static_cast<int>(m_appearance->equip[8]);
	}
	me.height = m_height;
	me.center = m_feet + render::Vec3{0, 0, m_height / 5 * 3.125f};
	return me;
}

int ZoneView::SoundAnimation(bool jumped) const
{
	// ProcessAnimation's pick, which unlike our track goes back to walking or running once a jump's
	// rise slows. The jump itself is the frame it starts.
	if (m_horse) // the horse's steps are its own
		return RidingAnimation(m_movement.Speed(), m_movement.RunSpeed());
	const render::Vec3 &velocity = m_movement.Velocity();
	bool moving = std::fabs(velocity.x) >= 0.001f || std::fabs(velocity.y) >= 0.001f;
	if (jumped)
		return moving ? render::Anim::RunJump : render::Anim::StandJump;
	if (m_movement.Swimming())
		return moving ? render::Anim::Swim : render::Anim::TreadWater;
	if (m_movement.Crouching())
		return render::Anim::Crouch;
	bool air = !m_movement.OnGround();
	if (moving) {
		if (air && velocity.z < -3.2f)
			return render::Anim::Fall;
		return m_movement.Speed() > m_walk_speed ? render::Anim::Run : render::Anim::Walk;
	}
	if (air && velocity.z < -2.25f)
		return render::Anim::Fall;
	return render::Anim::Idle;
}

void ZoneView::SpawnSounds(SpawnView &view, const ZoneSpawn &spawn, uint16_t id, uint64_t now_ms)
{
	ActorSound::Actor actor;
	actor.key = id;
	actor.type = spawn.npc;
	actor.race = spawn.race;
	actor.gender = spawn.gender;
	actor.texture = spawn.body_texture;
	actor.height = view.size;
	actor.center = view.Feet() + render::Vec3{0, 0, view.center_height};
	actor.primary = spawn.equipment[7];
	actor.secondary = spawn.equipment[8];
	actor.stance = spawn.stance;
	actor.hp = spawn.hp;
	if (view.model) // animated this frame
		for (int played : view.actor.Played())
			view.sound.Animation(*m_sound, actor, played, now_ms);
	bool wet;
	float landing = view.actor.TakeLanding(wet);
	if (landing < 0)
		view.sound.Landed(*m_sound, actor, landing, wet);
	render::Environment feet = view.actor.FeetEnvironment();
	view.sound.FeetChanged(*m_sound, actor, view.sound_feet, feet, view.actor.Velocity().z);
	view.sound_feet = feet;
	view.sound.Follow(*m_sound, actor);
}

void ZoneView::Push(float force, float heading, float pitch)
{
	// MovementSpeedY and X by the heading's cosine and sine, Z by the pitch's sine; the server's x
	// and y are our y and x.
	float a = render::HeadingToRadians(heading), p = render::HeadingToRadians(pitch);
	m_movement.Push({std::cos(a) * force, std::sin(a) * force, std::sin(p) * force});
}

void ZoneView::SetStance(uint8_t stance)
{
	std::string pose = stance == 110 ? "P02" : stance == 105 ? "P05" : stance == 115 ? "D05" : "";
	if (pose == m_pose)
		return;
	std::string was = m_pose;
	m_pose = pose;
	m_standing_up = false;
	if (pose.empty() && !was.empty()) {
		// Up: kneeling's backwards, sitting's or lying's by sitting's backwards (a model without it is
		// up at once).
		m_stand_animation = was == "P05" ? "P05" : "P02";
		m_standing_up = m_model && m_model->HasAnimation(m_stand_animation);
	}
	else if (!pose.empty()) {
		m_animation.clear(); // going down starts the pose over
		m_action.clear();
	}
}

bool ZoneView::FishingHook(float distance, render::Vec3 &surface) const
{
	if (!m_placed)
		return false;
	// Ahead along our heading (WLD x is the server's y), the floor below, 3 up.
	float a = render::HeadingToRadians(m_heading);
	render::Vec3 at = {m_server_position.y + std::cos(a) * distance, m_server_position.x + std::sin(a) * distance,
		m_server_position.z};
	float floor = 0;
	if (!m_zone.FloorBelow(at, 1000.0f, floor))
		return false;
	at.z = floor + 3.0f;
	render::Environment env = m_zone.Regions().EnvironmentAt(at);
	if (!render::Swimming(env) || env == render::Environment::Lava)
		return false;
	// The water's top: up from there while it's still water.
	render::Vec3 top = at;
	for (int i = 0; i < 400 && render::Swimming(m_zone.Regions().EnvironmentAt(top)); ++i)
		top.z += 0.5f;
	if (std::fabs(m_server_position.z - top.z) > 30.0f)
		return false;
	surface = top;
	return true;
}

void ZoneView::PlaySoundAt(int slot, const render::Vec3 &position)
{
	if (m_sound)
		m_sound->PlaySoundAt(slot, {position.x, position.y, position.z});
}

void ZoneView::PlayAnimation(int id, float speed)
{
	if (!m_model || !m_appearance)
		return;
	std::string code = render::AnimationCode(id);
	if (!m_model->HasAnimation(code))
		code = render::AnimationCode(render::FallbackAnimation(id, m_appearance->race));
	if (code.empty() || !m_model->HasAnimation(code))
		return;
	m_action = code;
	m_action_rate = speed > 0 ? speed : 1;
	m_animation = code;
	m_animation_ms = 0;
	// CDisplay_PlayAnimationSound, as PlayAnimationTrack2 starts it (a swing's whoosh, a kick).
	if (m_sound)
		m_my_sound.Animation(*m_sound, MyActor(), id, m_last_move_ms);
}

void ZoneView::ChooseAnimation(float ms)
{
	if ((m_dead || m_pose == "D05") && m_model && m_model->HasAnimation("D05")) {
		// Dead: the fall, held on its last frame.
		m_action.clear();
		if (m_animation != "D05") {
			m_animation = "D05";
			m_animation_ms = 0;
		}
		m_animation_reversed = false;
		m_animation_rate = 1;
		m_animation_ms = std::min(m_animation_ms + ms, static_cast<double>(m_model->AnimationMs("D05")));
		m_eye_drop = 0;
		return;
	}
	if (!m_action.empty()) {
		bool cut = !m_movement.OnGround() || m_movement.Swimming() || m_movement.Crouching() || !m_pose.empty() || m_standing_up;
		if (!cut && m_model && m_animation == m_action && m_animation_ms + ms * m_action_rate < m_model->AnimationMs(m_action)) {
			m_animation_reversed = false;
			m_animation_rate = m_action_rate;
			m_animation_ms += ms * m_action_rate;
			m_eye_drop = 0;
			return;
		}
		m_action.clear(); // over, or cut short: the usual pick from here
	}

	// What the character is doing picks the animation, as EQPlayer's animation tracks do:
	// L01 walk (backwards for back), L02 run, L03/L04 jump running/standing, L05 falling, L08
	// crouching (a standing jump winding up), P01 idle.
	std::string animation;
	bool reversed = false;
	float rate = 1;
	float speed = m_movement.Speed();
	const render::Vec3 &velocity = m_movement.Velocity();
	if (!m_movement.OnGround()) {
		// ProcessAnimation only falls once we're dropping fast: below -3.2 a tick while moving,
		// -2.25 standing still, more than a jump ever gets to. Until then a jump plays on, and
		// stepping off a ledge keeps walking or running.
		bool moving = std::fabs(velocity.x) >= 0.001f || std::fabs(velocity.y) >= 0.001f;
		if (velocity.z < (moving ? -3.2f : -2.25f))
			animation = "L05";
		else if (m_animation == "L03" || m_animation == "L04")
			animation = m_animation;
		else if (velocity.z > 0)
			animation = moving ? "L03" : "L04";
	}
	else if (m_movement.Crouching())
		animation = "L08";
	// ProcessAnimation in water: P06 swimming while we move (faster with the speed), L09 treading water.
	float across = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
	if (m_movement.Swimming()) {
		bool moving = std::fabs(velocity.x) >= 0.001f || std::fabs(velocity.y) >= 0.001f;
		animation = moving ? "P06" : "L09";
		rate = moving ? across * 0.5f + 1 : 1;
	}
	// Sitting: getting down, held; standing up: the same backwards, played out even as we move off
	// (a jump, a fall or the water cut it short).
	if (animation.empty() && !m_pose.empty() && m_model && m_model->HasAnimation(m_pose))
		animation = m_pose;
	else if (animation.empty() && m_standing_up) {
		animation = m_stand_animation;
		reversed = true;
	}
	else
		m_standing_up = false;
	if (m_horse) {
		// On the horse: PlayAnimationTrack2's riding animation for anything standing or moving.
		animation = render::AnimationCode(RidingAnimation(speed, m_movement.RunSpeed()));
		if (m_model && !m_model->HasAnimation(animation))
			animation = m_model->HasAnimation("P09") ? "P09" : "P01";
		m_standing_up = false;
	}
	if (animation.empty()) {
		if (speed > 0)
			animation = speed >= m_movement.RunSpeed() ? "L02" : "L01";
		else if (speed < 0) {
			animation = "L01";
			reversed = true;
		}
		else if (m_movement.Strafe() != 0)
			animation = "L01"; // ProcessAnimation walks while we're moving but not running
		else
			animation = "P01";
	}
	if ((animation == "L01" || animation == "L02") && !m_horse)
		rate = MoveAnimationRate(across);
	if (m_model && !m_model->HasAnimation(animation))
		animation = animation == "L01" || animation == "L02" ? animation : "P01";
	if (animation != m_animation || (animation == "P02" && reversed != m_animation_reversed)) {
		m_animation = animation;
		m_animation_ms = 0;
	}
	m_animation_reversed = reversed;
	m_animation_rate = rate;
	m_animation_ms += ms * rate;
	// Jumps, falls, the crouch and sitting down hold their last frame instead of looping.
	if (m_model && (animation == "L03" || animation == "L04" || animation == "L05" || animation == "L08")) {
		double length = m_model->AnimationMs(animation);
		if (length > 0)
			m_animation_ms = std::min(m_animation_ms, length);
	}
	m_eye_drop = 0;
	if (m_model && (animation == "P02" || animation == "P05")) {
		double length = m_model->AnimationMs(animation);
		if (length > 0) {
			if (reversed && m_animation_ms >= length - 1)
				m_standing_up = false; // up: P01 from the next frame
			m_animation_ms = std::min(m_animation_ms, length - 1);
			// The time Render draws it at (backwards for getting up), and our head there.
			uint64_t time = static_cast<uint64_t>(m_animation_ms);
			if (reversed)
				time = static_cast<uint64_t>(length) - 1 - time;
			m_eye_drop = std::max(m_model->HeadZ() - m_model->HeadZAt(time, animation, false), 0.0f);
		}
	}
}

ZoneView::DebugStats ZoneView::Stats() const
{
	DebugStats stats;
	stats.triangles = m_triangles_drawn;
	stats.renderer = m_renderer->Name();
	stats.feet_environment = m_movement.FeetEnvironment();
	stats.body_environment = m_movement.BodyEnvironment();
	stats.head_environment = m_movement.HeadEnvironment();
	for (const std::string &type : m_zone.Regions().Types(m_zone.Regions().RegionAt(m_feet + render::Vec3{0, 0, m_height * 0.625f})))
		stats.region_types += (stats.region_types.empty() ? "" : ", ") + type;
	stats.meshes_in_view = m_zone.MeshesInView();
	stats.meshes_hidden = m_zone.MeshesHidden();
	stats.spawns = m_actors.size();
	for (const auto &[id, actor] : m_actors) {
		stats.spawns_built += actor.model ? 1 : 0;
		stats.spawns_drawn += actor.drawn ? 1 : 0;
	}
	stats.models_cached = m_model_cache.size();
	stats.heading = m_heading;
	stats.velocity = m_movement.Velocity();
	stats.on_ground = m_movement.OnGround();
	stats.animation = m_animation;
	stats.animation_rate = m_animation_rate;
	stats.view = m_view;
	stats.camera_distance = m_distance;
	stats.camera_pitch = m_pitch;
	stats.feet_z = m_feet.z;
	if (m_placed)
		stats.have_floor = m_zone.FloorBelow(m_feet + render::Vec3{0, 0, 2}, 1000, stats.floor_z);
	stats.zone_triangles = m_zone.TriangleCount();
	stats.zone_objects = m_zone.ObjectCount();
	stats.light = m_daylight;
	return stats;
}

bool ZoneView::SpawnStats(uint16_t id, SpawnDebug &out) const
{
	auto it = m_actors.find(id);
	if (it == m_actors.end())
		return false;
	const SpawnView &view = it->second;
	out = SpawnDebug{};
	if (view.model)
		out.model = view.model->Code();
	out.animation = render::AnimationCode(view.actor.Animation());
	out.animation_rate = view.actor.AnimationRate();
	out.drawn = view.drawn;
	out.camera_distance = view.distance;
	out.feet = view.Feet();
	out.body_environment = view.actor.BodyEnvironment();
	return true;
}

void ZoneView::AddGroundItem(uint32_t drop_id, const std::string &model, float x, float y, float z, float heading)
{
	GroundItemView &view = m_ground_items[drop_id];
	view = GroundItemView{};
	view.model_name = model;
	view.at = {y, x, z}; // WLD x/y are the server's y/x
	view.heading = heading;
	// CDisplay::CreateActor's sizes for the tradeskill containers' models: smaller in the small
	// races' cities (Rivervale, Ak'Anon, Kaladim), else larger; and the bulletin boards.
	auto is = [&](const std::string &prefix) {
		return model.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), model.begin(),
			[](char a, char b) { return std::toupper(static_cast<unsigned char>(a)) == std::toupper(static_cast<unsigned char>(b)); });
	};
	const std::string &zone = m_info.short_name;
	if (zone == "kaladima" || zone == "kaladimb" || zone == "rivervale" || zone == "akanon") {
		if (is("IT70") || is("IT74"))
			view.scale = 0.8f;
	} else if (is("IT66") || is("IT69") || is("IT73")) {
		view.scale = 1.5f;
	} else if (is("IT74")) {
		view.scale = 1.3f;
	}
	if (is("BBBOARD"))
		view.scale = 1.5f;
}

void ZoneView::RemoveGroundItem(uint32_t drop_id)
{
	m_ground_items.erase(drop_id);
}

size_t ZoneView::GroundItemsShown() const
{
	return static_cast<size_t>(std::count_if(m_ground_items.begin(), m_ground_items.end(),
		[](const auto &item) { return item.second.model != nullptr; }));
}

void ZoneView::DrawGroundItems(uint64_t now_ms)
{
	if (!m_loaded || !m_error.empty())
		return;
	render::Vec3 camera = m_renderer->CurrentCamera().position;
	for (auto &[id, view] : m_ground_items) {
		if (!view.placed) {
			// The 0x402c case of HandleWorldMessage: CreateActor at the floor below it + 0.5. A
			// model that isn't there is IT63's (a bag), as CreateActor's fallback for these.
			view.placed = true;
			std::string error;
			view.model = m_models.BuildItem(view.model_name, error);
			if (!view.model)
				view.model = m_models.BuildItem("IT63", error);
			float floor_z;
			view.feet = view.at;
			if (m_zone.FloorBelow(view.at + render::Vec3{0, 0, 1}, 1000, floor_z))
				view.feet.z = floor_z + 0.5f;
		}
		if (!view.model)
			continue;
		view.distance = render::Length(view.feet - camera);
		if (view.distance > FarPlane())
			continue;
		render::Transform placement = GroundItemPlacement(view);
		view.model->Draw(*m_renderer, placement, ModelLightingAt(view.feet), 0);
		view.particles.Draw(*m_renderer, *view.model, placement, 0, "P01", true, now_ms, m_daylight.ambient);
	}
}

void ZoneView::LaunchMissile(const ZoneMissile &missile)
{
	if (m_loaded && m_error.empty())
		m_missiles.Launch(missile);
}

render::CharacterModel *ZoneView::MissileModel(const std::string &name)
{
	// CreateActor's "<name>_ACTORDEF": an item's model, as on the ground.
	auto it = m_missile_models.find(name);
	if (it == m_missile_models.end()) {
		std::string error;
		it = m_missile_models.emplace(name, m_models.BuildItem(name, error)).first;
	}
	return it->second.get();
}

void ZoneView::UpdateMissiles(uint64_t now_ms)
{
	m_missiles_ms = now_ms;
	if (m_missiles.All().empty())
		return;
	Missiles::World world;
	float my_center = m_height / 5 * 3.125f;
	world.find = [&](uint16_t id, Missiles::Spawn &spawn) {
		if (id == 0)
			return false;
		if (id == m_my_id) {
			if (!m_placed)
				return false;
			spawn.at = m_feet + render::Vec3{0, 0, my_center};
			spawn.height_offset = my_center;
			return true;
		}
		auto it = m_actors.find(id);
		if (it == m_actors.end())
			return false;
		spawn.at = it->second.Feet() + render::Vec3{0, 0, it->second.center_height};
		spawn.height_offset = it->second.center_height;
		spawn.dead = it->second.dead;
		return true;
	};
	world.spawn_hit = [&](const render::Vec3 &from, const render::Vec3 &to, float radius, uint16_t ignore) -> uint16_t {
		// The actors' collision: each spawn a column from its feet up twice its center, half its
		// center across.
		auto hits = [&](const render::Vec3 &feet, float center) {
			render::Vec3 d = to - from;
			float flat = d.x * d.x + d.y * d.y;
			float t = flat > 0 ? std::clamp(((feet.x - from.x) * d.x + (feet.y - from.y) * d.y) / flat, 0.0f, 1.0f) : 0;
			render::Vec3 p = from + d * t;
			float reach = center * 0.5f + radius;
			return std::hypot(p.x - feet.x, p.y - feet.y) <= reach && p.z >= feet.z - radius && p.z <= feet.z + center * 2 + radius;
		};
		if (m_my_id && m_my_id != ignore && m_placed && hits(m_feet, my_center))
			return m_my_id;
		for (const auto &[id, view] : m_actors)
			if (id != ignore && view.model && hits(view.Feet(), view.center_height))
				return id;
		return 0;
	};
	world.blocked = [&](const render::Vec3 &from, const render::Vec3 &to) {
		float fraction;
		return m_zone.SegmentHit(from, to, fraction, nullptr, true);
	};
	world.has_model = [&](const std::string &name) { return MissileModel(name) != nullptr; };
	m_missiles.Update(now_ms, world);
}

render::Transform ZoneView::MissilePlacement(const Missiles::Missile &missile, uint64_t now_ms) const
{
	// t3dSetActorLocation: its heading, then its tilt (the actor's pitch, about y the other way).
	render::Transform placement;
	placement.rotation = render::AxisAngle({0, 0, 1}, render::HeadingToRadians(missile.heading)) *
	                     render::AxisAngle({0, 1, 0}, -render::HeadingToRadians(missile.pitch));
	placement.translation = m_missiles.DrawnAt(missile, now_ms);
	return placement;
}

void ZoneView::DrawMissiles(uint64_t now_ms)
{
	render::Vec3 camera = m_renderer->CurrentCamera().position;
	for (const Missiles::Missile &missile : m_missiles.All()) {
		render::CharacterModel *model = missile.made ? MissileModel(missile.model) : nullptr;
		if (!model)
			continue;
		render::Transform placement = MissilePlacement(missile, now_ms);
		if (render::Length(placement.translation - camera) > FarPlane())
			continue;
		model->Draw(*m_renderer, placement, ModelLightingAt(placement.translation), 0);
	}
}

render::Transform ZoneView::GroundItemPlacement(const GroundItemView &view)
{
	render::Transform placement;
	placement.rotation = render::AxisAngle({0, 0, 1}, render::HeadingToRadians(view.heading));
	placement.translation = view.feet;
	placement.scale = view.scale;
	return placement;
}

ZoneView::Picked ZoneView::Pick(int x, int y, uint16_t me) const
{
	// s3dPickObjectOnScreen: the ray from the camera through the pixel, out to the far plane, and
	// the nearest model triangle it hits (collideRayWithEntity) of what was drawn.
	Picked picked;
	if (!m_loaded || !m_error.empty())
		return picked;
	render::Vec3 from = m_renderer->CurrentCamera().position;
	render::Vec3 direction = m_renderer->ViewRay(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
	float best = FarPlane();
	float distance;
	for (const auto &[id, view] : m_actors) {
		if (view.drawn && view.model &&
			view.model->RayHit(view.placement, view.time_ms, view.animation, view.loops, from, direction, best, distance)) {
			best = distance;
			picked = Picked{id, false, 0};
		}
	}
	if (me != 0 && m_model_shown) {
		render::Transform placement = MyPlacement();
		if (m_model->RayHit(placement, m_player_time_ms, m_animation, m_player_loops, from, direction, best, distance)) {
			best = distance;
			picked = Picked{me, false, 0};
		}
	}
	for (const auto &[id, view] : m_ground_items) {
		if (view.model && view.distance <= FarPlane() &&
			view.model->RayHit(GroundItemPlacement(view), 0, "P01", true, from, direction, best, distance)) {
			best = distance;
			picked = Picked{0, true, id};
		}
	}
	return picked;
}

bool ZoneView::PickGroundItem(int x, int y, uint16_t me, uint32_t &drop_id) const
{
	Picked picked = Pick(x, y, me);
	if (picked.ground)
		drop_id = picked.drop_id;
	return picked.ground;
}

float ZoneView::CenterHeight(uint16_t id) const
{
	if (id != 0 && id == m_my_id)
		return m_height / 5 * 3.125f;
	auto it = m_actors.find(id);
	return it != m_actors.end() ? it->second.center_height : 0;
}

float ZoneView::BoundingRadius(uint16_t id) const
{
	if (id != 0 && id == m_my_id)
		return m_model ? m_model->BoundingRadius() : -1;
	auto it = m_actors.find(id);
	return it != m_actors.end() && it->second.model ? it->second.model->BoundingRadius() : -1;
}

uint16_t ZoneView::PickSpawn(int x, int y, uint16_t me) const
{
	return Pick(x, y, me).spawn;
}

ZoneDoors::Use ZoneView::ClickDoor(int x, int y, int16_t disarm, uint8_t &id, int16_t &skill) const
{
	// CDisplay::GetClickedActor: what's under the cursor, unless the zone is in front of it.
	if (!m_loaded || !m_error.empty())
		return ZoneDoors::Use::Nothing;
	render::Vec3 from = m_renderer->CurrentCamera().position;
	render::Vec3 direction = m_renderer->ViewRay(static_cast<float>(x), static_cast<float>(y));
	float distance, fraction;
	int door = m_zone.DoorHit(from, direction, FarPlane(), distance);
	if (door < 0 || m_zone.SegmentHit(from, from + direction * distance, fraction, nullptr, false))
		return ZoneDoors::Use::Nothing;
	render::Vec3 center = m_feet + render::Vec3{0, 0, m_height / 5 * 3.125f};
	return m_doors.Click(door, center, m_height, disarm, id, skill);
}

bool ZoneView::CanSee(const render::Vec3 &from, const render::Vec3 &to) const
{
	float fraction;
	return !m_loaded || !m_error.empty() || !m_zone.SegmentHit(from, to, fraction, nullptr, false);
}

ZoneMovement ZoneView::Movement() const
{
	ZoneMovement m;
	m.x = m_server_position.x; // m_server_position is (x, y, z) in the server's terms
	m.y = m_server_position.y;
	m.z = m_server_position.z;
	m.heading = m_heading;
	m.speed = m_movement.Speed();
	m.heading_speed = m_movement.HeadingSpeed();
	m.dy = m_movement.Velocity().x; // WLD x is the server's y
	m.dx = m_movement.Velocity().y;
	m.dz = m_movement.Velocity().z;
	return m;
}

render::Camera ZoneView::MakeCamera(float &distance) const
{
	float height = m_model ? m_model->MaxZ() - m_model->MinZ() : 6;
	float eye = m_model ? (m_model->HeadZ() - m_model->MinZ()) * 1.05f : height * 0.9f;
	if (m_horse)
		eye *= 0.85f; // MountEQPlayer: CameraHeightOffset * 0.85
	render::Vec3 feet = MyFeet();
	float yaw = render::HeadingToRadians(m_heading + m_orbit);
	float pitch = m_pitch * render::Pi / 180;
	render::Vec3 look{std::cos(yaw) * std::cos(pitch), std::sin(yaw) * std::cos(pitch), std::sin(pitch)};

	render::Camera camera;
	camera.horizontal_fov = render::Pi / 2;
	camera.near_plane = 0.3f;
	// CDisplay::SetFog: the yon is the fog's end; everything past it is the sky or the fog color.
	camera.far_plane = FarPlane();
	camera.fog = !m_debug_draw.no_fog && !m_debug_draw.no_clip;
	FogRange(camera.fog_start, camera.fog_end);
	// The fog darkens with the light (at night it fades into the dark, not into daylight grey).
	DayLight light = Lighting();
	float level = std::clamp((light.ambient.x + light.ambient.y + light.ambient.z) / 3 / 0.9f, 0.12f, 1.0f);
	camera.fog_color = ScaleColor(m_info.fog_color, {level, level, level});
	if (WeatherShown() && m_weather.flash) {
		// SetAmbientLight's flash: no channel of the fog darker than our floor's exposure.
		uint32_t least = static_cast<uint32_t>(std::clamp(ExposureAt(m_feet + render::Vec3{0, 0, m_height / 5 * 3.125f}), 0.0f, 1.0f) * 255);
		uint32_t flashed = 0;
		for (int shift : {16, 8, 0})
			flashed |= std::max((camera.fog_color >> shift) & 0xff, least) << shift;
		camera.fog_color = flashed;
	}
	distance = 0;
	if (m_view == CameraView::FirstPerson) {
		camera.position = feet + render::Vec3{0, 0, eye - m_eye_drop * 1.05f};
		camera.forward = look;
	}
	else {
		// Behind the character and a little above, looking at their head. Like the real
		// client's camera, it comes in closer when a wall or the floor is in the way. The
		// test covers a box around the camera, not just its center, so a slope or wall it
		// backs into at a shallow angle stays clear of the near plane.
		render::Vec3 target = feet + render::Vec3{0, 0, eye};
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
	UpdateMissiles(now_ms);

	using Clock = std::chrono::steady_clock;
	auto since = [](Clock::time_point start) { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); };
	Clock::time_point start = Clock::now();
	float distance;
	render::Camera camera = MakeCamera(distance);
	m_renderer->Begin(canvas.Width(), canvas.Height(), camera);
	// SetAmbientLight follows our floor's exposure 0.01 a frame (here 0.6 a second), and turns
	// infravision's glow on when the sky's light falls under 0.53, off when it's over 0.7.
	float elapsed = m_vision_ms && now_ms > m_vision_ms ? (now_ms - m_vision_ms) / 1000.0f : 0;
	m_vision_ms = now_ms;
	float exposure = ExposureAt(m_feet + render::Vec3{0, 0, m_height / 5 * 3.125f});
	float step = std::min(0.6f * elapsed, std::fabs(exposure - m_vision_exposure));
	m_vision_exposure += exposure > m_vision_exposure ? step : -step;
	DayLight light = Lighting();
	bool glow = CurrentVision() == Vision::Infravision && (light.sky < 0.53f || (m_infravision_glow && light.sky <= 0.7f));
	if (glow != m_infravision_glow) {
		m_infravision_glow = glow;
		light = Lighting();
	}
	m_daylight = light;
	// The zone: its baked light, the ambient over its vertices' exposure, the sun, and the lights
	// spawns carry (the nearest few to the camera).
	PlaceCarriedLights(now_ms);
	std::vector<render::PointLight> scene_lights = m_carried_lights;
	std::sort(scene_lights.begin(), scene_lights.end(), [&](const render::PointLight &a, const render::PointLight &b) {
		return render::Length(a.position - camera.position) < render::Length(b.position - camera.position);
	});
	if (scene_lights.size() > render::Renderer::SceneLightLimit)
		scene_lights.resize(render::Renderer::SceneLightLimit);
	m_renderer->SetSceneLights(light.sun_direction, light.sun_color, scene_lights);
	m_renderer->SetVertexLight({1, 1, 1}, light.ambient, light.special);
	m_zone.SetOcclusionCulling(!m_debug_draw.no_culling);
	m_zone.Draw(*m_renderer, now_ms, scene_lights, true);
	m_renderer->SetVertexLight({1, 1, 1}, {0, 0, 0});
	m_times.zone = since(start);
	start = Clock::now();
	// Our own model isn't drawn in first person, or with the camera pushed into our head.
	m_model_shown = m_model && m_view == CameraView::Chase && distance >= 1.5f;
	if (m_model) {
		// The animation's own clock once we're moving (the preview tool never moves: real time).
		uint64_t time = m_last_move_ms ? static_cast<uint64_t>(m_animation_ms) : now_ms;
		if (m_animation_reversed) {
			uint32_t length = m_model->AnimationMs(m_animation);
			if (length > 0)
				time = length - 1 - time % length;
		}
		m_player_time_ms = time;
		// Jumps, falls and the crouch stop at the end.
		m_player_loops = !(m_animation == "L03" || m_animation == "L04" || m_animation == "L05" || m_animation == "L08" ||
		                   m_animation == "P02" || m_animation == "D05");
	}
	if (m_model_shown) {
		render::Transform placement = MyPlacement();
		uint64_t time = m_player_time_ms;
		bool held = !m_player_loops;
		m_model->Draw(*m_renderer, placement, ModelLightingAt(MyFeet() + render::Vec3{0, 0, m_height / 5 * 3.125f}), time,
			m_animation, !held);
		m_held.Draw(*m_renderer, *m_model, placement, time, m_animation, !held, now_ms, light.ambient);
	}
	m_camera_position = camera.position;
	DrawSpawns(now_ms);
	DrawGroundItems(now_ms);
	DrawMissiles(now_ms);
	DrawWeather(now_ms);
	m_spell_effects.Update(now_ms, camera.position,
		[this](uint16_t id, SpellEffects::Actor &actor) { return FindActor(id, actor); });
	m_spell_effects.Draw(*m_renderer);
	DrawNameSprites(camera);
	m_times.models = since(start);
	start = Clock::now();
	// The sky dims with the day period, toward a deep blue at night. RealRender_World draws none
	// while there's weather: the fog's color is all past it.
	bool sky = m_sky.Loaded() && m_sky_mode != 0 && !(WeatherShown() && m_weather.active);
	render::Vec3 sky_tint{light.sky + 0.03f * (1 - light.sky), light.sky + 0.04f * (1 - light.sky), light.sky + 0.10f * (1 - light.sky)};
	if (sky) {
		m_renderer->BeginBackground();
		m_renderer->SetVertexLight(sky_tint, {0, 0, 0});
		m_sky.Draw(*m_renderer, now_ms, m_sky_mode == 2);
		m_renderer->EndBackground();
	}
	m_triangles_drawn = m_renderer->TriangleCount();
	m_renderer->Finish(canvas.Row(0), canvas.Stride(), sky ? ScaleColor(m_sky.HorizonColor(), sky_tint) : camera.fog_color);
	DrawDebugOverlays(canvas);
	m_times.submit = since(start);
}

void ZoneView::FogRange(float &start, float &end) const
{
	if (WeatherShown() && m_weather.active) {
		start = m_weather.fog_start;
		end = m_weather.fog_end;
		return;
	}
	ZoneFog(start, end);
}

void ZoneView::ZoneFog(float &start, float &end) const
{
	start = m_info.fog_start;
	end = m_info.fog_end;
	const float ClipPlane = m_clip_plane;
	if (ClipPlane != 1) {
		// Below 1 the end comes in from the fog's toward the min clip (at 0.1); above, it goes out
		// toward the max clip (at 4).
		float depth = end - start;
		if (ClipPlane <= 1)
			end = m_info.min_clip + (ClipPlane - 0.1f) * (m_info.fog_end - m_info.min_clip);
		else
			end = m_info.fog_end + (ClipPlane - 1) * (m_info.max_clip - m_info.fog_end) / 3;
		start = std::max(end - depth, 0.0f);
	}
}

float ZoneView::FarPlane() const
{
	if (m_debug_draw.no_clip)
		return NoClipFarPlane;
	float fog_start, fog_end;
	FogRange(fog_start, fog_end);
	float far_plane = std::max(fog_end, 50.0f);
	return m_debug_draw.no_fog ? std::max(far_plane, m_info.max_clip) : far_plane;
}

namespace
{
	// do_rain's RAINS_CLOUD, RAINW_CLOUD and RAIN_CLOUD, and do_snow's SNOWS_CLOUD, SNOWW_CLOUD and
	// SNOW_CLOUD, at the options' density (render::PCloudDensity) as they're made.

	void MakeWeatherDefs(bool rain, const render::Texture *ring, const render::Texture *flake, render::ParticleCloudDef &ground,
		render::ParticleCloudDef &water, render::ParticleCloudDef &falling)
	{
		using Def = render::ParticleCloudDef;
		Def box;
		float reach = rain ? 225.0f : 300.0f;
		box.low = {-reach, -reach, 250};
		box.high = {reach, reach, 250};
		box.max_particles = 1000;
		box.speed = 100;
		// Where they hit the ground, a dot fading out: rain's for a fifth of a second, snow's half.
		ground = box;
		ground.kind = Def::Kind::Points;
		ground.flags = Def::Fade;
		ground.lifespan_ms = rain ? 200 : 500;
		// The water: rain's rings spread to 3 either side over half a second; snow's a dot.
		water = box;
		water.kind = rain ? Def::Kind::Rings : Def::Kind::Points;
		water.flags = Def::Fade;
		water.lifespan_ms = rain ? 500 : 200;
		water.size = 3;
		water.sprite = rain ? ring : nullptr;
		// Falling from 250 over us, over a box 450 (rain) or 600 (snow) across: rain a streak at 200
		// a second, one every 3 ms, for up to 2 seconds; snow a flake at 50 a second, wobbling, one
		// every 5 ms, for up to 15 seconds.
		falling = box;
		falling.kind = rain ? Def::Kind::Lines : Def::Kind::Sprites;
		falling.flags = Def::Collide | Def::FollowX | Def::FollowY | Def::FollowZ | (rain ? 0u : uint32_t{Def::Wobble});
		falling.max_particles = rain ? 1000 : 1500;
		falling.normal = {0, 0, rain ? -2.0f : -1.0f};
		falling.speed = rain ? 100.0f : 50.0f;
		falling.lifespan_ms = rain ? 2000 : 15000;
		falling.interval_ms = rain ? 3 : 5;
		falling.size = rain ? 1.0f : 0.25f;
		falling.sprite = rain ? nullptr : flake;
	}
}

void ZoneView::DrawWeather(uint64_t now_ms)
{
	// The clouds come with the rain or snow once it's in (t3dCreateParticleCloud) and are stopped
	// when it's gone (t3dStopPCloudAndAutoDestroy), their last particles still falling.
	if (!WeatherShown())
		m_weather_clouds.clear();
	Weather::Kind want = WeatherShown() ? m_weather.clouds : Weather::Kind::None;
	if (want != m_cloud_kind) {
		if (!m_weather_clouds.empty())
			m_weather_clouds.back().falling->Stop(now_ms);
		if (want != Weather::Kind::None) {
			render::ParticleCloudDef ground, water, falling;
			MakeWeatherDefs(want == Weather::Kind::Rain, m_rain_ring, m_snowflake, ground, water, falling);
			WeatherClouds clouds;
			float density = render::PCloudDensity();
			clouds.ground = std::make_unique<render::ParticleCloud>(ground, density, nullptr, nullptr);
			clouds.water = std::make_unique<render::ParticleCloud>(water, density, nullptr, nullptr);
			clouds.falling =
				std::make_unique<render::ParticleCloud>(falling, density, clouds.ground.get(), clouds.water.get());
			m_weather_clouds.push_back(std::move(clouds));
		}
		m_cloud_kind = want;
	}
	if (m_weather_clouds.empty())
		return;

	// s3dCollideRaySegmentWithWorld: the nearer of a solid surface and a water, lava or slime region.
	const render::ParticleCloud::Collide collide = [this](const render::Vec3 &from, const render::Vec3 &to, float &fraction,
		uint32_t &flags) {
		float solid = 1, region = 1;
		uint32_t found = 0;
		bool hit_solid = m_zone.SegmentHit(from, to, solid);
		bool hit_region = m_zone.Regions().SegmentEnters(from, to,
			render::ZoneRegions::WaterFlag | render::ZoneRegions::LavaFlag | render::ZoneRegions::SlimeFlag, region, found);
		if (hit_region && (!hit_solid || region <= solid)) {
			fraction = region;
			flags = found;
			return true;
		}
		fraction = solid;
		flags = 0;
		return hit_solid;
	};
	render::Vec3 eye = m_renderer->CurrentCamera().position;
	uint32_t rain_color = m_weather.blood ? 0xb40101 : 0x606060; // t3dSetRainColor
	for (size_t i = 0; i < m_weather_clouds.size();) {
		WeatherClouds &clouds = m_weather_clouds[i];
		clouds.falling->Update(now_ms, eye, collide);
		clouds.ground->Update(now_ms, eye, collide);
		clouds.water->Update(now_ms, eye, collide);
		if (clouds.falling->Done(now_ms)) { // destroyed: the others are stopped, to go once they're empty
			clouds.ground->Stop(now_ms);
			clouds.water->Stop(now_ms);
		}
		for (render::ParticleCloud *cloud : {clouds.falling.get(), clouds.ground.get(), clouds.water.get()})
			cloud->Draw(*m_renderer, now_ms, rain_color, m_daylight.ambient);
		if (clouds.falling->Done(now_ms) && clouds.ground->Done(now_ms) && clouds.water->Done(now_ms))
			m_weather_clouds.erase(m_weather_clouds.begin() + static_cast<long>(i));
		else
			++i;
	}
}

ZoneView::WeatherCounts ZoneView::WeatherParticles() const
{
	WeatherCounts counts;
	for (const WeatherClouds &clouds : m_weather_clouds) {
		counts.falling += clouds.falling->Count();
		counts.ground += clouds.ground->Count();
		counts.water += clouds.water->Count();
	}
	return counts;
}

void ZoneView::DrawDebugOverlays(Canvas &canvas) const
{
	uint32_t *pixels = canvas.Row(0);
	int stride = canvas.Stride(), width = canvas.Width(), height = canvas.Height();
	// Lines anywhere on it, straight into its pixels.
	if (m_debug_draw.wireframe || m_debug_draw.walls || m_debug_draw.floor)
		canvas.MarkDrawn({0, 0, width, height});
	auto line = [&](const render::Vec3 &a, const render::Vec3 &b, uint32_t color) {
		float ax, ay, bx, by;
		if (m_renderer->Project(a, ax, ay) && m_renderer->Project(b, bx, by))
			render::DrawLine(pixels, stride, width, height, ax, ay, bx, by, color);
	};
	if (m_debug_draw.wireframe) {
		// Everything drawn (after Finish, so name tags still hide behind walls), as outlines on black.
		canvas.Fill({0, 0, width, height}, 0x000000);
		m_renderer->DrawEdges(pixels, stride, 0x40a060);
	}
	if (m_debug_draw.walls && m_placed) {
		// The steep solid triangles within reach, and the sphere that collides with the world
		// (PlayerMovement::Sweep): we walk onto what's under its bottom.
		{
			render::Vec3 center = m_movement.SphereCenter();
			float radius = m_movement.Radius();
			render::Vec3 last_ring, last_upright;
			for (int i = 0; i <= 32; ++i) {
				float a = i * (2 * render::Pi / 32), c = std::cos(a) * radius, s = std::sin(a) * radius;
				render::Vec3 ring = center + render::Vec3{c, s, 0}, upright = center + render::Vec3{c, 0, s};
				if (i > 0) {
					line(last_ring, ring, 0x40c0ff);
					line(last_upright, upright, 0x40c0ff);
				}
				last_ring = ring;
				last_upright = upright;
			}
		}
		const float reach = 40;
		const auto &solid = m_zone.SolidTriangles();
		for (size_t i = 0; i + 2 < solid.size(); i += 3) {
			const render::Vec3 &a = solid[i], &b = solid[i + 1], &c = solid[i + 2];
			render::Vec3 center = (a + b + c) * (1.0f / 3);
			if (std::fabs(center.x - m_feet.x) > reach || std::fabs(center.y - m_feet.y) > reach ||
				std::fabs(center.z - m_feet.z) > reach)
				continue;
			render::Vec3 normal = render::Normalize(render::Cross(b - a, c - a));
			if (std::fabs(normal.z) >= 0.7f)
				continue;
			line(a, b, 0xff4040);
			line(b, c, 0xff4040);
			line(c, a, 0xff4040);
		}
	}
	if (m_debug_draw.floor && m_placed) {
		// A cross on the floor right under us, and a post from it up to our feet.
		float floor_z;
		if (m_zone.FloorBelow(m_feet + render::Vec3{0, 0, 2}, 1000, floor_z)) {
			render::Vec3 floor{m_feet.x, m_feet.y, floor_z + 0.05f};
			line(floor - render::Vec3{2, 0, 0}, floor + render::Vec3{2, 0, 0}, 0xffe040);
			line(floor - render::Vec3{0, 2, 0}, floor + render::Vec3{0, 2, 0}, 0xffe040);
			line(floor, m_feet, 0xffe040);
		}
	}
}

// ---------------------------------------------------------------- other spawns

std::string ZoneView::SpawnModelKey(const ZoneSpawn &spawn)
{
	char key[512];
	int length = snprintf(key, sizeof(key), "%u/%u/%u/%.2f/%u/%u/%u/%u.%u.%u.%u.%u.%u", spawn.race, spawn.gender, spawn.face,
		spawn.size, spawn.body_texture, spawn.helm, spawn.class_id, spawn.hair_style, spawn.hair_color, spawn.beard,
		spawn.beard_color, spawn.eye_color1, spawn.eye_color2);
	for (int k = 0; k < render::EquipSlots; ++k)
		length += snprintf(key + length, sizeof(key) - static_cast<size_t>(length), "/%u:%08x", spawn.equipment[k], spawn.colors[k]);
	return key;
}

std::shared_ptr<render::CharacterModel> ZoneView::SpawnModel(const ZoneSpawn &spawn, const std::string &key)
{
	auto cached = m_model_cache.find(key);
	if (cached != m_model_cache.end())
		return cached->second;
	render::CharacterAppearance appearance;
	appearance.race = spawn.race;
	appearance.gender = spawn.gender;
	appearance.face = spawn.face;
	appearance.height = spawn.size;
	appearance.texture = spawn.body_texture;
	appearance.helm = spawn.helm;
	appearance.class_id = spawn.class_id;
	appearance.hair_style = spawn.hair_style;
	appearance.hair_color = spawn.hair_color;
	appearance.beard = spawn.beard;
	appearance.beard_color = spawn.beard_color;
	appearance.eye_color1 = spawn.eye_color1;
	appearance.eye_color2 = spawn.eye_color2;
	for (int k = 0; k < render::EquipSlots; ++k) {
		appearance.equip[k] = spawn.equipment[k];
		appearance.tint[k] = spawn.colors[k];
	}
	std::string error;
	std::shared_ptr<render::CharacterModel> model = m_models.Build(appearance, error);
	if (!model && m_missing_models.insert(render::ActorTag(spawn.race, spawn.gender)).second)
		fprintf(stderr, "[zone view] %s (race %u)\n", error.c_str(), spawn.race);
	m_model_cache[key] = model;
	return model;
}

void ZoneView::StartSpellEffect(const SpellEffects::Request &request)
{
	if (!m_loaded || !m_error.empty())
		return;
	m_spell_effects.SetNewEffects(m_debug_draw.new_spell_effects);
	m_spell_effects.Start(request, [this](uint16_t id, SpellEffects::Actor &actor) { return FindActor(id, actor); });
}

bool ZoneView::PlayActorSound(uint16_t on, int slot)
{
	// EQPlayer::PlaySoundA from the spawn's center: ours on our own channel.
	ActorSound::Actor actor;
	if (on == m_my_id)
		actor = MyActor();
	else {
		auto it = m_actors.find(on);
		if (it == m_actors.end())
			return false;
		actor.height = it->second.size;
		actor.center = it->second.Feet() + render::Vec3{0, 0, it->second.center_height};
	}
	if (m_sound)
		m_sound->PlayActorSound(slot, {actor.center.x, actor.center.y, actor.center.z}, actor.height, actor.ours);
	return true;
}

void ZoneView::PlaySpawnWave(uint16_t on, int slot)
{
	render::Vec3 at;
	if (on == m_my_id)
		at = MyActor().center;
	else {
		auto it = m_actors.find(on);
		if (it == m_actors.end())
			return;
		at = it->second.Feet();
	}
	if (m_sound)
		m_sound->PlayWaveAt(slot, {at.x, at.y, at.z}, 100, 100);
}

void ZoneView::PlaySpellSound(const SpellEffects::Request &request, uint16_t on, int slot)
{
	if (!PlayActorSound(on, slot))
		return;
	if (m_spell_sounds.size() >= 8)
		m_spell_sounds.erase(m_spell_sounds.begin());
	m_spell_sounds.push_back({m_spell_effects.Now(), slot, request.spell, request.stage, on});
}

bool ZoneView::FindActor(uint16_t id, SpellEffects::Actor &actor) const
{
	if (id == 0)
		return false;
	if (id == m_my_id) {
		if (!m_model || !m_appearance)
			return false;
		actor.model = m_model.get();
		actor.placement = MyPlacement();
		actor.time_ms = m_player_time_ms;
		actor.animation = m_animation;
		actor.loop = m_player_loops;
		actor.race = m_appearance->race;
		actor.player = true;
		actor.level = m_my_level;
		return true;
	}
	auto it = m_actors.find(id);
	if (it == m_actors.end() || !it->second.model)
		return false;
	const SpawnView &view = it->second;
	const render::CharacterModel &model = *view.model;
	actor.model = &model;
	actor.placement.rotation = render::AxisAngle({0, 0, 1}, render::HeadingToRadians(view.actor.Heading()));
	actor.placement.translation = view.Feet() + render::Vec3{0, 0, model.OriginOverFeet()};
	actor.time_ms = view.actor.AnimationTime(model);
	actor.animation = render::AnimationCode(view.actor.Animation());
	actor.loop = view.actor.AnimationLoops();
	actor.race = view.race;
	actor.player = view.player;
	actor.level = view.level;
	return true;
}

void ZoneView::UpdateSpawns(const std::map<uint16_t, ZoneSpawn> &spawns, uint16_t my_id, uint64_t now_ms)
{
	m_my_id = my_id;
	if (!m_loaded || !m_error.empty())
		return;
	if (m_loader.joinable())
		m_loader.join();
	for (auto it = m_actors.begin(); it != m_actors.end();) {
		if (spawns.count(it->first)) {
			++it;
			continue;
		}
		if (m_sound)
			m_sound->RemoveActorLoop(it->first);
		it = m_actors.erase(it);
	}
	// The horses (and everyone else) first, then the riders on them.
	std::map<uint16_t, uint16_t> riders; // horse -> rider, theirs
	for (const auto &[rider, horse] : m_mounts)
		if (rider != my_id && spawns.count(rider) && spawns.count(horse))
			riders[horse] = rider;
	std::vector<std::pair<uint16_t, const ZoneSpawn *>> on_horses;
	for (const auto &[id, spawn] : spawns) {
		if (id == my_id || id == 0)
			continue;
		SpawnView &view = m_actors[id];
		view.race = spawn.race;
		view.player = spawn.npc == 0;
		view.level = spawn.level;
		view.dead = spawn.npc == 2 || spawn.npc == 3;
		view.walk_speed = spawn.walk_speed;
		view.run_speed = spawn.run_speed;
		auto mount = m_mounts.find(id);
		if (mount != m_mounts.end() && spawns.count(mount->second)) {
			on_horses.emplace_back(id, &spawn);
			continue;
		}
		if (view.attached) { // off the horse: on its own feet from here
			view.attached = false;
			view.actor.StopRiding();
		}
		if (id == m_horse)
			m_horse_spawn = spawn; // moved with us (DriveHorse)
		else if (auto rider = riders.find(id); rider != riders.end()) {
			// ProcessUpdateStats: the rider's updates move the horse; ChangeHeight: as tall as the rider.
			const ZoneSpawn &by = spawns.at(rider->second);
			ZoneSpawn moved = spawn;
			moved.x = by.x;
			moved.y = by.y;
			moved.z = by.z;
			moved.heading = by.heading;
			moved.dx = by.dx;
			moved.dy = by.dy;
			moved.dz = by.dz;
			moved.speed = by.speed;
			moved.heading_speed = by.heading_speed;
			moved.updated_ms = by.updated_ms;
			moved.size = by.size > 0 ? by.size : (render::IsPlayerRace(by.race) ? render::RaceHeight(by.race) : 6);
			PoseSpawn(view, moved, now_ms);
		}
		else
			PoseSpawn(view, spawn, now_ms);
		if (m_sound)
			SpawnSounds(view, spawn, id, now_ms);
	}
	for (const auto &[id, spawn] : on_horses) {
		SpawnView &view = m_actors[id];
		const SpawnView &horse = m_actors[m_mounts.at(id)];
		float ms = view.last_ms && now_ms > view.last_ms ? static_cast<float>(now_ms - view.last_ms) : 0;
		view.last_ms = now_ms;
		float size = spawn->size > 0 ? spawn->size : (render::IsPlayerRace(spawn->race) ? render::RaceHeight(spawn->race) : 6);
		view.actor.Update(*spawn, size, m_info.gravity, now_ms);
		view.light = spawn->light;
		view.warm = spawn->body_type != 3 && spawn->body_type != 5 && spawn->body_type != 8 && spawn->body_type != 12;
		view.center_height = size / 5 * 3.125f;
		view.size = size;
		render::Transform dag;
		view.attached = RiderDag(horse, dag);
		if (view.attached) {
			view.attach = {dag.rotation, dag.translation, 1};
			float origin = view.model ? view.model->OriginOverFeet() : 0;
			view.actor.Ride(dag.translation - render::Vec3{0, 0, origin}, horse.actor.Heading(),
				RidingAnimation(spawn->speed, horse.run_speed));
		}
		else
			view.actor.Ride(horse.Feet(), horse.actor.Heading(), RidingAnimation(spawn->speed, horse.run_speed));
		ResolveModel(view, *spawn, render::Length(view.Feet() - m_camera_position) < FarPlane() + 50);
		if (view.model)
			view.actor.Animate(*spawn, *view.model, ms, now_ms);
		if (m_sound)
			SpawnSounds(view, *spawn, id, now_ms);
	}
}

void ZoneView::PlaceCarriedLights(uint64_t now_ms)
{
	// ChangeLight / MoveLight: 2 in front of the spawn and 4 over its center, none in water but
	// the magic ones.
	m_carried_lights.clear();
	m_carried_light_regions.clear();
	auto add = [&](uint8_t type, const render::Vec3 &feet, float center_height, float heading, render::Environment body) {
		render::PointLight light;
		if (!type || (render::Swimming(body) && !render::CarriedLightShinesInWater(type)) ||
			!render::CarriedLight(type, m_info.short_name.c_str(), now_ms, light.color, light.radius))
			return;
		float a = render::HeadingToRadians(heading);
		light.position = feet + render::Vec3{std::cos(a) * 2, std::sin(a) * 2, center_height + 4};
		m_carried_lights.push_back(light);
		m_carried_light_regions.push_back(m_zone.Regions().RegionsTouched(light.position, light.radius));
	};
	if (m_placed)
		add(m_my_light, m_feet, m_height / 5 * 3.125f, m_heading, m_movement.BodyEnvironment());
	for (const auto &[id, view] : m_actors)
		add(view.light, view.Feet(), view.center_height, view.actor.Heading(), view.actor.BodyEnvironment());
	// The missiles' (CreateLight, MoveLight): right where they are.
	for (const Missiles::Missile &missile : m_missiles.All()) {
		render::PointLight light;
		if (!missile.made || !missile.from.light ||
			!render::CarriedLight(missile.from.light, m_info.short_name.c_str(), now_ms, light.color, light.radius))
			continue;
		light.position = m_missiles.DrawnAt(missile, now_ms);
		m_carried_lights.push_back(light);
		m_carried_light_regions.push_back(m_zone.Regions().RegionsTouched(light.position, light.radius));
	}
	// The spells' sprite flashes: EQEffect's CreateLight (type 6, the lglantern's color, out to 250)
	// where the sprite is, while it lasts.
	std::vector<render::Vec3> flashes;
	m_spell_effects.FlashLights(flashes);
	for (const render::Vec3 &at : flashes) {
		render::PointLight light;
		float radius;
		render::CarriedLight(6, m_info.short_name.c_str(), now_ms, light.color, radius);
		light.radius = std::min(250.0f, render::MaxLightRadius(m_info.short_name.c_str()));
		light.position = at;
		m_carried_lights.push_back(light);
		m_carried_light_regions.push_back(m_zone.Regions().RegionsTouched(light.position, light.radius));
	}
}

ZoneView::Hearing ZoneView::SoundListener(bool surroundings) const
{
	Hearing hearing;
	hearing.position = m_feet + render::Vec3{0, 0, m_height / 5 * 3.125f}; // the server's z: the model's center
	float yaw = render::HeadingToRadians(m_heading + m_orbit);
	hearing.forward = {std::cos(yaw), std::sin(yaw), 0};
	if (!surroundings || !m_loaded || !m_error.empty())
		return hearing;
	hearing.covered = ExposureAt(hearing.position) < 0.48f;
	// ActorInfo's ZCeiling over its Z: the first thing straight up.
	const float reach = 1000;
	render::Vec3 from = m_feet + render::Vec3{0, 0, 1};
	float fraction;
	if (m_zone.SegmentHit(from, from + render::Vec3{0, 0, reach}, fraction, nullptr, false))
		hearing.ceiling = fraction * reach + 1;
	return hearing;
}

float ZoneView::ExposureAt(const render::Vec3 &center) const
{
	float floor_z, exposure = 1;
	if (!m_zone.FloorBelow(center, 400, floor_z, &exposure))
		exposure = 1;
	return exposure;
}

render::ModelLighting ZoneView::ModelLightingAt(const render::Vec3 &center, bool warm)
{
	// s3dGetActorScaledAmbient: the ambient and the sun times the exposure of the floor under the
	// actor (straight down 400), full where there's none (applyLightingToMesh,
	// applyPhysicsAndLightingEffects); the world's constant and special ambient as they are, and
	// infravision's glow (the actor's constant ambient) on the warm-blooded.
	float exposure = ExposureAt(center);
	render::ModelLighting lighting;
	lighting.ambient = m_daylight.ambient * exposure + m_zone.ConstantAmbient() + m_daylight.special +
	                   (warm ? m_daylight.glow : render::Vec3{});
	lighting.sun_direction = m_daylight.sun_direction;
	lighting.sun_color = m_daylight.sun_color * exposure;
	lighting.lights = LightsFor(center);
	return lighting;
}

const std::vector<render::PointLight> *ZoneView::LightsFor(const render::Vec3 &center)
{
	// The 3 brightest of the zone's own lights and the carried ones that reach its region.
	int region = m_zone.Regions().Empty() ? -1 : m_zone.Regions().RegionAt(center);
	thread_local std::vector<render::PointLight> carried;
	render::SelectLights(m_zone.StaticLights(), m_zone.StaticLightRegions(), center, region, 3, m_lights_for);
	render::SelectLights(m_carried_lights, m_carried_light_regions, center, region, 3, carried);
	m_lights_for.insert(m_lights_for.end(), carried.begin(), carried.end());
	auto score = [&](const render::PointLight &light) {
		render::Vec3 d = light.position - center;
		return light.radius * light.radius * 0.05f / (render::Dot(d, d) + 1);
	};
	std::sort(m_lights_for.begin(), m_lights_for.end(),
		[&](const render::PointLight &a, const render::PointLight &b) { return score(a) > score(b); });
	if (m_lights_for.size() > 3)
		m_lights_for.resize(3);
	return m_lights_for.empty() ? nullptr : &m_lights_for;
}

void ZoneView::PoseSpawn(SpawnView &view, const ZoneSpawn &spawn, uint64_t now_ms)
{
	float ms = view.last_ms && now_ms > view.last_ms ? static_cast<float>(now_ms - view.last_ms) : 0;
	view.last_ms = now_ms;
	float size = spawn.size > 0 ? spawn.size : (render::IsPlayerRace(spawn.race) ? render::RaceHeight(spawn.race) : 6);
	view.actor.Update(spawn, size, m_info.gravity, now_ms);
	view.light = spawn.light;
	view.warm = spawn.body_type != 3 && spawn.body_type != 5 && spawn.body_type != 8 && spawn.body_type != 12;
	view.center_height = size / 5 * 3.125f;
	view.size = size;

	// Build the model once they're close enough to be seen (and again when their look changes).
	// Up close they move with the floor and the water under them; far off they just carry on.
	float range = FarPlane() + 50;
	bool near = render::Length(view.Feet() - m_camera_position) < range;
	view.actor.Step(m_zone, ms, near, now_ms);
	ResolveModel(view, spawn, near);
	if (view.model)
		view.actor.Animate(spawn, *view.model, ms, now_ms);
}

void ZoneView::ResolveModel(SpawnView &view, const ZoneSpawn &spawn, bool near)
{
	if ((near && !view.resolved) || (view.resolved && spawn.look_serial != view.look_serial)) {
		std::string key = SpawnModelKey(spawn);
		if (!view.resolved || key != view.model_key)
			view.model = SpawnModel(spawn, key);
		view.model_key = key;
		view.look_serial = spawn.look_serial;
		view.resolved = true;
	}
}

void ZoneView::DrawSpawns(uint64_t now_ms)
{
	float far_plane = FarPlane();
	for (auto &[id, view] : m_actors) {
		view.drawn = false;
		if (!view.model || m_hidden.count(id))
			continue;
		const render::CharacterModel &model = *view.model;
		float height = model.MaxZ() - model.MinZ();
		render::Vec3 center = view.Feet() + render::Vec3{0, 0, height * 0.5f};
		view.distance = render::Length(center - m_camera_position);
		if (view.distance > far_plane + height || !m_renderer->SphereVisible(center, height * 0.75f) ||
			!m_zone.InView(center, height * 0.75f))
			continue;
		render::Transform placement;
		placement.rotation = render::AxisAngle({0, 0, 1}, render::HeadingToRadians(view.actor.Heading()));
		placement.translation = view.Feet() + render::Vec3{0, 0, model.OriginOverFeet()};
		if (view.attached)
			placement = view.attach;
		uint64_t time = view.actor.AnimationTime(model);
		std::string animation = render::AnimationCode(view.actor.Animation());
		model.Draw(*m_renderer, placement, ModelLightingAt(view.Feet() + render::Vec3{0, 0, view.center_height}, view.warm),
			time, animation, view.actor.AnimationLoops());
		view.held.Draw(*m_renderer, model, placement, time, animation, view.actor.AnimationLoops(), now_ms, m_daylight.ambient);
		view.drawn = true;
		view.placement = placement;
		view.time_ms = time;
		view.animation = animation;
		view.loops = view.actor.AnimationLoops();
	}
}


std::vector<ZoneView::ViewedSpawn> ZoneView::SpawnsInView(float range) const
{
	std::vector<ViewedSpawn> out;
	if (!m_loaded || !m_error.empty())
		return out;
	render::Vec3 my_head = m_feet + render::Vec3{0, 0, m_height};
	render::Vec3 my_feet = m_feet + render::Vec3{0, 0, 0.5f}; // off the floor the line starts on
	for (const auto &[id, view] : m_actors) {
		if (!view.drawn || !view.model)
			continue;
		render::Vec3 location = view.placement.translation;
		float from_camera = render::Length(location - m_camera_position);
		if (from_camera > range)
			continue;
		ViewedSpawn s;
		s.id = id;
		s.from_camera = from_camera;
		s.camera_clear = CanSee(m_camera_position + render::Vec3{0, 0, 1}, location + render::Vec3{0, 0, 2.5f});
		render::Vec3 feet = view.Feet() + render::Vec3{0, 0, 0.5f};
		render::Vec3 head = view.Feet() + render::Vec3{0, 0, view.model->MaxZ() - view.model->MinZ()};
		render::Vec3 apart = view.Feet() - m_feet;
		s.from_me = render::Length(apart);
		s.from_me_2d = std::sqrt(apart.x * apart.x + apart.y * apart.y);
		s.me_clear = CanSee(my_head, head) || CanSee(my_head, feet) || CanSee(my_feet, feet) || CanSee(my_feet, head);
		out.push_back(s);
	}
	return out;
}

bool ZoneView::IsLuclinModel(uint16_t race, uint8_t gender)
{
	if (!m_loaded)
		return false;
	uint32_t key = static_cast<uint32_t>(race) << 8 | gender;
	auto it = m_luclin_models.find(key);
	if (it == m_luclin_models.end())
		it = m_luclin_models.emplace(key, m_models.IsLuclin(race, gender)).first;
	return it->second;
}

std::vector<uint16_t> ZoneView::DrawnSpawns() const
{
	std::vector<uint16_t> ids;
	for (const auto &[id, view] : m_actors)
		if (view.drawn)
			ids.push_back(id);
	return ids;
}

std::vector<ZoneView::NameTag> ZoneView::NameTags(float yon) const
{
	std::vector<NameTag> tags;
	for (const NameTag &tag : m_name_tags)
		if (tag.depth <= yon)
			tags.push_back(tag);
	return tags;
}

int ZoneView::HeadPoint(const render::CharacterModel &model, float &lift)
{
	auto it = m_head_points.find(&model);
	if (it == m_head_points.end()) {
		if (m_head_points.size() > 512)
			m_head_points.clear(); // models come and go
		const std::string &tag = model.Code();
		std::string name = tag + "HEHEAD_DAG"; // a Luclin model's (its model's +0x3b0)
		int bone = model.FindBone(name);
		if (bone < 0) {
			name = tag + "HEAD_POINT_DAG";
			bone = model.FindBone(name);
		}
		if (bone < 0) {
			name = tag + "HE_DAG";
			bone = model.FindBone(name);
		}
		float factor = 0.55f;
		if (name.size() > 5 && std::toupper(static_cast<unsigned char>(name[5])) == 'H')
			factor = name[0] == 'O' && name[1] == 'G' ? 1.2f : 1.1f;
		it = m_head_points.emplace(&model, std::make_pair(bone, factor)).first;
	}
	lift = it->second.second * model.Scale() + 1.0f;
	return it->second.first;
}

void ZoneView::DrawNameSprites(const render::Camera &camera)
{
	m_name_tags.clear();
	if (m_name_sprites.empty())
		return;
	const render::Vec3 &right = m_renderer->Right(), &up = m_renderer->Up(), &forward = m_renderer->Forward();
	// renderNameTag lights the tint itself: no vertex light, no ambient add, no sun.
	m_renderer->SetVertexLight({1, 1, 1}, {0, 0, 0});
	m_renderer->SetDynamicLights(false, 0);
	render::Material material;
	material.blend = render::Blend::Translucent;
	material.texel_alpha = true;
	material.two_sided = true;
	material.fog = false;
	static const uint32_t Quad[6] = {0, 1, 2, 0, 2, 3};
	std::vector<render::Transform> placed;
	for (const NameSprite &sprite : m_name_sprites) {
		// The actor as it was just drawn.
		const render::CharacterModel *model = nullptr;
		render::Transform placement;
		uint64_t time = 0;
		std::string animation;
		bool loops = true;
		if (sprite.id == 0) {
			if (!m_model_shown)
				continue;
			model = m_model.get();
			placement = MyPlacement();
			time = m_player_time_ms;
			animation = m_animation;
			loops = m_player_loops;
		}
		else {
			auto it = m_actors.find(sprite.id);
			if (it == m_actors.end() || !it->second.drawn || !it->second.model)
				continue;
			model = it->second.model.get();
			placement = it->second.placement;
			time = it->second.time_ms;
			animation = it->second.animation;
			loops = it->second.loops;
		}
		float lift;
		int bone = HeadPoint(*model, lift);
		if (bone < 0)
			continue; // SetNameSpriteState: no DagHeadPoint, no name
		model->PlaceBones(placement, time, animation, loops, {bone}, placed);
		render::Vec3 head = placed[0].translation;
		// The head point's depth; s3dSetStringSpriteYonClip's yon.
		float depth = render::Dot(head - camera.position, forward);
		if (depth <= camera.near_plane || depth > m_name_yon)
			continue;
		// A unit a line (20 texels of 0.05) from 20 units out; closer, the size it had at 20.
		float near_scale = depth < ::NameSprite::NearestDepth ? depth / ::NameSprite::NearestDepth : 1.0f;
		render::Vec3 anchor = head + up * lift;
		// The first line centered on the anchor, its bottom kept where it'd be unscaled; the next
		// under it.
		float bottom = 0;
		uint32_t color = NameSpriteLit(sprite.color, m_daylight.ambient.x, m_daylight.ambient.y, m_daylight.ambient.z);
		for (int line = 0; line < 2; ++line) {
			const NameLine &text = sprite.lines[line];
			if (!text.texture || text.width <= 0 || text.height <= 0)
				continue;
			float texel = ::NameSprite::UnitsPerTexel * near_scale * (line == 0 ? 1.0f : ::NameSprite::LineScale);
			float height = text.height * texel;
			float top;
			if (line == 0) {
				bottom = -0.5f * text.height * ::NameSprite::UnitsPerTexel;
				top = bottom + height;
			}
			else {
				top = bottom;
				bottom = top - height;
			}
			float x0 = -(text.text_width * 0.5f + text.left) * texel, x1 = x0 + text.width * texel;
			float u1 = static_cast<float>(text.width) / text.texture->width;
			float v1 = static_cast<float>(text.height) / text.texture->height;
			m_name_vertices.assign(4, render::Vertex{});
			m_name_vertices[0].position = anchor + right * x0 + up * top;
			m_name_vertices[1].position = anchor + right * x1 + up * top;
			m_name_vertices[2].position = anchor + right * x1 + up * bottom;
			m_name_vertices[3].position = anchor + right * x0 + up * bottom;
			m_name_vertices[0].u = 0, m_name_vertices[0].v = 0;
			m_name_vertices[1].u = u1, m_name_vertices[1].v = 0;
			m_name_vertices[2].u = u1, m_name_vertices[2].v = v1;
			m_name_vertices[3].u = 0, m_name_vertices[3].v = v1;
			for (render::Vertex &v : m_name_vertices) {
				v.color = color & 0xffffff;
				v.normal = forward * -1.0f;
			}
			material.texture = text.texture;
			m_renderer->Draw(m_name_vertices.data(), 4, Quad, 6, material);
		}
		float sx, sy, d;
		if (m_renderer->Project(anchor, sx, sy, d))
			m_name_tags.push_back({sprite.id, static_cast<int>(sx), static_cast<int>(sy), depth, m_renderer->Focal()});
	}
	std::sort(m_name_tags.begin(), m_name_tags.end(), [](const NameTag &a, const NameTag &b) { return a.depth < b.depth; });
}
