// CEverquest_InterpretCmd (@0054572f): a line typed into the chat window. A word starting with '/'
// (or a lone ' or :) is looked up in eqgame's command table, in its order: the first command the
// word starts is the one, so "/s" is /split and "/g" is /gsay, as in eqgame. Anything else is said.

#include "frontend.h"
#include "item_display.h"
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
		Random, LootNoDrop, FastDrop, AutoSplit, Roleplay, TargetGroupBuff, Split, Yell, Consent, Social, Attack, CombatMusic, DoAbility, Filter, ServerFilter, Invite, Disband, Who, WhoTarget, Follow, MakeLeader, Lfg,
		UserColor, GuildInvite, GuildRemove, GuildDelete, GuildStatus, GuildLeader, GuildWar, GuildPeace, GuildMotd,
		MakeRaidLeader, RaidInvite, RaidDecline, RaidAccept, RaidDisband, SetLootType, AddRaidLooter, RemoveRaidLooter,
		RaidSay, RaidWindow, Channel,
	};

	struct CommandEntry
	{
		const char *name;
		int gm;          // the GM level it needs; 0 anyone
		Command command;
	};

	// eqgame's table at 0x609afc, all 277 in order (abbreviations depend on it).
	constexpr CommandEntry Commands[] = {
		{"/who", 0, Command::Who}, {"/consider", 0, Command::Consider}, {"/feedback", 0, Command::None},
		{"/consent", 0, Command::Consent}, {"/played", 0, Command::Played}, {"/ignore", 0, Command::None},
		{"/anonymous", 0, Command::Anonymous}, {"/afk", 0, Command::Afk}, {"/filter", 0, Command::Filter},
		{"/assist", 0, Command::Assist}, {"/duel", 0, Command::None}, {"/decline", 0, Command::None},
		{"/split", 0, Command::Split}, {"/autosplit", 0, Command::AutoSplit}, {"/quit", 0, Command::None},
		{"/stopsong", 0, Command::None}, {"/friends", 0, Command::None}, {"/shield", 0, Command::None},
		{"/shownames", 0, Command::ShowNames}, {"/shownpcnames", 10, Command::None}, {"/emote", 0, Command::Emote},
		{":", 0, Command::Emote}, {"/say", 0, Command::Say}, {"'", 0, Command::Say}, {"/tell", 0, Command::Tell},
		{"/msg", 0, Command::Tell}, {"/send", 0, Command::Tell}, {"/ttell", 0, Command::None}, {"/gsay", 0, Command::Group},
		{"/shout", 0, Command::Shout}, {"/auction", 0, Command::Auction}, {"/ooc", 0, Command::Ooc},
		{"/reply", 0, Command::Reply}, {"/channel", 0, Command::Channel}, {"/guildsay", 0, Command::Guild},
		{"/guildinvite", 0, Command::GuildInvite}, {"/guildremove", 0, Command::GuildRemove},
		{"/guilddelete", 0, Command::GuildDelete}, {"/guildstatus", 0, Command::GuildStatus},
		{"/guildleader", 0, Command::GuildLeader}, {"/guildwar", 0, Command::GuildWar},
		{"/guildpeace", 0, Command::GuildPeace}, {"/pet", 0, Command::None}, {"/petition", 0, Command::None},
		{"/private", 1, Command::None}, {"/find", 1, Command::None}, {"/goto", 1, Command::None},
		{"/becomenpc", 1, Command::None}, {"/searchcorpse", 1, Command::None}, {"/yell", 0, Command::Yell},
		{"/motd", 0, Command::Motd}, {"/corpse", 0, Command::None}, {"/me", 0, Command::Emote},
		{"/location", 0, Command::Location}, {"/adriver", 0, Command::None}, {"/sdriver", 0, Command::None},
		{"/chat", 0, Command::None}, {"/join", 0, Command::None}, {"/leave", 0, Command::None},
		{"/leaveall", 0, Command::None}, {"/list", 0, Command::None}, {"/announce", 0, Command::None},
		{"/1", 0, Command::None}, {"/2", 0, Command::None}, {"/3", 0, Command::None}, {"/4", 0, Command::None},
		{"/5", 0, Command::None}, {"/6", 0, Command::None}, {"/7", 0, Command::None}, {"/8", 0, Command::None},
		{"/9", 0, Command::None}, {"/10", 0, Command::None}, {"/autojoin", 0, Command::None}, {"/keys", 0, Command::None},
		{"/mp3", 0, Command::None}, {"/smile", 0, Command::Social}, {"/hail", 0, Command::Hail}, {"/frown", 0, Command::Social},
		{"/laugh", 0, Command::Social}, {"/cry", 0, Command::Social}, {"/chuckle", 0, Command::Social}, {"/hug", 0, Command::Social},
		{"/poke", 0, Command::Social}, {"/bow", 0, Command::Social}, {"/bounce", 0, Command::Social}, {"/panic", 0, Command::Social},
		{"/nod", 0, Command::Social}, {"/rofl", 0, Command::Social}, {"/wave", 0, Command::Social}, {"/cheer", 0, Command::Social},
		{"/gesture", 0, Command::Social}, {"/rude", 0, Command::Social}, {"/flipoff", 0, Command::Social},
		{"/bird", 0, Command::Social}, {"/finger", 0, Command::Social}, {"/agree", 0, Command::Social},
		{"/amaze", 0, Command::Social}, {"/apologize", 0, Command::Social}, {"/applaud", 0, Command::Social},
		{"/plead", 0, Command::Social}, {"/bite", 0, Command::Social}, {"/bleed", 0, Command::Social},
		{"/blink", 0, Command::Social}, {"/blush", 0, Command::Social}, {"/boggle", 0, Command::Social},
		{"/bonk", 0, Command::Social}, {"/bored", 0, Command::Social}, {"/brb", 0, Command::Social}, {"/burp", 0, Command::Social},
		{"/bye", 0, Command::Social}, {"/cackle", 0, Command::Social}, {"/calm", 0, Command::Social}, {"/clap", 0, Command::Social},
		{"/comfort", 0, Command::Social}, {"/congratulate", 0, Command::Social}, {"/cough", 0, Command::Social},
		{"/cringe", 0, Command::Social}, {"/curious", 0, Command::Social}, {"/dance", 0, Command::Social},
		{"/drool", 0, Command::Social}, {"/duck", 0, Command::Social}, {"/eye", 0, Command::Social}, {"/gasp", 0, Command::Social},
		{"/giggle", 0, Command::Social}, {"/glare", 0, Command::Social}, {"/grin", 0, Command::Social},
		{"/groan", 0, Command::Social}, {"/grovel", 0, Command::Social}, {"/happy", 0, Command::Social},
		{"/hungry", 0, Command::Social}, {"/introduce", 0, Command::Social}, {"/jk", 0, Command::Social},
		{"/kneel", 0, Command::Social}, {"/lost", 0, Command::Social}, {"/massage", 0, Command::Social},
		{"/moan", 0, Command::Social}, {"/mourn", 0, Command::Social}, {"/peer", 0, Command::Social}, {"/point", 0, Command::Social},
		{"/ponder", 0, Command::Social}, {"/puzzle", 0, Command::Social}, {"/raise", 0, Command::Social},
		{"/ready", 0, Command::Social}, {"/roar", 0, Command::Social}, {"/salute", 0, Command::Social},
		{"/shiver", 0, Command::Social}, {"/shrug", 0, Command::Social}, {"/sigh", 0, Command::Social},
		{"/smirk", 0, Command::Social}, {"/snarl", 0, Command::Social}, {"/snicker", 0, Command::Social},
		{"/stare", 0, Command::Social}, {"/tap", 0, Command::Social}, {"/tease", 0, Command::Social},
		{"/thank", 0, Command::Social}, {"/thirsty", 0, Command::Social}, {"/veto", 0, Command::Social},
		{"/welcome", 0, Command::Social}, {"/whine", 0, Command::Social}, {"/whistle", 0, Command::Social},
		{"/yawn", 0, Command::Social}, {"/smack", 0, Command::Social}, {"/pat", 0, Command::Social},
		{"/fidget", 0, Command::Social}, {"/flex", 0, Command::Social}, {"/nudge", 0, Command::Social},
		{"/purr", 0, Command::Social}, {"/kiss", 0, Command::Social}, {"/time", 0, Command::Time},
		{"/discipline", 0, Command::None}, {"/random", 0, Command::Random}, {"/summon", 3, Command::None},
		{"/broadcast", 4, Command::None}, {"/lastname", 4, Command::None}, {"/zone", 3, Command::None},
		{"/kick", 4, Command::None}, {"/kill", 3, Command::None}, {"/delcorpse", 8, Command::None},
		{"/hideme", 3, Command::None}, {"/servers", 3, Command::None}, {"/height", 4, Command::None},
		{"/exit", 0, Command::None}, {"/loot", 0, Command::None}, {"/roleplay", 0, Command::Roleplay},
		{"/follow", 0, Command::Follow}, {"/report", 0, Command::None}, {"/surname", 0, Command::None},
		{"/log", 0, Command::None}, {"/toggletell", 3, Command::None}, {"/inquire", 3, Command::None},
		{"/praise", 3, Command::None}, {"/warn", 3, Command::None}, {"/edit", 3, Command::None},
		{"/voice", 0, Command::None}, {"/microphone", 0, Command::None}, {"/movelog", 0, Command::None},
		{"/hidecorpses", 0, Command::None}, {"/vrdelay", 0, Command::None}, {"/reverb", 0, Command::None},
		{"/fastdrop", 0, Command::FastDrop}, {"/trackplayers", 0, Command::None}, {"/help", 0, Command::None},
		{"/lfg", 0, Command::Lfg}, {"/approval", 4, Command::None}, {"/sit", 0, Command::Sit},
		{"/stand", 0, Command::Stand}, {"/book", 0, Command::Book}, {"/cast", 0, Command::Cast},
		{"/doability", 0, Command::DoAbility}, {"/whotarget", 0, Command::WhoTarget}, {"/language", 0, Command::None},
		{"/target", 0, Command::Target}, {"/camp", 0, Command::Camp}, {"/attack", 0, Command::Attack},
		{"/invite", 0, Command::Invite}, {"/disband", 0, Command::Disband}, {"/note", 0, Command::None},
		{"/wincolor", 0, Command::None}, {"/usercolor", 0, Command::UserColor}, {"/resetwindows", 0, Command::None},
		{"/mcicontrol", 0, Command::None}, {"/popmail", 0, Command::None}, {"/audiodebug", 0, Command::None},
		{"/dynamiclights", 0, Command::None}, {"/chatfontsize", 0, Command::None}, {"/lootnodrop", 0, Command::LootNoDrop},
		{"/reversesound", 0, Command::None}, {"/emotezone", 4, Command::None}, {"/invwinlabels", 0, Command::None},
		{"/tracksort", 0, Command::None}, {"/trackfilter", 0, Command::None}, {"/mousespeed", 0, Command::None},
		{"/gems", 0, Command::None}, {"/inspect", 0, Command::None}, {"/serverfilter", 0, Command::ServerFilter},
		{"/showspelleffects", 0, Command::None}, {"/combatmusic", 0, Command::CombatMusic}, {"/mixahead", 0, Command::None},
		{"/guildmotd", 0, Command::GuildMotd}, {"/charinfo", 0, Command::None}, {"/safelock", 0, Command::None},
		{"/alternateadv", 0, Command::None}, {"/targetgroupbuff", 0, Command::TargetGroupBuff}, {"/tgb", 0, Command::TargetGroupBuff},
		{"/hotbutton", 0, Command::None}, {"/particledensity", 0, Command::None}, {"/facepick", 0, Command::None},
		{"/dismount", 0, Command::None}, {"/rtarget", 0, Command::None}, {"/showgrass", 0, Command::None},
		{"/alias", 0, Command::None}, {"/hitsmode", 0, Command::None}, {"/clearhits", 0, Command::None},
		{"/dopropertinting", 0, Command::None}, {"/makeleader", 0, Command::MakeLeader}, {"/viewport", 0, Command::None},
		{"/clearchat", 0, Command::ClearChat}, {"/bazaar", 0, Command::None}, {"/trader", 0, Command::None},
		{"/music", 0, Command::None}, {"/bugreport", 0, Command::None}, {"/loadskin", 0, Command::None},
		{"/newui", 0, Command::None}, {"/webupdate", 0, Command::None}, {"/viewpetition", 0, Command::None},
		{"/deletepetition", 0, Command::None}, {"/raidwindow", 0, Command::RaidWindow}, {"/makeraidleader", 0, Command::MakeRaidLeader},
		{"/raidinvite", 0, Command::RaidInvite}, {"/raiddecline", 0, Command::RaidDecline}, {"/raidaccept", 0, Command::RaidAccept},
		{"/raiddisband", 0, Command::RaidDisband}, {"/setloottype", 0, Command::SetLootType}, {"/addraidlooter", 0, Command::AddRaidLooter},
		{"/removeraidlooter", 0, Command::RemoveRaidLooter}, {"/rsay", 0, Command::RaidSay}, {"/quit", 0, Command::None},
	};


	// CEverQuest_send_social (@00532ddd)'s socials, by its number (the order of these names; /gesture,
	// /flipoff, /bird and /finger are /rude's): the animation (-1 none), then eqstr ids and their
	// arguments for what's sent with no target and with one, and our own echo of each (the sent
	// ones + 200). An argument is a letter: N our name, S O P our he/him/his, T the target's name,
	// s o p its he/him/his. Sent, our name is "" (the zone puts it in front) and the pronouns are
	// ours by our gender; in the echo they're "You", "you", "you" and "your". Taken from the exe's
	// two switches.
	struct SocialText
	{
		uint32_t id; // 0: none
		const char *args;
	};
	struct SocialEntry
	{
		int animation;
		SocialText sent, sent_target, echo, echo_target;
	};
	constexpr SocialEntry Socials[] = {
		{77, {12500, "N"}, {12501, "NT"}, {12700, ""}, {12701, "T"}}, // /smile
		{28, {12502, "N"}, {12503, "NT"}, {12702, ""}, {12703, "T"}}, // /frown
		{63, {12504, "N"}, {12505, "NT"}, {12704, ""}, {12705, "T"}}, // /laugh
		{28, {12506, "N"}, {12507, "NT"}, {12706, ""}, {12707, "T"}}, // /cry
		{54, {12508, "N"}, {12509, "NT"}, {12708, ""}, {12709, "T"}}, // /chuckle
		{-1, {12510, "NO"}, {12511, "NT"}, {12710, ""}, {12711, "T"}}, // /hug
		{-1, {12512, "NO"}, {12513, "NT"}, {12712, ""}, {12713, "T"}}, // /poke
		{70, {12514, "N"}, {12515, "NT"}, {12714, ""}, {12715, "T"}}, // /bow
		{-1, {12516, "N"}, {12517, "NT"}, {12716, ""}, {12717, "T"}}, // /bounce
		{-1, {12518, "N"}, {12519, "NT"}, {12718, ""}, {12719, "T"}}, // /panic
		{48, {12520, "N"}, {12521, "NT"}, {12720, ""}, {12721, "T"}}, // /nod
		{-1, {12522, "N"}, {12523, "NT"}, {12722, ""}, {12723, "T"}}, // /rofl
		{29, {12524, "N"}, {12525, "NT"}, {12724, ""}, {12725, "T"}}, // /wave
		{27, {12526, "N"}, {12527, "NT"}, {12726, ""}, {12727, "T"}}, // /cheer
		{30, {12528, "N"}, {12529, "NT"}, {12728, ""}, {12729, "T"}}, // /rude
		{48, {12531, "NO"}, {12530, "NT"}, {12731, ""}, {12730, "T"}}, // /agree
		{49, {12533, "N"}, {12532, "NT"}, {12733, ""}, {12732, "NT"}}, // /amaze
		{50, {12535, "N"}, {12534, "NT"}, {12735, ""}, {12734, "NT"}}, // /apologize
		{51, {12537, "N"}, {12536, "NT"}, {12737, ""}, {12736, "NT"}}, // /applaud
		{50, {12539, "NO"}, {12538, "NT"}, {12739, ""}, {12738, "NT"}}, // /plead
		{-1, {12541, "N"}, {12540, "NT"}, {12741, ""}, {12740, "NT"}}, // /bite
		{52, {12543, "N"}, {12542, "NT"}, {12743, ""}, {12742, "T"}}, // /bleed
		{59, {12545, "N"}, {12544, "NT"}, {12745, ""}, {12744, "NT"}}, // /blink
		{53, {12547, "N"}, {12546, "NT"}, {12747, ""}, {12746, "T"}}, // /blush
		{49, {12549, "NP"}, {12548, "NTP"}, {12749, "P"}, {12748, "NTP"}}, // /boggle
		{-1, {12551, "N"}, {12550, "NT"}, {12751, ""}, {12750, "NT"}}, // /bonk
		{31, {12553, "NS"}, {12552, "NTS"}, {12753, "S"}, {12752, "NTS"}}, // /bored
		{29, {12555, "NS"}, {12554, "NTS"}, {12755, "S"}, {12754, "NTS"}}, // /brb
		{55, {12557, "N"}, {12556, "NT"}, {12757, ""}, {12756, "T"}}, // /burp
		{29, {12559, "N"}, {12558, "NT"}, {12759, ""}, {12758, "T"}}, // /bye
		{54, {12561, "N"}, {12560, "NT"}, {12761, ""}, {12760, "T"}}, // /cackle
		{-1, {12563, "N"}, {12562, "NT"}, {12763, ""}, {12762, "T"}}, // /calm
		{51, {12565, "NP"}, {12564, "NT"}, {12765, "P"}, {12764, "NT"}}, // /clap
		{-1, {12567, "N"}, {12566, "NT"}, {12767, ""}, {12766, "T"}}, // /comfort
		{27, {12569, "NO"}, {12568, "NT"}, {12769, "O"}, {12768, "NT"}}, // /congratulate
		{55, {12571, "N"}, {12570, "NT"}, {12771, ""}, {12770, "T"}}, // /cough
		{56, {12573, "N"}, {12572, "NT"}, {12773, ""}, {12772, "T"}}, // /cringe
		{57, {12575, "NO"}, {12574, "NT"}, {12775, "O"}, {12774, "NT"}}, // /curious
		{58, {12577, "NP"}, {12576, "NTo"}, {12777, "P"}, {12776, "NTo"}}, // /dance
		{61, {12579, "NO"}, {12578, "NT"}, {12779, "O"}, {12778, "T"}}, // /drool
		{56, {12581, "N"}, {12580, "NT"}, {12781, ""}, {12780, "T"}}, // /duck
		{60, {12583, "N"}, {12582, "NT"}, {12783, ""}, {12782, "T"}}, // /eye
		{49, {12585, "N"}, {12584, "NT"}, {12785, ""}, {12784, "NT"}}, // /gasp
		{54, {12587, "N"}, {12586, "NT"}, {12787, ""}, {12786, "T"}}, // /giggle
		{60, {12589, "N"}, {12588, "NT"}, {12789, ""}, {12788, "T"}}, // /glare
		{-1, {12591, "N"}, {12590, "NT"}, {12791, ""}, {12790, "T"}}, // /grin
		{28, {12593, "N"}, {12592, "NT"}, {12793, ""}, {12792, "T"}}, // /groan
		{50, {12595, "N"}, {12594, "NT"}, {12795, ""}, {12794, "T"}}, // /grovel
		{27, {12597, "N"}, {12596, "NT"}, {12797, ""}, {12796, "T"}}, // /happy
		{52, {12599, "N"}, {12598, "NTS"}, {12799, ""}, {12798, "NTS"}}, // /hungry
		{29, {12601, "NO"}, {12600, "NT"}, {12801, "O"}, {12800, "NT"}}, // /introduce
		{-1, {12603, "N"}, {12602, "NTS"}, {12803, ""}, {12802, "NTS"}}, // /jk
		{62, {12605, "N"}, {12604, "NT"}, {12805, ""}, {12804, "NT"}}, // /kneel
		{-1, {12607, "N"}, {12606, "NTS"}, {12807, ""}, {12806, "NTS"}}, // /lost
		{-1, {12609, "N"}, {12608, "NT"}, {12809, ""}, {12808, "NT"}}, // /massage
		{-1, {12611, "N"}, {12610, "NT"}, {12811, ""}, {12810, "T"}}, // /moan
		{28, {12613, "NP"}, {12612, "NPT"}, {12813, "P"}, {12812, "NPT"}}, // /mourn
		{61, {12615, "N"}, {12614, "NTo"}, {12815, ""}, {12814, "NTo"}}, // /peer
		{64, {12617, "N"}, {12616, "NT"}, {12817, ""}, {12816, "NT"}}, // /point
		{65, {12619, "N"}, {12618, "NTo"}, {12819, ""}, {12818, "NTo"}}, // /ponder
		{57, {12621, "N"}, {12620, "NT"}, {12821, ""}, {12820, "NT"}}, // /puzzle
		{66, {12623, "NP"}, {12622, "NTP"}, {12823, "P"}, {12822, "NTP"}}, // /raise
		{66, {12625, "NS"}, {12624, "NTs"}, {12825, "S"}, {12824, "NTs"}}, // /ready
		{27, {12627, "N"}, {12626, "NT"}, {12827, ""}, {12826, "T"}}, // /roar
		{67, {12629, "N"}, {12628, "NT"}, {12829, ""}, {12828, "NT"}}, // /salute
		{68, {12631, "N"}, {12630, "NT"}, {12831, ""}, {12830, "NT"}}, // /shiver
		{65, {12633, "N"}, {12632, "NT"}, {12833, ""}, {12832, "NT"}}, // /shrug
		{28, {12635, "N"}, {12634, "NT"}, {12835, ""}, {12834, "NT"}}, // /sigh
		{-1, {12637, "N"}, {12636, "NT"}, {12837, ""}, {12836, "NT"}}, // /smirk
		{60, {12639, "NP"}, {12638, "NT"}, {12839, "P"}, {12838, "NT"}}, // /snarl
		{54, {12641, "N"}, {12640, "NT"}, {12841, ""}, {12840, "NT"}}, // /snicker
		{57, {12643, "N"}, {12642, "NTp"}, {12843, ""}, {12842, "NTp"}}, // /stare
		{69, {12645, "NP"}, {12644, "NPST"}, {12845, "P"}, {12844, "NPST"}}, // /tap
		{-1, {12647, "N"}, {12646, "NT"}, {12847, ""}, {12846, "NT"}}, // /tease
		{70, {12649, "N"}, {12648, "NT"}, {12849, ""}, {12848, "NT"}}, // /thank
		{-1, {12651, "N"}, {12650, "NTS"}, {12851, ""}, {12850, "NTS"}}, // /thirsty
		{59, {12653, "N"}, {12652, "NT"}, {12853, ""}, {12852, "NT"}}, // /veto
		{29, {12655, "N"}, {12654, "NT"}, {12855, ""}, {12854, "NT"}}, // /welcome
		{-1, {12657, "N"}, {12656, "NT"}, {12857, ""}, {12856, "NT"}}, // /whine
		{61, {12659, "N"}, {12658, "NT"}, {12859, ""}, {12858, "NT"}}, // /whistle
		{31, {12661, "NP"}, {12660, "NTS"}, {12861, "P"}, {12860, "NT"}}, // /yawn
		{-1, {12662, "NO"}, {12663, "NT"}, {12862, ""}, {12863, "T"}}, // /smack
		{-1, {12664, "NO"}, {12665, "NT"}, {12864, ""}, {12865, "T"}}, // /pat
		{-1, {0, ""}, {12666, "NT"}, {0, ""}, {12866, "T"}}, // /nudge
		{-1, {12667, "N"}, {0, ""}, {12867, ""}, {0, ""}}, // /fidget
		{-1, {12670, "NO"}, {12671, "NT"}, {12870, ""}, {12871, "T"}}, // /purr
		{-1, {12672, "NO"}, {12673, "NT"}, {12872, "O"}, {12873, "T"}}, // /kiss
		{-1, {12668, "NP"}, {12669, "NT"}, {12868, ""}, {12869, "T"}}, // /flex
	};
	constexpr const char *SocialNames[] = {
		"/smile", "/frown", "/laugh", "/cry", "/chuckle", "/hug", "/poke", "/bow", "/bounce", "/panic", "/nod",
		"/rofl", "/wave", "/cheer", "/rude", "/agree", "/amaze", "/apologize", "/applaud", "/plead", "/bite",
		"/bleed", "/blink", "/blush", "/boggle", "/bonk", "/bored", "/brb", "/burp", "/bye", "/cackle", "/calm",
		"/clap", "/comfort", "/congratulate", "/cough", "/cringe", "/curious", "/dance", "/drool", "/duck",
		"/eye", "/gasp", "/giggle", "/glare", "/grin", "/groan", "/grovel", "/happy", "/hungry", "/introduce",
		"/jk", "/kneel", "/lost", "/massage", "/moan", "/mourn", "/peer", "/point", "/ponder", "/puzzle",
		"/raise", "/ready", "/roar", "/salute", "/shiver", "/shrug", "/sigh", "/smirk", "/snarl", "/snicker",
		"/stare", "/tap", "/tease", "/thank", "/thirsty", "/veto", "/welcome", "/whine", "/whistle", "/yawn",
		"/smack", "/pat", "/nudge", "/fidget", "/purr", "/kiss", "/flex"
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

namespace
{
	// EQ_Character::Race (@004bbd39): the playable races' names (and the Froglok's), "Unknown" for
	// the rest.
	std::string PcRace(const EqStr *strings, int race)
	{
		uint32_t id = race >= 1 && race <= 12 ? 1469 + race : race == 128 ? 1482 : race == 130 ? 1483 : race == 330 ? 2482 : 1484;
		return FormatEqString(strings, id, race >= 1 && race <= 12 ? RaceName(static_cast<uint16_t>(race)) : "Unknown", {});
	}

	// is_number (@004f3739): an optional sign, then digits only, and something there.
	bool IsNumber(const std::string &text)
	{
		size_t i = !text.empty() && (text[0] == '+' || text[0] == '-') ? 1 : 0;
		if (text.empty())
			return false;
		for (; i < text.size(); ++i)
			if (!std::isdigit(static_cast<unsigned char>(text[i])))
				return false;
		return true;
	}

	// /usercolor's list of the color numbers, page by page (eqgame's own lines, not eqstr's).
	const char *const UserColorPages[7][11] = {
		{"Here is a list of usercolor numbers:", " 1 - Say", " 2 - Tell", " 3 - Group", " 4 - Guild", " 5 - OOC",
			" 6 - Auction", " 7 - Shout", " 8 - Emote", " 9 - Spells (meming, scribing, casting, etc.)",
			" 10 - You hit other"},
		{" 11 - Other hits you", " 12 - You miss other", " 13 - Other misses you", " 14 - Some broadcasts (duels)",
			" 15 - Skills (ups, non-combat use, etc.)", " 16 - Disciplines or special abilities",
			" 17 - Unused at this time", " 18 - Default text and stuff you type", " 19 - Unused at this time",
			" 20 - Merchant Offer Price", nullptr},
		{" 21 - Merchant Buy/Sell", " 22 - Your death message", " 23 - Others death message",
			" 24 - Other damage other", " 25 - Other miss other", " 26 - /who command", " 27 - yell for help",
			" 28 - Hit for non-melee", " 29 - Spell worn off", " 30 - Money splits", nullptr},
		{" 31 - Loot message", " 32 - Dice Roll (/random)", " 33 - Others spells",
			" 34 - Spell Failures (resists, fizzles, missing component, bad target, etc.)",
			" 35 - Chat Channel Messages", " 36 - Chat Channel 1", " 37 - Chat Channel 2", " 38 - Chat Channel 3",
			" 39 - Chat Channel 4", " 40 - Chat Channel 5", nullptr},
		{" 41 - Chat Channel 6", " 42 - Chat Channel 7", " 43 - Chat Channel 8", " 44 - Chat Channel 9",
			" 45 - Chat Channel 10", " 46 - Melee Crits", " 47 - Spell Crits", " 48 - Too far away (melee)",
			" 49 - NPC Rampage", " 50 - NPC Flurry", nullptr},
		{" 51 - NPC Enrage", " 52 - Say echo", " 53 - Tell echo", " 54 - Group echo", " 55 - Guild echo",
			" 56 - OOC echo", " 57 - Auction echo", " 58 - Shout echo", " 59 - Emote echo",
			" 60 - Chat Channel 1 echo", nullptr},
		{" 61 - Chat Channel 2 echo", " 62 - Chat Channel 3 echo", " 63 - Chat Channel 4 echo",
			" 64 - Chat Channel 5 echo", " 65 - Chat Channel 6 echo", " 66 - Chat Channel 7 echo",
			" 67 - Chat Channel 8 echo", " 68 - Chat Channel 9 echo", " 69 - Chat Channel 10 echo", nullptr, nullptr},
	};
	// Each page's last line: where the next starts (the seventh's is its own 69).
	const char *const UserColorNext[6] = {
		" Type /usercolor page2 for numbers 11 - 20", " Type /usercolor page3 for numbers 21 - 30",
		" Type /usercolor page4 for numbers 31 - 40", " Type /usercolor page5 for numbers 41 - 50",
		" Type /usercolor page6 for numbers 51 and up", " Type /usercolor page7 for numbers 61 and up",
	};

	// CEverQuest_GrabFirstWord (@00545aae): past the spaces, a word up to a space (or a quoted one,
	// ' or "), the rest after it.
	std::string GrabFirstWord(std::string &rest)
	{
		size_t i = 0;
		while (i < rest.size() && std::isspace(static_cast<unsigned char>(rest[i])))
			++i;
		char end = ' ';
		if (i < rest.size() && (rest[i] == '\'' || rest[i] == '"'))
			end = rest[i++];
		std::string word;
		while (i < rest.size() && rest[i] != end && rest[i] != '\x12')
			word.push_back(rest[i++]);
		if (i < rest.size() && rest[i] == end)
			++i;
		while (i < rest.size() && std::isspace(static_cast<unsigned char>(rest[i])))
			++i;
		rest = rest.substr(i);
		return word;
	}

	bool SameText(const std::string &a, const std::string &b)
	{
		return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
			return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
		});
	}

	// strnicmp(a, b, n).
	bool SamePrefix(const std::string &a, const std::string &b, size_t n)
	{
		for (size_t i = 0; i < n; ++i) {
			char x = i < a.size() ? a[i] : '\0', y = i < b.size() ? b[i] : '\0';
			if (std::tolower(static_cast<unsigned char>(x)) != std::tolower(static_cast<unsigned char>(y)))
				return false;
			if (x == '\0')
				return true;
		}
		return true;
	}
}

void FrontEnd::Lfg(bool ours, const std::string &word)
{
	// /lfg (@004f9518): "on" or "off" (eqstr 12947, 12946). Ours says what changed; the zone's
	// (coming into a zone LFG) sends it again whatever.
	auto text = [this](uint32_t id, const char *fallback) { return FormatEqString(m_ui.eqstr, id, fallback, {}); };
	bool on;
	if (SameText(word, text(12947, "on"))) {
		if (m_group.InGroup()) {
			ChatOut(text(13132, "You are already in a group!"), ChatType::Default);
			return;
		}
		// (A Bazaar trader can't: 13133. We're never one.)
		on = true;
		if (ours && m_lfg == on)
			return;
		if (ours)
			ChatOut(text(13134, "You are now Looking For a Group."), ChatType::Default);
	}
	else if (SameText(word, text(12946, "off"))) {
		on = false;
		if (ours && m_lfg == on)
			return;
		if (ours)
			ChatOut(text(13135, "You are no longer Looking For a Group."), ChatType::Default);
	}
	else {
		if (ours)
			ChatOut(text(13136, "FORMAT: /lfg <on/off>."), ChatType::Default);
		return;
	}
	m_zone.SendLfg(on);
	m_lfg = on;
}

void FrontEnd::Who(const std::string &given)
{
	// CEverQuest::Who (@0052f614): up to six words, each a filter.
	auto text = [this](uint32_t id, const char *fallback, const std::vector<std::string> &args = {}) {
		return FormatEqString(m_ui.eqstr, id, fallback, args);
	};
	const GuildNames &guilds = m_zone.Guilds();
	auto guild_name = [&](int16_t id) {
		auto it = guilds.find(id);
		return it != guilds.end() && !it->second.empty() ? it->second : std::string("Unknown Guild");
	};
	std::string rest = given, whom;
	int mode = 0; // 2 our corpses, 3 /who all
	int16_t race = -1, class_id = -1, level_low = -1, level_high = -1, gm = -1, guild = -1;
	int first = -1, second = -1;
	bool have_class = false, have_race = false, have_first = false, have_second = false;
	for (int n = 0; n < 6; ++n) {
		std::string word = GrabFirstWord(rest);
		if (word.empty())
			break;
		if (SameText(word, text(3118, "corpse")))
			mode = 2;
		else if (SameText(word, text(13205, "ALL")) || SameText(word, "all"))
			mode = 3;
		else if (SameText(word, text(3119, "GM")) || SameText(word, "GM"))
			gm = 1;
		else if (SameText(word, text(3120, "NONGM")))
			gm = 0;
		else if (SameText(word, text(3112, "friend")) || SameText(word, "friends"))
			guild = -2;
		else if (SameText(word, text(3121, "LFG")))
			guild = -3;
		else if (SameText(word, "Trader") && mode != 3)
			guild = -4;
		else {
			std::string guild_word = text(13122, "guild");
			// "guild<n>": that guild by number.
			if (SamePrefix(word, guild_word, 5) && word.size() > 5 && std::isalnum(static_cast<unsigned char>(word[5]))) {
				guild = static_cast<int16_t>(std::atoi(word.c_str() + 5));
				whom = guild_name(guild);
				continue;
			}
			if (SameText(word, guild_word)) {
				if (m_zone.Self().guild_id != -1) {
					guild = m_zone.Self().guild_id;
					whom = guild_name(guild);
					continue;
				}
				ChatOut(text(12336, "You aren't in a guild, so can't ask for guild info in who list."), ChatType::Default);
			}
			// A class or race the word starts (the first of each).
			bool matched = false;
			if (!have_class)
				for (int c = 1; c <= 32 && !matched; ++c)
					if (SamePrefix(GameValues::ClassDesc(m_ui.eqstr, c), word, word.size())) {
						have_class = matched = true;
						class_id = static_cast<int16_t>(c);
					}
			if (!have_race && !matched)
				for (int r = 0; r < 331 && !matched; ++r)
					if (SamePrefix(PcRace(m_ui.eqstr, r), word, word.size())) {
						have_race = matched = true;
						race = static_cast<int16_t>(r);
					}
			if (matched)
				continue;
			// The first number, the second; otherwise a name or a guild's.
			if (std::isdigit(static_cast<unsigned char>(word[0])) && !have_first) {
				have_first = true;
				first = std::atoi(word.c_str());
				continue;
			}
			if (std::isdigit(static_cast<unsigned char>(word[0])) && !have_second) {
				have_second = true;
				second = std::atoi(word.c_str());
				continue;
			}
			// FindGuildIndexName: a guild the word starts (under 31 characters).
			if (word.size() < 31)
				for (const auto &[id, name] : guilds)
					if (!name.empty() && SamePrefix(name, word, word.size())) {
						guild = id;
						break;
					}
			whom = word;
		}
	}
	// A level, or a range either way round.
	if (first > 0 || second > 0) {
		int low = second, high = second;
		if (first > 0) {
			if (second < 1)
				second = first;
			low = first;
			high = second;
			if (first >= second) {
				low = second;
				high = first;
			}
		}
		level_low = static_cast<int16_t>(low);
		level_high = static_cast<int16_t>(high);
	}
	if (mode == 3) {
		if (guild != -2) {
			m_zone.WhoAll(whom, race, class_id, level_low, level_high, gm, guild);
			return;
		}
		// /who all friends: OP_FriendsWho with the friends list (no /friends yet: none).
		ChatOut(text(12308, "You have no friends, but you can add some with: /friends <name>"), 0x119);
		return;
	}
	ChatOut(guild == -3 ? text(12309, "Players Looking For Groups:") : text(12310, "Players on EverQuest:"), 0x119);
	ChatOut("---------------------------", 0x119);
	const ZoneProfile &me = m_zone.Profile();
	int count = 0;
	auto list = [&](const ZoneSpawn &spawn) {
		bool ok = true;
		if (class_id != -1 && (spawn.class_id != class_id || spawn.anon != 0))
			ok = false;
		if (race != -1 && (spawn.race != race || spawn.anon != 0))
			ok = false;
		if (level_low != -1 && (spawn.level < level_low || spawn.level > level_high || spawn.anon != 0))
			ok = false;
		if (gm != -1 && spawn.gm != gm)
			ok = false;
		if (guild == -3 && !spawn.lfg)
			ok = false;
		if (guild == -4) // a trader (the Bazaar's; none known here)
			ok = false;
		if (guild < 0) {
			if (!whom.empty() && !SamePrefix(whom, spawn.name, whom.size()))
				ok = false;
		}
		else if (spawn.guild_id != guild)
			ok = false;
		if (spawn.gm != 0 && spawn.anon != 0)
			ok = false;
		if (!ok)
			return;
		std::string guild_text = spawn.guild_id == -1 || spawn.anon == 1 ? "" : " <" + guild_name(spawn.guild_id) + ">";
		std::string afk = spawn.afk ? text(12311, " AFK ") : "";
		std::string gm_text = spawn.gm ? text(12312, "* GM * ") : "";
		std::string linkdead = spawn.linkdead && !spawn.pvp ? text(12313, " <LINKDEAD>") : "";
		std::string lfg = spawn.lfg == 1 ? text(12314, " LFG") : "";
		std::string trader; // " TRADER" (12315) for a Bazaar trader; not known here
		std::string line;
		if (mode == 0) {
			if (spawn.npc != 0)
				return;
			if (spawn.anon == 0)
				line = trader + afk + linkdead + gm_text + "[" + std::to_string(spawn.level) + " " +
				       GameValues::ClassTitle(m_ui.eqstr, spawn.class_id, spawn.level) + "] " + spawn.DisplayName() + " (" +
				       PcRace(m_ui.eqstr, spawn.race) + ")" + guild_text + lfg;
			else
				line = trader + afk + linkdead + gm_text + "[" + text(3122, "ANONYMOUS") + "] " + spawn.DisplayName() + " " +
				       guild_text + lfg;
		}
		else if (mode == 2 && spawn.npc > 1 && SamePrefix(spawn.name, me.name, me.name.size()))
			line = "[" + std::to_string(spawn.level) + " " + GameValues::ClassTitle(m_ui.eqstr, spawn.class_id, spawn.level) + "] " +
			       spawn.DisplayName() + " (" + PcRace(m_ui.eqstr, spawn.race) + ")";
		else
			return;
		ChatOut(line, 0x119);
		++count;
	};
	// pinstSpawnList, ourselves among them.
	ZoneSpawn self = m_zone.Self();
	self.id = m_zone.MySpawnId();
	list(self);
	for (const auto &[id, spawn] : m_zone.Spawns())
		if (id != self.id)
			list(spawn);
	std::string zone = FullZoneName(me.zone_id, m_ui.eqstr);
	if (count == 0)
		ChatOut(text(12317, "There are no players in %1 that match those who filters.", {zone}), 0x119);
	else if (count == 1)
		ChatOut(text(12318, "There is 1 player in %1.", {zone}), 0x119);
	else
		ChatOut(text(12316, "There are %1 players in %2.", {std::to_string(count), zone}), 0x119);
}

void FrontEnd::WhoAnswer(const ZoneWho &who)
{
	// msg_who_response (@004e4d3d): the heading and its line, a line for each player, the closing.
	auto text = [this](uint32_t id, const std::vector<std::string> &args) {
		return FormatEqString(m_ui.eqstr, id, "", args);
	};
	if (who.heading != -1) {
		ChatOut(text(static_cast<uint16_t>(who.heading), {}), ChatType::Default);
		ChatOut(who.line, ChatType::Default);
	}
	for (const ZoneWhoPlayer &p : who.players) {
		std::string line;
		if (p.format == 5026 || p.format == 5027)
			line = text(p.format, {ShortZoneName(p.zone)});
		else {
			std::string flag = p.flag != 0xffff ? text(p.flag, {}) : "";
			std::string rank = p.rank != 0xffff ? std::to_string(p.rank) : ""; // %T1: the eqstr of that id
			if (p.format == 5022 || p.format == 5025) {
				std::string pid;
				if (p.pid == 5003)
					pid = text(5003, {std::to_string(static_cast<int32_t>(p.gm)), p.account});
				else if (p.pid == 5004)
					pid = text(5004, {p.account});
				std::string zone = p.zone_string != 0xffff ? text(p.zone_string, {ShortZoneName(p.zone)}) : "";
				std::string level = std::to_string(static_cast<int16_t>(p.level));
				std::string cls = GameValues::ClassTitle(m_ui.eqstr, p.class_id, p.level);
				std::string race = PcRace(m_ui.eqstr, p.race);
				if (p.format == 5022)
					line = text(5022, {rank, level, cls, p.name, race, zone, pid, flag});
				else
					line = text(5025, {rank, level, cls, p.name, race, p.guild, zone, pid, flag});
			}
			else if (p.format == 5023)
				line = text(5023, {rank, p.name, p.guild, flag});
			else if (p.format == 5024)
				line = text(5024, {rank, p.name, flag});
			else
				continue;
		}
		ChatOut(line, 10);
	}
	std::string n28 = std::to_string(who.numbers[0]), n2c = std::to_string(who.numbers[1]), n30 = std::to_string(who.numbers[2]),
	            n34 = std::to_string(who.numbers[3]);
	switch (who.closing) {
	case 5028: case 5034: case 5035: case 5036:
		ChatOut(text(static_cast<uint32_t>(who.closing), {n30}), ChatType::Default);
		break;
	case 5029: case 5033:
		ChatOut(text(static_cast<uint32_t>(who.closing), {}), ChatType::Default);
		break;
	case 5030:
		ChatOut(text(5030, {n34, n30, n2c}), ChatType::Default);
		break;
	case 5031:
		ChatOut(text(5031, {n34, n30, n28}), ChatType::Default);
		break;
	case 5032:
		ChatOut(text(5032, {n34, n30, n2c, n28}), ChatType::Default);
		break;
	default:
		break;
	}
}

bool FrontEnd::NotOnSameDeity(const ZoneSpawn &spawn) const
{
	int mine = DeityAlliance(static_cast<int16_t>(m_zone.Profile().deity));
	return mine != 0 && spawn.gm == 0 && DeityAlliance(spawn.deity) != mine;
}

namespace
{
	// CEverQuest::stripName (@00537e4b): a spawn's name up to an apostrophe ("a_rat's_corpse"),
	// without its digits and '#', underscores as spaces.
	std::string StripName(const std::string &name)
	{
		std::string out;
		for (char c : name) {
			if (c == '\'')
				break;
			if (std::isdigit(static_cast<unsigned char>(c)) || c == '#')
				continue;
			out.push_back(c == '_' ? ' ' : c);
		}
		return out;
	}
}

std::string FrontEnd::PercentConvert(const std::string &given) const
{
	uint16_t me = m_zone.MySpawnId();
	const ZoneSpawn *target = nullptr;
	if (uint16_t id = Target()) {
		auto it = m_zone.Spawns().find(id);
		target = id == me ? &m_zone.Self() : it != m_zone.Spawns().end() ? &it->second : nullptr;
	}
	std::string text = given;
	size_t from = 0; // where the last conversion's two characters ended (param_1)
	int converted = 0;
	for (size_t at = text.find('%'); at != std::string::npos; at = text.find('%', at + 1)) {
		// At the start, the second character, or after a space or an apostrophe. (eqgame measures
		// from the end of the last %x it took out, not of what went in; a % just before there
		// would make it copy a negative length, so it's passed over here.)
		if (at < from)
			continue;
		size_t count = at - from;
		if (count >= 2 && text[at - 1] != ' ' && text[at - 1] != '\'')
			continue;
		if (converted >= 11) {
			text[at] = '@'; // past eleven, the % goes
			continue;
		}
		char c = at + 1 < text.size() ? static_cast<char>(std::tolower(static_cast<unsigned char>(text[at + 1]))) : 0;
		std::string with;
		switch (c) {
		case 't':
			with = target ? StripName(target->name) : "Target";
			break;
		case 'm': {
			// The first spawn whose owner we are, temporary or not.
			with = "Pet";
			for (const auto &[id, spawn] : m_zone.Spawns())
				if (spawn.pet_owner == me && me != 0) {
					with = StripName(spawn.name);
					break;
				}
			break;
		}
		case 's':
			with = target && target->gender == 0 ? "he" : target && target->gender == 1 ? "she" : "it";
			break;
		case 'o':
			with = target && target->gender == 0 ? "him" : target && target->gender == 1 ? "her" : "it";
			break;
		case 'p':
			with = target && target->gender == 0 ? "his" : target && target->gender == 1 ? "her" : "its";
			break;
		case 'r':
			// A player's race, "Corpse" for a player's corpse, "NPC" for the rest; no target, nothing.
			if (target)
				with = target->npc == 0 ? RaceDescription(m_game_values, target->race) : target->npc == 2 ? "Corpse" : "NPC";
			break;
		default:
			continue;
		}
		text = text.substr(0, at) + with + text.substr(at + 2);
		++converted;
		from = at + 2;
	}
	return text;
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
	// allowslashcommand (@004f3cbb): not while casting (but a bard's songs), nor while we've no control
	// of ourselves (stance 102). (/safelock and the windows that block commands aren't ours yet.)
	auto allow = [&] {
		uint32_t why = 0;
		const char *text = "";
		if (m_zone.Self().stance == 102)
			why = 12368, text = "You do not have control of yourself right now.";
		else if (m_spell_cast.Casting() && me.class_id != 8)
			why = 12938, text = "You can't use that command while casting...";
		if (why != 0)
			ChatOut(format(why, text, {}), ChatType::Default);
		return why == 0;
	};
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
		// Not a command: to the chat window's default channel (CChatWindow::WndNotification puts its
		// 0x140 in DAT_00798598 for InterpretCmd): 0 guild, 2 group, 4 auction, 5 OOC, 7 a tell to its
		// name, 15 the raid (CRaid_SendRaidChat, without the commands' expansion check), 19 a chat
		// channel, anything else said.
		args = line;
		switch (m_chat_channel) {
		case 0: command = Command::Guild; break;
		case 2: command = Command::Group; break;
		case 4: command = Command::Auction; break;
		case 5: command = Command::Ooc; break;
		case 7: // sprintf("%s %s", the name, the text), then /tell
			command = Command::Tell;
			args = m_chat_channel_to + " " + line;
			break;
		case 15: command = Command::RaidSay; break;
		case 19:
			ChatOut("/chat isn't supported yet.", ChatType::Default);
			return;
		default: break;
		}
	}
	const bool by_channel = !found;
	if (found && command == Command::None) {
		ChatOut(std::string(name) + " isn't supported yet.", ChatType::Default);
		return;
	}

	// CEverQuest::send_chat and the like: sent, and printed in the echo color for that channel.
	auto send = [&](uint16_t channel, const std::string &to, const std::string &text, uint32_t echo, const char *echo_text, int type) {
		if (text.empty())
			return;
		// SendNewText sends it through DoPercentConvert, and the echo's print_chat_dsp_chat too.
		std::string sent = PercentConvert(text);
		m_zone.SendChannel(channel, to, sent);
		ChatOut(format(echo, echo_text, {sent}), type);
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
		else if (m_tell_list.empty()) {
			ChatOut(format(12364, "You have not received any tells, so you cannot reply.", {}), ChatType::Default);
			break;
		}
		else
			to = m_tell_list.front(); // _reply: __LastTell's first
		if (Lower(to) == Lower(me.name))
			ChatOut(format(12323, "Talking to yourself again?", {}), ChatType::Default);
		else if (!to.empty() && !text.empty())
			m_zone.SendChannel(7, to, PercentConvert(text)); // send_tell's SendNewText
		break;
	}
	case Command::Emote:
		// CEverQuest::Emote: the zone puts our name in front.
		if (args.empty()) {
			ChatOut(format(13067, "That is not a valid emote, please try again.", {}), ChatType::Default);
			ChatOut(format(13068, "FORMAT: /emote <what you want to emote>", {}), ChatType::Default);
			break;
		}
		m_zone.Emote(PercentConvert(" " + args));
		ChatOut(me.name + " " + args, ChatType::EchoEmote); // converted by ChatOut, as print_chat_dsp_chat does
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
	case Command::Attack:
		// /attack (@004fd3da): off, on, or toggled; nothing said (DoPassageOfTime turns it off again
		// with "Auto attack is off." unless we're standing).
		if (!allow())
			break;
		if (MatchesPrefix(args, format(12946, "off", {}), 3))
			SetAutoAttack(false);
		else if (MatchesPrefix(args, format(12947, "on", {}), 2))
			SetAutoAttack(true);
		else
			SetAutoAttack(!m_attack);
		break;
	case Command::Filter: {
		// /filter (@004fbc3a): a switch by its name (any case), toggled or stepped round, and said as
		// "%1 *ON*" and the like; anything else, the usage. Then the zone's told and they're kept.
		struct Switch
		{
			uint32_t name, label;
			const char *name_text, *label_text;
			int32_t ChatFilters::*value;
			int steps;      // 2 a toggle; 3 and 4 step round
			bool on_when_0; // a toggle said *ON* at 0
		};
		static const Switch switches[] = {
			{1315, 1316, "badword", "Bad Words Filter", &ChatFilters::bad_words, 2, true},
			{1317, 1318, "guildsay", "Guild Say", &ChatFilters::guild, 2, false},
			{1319, 1320, "social", "Socials", &ChatFilters::socials, 2, false},
			{1321, 1322, "gsay", "Group Say", &ChatFilters::group, 2, false},
			{1323, 1325, "shout", "Shouts", &ChatFilters::shouts, 2, false},
			{1326, 1324, "auction", "Auctions", &ChatFilters::auctions, 2, false},
			{1327, 1328, "ooc", "OOC", &ChatFilters::ooc, 2, false},
			{1329, 1330, "mymisses", "My Misses", &ChatFilters::my_misses, 2, false},
			{1331, 1332, "othersmisses", "Others Misses", &ChatFilters::others_misses, 2, false},
			{1352, 1353, "othershits", "Others Hits", &ChatFilters::others_hits, 2, false},
			{1333, 1334, "atkmissesme", "Attacker Misses Me", &ChatFilters::misses_me, 2, false},
			{1336, 1335, "pcspells", "PC Spells", &ChatFilters::pc_spells, 3, false},
			{1337, 1338, "npcspells", "NPC Spells", &ChatFilters::npc_spells, 2, true},
			{1339, 1340, "bardsongs", "Bard Songs", &ChatFilters::bard_songs, 4, false},
			{1341, 1342, "dmgshield", "Damage Shields", &ChatFilters::damage_shields, 2, true},
			{1343, 1344, "critspells", "Critical Spells", &ChatFilters::critical_spells, 3, false},
			{1345, 1346, "critmelee", "Critical Melee", &ChatFilters::critical_melee, 3, false},
		};
		const Switch *found = nullptr;
		for (const Switch &s : switches)
			if (Lower(args) == Lower(format(s.name, s.name_text, {})))
				found = &s;
		if (!found) {
			static const std::pair<uint32_t, const char *> usage[] = {{1354, "Usage: /filter <name>"},
				{1355, "Toggle text filters on or off."}, {1356, "<name> can be one of the following:"},
				{1357, "badword, guildsay, social, gsay (for group say),"},
				{1358, "shout, auction, ooc, mymisses, othersmisses,"},
				{1359, "othershits, atkmissesme, pcspells, npcspells,"},
				{1360, "bardsongs, dmgshield, critspells, critmelee"}};
			for (const auto &[id, text] : usage)
				ChatOut(format(id, text, {}), ChatType::Default);
			break;
		}
		int32_t &value = m_filters.*(found->value);
		value = found->steps == 2 ? value ^ 1 : (value + 1) % found->steps;
		// The state: a toggle's *ON* or *OFF*; pcspells *ALL*, *OFF*, *GRP*; bardsongs *ALL*, *ME*,
		// *GRP*, *OFF*; the criticals *ALL*, *ME*, *OFF*.
		uint32_t state;
		const char *state_text;
		if (found->steps == 2) {
			bool on = found->on_when_0 ? value == 0 : value != 0;
			state = on ? 1351 : 1348, state_text = on ? "%1 *ON*" : "%1 *OFF*";
		}
		else {
			static const uint32_t pc[] = {1347, 1348, 1349}, bard[] = {1347, 1350, 1349, 1348}, crit[] = {1347, 1350, 1348};
			const uint32_t *ids = found->steps == 4 ? bard : found->value == &ChatFilters::pc_spells ? pc : crit;
			state = ids[value];
			state_text = state == 1347 ? "%1 *ALL*" : state == 1348 ? "%1 *OFF*" : state == 1349 ? "%1 *GRP*" : "%1 *ME*";
		}
		ChatOut(format(state, state_text, {format(found->label, found->label_text, {})}), ChatType::Default);
		FiltersChanged();
		break;
	}
	case Command::ServerFilter:
		// /serverfilter (@004fecba): "off" (3 letters) or "on" (2), kept and sent; else its format.
		if (MatchesPrefix(args, "off", 3)) {
			m_filters.server_filter = 0;
			ChatOut("Server side MSG filtering is *OFF*", ChatType::Default);
		}
		else if (MatchesPrefix(args, "on", 2)) {
			m_filters.server_filter = 1;
			ChatOut("Server side MSG filtering is *ON*", ChatType::Default);
		}
		else {
			ChatOut("Format: /serverfilter <on/off>", ChatType::Default);
			ChatOut("This command will prevent you from receiving spell messages and combat hits. Previously these "
			        "messages were only filtered on the client, now they can be filtered on the server. This should "
			        "provide better performance for those with slower connections.",
				ChatType::Red);
			break;
		}
		FiltersChanged();
		break;
	case Command::DoAbility:
		// /doability (@004fd260): the abilities page's 1..6, the combat page's skills 7..10.
		if (allow())
			m_abilities.DoAbility(args, m_now_ms);
		break;
	case Command::UserColor: {
		// /usercolor (@004fe3a2): "<n> <red> <green> <blue>", or "<n> reset" for its built-in color
		// (GetDefaultUserDefinedColor), sets color n (1..72, though it says 69) as the Colors page's
		// Accept does, into [TextColors] (my-client.ini for eqclient.ini); "help" or "page1".."page7"
		// list the numbers; anything else, the format.
		std::string rest = args;
		std::string number = GrabFirstWord(rest);
		std::string second = GrabFirstWord(rest);
		std::string third = GrabFirstWord(rest);
		std::string fourth = GrabFirstWord(rest);
		int n = IsNumber(number) ? std::atoi(number.c_str()) : -1;
		int red = -1, green = -1, blue = -1;
		int page = -1;
		if (SameText(second, "reset")) {
			// GetDefaultUserDefinedColor (@004aa2ee): a color out of 0..71 is color 0's, and it writes
			// the built-in color to [TextColors] itself.
			int index = n - 1 >= 0 && n - 1 < ChatColors::UserColors ? n - 1 : 0;
			uint32_t rgb = ChatColors::Default(index);
			WriteUserColor(index, rgb);
			red = rgb >> 16 & 0xff, green = rgb >> 8 & 0xff, blue = rgb & 0xff;
		}
		else {
			if (SameText(number, "help") || SameText(number, "page1"))
				page = 0;
			for (int i = 2; i <= 7 && page < 0; ++i)
				if (SameText(number, "page" + std::to_string(i)))
					page = i - 1;
			if (page < 0 && IsNumber(second)) {
				red = std::atoi(second.c_str());
				if (IsNumber(third))
					green = std::atoi(third.c_str());
				if (IsNumber(fourth))
					blue = std::atoi(fourth.c_str());
			}
		}
		if (page >= 0) {
			for (const char *line : UserColorPages[page])
				if (line)
					ChatOut(line, ChatType::Default);
			if (page < 6)
				ChatOut(UserColorNext[page], ChatType::Default);
			break;
		}
		if (!args.empty() && red >= 0 && red < 256 && green >= 0 && green < 256 && blue >= 0 && blue < 256 && n >= 0 &&
			n < 73) {
			// CDisplay_SetUserDefinedColor takes 0..71 only: color 0 is written but changes nothing.
			SaveUserColor(n - 1, static_cast<uint32_t>(red << 16 | green << 8 | blue));
			break;
		}
		ChatOut("Format: /usercolor <number> <red> <green> <blue>", ChatType::Default);
		ChatOut("<number> -> 1 to 69", ChatType::Default);
		ChatOut("<red> <green> <blue> -> 0 to 255", ChatType::Default);
		ChatOut("To reset a user color use /usercolor <number> reset", ChatType::Default);
		ChatOut("To display a list of usercolor #'s use /usercolor help", ChatType::Default);
		break;
	}
	case Command::CombatMusic:
		// /combatmusic (@004fa035): with nothing after it, the format; "off" or anything else, set
		// (kept as CombatMusic). Either way, the setting said.
		if (args.empty())
			ChatOut(format(7523, "FORMAT: /combatmusic <ON | OFF> (default is ON)", {}), ChatType::Default);
		else {
			m_config.combat_music = Lower(args) != Lower(format(12946, "off", {}));
			if (m_attack)
				SetAutoAttack(true); // the music follows
		}
		ChatOut(m_config.combat_music ? format(7524, "Combat music is now ON", {}) : format(7525, "Combat music is now OFF", {}),
			ChatType::Default);
		break;
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
		SavePC();
		break;
	}
	case Command::Split: {
		// CEverQuest_DoSplit (@00537476): in a group, not while the zone's answer to something is
		// waited on; platinum, gold, silver and copper (atol, nothing under 1), each no more than
		// we have; then OP_Split and the coins off ours.
		if (!m_group.InGroup()) {
			ChatOut(format(12328, "You are not in a group!  Keep it all.", {}), ChatType::Default);
			break;
		}
		if (ZoneAnswerWaiting())
			break;
		std::string rest;
		long coins[4];
		std::string text = args;
		for (int i = 0; i < 3; ++i) {
			coins[i] = atol(FirstWord(text, rest).c_str());
			text = rest;
		}
		coins[3] = atol(text.c_str());
		if (coins[0] + coins[1] + coins[2] + coins[3] <= 0) {
			ChatOut(format(12329, "Please input a valid number of coins to split.", {}), ChatType::Default);
			ChatOut(format(12330, "Format: /split <platinum> <gold> <silver> <copper>.", {}), ChatType::Default);
			ChatOut(format(12331, "e.g. /split 0 1 5 24  would split 0 plat, 1 gold, 5 silver, 24 copper.", {}),
				ChatType::Default);
			break;
		}
		const ZoneProfile &me = m_zone.Profile();
		const int32_t have[4] = {me.platinum, me.gold, me.silver, me.copper};
		static const std::pair<uint32_t, const char *> short_of[4] = {{12332, "You don't have that much platinum!"},
			{12333, "You don't have that much gold!"}, {12334, "You don't have that much silver!"},
			{12335, "You don't have that much copper!"}};
		bool enough = true;
		for (int i = 0; i < 4 && enough; ++i) {
			if (coins[i] < 1)
				coins[i] = 0;
			else if (have[i] < coins[i]) {
				ChatOut(format(short_of[i].first, short_of[i].second, {}), ChatType::Default);
				enough = false;
			}
		}
		if (enough)
			m_zone.Split(static_cast<int32_t>(coins[0]), static_cast<int32_t>(coins[1]), static_cast<int32_t>(coins[2]),
				static_cast<int32_t>(coins[3]));
		break;
	}
	case Command::Yell:
		// /yell (@004f82ea): said in red, then OP_YellForHelp; the zone tells those near.
		ChatOut(format(13087, "You yell for help.", {}), ChatType::Red);
		m_zone.YellForHelp();
		break;
	case Command::Consent:
		// /consent (@004fb39e): the rest of the line as the name, if there is any.
		if (!args.empty())
			m_zone.Consent(args);
		break;
	case Command::TargetGroupBuff: {
		// /targetgroupbuff (@004ff181): "off" (3 letters) or "on" (2) kept in [Defaults]
		// TargetGroupBuff (eqclient.ini in eqgame) and told the zone; either way, then 2, which
		// asks the zone to say which it is.
		std::string value;
		if (MatchesPrefix(args, format(12946, "off", {}), 3))
			value = "0";
		else if (MatchesPrefix(args, format(12947, "on", {}), 2))
			value = "1";
		if (!value.empty()) {
			if (!m_config.settings_ini.empty()) {
				IniFile ini;
				ini.Load(m_config.settings_ini);
				ini.Set("Defaults", "TargetGroupBuff", value);
				ini.Save(m_config.settings_ini);
			}
			m_zone.TargetGroupBuff(value == "1" ? 1 : 0);
		}
		m_zone.TargetGroupBuff(2);
		break;
	}
	case Command::AutoSplit: {
		// /autosplit (@004fb3cd): "off" (3 letters), "on" (2), or toggled; said (13197, 13198) and
		// CEverQuest_ToggleAutoSplit.
		bool on = m_zone.Profile().autosplit;
		if (MatchesPrefix(args, format(12946, "off", {}), 3))
			on = false;
		else if (MatchesPrefix(args, format(12947, "on", {}), 2))
			on = true;
		else
			on = !on;
		ChatOut(on ? format(13197, "You will now automatically split money with your group.", {})
		           : format(13198, "You will no longer split money with your group.", {}),
			ChatType::Default);
		m_zone.SetAutoSplit(on);
		break;
	}
	case Command::Roleplay: {
		// /roleplay (@004fc433), on the anonymous state (1 anonymous, 2 roleplaying, 3 both): "off"
		// (3 letters) sets 0 from 2 and 1 from anything else (so from 0 too, as eqgame does); "on"
		// (2) sets 2 from 0, else 3; anything else steps 0 -> 2, 1 -> 3, 2 -> 0, 3 -> 1. Said
		// (13233, 13234), then SpawnAppearance 21 and our name's tint (it follows Self()).
		uint8_t anon = m_zone.Self().anon;
		if (MatchesPrefix(args, format(12946, "off", {}), 3))
			anon = anon != 2 ? 1 : 0;
		else if (MatchesPrefix(args, format(12947, "on", {}), 2))
			anon = anon != 0 ? 3 : 2;
		else
			anon = anon == 0 ? 2 : anon == 1 ? 3 : anon == 3 ? 1 : 0;
		ChatOut(anon < 2 ? format(13234, "You are no longer roleplaying.", {}) : format(13233, "You are now roleplaying.", {}),
			ChatType::Default);
		m_zone.SetAnonymous(anon);
		SavePC();
		break;
	}
	case Command::Assist: {
		// /assist (@004fd7dc): not while we can't (allowslashcommand: casting, but for a bard's
		// songs). Our target's target, or the nearest one whose name starts with what's typed; "on"
		// and "off" set AttackOnAssist (and are still looked up as names). The zone answers with
		// their target (Assisted).
		if (!allow())
			break;
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
	case Command::Random: {
		// /random (@004f62aa): "high" or "low high", either way round, each under 7 characters,
		// not below 0 and not over 32000. The zone rolls (nothing typed: 0 to 100) and everyone
		// near hears it (OP_RandomReply).
		std::string second;
		std::string first = FirstWord(args, second);
		if (first.size() >= 7 || second.size() >= 7) {
			ChatOut(format(13060, "Please use smaller numbers.", {}), ChatType::Default);
			break;
		}
		long low = std::atol(second.c_str()), high = std::atol(first.c_str());
		if (low < 0 || high < 0) {
			ChatOut(format(13061, "Please use positive numbers.", {}), ChatType::Default);
			break;
		}
		if (high < low)
			std::swap(low, high);
		if (high > 32000) {
			ChatOut(format(13062, "Please use numbers <= 32000.", {}), ChatType::Default);
			break;
		}
		m_zone.Random(static_cast<uint32_t>(low), static_cast<uint32_t>(high));
		break;
	}
	case Command::LootNoDrop: {
		// /lootnodrop (@004f5c8b): whether looting a NO DROP item asks first: "never" (2),
		// "sometimes" (1: only what we can't use), "always" (0), or 2, 1, 0, by their first four
		// letters. Nothing typed says how to use it and what it is.
		static const char *const settings[] = {"Always display loot requestor for no-drop items.",
			"Display loot requestor for no-drop items I cannot use.", "Never display requestor for looting no-drop items."};
		int &setting = m_loot.loot_no_drop;
		if (setting < 0 || setting > 2)
			setting = 0;
		std::string always = format(12987, "always", {}), sometimes = format(12988, "sometimes", {}), never = format(12989, "never", {});
		if (args.empty()) {
			ChatOut(format(12990, "Usage:/lootnodrop <%1 | %2 | %3>", {always, sometimes, never}), ChatType::Default);
			ChatOut(format(12991, "Your Current Setting: ", {}) + format(12992 + static_cast<uint32_t>(setting), settings[setting], {}),
				ChatType::Default);
			break;
		}
		if (MatchesPrefix(args, never, 4) || args[0] == '2')
			setting = 2;
		else if (MatchesPrefix(args, sometimes, 4) || args[0] == '1')
			setting = 1;
		else if (MatchesPrefix(args, always, 4) || args[0] == '0')
			setting = 0;
		else
			break;
		m_config.loot_no_drop = setting;
		ChatOut(format(12995, "New Setting: ", {}) + format(12992 + static_cast<uint32_t>(setting), settings[setting], {}),
			ChatType::Default);
		break;
	}
	case Command::FastDrop: {
		// /fastdrop (@004f9222): "on" drops an item clicked onto the ground at once, "off" asks
		// first, "never" doesn't drop it at all; the whole word, any case.
		std::string word = Lower(args);
		int &setting = m_ground.fast_drop;
		if (word == Lower(format(12947, "on", {}))) {
			setting = 1;
			ChatOut(format(13116, "Fast Drop is now *ON*.  You will drop items without confirmation.", {}), ChatType::Default);
		}
		else if (word == Lower(format(12946, "off", {}))) {
			setting = 0;
			ChatOut(format(13117, "Fast Drop is now *OFF*.  A confirmation requestor will be displayed before items are dropped.", {}),
				ChatType::Default);
		}
		else if (word == Lower(format(12989, "never", {}))) {
			setting = -1;
			ChatOut(format(13118, "You have chosen to NEVER drop items.", {}), ChatType::Default);
		}
		else {
			ChatOut(format(13119, "Please specify if you want to turn fast dropping of items 'on' or 'off', or, if you don't want "
				"to drop items at all, specify 'never'.", {}), ChatType::Default);
			break;
		}
		m_config.fast_drop = setting;
		break;
	}
	case Command::Social: {
		// CEverQuest_send_social (@00532ddd): "/smile [name] [text]". The first word picks who it's at
		// (EQPlayer_get_player_partial_name; nothing, or eqstr 12295, our target), not us and not past
		// 60; anything else typed is said as it is ("You emote '...'").
		int social = 14; // /gesture, /flipoff, /bird and /finger are /rude
		for (int i = 0; i < static_cast<int>(std::size(SocialNames)); ++i)
			if (std::string(name) == SocialNames[i])
				social = i;
		const SocialEntry &entry = Socials[social];
		std::string text;
		std::string who = FirstWord(args, text);
		uint16_t at = Target();
		if (!who.empty() && Lower(who) != Lower(format(12295, "say", {}))) {
			std::string want = Lower(who);
			for (auto &c : want)
				if (c == ' ')
					c = '_';
			at = 0;
			if (Lower(me.name).compare(0, want.size(), want) == 0)
				at = m_zone.MySpawnId();
			else {
				const ZoneMovement &here = m_zone_view ? m_zone_view->Movement() : ZoneMovement{};
				float best_d = 0;
				for (const auto &[spawn_id, spawn] : spawns) {
					if (Lower(spawn.name).compare(0, want.size(), want) != 0)
						continue;
					float d = std::hypot(spawn.x - here.x, spawn.y - here.y);
					if (at == 0 || d < best_d) {
						at = spawn_id;
						best_d = d;
					}
				}
			}
			if (at == 0) {
				ChatOut(format(12296, "I don't see anyone by that name around here...", {}), ChatType::Default);
				break;
			}
		}
		if (at == m_zone.MySpawnId())
			at = 0;
		const ZoneSpawn *them = nullptr;
		if (at != 0) {
			auto it = spawns.find(at);
			them = it != spawns.end() ? &it->second : nullptr;
			if (!them)
				at = 0;
			else if (m_zone.SpawnDistance(at) > 60.0f) {
				ChatOut(format(12321, "I don't see anyone by that name around here...", {}), ChatType::Default);
				break;
			}
		}
		// he, him, his by gender (0 male, 1 female, else it).
		auto pronouns = [&](int gender, std::string out[3]) {
			out[0] = format(gender == 0 ? 12209 : gender == 1 ? 12210 : 12211, gender == 0 ? "he" : gender == 1 ? "she" : "it", {});
			out[1] = gender == 0 ? format(12297, "him", {}) : gender == 1 ? format(12298, "her", {}) : format(12211, "it", {});
			out[2] = format(gender == 0 ? 12301 : gender == 1 ? 12299 : 12300, gender == 0 ? "his" : gender == 1 ? "her" : "its", {});
		};
		std::string mine[3], theirs[3];
		pronouns(me.gender, mine);
		if (them)
			pronouns(them->gender, theirs);
		std::string target_name = them ? them->DisplayName() : "";
		auto build = [&](const SocialText &piece, const std::string &name, const std::string (&ours)[3]) {
			if (piece.id == 0)
				return std::string();
			std::vector<std::string> values;
			for (const char *a = piece.args; *a; ++a) {
				switch (*a) {
				case 'N': values.push_back(name); break;
				case 'S': values.push_back(ours[0]); break;
				case 'O': values.push_back(ours[1]); break;
				case 'P': values.push_back(ours[2]); break;
				case 'T': values.push_back(target_name); break;
				case 's': values.push_back(theirs[0]); break;
				case 'o': values.push_back(theirs[1]); break;
				case 'p': values.push_back(theirs[2]); break;
				}
			}
			return format(piece.id, "", values);
		};
		// A full stop after a letter or digit at the end.
		auto stop = [](std::string &line) {
			if (!line.empty() && std::isalnum(static_cast<unsigned char>(line.back())))
				line += ".";
		};
		int animation = entry.animation;
		const ZoneMovement &moving = m_zone_view ? m_zone_view->Movement() : ZoneMovement{};
		if (moving.speed != 0 || m_zone.Self().stance != 100)
			animation = -1; // only standing still
		std::string lone = build(entry.sent, "", mine), sent = build(entry.sent_target, "", mine);
		if (text.size() >= 2)
			sent = " " + text + ".";
		else if (at == 0 && !lone.empty())
			sent = " " + lone;
		if (!sent.empty()) {
			stop(sent);
			sent = PercentConvert(sent);
			m_zone.Emote(sent, 0xffff); // send_social's Emote_Struct has 0xffff in front
		}
		if (animation != -1)
			m_zone.SendAnimation(animation);
		// The echo, in the emote color.
		const std::string you[3] = {format(12303, "you", {}), format(12303, "you", {}), format(12304, "your", {})};
		std::string you_name = format(12302, "You", {});
		lone = build(entry.echo, you_name, you);
		std::string echo = build(entry.echo_target, you_name, you);
		if (text.size() >= 2)
			echo = format(12305, "You emote '%1'", {text});
		else if (at == 0 && !lone.empty())
			echo = lone;
		if (animation != -1 && m_zone_view)
			m_zone_view->PlayAnimation(animation);
		if (!echo.empty()) {
			stop(echo);
			ChatOut(echo, ChatType::Emote);
		}
		break;
	}
	case Command::Who:
		Who(args);
		break;
	case Command::Follow:
		Follow();
		break;
	case Command::MakeLeader:
		m_group.MakeLeader(args);
		break;
	case Command::Channel: {
		// /channel (@004f799f): the chat window's default channel for what's typed without a
		// command, by name or its short form; a tell's or chat channel's name after it.
		std::string rest = args;
		std::string word = GrabFirstWord(rest).substr(0, 63);
		std::string to = GrabFirstWord(rest).substr(0, 31);
		if (word.empty()) {
			ChatOut(format(13080, "Usage: /channel <channel>", {}), ChatType::Default);
			ChatOut(format(13081, "<channel> can be one of: guildsay, gsay, tell, auction, ooc, or chat.", {}),
				ChatType::Default);
			ChatOut(format(13082, "If you specify tell then follow it with the name of the person to tell. (e.g. /channel tell Fippy)",
				{}), ChatType::Default);
			ChatOut(format(13083, "If you specify chat, follow it with the name of the channel. (e.g. /channel chat mychannel)",
				{}), ChatType::Default);
			break;
		}
		struct Choice
		{
			uint32_t id;
			const char *text, *brief;
			uint8_t channel;
		};
		static const Choice choices[] = {
			{13070, "GuildSay", "gu", 0}, {13071, "gsay", "g", 2}, {5111, "rsay", "rs", 15}, {13072, "tell", "t", 7},
			{13074, "Auction", "auc", 4}, {13075, "OOC", nullptr, 5}, {13076, "Say", nullptr, 8}, {13077, "chat", nullptr, 19},
		};
		const Choice *picked = nullptr;
		for (const auto &choice : choices)
			if (Lower(word) == Lower(format(choice.id, choice.text, {})) || (choice.brief && Lower(word) == choice.brief)) {
				picked = &choice;
				break;
			}
		if (!picked) {
			// An unknown name: the usage again.
			ChatOut(format(13080, "Usage: /channel <channel>", {}), ChatType::Default);
			ChatOut(format(13081, "<channel> can be one of: guildsay, gsay, tell, auction, ooc, or chat.", {}),
				ChatType::Default);
			ChatOut(format(13082, "If you specify tell then follow it with the name of the person to tell. (e.g. /channel tell Fippy)",
				{}), ChatType::Default);
			ChatOut(format(13083, "If you specify chat, follow it with the name of the channel. (e.g. /channel chat mychannel)",
				{}), ChatType::Default);
			break;
		}
		std::string shown;
		m_chat_channel = picked->channel;
		if (picked->channel == 7 || picked->channel == 19) {
			m_chat_channel_to = to;
			shown = picked->channel == 7 ? format(13073, "Tell to %1", {to}) : format(13078, "Chat to Channel %1", {to});
		}
		else
			shown = format(picked->id, picked->text, {});
		ChatOut(format(13079, "Your default channel for typing is now %1.", {shown}), ChatType::Default);
		break;
	}
	// The raid's commands (@004f3b4f..@004f3c95): only with the Planes of Power (Expansions 8) or on
	// a test server (@00799760).
	case Command::MakeRaidLeader:
	case Command::RaidInvite:
	case Command::RaidDecline:
	case Command::RaidAccept:
	case Command::RaidDisband:
	case Command::SetLootType:
	case Command::AddRaidLooter:
	case Command::RemoveRaidLooter:
	case Command::RaidSay:
	case Command::RaidWindow:
		if (!RaidAllowed() && !by_channel)
			break;
		switch (command) {
		case Command::RaidWindow: // @004f3b0a: CRaidWnd::Activate, or down
			m_raid_window.Toggle();
			break;
		case Command::MakeRaidLeader: {
			// The target's name with nothing typed, else the name typed (EQUtil_FormatCharacterName).
			std::string who;
			if (args.empty() && target != spawns.end())
				who = target->second.name;
			else
				for (size_t i = 0; i < args.size() && i < 63; ++i)
					who += static_cast<char>(i == 0 ? std::toupper(static_cast<unsigned char>(args[i]))
					                                : std::tolower(static_cast<unsigned char>(args[i])));
			m_raid.MakeLeader(who);
			break;
		}
		case Command::RaidInvite:
			m_raid.Invite();
			break;
		case Command::RaidDecline:
			m_raid.Respond(false);
			break;
		case Command::RaidAccept:
			m_raid.Respond(true);
			break;
		case Command::RaidDisband:
			m_raid.Disband();
			break;
		case Command::SetLootType:
			m_raid.SetLootType(args);
			break;
		case Command::AddRaidLooter:
			m_raid.AddLooter();
			break;
		case Command::RemoveRaidLooter:
			m_raid.RemoveLooter();
			break;
		default: // CRaid_SendRaidChat
			if (!m_raid.InRaid())
				ChatOut(format(5108, "You cannot send chat with the raid chat channel unless you are in a raid.", {}),
					ChatType::Raid);
			else
				send(15, "", args, 5109, "You tell your raid, '%1'", ChatType::Raid);
			break;
		}
		break;
	case Command::GuildInvite: {
		// /guildinvite (@004f4b68): GrabFirstWord's word and what's left.
		std::string rest = args;
		std::string word = GrabFirstWord(rest);
		m_guild.Invite(word, rest);
		break;
	}
	case Command::GuildRemove:
		m_guild.Remove(args);
		break;
	case Command::GuildDelete:
		m_guild.Delete();
		break;
	case Command::GuildStatus:
		m_guild.Status(args);
		break;
	case Command::GuildLeader:
		m_guild.Leader(args);
		break;
	case Command::GuildWar: {
		// /guildwar (@004f4add): a name, then the days.
		std::string rest = args;
		std::string word = GrabFirstWord(rest);
		m_guild.War(word, rest);
		break;
	}
	case Command::GuildPeace:
		m_guild.Peace(args);
		break;
	case Command::GuildMotd:
		m_guild.Motd(args);
		break;
	case Command::Lfg:
		Lfg(true, args);
		break;
	case Command::WhoTarget:
		// /whotarget (@004fd2fa): /who of the target's name.
		if (target != spawns.end())
			Who(target->second.name);
		break;
	case Command::Invite:
		// /invite (@004fd38d): an invitation we have accepted, else CEverQuest_Invite.
		if (!allow())
			break;
		if (m_group.Invited())
			m_group.Accept();
		else
			m_group.Invite();
		break;
	case Command::Disband:
		// /disband (@004fd3c5).
		if (allow())
			m_group.Disband();
		break;
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
