#include "raid.h"

#include "eqstr.h"
#include "game_values.h"
#include "group.h"
#include "raid_window.h"

#include <cctype>
#include <cstdlib>

namespace
{
	// UTF8_stricmp, for these names.
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

void Raid::Attach(ZoneSession &zone, const GameValues &values, Group &group, Host host)
{
	m_zone = &zone;
	m_values = &values;
	m_group = &group;
	m_host = std::move(host);
	Clear();
}

void Raid::Clear()
{
	ResetRaid();
	m_inviter.clear();
	m_invite_new = false;
}

std::string Raid::Text(uint32_t id, const std::vector<std::string> &args, const std::string &fallback) const
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

void Raid::Print(const std::string &text, int type) const
{
	if (m_host.chat)
		m_host.chat(text, type);
}

const std::string &Raid::MyName() const
{
	return m_zone->Profile().name;
}

int Raid::Find(const std::string &name) const
{
	if (m_count <= 0)
		return -1;
	for (int i = 0; i < MaxMembers; ++i)
		if (!m_members[i].name.empty() && Same(name, m_members[i].name))
			return i;
	return -1;
}

int Raid::FindGroupLeader(uint32_t group) const
{
	for (int i = 0; i < MaxMembers; ++i)
		if (m_members[i].group == group && m_members[i].group_leader)
			return i;
	return -1;
}

int Raid::Selected() const
{
	if (m_selected < 0 || m_selected >= MaxMembers || m_members[m_selected].name.empty())
		return -1;
	return m_selected;
}

std::string Raid::ClassTitle(const Member &m) const
{
	return GameValues::ClassTitle(m_values ? m_values->eqstr : nullptr, m.class_id, m.level);
}

void Raid::LevelAverageChanged() const
{
	if (m_window)
		m_window->SetLevelAverage(LevelAverage());
}

void Raid::SetLeaderName(const std::string &name)
{
	int was = Find(m_leader), is = Find(name);
	m_leader = name;
	if (m_window && was != -1)
		m_window->SetRank(m_members[was].name, m_members[was].group_leader ? "Group Leader" : "", m_members[was].group);
	if (m_window && is != -1)
		m_window->SetRank(m_members[is].name, "Raid Leader", m_members[is].group);
	UpdateOptionsWindow();
}

void Raid::UpdateOptionsWindow()
{
	if (!m_window)
		return;
	m_window->ClearLooterList();
	if (!m_leader.empty())
		m_window->AddLooterToList(m_leader);
	if (m_loot_type == 2) {
		for (int i = 0; i < MaxMembers; ++i)
			if (m_members[i].group_leader && m_used[i] && !Same(m_leader, m_members[i].name))
				m_window->AddLooterToList(m_members[i].name);
	}
	else if (m_loot_type == 3) {
		for (const auto &looter : m_looters)
			if (!looter.empty())
				m_window->AddLooterToList(looter);
	}
}

void Raid::UpdateRaidPlayerList()
{
	if (!m_window)
		return;
	for (int i = 0; i < MaxMembers; ++i) {
		if (!m_used[i])
			continue;
		const Member &m = m_members[i];
		std::string rank = m.group_leader ? "Group Leader" : "";
		if (Same(m.name, m_leader))
			rank = "Raid Leader";
		m_window->AddPlayer(rank, m.name, ClassTitle(m), std::to_string(m.level), m.group, m.class_id, m.group_leader);
	}
	m_window->SetRaidCount(m_count);
}

void Raid::UpdateClassColor(int index, uint32_t color)
{
	// The color's class, in the buttons' order.
	static const int classes[] = {8, 15, 2, 6, 14, 13, 7, 11, 3, 4, 9, 5, 10, 1, 12};
	if (index < 0 || index >= 15 || !m_window)
		return;
	m_window->SetClassColor(index, color);
	for (int i = 0; i < MaxMembers; ++i)
		if (m_used[i] && m_members[i].class_id == classes[index])
			m_window->SetPlayerClassColor(m_members[i].name, m_members[i].class_id, m_members[i].group);
}

void Raid::Select(const std::string &name)
{
	if (name.empty()) {
		if (m_selected != -1 && m_window)
			m_window->Highlight("", 0xffffffff, m_members[m_selected].name, m_members[m_selected].group);
		m_selected = -1;
		return;
	}
	int i = Find(name);
	if (i == -1 || m_members[i].name.empty())
		return;
	if (m_window) {
		if (m_selected == -1)
			m_window->Highlight(name, m_members[i].group, "", 0xffffffff);
		else
			m_window->Highlight(name, m_members[i].group, m_members[m_selected].name, m_members[m_selected].group);
	}
	m_selected = i;
	if (m_host.target)
		m_host.target(m_members[i].name);
}

bool Raid::IsRaidGroupLeader() const
{
	if (!m_zone)
		return false;
	int i = Find(MyName());
	return i != -1 && m_members[i].group_leader;
}

void Raid::ResetRaid()
{
	for (int i = 0; i < MaxMembers; ++i) {
		m_used[i] = false;
		m_members[i] = Member{};
	}
	m_leader.clear();
	for (auto &looter : m_looters)
		looter.clear();
	m_selected = -1;
	m_count = 0;
	m_leading = false;
	m_raid_id = 0;
	m_state = 1;
	m_loot_type = 1;
	m_level_sum = 0;
	UpdateOptionsWindow();
	LevelAverageChanged();
}

void Raid::ResetFromDeath()
{
	if (Invited()) {
		m_invite_new = false;
		m_inviter.clear();
		m_state = 1;
	}
}

void Raid::Died()
{
	if (InRaid()) {
		Select("");
		ResetFromDeath();
	}
}

void Raid::ZoneEvent(const ZoneSession::Event &event)
{
	using Type = ZoneSession::Event::Type;
	if (!m_zone)
		return;
	const ZoneRaid &r = event.raid;
	if (event.type == Type::RaidInvited) {
		// HandleC2SRaidMessage.
		if (r.action == 0 || r.action == 3)
			InviteArrived(r);
		else if (r.action == 0x15)
			Print(Text(static_cast<uint32_t>(r.param), {r.name}, "%1"));
		return;
	}
	if (event.type != Type::RaidUpdated)
		return;
	// HandleS2CRaidMessage.
	switch (r.action) {
	case 0:
		AddMember(r);
		break;
	case 1:
		DeleteMember(r);
		break;
	case 2: { // RenameMember: the new name at 0x84
		int i = Find(r.name);
		if (i == -1)
			break;
		m_members[i].name = r.renamed;
		Print(Text(5068, {r.name, r.renamed}, "%1's name changed to %2"));
		if (Same(m_leader, r.name))
			m_leader = r.renamed;
		if (m_window)
			m_window->Rename(r.renamed, r.name, m_members[i].group);
		break;
	}
	case 3: { // HandleS2CRaidMessage2??: into another group (CRaidWnd::ChangePosition)
		int i = Find(r.name);
		if (i == -1)
			break;
		if (m_window)
			m_window->ChangePosition(m_members[i].name, m_members[i].group, static_cast<uint32_t>(r.param),
				m_members[i].class_id, m_members[i].group_leader);
		m_members[i].group = static_cast<uint32_t>(r.param);
		break;
	}
	case 4:
		m_state = 1;
		m_leader.clear();
		break;
	case 5:
		Print(Text(5071, {}, "Your raid was disbanded."));
		ResetRaid();
		if (m_window)
			m_window->ClearPlayerList();
		break;
	case 6:
		Initialize(r);
		break;
	case 7:
		Print(Text(5072, {r.name}, "%1 was not assigned leadership."));
		break;
	case 8: // SetRaidLeader: we formed it
		SetLeaderName(MyName());
		m_state = 4;
		m_raid_id = r.raid_id;
		m_leading = true;
		Print(Text(5061, {}, "You formed a raid."));
		break;
	case 10:
		if (InRaid())
			Print(Text(5071, {}, "Your raid was disbanded."));
		ResetRaid();
		if (m_window)
			m_window->ClearPlayerList();
		break;
	case 11: { // CRaid_SetLootTypeResponse: %T1, the type's own string
		std::string type;
		if (r.param >= 1 && r.param <= 3)
			type = std::to_string(5100 + r.param);
		const char *names[] = {"raid leader only", "raid and group leaders", "leader selected looters"};
		std::string fallback = "The loot type is now - ";
		if (r.param >= 1 && r.param <= 3)
			fallback += names[r.param - 1];
		Print(Text(5100, {type}, fallback));
		break;
	}
	case 12:
	case 0x15:
		Print(Text(static_cast<uint32_t>(r.param), {r.name}, "%1"));
		break;
	case 13:
		GroupLeaderChange(r);
		break;
	case 0x14: // ChangeLeadership
		SetLeaderName(r.name);
		if (Same(r.name, MyName())) {
			Print(Text(5069, {}, "You are now the leader of the raid."));
			m_leading = true;
		}
		else {
			Print(Text(5070, {m_leader}, "%1 is now the leader of your raid."));
			m_leading = false;
		}
		break;
	case 0x16: // UpdateOptionsWindow @0049dc48
		m_loot_type = static_cast<uint32_t>(r.param);
		if (r.param == 1 || r.param == 2)
			for (auto &looter : m_looters)
				looter.clear();
		UpdateOptionsWindow();
		break;
	case 0x17: // CRaidOptionsWnd::AddLooterToList @0049dc80: into the list, then the first empty place
		if (m_window)
			m_window->AddLooterToList(r.name);
		for (auto &looter : m_looters)
			if (looter.empty()) {
				looter = r.name;
				break;
			}
		break;
	case 0x18: // AddLooterToList @0049dcc4 (a removal)
		for (auto &looter : m_looters)
			if (Same(looter, r.name))
				looter.clear();
		UpdateOptionsWindow();
		break;
	default:
		break;
	}
}

void Raid::InviteArrived(const ZoneRaid &r)
{
	Print(Text(5065, {r.name}, "%1 invites you to join a raid."));
	uint32_t why;
	if (Invited())
		why = 5066;
	else if (InRaid())
		why = 5060;
	else if (m_group && m_group->Invited())
		why = 12264;
	else {
		Print(Text(5067, {}, "To join the raid click the accept button in the raid window or type /raidaccept."));
		m_state = 2;
		m_inviter = r.name;
		if (r.action == 3)
			m_invite_new = true;
		return;
	}
	m_zone->RaidCommand(0x15, r.name, m_zone->Self().name, static_cast<int32_t>(why));
}

void Raid::MembershipChanges(const ZoneRaid &r)
{
	if (!InRaid())
		return;
	// eqgame says the old leader's name here, not the new one's.
	if (!Same(m_leader, r.leader))
		Print(Text(5070, {m_leader}, "%1 is now the leader of your raid."));
	bool kept[MaxMembers] = {};
	for (const auto &member : r.members) {
		int i = Find(member.name);
		if (i == -1)
			Print(Text(5059, {member.name}, "%1 joined the raid."));
		else
			kept[i] = true;
	}
	for (int i = 0; i < MaxMembers; ++i)
		if (m_used[i] && !kept[i])
			Print(Text(5063, {m_members[i].name}, "%1 has left the raid."));
}

void Raid::Initialize(const ZoneRaid &r)
{
	uint32_t was = m_state;
	if (m_group && m_group->Invited())
		m_group->DropInvitation();
	MembershipChanges(r);
	if (m_window)
		m_window->ClearPlayerList();
	ResetRaid();
	m_raid_id = r.raid_id;
	m_count = static_cast<int>(r.members.size());
	for (int i = 0; i < m_count && i < MaxMembers; ++i) {
		const ZoneRaidMember &in = r.members[i];
		Member &m = m_members[i];
		m.name = in.name;
		m.class_id = in.class_id;
		m.level = in.level;
		m.flag = in.flag;
		m.group_leader = in.group_leader;
		m.group = in.group;
		m_level_sum += in.level;
		m_used[i] = true;
		if (m_window)
			m_window->AddPlayer(m.group_leader ? "Group Leader" : "", m.name, ClassTitle(m), std::to_string(m.level),
				m.group, m.class_id, m.group_leader);
	}
	if (m_window)
		m_window->SetRaidCount(m_count);
	m_loot_type = r.loot_type;
	for (size_t i = 0; i < r.looters.size() && i < MaxLooters; ++i)
		m_looters[i] = r.looters[i];
	UpdateOptionsWindow();
	LevelAverageChanged();
	SetLeaderName(r.leader);
	m_state = 4;
	if (Same(m_leader, MyName()))
		m_leading = true;
	int leader = Find(m_leader);
	if (leader != -1 && m_window)
		m_window->SetRank(m_members[leader].name, "Raid Leader", m_members[leader].group);
	if (!(was & 4))
		Print(Text(5083, {}, "You have joined the raid."));
}

void Raid::AddMember(const ZoneRaid &r)
{
	if (m_count == MaxMembers)
		return;
	int slot = -1;
	for (int i = 0; i < MaxMembers && slot == -1; ++i)
		if (!m_used[i])
			slot = i;
	if (slot == -1)
		return; // "CRaid::AddRaidMember - FindOpenIndex failed"
	Member &m = m_members[slot];
	m = Member{};
	m.name = r.name;
	m.group_leader = r.group_leader;
	m.group = static_cast<uint32_t>(r.param);
	m.class_id = r.class_id;
	m.level = r.level;
	++m_count;
	m_used[slot] = true;
	m_level_sum += r.level;
	LevelAverageChanged();
	if (m_window) {
		m_window->AddPlayer(m.group_leader ? "Group Leader" : "", m.name, ClassTitle(m), std::to_string(m.level), m.group,
			m.class_id, m.group_leader);
		m_window->SetRaidCount(m_count);
	}
	Print(Text(5059, {m.name}, "%1 joined the raid."));
}

void Raid::DeleteMember(const ZoneRaid &r)
{
	if (Same(r.name, MyName())) {
		ResetRaid();
		if (m_window)
			m_window->ClearPlayerList();
		Print(Text(5062, {}, "You were removed from the raid."));
		return;
	}
	int i = Find(r.name);
	if (i == -1)
		return;
	m_used[i] = false;
	--m_count;
	if (m_window) {
		m_window->RemovePlayer(m_members[i].name, m_members[i].group);
		m_window->SetRaidCount(m_count);
	}
	m_level_sum -= m_members[i].level;
	LevelAverageChanged();
	m_members[i] = Member{};
	Print(Text(5063, {r.name}, "%1 has left the raid."));
	UpdateOptionsWindow();
}

void Raid::GroupLeaderChange(const ZoneRaid &r)
{
	uint32_t group = static_cast<uint32_t>(r.param);
	int i = Find(r.name);
	if (i == -1)
		return;
	int was = FindGroupLeader(group);
	m_members[i].group_leader = true;
	if (m_window)
		m_window->ChangeGroupLeader(m_members[i].name, group, was != -1 ? &m_members[was].name : nullptr);
	int leader = Find(m_leader);
	if (was != -1) {
		m_members[was].group_leader = false; // eqgame's order: the same member twice ends up not leading
		if (leader != was && m_window)
			m_window->SetRank(m_members[was].name, "", group);
	}
	if (leader != i && m_window)
		m_window->SetRank(m_members[i].name, "Group Leader", group);
}

void Raid::Invite()
{
	if (!m_zone || !m_values || Invited())
		return;
	uint32_t id;
	const char *text;
	const ZoneSpawn *target = m_values->Target();
	if (InRaid() && !Same(m_leader, MyName())) {
		id = 5073;
		text = "Only the leader of the raid can invite someone.";
	}
	else if (!target) {
		id = 5074;
		text = "To invite someone to your raid, you must first target them.";
	}
	else if (target->npc != 0) {
		id = 5075;
		text = "Only player characters can be in a raid.";
	}
	else if (target->id == m_zone->MySpawnId()) {
		id = 5076;
		text = "You cannot invite yourself to the raid.";
	}
	else if (m_host.other_team && m_host.other_team(*target)) {
		id = 395;
		text = "You can only invite those players on your deity team.";
	}
	else if (Find(target->name) != -1) {
		id = 5077;
		text = "That person is already in your raid.";
	}
	else if (m_count >= MaxMembers) {
		id = 5048;
		text = "The raid is full.";
	}
	else {
		bool grouped = false;
		if (m_group)
			for (int i = 0; i < Group::Members; ++i)
				grouped |= !m_group->Member(i).empty() && m_group->Member(i) == target->name;
		if (!grouped) {
			m_zone->RaidCommand((m_state & 1) ? 3 : 0, target->name, m_zone->Self().name, 0);
			Print(Text(5064, {target->name}, "You invite %1 to join your raid."));
			return;
		}
		id = 12272;
		text = "That person is already in your party.";
	}
	Print(Text(id, {}, text));
}

void Raid::Respond(bool accept)
{
	if (!m_zone || !Invited())
		return;
	if (!accept) {
		Print(Text(5080, {m_inviter}, "You declined the invitation to join %1's raid."));
		m_state = 1;
		m_zone->RaidCommand(0x15, m_inviter, MyName(), 5081);
		m_inviter.clear();
		return;
	}
	// EQPlayer_get_player: the inviter here in the zone.
	bool here = false;
	for (const auto &[id, spawn] : m_zone->Spawns())
		here |= Same(spawn.name, m_inviter);
	if (!here) {
		Print(Text(5078, {m_inviter}, "You cannot join the raid because %1 is either linkdead or is not in the zone."), 13);
		m_inviter.clear();
		m_state = 1;
		return;
	}
	Print(Text(5079, {m_inviter}, "You notify %1 that you agree to join the raid."));
	uint32_t action = 1;
	if (m_invite_new) {
		m_invite_new = false;
		action = 4;
	}
	m_zone->RaidCommand(action, m_inviter, MyName(), 0);
	m_inviter.clear();
}

void Raid::Disband()
{
	if (!m_zone)
		return;
	int i = m_leading ? Selected() : -1;
	if (i == -1) {
		m_zone->RaidCommand(5, MyName(), MyName(), 0);
		return;
	}
	m_zone->RaidCommand(5, MyName(), m_members[i].name, 0);
	if (m_window) // UpdateRaidTargetHighlight(the member, their group, none): theirs red as they go
		m_window->Highlight(m_members[i].name, m_members[i].group, "", 0xffffffff);
	m_selected = -1;
}

void Raid::MakeLeader(const std::string &name)
{
	if (!m_zone || !InRaid() || !m_leading)
		return;
	if (!IsRaidMember(name)) {
		Print(Text(5082, {name}, "%1 is not in your raid."));
		return;
	}
	m_zone->RaidCommand(0x14, MyName(), name, 0);
}

void Raid::MakeSelectedLeader()
{
	if (!m_zone || !InRaid() || !m_leading)
		return;
	int i = Selected();
	if (i != -1)
		m_zone->RaidCommand(0x14, MyName(), m_members[i].name, 0);
}

void Raid::SetLootType(const std::string &arg)
{
	if (!m_zone || !InRaid())
		return;
	if (m_leading && arg.size() == 1 && std::isalnum(static_cast<unsigned char>(arg[0]))) {
		int type = std::atoi(arg.c_str());
		if (type < 1 || type > 3 || static_cast<uint32_t>(type) == m_loot_type)
			return;
		m_loot_type = static_cast<uint32_t>(type);
		m_zone->RaidCommand(0x16, MyName(), MyName(), type);
		return;
	}
	Print(Text(5094, {}, "To set the loot type enter '/setloottype arg', where arg is 1(Leader only), 2(leader and group "
	                     "leaders only), or 3(leader selected looters).  If you chose leader selected looters, use the "
	                     "/addraidlooters to specify who in the raid can loot.  You must be the raid leader to set the loot type."));
}

void Raid::AddLooter()
{
	if (!m_zone || !InRaid())
		return;
	int i = Selected();
	if (!m_leading || m_loot_type != 3 || i == -1) {
		Print(Text(5095, {}, "To add a looter, you must target someone in your raid and you must be the raid leader.  The loot "
		                     "type must also be set to leader chooses."));
		return;
	}
	for (const auto &looter : m_looters)
		if (!looter.empty() && Same(looter, m_members[i].name))
			return; // IsPlayerInRaidLooterList
	m_zone->RaidCommand(0x17, m_leader, m_members[i].name, 0);
}

void Raid::RemoveLooter()
{
	if (!m_zone || !InRaid())
		return;
	int i = Selected();
	if (!m_leading || m_loot_type != 3 || i == -1) {
		Print(Text(5096, {}, "To remove a looter, you must target someone in your raid and you must be the raid leader."));
		return;
	}
	m_zone->RaidCommand(0x18, m_leader, m_members[i].name, 0);
}
