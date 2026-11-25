#pragma once

#include "../core/eqstr.h"

#include <cstdint>
#include <string>

// /discipline (@004f4fb5): the disciplines a class has, by the words of eqstr 1280..1314.
namespace Disciplines
{
	struct Result
	{
		// What to say (0 nothing) and its English: the class's usage (12996..13003), 12976 for a class
		// with none, 13004 under the level.
		uint32_t message = 0;
		const char *text = "";
		// What to ask the zone for (OP_Discipline); -1 nothing. 0, with nothing typed, asks how long
		// until the next (the usage is said too).
		int32_t discipline = -1;
	};

	Result Command(const EqStr *strings, int class_id, int level, uint32_t expansions, const std::string &typed);
}
