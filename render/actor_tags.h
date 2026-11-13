#pragma once

// The model a race and gender use, as EQPlayer::GetActorTag (@0050845d) names it:
// "<tag>_ACTORDEF" in the loaded _chr.s3d files, e.g. HUM (human male), RAT, SPI.

#include <cstdint>
#include <string>

namespace render
{
	std::string ActorTag(uint16_t race, uint8_t gender);
	// The races a player can be (their models are scaled to the race's height).
	bool IsPlayerRace(uint16_t race);
	// Minor Illusion (142) and Tree (143): CreatePlayerActor gives them the nearest zone object's
	// actor (GetNearestActorTag), not a model of their own.
	inline bool IsObjectRace(uint16_t race) { return race == 142 || race == 143; }
}
