#pragma once

// eqgame's /filter switches (@004fbc3a), with CEverQuest_loadOptions' defaults. They go to the zone
// as OP_SetServerFilter (CEverQuest_send_update_filters, UpdateFilters_Struct's order below) when it
// gives us our spawn id (msg_stat_change's SpawnAppearance 0x10) and whenever one changes; the zone
// then leaves out what's filtered. eqgame keeps them in eqOptions1.opt and eqOptions2.opt; we keep
// them in my-client.ini's [Filters].

#include <cstdint>
#include <string>
#include <vector>

class IniFile;

struct ChatFilters
{
	int32_t damage_shields = 0; // DAT_00799670: 0 shown, 1 hidden
	int32_t npc_spells = 0;     // DAT_00799658: 0 shown, 1 hidden
	int32_t pc_spells = 0;      // DAT_00799654: 0 all, 1 off, 2 the group's
	int32_t bard_songs = 0;     // DAT_0079965c: 0 all, 1 ours, 2 the group's, 3 off
	int32_t server_filter = 0;  // DAT_00798590: /serverfilter
	// DAT_00798bf4..c18: 1 shown, 0 hidden.
	int32_t guild = 1, socials = 1, group = 1, shouts = 1, auctions = 1, ooc = 1;
	int32_t my_misses = 1, others_misses = 0, others_hits = 1, misses_me = 1;
	int32_t critical_spells = 0, critical_melee = 0; // DAT_0079967c, 80: 0 all, 1 ours, 2 off
	int32_t bad_words = 0; // DAT_00798b24: 0 the bad word filter on (not sent)

	std::vector<uint8_t> Packet() const; // UpdateFilters_Struct, 68 bytes
	void Load(const IniFile &ini);
	void Store(IniFile &ini) const;
};
