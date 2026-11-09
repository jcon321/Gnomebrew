#include "actor_sound.h"

#include <cmath>

namespace
{
	// The global waves CDisplay_PlayAnimationSound picks (EqSoundManager's slots).
	constexpr int StepWalkL = 92, StepWalkR = 93, StepRunL = 94, StepRunR = 95, StepLadderR = 96, StepLadderL = 97,
	              StepCrouch = 98, TreadWater1 = 101, TreadWater2 = 102, JumpLand = 114, FallHit = 115, BowDraw = 116,
	              Swing = 118, SwingBig = 119, Impale = 121, Stab = 122, BashShield = 124, Kick = 126, RoundKick = 128,
	              LeftAttack = 129, Punch = 130, Whip = 150, StepWalk2 = 151, StepWalk3 = 152, StepRun2 = 153,
	              StepRun3 = 154;
	constexpr int WaterOut = 99, WaterIn = 100, TorchLoop = 160, UnderwaterLoop = 161;

	audio::Vec3 At(const render::Vec3 &v) { return {v.x, v.y, v.z}; }

	// EQPlayer::LegalPlayerRace: the player races.
	bool PlayerRace(uint16_t race) { return race <= 12 || race == 128 || race == 130; }
}

int ActorSound::ItemType(int it)
{
	switch (it) {
	case 1: case 2: case 3: case 5: case 7: case 9: case 16: case 17: case 19: case 20: case 23: case 24: case 25:
	case 30: case 34: case 35: case 37: case 39: case 40: case 41: case 42: case 43: case 44: case 50: case 51:
	case 53: case 57: case 58: case 59: case 60: case 62:
		return 2;
	case 4: return 4;
	case 61: return 5;
	default:
		if (it < 1)
			return 0;
		return it > 199 && it < 301 ? 3 : 1;
	}
}

int ActorSound::Rand()
{
	m_random = m_random * 1664525u + 1013904223u;
	return static_cast<int>((m_random >> 8) & 0x7fff);
}

void ActorSound::Refresh(audio::EqSound &sound, const Actor &actor)
{
	bool tall = actor.height > 10;
	if (m_have_sounds && actor.race == m_race && actor.gender == m_gender && actor.texture == m_texture && tall == m_tall)
		return;
	if (!m_have_sounds)
		m_random = 0x9e3779b9u ^ (actor.key * 2654435761u);
	m_have_sounds = true;
	m_race = actor.race;
	m_gender = actor.gender;
	m_texture = actor.texture;
	m_tall = tall;
	m_sounds = audio::SoundsForActor(actor.race, actor.gender, actor.texture, actor.height);
	// LoadEntitySoundEffects ends with the idle loop.
	SetLoop(sound, actor, m_sounds.slot[audio::ActorSounds::IdleLoop]);
}

void ActorSound::SetLoop(audio::EqSound &sound, const Actor &actor, int loop)
{
	if (loop == 0)
		loop = m_sounds.slot[audio::ActorSounds::IdleLoop];
	if (actor.type > 1 || actor.ours) // corpses and we have none
		loop = 0;
	m_loop = loop;
	sound.SetActorLoop(actor.key, At(actor.center), actor.height, loop);
}

void ActorSound::Follow(audio::EqSound &sound, const Actor &actor)
{
	Refresh(sound, actor);
	if (actor.type > 1 && m_loop != 0)
		m_loop = 0;
	sound.SetActorLoop(actor.key, At(actor.center), actor.height, m_loop);
}

void ActorSound::Remove(audio::EqSound &sound, const Actor &actor)
{
	sound.RemoveActorLoop(actor.key);
	m_loop = 0;
}

void ActorSound::Animation(audio::EqSound &sound, const Actor &actor, int id, uint64_t now_ms)
{
	using F = audio::ActorSounds;
	Refresh(sound, actor);
	if (id != m_animation)
		SetLoop(sound, actor, m_sounds.slot[F::IdleLoop]);
	m_animation = id;
	if (id == 32 || id == 71 || actor.race == 127) // standing idles; the invisible man
		return;

	const int *slots = m_sounds.slot;
	bool legal = PlayerRace(actor.race);
	auto since = [&] { return now_ms - m_step_ms; };
	enum class Path { None, Pick, Swim, Tread } path = Path::Pick;
	if (id == 17 || id == 18 || id == 22 || id == 23) {
		// Ours walking and running, and anyone's crouch walking and climbing: a step at most every 500 ms.
		if (actor.ours || id == 22 || id == 23) {
			if (since() > 500 && !actor.sneaking)
				m_step_ms = now_ms;
			else
				path = Path::None;
		}
	}
	else if (id == 37 || id == 25) {
		// Swimming and treading water: players' races splash, at most every 551 / 601 ms.
		if (!legal || (id == 37 ? since() < 551 : since() <= 600))
			path = Path::None;
		else {
			m_step_ms = now_ms;
			path = id == 37 ? Path::Swim : Path::Tread;
		}
	}

	int s = 0;
	auto footsteps = [&](int own, int first, int second, int third, int fourth) {
		if (actor.sneaking)
			return;
		if (!actor.ours) {
			int field = slots[own];
			if (field == 0)
				return;
			if (field != second) {
				SetLoop(sound, actor, field); // a loop of its own
				return;
			}
		}
		static const int steps[4] = {second, first, third, fourth};
		s = steps[Rand() % 4];
	};
	// The attacks: the race's own one time in three (ours always); a weapon whooshes as well
	// (and a punch is dropped), a whip is the whip's, and bare hands without one of the race's are
	// a kick, stab, punch, left attack or swing. C10 swings for anything in hand and picks a
	// bare-handed sound even over the race's.
	static const int bare[5] = {Kick, Stab, Punch, LeftAttack, Swing};
	auto attack = [&](bool c10) {
		int primary = ItemType(actor.primary);
		if (Rand() % 3 == 0 || actor.ours)
			s = slots[F::Attack];
		if (primary == 5 && !c10)
			s = Whip;
		else if (primary == 0) {
			if (s == 0 || c10)
				s = bare[Rand() % 5];
		}
		else {
			sound.PlayActorSound(Swing, At(actor.center), actor.height, actor.ours);
			if (s == Punch)
				s = 0;
		}
	};

	switch (path) {
	case Path::None: return;
	case Path::Swim: s = TreadWater2; break;
	case Path::Tread: s = TreadWater1; break;
	case Path::Pick:
		switch (id) {
		case 18: footsteps(F::Run, StepRunL, StepRunR, StepRun2, StepRun3); break;
		case 17: footsteps(F::Walk, StepWalkL, StepWalkR, StepWalk2, StepWalk3); break;
		case 26: // a fidget: one of its two loops
			SetLoop(sound, actor, Rand() % 100 + 1 < 50 ? slots[F::FidgetLoopA] : slots[F::FidgetLoopB]);
			return;
		case 19: case 20: s = slots[F::Jump]; break;
		case 22:
			if (!actor.sneaking)
				s = slots[F::CrouchWalk];
			break;
		case 23:
			if (!actor.sneaking) {
				s = m_ladder_left ? StepLadderL : StepLadderR;
				m_ladder_left = !m_ladder_left;
			}
			break;
		case 33:
			if (!actor.sneaking)
				s = actor.race == 60 ? 0x1d0 : StepCrouch;
			break;
		case 36:
			if (!actor.sneaking)
				s = StepCrouch;
			break;
		case 15: s = slots[F::Drown]; break;
		case 16:
			if (actor.type != 0 || actor.stance == 120 || actor.hp > 0)
				s = slots[F::Death];
			break;
		case 1: s = Kick; break;
		case 2: s = ItemType(actor.primary) == 5 ? Whip : Stab; break;
		case 3: s = ItemType(actor.primary) == 5 ? Whip : SwingBig; break;
		case 4: s = Impale; break;
		case 5:
			attack(false);
			if (actor.race == 49 || actor.race == 122)
				s = slots[F::Attack];
			break;
		case 6: {
			int secondary = ItemType(actor.secondary);
			s = secondary == 5 ? Whip : secondary == 3 ? BashShield : secondary != 0 ? Swing : LeftAttack;
			break;
		}
		case 7: s = ItemType(actor.secondary) == 3 ? BashShield : SwingBig; break;
		case 8: attack(false); break;
		case 9: s = BowDraw; break;
		case 10: attack(true); break;
		case 11: case 45: s = RoundKick; break;
		case 42: case 43: case 44: s = slots[F::Cast]; break;
		case 46: case 47: s = Punch; break;
		default: return;
		}
		break;
	}
	if (s <= 0x1f) // none (or nothing of the race's)
		return;
	// Ours don't start over themselves.
	if (actor.ours && sound.WavePlaying(s))
		return;
	sound.PlayActorSound(s, At(actor.center), actor.height, actor.ours);
}

void ActorSound::Landed(audio::EqSound &sound, const Actor &actor, float speed_z, bool in_water, int safe_fall,
	int damage)
{
	Refresh(sound, actor);
	if (!in_water && actor.race != 127 && speed_z * 2 <= -2.5f)
		sound.PlayActorSound(JumpLand, At(actor.center), actor.height, actor.ours);
	if (speed_z > -2.5f || !actor.ours)
		return;
	// EQPlayer::IHaveFallen: fall_hit when TakeFallDamage hurt (here without the encumbrance when
	// it isn't given).
	float fall = std::fabs(speed_z + 2.5f) - 1.5f;
	if (damage >= 0)
		fall = -1;
	else
		damage = 0;
	if (fall >= 9)
		damage = 20000;
	else if (fall > 0) {
		damage = static_cast<int>(fall * 10);
		if (damage > 0 && safe_fall > 0)
			damage -= safe_fall;
	}
	sound.PlayActorSound(damage > 0 ? FallHit : JumpLand, At(actor.center), actor.height, true);
}

void ActorSound::FeetChanged(audio::EqSound &sound, const Actor &actor, render::Environment from, render::Environment to,
	float speed_z)
{
	using render::Environment;
	if (from == to)
		return;
	int s = 0;
	if (from != Environment::Dry && to == Environment::Dry)
		s = WaterOut;
	if (to == Environment::Lava)
		s = WaterOut;
	else if (render::Swimming(to) && (speed_z >= 1 || speed_z <= -1))
		s = WaterIn;
	if (s)
		sound.PlayActorSound(s, At(actor.center), actor.height, actor.ours);
}

void ActorSound::HeadChanged(audio::EqSound &sound, render::Environment from, render::Environment to)
{
	if (from == to)
		return;
	auto loop = [](render::Environment environment) {
		return environment == render::Environment::Lava ? TorchLoop : render::Swimming(environment) ? UnderwaterLoop : 0;
	};
	// Out of water (5): SetGenericEnvironment; into it, SetSpecialEnvironment(22), underwater.
	if (int slot = loop(from))
		sound.StopWave(slot);
	if (from == render::Environment::Water)
		sound.SetGenericEnvironment();
	if (to == render::Environment::Water)
		sound.SetSpecialEnvironment(22);
	if (int slot = loop(to))
		sound.PlayWave(slot);
}
