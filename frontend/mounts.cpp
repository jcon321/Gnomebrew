#include "mounts.h"

#include "zone_session.h"
#include "zone_view.h"

#include <cctype>
#include <cstring>
#include <strings.h>

namespace
{
	constexpr uint16_t HorseRace = 216;
	constexpr int SummonHorse = 113; // SPA 0x71

	bool SameName(const std::string &a, const std::string &b)
	{
		return strcasecmp(a.c_str(), b.c_str()) == 0;
	}

	// CEverQuest::trimName, as ZoneSpawn::DisplayName does it.
	std::string Trimmed(const std::string &name)
	{
		ZoneSpawn spawn;
		spawn.name = name;
		return spawn.DisplayName();
	}
}

void Mounts::Attach(ZoneSession &zone, Host host)
{
	m_zone = &zone;
	m_host = std::move(host);
}

void Mounts::Reset()
{
	m_mount.clear();
	m_rider.clear();
	m_initial = false;
}

const ZoneSpawn *Mounts::Find(uint16_t id) const
{
	if (!m_zone || id == 0)
		return nullptr;
	if (id == m_zone->MySpawnId())
		return &m_zone->Self();
	auto it = m_zone->Spawns().find(id);
	return it != m_zone->Spawns().end() ? &it->second : nullptr;
}

uint16_t Mounts::Me() const
{
	return m_zone ? m_zone->MySpawnId() : 0;
}

bool Mounts::Ready() const
{
	return m_view && m_view->Loaded() && m_view->Error().empty();
}

bool Mounts::InvalidRiderTexture(uint16_t rider) const
{
	const ZoneSpawn *spawn = Find(rider);
	return spawn && !m_view->IsLuclinModel(spawn->race, spawn->gender);
}

bool Mounts::UntexturedHorse(uint16_t horse) const
{
	const ZoneSpawn *spawn = Find(horse);
	return spawn && spawn->race == HorseRace && !m_view->HasOwnModel(spawn->race, spawn->gender);
}

void Mounts::SpawnAdded(uint16_t id)
{
	if (Ready() && m_initial)
		MakeRiderMountUp(id);
}

void Mounts::Frame()
{
	if (m_initial || !Ready() || !m_zone)
		return;
	m_initial = true;
	for (const auto &[id, spawn] : m_zone->Spawns())
		MakeRiderMountUp(id);
}

void Mounts::MakeRiderMountUp(uint16_t horse)
{
	// An NPC horse with no Rider yet, named "<rider>`s_Mount00".
	const ZoneSpawn *spawn = Find(horse);
	if (!spawn || spawn->npc != 1 || spawn->race != HorseRace || m_rider.count(horse))
		return;
	const std::string &name = spawn->name;
	if (name.size() <= 8 || name.find("`s_Mount") == std::string::npos)
		return;
	std::string rider_name = name.substr(0, name.size() - 10);
	if (rider_name.empty())
		return;
	// EQPlayer_get_player: the first spawn of the name, ours included.
	uint16_t rider = 0;
	if (SameName(m_zone->Self().name, rider_name))
		rider = Me();
	else
		for (const auto &[id, other] : m_zone->Spawns())
			if (SameName(other.name, rider_name)) {
				rider = id;
				break;
			}
	if (rider)
		Mount(rider, horse);
}

void Mounts::Tick()
{
	// ProcessAffects: a buff of SPA 113 and no Mount: onto "<our name>`s_Mount", the shortest name
	// starting so (trimName'd both ways, case aside).
	if (!m_zone || !Ready() || !m_host.has_affect || !m_host.has_affect(SummonHorse) || m_mount.count(Me()))
		return;
	std::string wanted = Trimmed(m_zone->Self().name.substr(0, 0x17) + "`s_Mount");
	uint16_t found = 0;
	size_t found_length = 0;
	auto consider = [&](uint16_t id, const ZoneSpawn &spawn) {
		std::string trimmed = Trimmed(spawn.name);
		if (strncasecmp(trimmed.c_str(), wanted.c_str(), wanted.size()) != 0)
			return;
		if (!found || spawn.name.size() < found_length) {
			found = id;
			found_length = spawn.name.size();
		}
	};
	for (const auto &[id, spawn] : m_zone->Spawns())
		consider(id, spawn);
	consider(Me(), m_zone->Self());
	if (found)
		Mount(Me(), found);
}

void Mounts::Mount(uint16_t rider, uint16_t horse)
{
	const ZoneSpawn *horse_spawn = Find(horse);
	if (!horse_spawn || !Find(rider) || !Ready())
		return;
	auto on = m_mount.find(rider);
	if (on != m_mount.end()) {
		if (on->second == horse)
			return;
		Dismount(rider);
	}
	// Without the Luclin player models the horse only takes its Rider.
	if (!m_view->LuclinPcModels()) {
		m_rider[horse] = rider;
		return;
	}
	if (horse_spawn->race != HorseRace)
		return; // onto something's DagRightPoint: not done
	bool invalid = InvalidRiderTexture(rider);
	if (invalid)
		m_rider[horse] = rider;
	// CDisplay_AttachPlayerToPlayerDag with no dag: the horse's <tag>RIDER_DAG (HSMRIDER_DAG,
	// HSNRIDER_DAG: every horse model has one), never for an old model or an untextured horse.
	if (invalid || UntexturedHorse(horse)) {
		if (rider == Me()) {
			if (m_host.say)
				m_host.say(5524,
					"You must have both the Horse Models and your current Luclin Character Model enabled to ride a horse.",
					13);
			if (m_host.remove_affect)
				m_host.remove_affect(SummonHorse);
		}
		return;
	}
	m_mount[rider] = horse;
	m_rider[horse] = rider;
}

void Mounts::Dismount(uint16_t rider)
{
	auto on = m_mount.find(rider);
	if (on == m_mount.end())
		return;
	m_rider.erase(on->second);
	m_mount.erase(on);
}

uint16_t Mounts::ChangeFormOff(uint16_t rider)
{
	auto on = m_mount.find(rider);
	if (on == m_mount.end())
		return 0;
	uint16_t horse = on->second;
	Dismount(rider);
	return horse;
}

void Mounts::ChangeFormOn(uint16_t horse)
{
	if (horse && Ready())
		MakeRiderMountUp(horse);
}

bool Mounts::DismountCommand()
{
	if (!m_mount.count(Me()))
		return false;
	if (m_host.say)
		m_host.say(13284, "Dismounting.", 0x111);
	if (m_host.remove_affect)
		m_host.remove_affect(SummonHorse);
	Dismount(Me());
	return true;
}

bool Mounts::StanceChange(uint16_t id, uint8_t stance)
{
	if (!m_mount.count(id))
		return true;
	if (stance == 110)
		return false; // no sitting on a horse
	if (stance == 115) {
		Dismount(id);
		if (id == Me() && m_host.remove_affect)
			m_host.remove_affect(SummonHorse);
	}
	return true;
}

void Mounts::SpawnRemoved(uint16_t id)
{
	// Dismount2: whoever rides it gets off, and it gets off what it rides.
	for (auto it = m_mount.begin(); it != m_mount.end();) {
		auto next = std::next(it);
		if (it->second == id || it->first == id)
			Dismount(it->first);
		it = next;
	}
	// ~EQPlayer: a Rider that was it is cleared (an old model's horse), and its own go.
	for (auto it = m_rider.begin(); it != m_rider.end();)
		it = it->second == id || it->first == id ? m_rider.erase(it) : std::next(it);
}

uint16_t Mounts::MountOf(uint16_t rider) const
{
	auto it = m_mount.find(rider);
	return it != m_mount.end() ? it->second : 0;
}

uint16_t Mounts::RiderOf(uint16_t horse) const
{
	auto it = m_rider.find(horse);
	return it != m_rider.end() ? it->second : 0;
}

bool Mounts::Mounted() const
{
	return m_mount.count(Me()) != 0;
}

std::set<uint16_t> Mounts::Hidden() const
{
	std::set<uint16_t> hidden;
	if (!m_zone || !Ready())
		return hidden;
	for (const auto &[id, spawn] : m_zone->Spawns()) {
		if (spawn.race != HorseRace)
			continue;
		uint16_t rider = RiderOf(id);
		if (UntexturedHorse(id) || (rider && InvalidRiderTexture(rider)))
			hidden.insert(id);
	}
	return hidden;
}
