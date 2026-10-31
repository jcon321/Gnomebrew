#pragma once

// eqgame's spell effects (DoSpellEffect @0052ca8d): a spell's SpellsNew.eff record when it has one
// (EQ_Spell +0xf8: in the file and named), otherwise its spells.eff record (+0xf0).
//
// The Luclin kind (DoSprite_or_ParticleEffectNew @0052c64a), drawn by render::SpellParticles:
//   SpellsNew.eff  0x10c-byte records: a name (64), then for each stage (0 casting, 1 the missile,
//                  2 landing) a sound and four emitters {spellsnew.edd definition, the caster's least
//                  level (an NPC counts as level 1), on whom (0 a spot, 1 the caster, 2 the missile,
//                  3 the target), where on them (0 where they stand, not following; 1 their root;
//                  2 the chest, CH_DAG; 3 HEAD_POINT_DAG; 4 L_POINT_DAG, else SPELL_POINT_DAG; 5
//                  R_POINT_DAG, else SPELL_POINT_DAG; 6 and 7 the feet, BO_L_DAG and BO_R_DAG; 8
//                  WEAPON_POINT_DAG; a bone the model hasn't, its root)}.
//   Each emitter is tagged (the spawn << 16 | the spell) and follows its bone, the axes its actor's
//   (up, then its heading's), until it's done or the spawn goes.
//   The options' spell particle density and opacity (1 and 1, eqgame's defaults) scale them; at 0
//   there are none.
//
// The old kind (DoSpriteEffect @0052b73f, DoParticleEffect @0052bee2), for the rest:
//   spells.eff     256 records of 0xae8 bytes, a 0x3a0-byte block for each stage:
//                  +0x08  three emitters' blit sprites (32 each; gequip's, e.g. GENA10_SPRITE)
//                  +0x68  on whom the emitters are ("Source" the caster, "Target" the target)
//                  +0x88  their bones (GetDagName: 1 HEAD_POINT, 2 R_POINT, 3 L_POINT, 4 BO_R or
//                         BOFOOTR, 5 BO_L or BOFOOTL, else CH or CHEST_POINT; none, the root)
//                  +0x94  their kinds (-1 none), each a PCLOUD of CreatePartEffects (@004d7397):
//                         0 two PCLOUD0s, on the hands (L_POINT and R_POINT, else SPELL_POINT);
//                         1 a PCLOUD5 on the bone; 2 a PCLOUD1 on the bone; 3 a PCLOUD3 and 4 a
//                         PCLOUD4 on the floor under them (10 over them when the normal's down);
//                         5 a PCLOUD6 there and another on the bone
//                  +0xa0  the sprite effect's actors (12 of 32: gequip's name_ACTORDEF)
//                  +0x220 the sprite effect's kind (-1 none; 1 one at the target, 2 a ring of them
//                         circling it), +0x224 the stage's sound
//                  +0x228 the emitters' overrides of their PCLOUD (MakePCloudInstance @0052bcb0;
//                         each 0 keeps it): color (+0x228), gravity (+0x234), spawn normal
//                         (+0x240), radius (+0x264), angle (+0x270), lifespan (+0x27c), speed
//                         (+0x288), interval (+0x294), sprite size (+0x2a0)
//                  +0x2ac the sprites' tints (3 each), +0x300 and +0x318 how fast they turn and
//                         climb, +0x330 their circle's radius, +0x360 their kind (EQEffect's: 1 a
//                         flash for 3/4 of a second, 2 circling for 2 seconds), +0x378 their size
//   The emitters go by the caster's level (an NPC counts as 1): the second needs 24, the third 39.
//   The sprites move as CDisplay::ProcessEffects and process_physics move an EQEffect: put back
//   on the target each frame, then pushed out along their heading (turning) at most every 32 ms.
//   A sprite flash (kind 1) carries a light while it lasts (EQEffect's CreateLight: type 6, the
//   lglantern's, out to 250). The old kind is eqgame's ShowSpellEffects (/showspelleffects, on by
//   default) here always on, and the dragon breaths (effects 61..64) go off a second after their
//   casting begins.
//
// Either way the stage's sound (the record's: SpellsNew.eff's +0x40, spells.eff's +0x224; -1 none)
// plays from the caster casting, the target landing (EQPlayer::PlaySoundA), whatever's drawn: one
// of the numbered game waves (103..107 Spell_1..5, 108 SpelCast, 109 SpelGdHt, 110..113
// SpelHit1..4, and a few others).
//
// A missile's own effect (EQMissile::ProcessMissile's DoSpellEffect, stage 1 of a bolt) has its
// emitters on whom 2 bound to the missile's actor: they follow its root, its axes turned to its
// heading and tilt, until it's gone.
//
// Not done: an old (spells.eff) effect on a missile, and the sprites' circling between the 32 ms
// steps (they keep their last place where eqgame would draw them back on the target).

#include "character_model.h"
#include "particles.h"
#include "pfs.h"
#include "spell_particles.h"
#include "textures.h"
#include "wld.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class SpellEffects
{
public:
	bool Load(const std::string &eq_dir, std::string &error);

	// How a spawn shows this frame: its model, where it is and the pose it's in. False: no model
	// (gone, or a race without one).
	struct Actor
	{
		const render::CharacterModel *model = nullptr;
		render::Transform placement;
		uint64_t time_ms = 0;
		std::string animation = "P01";
		bool loop = true;
		uint16_t race = 0;
		bool player = false; // EQPlayer::Type 0: the emitters go by their level
		uint8_t level = 1;
	};
	using FindActor = std::function<bool(uint16_t id, Actor &actor)>;
	// CDisplay::GetFloorHeight: the floor's height under a spot (its own height when there's none).
	using FloorAt = std::function<float(const render::Vec3 &at)>;
	void SetFloor(FloorAt floor) { m_floor = std::move(floor); }
	// Where a missile (Missiles' id) is drawn this frame; false when it's gone.
	using FindMissile = std::function<bool(uint32_t id, render::Transform &placement)>;
	void SetMissiles(FindMissile find) { m_find_missile = std::move(find); }

	struct Request
	{
		int stage = 0;       // 0 casting, 1 the missile, 2 landing
		int effect = 0;      // the spell's SpellsNew.eff record
		int old_effect = -1; // its spells.eff record (SpellAffectIndex), for when it has no SpellsNew.eff one
		int spell = 0;
		uint16_t caster = 0, target = 0;
		int duration_ms = 0; // the casting's cast time
		int casting_anim = 0; // with its length added (CEverQuest_StartCasting's DoParticleEffect)
		bool at_point = false; // the spot (world space) for the emitters on one
		render::Vec3 point;
		int delay_ms = 0;    // started this long after (a dragon breath's casting)
		uint32_t missile = 0; // the missile for the emitters on one (Missiles' id)
	};
	// EQPlayer::PlaySoundA: a stage's sound (a numbered wave) from spawn `on`, as it starts.
	using PlaySound = std::function<void(const Request &request, uint16_t on, int slot)>;
	void SetSound(PlaySound play) { m_play_sound = std::move(play); }

	void Start(const Request &request, const FindActor &find);
	void Update(uint64_t now_ms, const render::Vec3 &camera, const FindActor &find);
	void Draw(render::Renderer &renderer);
	void Clear();
	// The options' Spell Particle Density (0, 0.25, 0.5 or 1) and Opacity (0..1).
	void SetDetail(float density, float opacity)
	{
		m_density = density;
		m_opacity = opacity;
	}
	// The debug menu's: off, every spell has its old effect, as eqgame does without SpellsNew.eff
	// (loadSpellEffectsFromFile @004d77a6 makes a table of two blank records then).
	void SetNewEffects(bool on) { m_new_effects = on; }
	size_t EmitterCount() const;
	size_t ParticleCount() const;
	// Where the sprite flashes' lights are this frame.
	void FlashLights(std::vector<render::Vec3> &at) const;
	uint64_t Now() const { return m_now_ms; } // the last Update's

private:
	struct Emitter
	{
		int def = 0, min_level = 0, target = 0, attach = 0;
	};
	struct Record
	{
		std::string name;
		int sounds[3] = {-1, -1, -1};
		Emitter stages[3][4];
	};

	// A spells.eff stage.
	struct OldEmitter
	{
		int kind = -1;
		std::string sprite;
		int dag = -1;
		uint32_t color = 0; // 0x..RRGGBB
		float gravity = 0;
		render::Vec3 normal;
		float radius = 0, angle = 0, speed = 0, size = 0;
		uint32_t lifespan_ms = 0, interval_ms = 0;
	};
	struct OldSprite
	{
		std::string actor; // name_ACTORDEF
		uint32_t tint = 0; // 0x..RRGGBB; 0 white
		int kind = 0;
		float turn = 0, climb = 0; // 512ths of a turn every 50 ms
		float radius = 0, size = 0;
	};
	struct OldStage
	{
		std::string on;
		int sound = -1;
		OldEmitter emitters[3];
		int sprite_kind = -1;
		std::vector<OldSprite> sprites;
	};
	struct OldRecord
	{
		OldStage stages[3];
	};
	// A blit sprite's frames.
	struct Blit
	{
		std::vector<const render::Texture *> frames;
		uint32_t frame_ms = 0;
	};
	// An old emitter's cloud: on a bone (spawn << 8 | attach) or set down (0).
	struct OldCloud
	{
		std::unique_ptr<render::ParticleCloud> cloud;
		uint32_t binding = 0;
	};
	// An EQEffect: a blit sprite actor.
	struct SpriteEffect
	{
		const Blit *blit = nullptr;
		uint32_t tint = 0xffffff;
		float size = 1;
		int kind = 0;
		uint16_t on = 0;            // the spawn it's put back on each frame (kind 2)
		render::Vec3 center, offset; // where it is: the spawn's place, and how far out it's pushed
		float heading = 0, pitch = 0, speed = 0, turn = 0, climb = 0; // 512ths of a turn; speed out
		render::Vec3 velocity;
		uint64_t end_ms = 0, physics_ms = 0;
	};

	bool UsesNew(int effect) const;
	void StartNew(const Request &request, const FindActor &find);
	void StartOld(const Request &request, const FindActor &find);
	int CastDuration(const Request &request, const Actor *caster) const;
	// A bound emitter's bone now: binding = spawn << 8 | attach.
	bool PlaceBone(uint32_t binding, const FindActor &find, render::Transform &bone);
	bool Locate(uint32_t binding, const FindActor &find, render::SpellParticles::Frame &frame);
	int Bone(const render::CharacterModel &model, int attach);
	const Blit *FindBlit(const std::string &name);
	void MakeCloud(int pcloud, const OldEmitter &emitter, const render::Vec3 &at, uint32_t binding, int duration_ms);
	void UpdateSprites(uint64_t now_ms, const FindActor &find);
	void DrawSprites(render::Renderer &renderer);

	std::vector<Record> m_records;
	render::SpellParticles m_particles;
	float m_density = 1, m_opacity = 1;
	bool m_new_effects = true;
	std::map<std::pair<const render::CharacterModel *, int>, int> m_bones; // by model and attach point

	std::vector<OldRecord> m_old;
	PfsArchive m_equipment; // gequip.s3d: the old effects' sprites
	WldFile m_equipment_wld;
	render::TextureCache m_textures;
	std::map<std::string, Blit> m_blits; // by name; no frames: not there
	std::vector<OldCloud> m_clouds;
	std::vector<SpriteEffect> m_sprites;
	std::vector<std::pair<uint64_t, Request>> m_later; // delayed starts, by when
	FloorAt m_floor;
	FindMissile m_find_missile;
	PlaySound m_play_sound;
	uint64_t m_now_ms = 0;
};
