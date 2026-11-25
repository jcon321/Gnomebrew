#include "disciplines.h"
#include "chat_text.h"

#include <cctype>

namespace
{
	struct Entry
	{
		uint32_t word;       // eqstr: the word's start
		const char *english;
		size_t letters;      // of it that must match
		int32_t id;          // OP_Discipline's
		int level;
	};

	constexpr Entry Warrior[] = {
		{1289, "aggr", 4, 1, 60}, {1281, "resi", 4, 30, 30}, {1282, "fearl", 5, 31, 40}, {1290, "prec", 4, 2, 57},
		{1291, "defe", 4, 3, 55}, {1292, "evas", 4, 4, 52}, {1293, "fur", 3, 6, 56}, {1294, "char", 4, 16, 53},
		{1295, "mig", 3, 17, 54}, {1296, "fel", 3, 14, 58}, {1297, "for", 3, 13, 59},
	};
	constexpr Entry Monk[] = {
		{1280, "sile", 4, 20, 59}, {1281, "resi", 4, 30, 30}, {1282, "fearl", 5, 31, 40}, {1283, "ash", 3, 5, 60},
		{1284, "ston", 4, 11, 51}, {1285, "thun", 4, 12, 52}, {1286, "whir", 4, 6, 53}, {1287, "void", 4, 13, 54},
		{1288, "inne", 4, 14, 56}, {1298, "hund", 4, 15, 57},
	};
	constexpr Entry Rogue[] = {
		{1299, "cou", 3, 6, 53}, {1281, "resi", 4, 30, 30}, {1282, "fearl", 5, 31, 40}, {1300, "nimb", 4, 19, 55},
		{1301, "blin", 4, 15, 58}, {1302, "duel", 4, 14, 52}, {1303, "dead", 4, 16, 54}, {1304, "kine", 4, 21, 57},
	};
	constexpr Entry Paladin[] = {
		{1305, "hol", 3, 22, 55}, {1306, "san", 3, 23, 60}, {1281, "resi", 4, 30, 51}, {1282, "fearl", 5, 31, 54},
	};
	constexpr Entry Beastlord[] = {
		{1281, "resi", 4, 30, 51}, {1282, "fearl", 5, 31, 54}, {1307, "prot", 4, 11, 55}, {1308, "beast", 5, 14, 60},
	};
	constexpr Entry Ranger[] = {
		{1309, "tru", 3, 24, 55}, {1281, "resi", 4, 30, 51}, {1282, "fearl", 5, 31, 54}, {1310, "wea", 3, 25, 60},
	};
	constexpr Entry ShadowKnight[] = {
		{1311, "unh", 3, 26, 55}, {1281, "resi", 4, 30, 51}, {1282, "fearl", 5, 31, 54}, {1312, "lee", 3, 27, 60},
	};
	constexpr Entry Bard[] = {
		{1313, "def", 3, 28, 55}, {1281, "resi", 4, 30, 51}, {1282, "fearl", 5, 31, 54}, {1314, "pur", 3, 29, 60},
	};

	struct ClassDisciplines
	{
		int class_id;
		uint32_t usage;
		const char *english;
		const Entry *entries;
		size_t count;
	};
	template <size_t N>
	constexpr ClassDisciplines Of(int class_id, uint32_t usage, const char *english, const Entry (&entries)[N])
	{
		return {class_id, usage, english, entries, N};
	}
	constexpr ClassDisciplines Classes[] = {
		Of(1, 12996, "Usage:/discipline <resistant|fearless|evasive|charge|mightystrike|defensive|furious|precise|fellstrike|fortitude|aggressive>.", Warrior),
		Of(15, 12997, "Usage:/discipline <resistant|fearless|protectivespirit|beastialrage>.", Beastlord),
		Of(7, 12998, "Usage:/discipline <resistant|fearless|stonestance|thunderkick|whirlwind|voiddance|innerflame|hundredfist|silentfist|ashenhand>", Monk),
		Of(9, 12999, "Usage:/discipline <resistant|fearless|counterattack|deadeye|nimble|kinesthetics|blindingspeed|duelist>.", Rogue),
		Of(3, 13000, "Usage:/discipline <resistant|fearless|holyforge|sanctification>.", Paladin),
		Of(4, 13001, "Usage:/discipline <resistant|fearless|trueshot|weaponshield>.", Ranger),
		Of(5, 13002, "Usage:/discipline <resistant|fearless|unholyaura|leechcurse>.", ShadowKnight),
		Of(8, 13003, "Usage:/discipline <resistant|fearless|deftdance|puretone>.", Bard),
	};

	// strnicmp(a, b, n) == 0.
	bool SameStart(const std::string &a, const std::string &b, size_t n)
	{
		for (size_t i = 0; i < n; ++i) {
			unsigned char x = i < a.size() ? a[i] : 0, y = i < b.size() ? b[i] : 0;
			if (std::tolower(x) != std::tolower(y))
				return false;
			if (x == 0)
				return true;
		}
		return true;
	}
}

Disciplines::Result Disciplines::Command(const EqStr *strings, int class_id, int level, uint32_t expansions, const std::string &typed)
{
	Result result;
	// Nothing at all without an expansion: a hybrid's want Kunark or later, a beastlord's Luclin.
	bool hybrid = class_id == 3 || class_id == 4 || class_id == 5 || class_id == 8;
	if (expansions == 0 || (hybrid && !(expansions & 0x1e)) || (class_id == 15 && !(expansions & 4)))
		return result;
	const ClassDisciplines *ours = nullptr;
	for (const ClassDisciplines &c : Classes)
		if (c.class_id == class_id)
			ours = &c;
	if (!ours) {
		result.message = 12976;
		result.text = "This is a Warrior/Monk/Rogue/Paladin/Ranger/Shadowknight/Bard/Beastlord only ability.";
		return result;
	}
	if (typed.empty()) {
		result.message = ours->usage;
		result.text = ours->english;
		result.discipline = 0;
		return result;
	}
	const Entry *entry = nullptr;
	for (size_t i = 0; i < ours->count && !entry; ++i)
		if (SameStart(typed, FormatEqString(strings, ours->entries[i].word, ours->entries[i].english, {}), ours->entries[i].letters))
			entry = &ours->entries[i];
	if (!entry) {
		result.message = ours->usage;
		result.text = ours->english;
	}
	else if (level < entry->level) {
		result.message = 13004;
		result.text = "You are not sufficient level to use this discipline.";
	}
	else
		result.discipline = entry->id;
	return result;
}
