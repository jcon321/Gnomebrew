#include "group.h"

#include "chat_text.h"
#include "eqstr.h"
#include "game_values.h"
#include "raid.h"

#include <cctype>

namespace
{
	// stricmp (and UTF8_stricmp, for these names).
	bool Same(const std::string &a, const std::string &b)
	{
		if (a.size() != b.size())
			return false;
		for (size_t i = 0; i < a.size(); ++i)
			if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
				return false;
		return true;
	}
}

void Group::Attach(ZoneSession &zone, const GameValues &values, Host host)
{
	m_zone = &zone;
	m_values = &values;
	m_host = std::move(host);
	Clear();
}

void Group::Clear()
{
	ClearMembers();
	DropInvitation();
	m_selected = -1;
}

void Group::ClearMembers()
{
	for (auto &member : m_members)
		member.clear();
	m_leader.clear();
}

void Group::DropInvitation()
{
	m_invited = false;
	m_inviter.clear();
	m_invite_group.clear();
}

std::string Group::Text(uint32_t id, const std::vector<std::string> &args, const std::string &fallback) const
{
	const EqStr *eqstr = m_values ? m_values->eqstr : nullptr;
	if (eqstr && eqstr->Has(id))
		return eqstr->Format(id, args);
	std::string out = fallback;
	for (size_t i = 0; i < args.size(); ++i) {
		std::string key = "%" + std::to_string(i + 1);
		size_t at = out.find(key);
		if (at != std::string::npos)
			out.replace(at, key.size(), args[i]);
	}
	return out;
}

void Group::Say(const std::string &text, int type) const
{
	if (m_host.chat)
		m_host.chat(text, type);
}

bool Group::Leading() const
{
	return m_zone && Same(m_leader, m_zone->Profile().name);
}

const ZoneSpawn *Group::MemberSpawn(int index) const
{
	// EQPlayer_get_player: the player of that name in the zone.
	if (!m_zone || index < 0 || index >= Members || m_members[index].empty())
		return nullptr;
	for (const auto &[id, spawn] : m_zone->Spawns())
		if (spawn.npc == 0 && Same(spawn.name, m_members[index]))
			return &spawn;
	return nullptr;
}

const ZoneSpawn *Group::PetOf(const ZoneSpawn &owner) const
{
	if (!m_zone || owner.id == 0)
		return nullptr;
	for (const auto &[id, spawn] : m_zone->Spawns())
		if (spawn.pet_owner == owner.id && spawn.npc < 2)
			return &spawn;
	return nullptr;
}

bool Group::IsAGroupMember(const std::string &name) const
{
	if (!m_zone || name.empty())
		return false;
	const ZoneProfile &me = m_zone->Profile();
	if (Same(name, me.name))
		return true;
	for (const auto &member : me.group_members)
		if (Same(name, member))
			return true;
	return false;
}

void Group::ProfileArrived()
{
	if (!m_zone)
		return;
	const ZoneProfile &me = m_zone->Profile();
	auto other = [&](const std::string &name) { return !name.empty() && !Same(name, me.name); };
	bool listed = false;
	for (const auto &name : me.group_members)
		listed |= other(name);
	std::string old[Members];
	bool had = false;
	for (int i = 0; i < Members; ++i) {
		old[i] = m_members[i];
		had |= other(old[i]);
		m_members[i].clear();
	}
	auto fill = [&] {
		int n = 0;
		for (const auto &name : me.group_members)
			if (other(name) && n < Members)
				m_members[n++] = name;
	};
	if (!had) {
		if (!listed)
			return;
		// Into the zone grouped: who leads isn't known until the zone says (OP_GroupUpdate 8).
		if (m_leader.empty())
			m_leader = "PENDING";
		Say(m_leader != me.name ? Text(12004, {}, "You have joined the group.") : Text(12003, {}, "You have formed the group."));
		if (m_host.lfg_off)
			m_host.lfg_off();
		fill();
		return;
	}
	int lead = 0;
	if (!m_leader.empty()) {
		for (const auto &name : me.group_members)
			if (Same(m_leader, name))
				lead = 1;
		if (Same(m_leader, me.name))
			lead = 2;
	}
	if (!listed) {
		Say(lead != 2 ? Text(12001, {}, "You have been removed from the group.") : Text(12002, {}, "Your group has been disbanded."));
		m_leader.clear();
		return;
	}
	fill();
	for (const auto &name : m_members) {
		bool before = false;
		for (const auto &was : old)
			before |= !name.empty() && Same(name, was);
		if (!name.empty() && !before)
			Say(Text(1399, {name}, "%1 has joined the group."));
	}
	for (const auto &was : old) {
		bool still = false;
		for (const auto &name : m_members)
			still |= !was.empty() && Same(name, was);
		if (!was.empty() && !still)
			Say(Text(12005, {was}, "%1 has left the group."));
	}
}

void Group::ZoneEvent(const ZoneSession::Event &event)
{
	using Type = ZoneSession::Event::Type;
	if (!m_zone)
		return;
	const ZoneProfile &me = m_zone->Profile();
	const ZoneGroup &g = event.group;
	switch (event.type) {
	case Type::GroupInvited: {
		// CEverQuest_doInvite.
		const std::string &inviter = g.names[0];
		Say(g.second.empty() ? Text(12280, {inviter}, "%1 invites you to join a group.")
		                     : Text(12279, {inviter, g.second}, "%1 invites you to join %2."));
		uint8_t why = 0;
		if (m_invited)
			why = 1;
		else if (!m_leader.empty())
			why = 2;
		else if (m_raid && m_raid->Invited())
			why = 4;
		else if (m_raid && m_raid->InRaid() && !(m_raid->IsRaidMember(inviter) && !m_raid->IsRaidGroupLeader()))
			why = 5;
		if (why != 0) {
			m_zone->GroupCancelInvite(inviter, why);
			break;
		}
		Say(Text(12281, {}, "To join the group, click on the 'FOLLOW' option, or 'DISBAND' to cancel."));
		m_invited = true;
		m_inviter = inviter;
		m_invite_group = g.second;
		break;
	}
	case Type::GroupInviteCancelled: {
		// CEverQuest_doUnInvite: about the one we invited (the second name).
		static const struct
		{
			int why;
			uint32_t id;
			const char *text;
		} reasons[] = {
			{1, 12264, "%1 is currently considering joining another group."},
			{2, 12265, "%1 is already in another group."},
			{3, 12266, "%1 rejects your offer to join the group."},
			{4, 5086, "%1 is considering joining another raid."},
			{5, 5088, "%1 rejects your invite because they are in a raid and you are not in theirs, or they are a raid group leader"},
		};
		for (const auto &reason : reasons)
			if (reason.why == g.param)
				Say(Text(reason.id, {g.second}, reason.text));
		break;
	}
	case Type::GroupFollowed:
		// CEverQuest_InviteOk.
		DropInvitation();
		break;
	case Type::GroupUpdated:
		switch (g.action) {
		case 0: // joined: into the first free place
			for (auto &member : m_members)
				if (member.empty()) {
					member = g.names[0];
					Say(Text(1399, {member}, "%1 has joined the group."));
					break;
				}
			break;
		case 1: { // left
			if (Same(g.names[0], me.name)) {
				ClearMembers();
				Say(Text(12001, {}, "You have been removed from the group."));
				break;
			}
			for (int i = 0; i < Members; ++i)
				if (Same(g.names[0], m_members[i])) {
					Say(Text(12005, {g.names[0]}, "%1 has left the group."));
					for (int j = i; j + 1 < Members; ++j)
						m_members[j] = m_members[j + 1];
					m_members[Members - 1].clear();
					break;
				}
			break;
		}
		case 3: // a name changed
			if (Same(m_leader, me.name)) {
				m_leader = g.second; // eqgame's: the leader's name becomes the new one, whoever it was
				break;
			}
			for (auto &member : m_members)
				if (Same(g.names[0], member)) {
					member = g.second;
					Say(Text(5039, {g.names[0], g.second}, "A member of your group has changed their name from %1 to %2"));
					if (Same(m_leader, g.names[0]))
						m_leader = g.second;
				}
			break;
		case 5:
		case 6:
			ClearMembers();
			DropInvitation();
			m_selected = -1;
			if (g.action == 6)
				Say(Text(12002, {}, "Your group has been disbanded."));
			else if (g.param != 0)
				Say(Text(static_cast<uint32_t>(g.param), {}, "You have been removed from the group."));
			break;
		case 7: // the whole group
			for (int i = 0; i < Members; ++i)
				m_members[i] = g.names[i];
			m_leader = g.leader;
			Say(Text(12004, {}, "You have joined the group."));
			if (m_raid)
				m_raid->ResetFromDeath();
			break;
		case 8: // a new leader
			m_leader = g.names[0];
			if (Same(g.names[0], me.name))
				Say(Text(5040, {}, "You are now the leader of your group."));
			else
				Say(Text(5041, {m_leader}, "%1 is now the leader of your group."));
			break;
		case 9: // we formed it
			m_leader = g.names[0];
			Say(Text(12003, {}, "You have formed the group."));
			if (m_host.lfg_off)
				m_host.lfg_off();
			DropInvitation();
			if (m_raid)
				m_raid->ResetFromDeath();
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

void Group::Invite()
{
	if (!m_zone || !m_values)
		return;
	const ZoneProfile &me = m_zone->Profile();
	if (!m_leader.empty() && m_leader != me.name) {
		Say(Text(12267, {}, "You cannot invite someone to join your group, only your leader may do so."));
		return;
	}
	const ZoneSpawn *target = m_values->Target();
	if (!target) {
		Say(Text(12268, {}, "You must target a player first to invite to join your group."));
		return;
	}
	if (target->npc != 0) {
		Say(Text(12269, {}, "You can only group with player characters."));
		return;
	}
	if (target->id == m_zone->MySpawnId()) {
		Say(Text(12270, {}, "You cannot invite yourself."));
		return;
	}
	if (m_host.other_team && m_host.other_team(*target)) {
		Say(Text(12271, {}, "You can only invite those players on your deity team."));
		return;
	}
	if (m_raid && m_raid->InRaid() && !m_raid->IsRaidGroupLeader()) {
		Say(Text(5087, {}, "You cannot create a group since you are in a raid and are not a group leader of the raid."));
		return;
	}
	for (const auto &member : m_members)
		if (!member.empty() && member == target->name) {
			Say(Text(12272, {}, "That person is already in your party."));
			return;
		}
	for (const auto &member : m_members)
		if (member.empty()) {
			m_zone->GroupInvite(target->name, !m_leader.empty());
			Say(Text(12273, {target->name}, "You invite %1 to join your group."));
			return;
		}
	Say(Text(12275, {}, "Your party is already full. DISBAND someone to make room for others."));
}

void Group::Accept()
{
	if (!m_zone)
		return;
	// The inviter has to be a player here in the zone.
	const ZoneSpawn *inviter = nullptr;
	for (const auto &[id, spawn] : m_zone->Spawns())
		if (Same(spawn.name, m_inviter))
			inviter = &spawn;
	if (!inviter || inviter->npc != 0) {
		Say(Text(12282, {m_inviter}, "You cannot join the group because %1 is either link-dead or is not in this zone."), 13);
		m_invited = false;
	}
	else {
		Say(m_invite_group.empty() ? Text(12283, {m_inviter}, "You notify %1 that you agree to join the group.")
		                           : Text(12284, {m_inviter, m_invite_group}, "You notify %1 that you agree to join %2."));
		m_invited = false;
		m_leader = m_inviter;
		if (m_host.lfg_off)
			m_host.lfg_off();
		// OP_GroupFollow; eqgame sends 0x4042 for an invitation that came as OP_GroupInvite2, but
		// the zone only ever hands invitations on as OP_GroupInvite.
		m_zone->GroupFollow(m_inviter);
	}
	m_inviter.clear();
	m_invite_group.clear();
}

void Group::Disband()
{
	if (!m_zone)
		return;
	const ZoneProfile &me = m_zone->Profile();
	if (Same(m_leader, me.name)) {
		if (m_selected >= 0 && m_selected < Members && !m_members[m_selected].empty()) {
			std::string name = m_members[m_selected];
			m_zone->GroupDisband(name);
			Say(Text(12287, {name}, "You remove %1 from the party."));
			m_selected = -1;
			return;
		}
	}
	else if (m_invited) {
		Say(Text(12289, {m_inviter}, "You cancel the invitation to join %1's group."));
		m_zone->GroupCancelInvite(m_inviter, 3);
		DropInvitation();
		return;
	}
	m_zone->GroupDisband(me.name);
}

void Group::MakeLeader(const std::string &name)
{
	if (!m_zone || !m_values || m_leader.empty() || !Same(m_leader, m_zone->Profile().name))
		return;
	std::string who = name;
	const ZoneSpawn *target = m_values->Target();
	if (name.empty() && target) {
		for (const auto &member : m_members)
			if (Same(target->name, member)) {
				m_zone->GroupMakeLeader(target->name);
				return;
			}
		who = target->name;
	}
	else
		for (const auto &member : m_members)
			if (Same(name, member)) {
				// EQUtil_FormatCharacterName: as names are written, a capital then the rest small.
				std::string formatted = name;
				for (size_t i = 0; i < formatted.size(); ++i)
					formatted[i] = static_cast<char>(i == 0 ? std::toupper(static_cast<unsigned char>(formatted[i]))
					                                        : std::tolower(static_cast<unsigned char>(formatted[i])));
				m_zone->GroupMakeLeader(formatted);
				return;
			}
	Say(Text(5042, {who}, "%1 is not in your group."));
}

void Group::TargetMember(int index)
{
	if (index < 0 || index >= Members || m_members[index].empty() || !m_values)
		return;
	m_selected = index;
	const ZoneSpawn *member = MemberSpawn(index);
	if (!member) {
		m_host.set_target(0);
		return;
	}
	if (m_values->target == member->id)
		if (const ZoneSpawn *pet = PetOf(*member)) {
			m_host.set_target(pet->id);
			return;
		}
	m_host.set_target(member->id);
}

void Group::TargetByGauge(int eq_type)
{
	if (!m_zone || !m_values)
		return;
	if (eq_type == 1) {
		m_host.set_target(m_zone->MySpawnId());
		return;
	}
	if (eq_type >= 11 && eq_type <= 15) {
		const ZoneSpawn *member = MemberSpawn(eq_type - 11);
		m_host.set_target(member ? member->id : 0);
		if (member)
			m_selected = eq_type - 11;
		return;
	}
	const ZoneSpawn *owner = nullptr;
	if (eq_type == 16)
		owner = &m_zone->Self();
	else if (eq_type >= 17 && eq_type <= 21) {
		if (m_members[eq_type - 17].empty())
			return;
		owner = MemberSpawn(eq_type - 17);
	}
	if (!owner)
		return;
	ZoneSpawn me_as_owner;
	if (owner == &m_zone->Self()) {
		me_as_owner = m_zone->Self();
		me_as_owner.id = m_zone->MySpawnId();
		owner = &me_as_owner;
	}
	if (const ZoneSpawn *pet = PetOf(*owner))
		m_host.set_target(pet->id);
}

void Group::Clicked(uint16_t spawn)
{
	for (int i = 0; i < Members; ++i) {
		const ZoneSpawn *member = MemberSpawn(i);
		if (member && member->id == spawn)
			m_selected = i;
	}
}
