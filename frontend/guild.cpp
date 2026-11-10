#include "guild.h"

#include "chat_text.h"
#include "eqstr.h"

#include <cctype>
#include <cstdlib>

namespace
{
	constexpr uint32_t QuestionMs = 10000;   // an invite's, war's and peace's (Activate's 10000)
	constexpr uint32_t DeleteMs = 20000;     // /guilddelete's
	constexpr int MaxGuildId = 0x1ff;        // /guildmotd's last guild

	// stricmp.
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

void Guild::Attach(ZoneSession &zone, const EqStr *eqstr, Host host)
{
	m_zone = &zone;
	m_eqstr = eqstr;
	m_host = std::move(host);
	m_ask_motd = false;
}

std::string Guild::Text(uint32_t id, const std::vector<std::string> &args, const std::string &fallback) const
{
	if (m_eqstr && m_eqstr->Has(id))
		return m_eqstr->Format(id, args);
	std::string out = fallback;
	for (size_t i = 0; i < args.size(); ++i) {
		std::string key = "%" + std::to_string(i + 1);
		size_t at = out.find(key);
		if (at != std::string::npos)
			out.replace(at, key.size(), args[i]);
	}
	return out;
}

void Guild::Say(const std::string &text, int type) const
{
	if (m_host.chat && !text.empty())
		m_host.chat(text, type);
}

bool Guild::IsUs(const ZoneSpawn &spawn) const
{
	return m_zone && spawn.id != 0 && spawn.id == m_zone->MySpawnId();
}

const ZoneSpawn *Guild::FindPlayer(const std::string &name) const
{
	// EQPlayer_get_player: the spawn of that name (any case), us among them.
	if (!m_zone || name.empty())
		return nullptr;
	if (Same(m_zone->Self().name, name))
		return &m_zone->Self();
	for (const auto &[id, spawn] : m_zone->Spawns())
		if (Same(spawn.name, name))
			return &spawn;
	return nullptr;
}

void Guild::InZone()
{
	// DoMainLoop, the first zone after SetDefaultGameValues: our spawn in a guild asks for its
	// message of the day (OP_SetGuildMOTD with none).
	if (!m_ask_motd || !m_zone)
		return;
	m_ask_motd = false;
	if (m_zone->Self().guild_id != -1)
		m_zone->SetGuildMOTD("");
}

// ---------------------------------------------------------------- the commands

void Guild::Invite(const std::string &word, const std::string &rest)
{
	// CEverQuest::GuildInvite (@0052edc0).
	if (!m_zone)
		return;
	const ZoneProfile &me = m_zone->Profile();
	if (me.guild_id == -1) {
		Say(Text(12242, {}, "You are not a member of any guild."));
		return;
	}
	if (me.guild_rank == 0) {
		Say(Text(12243, {}, "You are not a guild officer or leader, and therefore cannot invite members to join your guild."));
		return;
	}
	std::string other, name = word, text;
	bool officer = false;
	if (word.empty() || word == "o" || word[0] == 'l') {
		// The target: a player, not us.
		const ZoneSpawn *target = m_host.target ? m_host.target() : nullptr;
		if (!target || target->npc != 0 || IsUs(*target)) {
			Say(Text(12248, {}, "You must target a player to invite or supply a player name."));
			return;
		}
		if (m_host.other_team && m_host.other_team(*target)) {
			Say(Text(12244, {}, "You can only invite those players on your deity team."));
			return;
		}
		other = target->name;
		text = Text(12245, {target->name}, "You have invited %1 to become a member of the guild.");
		if (word.empty() || word[0] != 'o') {
			Say(text);
			m_zone->GuildInvite(other, 0);
			return;
		}
		name = target->name;
	}
	else {
		if (Same(me.name, word))
			return;
		other = word;
		// A player here, or a GM as an NPC (IsGameMaster).
		const ZoneSpawn *player = FindPlayer(word);
		if (!player || (player->npc != 0 && player->gm == 0)) {
			Say(Text(12249, {}, "You may only invite living Player Characters in your current zone into your guild."));
			return;
		}
		text = Text(12245, {word}, "You have invited %1 to become a member of the guild.");
		if (rest != "o") {
			Say(text);
			m_zone->GuildInvite(other, 0);
			return;
		}
	}
	if (me.guild_rank != 2) {
		Say(Text(12247, {}, "Only the guild leader can offer guild officer status."));
		return;
	}
	officer = true;
	Say(Text(12246, {name}, "You have invited %1 to become an officer in the guild."));
	m_zone->GuildInvite(other, officer ? 1 : 0);
}

void Guild::Remove(const std::string &name)
{
	// CEverQuest_GuildRemove (@0052f075).
	if (!m_zone)
		return;
	const ZoneProfile &me = m_zone->Profile();
	if (me.guild_id == -1) {
		Say(Text(12242, {}, "You are not a member of any guild."));
		return;
	}
	const ZoneSpawn *target = m_host.target ? m_host.target() : nullptr;
	if ((name.empty() && target && IsUs(*target)) || name == me.name) {
		// Ourselves: not the leader.
		if (me.guild_rank == 2) {
			Say(Text(12251, {}, "You must transfer leadership or delete the guild before removing yourself."));
			return;
		}
		m_zone->GuildRemove(me.name);
		Say(Text(12252, {}, "Attempting to remove you from the guild..."));
		return;
	}
	if (me.guild_rank == 0) {
		Say(Text(12253, {}, "You are not a guild officer or leader, and therefore cannot remove members from the guild."));
		return;
	}
	std::string who = name;
	if (who.empty()) {
		if (!target || target->npc != 0) {
			Say(Text(12255, {}, "You must target a player to remove or supply a player name."));
			return;
		}
		who = target->name;
	}
	m_zone->GuildRemove(who);
	Say(Text(12254, {who}, "Attempting to remove %1 from the guild..."));
}

void Guild::Delete()
{
	// CEverQuest_GuildDelete (@0052f29f): the leader is asked (0x17, 20 s); yes, ProcessYes's 0x17.
	if (!m_zone)
		return;
	if (m_zone->Profile().guild_rank < 2) {
		Say(Text(10013, {}, "You are not the guild leader, and therefore cannot disband the entire guild."));
		return;
	}
	if (!m_host.ask || (m_host.asking && m_host.asking()))
		return;
	m_host.ask(0x17, DeleteMs, Text(10014, {}, "ARE YOU SURE you wish to delete/disband your entire guild?"), [this](bool yes) {
		if (!yes || !m_zone)
			return;
		m_zone->GuildDelete();
		Say(Text(10015, {}, "Your guild has been disbanded!  All members will be removed."));
	});
}

void Guild::Status(const std::string &name)
{
	// CEverQuest::GuildStatus (@0052f36b): a player (not a GM), named or targeted.
	if (!m_zone)
		return;
	const ZoneSpawn *spawn = name.empty() ? (m_host.target ? m_host.target() : nullptr) : FindPlayer(name);
	if (!spawn || spawn->npc != 0 || spawn->gm != 0) {
		Say(Text(12260, {}, "You must target a living Player Character, or supply a player name, to list guild status."));
		return;
	}
	std::string who = spawn->DisplayName(); // trimName
	if (spawn->guild_id == -1) {
		Say(who + " is not in a guild.");
		return;
	}
	std::string guild = m_zone->GuildName(spawn->guild_id);
	switch (spawn->guild_rank) {
	case 0: Say(Text(12256, {who, guild}, "%1 is a member of %2.")); break;
	case 1: Say(Text(12257, {who, guild}, "%1 is an officer of %2.")); break;
	case 2: Say(Text(12258, {who, guild}, "%1 is the leader of %2.")); break;
	default: Say(Text(12259, {who, guild}, "%1 is not in a guild.")); break;
	}
}

void Guild::Leader(const std::string &name)
{
	// CEverQuest_GuildLeader (@0052f49e): the leader hands it on.
	if (!m_zone)
		return;
	const ZoneProfile &me = m_zone->Profile();
	if (me.guild_id == -1) {
		Say(Text(12242, {}, "You are not a member of any guild."));
		return;
	}
	if (me.guild_rank != 2) {
		Say(Text(12261, {}, "Only the guild leader may transfer membership to an officer."));
		return;
	}
	std::string who = name;
	const ZoneSpawn *target = m_host.target ? m_host.target() : nullptr;
	if (who.empty() && target)
		who = target->name;
	Say(Text(12262, {who}, "Trying to transfer leadership of the guild to %1..."));
	m_zone->GuildLeader(who);
}

void Guild::War(std::string word, const std::string &rest)
{
	// /guildwar (@004f4add): the days 1..14 (14 when not given), the target without a name; then
	// CEverQuest_GuildWar (@00530a45): sent whoever we are, said by the leader.
	if (!m_zone)
		return;
	int days = std::atoi(rest.c_str());
	if (days < 1 || days > 14)
		days = 14;
	const ZoneSpawn *target = m_host.target ? m_host.target() : nullptr;
	if (word.empty() && target)
		word = target->name;
	m_zone->GuildWar(word, days);
	if (m_zone->Profile().guild_rank == 2)
		Say(Text(12276, {word, std::to_string(days)}, "You have petitioned %1 for your guilds to go to war for %2 days."));
}

void Guild::Peace(const std::string &name)
{
	// /guildpeace (@004f4b3d), CEverQuest_GuildPeace (@00530b05): the leader only.
	if (!m_zone)
		return;
	std::string who = name;
	const ZoneSpawn *target = m_host.target ? m_host.target() : nullptr;
	if (who.empty() && target)
		who = target->name;
	if (m_zone->Profile().guild_rank != 2) {
		Say(Text(12278, {}, "Only a guild leader may sue for peace."));
		return;
	}
	Say(Text(12277, {who}, "You have petitioned %1 for your guilds to make peace."));
	m_zone->GuildPeace(who);
}

void Guild::Motd(const std::string &text)
{
	// /guildmotd (@004f4972), as our spawn: an officer or the leader sets "Name - text"; anyone
	// else, or no text, asks for it (an empty one).
	if (!m_zone)
		return;
	const ZoneSpawn &me = m_zone->Self();
	if (me.guild_id == -1 || me.guild_id > MaxGuildId) {
		Say(Text(12966, {}, "You are not in a guild."));
		return;
	}
	std::string motd;
	if (me.guild_rank > 0 && !text.empty() && me.name.size() + 5 + text.size() < 0x200)
		motd = me.name + " - " + text;
	m_zone->SetGuildMOTD(motd);
}

// ---------------------------------------------------------------- the zone's

void Guild::ZoneEvent(const ZoneSession::Event &event)
{
	using Type = ZoneSession::Event::Type;
	if (!m_zone)
		return;
	const ZoneGuild &g = event.guild;
	switch (event.type) {
	case Type::GuildInvited:
		Invited(g);
		break;
	case Type::GuildMemberRank:
		// msg_guild_list (@004e483f): for us, someone's rank in our guild.
		if (m_zone->Profile().name != g.us)
			break;
		if (g.rank == 0)
			Say(Text(1429, {g.name}, "%1 is a member of your guild."));
		else if (g.rank == 1)
			Say(Text(1430, {g.name}, "%1 is an officer of your guild."));
		else if (g.rank == 2)
			Say(Text(1431, {g.name}, "%1 is the leader of your guild."));
		break;
	case Type::GuildDisbanded:
		Say(Text(1377, {}, "Your guild has been disbanded!  You are no longer a member of any guild."), ChatType::Yellow);
		break;
	case Type::GuildWarAsked:
		WarAsked(g, false);
		break;
	case Type::GuildPeaceAsked:
		WarAsked(g, true);
		break;
	case Type::GuildWarRefused:
		// handleRequestDenial (@004e48df).
		if (g.flag == -5)
			Say("Could not declare war on that target's guild.  Try again.");
		else if (g.flag == -2)
			Say(g.name + " has denied your request for war.");
		break;
	case Type::GuildPeaceRefused:
		// HandleWorldMessage's 0x4194.
		if (g.flag == -5)
			Say("Could not sue for peace for that target's guild.  Try again.");
		else if (g.flag == -2)
			Say(g.name + " has denied your request for peace.");
		break;
	case Type::GuildChanged:
		Changed(event);
		break;
	case Type::GuildRankChanged:
		RankChanged(event);
		break;
	default:
		break;
	}
}

void Guild::Invited(const ZoneGuild &invite)
{
	// msg_invite_guild (@004dbfc5): an invite for us, or a change of our rank in our guild.
	const ZoneProfile &me = m_zone->Profile();
	if (!Same(me.name, invite.us))
		return;
	bool busy = m_host.asking && m_host.asking();
	int16_t guild = static_cast<int16_t>(invite.guild);
	int32_t rank = invite.rank;
	std::string inviter = invite.name;
	auto answer = [this, inviter, guild](uint32_t response) { m_zone->GuildInviteAnswer(inviter, response, guild); };
	auto ask = [this, inviter, guild, rank](const std::string &text) {
		if (!m_host.ask)
			return;
		m_host.ask(0, QuestionMs, text, [this, inviter, guild, rank](bool yes) {
			// ProcessYes's 0: the rank taken; ProcessNo's (and the time running out): 5.
			if (m_zone)
				m_zone->GuildInviteAnswer(inviter, yes ? static_cast<uint32_t>(rank) : 5u, guild);
		});
	};
	std::string guild_name = m_zone->GuildName(guild);
	bool receive = !m_host.receive_invites || m_host.receive_invites();
	if (me.guild_id != guild && !receive) {
		// Receive Guild Invites off: turned down (5), and said so.
		answer(5);
		if (rank == 0)
			Say(Text(543, {inviter, guild_name}, "You have automatically declined %1's invite to join %2 as a member."));
		else if (rank == 1)
			Say(Text(544, {inviter, guild_name}, "You have automatically declined %1's invite to join %2 as an officer."));
		else if (rank == 2)
			Say(Text(545, {inviter, guild_name}, "You have automatically declined %1's invite to join %2 as the leader."));
		Say(Text(546, {}, "To Enable guild invites, toggle the guild invites to ON in the General options menu."));
		return;
	}
	std::string question;
	if (rank == 0)
		question = Text(534, {inviter, guild_name}, "%1 is interested in having you join %2 as a member.");
	else if (rank == 1)
		question = Text(535, {inviter, guild_name}, "%1 is interested in having you join %2 as an officer.");
	else if (rank == 2)
		question = Text(536, {inviter, guild_name}, "%1 is interested in having you join %2 as the leader.");
	if (me.guild_id >= 0 && me.guild_id != guild) {
		answer(3); // in another guild
		Say(question);
		return;
	}
	if (busy) {
		answer(4); // considering something else
		Say(question);
		return;
	}
	if (me.guild_id == -1 && rank >= 0 && rank < 3) {
		ask(question);
		return;
	}
	if (me.guild_id != guild)
		return;
	// Our own guild: the rank we have, a demotion (said, and taken), or a promotion (asked).
	if (rank == static_cast<int32_t>(me.guild_rank)) {
		answer(static_cast<uint32_t>(rank + 10));
		return;
	}
	if (rank < static_cast<int32_t>(me.guild_rank)) {
		if (rank == 0)
			Say(Text(547, {inviter}, "%1 has demoted you to regular membership."));
		answer(static_cast<uint32_t>(rank));
		return;
	}
	std::string promotion;
	if (rank == 1)
		promotion = Text(548, {inviter}, "%1 is interested in promoting you to guild officer.");
	else if (rank == 2)
		promotion = Text(549, {inviter}, "%1 is interested in promoting you to guild leader.");
	ask(promotion);
}

void Guild::WarAsked(const ZoneGuild &asked, bool peace)
{
	// msg_guild_war (@004e493c) and msg_guild_peace (@004e4b09): our leader is asked (8 or 9, 10 s);
	// with the dialog up, it's -2 straight back.
	const ZoneProfile &me = m_zone->Profile();
	if (asked.flag == 0 || me.guild_rank != 2)
		return;
	std::string their_guild = m_zone->GuildSlotName(static_cast<int16_t>(asked.guild));
	std::string text = peace
		? Text(10003, {asked.name, their_guild}, "%1, leader of the guild %2 wishes to make peace with your guild.  Do you accept?")
		: Text(10002, {asked.name, their_guild, std::to_string(asked.days)},
			"%1, leader of the guild %2 wishes to declare war on your guild for %3 day(s).  Do you accept?");
	std::string asker = asked.name;
	int32_t guild = asked.guild, days = asked.days;
	if (m_host.ask && !(m_host.asking && m_host.asking())) {
		m_host.ask(peace ? 9 : 8, QuestionMs, text, [this, peace, asker, guild, days](bool yes) {
			// ProcessYes's 8 and 9: 0; ProcessNo's: -2. Our name, as the profile has it.
			if (m_zone)
				m_zone->GuildWarAnswer(peace, asker, m_zone->Profile().name, guild, days, yes ? 0 : -2);
		});
		return;
	}
	m_zone->GuildWarAnswer(peace, asker, asked.us, guild, days, -2);
}

void Guild::Changed(const ZoneSession::Event &event)
{
	// msg_stat_change's 0x16: someone joining or leaving our guild, or us joining or leaving one.
	const ZoneGuild &g = event.guild;
	int32_t value = event.value;
	if (!g.self) {
		if (g.old_guild == value)
			return;
		if (g.our_guild == value && value >= 0)
			Say(Text(12881, {g.name}, "%1 has joined your guild."));
		else if (g.our_guild == g.old_guild && g.old_guild >= 0)
			Say(Text(12882, {g.name}, "%1 is no longer a member of your guild."));
		return;
	}
	if (g.our_guild == value)
		return;
	if (value < 0) {
		std::string old_name = m_zone->GuildSlotName(g.our_guild);
		if (!old_name.empty())
			Say(Text(12880, {old_name}, "You are no longer a member of %1."));
		else
			Say(Text(12879, {}, "You are no longer a member of a guild."));
	}
	else
		Say(Text(12878, {m_zone->GuildSlotName(static_cast<int16_t>(value))}, "You have joined %1."));
}

void Guild::RankChanged(const ZoneSession::Event &event)
{
	// msg_stat_change's 0x17: a rank in our guild, or ours, 0..2.
	const ZoneGuild &g = event.guild;
	int32_t value = event.value;
	if (value < 0 || value > 2)
		return;
	if (!g.self) {
		if (g.old_rank == value || g.our_guild != g.old_guild || g.old_guild < 0)
			return;
		static const char *const Others[3] = {"%1 is now a regular member of your guild.", "%1 is now an officer of your guild.",
			"%1 is now the leader of your guild."};
		Say(Text(12886 + value, {g.name}, Others[value]));
		return;
	}
	if (g.our_rank == value)
		return;
	static const char *const Ours[3] = {"You are now a regular member of the guild.", "You are now an officer of the guild.",
		"You are now the leader of the guild."};
	Say(Text(12883 + value, {}, Ours[value]));
}
