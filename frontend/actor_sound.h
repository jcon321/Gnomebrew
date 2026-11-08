#pragma once

// The sounds an actor (us, or a spawn) makes, from its race's (EQPlayer::SetSounds' table):
//   CDisplay_PlayAnimationSound (@004da6a2): called with every PlayAnimationTrack2, which
//     ProcessAnimation calls each frame with what the actor's doing, and once for each stance change
//     and the server's OP_Animation. A change of animation puts the actor's idle loop back on.
//     Walking and running: our footsteps (one of four, at most every 500 ms, none while sneaking),
//     or a spawn's walk/run loop on its EqMobileEmitter (or footsteps like ours, for the races that
//     have them). Treading water and swimming splash (players' races, at most every 600 / 551 ms).
//     Fidgets switch the loop to one of two. Jumps, crouch walking, ladders, sitting and kneeling,
//     drowning and dying play their sounds; attacks the race's own one time in three (always ours),
//     or by what's in the hands (CDisplay_GetItemType): a whoosh for a weapon, a punch or kick
//     sound bare handed, the whip's. Ours don't start while the same wave still plays.
//   CDisplay::CheckForUnderFloor (@004b479e): landing at 1.25 a tick down or faster, out of the
//     water, thumps (jumpland); at 2.5, ours goes through EQ_Character::TakeFallDamage, and a fall
//     that would hurt is fall_hit instead. (Only the sound: the damage is the server's to apply.)
//   EQPlayer::feetEnvironmentChange (@0050bc30): the feet coming out of water or lava (WaterOut),
//     or going into water at 1 a tick up or down or faster (WaterIn), or into lava (WaterOut).
//   EQPlayer::headEnvironmentChange (@0050bcac), ours only: the head going under water plays the
//     underwater sound where we are (WatUndLp; Torch_Lp in lava, once each), and coming up stops it.
//     Into water (5, not the other kinds) the reverb's room is underwater (SetSpecialEnvironment
//     0x16); out of it, back to the generic one.
// Sounds play from the actor's position (its model's center).

#include "eq_sound.h"
#include "actor_sound_table.h"
#include "math3d.h"
#include "zone_regions.h"

#include <cstdint>

class ActorSound
{
public:
	struct Actor
	{
		uint32_t key = 0;      // its loop's: the spawn id
		bool ours = false;
		int type = 1;          // 0 a player, 1 an NPC, 2 and up a corpse (Spawn_Struct's NPC byte)
		uint16_t race = 1;
		uint8_t gender = 0, texture = 0;
		float height = 6;
		render::Vec3 center;   // the model's center (the server's position)
		int primary = 0, secondary = 0; // the IT numbers of what's in its hands
		uint8_t stance = 100;
		int hp = 100;
		bool sneaking = false;
	};

	// PlayAnimationTrack2's call: the animation number it plays.
	void Animation(audio::EqSound &sound, const Actor &actor, int id, uint64_t now_ms);
	// CheckForUnderFloor: the feet met the floor going down at `speed_z` a tick (negative).
	// `in_water`: the body's in water. `safe_fall`: our safe fall skill.
	void Landed(audio::EqSound &sound, const Actor &actor, float speed_z, bool in_water, int safe_fall = 0);
	// The region at the feet changed from `from` to `to` (render::Environment), going `speed_z` a tick.
	void FeetChanged(audio::EqSound &sound, const Actor &actor, render::Environment from, render::Environment to,
		float speed_z);
	// Ours: the region at the eyes changed.
	static void HeadChanged(audio::EqSound &sound, render::Environment from, render::Environment to);
	// Every frame: the actor's loop follows it (EqMobileEmitter::Move).
	void Follow(audio::EqSound &sound, const Actor &actor);
	void Remove(audio::EqSound &sound, const Actor &actor);

	// CDisplay_GetItemType: 0 nothing, 1 blunt, 2 bladed, 3 a shield, 4 a bow, 5 a whip (IT61).
	static int ItemType(int it);

private:
	void Refresh(audio::EqSound &sound, const Actor &actor); // SetSounds, when the look changes
	void SetLoop(audio::EqSound &sound, const Actor &actor, int loop); // EQPlayer::SetCurrentLoopSound
	int Rand();

	audio::ActorSounds m_sounds;
	bool m_have_sounds = false;
	uint16_t m_race = 0;
	uint8_t m_gender = 0, m_texture = 0;
	bool m_tall = false;
	int m_animation = -1;   // ActorInfo->Animation: the last one played
	uint64_t m_step_ms = 0; // the last footstep or splash
	bool m_ladder_left = false;
	int m_loop = 0;
	uint32_t m_random = 0;
};
