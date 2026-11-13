#pragma once

// eqgame's group, as the client keeps it (the zone owns the group itself):
//   - the profile's groupMembers (EQ_PC+0x1390: six names, ours among them), kept by ZoneSession
//     as msg_wldgroup2 keeps them (OP_GroupUpdate's join, leave, rename, disband and update);
//   - the group window's five others (@007912b5, __GroupCount, pinstGroup: the spawn of each
//     that's in the zone, looked up by name here) and __GroupLeader (@0079140c);
//   - CheckForGroupChanges (@004dc9be), after each OP_PlayerProfile: the others from the profile's
//     list, "You have formed the group." (12003) or "You have joined the group." (12004) coming into a
//     zone grouped (the leader "PENDING" until the zone says), the ones gone ("%1 has left the
//     group.", 12005) and come ("%1 has joined the group.", 1399) since the last zone, or
//     "You have been removed from the group." (12001) / "Your group has been disbanded." (12002)
//     when the list's empty;
//   - msg_wldgroup (@004dc47c), OP_GroupUpdate by its action: 0 someone joined (1399), 1 someone
//     left (12005; the rest move up) or we were removed (12001), 3 a member's name changed (5039),
//     5 and 6 the group's gone (5's eqstr id, 6's 12002), 7 the whole list and leader (12004), 8 a
//     new leader (5040 ours, 5041 another's), 9 we formed it (12003);
//   - an invitation: CEverQuest_doInvite (@005304e5), OP_GroupInvite: "%1 invites you to join a
//     group." (12280, or 12279 with a group's name), then with none pending and no group "To join
//     the group, click on the 'FOLLOW' option, or 'DISBAND' to cancel." (12281) and the invitation
//     kept (EQPlayer::SetInvited: the group window's Follow and Decline); else OP_GroupCancelInvite
//     back with 1 (considering another) or 2 (in a group). CEverQuest_doUnInvite (@0053020f),
//     OP_GroupCancelInvite: why ours came to nothing (12264..12266, 5086, 5088). CEverQuest_InviteOk
//     (@005301ea), OP_GroupFollow: the invitation's over;
//   - CEverQuest_Invite (@005302c1), /invite and the Invite buttons: only the leader or someone not
//     in a group (12267), a player targeted (12268, 12269), not us (12270), on the deity team
//     servers our team (12271), not one already in (12272); OP_GroupInvite (OP_GroupInvite2 as the
//     leader), "You invite %1 to join your group." (12273), or a full group (12275);
//   - CEverQuest::AcceptGroupInvite (@0053069c), Follow and /invite while invited: the inviter a
//     player in the zone, else 12282 (red) and the invitation's dropped; "You notify %1 that you
//     agree to join the group." (12283, 12284 with a group's name), the inviter our leader, and
//     OP_GroupFollow;
//   - CEverQuest_Disband (@00530829), /disband, Disband and Decline: the leader with a member
//     selected (ActorInfo+0x278: Target party member, a click on them in the world or their
//     gauge) removes them (OP_GroupDisband with their name, "You remove %1 from the party."
//     12287); invited, "You cancel the invitation to join %1's group." (12289) and
//     OP_GroupCancelInvite 3; otherwise OP_GroupDisband with our name (we leave);
//   - Target party member 1..5 (ExecuteCmd 0x17..0x1b) and setTargetBasedOnIndex (@00436474, a
//     click on a gauge): the member, a second press their pet; a member not in the zone, no target.
//   - the raid's say (frontend/raid.*): CEverQuest_Invite only as one of its group leaders (5087),
//     doInvite's 4 (asked to a raid) and 5 (in a raid, the inviter not in it or we lead a group of
//     it), and msg_wldgroup's 7 and 9 dropping a raid invitation (CRaid::ResetFromDeath).
//   - ProcessGameLoop's 0xca, a camp's end: the selection and invitation gone and CEverQuest_Disband
//     (FrontEnd's LoggedOut, before the zone session closes).

#include "zone_session.h"

#include <cstdint>
#include <functional>
#include <array>
#include <string>

class GameValues;
class Raid;

class Group
{
public:
	static constexpr int Members = 5; // the others

	struct Host
	{
		std::function<void(const std::string &, int)> chat; // a message, in a chat color type
		std::function<void(uint16_t)> set_target;            // pinstTarget (0 none)
		std::function<bool(const ZoneSpawn &)> other_team;   // ruleset 4's EQPlayer::NotOnSameDeity
		std::function<void()> lfg_off;                       // _lfg(us, "off") on joining
	};

	void Attach(ZoneSession &zone, const GameValues &values, Host host);
	void Clear(); // out of the game: no group, no invitation
	void SetRaid(Raid *raid) { m_raid = raid; }

	void ProfileArrived(); // CheckForGroupChanges
	void ZoneEvent(const ZoneSession::Event &event);

	void Invite();  // CEverQuest_Invite
	void Accept();  // CEverQuest::AcceptGroupInvite
	void Disband(); // CEverQuest_Disband
	// /makeleader (@004f397d): the leader names a member (or targets one) to lead; otherwise
	// "%1 is not in your group." (5042).
	void MakeLeader(const std::string &name);
	// ExecuteCmd 0x17..0x1b: member 0..4, and their pet when they're the target already.
	void TargetMember(int index);
	// setTargetBasedOnIndex: a gauge's EQType clicked (1 us, 11..15 a member, 16 our pet, 17..21 a
	// member's pet).
	void TargetByGauge(int eq_type);
	// EQ_3DView_MouseUp: a click on a member in the world selects them for the leader's Disband.
	void Clicked(uint16_t spawn);
	void Unselect() { m_selected = -1; } // Target myself (ExecuteCmd 22)

	bool Invited() const { return m_invited; }          // EQPlayer::IsInvited
	bool InGroup() const { return !m_leader.empty(); }  // __GroupLeader set
	const std::string &Leader() const { return m_leader; }
	bool Leading() const;                               // __GroupLeader is us
	const std::string &Member(int index) const { return m_members[index]; }
	const ZoneSpawn *MemberSpawn(int index) const;      // pinstGroup: in the zone, or null
	// CEverQuest_ReportDeath_ClearOutRentables: pinstGroup emptied, so no member has a spawn until
	// it's linked again: its spawn coming in (msg_eq_netplayer), the group changing (msg_wldgroup)
	// or the next zone's profile (CheckForGroupChanges).
	void ReleaseSpawns() { m_unlinked.fill(true); }
	void SpawnAdded(const ZoneSpawn &spawn); // msg_eq_netplayer: a player of the group links again
	const ZoneSpawn *PetOf(const ZoneSpawn &owner) const; // ActorInfo's PetID
	bool IsAGroupMember(const std::string &name) const; // EQ_PC::IsAGroupMember: the profile's list, or us
	bool show_pets = true;                              // CGroupWnd+0x164, the window menu's pet gauges
	void DropInvitation(); // SetInvited(0) and the inviter forgotten (CRaid::InitializeRaid's too)

private:
	std::string Text(uint32_t id, const std::vector<std::string> &args, const std::string &fallback) const;
	void Say(const std::string &text, int type = 0x111) const;
	void ClearMembers(); // the members, the leader

	ZoneSession *m_zone = nullptr;
	const GameValues *m_values = nullptr;
	Raid *m_raid = nullptr;
	Host m_host;
	std::string m_members[Members];
	std::array<bool, Members> m_unlinked{}; // pinstGroup's entry let go (ReleaseSpawns)
	std::string m_leader;
	int m_selected = -1;            // ActorInfo+0x278
	bool m_invited = false;         // DAT_007f6afa and EQPlayer::IsInvited
	std::string m_inviter;          // DAT_007f6f7d
	std::string m_invite_group;     // DAT_007f6fbd
};
