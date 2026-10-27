#pragma once

// eqgame's spell effects, the Luclin kind (DoSpellEffect @0052ca8d with a SpellsNew.eff record,
// DoSprite_or_ParticleEffectNew @0052c64a), drawn by render::SpellParticles:
//   SpellsNew.eff  0x10c-byte records: a name (64), then for each stage (0 casting, 1 the missile,
//                  2 landing) a sound and four emitters {spellsnew.edd definition, the caster's least
//                  level (an NPC counts as level 1), on whom (0 a spot, 1 the caster, 2 the missile,
//                  3 the target), where on them (0 where they stand, not following; 1 their root;
//                  2 the chest, CH_DAG; 3 HEAD_POINT_DAG; 4 L_POINT_DAG, else SPELL_POINT_DAG; 5
//                  R_POINT_DAG, else SPELL_POINT_DAG; 6 and 7 the feet, BO_L_DAG and BO_R_DAG; 8
//                  WEAPON_POINT_DAG; a bone the model hasn't, its root)}.
//   Each emitter is tagged (the spawn << 16 | the spell) and follows its bone, the axes its actor's
//   (up, then its heading's), until it's done or the spawn goes.
// The options' spell particle density and opacity (1 and 1, eqgame's defaults) scale them; at 0
// there are none. Not done: the stages' sounds, the missiles (stage 1, EQMissile), and the older
// spells.eff effects of the spells that have no SpellsNew.eff one.

#include "character_model.h"
#include "spell_particles.h"

#include <cstdint>
#include <functional>
#include <map>
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

	struct Request
	{
		int stage = 0;       // 0 casting, 2 landing
		int effect = 0;      // the spell's SpellsNew.eff record
		int spell = 0;
		uint16_t caster = 0, target = 0;
		int duration_ms = 0; // the casting's cast time
		int casting_anim = 0; // with its length added (CEverQuest_StartCasting's DoParticleEffect)
		bool at_point = false; // the spot (world space) for the emitters on one
		render::Vec3 point;
	};
	void Start(const Request &request, const FindActor &find);
	void Update(uint64_t now_ms, const render::Vec3 &camera, const FindActor &find);
	void Draw(render::Renderer &renderer) { m_particles.Draw(renderer); }
	void Clear() { m_particles.Clear(); }
	// The options' Spell Particle Density (0, 0.25, 0.5 or 1) and Opacity (0..1).
	void SetDetail(float density, float opacity)
	{
		m_density = density;
		m_opacity = opacity;
	}
	size_t EmitterCount() const { return m_particles.EmitterCount(); }
	size_t ParticleCount() const { return m_particles.ParticleCount(); }

private:
	struct Emitter
	{
		int def = 0, min_level = 0, target = 0, attach = 0;
	};
	struct Record
	{
		std::string name;
		Emitter stages[3][4];
	};
	// A bound emitter's frame now: binding = spawn << 8 | attach.
	bool Locate(uint32_t binding, const FindActor &find, render::SpellParticles::Frame &frame);
	int Bone(const render::CharacterModel &model, int attach);

	std::vector<Record> m_records;
	render::SpellParticles m_particles;
	float m_density = 1, m_opacity = 1;
	std::map<std::pair<const render::CharacterModel *, int>, int> m_bones; // by model and attach point
};
