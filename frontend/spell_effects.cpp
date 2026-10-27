#include "spell_effects.h"

#include "animations.h"

#include <cstring>
#include <fstream>

namespace
{
	constexpr size_t RecordSize = 0x10c;

	int32_t Int(const uint8_t *r, size_t o)
	{
		int32_t v;
		std::memcpy(&v, r + o, 4);
		return v;
	}
}

bool SpellEffects::Load(const std::string &eq_dir, std::string &error)
{
	m_records.clear();
	std::ifstream file(eq_dir + "/spellsnew.eff", std::ios::binary);
	if (!file) {
		error = "can't read spellsnew.eff";
		return false;
	}
	std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	for (size_t at = 0; at + RecordSize <= data.size(); at += RecordSize) {
		const uint8_t *r = data.data() + at;
		Record record;
		size_t len = 0;
		while (len < 64 && r[len])
			++len;
		record.name.assign(reinterpret_cast<const char *>(r), len);
		for (int stage = 0; stage < 3; ++stage)
			for (int i = 0; i < 4; ++i) {
				size_t o = 0x44 + stage * 0x44 + i * 16;
				record.stages[stage][i] = {Int(r, o), Int(r, o + 4), Int(r, o + 8), Int(r, o + 12)};
			}
		m_records.push_back(std::move(record));
	}
	return m_particles.Load(eq_dir, error);
}

int SpellEffects::Bone(const render::CharacterModel &model, int attach)
{
	auto key = std::make_pair(&model, attach);
	auto it = m_bones.find(key);
	if (it != m_bones.end())
		return it->second;
	// The attach point's bones, the first the model has; none, its root (t3dGetDag 0).
	static const char *const Names[9][2] = {{nullptr, nullptr}, {nullptr, nullptr}, {"CH_DAG", "CHEST_POINT_DAG"},
		{"HEAD_POINT_DAG", nullptr}, {"L_POINT_DAG", "SPELL_POINT_DAG"}, {"R_POINT_DAG", "SPELL_POINT_DAG"},
		{"BO_L_DAG", "BOFOOTL_DAG"}, {"BO_R_DAG", "BOFOOTR_DAG"}, {"WEAPON_POINT_DAG", nullptr}};
	int bone = 0;
	if (attach >= 2 && attach <= 8)
		for (const char *name : Names[attach])
			if (name) {
				int found = model.FindBone(model.Code() + name);
				if (found >= 0) {
					bone = found;
					break;
				}
			}
	if (m_bones.size() > 512)
		m_bones.clear(); // models come and go
	m_bones.emplace(key, bone);
	return bone;
}

bool SpellEffects::Locate(uint32_t binding, const FindActor &find, render::SpellParticles::Frame &frame)
{
	Actor actor;
	if (!find(static_cast<uint16_t>(binding >> 8), actor) || !actor.model)
		return false;
	std::vector<render::Transform> placed;
	actor.model->PlaceBones(actor.placement, actor.time_ms, actor.animation, actor.loop, {Bone(*actor.model, binding & 0xff)},
		placed);
	const render::Transform &bone = placed[0];
	frame.position = bone.translation;
	// The actor's axes: up, then along its heading and across it.
	frame.axes[0] = {0, 0, 1};
	frame.axes[1] = actor.placement.ApplyDirection({1, 0, 0});
	frame.axes[2] = actor.placement.ApplyDirection({0, 1, 0});
	frame.bone = true;
	frame.bone_axes[0] = bone.ApplyDirection({0, 0, 1});
	frame.bone_axes[1] = bone.ApplyDirection({1, 0, 0});
	frame.bone_axes[2] = bone.ApplyDirection({0, 1, 0});
	return true;
}

void SpellEffects::Start(const Request &request, const FindActor &find)
{
	if (request.effect < 1 || static_cast<size_t>(request.effect) >= m_records.size() || request.stage < 0 ||
		request.stage > 2 || m_density <= 0 || m_opacity <= 0)
		return;
	Actor caster;
	bool have_caster = request.caster && find(request.caster, caster);
	int level = have_caster && caster.player ? caster.level : 1;
	int duration = request.duration_ms;
	if (request.casting_anim && have_caster && caster.model) {
		// The casting animation's length on top of the cast time.
		std::string code = render::AnimationCode(request.casting_anim);
		if (!caster.model->HasAnimation(code))
			code = render::AnimationCode(render::FallbackAnimation(request.casting_anim, caster.race));
		if (caster.model->HasAnimation(code))
			duration += static_cast<int>(caster.model->AnimationMs(code));
	}
	for (const Emitter &emitter : m_records[request.effect].stages[request.stage]) {
		if (emitter.def < 1 || level < emitter.min_level)
			continue;
		render::SpellParticles::Frame frame;
		uint32_t binding = 0;
		uint16_t on = 0;
		if (emitter.target == 0) {
			if (!request.at_point)
				continue;
			frame.position = request.point;
		}
		else if (emitter.target == 1 || emitter.target == 3) {
			on = emitter.target == 1 ? request.caster : request.target;
			Actor actor;
			if (!on || !find(on, actor))
				continue;
			if (emitter.attach == 0)
				frame.position = actor.placement.translation; // where they stand, staying there
			else {
				binding = static_cast<uint32_t>(on) << 8 | static_cast<uint32_t>(emitter.attach & 0xff);
				if (!Locate(binding, find, frame))
					continue;
			}
		}
		else
			continue; // 2: on the missile, not yet
		uint32_t tag = static_cast<uint32_t>(on) << 16 | static_cast<uint16_t>(request.spell);
		m_particles.Create(emitter.def, tag, duration, m_density, m_opacity, frame, binding);
	}
}

void SpellEffects::Update(uint64_t now_ms, const render::Vec3 &camera, const FindActor &find)
{
	m_particles.Update(now_ms, camera,
		[&](uint32_t binding, render::SpellParticles::Frame &frame) { return Locate(binding, find, frame); });
}
