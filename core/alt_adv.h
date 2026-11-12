#pragma once

// Alternate advancement: eqgame's AltAdvManager (pinstAltAdvManager, made by
// AltAdvManager::AltAdvManager @004b74f9). The client keeps the whole table of 227 abilities in
// its binary (AltAdvEntry_initializeAltAdvancement @004b4c36, a 0x38-byte entry each, by the
// ability's id); the zone sends only our ranks in them (the profile's aa_array, OP_RespondAA) and
// our points. The ids are the client's ("Mac" AA ids); EQMacEmu translates them to its own.
//
// An entry, by its offset in eqgame's:
//   0x00, 0x04  the eqstr ids of a hot button's two words (CHotButtonWnd::OnProcessFrame); -1 none
//   0x08, 0x0c  its name and description
//   0x10        the level it opens at
//   0x12        its cost (the first rank's)
//   0x14, 0x16  what it requires: an ability and its rank; 0 nothing, -1 six points in the
//               general abilities, -2 twelve in the archetype ones (CheckAbilityRequirements)
//   0x18        its ranks
//   0x1a        each rank's cost for the general, archetype and class abilities (rank * it)
//   0x1c        whether it's in use at all
//   0x1e        a spell id (the activated abilities' spell; nothing here reads it)
//   0x20        set as it's first trainable (AltAdvManager_UpdateTrainableAbilities; the old UI's)
//   0x21        its page: 1 general, 2 archetype, 3 class, 4 PoP advance, 5 PoP ability
//   0x24        when it was last used, our time_t (updateAltAdvancementTimers)
//   0x28, 0x2a  the Planes of Power abilities' cost type and cost step (CalculateAbilityPointCost)
//   0x2c, 0x34  their levels: a rank per 0x2c levels, or 0x34 levels a rank (GetAbilityRequiredLevel)
//   0x2e        set on Innate Regeneration (14) only; nothing reads it here
//   0x30        its reuse time in seconds; 0 a passive ability

#include <cstdint>
#include <string>
#include <vector>

class EqStr;

namespace AltAdv
{
	constexpr int Count = 227; // 0xe3

	// A hot button holding an ability is a social one of this plus its id
	// (CCursorAttachment::Activate type 4; __CurrentSocial >> 3 from 0x78 is "/alt activate").
	constexpr int HotButtonBase = 0x78;

	// The pages, as the AA window's tabs (CAAWnd::Init sorts the abilities onto them).
	enum Category : uint8_t
	{
		None, General, Archetype, Class, PopAdvance, PopAbility,
	};

	struct Ability
	{
		int32_t label1, label2;       // 0x00, 0x04
		int32_t name, description;    // 0x08, 0x0c
		int16_t min_level;            // 0x10
		int16_t cost;                 // 0x12
		int16_t required;             // 0x14
		int16_t required_rank;        // 0x16
		uint8_t max_rank;             // 0x18
		int16_t cost_inc;             // 0x1a
		uint8_t enabled;              // 0x1c
		int16_t spell;                // 0x1e
		uint8_t category;             // 0x21
		int16_t cost_type;            // 0x28
		int16_t pop_cost_inc;         // 0x2a
		int16_t level_step;           // 0x2c
		uint8_t unknown_2e;           // 0x2e
		uint32_t reuse;               // 0x30, seconds
		int32_t level_inc;            // 0x34
	};

	// AltAdvEntry_initializeAltAdvancement's entries, by id.
	extern const Ability Abilities[Count];

	// What the manager reads of EQ_Character: class, level, Expansions (+0xd3c), the points to
	// spend (AlternateAdvancementUnspent) and the ranks (ActorInfo's AAAbilities, by id).
	struct Character
	{
		int class_id = 0;
		int level = 0;
		uint32_t expansions = 0;
		int unspent = 0;
		const uint8_t *ranks = nullptr; // Count of them
	};

	// EQ_Character::GetAbility (@004c3fb0): our rank in it; 0 outside 1..226.
	int Rank(const Character &ch, int id);
	// AltAdvManager_Add_MGB_for_Beastlords_fix (@004b8231): whether a class has the ability at all
	// (the general ones every class; the rest by the class's list).
	bool ClassHas(int class_id, int id);
	// AltAdvManager::CalculateAbilityPointCost (@004b7802): the next rank's cost, at `rank`.
	int PointCost(int rank, int id);
	// AltAdvManager::TotalPointsInSkill (@004b78a9): what `rank` ranks cost all told (0 for an
	// ability the class hasn't).
	int TotalPoints(const Character &ch, int rank, int id);
	// EQ_Character::GetAbilityRequiredLevel (@004b805e): the level the next rank needs.
	int RequiredLevel(const Character &ch, int id);
	// AltAdvManager::CheckAbilityRequirements (@004b7e8a): the ability or points it requires.
	bool MeetsRequirements(const Character &ch, int id);
	// AltAdvManager::CanTrainAbility (@004b812a): its next rank can be bought now.
	bool CanTrain(const Character &ch, int id);
	// AltAdvManager::GetCalculatedTimer (@004b761a): its reuse in seconds, less the Hastened
	// abilities' share; 0 for a passive one.
	uint32_t ReuseTime(const Character &ch, int id);
	// AltAdvManager::IsAbilityReady (@004b755b): ready again (reuse past since `last_used`, both
	// time_t), and the seconds to go in `remaining` (0 for a passive one).
	bool Ready(const Character &ch, int id, int64_t last_used, int64_t now, int64_t *remaining = nullptr);
	// AltAdvManager::GetAbilityReqs (@004b8b78): "Requirements: ..." for the ability.
	std::string Requirements(const Character &ch, int id, const EqStr *eqstr);
	// updateAltAdvancementTimers (@004e15c9): the abilities sharing the timer the zone sent for
	// `id` (the four elemental forms, 61..64; the two touches, 86 and 87); just `id` for the rest.
	std::vector<int> SharedTimer(int id);
	// AltAdvManager::GetAbilityTitle (@004b7db5): 1 with more than 5 points in the general
	// abilities, 2 more than 11 in the archetype ones, 3 more than 23 in the class ones; else 0.
	int Title(const Character &ch);
}
