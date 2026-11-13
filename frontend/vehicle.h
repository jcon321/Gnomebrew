#pragma once

// Boats, as eqgame.exe carries what stands on them:
//   isSpecialRace_allBoatTypes (@004f30be): races 72 (ship), 73 (launch), 114 (ghost ship) and
//     141 (boat) are vehicles. CDisplay_IsCollisionValid (@004b36ba) makes their actors solid, and
//     a player whose collision meets one has it as ActorInfo's AttachedEntity (+0x190); DoFloorCheck
//     clears it before each floor check, so it's the boat under our feet.
//   vehicle_premove (@0054d664) and vehicle_postmove (@0054d7a9) around every move of a boat
//     (process_physics, an update's unpack_physics in ProcessUpdateStats, MovePlayer): first everyone
//     attached to it goes into the boat's own frame (turned back by its heading and pitch, from its
//     position), then back out from where the boat is now. So they go along and turn with it, their
//     heading and motion too.
// World space is the WLD frame (x = the server's y, y = the server's x).

#include "math3d.h"

#include <cmath>
#include <cstdint>

inline bool IsBoatRace(uint16_t race)
{
	return race == 72 || race == 73 || race == 114 || race == 141;
}

// A boat's move in one frame: from where it was to where it is now (its feet and heading, 512ths).
struct VehicleMove
{
	render::Vec3 from, to;
	float from_heading = 0, to_heading = 0;

	float Turn() const { return to_heading - from_heading; }
	// premove then postmove on one of its passengers (pitch is left out: boats don't pitch here).
	render::Vec3 Carry(const render::Vec3 &position) const
	{
		return to + Rotate(position - from);
	}
	render::Vec3 Rotate(const render::Vec3 &v) const
	{
		float a = render::HeadingToRadians(Turn());
		float c = std::cos(a), s = std::sin(a);
		return {v.x * c - v.y * s, v.x * s + v.y * c, v.z};
	}
	float CarryHeading(float heading) const
	{
		heading = std::fmod(heading + Turn(), 512.0f); // CDisplay_FixHeading
		return heading < 0 ? heading + 512 : heading;
	}
};
