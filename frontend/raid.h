#pragma once

// eqgame's raid, CRaid (instCRaid, made by InitializeRaidInfo @005240bc; CRaid::CRaid @0049cf43):
//   - its state (+0x39b4): 1 none, 2 invited (IsRaidLeader @0049d26c, misnamed: it tests this), 4 in a
//     raid (IsInRaid @0049d260); the leader's name (+0x3934), our own leadership (+0x39b9, what
//     CRaidWnd::IsRaidLeader reads), who invited us (+0x3974) and whether to a new raid (+0x39b8),
//     the raid's id (+0x392c), the member selected in the raid window (+0x39bc, -1 none), the loot
//     type (+0x39c0: 1 the leader, 2 and the group leaders, 3 and the ones the leader picks) and
//     those looters (+0x39c4, nine names), the levels added up (+0x3c04);
//   - 72 members (0x48 used flags at +0, 0xd0 bytes each from +0x48: the name, the level and class
//     title as text for the window, the class, a flag, group leader, the group 0..9 or -1);
//   - HandleC2SRaidMessage (@0049d1fa), OP_RaidInvite: 0 and 3 an invitation (HandleCreateInviteRaid
//     @0049e54c: "%1 invites you to join a raid." 5065, then 5067 and it's kept, or back with 0x15
//     and why: 5066 already invited, 5060 in a raid, 12264 asked to a group), 0x15 why ours came to
//     nothing (the eqstr id at 0x84, the name at 0x44);
//   - HandleS2CRaidMessage (@0049cfcf), OP_RaidUpdate by its action: 0 a member joined
//     (AddRaidMember, 5059), 1 one left (DeleteRaidMembe: 5063, or us 5062 and it's over), 2 a
//     rename (RenameMember, 5068), 3 a member moved to a group (HandleS2CRaidMessage2??), 4 the
//     invitation dropped, 5 disbanded (5071), 6 the whole raid (InitializeRaid: who came and went
//     since, ProcessRaidMembershipChanges; "You have joined the raid." 5083), 7 5072, 8 we formed it
//     (SetRaidLeader, 5061), 10 none (5071 if we were in one), 11 the loot type said
//     (CRaid_SetLootTypeResponse, 5100), 12 and 0x15 an eqstr id said, 13 a group's new leader
//     (RaidGroupLeaderChange @0049dae6), 0x14 the raid's new leader (ChangeLeadership: 5069, 5070),
//     0x16 the loot type set (UpdateOptionsWindow @0049dc48), 0x17 and 0x18 a looter added or taken
//     off;
//   - what we send (CRaid_RaidInvite @0049e9b3, OP_RaidInvite): CreateInviteRaid (/raidinvite: only
//     the leader, a player targeted who isn't us, on the deity team servers our team, not one in the
//     raid or our group, room for them; 3 to a new raid, 0 to ours; 5064), SendInviteResponse
//     (/raidaccept: the inviter in the zone, 5079, 4 (1 for an invitation to a raid that was there);
//     /raiddecline: 5080 and 0x15 with 5081), RemoveRaidMember (/raiddisband: the leader's selected
//     member, or us; 5), SendLeadershipChange (/makeraidleader: 0x14), SetLootType (/setloottype 1..3:
//     0x16, else 5094), AddRaidLooter and RemoveRaidLooter (/addraidlooter, /removeraidlooter: the
//     leader's selected member with loot type 3; 0x17, 0x18, else 5095, 5096); CRaid_SendRaidChat
//     (/rsay: 5108 out of a raid, else "You tell your raid, '%1'" 5109 and channel 15) is
//     chat_commands.cpp's;
//   - ResetRaid (@0049e76b), ResetFromDeath (@0049d277: an invitation dropped, as we die or join a
//     group), IsRaidGroupLeader (@0049d2a2), IsRaidMember (@0049e71f).
// Not ours yet: the raid window (CRaidWnd) and raid options window (CRaidOptionsWnd) that the
// state feeds, SetTargetRaidPlayer (@0049d2c2, a click on a member there, the selection the
// leader's commands act on) and /raidwindow; on camping out eqgame's last RemoveRaidMember
// (ProcessGameLoop's 0xca, after our session's closed).

#include "zone_session.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class GameValues;
class Group;

class Raid
{
public:
	static constexpr int MaxMembers = 72, MaxLooters = 9;

	struct Member
	{
		std::string name;
		uint16_t class_id = 0;
		uint8_t level = 0;
		bool flag = false;            // +0xca, the byte before group leader in InitializeRaid's list
		bool group_leader = false;    // +0xcb
		uint32_t group = 0xffffffff;  // +0xcc: 0..9, or -1 in none
	};

	struct Host
	{
		std::function<void(const std::string &, int)> chat; // a message, in a chat color type
		std::function<bool(const ZoneSpawn &)> other_team;   // ruleset 4's EQPlayer::NotOnSameDeity
	};

	void Attach(ZoneSession &zone, const GameValues &values, Group &group, Host host);
	void Clear(); // out of the game: ResetRaid, and no invitation

	void ZoneEvent(const ZoneSession::Event &event);

	void Invite();                          // CreateInviteRaid
	void Respond(bool accept);              // SendInviteResponse
	void Disband();                         // RemoveRaidMember
	void MakeLeader(const std::string &name); // SendLeadershipChange
	void SetLootType(const std::string &arg); // SetLootType
	void AddLooter();                       // AddRaidLooter
	void RemoveLooter();                    // RemoveRaidLooter
	void Died();                            // EQ_PC::PrepareForRepop's: the selection, ResetFromDeath
	void ResetFromDeath();

	bool InRaid() const { return (m_state & 4) != 0; }
	bool Invited() const { return (m_state & 2) != 0; } // eqgame's CRaid::IsRaidLeader
	bool Leading() const { return m_leading; }           // CRaidWnd::IsRaidLeader
	bool IsRaidGroupLeader() const;
	bool IsRaidMember(const std::string &name) const { return Find(name) != -1; }
	const std::string &Leader() const { return m_leader; }
	int Count() const { return m_count; }
	bool Used(int index) const { return index >= 0 && index < MaxMembers && m_used[index]; }
	const Member &MemberAt(int index) const { return m_members[index]; }
	uint32_t LootType() const { return m_loot_type; }
	const std::string &Looter(int index) const { return m_looters[index]; }
	int LevelAverage() const { return m_count > 0 ? static_cast<int>(m_level_sum / m_count) : 0; } // UpdateLevelAverage

private:
	std::string Text(uint32_t id, const std::vector<std::string> &args, const std::string &fallback) const;
	void Print(const std::string &text, int type = 0x111) const;
	const std::string &MyName() const;
	int Find(const std::string &name) const;            // FindRaidMemberIndex
	int FindGroupLeader(uint32_t group) const;          // FindRaidMemberByGroupIndex
	int Selected() const;                               // +0x39bc, when it's a member still
	void ResetRaid();
	void InviteArrived(const ZoneRaid &r);              // HandleCreateInviteRaid
	void Initialize(const ZoneRaid &r);                 // InitializeRaid
	void MembershipChanges(const ZoneRaid &r);          // ProcessRaidMembershipChanges
	void AddMember(const ZoneRaid &r);                  // AddRaidMember
	void DeleteMember(const ZoneRaid &r);               // DeleteRaidMembe
	void GroupLeaderChange(const ZoneRaid &r);          // RaidGroupLeaderChange @0049dae6

	ZoneSession *m_zone = nullptr;
	const GameValues *m_values = nullptr;
	Group *m_group = nullptr;
	Host m_host;

	bool m_used[MaxMembers] = {};
	Member m_members[MaxMembers];
	int m_count = 0;
	uint32_t m_raid_id = 0;
	std::string m_leader;
	std::string m_inviter;
	uint32_t m_state = 1;
	bool m_invite_new = false;
	bool m_leading = false;
	int m_selected = -1;
	uint32_t m_loot_type = 1;
	std::string m_looters[MaxLooters];
	uint32_t m_level_sum = 0;
};
