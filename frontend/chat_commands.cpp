// CEverquest_InterpretCmd (@0054572f): a line typed into the chat window. A word starting with '/'
// (or a lone ' or :) is looked up in eqgame's command table, in its order: the first command the
// word starts is the one, so "/s" is /split and "/g" is /gsay, as in eqgame. Anything else is said.

#include "frontend.h"
#include "zone_names.h"

#include <cctype>
#include <cmath>
#include <cstdlib>

namespace
{
	// What we do with a command so far; the rest say so.
	enum class Command
	{
		None, Say, Tell, Group, Shout, Auction, Ooc, Guild, Reply, Emote, Hail, Location, Target, Camp,
		ClearChat, ShowNames, Sit, Stand, Book, Cast, Consider, Played, Motd, Time, Afk, Anonymous, Assist,
	};

	struct CommandEntry
	{
		const char *name;
		int gm;          // the GM level it needs; 0 anyone
		Command command;
	};

	// eqgame's table at 0x609afc, all 277 in order (abbreviations depend on it).
	constexpr CommandEntry Commands[] = {
		{"/who", 0, Command::None}, {"/consider", 0, Command::Consider}, {"/feedback", 0, Command::None},
		{"/consent", 0, Command::None}, {"/played", 0, Command::Played}, {"/ignore", 0, Command::None},
		{"/anonymous", 0, Command::Anonymous}, {"/afk", 0, Command::Afk}, {"/filter", 0, Command::None},
		{"/assist", 0, Command::Assist}, {"/duel", 0, Command::None}, {"/decline", 0, Command::None},
		{"/split", 0, Command::None}, {"/autosplit", 0, Command::None}, {"/quit", 0, Command::None},
		{"/stopsong", 0, Command::None}, {"/friends", 0, Command::None}, {"/shield", 0, Command::None},
		{"/shownames", 0, Command::ShowNames}, {"/shownpcnames", 10, Command::None}, {"/emote", 0, Command::Emote},
		{":", 0, Command::Emote}, {"/say", 0, Command::Say}, {"'", 0, Command::Say}, {"/tell", 0, Command::Tell},
		{"/msg", 0, Command::Tell}, {"/send", 0, Command::Tell}, {"/ttell", 0, Command::None}, {"/gsay", 0, Command::Group},
		{"/shout", 0, Command::Shout}, {"/auction", 0, Command::Auction}, {"/ooc", 0, Command::Ooc},
		{"/reply", 0, Command::Reply}, {"/channel", 0, Command::None}, {"/guildsay", 0, Command::Guild},
		{"/guildinvite", 0, Command::None}, {"/guildremove", 0, Command::None}, {"/guilddelete", 0, Command::None},
		{"/guildstatus", 0, Command::None}, {"/guildleader", 0, Command::None}, {"/guildwar", 0, Command::None},
		{"/guildpeace", 0, Command::None}, {"/pet", 0, Command::None}, {"/petition", 0, Command::None},
		{"/private", 1, Command::None}, {"/find", 1, Command::None}, {"/goto", 1, Command::None},
		{"/becomenpc", 1, Command::None}, {"/searchcorpse", 1, Command::None}, {"/yell", 0, Command::None},
		{"/motd", 0, Command::Motd}, {"/corpse", 0, Command::None}, {"/me", 0, Command::Emote},
		{"/location", 0, Command::Location}, {"/adriver", 0, Command::None}, {"/sdriver", 0, Command::None},
		{"/chat", 0, Command::None}, {"/join", 0, Command::None}, {"/leave", 0, Command::None},
		{"/leaveall", 0, Command::None}, {"/list", 0, Command::None}, {"/announce", 0, Command::None},
		{"/1", 0, Command::None}, {"/2", 0, Command::None}, {"/3", 0, Command::None}, {"/4", 0, Command::None},
		{"/5", 0, Command::None}, {"/6", 0, Command::None}, {"/7", 0, Command::None}, {"/8", 0, Command::None},
		{"/9", 0, Command::None}, {"/10", 0, Command::None}, {"/autojoin", 0, Command::None}, {"/keys", 0, Command::None},
		{"/mp3", 0, Command::None}, {"/smile", 0, Command::None}, {"/hail", 0, Command::Hail}, {"/frown", 0, Command::None},
		{"/laugh", 0, Command::None}, {"/cry", 0, Command::None}, {"/chuckle", 0, Command::None}, {"/hug", 0, Command::None},
		{"/poke", 0, Command::None}, {"/bow", 0, Command::None}, {"/bounce", 0, Command::None}, {"/panic", 0, Command::None},
		{"/nod", 0, Command::None}, {"/rofl", 0, Command::None}, {"/wave", 0, Command::None}, {"/cheer", 0, Command::None},
		{"/gesture", 0, Command::None}, {"/rude", 0, Command::None}, {"/flipoff", 0, Command::None},
		{"/bird", 0, Command::None}, {"/finger", 0, Command::None}, {"/agree", 0, Command::None},
		{"/amaze", 0, Command::None}, {"/apologize", 0, Command::None}, {"/applaud", 0, Command::None},
		{"/plead", 0, Command::None}, {"/bite", 0, Command::None}, {"/bleed", 0, Command::None},
		{"/blink", 0, Command::None}, {"/blush", 0, Command::None}, {"/boggle", 0, Command::None},
		{"/bonk", 0, Command::None}, {"/bored", 0, Command::None}, {"/brb", 0, Command::None}, {"/burp", 0, Command::None},
		{"/bye", 0, Command::None}, {"/cackle", 0, Command::None}, {"/calm", 0, Command::None}, {"/clap", 0, Command::None},
		{"/comfort", 0, Command::None}, {"/congratulate", 0, Command::None}, {"/cough", 0, Command::None},
		{"/cringe", 0, Command::None}, {"/curious", 0, Command::None}, {"/dance", 0, Command::None},
		{"/drool", 0, Command::None}, {"/duck", 0, Command::None}, {"/eye", 0, Command::None}, {"/gasp", 0, Command::None},
		{"/giggle", 0, Command::None}, {"/glare", 0, Command::None}, {"/grin", 0, Command::None},
		{"/groan", 0, Command::None}, {"/grovel", 0, Command::None}, {"/happy", 0, Command::None},
		{"/hungry", 0, Command::None}, {"/introduce", 0, Command::None}, {"/jk", 0, Command::None},
		{"/kneel", 0, Command::None}, {"/lost", 0, Command::None}, {"/massage", 0, Command::None},
		{"/moan", 0, Command::None}, {"/mourn", 0, Command::None}, {"/peer", 0, Command::None}, {"/point", 0, Command::None},
		{"/ponder", 0, Command::None}, {"/puzzle", 0, Command::None}, {"/raise", 0, Command::None},
		{"/ready", 0, Command::None}, {"/roar", 0, Command::None}, {"/salute", 0, Command::None},
		{"/shiver", 0, Command::None}, {"/shrug", 0, Command::None}, {"/sigh", 0, Command::None},
		{"/smirk", 0, Command::None}, {"/snarl", 0, Command::None}, {"/snicker", 0, Command::None},
		{"/stare", 0, Command::None}, {"/tap", 0, Command::None}, {"/tease", 0, Command::None},
		{"/thank", 0, Command::None}, {"/thirsty", 0, Command::None}, {"/veto", 0, Command::None},
		{"/welcome", 0, Command::None}, {"/whine", 0, Command::None}, {"/whistle", 0, Command::None},
		{"/yawn", 0, Command::None}, {"/smack", 0, Command::None}, {"/pat", 0, Command::None},
		{"/fidget", 0, Command::None}, {"/flex", 0, Command::None}, {"/nudge", 0, Command::None},
		{"/purr", 0, Command::None}, {"/kiss", 0, Command::None}, {"/time", 0, Command::Time},
		{"/discipline", 0, Command::None}, {"/random", 0, Command::None}, {"/summon", 3, Command::None},
		{"/broadcast", 4, Command::None}, {"/lastname", 4, Command::None}, {"/zone", 3, Command::None},
		{"/kick", 4, Command::None}, {"/kill", 3, Command::None}, {"/delcorpse", 8, Command::None},
		{"/hideme", 3, Command::None}, {"/servers", 3, Command::None}, {"/height", 4, Command::None},
		{"/exit", 0, Command::None}, {"/loot", 0, Command::None}, {"/roleplay", 0, Command::None},
		{"/follow", 0, Command::None}, {"/report", 0, Command::None}, {"/surname", 0, Command::None},
		{"/log", 0, Command::None}, {"/toggletell", 3, Command::None}, {"/inquire", 3, Command::None},
		{"/praise", 3, Command::None}, {"/warn", 3, Command::None}, {"/edit", 3, Command::None},
		{"/voice", 0, Command::None}, {"/microphone", 0, Command::None}, {"/movelog", 0, Command::None},
		{"/hidecorpses", 0, Command::None}, {"/vrdelay", 0, Command::None}, {"/reverb", 0, Command::None},
		{"/fastdrop", 0, Command::None}, {"/trackplayers", 0, Command::None}, {"/help", 0, Command::None},
		{"/lfg", 0, Command::None}, {"/approval", 4, Command::None}, {"/sit", 0, Command::Sit},
		{"/stand", 0, Command::Stand}, {"/book", 0, Command::Book}, {"/cast", 0, Command::Cast},
		{"/doability", 0, Command::None}, {"/whotarget", 0, Command::None}, {"/language", 0, Command::None},
		{"/target", 0, Command::Target}, {"/camp", 0, Command::Camp}, {"/attack", 0, Command::None},
		{"/invite", 0, Command::None}, {"/disband", 0, Command::None}, {"/note", 0, Command::None},
		{"/wincolor", 0, Command::None}, {"/usercolor", 0, Command::None}, {"/resetwindows", 0, Command::None},
		{"/mcicontrol", 0, Command::None}, {"/popmail", 0, Command::None}, {"/audiodebug", 0, Command::None},
		{"/dynamiclights", 0, Command::None}, {"/chatfontsize", 0, Command::None}, {"/lootnodrop", 0, Command::None},
		{"/reversesound", 0, Command::None}, {"/emotezone", 4, Command::None}, {"/invwinlabels", 0, Command::None},
		{"/tracksort", 0, Command::None}, {"/trackfilter", 0, Command::None}, {"/mousespeed", 0, Command::None},
		{"/gems", 0, Command::None}, {"/inspect", 0, Command::None}, {"/serverfilter", 0, Command::None},
		{"/showspelleffects", 0, Command::None}, {"/combatmusic", 0, Command::None}, {"/mixahead", 0, Command::None},
		{"/guildmotd", 0, Command::None}, {"/charinfo", 0, Command::None}, {"/safelock", 0, Command::None},
		{"/alternateadv", 0, Command::None}, {"/targetgroupbuff", 0, Command::None}, {"/tgb", 0, Command::None},
		{"/hotbutton", 0, Command::None}, {"/particledensity", 0, Command::None}, {"/facepick", 0, Command::None},
		{"/dismount", 0, Command::None}, {"/rtarget", 0, Command::None}, {"/showgrass", 0, Command::None},
		{"/alias", 0, Command::None}, {"/hitsmode", 0, Command::None}, {"/clearhits", 0, Command::None},
		{"/dopropertinting", 0, Command::None}, {"/makeleader", 0, Command::None}, {"/viewport", 0, Command::None},
		{"/clearchat", 0, Command::ClearChat}, {"/bazaar", 0, Command::None}, {"/trader", 0, Command::None},
		{"/music", 0, Command::None}, {"/bugreport", 0, Command::None}, {"/loadskin", 0, Command::None},
		{"/newui", 0, Command::None}, {"/webupdate", 0, Command::None}, {"/viewpetition", 0, Command::None},
		{"/deletepetition", 0, Command::None}, {"/raidwindow", 0, Command::None}, {"/makeraidleader", 0, Command::None},
		{"/raidinvite", 0, Command::None}, {"/raiddecline", 0, Command::None}, {"/raidaccept", 0, Command::None},
		{"/raiddisband", 0, Command::None}, {"/setloottype", 0, Command::None}, {"/addraidlooter", 0, Command::None},
		{"/removeraidlooter", 0, Command::None}, {"/rsay", 0, Command::None}, {"/quit", 0, Command::None},
	};

	bool IsSpace(char c) { return c == ' ' || c == '\t'; }

	// CXStr_isStringPrefixMatch: `word` is the start of `name`, ignoring case.
	bool StartsCommand(const std::string &word, const char *name)
	{
		size_t i = 0;
		for (; i < word.size(); ++i)
			if (!name[i] || std::tolower(static_cast<unsigned char>(word[i])) != std::tolower(static_cast<unsigned char>(name[i])))
				return false;
		return i > 0;
	}

	// CEverQuest_GrabFirstWord: the first word, and what follows it with the spaces skipped.
	std::string FirstWord(const std::string &text, std::string &rest)
	{
		size_t end = 0;
		while (end < text.size() && !IsSpace(text[end]))
			++end;
		size_t next = end;
		while (next < text.size() && IsSpace(text[next]))
			++next;
		rest = text.substr(next);
		return text.substr(0, end);
	}

	std::string Lower(std::string s)
	{
		for (auto &c : s)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return s;
	}

	// UTF8_strnicmp(text, word, n) == 0: their first n characters match, ignoring case (a shorter
	// text matches only as long a word).
	bool MatchesPrefix(const std::string &text, const std::string &word, size_t n)
	{
		return Lower(text.substr(0, n)) == Lower(word.substr(0, n));
	}

	// ctime without its newline, as eqgame strips it.
	std::string CTime(std::time_t when)
	{
		const char *text = std::ctime(&when);
		std::string s = text ? text : "";
		while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
			s.pop_back();
		return s;
	}

	// GetTimeString (@004f4be1): the days, hours, minutes and seconds that aren't 0, largest first,
	// joined as eqstr's "%1, %2, and %3"; just "0 seconds" when all are.
	std::string TimeString(const EqStr *strings, int days, int hours, int minutes, int seconds)
	{
		auto part = [strings](int n, uint32_t one, uint32_t many, const char *one_text, const char *many_text) {
			return FormatEqString(strings, n == 1 ? one : many, n == 1 ? one_text : many_text, {std::to_string(n)});
		};
		std::vector<std::string> parts;
		if (days != 0)
			parts.push_back(part(days, 12969, 12970, "%1 day", "%1 days"));
		if (hours != 0)
			parts.push_back(part(hours, 12971, 12972, "%1 hour", "%1 hours"));
		if (minutes != 0)
			parts.push_back(part(minutes, 12973, 12974, "%1 minute", "%1 minutes"));
		if (seconds != 0 || parts.empty())
			parts.push_back(part(seconds, 6686, 6687, "%1 second", "%1 seconds"));
		static const char *const joined[] = {"%1", "%1 and %2", "%1, %2, and %3", "%1, %2, %3, and %4"};
		size_t n = parts.size() - 1;
		return FormatEqString(strings, 6688 + static_cast<uint32_t>(n), joined[n], parts);
	}

	// formatTimeFromSeconds (@004f4dcc).
	std::string SecondsString(const EqStr *strings, int seconds)
	{
		return TimeString(strings, seconds / 86400, seconds % 86400 / 3600, seconds % 3600 / 60, seconds % 60);
	}

	// EQPlayer_WhatDeityAlliance (@0050976e): 1, 2 or 3 by deity; 0 the agnostic and anyone else.
	int DeityAlliance(int deity)
	{
		switch (deity) {
		case 201: case 203: case 206: case 211: return 3;
		case 202: case 205: case 207: case 209: case 213: case 214: case 216: return 2;
		case 204: case 208: case 210: case 212: case 215: return 1;
		default: return 0;
		}
	}
}

void FrontEnd::InterpretCommand(const std::string &typed)
{
	size_t start = 0;
	while (start < typed.size() && IsSpace(typed[start]))
		++start;
	std::string line = typed.substr(start);
	if (line.empty() || m_zone.GetState() != ZoneSession::State::InZone)
		return;

	auto format = [this](uint32_t id, const char *fallback, const std::vector<std::string> &args) {
		return FormatEqString(m_ui.eqstr, id, fallback, args);
	};
	const ZoneProfile &me = m_zone.Profile();
	const auto &spawns = m_zone.Spawns();
	auto target = spawns.find(Target());
	bool npc_target = target != spawns.end() && target->second.npc == 1;

	// The command word: up to a space after a '/', or else just the first character (' and :).
	std::string args;
	std::string word;
	if (line[0] == '/')
		word = FirstWord(line, args);
	else {
		word = line.substr(0, 1);
		size_t next = 1;
		while (next < line.size() && IsSpace(line[next]))
			++next;
		args = line.substr(next);
	}
	Command command = Command::Say;
	const char *name = nullptr;
	bool found = false;
	int gm = m_zone.Self().gm;
	for (const auto &entry : Commands) {
		if (word[0] == entry.name[0] && StartsCommand(word, entry.name)) {
			if (entry.gm > 0 && gm == 0)
				continue; // a GM's command, as if it weren't there
			command = entry.command;
			name = entry.name;
			found = true;
			break;
		}
	}
	if (!found) {
		if (line[0] == '/') {
			ChatOut(format(12380, "That is not a valid command.  Please use /help.", {}), ChatType::Red);
			return;
		}
		args = line; // not a command: said, the default channel
	}
	if (found && command == Command::None) {
		ChatOut(std::string(name) + " isn't supported yet.", ChatType::Default);
		return;
	}

	// CEverQuest::send_chat and the like: sent, and printed in the echo color for that channel.
	auto send = [&](uint16_t channel, const std::string &to, const std::string &text, uint32_t echo, const char *echo_text, int type) {
		if (text.empty())
			return;
		m_zone.SendChannel(channel, to, text);
		ChatOut(format(echo, echo_text, {text}), type);
	};
	switch (command) {
	case Command::Say:
		// To the NPC we have targeted, if there is one (so it hears us); otherwise to whoever's near.
		send(8, npc_target ? target->second.name : "", args, 12344, "You say, '%1'", ChatType::EchoSay);
		break;
	case Command::Hail: {
		std::string hail = target != spawns.end()
		                       ? format(13085, "Hail, %1", {target->second.DisplayName()})
		                       : format(13084, "Hail", {});
		send(8, npc_target ? target->second.name : "", hail, 12344, "You say, '%1'", ChatType::EchoSay);
		break;
	}
	case Command::Group:
		send(2, "", args, 12346, "You tell your party, '%1'", ChatType::EchoGroup);
		break;
	case Command::Shout:
		send(3, "", args, 12348, "You shout, '%1'", ChatType::EchoShout);
		break;
	case Command::Auction:
		send(4, "", args, 12350, "You auction, '%1'", ChatType::EchoAuction);
		break;
	case Command::Ooc:
		send(5, "", args, 12352, "You say out of character, '%1'", ChatType::EchoOoc);
		break;
	case Command::Guild:
		send(0, "", args, 12263, "You say to your guild, '%1'", ChatType::EchoGuild);
		break;
	case Command::Tell:
	case Command::Reply: {
		// The zone sends back "You told ..." itself.
		std::string to, text = args;
		if (command == Command::Tell)
			to = FirstWord(args, text);
		else if (m_last_tell.empty()) {
			ChatOut(format(12364, "You have not received any tells, so you cannot reply.", {}), ChatType::Default);
			break;
		}
		else
			to = m_last_tell;
		if (Lower(to) == Lower(me.name))
			ChatOut(format(12323, "Talking to yourself again?", {}), ChatType::Default);
		else if (!to.empty() && !text.empty())
			m_zone.SendChannel(7, to, text);
		break;
	}
	case Command::Emote:
		// CEverQuest::Emote: the zone puts our name in front.
		if (args.empty()) {
			ChatOut(format(13067, "That is not a valid emote, please try again.", {}), ChatType::Default);
			ChatOut(format(13068, "FORMAT: /emote <what you want to emote>", {}), ChatType::Default);
			break;
		}
		m_zone.Emote(" " + args);
		ChatOut(me.name + " " + args, ChatType::EchoEmote);
		break;
	case Command::Location: {
		// Our position in the server's terms: y, x, z.
		char y[32], x[32], z[32];
		const ZoneMovement &at = m_zone_view ? m_zone_view->Movement() : ZoneMovement{};
		snprintf(y, sizeof(y), "%.2f", at.y);
		snprintf(x, sizeof(x), "%.2f", at.x);
		snprintf(z, sizeof(z), "%.2f", at.z);
		ChatOut(format(13177, "Your Location is %1, %2, %3", {y, x, z}), ChatType::Default);
		break;
	}
	case Command::Target: {
		// EQPlayer_get_player_partial_name: the nearest spawn whose name starts with it.
		if (args.empty()) {
			ChatOut(format(13267, "Format: /target <name>", {}), ChatType::Default);
			break;
		}
		std::string want = Lower(args);
		for (auto &c : want)
			if (c == ' ')
				c = '_';
		const ZoneMovement &at = m_zone_view ? m_zone_view->Movement() : ZoneMovement{};
		uint16_t best = 0;
		float best_d = 0;
		for (const auto &[id, spawn] : spawns) {
			if (Lower(spawn.name).compare(0, want.size(), want) != 0)
				continue;
			float d = std::hypot(spawn.x - at.x, spawn.y - at.y);
			if (best == 0 || d < best_d) {
				best = id;
				best_d = d;
			}
		}
		SetTarget(best, true); // the zone answers with the target it set
		break;
	}
	case Command::Consider: {
		// /consider (@004f6364): our target, or with anything typed after it, the one typing (eqgame
		// hands its handlers the local player: "/consider x" considers us).
		uint16_t id = args.empty() ? Target() : m_zone.MySpawnId();
		const ZoneSpawn *spawn = id == m_zone.MySpawnId() && id != 0 ? &m_zone.Self() : nullptr;
		if (!spawn && target != spawns.end() && target->first == id)
			spawn = &target->second;
		if (!spawn || id == 0) {
			ChatOut(format(13063, "Consider whom?", {}), ChatType::Default);
			break;
		}
		if (spawn->npc < 2 && (spawn->race == 72 || spawn->race == 141 || spawn->race == 73 || spawn->race == 114)) {
			ChatOut(format(13064, "You can't consider that.", {}), ChatType::Default);
			break;
		}
		m_zone.Consider(id, spawn->npc >= 2);
		break;
	}
	case Command::Played: {
		// /played (@004f4f97, display_played): the birthday, the time played (the profile's
		// minutes, a minute added for each one since), this session (since the client started).
		// Then the alarm's time left, when there's one (there's no alarm window yet).
		ChatOut(format(12968, "%1's birthdate: %2.", {me.name, CTime(me.birthday).substr(0, 24)}), ChatType::Default);
		uint32_t played = m_zone.MinutesPlayed();
		ChatOut(format(12975, "Total time playing %1: %2", {me.name,
			TimeString(m_ui.eqstr, static_cast<int>(played / 1440), static_cast<int>(played % 1440 / 60), static_cast<int>(played % 60), 0)}),
			ChatType::Default);
		ChatOut(format(6683, "This session: %1", {SecondsString(m_ui.eqstr, static_cast<int>(std::time(nullptr) - m_started_at))}),
			ChatType::Default);
		break;
	}
	case Command::Motd:
		// /motd (@004f492e): the world's message of the day again, in yellow.
		ChatOut(format(12965, "MESSAGE OF THE DAY: %1", {m_world.Motd()}), ChatType::Yellow);
		break;
	case Command::Time: {
		// /time (CEverQuest_WhatTimeIsIt @005471ae): Norrath's time and date (EQWorldData::
		// CurrentGameTime), but not in the Plane of Time (219, 223) nor on Luclin, then Earth's.
		uint32_t zone = me.zone_id;
		int hour = 0, minute = 0, day = 1, month = 1, year = 0;
		if (zone == 219 || zone == 223)
			ChatOut(format(4036, "In this place, Time has no meaning that you can comprehend.", {}), ChatType::Default);
		else if (ZoneExpansion(zone) == 3)
			ChatOut(format(12388, "The sun does not move here.  Without that frame of reference, you cannot tell what time it is.", {}),
				ChatType::Default);
		else if (m_zone.GameDate(hour, minute, day, month, year)) {
			static const char *const weekdays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
			static const char *const months[] = {"January", "February", "March", "April", "May", "June", "July", "August",
				"September", "October", "November", "December"};
			char clock[16], ordinal[16], line[160];
			snprintf(clock, sizeof(clock), hour == 12 || (hour > 12 && hour != 24) ? "%d PM" : "%d AM",
				hour == 24 ? 12 : hour > 12 ? hour - 12 : hour);
			const char *suffix = day == 1 || day == 21 ? "st" : day == 2 || day == 22 ? "nd" : day == 3 || day == 23 ? "rd" : "th";
			snprintf(ordinal, sizeof(ordinal), "%d%s", day, suffix);
			// The week runs 1..7 and again: days 8..14 less 7, 15..21 less 14, 22 on less 21.
			int weekday = day >= 22 ? day - 21 : day >= 15 ? day - 14 : day > 7 ? day - 7 : day;
			snprintf(line, sizeof(line), "It is %s on %s, the %s day of %s, of the year %d.", clock,
				weekday >= 1 && weekday <= 7 ? weekdays[weekday - 1] : "", ordinal,
				month >= 1 && month <= 12 ? months[month - 1] : "", year);
			ChatOut(line, ChatType::Default);
		}
		ChatOut(format(12389, "Earth Time: %1", {CTime(std::time(nullptr))}), ChatType::Default);
		break;
	}
	case Command::Afk: {
		// /afk (@004fb489): off, on with a message to answer tells with, or toggled. The zone hears
		// SpawnAppearance 24 (the chat server's "/afk on silent" isn't ours).
		std::string off = format(12946, "off", {});
		bool afk;
		if (MatchesPrefix(args, off, 3))
			afk = false;
		else if (!args.empty()) {
			afk = true;
			if (args.size() < 256)
				m_afk_message = args;
		}
		else
			afk = !m_zone.Self().afk;
		if (!afk || args.empty())
			m_afk_message.clear();
		m_zone.SetAfk(afk);
		ChatOut(afk ? format(13199, "You are now A.F.K. (Away From Keyboard).", {})
		            : format(13200, "You are no longer A.F.K. (Away From Keyboard).", {}),
			ChatType::Default);
		break;
	}
	case Command::Anonymous: {
		// /anonymous (@004fc5d1): off, on, or toggled; not while roleplaying. The zone hears
		// SpawnAppearance 21 and keeps it (SavePC); our name sprite follows Self().
		uint8_t anon = m_zone.Self().anon;
		if (anon > 1) {
			ChatOut(format(13235, "You may not use /anonymous while roleplaying.", {}), ChatType::Default);
			break;
		}
		if (MatchesPrefix(args, format(12946, "off", {}), 3))
			anon = 0;
		else if (MatchesPrefix(args, format(12947, "on", {}), 2))
			anon = 1;
		else
			anon = static_cast<uint8_t>(1 - anon);
		ChatOut(anon == 0 ? format(13237, "You are no longer anonymous.", {}) : format(13236, "You are now anonymous.", {}),
			ChatType::Default);
		m_zone.SetAnonymous(anon);
		break;
	}
	case Command::Assist: {
		// /assist (@004fd7dc): not while we can't (allowslashcommand: casting, but for a bard's
		// songs). Our target's target, or the nearest one whose name starts with what's typed; "on"
		// and "off" set AttackOnAssist (and are still looked up as names). The zone answers with
		// their target (Assisted).
		if (m_spell_cast.Casting() && me.class_id != 8) {
			ChatOut(format(12938, "You can't use that command while casting...", {}), ChatType::Default);
			break;
		}
		uint16_t id = 0;
		if (args.empty()) {
			if (Target() == 0) {
				ChatOut(format(13265, "You must target the person you wish to assist, or type their name after the command.", {}),
					ChatType::Default);
				ChatOut(format(13266, "FORMAT: /assist <name>", {}), ChatType::Default);
				break;
			}
			id = Target();
		}
		else {
			if (Lower(args) == Lower(format(12947, "on", {}))) {
				m_config.attack_on_assist = true;
				ChatOut(format(13263, "Auto attack on assist is now *ON*.", {}), ChatType::Default);
			}
			if (Lower(args) == Lower(format(12946, "off", {}))) {
				m_config.attack_on_assist = false;
				ChatOut(format(13264, "Auto attack on assist is now *OFF*.", {}), ChatType::Default);
			}
			// EQPlayer_get_player_partial_name: the nearest whose name starts with it, us included.
			std::string want = Lower(args);
			for (auto &c : want)
				if (c == ' ')
					c = '_';
			if (Lower(me.name).compare(0, want.size(), want) == 0)
				id = m_zone.MySpawnId();
			else {
				const ZoneMovement &at = m_zone_view ? m_zone_view->Movement() : ZoneMovement{};
				float best_d = 0;
				for (const auto &[spawn_id, spawn] : spawns) {
					if (Lower(spawn.name).compare(0, want.size(), want) != 0)
						continue;
					float d = std::hypot(spawn.x - at.x, spawn.y - at.y);
					if (id == 0 || d < best_d) {
						id = spawn_id;
						best_d = d;
					}
				}
			}
			if (id == 0)
				break;
		}
		if (id == m_zone.MySpawnId())
			ChatOut(format(1403, "You cannot assist yourself!", {}), ChatType::Default);
		else
			m_zone.Assist(id);
		break;
	}
	case Command::Camp:
		StartCamp();
		break;
	case Command::ClearChat:
		m_chat.Clear();
		break;
	case Command::Sit:
		Sit();
		break;
	case Command::Stand:
		ChangeStance(100);
		break;
	case Command::Book: {
		// /book (@004fcfa6): the book opened, and at a page if one's given (the left one of two).
		if (!m_spell_book.Activated())
			m_spell_book.Activate(m_now_ms);
		if (!args.empty() && m_spell_book.Activated()) {
			int page = std::atoi(args.c_str()) - 1;
			m_spell_book.TurnToPage(page < 1 ? 0 : page % 2 != 0 ? page - 1 : page);
		}
		break;
	}
	case Command::Cast: {
		// /cast (@004fd12c): a gem from 1 to 8, as a click on it (HandleSpellLeftClick).
		int gem = std::atoi(args.c_str()) - 1;
		if (gem < 0 || gem > 7)
			ChatOut(format(13255, "Format: /cast 1 - 8", {}), ChatType::Default);
		else
			m_spell_book.GemLeftClick(gem, m_now_ms);
		break;
	}
	case Command::ShowNames: {
		// /shownames (@004ff84f): off, or 1 (first names) .. 4 (everything); anything else is 4.
		// With Zeal's 5 .. 7 too, and its messages for them.
		std::string rest;
		std::string level = FirstWord(args, rest);
		if (level.empty()) {
			ChatOut("Format: /shownames <off/1/2/3/4/5/6/7>", ChatType::Default);
			break;
		}
		std::string off = Lower(format(12946, "off", {}));
		if (Lower(level).compare(0, 3, off.substr(0, 3)) == 0) {
			m_config.show_names = 0;
			ChatOut(format(13293, "Player names are *off*.", {}), ChatType::Default);
			break;
		}
		int value = std::atoi(level.c_str());
		m_config.show_names = value >= 1 && value <= NameSprite::ShowNamesMax ? value : 4;
		static const char *const said[] = {"Showing only first names.", "Showing first and last names.",
			"Showing first, last and guild names.", "Showing everything.", "Showing title and first names.",
			"Showing title, first, and last names.", "Showing first and guild names."};
		int shown = m_config.show_names;
		ChatOut(shown <= 4 ? format(13293 + shown, said[shown - 1], {}) : said[shown - 1], ChatType::Default);
		break;
	}
	case Command::None:
		break;
	}
}

void FrontEnd::PrintConsider(const ZoneConsider &consider)
{
	auto format = [this](uint32_t id, const char *fallback, const std::vector<std::string> &args) {
		return FormatEqString(m_ui.eqstr, id, fallback, args);
	};
	// Who it's about, if we still know them; us too.
	const ZoneProfile &me = m_zone.Profile();
	ZoneSpawn self = m_zone.Self();
	self.name = me.name;
	self.deity = static_cast<int16_t>(me.deity);
	const ZoneSpawn *spawn = nullptr;
	if (consider.target != 0 && consider.target == m_zone.MySpawnId())
		spawn = &self;
	else if (auto it = m_zone.Spawns().find(consider.target); it != m_zone.Spawns().end())
		spawn = &it->second;
	if (!spawn)
		return;
	if (spawn->npc >= 2) {
		ChatOut(format(12241, "It is futile to consider the dead...", {}), ChatType::Default);
		return;
	}

	int diff = static_cast<int>(spawn->level) - static_cast<int>(me.level);
	std::string pronoun = spawn->gender == 0 ? format(12209, "he", {}) : spawn->gender == 1 ? format(12210, "she", {})
	                                                                                        : format(12211, "it", {});
	// The faction (players are indifferent), 1 ally .. 9 scowls.
	static const char *const factions[] = {"regards you as an ally", "looks upon you warmly", "kindly considers you",
		"judges you amiably", "regards you indifferently", "scowls at you, ready to attack", "glares at you threateningly",
		"glowers at you dubiously", "looks your way apprehensively"};
	uint32_t faction = spawn->npc == 0 ? 5 : consider.faction;
	std::string regard;
	if (faction >= 1 && faction <= 9)
		regard = format(12211 + faction, factions[faction - 1], {});
	uint8_t pvp = consider.pvpcon;
	if (pvp == 4) {
		// The deity teams' rules: our own team are allies; the rest by their demeanor.
		int mine = DeityAlliance(static_cast<int16_t>(me.deity)), theirs = DeityAlliance(spawn->deity);
		bool same = theirs == 0 || theirs == mine || (m_zone.Self().gm != 0);
		if (same)
			regard = format(12212, factions[0], {});
		else if (mine == 2)
			regard = theirs == 1 ? format(12221, "appears to have a goodly demeanor.", {})
			                     : format(12222, "appears to have an evil demeanor.", {});
		else if ((mine == 1 || mine == 3) && theirs == 2)
			regard = format(12220, factions[8], {});
		else
			regard = format(12218, factions[6], {});
	}

	// The con color: the zone's, or under PvP rules by the level difference (green past -4, -8
	// or -5, red from 5, 9 or 6 up, white between), and anyone under level 6 green to us past it.
	int color = 20;
	if (pvp == 1 || pvp == 2 || pvp == 4) {
		int green = pvp == 1 ? -4 : pvp == 2 ? -8 : -5, red = pvp == 1 ? 5 : pvp == 2 ? 9 : 6;
		if (diff < green)
			color = 2;
		else if (diff > 0)
			color = diff >= red ? 13 : 20;
	}
	else
		color = static_cast<int>(consider.color);
	if (pvp != 0) {
		if (spawn->level < 6)
			color = 2;
		else if (me.level < 6)
			color = 13;
	}

	// The assessment: by our level (under 15, under 25, the rest) and the color; PvP its own.
	std::string assessment;
	auto said = [&](uint32_t id, const char *text) { assessment = format(id, text, {pronoun}); };
	if (pvp == 0) {
		if (me.level < 15) {
			switch (color) {
			case 2: said(12226, "looks like a reasonably safe opponent."); break;
			case 4: diff < -3 || me.level < 7 ? said(12227, "looks like you would have the upper hand.")
			                                  : said(12228, "looks kind of risky, but you might win."); break;
			case 13: said(12225, "what would you like your tombstone to say?"); break;
			case 15: said(12231, "looks like quite a gamble."); break;
			case 18: said(12227, "looks like you would have the upper hand."); break;
			case 20: me.level < 8 ? said(12229, "looks like an even fight.") : said(12230, "looks kind of risky..you might win."); break;
			}
		}
		else if (me.level < 25) {
			switch (color) {
			case 2: diff < -5 ? said(12226, "looks like a reasonably safe opponent.")
			                  : said(12232, "You would probably win this fight..it's not certain though."); break;
			case 4: diff < -2 ? said(12234, "looks quite risky, but might be worth a try.")
			                  : said(12233, "looks kind of dangerous."); break;
			case 13: said(12225, "what would you like your tombstone to say?"); break;
			case 15: said(12231, "looks like quite a gamble."); break;
			case 18: said(12232, "You would probably win this fight..it's not certain though."); break;
			case 20: said(12235, "%1 appears to be quite formidable."); break;
			}
		}
		else {
			switch (color) {
			case 2: diff < -10 ? said(12236, "You could probably win this fight.")
			                   : said(12237, "This creature could pose problems, you would probably defeat it."); break;
			case 4: said(12235, "%1 appears to be quite formidable."); break;
			case 13: said(12225, "what would you like your tombstone to say?"); break;
			case 15: said(12238, "looks like %1 would wipe the floor with you!"); break;
			case 18: said(12232, "You would probably win this fight..it's not certain though."); break;
			case 20: said(12231, "looks like quite a gamble."); break;
			}
		}
	}
	else {
		switch (color) {
		case 2: said(12223, "%1 would not make a worthy opponent."); break;
		case 13: said(12225, "what would you like your tombstone to say?"); break;
		case 20: said(12224, "%1 would make quite a formidable opponent."); break;
		}
	}
	// "%1 %2 -- %3", in the con color; white is 10 with the new UI's chat.
	ChatOut(format(12239, "%1 %2 -- %3", {spawn->DisplayName(), regard, assessment}), color == 20 ? 10 : color);
}
