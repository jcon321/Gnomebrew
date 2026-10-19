#pragma once

// What each race sounds like: EQPlayer::SetSounds' table. An actor's sounds are numbers: slots of
// EqSoundManager's 4000 waves (under 3000), or loops, which play the wave in slot 3000 + the number
// from the actor's EqMobileEmitter.

#include <cstddef>
#include <cstdint>

namespace audio
{
	struct WaveName
	{
		int slot;
		const char *name;
	};
	// The waves SetSounds loads for some race, by slot.
	const WaveName *ActorWaves(size_t &count);

	struct ActorSounds
	{
		// ActorInfo +0x208 on, in order (the decompile's Unknown01C4 + 0x44 on).
		enum Field
		{
			IdleLoop,     // loop: standing about (and after every change of animation)
			FidgetLoopA,  // loops a fidget picks between
			FidgetLoopB,
			Jump,         // jumping (L03, L04)
			GetHit1, GetHit2, GetHit3, GetHit4,
			Gasp1, Gasp2,
			Drown,        // D04
			Death,        // D05
			Attack,       // the attacks that use the race's own (C05, C08, C10)
			Attack2,
			Cast,         // T04-T06
			Walk,         // a loop, or 93: footsteps like ours
			Run,          // a loop, or 95: footsteps
			CrouchWalk,   // L06
			// The rest is the actor's own state at run time (0 in the table).
			LadderFoot, CurrentLoop, Unused, StepTime,
			Count
		};
		int slot[Count] = {};
	};
	// texture: the body texture (anything over 3 counts as 0); height: the model's (one race splits at 10).
	ActorSounds SoundsForActor(uint16_t race, uint8_t gender, uint8_t texture, float height);
}
