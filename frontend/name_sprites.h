#pragma once

// The names over heads, as eqgame's CDisplay keeps them (name sprites) and eqgfx draws them.
//   - NameSpriteText is CDisplay::SetNameSpriteState (@004b0bd9) with the SIDL UI: the first
//     name (with an AA title before it at level 4), the surname at 2 and up, then LFG, AFK and
//     LD, and the guild on a second line at 3 and up. NPCs and corpses show their name only.
//     /shownames (ShowNamesLevel) 0 turns the players' and corpses' names off. With Zeal's
//     changes, which TAKP players have: levels 5 to 7 (NameplateExtendedShownames), no stray
//     spaces, and corpses' and the target's names never hidden.
//   - NameSpriteColor is CDisplay_SetNameSpriteTint (@004b114d): one color per kind of spawn.
//     The target's name runs from white back to its color over 11 frames, again and again
//     (RealRender_World tints it every frame, about 300 ms a cycle); NameSpritePulse is that.
//   - eqgfx's renderNameTag draws the text 0.05 units a texel of a 20 pixel bold Arial (CDisplay::
//     InitDDraw's font 5, antialiased), the glyphs stretched to size: a line is one unit tall from
//     20 units out, and closer than that keeps the size it had at 20. Lines after the first are
//     0.85 of the first (renderFormattedText). It lights the tint with the world's ambient
//     light (the sky's, before any floor's exposure) plus 0.4, each channel at most full.
//     eqgame's s3dSetStringSpriteYonClip hides it more than 70 units in front of the camera; TAKP's
//     eqgame.dll (akplus: [Defaults] EnableExtendedNameplateDistance, on unless set FALSE) and
//     Zeal (NameplateExtended) turn that off, so names show as far out as the zone is drawn.

#include "zone_session.h"

#include <cstdint>
#include <string>

class EqStr;

namespace NameSprite
{
	constexpr int ShowNamesDefault = 4;   // [Defaults] ShowNamesLevel, 0..7
	constexpr int ShowNamesMax = 7;       // Zeal's extended levels; eqgame's stop at 4
	// eqgame's distance: CDisplay::ChangeDagStringSprite's s3dSetStringSpriteYonClip(sprite, 1, 70).
	// Without extended nameplate distance, names further in front of the camera than this hide.
	constexpr float YonClip = 70;
	constexpr float UnitsPerTexel = 0.05f; // s3dCreateStringSprite's scale (eqgfx 0x100db1a8)
	constexpr float NearestDepth = 20;    // 1 / s3dSetStringSpriteMaxScaleFactor's 0.05
	constexpr int FontHeight = 20;        // font 5: Arial, lfHeight 20,
	constexpr int FontWeight = 700;       // bold (the LOGFONT eqgfx's font manager makes it from)
	constexpr float LineScale = 0.85f;    // each line after the first
	constexpr int PulseSteps = 11;        // the target's tint: 0 white .. 10 its own color
	constexpr int PulseMs = 300;          // a step a frame; the cycle is about 300 ms (Zeal's measure)
	constexpr float LightLift = 0.4f;     // renderNameTag: added to the ambient light
}

// The text over a spawn's head; empty draws nothing. '\n' starts the guild line.
// `targeted`: it's our target, whose name always shows.
std::string NameSpriteText(const ZoneSpawn &spawn, int show_names, const GuildNames &guilds, const EqStr *strings,
	bool targeted = false);
// Its color (0xRRGGBB). `ruleset` is the server's (OP_LogServer): 2 colors players by race team.
uint32_t NameSpriteColor(const ZoneSpawn &spawn, uint32_t ruleset);
// The target's color at pulse `step` (0..10).
uint32_t NameSpritePulse(uint32_t color, int step);
// renderNameTag (eqgfx @10054820): `color` lit by the world's ambient light (0..1 a channel).
uint32_t NameSpriteLit(uint32_t color, float red, float green, float blue);
