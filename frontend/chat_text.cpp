#include "chat_text.h"
#include "eqstr.h"
#include "ini_file.h"
#include "zone_session.h"

#include <algorithm>
#include <cctype>

namespace
{
	// GetDefaultUserDefinedColor's table (eqgame.exe 0x602730): User_1 .. User_72. Black means
	// "the default", which GetRGBAFromIndex shows as 0xf0f0f0.
	constexpr uint32_t DefaultUserColors[ChatColors::UserColors] = {
		0x000000, 0x800080, 0x00ffff, 0x28f028, 0x008000, 0x008000, 0xff0000, 0x000080,
		0x000080, 0xffffff, 0xff0000, 0xffffff, 0xffffff, 0xffff00, 0x000080, 0x000000,
		0x000000, 0x000000, 0x000000, 0xf0f000, 0xf0f000, 0x000000, 0x000000, 0x000000,
		0x000000, 0x800080, 0x000000, 0x000080, 0xf0f000, 0x008c00, 0x000080, 0xff0000,
		0x000080, 0xff0000, 0xd79a42, 0x6e8fb0, 0x6e8fb0, 0x6e8fb0, 0x6e8fb0, 0x6e8fb0,
		0x6e8fb0, 0x6e8fb0, 0x6e8fb0, 0x6e8fb0, 0x6e8fb0, 0xffffff, 0xffffff, 0xff0000,
		0xff0000, 0xff0000, 0xff0000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
		0x000000, 0x000000, 0x000000, 0xd79a42, 0xd79a42, 0xd79a42, 0xd79a42, 0xd79a42,
		0xd79a42, 0xd79a42, 0xd79a42, 0xd79a42, 0xd79a42, 0xffff00, 0xff00ff, 0x00c8c8,
	};

	// CEverQuest_GetLangDesc: eqstr_en.txt ids of the languages' names.
	constexpr uint32_t LanguageNames[] = {3114, 3200, 3201, 3202, 3203, 3204, 3205, 3206, 3207, 3208, 3217, 3219, 3220,
		3209, 3210, 3211, 3212, 3213, 3214, 3215, 3216, 3218, 3221, 3222, 3223};

	// CEverQuest::trimName: an NPC's name without its underscores and number.
	std::string TrimName(const std::string &name)
	{
		std::string out;
		for (char c : name) {
			if (std::isdigit(static_cast<unsigned char>(c)))
				continue;
			out.push_back(c == '_' ? ' ' : c);
		}
		return out;
	}

	bool EndsInDigit(const std::string &name)
	{
		return !name.empty() && std::isdigit(static_cast<unsigned char>(name.back()));
	}
}

ChatColors::ChatColors()
{
	for (int i = 0; i < UserColors; ++i)
		m_user[i] = DefaultUserColors[i];
}

bool ChatColors::Load(const std::string &settings_ini)
{
	IniFile ini;
	if (!ini.Load(settings_ini))
		return false;
	// loadUserDefinedColors: all three of a color, or the default.
	for (int i = 0; i < UserColors; ++i) {
		std::string key = "User_" + std::to_string(i + 1) + "_";
		if (!ini.Has("TextColors", key + "Red") || !ini.Has("TextColors", key + "Green") || !ini.Has("TextColors", key + "Blue"))
			continue;
		auto channel = [&](const char *name) { return static_cast<uint32_t>(ini.GetInt("TextColors", key + name, 0) & 0xff); };
		m_user[i] = channel("Red") << 16 | channel("Green") << 8 | channel("Blue");
	}
	return true;
}

uint32_t ChatColors::Get(int type) const
{
	if (type > 0xff) {
		int index = type - 0x100;
		uint32_t color = index >= 0 && index < UserColors ? m_user[index] : m_user[0];
		return color == 0 ? 0xf0f0f0 : color;
	}
	switch (type) {
	case 0: case 10: return 0xf0f0f0;
	case 2: return 0x008000;
	case 4: return 0x000080;
	case 5: return 0xf000f0;
	case 6: return 0x808080;
	case 7: return 0xe0e0e0;
	case 12: return 0xa0a0a0;
	case 13: return 0xf00000;
	case 14: return 0x00f000;
	case 15: return 0xf0f000;
	case 16: return 0x0000f0;
	case 18: return 0x00f0f0;
	case 20: return 0x000000;
	default: return 0x606060;
	}
}

std::string FormatEqString(const EqStr *strings, uint32_t id, const std::string &fallback, const std::vector<std::string> &args)
{
	if (strings && strings->Has(id))
		return strings->Format(id, args);
	std::string out;
	for (size_t i = 0; i < fallback.size(); ++i) {
		if (fallback[i] == '%' && i + 1 < fallback.size() && fallback[i + 1] >= '1' && fallback[i + 1] <= '9') {
			size_t arg = static_cast<size_t>(fallback[i + 1] - '1');
			if (arg < args.size())
				out += args[arg];
			++i;
		}
		else
			out.push_back(fallback[i]);
	}
	return out;
}

bool ChannelLine(const ZoneChat &chat, const std::string &me, const uint8_t *languages, const EqStr *strings,
	std::string &line, int &type)
{
	auto format = [&](uint32_t id, const char *fallback, const std::vector<std::string> &args) {
		return FormatEqString(strings, id, fallback, args);
	};
	bool tell = chat.channel == 7 || chat.channel == 14;
	std::string sender = chat.sender;
	if (!me.empty() && sender.size() == me.size() && std::equal(sender.begin(), sender.end(), me.begin(),
		[](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); })) {
		// Ours, back from the zone: we printed it when we sent it, except a tell's "You told".
		if (!tell)
			return false;
		std::string target = EndsInDigit(chat.target) ? TrimName(chat.target) : chat.target;
		line = format(1400, "You told %1, '%2'", {target, chat.text});
		type = ChatType::EchoTell;
		return true;
	}

	// " in Elvish," when it isn't the common tongue, or " in an unknown tongue," below 25 skill.
	std::string language;
	if (chat.language != 0) {
		if (chat.language < 32 && languages && languages[chat.language] < 25 && chat.channel != 14)
			language = " " + format(1412, "in an unknown tongue,", {});
		else {
			uint32_t name = chat.language < sizeof(LanguageNames) / sizeof(LanguageNames[0]) ? LanguageNames[chat.language] : 3115;
			language = " " + format(1411, "in %1,", {format(name, "a strange tongue", {})});
		}
	}
	const std::string &text = chat.text;
	switch (chat.channel) {
	case 0:
	case 17:
		line = format(1415, "%1 tells the guild,%2 '%3'", {sender, language, text});
		type = ChatType::Guild;
		break;
	case 2:
		line = format(1414, "%1 tells the group,%2 '%3'", {sender, language, text});
		type = ChatType::Group;
		break;
	case 3:
		line = format(1420, "%1 shouts,%2 '%3'", {sender, language, text});
		type = ChatType::Shout;
		break;
	case 4:
		line = format(1421, "%1 auctions,%2 '%3'", {sender, language, text});
		type = ChatType::Auction;
		break;
	case 5:
		line = format(1422, "%1 says out of character, '%2'", {sender, text});
		type = ChatType::Ooc;
		break;
	case 6:
		line = format(1413, "%1 BROADCASTS, '%2'", {sender, text});
		type = ChatType::Broadcast;
		break;
	case 7:
	case 14:
		line = format(1416, "%1 tells you,%2 '%3'", {sender, language, text});
		type = ChatType::Tell;
		break;
	case 10:
		line = format(1423, "%1 petitions, '%2'", {sender, text});
		type = ChatType::Yellow;
		break;
	case 11:
	case 18:
		line = format(1425, "%1 GMSAYS, '%2'", {sender, text});
		type = ChatType::Yellow;
		break;
	case 15:
		line = format(5112, "%1 tells the raid, %2 '%3'", {sender, language, text});
		type = ChatType::Raid;
		break;
	case 8:
		line = format(1410, "%1 says,%2 '%3'", {TrimName(sender), language, text});
		type = ChatType::Say;
		break;
	default:
		line = format(1410, "%1 says,%2 '%3'", {sender, language, text});
		type = ChatType::Default;
		break;
	}
	return true;
}
