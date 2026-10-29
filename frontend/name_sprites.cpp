#include "name_sprites.h"
#include "eqstr.h"

#include <algorithm>

namespace
{
	constexpr uint8_t Player = 0, Npc = 1;

	// eqstr_en.txt
	constexpr uint32_t LinkdeadString = 2240;  // " LD"
	constexpr uint32_t AfkString = 12311;      // " AFK "
	constexpr uint32_t LfgString = 12314;      // " LFG"

	std::string String(const EqStr *strings, uint32_t id, const char *fallback)
	{
		return strings && strings->Has(id) ? strings->Format(id, {}) : fallback;
	}

	// What each /shownames level shows: eqgame's 1..4, and Zeal's 5..7 (title and first name;
	// title, first and last; first and guild).
	bool ShowsTitle(int level) { return level == 4 || level == 5 || level == 6; }
	bool ShowsSurname(int level) { return level == 2 || level == 3 || level == 4 || level == 6; }
	bool ShowsGuild(int level) { return level == 3 || level == 4 || level == 7; }

	// CEverQuest_GetTitleDesc (@0052eb69): the AA title for a class, title rank (1..3) and
	// gender (0 male, 1 female); 0 for none.
	uint32_t TitleString(uint8_t class_id, uint8_t rank, uint8_t gender)
	{
		if (gender > 1)
			return 0;
		bool female = gender == 1;
		switch (rank) {
		case 1:
			return female ? 2415 : 2414; // Baron, Baroness
		case 2:
			switch (class_id) {
			case 1: case 4: case 8: case 9: return 2418;      // Veteran
			case 2: case 6: case 10: case 15: return 2417;    // Venerable
			case 3: case 5: return female ? 2422 : 2416;      // Sir, Lady
			case 7: return female ? 2424 : 2421;              // Brother, Sister
			case 11: return 2420;                             // Sage
			case 12: case 13: case 14: return female ? 2423 : 2419; // Master, Mistress
			default: return 0;
			}
		case 3:
			switch (class_id) {
			case 1: return 2429;                              // Marshall
			case 2: return 2436;                              // Exarch
			case 3: case 5: return female ? 2426 : 2425;      // Duke, Duchess
			case 4: return female ? 2431 : 2430;              // Hunter, Huntress
			case 6: case 10: case 15: return 2427;            // Elder
			case 7: return 2435;                              // Sensei
			case 8: return female ? 2433 : 2432;              // Impresario, Muse
			case 9: return 2428;                              // Marauder
			case 11: return 2434;                             // Lich
			case 12: case 13: case 14: return 2420;           // Sage
			default: return 0;
			}
		default:
			return 0;
		}
	}

	// CEverQuest::trimName: underscores become spaces; digits and '#' go.
	std::string Trim(const std::string &name)
	{
		ZoneSpawn trimmed;
		trimmed.name = name;
		return trimmed.DisplayName();
	}

	uint32_t Channel(uint32_t color, int shift) { return (color >> shift) & 0xff; }
}

std::string NameSpriteText(const ZoneSpawn &spawn, int show_names, const GuildNames &guilds, const EqStr *strings,
	bool targeted)
{
	// Ents (race 244) never show a name, as in eqgame; as Zeal has it, corpses and the target
	// always do. (eqgame also hides skeletons', gargoyles' and mimics' names in some animations.)
	if (spawn.npc == Npc && spawn.race == 244 && !targeted)
		return "";
	if (show_names < 1 && spawn.npc != Npc)
		return "";
	if (spawn.npc != Player)
		return spawn.DisplayName();

	// Anonymous players keep their guild to themselves; roleplaying ones don't.
	bool guild_shown = spawn.anon == 0 || spawn.anon > 1;
	std::string text;
	if (spawn.aa_title > 0 && spawn.gender != 2 && ShowsTitle(show_names)) {
		uint32_t title = TitleString(spawn.class_id, spawn.aa_title, spawn.gender);
		if (title != 0)
			text += String(strings, title, "");
		text += " ";
	}
	text += spawn.invis == 1 ? "(" + Trim(spawn.name) + ")" : Trim(spawn.name);
	// As Zeal has it: no space for a surname they haven't got, nor after AFK (eqstr's " AFK ").
	if (ShowsSurname(show_names) && !spawn.surname.empty())
		text += " " + Trim(spawn.surname);
	if (spawn.lfg)
		text += String(strings, LfgString, " LFG");
	if (!spawn.pvp) {
		if (spawn.afk) {
			text += String(strings, AfkString, " AFK");
			while (!text.empty() && text.back() == ' ')
				text.pop_back();
		}
		if (spawn.linkdead)
			text += String(strings, LinkdeadString, " LD");
	}
	while (!text.empty() && text.back() == ' ')
		text.pop_back();
	if (ShowsGuild(show_names) && guild_shown && spawn.guild_id != -1) {
		auto it = guilds.find(spawn.guild_id);
		text += "\n<" + (it != guilds.end() && !it->second.empty() ? it->second : std::string("Unknown Guild")) + ">";
	}
	return text;
}

uint32_t NameSpriteColor(const ZoneSpawn &spawn, uint32_t ruleset)
{
	bool roleplaying = spawn.anon > 1;
	if ((spawn.gm & 1) && spawn.npc != Npc && spawn.anon == 0)
		return 0x1da64b; // a GM
	if (spawn.npc != Player)
		return 0x17b8ad; // NPCs and corpses
	if (spawn.pvp)
		return roleplaying ? 0xef5d5d : 0xa61414;
	if (roleplaying && (ruleset == 0 || ruleset == 4)) // roleplayers keep their team's color on the team servers
		return 0x85489c;
	if (ruleset == 2) {
		// The race teams.
		switch (spawn.race) {
		case 4: case 5: case 7: return 0xffff00;                          // elves
		case 8: case 11: case 12: return 0x85489c;                        // the short races
		case 6: case 9: case 10: case 128: return 0xff8000;               // the evil races
		default: return 0x3d6bdc;                                         // humans, barbarians, erudites...
		}
	}
	return 0x3d6bdc;
}

uint32_t NameSpritePulse(uint32_t color, int step)
{
	auto pulse = [step](uint32_t c) {
		return static_cast<uint32_t>(std::max(0, 0xff - static_cast<int>(0xff - c) / 10 * step));
	};
	return pulse(Channel(color, 16)) << 16 | pulse(Channel(color, 8)) << 8 | pulse(Channel(color, 0));
}

uint32_t NameSpriteLit(uint32_t color, float red, float green, float blue)
{
	// Each channel's light as a byte, anything out of 0..255 full; the tint's byte times it, >> 8.
	auto lit = [](uint32_t c, float light) {
		int level = static_cast<int>((light + NameSprite::LightLift) * 255);
		if (level < 0 || level > 0xff)
			level = 0xff;
		return c * static_cast<uint32_t>(level) >> 8;
	};
	return lit(Channel(color, 16), red) << 16 | lit(Channel(color, 8), green) << 8 | lit(Channel(color, 0), blue);
}
