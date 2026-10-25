#pragma once

// What eqgame prints in the chat, and in which colors.
//   - ChatColors is CChatManager::GetRGBAFromIndex: a type under 256 is a fixed color; 256 + n is
//     the player's text color n, User_<n+1> in my-client.ini's [TextColors] (as eqgame's
//     loadUserDefinedColors reads them from eqclient.ini), or else GetDefaultUserDefinedColor's built-in one.
//   - ChannelLine is msg_new_text (@004e25a1): an OP_ChannelMessage as a line of chat.

#include <cstdint>
#include <string>
#include <vector>

class EqStr;
struct ZoneChat;

// The color types eqgame prints with (print_chat_dsp_chat's second argument).
namespace ChatType
{
	constexpr int White = 0;        // the fixed colors (Chat::White and so on, as the server uses them)
	constexpr int Red = 13;
	constexpr int Yellow = 15;
	constexpr int Say = 0x100;      // the player's colors from here: User_1
	constexpr int Tell = 0x101;
	constexpr int Group = 0x102;
	constexpr int Guild = 0x103;
	constexpr int Ooc = 0x104;
	constexpr int Auction = 0x105;
	constexpr int Shout = 0x106;
	constexpr int Emote = 0x107;
	constexpr int Spells = 0x108;   // scribing and memorizing
	constexpr int Broadcast = 0x10d;
	constexpr int Skills = 0x10e;   // "You have become better at..."
	constexpr int Default = 0x111;  // DefaultText
	constexpr int EchoSay = 0x133;
	constexpr int EchoTell = 0x134;
	constexpr int EchoGroup = 0x135;
	constexpr int EchoGuild = 0x136;
	constexpr int EchoOoc = 0x137;
	constexpr int EchoAuction = 0x138;
	constexpr int EchoShout = 0x139;
	constexpr int EchoEmote = 0x13a;
	constexpr int Raid = 0x147;
}

class ChatColors
{
public:
	static constexpr int UserColors = 72;

	ChatColors(); // the built-in colors
	// my-client.ini's [TextColors], over the built-in ones; false if the file can't be read.
	bool Load(const std::string &settings_ini);
	uint32_t Get(int type) const; // 0xRRGGBB

private:
	uint32_t m_user[UserColors];
};

// A channel message as eqgame prints it, and its color type; false for one it doesn't print (our
// own, which the zone sends back to us too, except for the "You told" of a tell).
// `languages` is our skill in each language (the profile's).
bool ChannelLine(const ZoneChat &chat, const std::string &me, const uint8_t *languages, const EqStr *strings,
	std::string &line, int &type);

// CEverQuest_GetLangDesc: the eqstr_en.txt id of a language's name (0..24), 0 for any other.
uint32_t LanguageNameId(int language);

// eqstr_en.txt's string `id` with %1.. filled in, or `fallback` (in the same form) without the table.
std::string FormatEqString(const EqStr *strings, uint32_t id, const std::string &fallback, const std::vector<std::string> &args);
