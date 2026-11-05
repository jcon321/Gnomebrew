#include "filters.h"

#include "ini_file.h"

#include <cstdlib>
#include <cstring>

namespace
{
	struct Field
	{
		const char *key;
		int32_t ChatFilters::*value;
	};
	// In UpdateFilters_Struct's order, then the bad word filter.
	const Field Fields[] = {
		{"DamageShields", &ChatFilters::damage_shields}, {"NpcSpells", &ChatFilters::npc_spells},
		{"PcSpells", &ChatFilters::pc_spells}, {"BardSongs", &ChatFilters::bard_songs},
		{"ServerFilter", &ChatFilters::server_filter}, {"GuildSay", &ChatFilters::guild},
		{"Socials", &ChatFilters::socials}, {"GroupSay", &ChatFilters::group}, {"Shouts", &ChatFilters::shouts},
		{"Auctions", &ChatFilters::auctions}, {"Ooc", &ChatFilters::ooc}, {"MyMisses", &ChatFilters::my_misses},
		{"OthersMisses", &ChatFilters::others_misses}, {"OthersHits", &ChatFilters::others_hits},
		{"AttackerMissesMe", &ChatFilters::misses_me}, {"CriticalSpells", &ChatFilters::critical_spells},
		{"CriticalMelee", &ChatFilters::critical_melee}, {"BadWords", &ChatFilters::bad_words},
	};
	constexpr int Sent = 17;
}

std::vector<uint8_t> ChatFilters::Packet() const
{
	std::vector<uint8_t> body(Sent * 4);
	for (int i = 0; i < Sent; ++i)
		std::memcpy(body.data() + i * 4, &(this->*Fields[i].value), 4);
	return body;
}

void ChatFilters::Load(const IniFile &ini)
{
	for (const Field &field : Fields) {
		std::string value = ini.Get("Filters", field.key);
		if (!value.empty())
			this->*field.value = std::atoi(value.c_str());
	}
}

void ChatFilters::Store(IniFile &ini) const
{
	for (const Field &field : Fields)
		ini.Set("Filters", field.key, std::to_string(this->*field.value));
}
