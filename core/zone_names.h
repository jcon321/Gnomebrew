#pragma once

// The zones eqgame knows by id: EQWorldData::EQWorldData (@005223c6) adds each one
// (EQWorldData::AddZone, the first of an id kept) with its long name and an eqstr id.

#include <cstdint>
#include <string>

class EqStr;

// EQWorldData::GetFullZoneName (@00523e49): the eqstr string when the table has it, else the
// built-in long name; "Unknown Zone" for an id it doesn't know. `strings` may be null.
std::string FullZoneName(uint32_t zone_id, const EqStr *strings);

// EQWorldData::ExpansionZone (@00523ec1): the expansion AddZone gives a zone (0 the original world,
// 1 Kunark, 2 Velious, 3 Luclin, 4 the Planes of Power); 0 for an id it doesn't know.
int ZoneExpansion(uint32_t zone_id);
