#include "zone_doors.h"
#include "eq_sound.h"

#include "zone_scene.h"

#include "zone_names.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace
{
	// CDisplay_FixHeading: into 0..512.
	float FixHeading(float heading)
	{
		heading = std::fmod(heading, 512.0f);
		return heading < 0 ? heading + 512 : heading;
	}

	// CDisplay_HeadingDiff: how far apart two headings are, the short way round (0..256).
	float HeadingDiff(float a, float b)
	{
		float d = std::fabs(a - b);
		return d <= 256 ? d : 512 - d;
	}

	// GetDoorMovementValue: how far a lift goes, from its parameter.
	float MoveValue(int16_t param)
	{
		return param >= 1 ? param : param > -4 ? 0 : -static_cast<float>(param);
	}

	// EQSwitch::GetSwitchDamage: what a trap hurts for; 0 for everything else.
	int Damage(uint8_t type)
	{
		switch (type) {
		case 115: case 116: case 130: case 135: return 4;
		case 120: case 125: return 30;
		case 140: case 145: case 150: case 151: return 10;
		case 146: case 152: return 50;
		default: return 0;
		}
	}

	// GetSwitchStateChangeTime: how long one stays open before it closes by itself, ms; 0 never.
	uint32_t CloseAfterMs(const ZoneDoor &door)
	{
		uint32_t ms = 20000;
		switch (door.type) {
		case 0: case 5: ms = 10000; break;
		case 35: case 116: case 145: case 151: ms = 6000; break;
		case 40: case 45: case 50:
			if (door.param > 0)
				ms = door.param == 0x7d01 ? 1800000 : static_cast<uint32_t>(door.param);
			break;
		case 57: case 58: case 140: ms = 2000; break;
		case 59: if (door.param > 170) ms = 0; break;
		case 105: case 106: case 107: ms = 0; break;
		case 115: ms = 1; break;
		case 120: case 125: case 135: case 146: case 152: ms = 3000; break;
		case 130: ms = 800; break;
		case 150: ms = 4000; break;
		default: break;
		}
		return door.param == -2 || door.param == -3 ? 0 : ms;
	}

	// DoorTable_CalculateDoorMovementParameters: a spinner's top speed (a tick), and how much faster
	// it gets each frame on the way.
	float SpinSpeed(uint8_t type, float &speed_up)
	{
		switch (type) {
		case 100: case 105: speed_up = 0.1f; return 3;
		case 101: case 106: speed_up = 0.075f; return 9;
		case 102: case 107: speed_up = 0.125f; return 15;
		case 115: case 116: speed_up = 0.375f; return 45;
		default: speed_up = 0; return 0;
		}
	}
}

void ZoneDoors::Spawn(const std::vector<ZoneDoor> &doors, uint64_t now_ms)
{
	m_doors.clear();
	for (const ZoneDoor &spawn : doors) {
		Door door;
		door.spawn = spawn;
		door.rest = {spawn.y, spawn.x, spawn.z}; // WLD x/y are the server's y/x
		door.rest_heading = spawn.heading;
		door.rest_incline = spawn.incline;
		door.at = door.rest;
		door.heading = door.rest_heading;
		door.incline = door.rest_incline;
		door.scale = (spawn.size == 255 || spawn.size < 1 ? 100 : spawn.size) * 0.01f;
		// ResetSwitchPosition: one that comes open starts opening.
		door.state = spawn.open == 1 ? 2 : spawn.open == 3 ? 0 : spawn.open;
		door.visible = spawn.type != 53 && spawn.type != 50 && spawn.type != 54 && !(spawn.type == 57 && spawn.incline == 1);
		door.changed_ms = now_ms;
		m_doors.push_back(door);
		m_spawned_types.push_back(spawn.type);
	}
}

std::vector<ZoneDoors::Sound> ZoneDoors::TakeSounds()
{
	std::vector<Sound> sounds;
	sounds.swap(m_sounds);
	return sounds;
}

std::vector<uint8_t> ZoneDoors::TakeSpawnedTypes()
{
	std::vector<uint8_t> types;
	types.swap(m_spawned_types);
	return types;
}

size_t ZoneDoors::Shown() const
{
	return static_cast<size_t>(std::count_if(m_doors.begin(), m_doors.end(), [](const Door &door) { return door.scene >= 0; }));
}

void ZoneDoors::ChangeState(uint8_t id, uint8_t state, uint64_t now_ms)
{
	for (Door &door : m_doors)
		if (door.spawn.id == id) {
			SetState(door, state, now_ms);
			return;
		}
}

void ZoneDoors::SetState(Door &door, uint8_t state, uint64_t now_ms) const
{
	// EQSwitch::ChangeState: closing starts a spinner at its top speed; open has it at rest.
	float speed_up;
	if (state == 3)
		door.speed = SpinSpeed(door.spawn.type, speed_up);
	else if (state == 1)
		door.speed = 0;
	if (door.spawn.type == 57 || door.spawn.type == 58 || door.state == state)
		return;
	door.state = state;
	door.changed_ms = now_ms;
	if (int slot = audio::EqSound::DoorSound(door.spawn.type, state))
		m_sounds.push_back({slot, door.at});
}

void ZoneDoors::Toggle(Door &door) const
{
	// ToggleDoorState: back to its resting state. Types 40 and 45 instead use the switch again
	// (UseSwitch with nobody's id, which goes to the zone); the zone closes those itself.
	if (door.state == door.spawn.inverted || door.spawn.type == 40 || door.spawn.type == 45)
		return;
	door.state = door.spawn.inverted == 0 ? 3 : door.spawn.inverted == 1 ? 2 : door.spawn.inverted;
}

bool ZoneDoors::Slide(Door &door, bool finish, float ticks, uint64_t now_ms) const
{
	// EQSwitch_UpdateDoorPosition: sideways, 0.35 a tick, by how far its type says.
	uint8_t type = door.spawn.type;
	float speed = 0.35f, distance = 0;
	if (type >= 10 && type <= 12)
		distance = 8;
	else if (type >= 15 && type <= 17)
		distance = 10;
	else if (type >= 20 && type <= 22)
		distance = 14;
	else if ((type >= 25 && type <= 27) || (type >= 78 && type <= 81)) {
		distance = MoveValue(door.spawn.param);
		if (distance == 0)
			distance = 22;
		if (type == 79 || type == 81)
			speed = -speed;
	}
	if (speed < 0)
		distance = -distance;
	// Across the way it faces: facing west (128) it slides along x, south (256) along y, east
	// (384) back along x and north back along y.
	float *axis = &door.at.y, rest = door.rest.y;
	if (door.heading == 128) {
		axis = &door.at.x;
		rest = door.rest.x;
	}
	else if (door.heading != 256) {
		if (door.heading == 384) {
			axis = &door.at.x;
			rest = door.rest.x;
		}
		speed = -speed;
		distance = -distance;
	}
	float open = rest + distance;
	distance = std::fabs(distance);
	if (door.state == 2) {
		*axis += speed * ticks;
		if (std::fabs(*axis - rest) < distance)
			return false;
		*axis = open;
		if (finish)
			SetState(door, 1, now_ms);
	}
	else {
		*axis -= speed * ticks;
		if (std::fabs(*axis - open) < distance)
			return false;
		*axis = rest;
		if (finish)
			SetState(door, 0, now_ms);
	}
	return true;
}

void ZoneDoors::Step(Door &door, float ticks, uint64_t now_ms) const
{
	uint8_t type = door.spawn.type;
	// 50-58 (plates, teleporters, the particle effect...) don't move.
	if (type >= 50 && type <= 58)
		return;
	// Out of its resting state long enough, it goes back; a sprung trap after a minute.
	uint64_t elapsed = now_ms - door.changed_ms;
	if (door.state != door.spawn.inverted) {
		if (door.state != 4) {
			uint32_t after = CloseAfterMs(door.spawn);
			if (after != 0 && after < elapsed)
				Toggle(door);
		}
		else if (elapsed <= 60000)
			return;
		else
			SetState(door, door.spawn.inverted, now_ms);
	}
	if (door.state == 0 || (door.state == 1 && type < 100))
		return;

	if (type < 9) {
		// Swinging a quarter turn about the hinge, 8 a tick (5 to 7 the other way).
		bool back = type >= 5 && type <= 7;
		float speed = back ? -8.0f : 8.0f;
		float open = FixHeading(door.rest_heading + (back ? -128 : 128));
		if (door.state == 2) {
			door.heading = FixHeading(door.heading + ticks * speed);
			if (HeadingDiff(door.heading, door.rest_heading) >= 128) {
				door.heading = open;
				SetState(door, 1, now_ms);
			}
		}
		else {
			door.heading = FixHeading(door.heading - ticks * speed);
			if (HeadingDiff(door.heading, open) >= 128) {
				door.heading = door.rest_heading;
				SetState(door, 0, now_ms);
			}
		}
	}
	else if (type == 30 || type == 35 || type == 40 || type == 130) {
		// Tipping over by the incline: trapdoors and drawbridges.
		float speed = type == 30 ? 10.0f : type == 35 || type == 130 ? 16.0f : 8.0f;
		float turn = type == 130 ? 248.0f : 128.0f;
		float open = FixHeading(door.rest_incline - turn);
		if (door.state == 2) {
			if (HeadingDiff(door.incline, door.rest_incline) >= turn) {
				door.incline = open;
				SetState(door, 1, now_ms);
			}
			else
				door.incline = FixHeading(door.incline - ticks * speed);
		}
		else if (door.state == 3) {
			if (HeadingDiff(door.incline, open) >= turn) {
				door.incline = door.rest_incline;
				SetState(door, 0, now_ms);
			}
			else
				door.incline = FixHeading(door.incline + ticks * speed);
		}
	}
	else if ((type >= 59 && type <= 81) || type == 120 || (type >= 140 && type <= 146)) {
		// Rising or sinking: lifts, portcullises, falling blocks. How far, and how fast it goes
		// (opening) and comes back (closing), a tick; 78 to 81 slide too, and only close once that's done.
		float distance = 128, open_speed = 0.25f, close_speed = 0.25f;
		bool slid = true;
		if (type == 59) {
			distance = MoveValue(door.spawn.param);
			open_speed = close_speed = 0.45f;
		}
		else if (type >= 60 && type <= 62) {
			distance = 8;
			open_speed = 0.45f;
		}
		else if (type >= 65 && type <= 67) {
			distance = 10;
			open_speed = 0.45f;
		}
		else if (type >= 70 && type <= 72) {
			distance = 14;
			open_speed = 0.45f;
		}
		else if (type >= 75 && type <= 77) {
			distance = MoveValue(door.spawn.param);
			if (distance == 0)
				distance = 22;
			open_speed = 0.45f;
		}
		else if (type >= 78 && type <= 81) {
			slid = Slide(door, false, ticks, now_ms);
			float sign = type >= 80 ? -1.0f : 1.0f;
			distance = sign * MoveValue(door.spawn.param);
			open_speed = close_speed = sign * 0.45f;
		}
		else if (type == 120 || type == 140) {
			distance = -10;
			open_speed = -1;
			close_speed = -0.25f;
		}
		else if (type == 145) {
			distance = -10;
			open_speed = close_speed = -0.25f;
		}
		else if (type == 146) {
			distance = -10;
			open_speed = close_speed = -1;
		}
		float open = door.rest.z + distance;
		distance = std::fabs(distance);
		if (door.state == 2) {
			door.at.z += ticks * open_speed;
			if (std::fabs(door.at.z - door.rest.z) >= distance) {
				door.at.z = open;
				SetState(door, 1, now_ms);
			}
		}
		else if (door.state == 3) {
			door.at.z -= ticks * close_speed;
			if (std::fabs(door.at.z - open) >= distance) {
				door.at.z = door.rest.z;
				if (slid)
					SetState(door, 0, now_ms);
			}
		}
	}
	else if (type >= 10 && type <= 27)
		Slide(door, true, ticks, now_ms);
	else if ((type >= 100 && type <= 107) || type == 115 || type == 116) {
		// Spinning, the heading (100 to 104) or the incline: faster each frame while it opens up to
		// its top speed, slower each frame while it closes. The client speeds up by the frame; we
		// take a frame as 1/30 s.
		float speed_up;
		float speed = SpinSpeed(type, speed_up);
		speed_up *= ticks * 1.5f;
		float &angle = type > 104 ? door.incline : door.heading;
		bool turn = true;
		if (door.state == 2) {
			door.speed += speed_up;
			if (door.speed >= speed) {
				SetState(door, 1, now_ms);
				turn = false;
			}
			else
				speed = door.speed;
		}
		else if (door.state != 1) {
			door.speed -= speed_up;
			if (door.speed <= 0) {
				SetState(door, 0, now_ms);
				turn = false;
			}
			else
				speed = door.speed;
		}
		if (turn)
			angle = FixHeading(angle + speed * ticks);
	}

	if (type == 45 || type == 109 || type == 115 || type == 116 || type == 125 || (type >= 150 && type <= 152)) {
		// Along the way it faces (blades, rams): how far, and how fast out and back, a tick.
		float open_speed = 0.75f, close_speed = 0.75f, distance = 10;
		switch (type) {
		case 125: close_speed = 0.35f; break;
		case 45: case 109: open_speed = close_speed = -0.05f; distance = -1; break;
		case 115: distance = 20; break;
		case 116: distance = 40; break;
		case 150: open_speed = 1; close_speed = 0.25f; break;
		case 151: open_speed = close_speed = 0.25f; break;
		case 152: open_speed = close_speed = 1; break;
		default: break;
		}
		// Facing north (0) along x, west (128) along y, south (256) back along x, east back along y.
		float *axis = &door.at.y, rest = door.rest.y;
		if (door.heading == 0) {
			axis = &door.at.x;
			rest = door.rest.x;
		}
		else if (door.heading != 128) {
			if (door.heading == 256) {
				axis = &door.at.x;
				rest = door.rest.x;
			}
			open_speed = -open_speed;
			close_speed = -close_speed;
			distance = -distance;
		}
		float open = rest + distance;
		distance = std::fabs(distance);
		if (door.state == 2) {
			*axis += ticks * open_speed;
			if (std::fabs(*axis - rest) >= distance) {
				*axis = open;
				SetState(door, 1, now_ms);
			}
		}
		else if (door.state == 3) {
			*axis -= ticks * close_speed;
			if (std::fabs(*axis - open) >= distance) {
				*axis = rest;
				SetState(door, 0, now_ms);
			}
		}
	}

	// Out, traps go straight back (109 to its resting state).
	if (door.state == 1 && type != 115) {
		if (type == 109)
			Toggle(door);
		else if (type >= 110 && type <= 152)
			SetState(door, 3, now_ms);
	}
}

void ZoneDoors::Update(render::ZoneScene &scene, uint64_t now_ms, int hour, bool emit)
{
	float ticks = m_last_ms && now_ms > m_last_ms ? (now_ms - m_last_ms) * 0.02f : 0;
	m_last_ms = now_ms;
	for (Door &door : m_doors) {
		if (door.spawn.type == 53) {
			// UpdateDoorStateBasedOnWorldTime: the incline (EQSwitch+0x30) 1 or 2; its own
			// +0xcf check (on the 2's) isn't known and counts as 0.
			if (door.spawn.incline == 1)
				door.state = hour < 5 || hour > 18 ? 0 : 1;
			else if (door.spawn.incline == 2)
				door.state = hour > 4 && hour < 19 ? 0 : 1;
			int tag = 0x10000 | door.spawn.id;
			if (emit && door.state == 1 && !door.emitting && door.spawn.param > 0 && door.spawn.param < 0x100) {
				door.emitting = true;
				m_particles.push_back({true, tag, door.spawn.param, door.at});
			}
			else if (door.state == 0 && door.emitting) {
				door.emitting = false;
				m_particles.push_back({false, tag, 0, door.at});
			}
		}
		render::Vec3 at = door.at;
		float heading = door.heading, incline = door.incline;
		Step(door, ticks, now_ms);
		if (door.tried && door.scene < 0)
			continue;
		bool moved = at.x != door.at.x || at.y != door.at.y || at.z != door.at.z || heading != door.heading || incline != door.incline;
		if (door.tried && !moved)
			continue;
		// t3dSetActorLocation: turned to its heading, then tilted by its incline, the actor's pitch,
		// which updateActorTransformMatrix turns about y the other way (-pitch).
		render::Transform placement;
		placement.rotation = render::AxisAngle({0, 0, 1}, render::HeadingToRadians(door.heading)) *
		                     render::AxisAngle({0, 1, 0}, -render::HeadingToRadians(door.incline));
		placement.translation = door.at;
		placement.scale = door.scale;
		if (!door.tried && door.spawn.type == 53) {
			door.tried = true; // EQSwitch_DoorLoadingCode makes no actor for a particle switch
			continue;
		}
		if (!door.tried) {
			door.tried = true;
			// The invisible ones too (not drawn, not clicked, not walked into): a floor as
			// CDisplay_IsCollisionValid has it, which the pressure plates (50) aren't.
			door.scene = scene.AddDoor(door.spawn.name, placement, !door.visible);
			if (!door.visible && door.spawn.type != 50)
				scene.SetDoorFloor(door.scene, true);
			door.radius = scene.DoorRadius(door.scene);
		}
		else
			scene.PlaceDoor(door.scene, placement);
	}
}

ZoneDoors::Use ZoneDoors::Click(int scene_door, const render::Vec3 &center, float height, const Held &held, uint8_t &id,
	uint16_t &item, int16_t &skill) const
{
	auto found = std::find_if(m_doors.begin(), m_doors.end(), [&](const Door &door) { return door.scene == scene_door; });
	if (found == m_doors.end())
		return Use::Nothing;
	const Door &door = *found;
	// CEverQuest_ClickedSwitch: a trap takes the disarm traps skill, anything else what we hold.
	int damage = Damage(door.spawn.type);
	item = damage != 0 ? 0xffff : held.item;
	skill = std::min<int16_t>(damage != 0 ? held.disarm : held.skill, 0xfe);
	return UseSwitch(door, true, center, height, item, skill, id);
}

ZoneDoors::Use ZoneDoors::UseSwitch(const Door &door, bool spawn, const render::Vec3 &center, float height, uint16_t item,
	int16_t skill, uint8_t &id) const
{
	(void)item;
	uint8_t type = door.spawn.type;
	if (type == 54 || type == 55 || type == 56)
		return Use::Nothing;
	// Not while it's moving (spinners and traps aside), nor once a parameter of -2 has it open.
	int damage = Damage(type);
	if ((type < 105 || type > 107) && damage == 0 && (door.state == 2 || door.state == 3))
		return Use::Nothing;
	if (door.state == 1 && door.spawn.param == -2)
		return Use::Nothing;
	skill = std::min<int16_t>(skill, 0xfe);
	if (spawn) {
		// A trap does nothing without the skill (EQSwitch_GetSwitchDamage2).
		if (damage != 0 && (skill < 1 || skill == 0xfe))
			return Use::Nothing;
		// Within reach: a quarter of the model's radius, our height, and 20.
		if (render::Length(door.at - center) > door.radius * 0.25f + height + 20)
			return Use::TooFar;
	}
	id = door.spawn.id;
	return Use::Ask;
}

int ZoneDoors::TouchedSwitch(const std::vector<std::pair<int, float>> &under, const render::Vec3 &from, float floor_z,
	float height, uint32_t zone_id) const
{
	int touched = -1;
	float lowest = 0;
	for (const auto &[scene_door, z] : under) {
		auto found = std::find_if(m_doors.begin(), m_doors.end(), [&](const Door &door) { return door.scene == scene_door; });
		if (found == m_doors.end())
			continue;
		uint8_t type = found->spawn.type;
		bool touches = type == 70 ? zone_id == 59 : type > 7 && (type < 60 || type > 77);
		if (!touches || from.z - z >= height + 2 || z < floor_z - 0.01f)
			continue;
		if (touched < 0 || z < lowest) {
			touched = scene_door;
			lowest = z;
		}
	}
	return touched;
}

ZoneDoors::Touch ZoneDoors::Touching(int scene_door, uint16_t me, uint16_t cursor_item, const render::Vec3 &center,
	float height, uint64_t now_ms, int percent)
{
	Touch touch;
	auto found = std::find_if(m_doors.begin(), m_doors.end(), [&](const Door &door) { return door.scene == scene_door; });
	if (scene_door < 0 || found == m_doors.end())
		return touch;
	Door &door = *found;
	if (door.state == 4)
		return touch;
	uint8_t type = door.spawn.type;
	auto use = [&](bool spawn, uint16_t item) {
		uint8_t id = 0;
		switch (UseSwitch(door, spawn, center, height, item, 0, id)) {
		case Use::Ask:
			touch.kind = Touch::Kind::Ask;
			touch.id = id;
			touch.item = item;
			touch.spawn = spawn ? me : 0;
			return true;
		case Use::TooFar:
			touch.kind = Touch::Kind::TooFar;
			return false;
		default:
			return false;
		}
	};
	if (type == 35) {
		if (now_ms - m_touch_ms <= 1000)
			return touch;
		m_touch_ms = now_ms;
		if (percent < 51)
			return touch;
		use(false, 0);
		return touch;
	}
	if (type == 57) {
		if (now_ms - m_touch_ms <= 2000)
			return touch;
		m_touch_ms = now_ms;
		auto entry = std::find_if(m_teleports.begin(), m_teleports.end(),
			[&](const TeleportEntry &e) { return e.id == door.spawn.param; });
		// DoesZoneFileExist: the zone's <short name>.s3d in the folder.
		std::string name = entry == m_teleports.end() ? "" : ShortZoneName(static_cast<uint32_t>(entry->zone));
		std::error_code ec;
		if (name.empty() || !std::filesystem::exists(m_eq_dir + "/" + name + ".s3d", ec))
			return touch;
		use(true, cursor_item);
		return touch;
	}
	if (type == 50) {
		if (door.state != 0) {
			if (CloseAfterMs(door.spawn) < now_ms - m_touch_ms)
				door.state = 0;
			return touch;
		}
		use(true, 0xffff);
		m_touch_ms = now_ms;
		return touch;
	}
	if (type >= 115) {
		uint64_t wait = CloseAfterMs(door.spawn) < 1000 ? 400 : 1000;
		if (m_last_trap == scene_door && now_ms - m_touch_ms < wait)
			return touch;
		m_last_trap = scene_door;
		touch.kind = Touch::Kind::Trap;
		m_touch_ms = now_ms;
	}
	return touch;
}

void ZoneDoors::LoadTeleportTable(const std::string &eq_dir, const std::string &zone)
{
	// Missing, eqgame sleeps a second and keeps the last zone's table; empty, it exits.
	m_eq_dir = eq_dir;
	std::ifstream in(eq_dir + "/" + zone + "_tp.txt", std::ios::binary);
	if (!in)
		return;
	m_teleports.clear();
	std::string line;
	if (!std::getline(in, line))
		return;
	while (std::getline(in, line)) {
		if (line.empty())
			break; // readLineFromFile under 1: the end
		std::vector<std::string> fields;
		size_t start = 0;
		for (size_t i = 0; i <= line.size(); ++i)
			if (i == line.size() || line[i] == '^' || line[i] == '\n') {
				fields.push_back(line.substr(start, i - start));
				start = i + 1;
			}
		fields.resize(6);
		TeleportEntry e;
		e.id = static_cast<int16_t>(std::atoi(fields[0].c_str()));
		e.y = static_cast<float>(std::atof(fields[1].c_str()));
		e.x = static_cast<float>(std::atof(fields[2].c_str()));
		e.z = static_cast<float>(std::atof(fields[3].c_str()));
		e.heading = static_cast<float>(std::atof(fields[4].c_str()));
		e.zone = std::atoi(fields[5].c_str());
		m_teleports.push_back(e);
	}
}
