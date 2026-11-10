#pragma once

// eqgame's guilds, as the client keeps them (the zone owns the guilds themselves):
//   - ours, EQ_Character's GuildId and GuildStatus (ZoneProfile's guild_id and guild_rank: -1 none;
//     0 member, 1 officer, 2 leader), and each spawn's (ZoneSpawn's guild_id and guild_rank);
//   - the commands: /guildinvite (CEverQuest::GuildInvite @0052edc0), /guildremove
//     (CEverQuest_GuildRemove @0052f075), /guilddelete (CEverQuest_GuildDelete @0052f29f),
//     /guildstatus (CEverQuest::GuildStatus @0052f36b), /guildleader (CEverQuest_GuildLeader
//     @0052f49e), /guildwar and /guildpeace (CEverQuest_GuildWar @00530a45, _GuildPeace @00530b05)
//     and /guildmotd (@004f4972);
//   - what the zone sends: an invite or a promotion (msg_invite_guild @004dbfc5), a member's rank
//     (msg_guild_list @004e483f), our guild disbanded (@004e473f), another guild's leader asking
//     ours for war or peace (msg_guild_war @004e493c, msg_guild_peace @004e4b09) and our petition
//     turned down (handleRequestDenial @004e48df, HandleWorldMessage's 0x4194), and a spawn's guild
//     or rank changing (msg_stat_change's 0x16 and 0x17);
//   - the questions in the confirmation dialog (CConfirmationDialog::ProcessYes and ProcessNo):
//     0 an invite or promotion (10 s; no or the time running out answers 5), 8 war and 9 peace
//     (10 s; no, -2), 0x17 deleting the guild (20 s);
//   - the guild's message of the day asked for once on entering the game (DoMainLoop, after
//     SetDefaultGameValues' DAT_00799b8a).
// Not ours: the old UI's requestor (DAT_007f9550) eqgame uses instead of the dialog without the new UI.

#include "zone_session.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class EqStr;

class Guild
{
public:
	struct Host
	{
		std::function<void(const std::string &, int)> chat;  // a message, in a chat color type
		std::function<const ZoneSpawn *()> target;           // pinstTarget (ours as ourselves), or null
		std::function<bool(const ZoneSpawn &)> other_team;   // ruleset 4's EQPlayer::NotOnSameDeity
		std::function<bool()> receive_invites;               // the options' Receive Guild Invites (DAT_00798adc)
		std::function<bool()> asking;                        // the confirmation dialog's up (Activated)
		// CConfirmationDialog::Activate when it isn't up already (false: it is, nothing asked);
		// `answer` gets yes or no (the time running out is no).
		std::function<bool(int category, uint32_t timeout_ms, const std::string &text, std::function<void(bool)> answer)> ask;
	};

	void Attach(ZoneSession &zone, const EqStr *eqstr, Host host);
	void EnteringGame() { m_ask_motd = true; } // SetDefaultGameValues: the MOTD's asked for once in
	void InZone();                             // DoMainLoop: ask for it

	void ZoneEvent(const ZoneSession::Event &event);

	// /guildinvite: its first word a name (or none, "o" or one starting with 'l': the target), and
	// "o" after a name (or "o" alone) for an officer.
	void Invite(const std::string &word, const std::string &rest);
	void Remove(const std::string &name);      // /guildremove: a name, or the target
	void Delete();                             // /guilddelete
	void Status(const std::string &name);      // /guildstatus: a name, or the target
	void Leader(const std::string &name);      // /guildleader: a name, or the target
	void War(std::string word, const std::string &rest); // /guildwar: a name (or the target), then days
	void Peace(const std::string &name);       // /guildpeace: a name, or the target
	void Motd(const std::string &text);        // /guildmotd

private:
	std::string Text(uint32_t id, const std::vector<std::string> &args, const std::string &fallback) const;
	void Say(const std::string &text, int type = 0x111) const;
	const ZoneSpawn *FindPlayer(const std::string &name) const; // EQPlayer_get_player
	bool IsUs(const ZoneSpawn &spawn) const;

	void Invited(const ZoneGuild &invite);     // msg_invite_guild
	void WarAsked(const ZoneGuild &asked, bool peace);
	void Changed(const ZoneSession::Event &event);
	void RankChanged(const ZoneSession::Event &event);

	ZoneSession *m_zone = nullptr;
	const EqStr *m_eqstr = nullptr;
	Host m_host;
	bool m_ask_motd = false;
};
