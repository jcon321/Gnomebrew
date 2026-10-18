#pragma once

// The client's animations by number, as CDisplay::PlayAnimationTrack takes them. The table
// of track codes is the one eqgame.exe fills at 004d7a21 (TAKP calls it initializeHotkeyLabels):
// 1..11 C01..C11 combat, 12..16 D01..D05 damage and death, 17..25 L01..L09 locomotion, 26 O01
// idle fidget, 32..38 P01..P07 postures, 39..47 T01..T09 casting and emotes, and so on. The
// server's OP_Animation ids (DoAnimation) are the same numbers.

#include <cstdint>
#include <string>

namespace render
{
	namespace Anim
	{
		constexpr int Combat1 = 1;     // C01: kick ...
		constexpr int Death = 16;      // D05: fall over (held at the end)
		constexpr int Walk = 17;       // L01
		constexpr int Run = 18;        // L02
		constexpr int RunJump = 19;    // L03
		constexpr int StandJump = 20;  // L04
		constexpr int Fall = 21;       // L05
		constexpr int CrouchWalk = 22; // L06
		constexpr int Climb = 23;      // L07
		constexpr int Crouch = 24;     // L08: duck down (and back up, reversed)
		constexpr int TreadWater = 25; // L09
		constexpr int Fidget = 26;     // O01: once in a while, standing
		constexpr int Idle = 32;       // P01
		constexpr int SitDown = 33;    // P02: sit (and stand up, reversed)
		constexpr int Shuffle = 34;    // P03: turning on the spot
		constexpr int Kneel = 36;      // P05
		constexpr int Swim = 37;       // P06
		constexpr int Sitting = 38;    // P07: sitting idle
		constexpr int Stand2 = 71;     // P08: standing idle after a stop
		constexpr int Fidget2 = 72;    // O02
		constexpr int SitFidget = 73;  // O03
		constexpr int Count = 79;
	}

	// The track code ("L01") for an animation number, or "" when there's none.
	const char *AnimationCode(int id);
	// getFallbackAnimationId (@004d9192): what to play when a model doesn't have `id`.
	int FallbackAnimation(int id, uint16_t race);
	// isStandingAnimation (@004d92c2): idles that fidgets and turns may replace.
	bool IsStandingAnimation(int id);
	// isMovementAnimation (@004d92ff).
	bool IsMovementAnimation(int id);

	// mapCharacterPrefixToRaceCode (@004d8065): the model whose tracks a model plays when it has
	// none of its own for a bone, e.g. the human male's are the elf male's ("HUM" -> "ELM"), a
	// minotaur's the gnoll's. Classic (non-Luclin) models. Returns the code itself when it has no source.
	std::string AnimationSource(const std::string &code);
}
