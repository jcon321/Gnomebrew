#pragma once

// eqgame's duels. Three globals: whom we challenged, or whose challenge we took up (DAT_00798674),
// who challenged us (DAT_00798676), and whether the duel's on (DAT_00798678). All three are cleared
// by CEverQuest::CEverQuest, SetDefaultGameValues, CEverQuest_EnterZone and EQ_PC::ResetPlayerStats
// (PrepareForRepop).
//   - /duel (@004fca55): the target must be a player (13248) and not us (13249); in a duel, 13250
//     when it's with the target, else 13251; the target who challenged us is taken up (13252, the
//     duel's on; OP_RequestDuel then OP_DuelAccept); with no challenge of our own pending the target
//     is challenged (13254, OP_RequestDuel); else 13253. All in color 15 but the first two (0x111).
//   - /decline (@004fc966): in a duel, 13245 (15); a challenger's turned down (13246, 0x111;
//     OP_DuelResponse 0) and forgotten; else 13247 (0x111).
//   - msg_duel (@004e31b7), OP_RequestDuel from the zone: an ignored challenger (CEverQuest_BeingIgnored)
//     is turned down unseen (OP_DuelResponse 0); out of a duel, the one we challenged taking it up
//     starts it (1424), a first challenge with none of ours pending is told (1426), the same
//     challenger again says nothing; anyone else is told we're considering another's (2); in a duel,
//     anyone but our opponent is told we're dueling (1).
//   - HandleWorldMessage's 0x40d0, OP_DuelResponse from the one we challenged: 1383 (0), 1384 (1) or
//     1385 (2), color 15, and our challenge is over (the challenger's kept).
//   - HandleWorldMessage's 0x4029 (OP_DeleteSpawn) of the one we challenged: out of a duel 1409
//     (15), in one 1408 (0x10d) and OP_DuelWon; all three cleared.
//   - CEverQuest::ReportDeath of the one we challenged: our killing blow in the duel 12100 (0x10d)
//     and OP_DuelWon, else 12101 (0x10d); all three cleared.
//   - EQPlayer::AllowedToAttack: players (or their pets) may fight in our duel (`Opponent`).

#include "zone_session.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class Duel
{
public:
	struct Host
	{
		// eqstr_en.txt's string `id` with its %1.. filled in, or `fallback`.
		std::function<std::string(uint32_t, const std::vector<std::string> &, const std::string &)> text;
		std::function<void(const std::string &, int)> chat; // print_chat_dsp_chat, in a color type
		std::function<bool(const std::string &)> ignored;    // CEverQuest_BeingIgnored
	};

	void Attach(ZoneSession &zone, Host host);
	void Reset();

	void DuelCommand(uint16_t target); // /duel at our target (0 none)
	void DeclineCommand();             // /decline
	void Requested(uint16_t challenger);         // msg_duel
	void Responded(uint16_t sender, int type);   // OP_DuelResponse from the zone
	void Despawned(uint16_t id, const std::string &name); // OP_DeleteSpawn (the spawn's gone already)
	void Died(uint16_t victim, uint16_t killer, const std::string &name); // ReportDeath

	bool Dueling() const { return m_dueling; }
	uint16_t Opponent() const { return m_challenged; } // DAT_00798674
	uint16_t Challenger() const { return m_challenger; }

private:
	std::string Text(uint32_t id, const std::vector<std::string> &args, const std::string &fallback) const;
	void Say(const std::string &text, int type) const;
	const ZoneSpawn *Spawn(uint16_t id) const;

	ZoneSession *m_zone = nullptr;
	Host m_host;
	uint16_t m_challenged = 0; // DAT_00798674
	uint16_t m_challenger = 0; // DAT_00798676
	bool m_dueling = false;    // DAT_00798678
};
