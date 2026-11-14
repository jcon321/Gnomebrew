#include "duel.h"

namespace
{
	constexpr int Yellow = 15, Broadcast = 0x10d, Default = 0x111;
}

void Duel::Attach(ZoneSession &zone, Host host)
{
	m_zone = &zone;
	m_host = std::move(host);
	Reset();
}

void Duel::Reset()
{
	m_challenged = 0;
	m_challenger = 0;
	m_dueling = false;
}

std::string Duel::Text(uint32_t id, const std::vector<std::string> &args, const std::string &fallback) const
{
	return m_host.text ? m_host.text(id, args, fallback) : fallback;
}

void Duel::Say(const std::string &text, int type) const
{
	if (m_host.chat)
		m_host.chat(text, type);
}

const ZoneSpawn *Duel::Spawn(uint16_t id) const
{
	// __EQP_IDArray: 1..4999.
	if (!m_zone || id == 0 || id > 4999)
		return nullptr;
	if (id == m_zone->MySpawnId())
		return &m_zone->Self();
	auto it = m_zone->Spawns().find(id);
	return it == m_zone->Spawns().end() ? nullptr : &it->second;
}

void Duel::DuelCommand(uint16_t target)
{
	const ZoneSpawn *spawn = Spawn(target);
	if (!spawn || spawn->npc != 0) {
		Say(Text(13248, {}, "You must target a PC to duel."), Default);
		return;
	}
	if (target == m_zone->MySpawnId()) {
		Say(Text(13249, {}, "You cannot duel yourself."), Default);
		return;
	}
	if (m_dueling) {
		if (m_challenged == target)
			Say(Text(13250, {}, "Both of you have accepted the challenge already. Fight!"), Yellow);
		else
			Say(Text(13251, {}, "You have already accepted a duel with someone else cowardly dog."), Yellow);
		return;
	}
	if (m_challenger == target) {
		m_dueling = true;
		m_challenged = target;
		Say(Text(13252, {spawn->name}, "You have accepted %1's challenge to duel to the death!"), Yellow);
		m_zone->RequestDuel(target, true);
		return;
	}
	if (m_challenger == 0) {
		m_challenged = target;
		Say(Text(13254, {spawn->name}, "You have challenged %1 to duel to the death!"), Yellow);
		m_zone->RequestDuel(target, false);
		return;
	}
	Say(Text(13253, {}, "You must decline your pending duel first before challenging another."), Yellow);
}

void Duel::DeclineCommand()
{
	if (m_dueling) {
		Say(Text(13245, {}, "You cannot decline now!  It is too late for you cowardly dog!"), Yellow);
		return;
	}
	if (m_challenger == 0) {
		Say(Text(13247, {}, "No one has challenged you to a duel for you to decline."), Default);
		return;
	}
	if (const ZoneSpawn *spawn = Spawn(m_challenger)) {
		Say(Text(13246, {spawn->name}, "You decline %1's invitation to duel."), Default);
		m_zone->DuelResponse(m_challenger, 0);
	}
	m_challenger = 0;
}

void Duel::Requested(uint16_t challenger)
{
	const ZoneSpawn *spawn = Spawn(challenger);
	if (!spawn)
		return;
	if (m_host.ignored && m_host.ignored(spawn->name)) {
		m_zone->DuelResponse(challenger, 0);
		return;
	}
	if (m_dueling) {
		if (m_challenged != challenger)
			m_zone->DuelResponse(challenger, 1);
		return;
	}
	if (m_challenged == challenger) {
		m_dueling = true;
		m_challenger = challenger;
		Say(Text(1424, {spawn->name}, "%1 has accepted your challenge to duel to the death!  Fight!"), Yellow);
		return;
	}
	if (m_challenged == 0) {
		if (m_challenger == 0) {
			m_challenger = challenger;
			Say(Text(1426, {spawn->name}, "%1 has challenged you to duel to the death!"), Yellow);
			return;
		}
		if (m_challenger == challenger)
			return;
	}
	m_zone->DuelResponse(challenger, 2);
}

void Duel::Responded(uint16_t sender, int type)
{
	const ZoneSpawn *spawn = Spawn(sender);
	if (!spawn || sender != m_challenged)
		return;
	if (type == 0)
		Say(Text(1383, {spawn->name}, "%1 has declined your challenge to duel to the death."), Yellow);
	else if (type == 1)
		Say(Text(1384, {spawn->name}, "%1 has already accepted a duel with someone else."), Yellow);
	else if (type == 2)
		Say(Text(1385, {spawn->name}, "%1 is considering a duel with someone else."), Yellow);
	m_dueling = false;
	m_challenged = 0;
}

void Duel::Despawned(uint16_t id, const std::string &name)
{
	if (id == 0 || id != m_challenged)
		return;
	if (!m_dueling) {
		Say(Text(1409, {name}, "Your challenge to duel %1 has been cancelled."), Yellow);
	}
	else {
		Say(Text(1408, {m_zone->Self().name, name, name},
				"%1 has defeated %2 in a duel to the death! %3 has fled like a cowardly dog!"),
			Broadcast);
		m_zone->DuelWon(id);
	}
	Reset();
}

void Duel::Died(uint16_t victim, uint16_t killer, const std::string &name)
{
	if (victim == 0 || victim != m_challenged)
		return;
	if (killer == m_zone->MySpawnId() && m_dueling) {
		Say(Text(12100, {m_zone->Self().name, name}, "%1 has defeated %2 in a duel to the death!"), Broadcast);
		m_zone->DuelWon(victim);
	}
	else {
		Say(Text(12101, {name, name}, "Your challenge to duel %1 has been cancelled by %2's untimely death."),
			Broadcast);
	}
	Reset();
}
