#include "spell_effects.h"

#include "animations.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>

namespace
{
	constexpr size_t RecordSize = 0x10c;
	constexpr size_t OldRecordSize = 0xae8, OldStageSize = 0x3a0, OldRecords = 256;
	constexpr float TurnStep = 6.2831855f / 512; // a 512th of a turn, t3dFloatFastSine's

	int32_t Int(const uint8_t *r, size_t o)
	{
		int32_t v;
		std::memcpy(&v, r + o, 4);
		return v;
	}

	float Float(const uint8_t *r, size_t o)
	{
		float v;
		std::memcpy(&v, r + o, 4);
		return v;
	}

	int16_t Short(const uint8_t *r, size_t o)
	{
		int16_t v;
		std::memcpy(&v, r + o, 2);
		return v;
	}

	std::string Text(const uint8_t *r, size_t o, size_t size)
	{
		size_t len = 0;
		while (len < size && r[o + len])
			++len;
		return std::string(reinterpret_cast<const char *>(r + o), len);
	}

	bool SameText(const std::string &a, const char *b)
	{
		size_t i = 0;
		for (; i < a.size() && b[i]; ++i)
			if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
				return false;
		return i == a.size() && !b[i];
	}

	// CreatePartEffects' PCLOUD0..6 (t3dDefineParticleCloud): sprites (I_SNOWFLAKESPRITE's until the
	// emitter's own), flags 0x701 (fading out), pulled at their gravity along (0, 0, -1).
	render::ParticleCloudDef Pcloud(int index)
	{
		using Def = render::ParticleCloudDef;
		struct Row
		{
			uint32_t max;
			float gravity;
			Def::Spawn spawn;
			uint32_t duration_ms;
			render::Vec3 normal;
			float radius, angle;
			uint32_t lifespan_ms;
			float speed;
			uint32_t interval_ms;
			float size;
		};
		static const Row Rows[7] = {
			{500, 0, Def::Spawn::Cone, 1600, {0, 0, -1}, 0, 20, 1000, 3, 20, 0.05f},
			{1000, 0, Def::Spawn::Sphere, 1600, {0, 0, 0}, 5, 0, 1000, 3, 2, 0.05f},
			{500, 5, Def::Spawn::Cone, 1600, {0, 0, 1}, 0, 20, 1000, 7, 20, 0.05f},
			{1000, 0, Def::Spawn::Disk, 5000, {0, 0, -1}, 20, 0, 1000, 7, 2, 0.2f},
			{1000, 5, Def::Spawn::Disk, 5000, {0, 0, 1}, 4, 0, 1000, 7, 2, 0.2f},
			{2000, 0, Def::Spawn::Cone, 5000, {-1, 0, 0}, 0, 20, 2000, 20, 1, 0.2f},
			{1000, 5, Def::Spawn::Ring, 5000, {0, 0, 1}, 5, 0, 1000, 7, 2, 0.2f},
		};
		const Row &row = Rows[index];
		Def def;
		def.kind = Def::Kind::Sprites;
		def.flags = 0x701;
		def.fog = false;
		def.max_particles = row.max;
		def.gravity = row.gravity;
		def.gravity_direction = {0, 0, -1};
		def.spawn = row.spawn;
		def.duration_ms = row.duration_ms;
		def.normal = row.normal;
		def.radius = row.radius;
		def.angle = row.angle;
		def.lifespan_ms = row.lifespan_ms;
		def.speed = row.speed;
		def.interval_ms = row.interval_ms;
		def.size = row.size;
		def.color = 0xffffff;
		return def;
	}

	// GetDagName's bone as an attach point of Bone's: 1 the head, 2 the right hand, 3 the left, 4
	// and 5 the right and left feet, else the chest.
	int DagAttach(int dag)
	{
		switch (dag) {
		case 1: return 3;
		case 2: return 5;
		case 3: return 4;
		case 4: return 7;
		case 5: return 6;
		default: return 2;
		}
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
		record.name = Text(r, 0, 64);
		for (int stage = 0; stage < 3; ++stage)
			record.sounds[stage] = Int(r, 0x40 + stage * 0x44);
		for (int stage = 0; stage < 3; ++stage)
			for (int i = 0; i < 4; ++i) {
				size_t o = 0x44 + stage * 0x44 + i * 16;
				record.stages[stage][i] = {Int(r, o), Int(r, o + 4), Int(r, o + 8), Int(r, o + 12)};
			}
		m_records.push_back(std::move(record));
	}

	// spells.eff (loadSpellEffectsFromFile reads all 256 or complains).
	m_old.clear();
	std::ifstream old_file(eq_dir + "/spells.eff", std::ios::binary);
	std::vector<uint8_t> old((std::istreambuf_iterator<char>(old_file)), std::istreambuf_iterator<char>());
	for (size_t at = 0; at + OldRecordSize <= old.size() && m_old.size() < OldRecords; at += OldRecordSize) {
		OldRecord record;
		for (int s = 0; s < 3; ++s) {
			const uint8_t *r = old.data() + at + s * OldStageSize;
			OldStage &stage = record.stages[s];
			stage.on = Text(r, 0x68, 32);
			stage.sound = Int(r, 0x224);
			for (int i = 0; i < 3; ++i) {
				OldEmitter &e = stage.emitters[i];
				e.kind = Int(r, 0x94 + i * 4);
				e.sprite = Text(r, 0x08 + i * 0x20, 32);
				e.dag = Int(r, 0x88 + i * 4);
				// t3dSetPCloudColor's: blue, green, red (as eqgfx paints it), when any's set.
				const uint8_t *c = r + 0x228 + i * 4;
				if (c[0] || c[1] || c[2])
					e.color = static_cast<uint32_t>(c[2]) << 16 | static_cast<uint32_t>(c[1]) << 8 | c[0];
				e.gravity = Float(r, 0x234 + i * 4);
				e.normal = {Float(r, 0x240 + i * 12), Float(r, 0x244 + i * 12), Float(r, 0x248 + i * 12)};
				e.radius = Float(r, 0x264 + i * 4);
				e.angle = Float(r, 0x270 + i * 4);
				e.lifespan_ms = static_cast<uint32_t>(Int(r, 0x27c + i * 4));
				e.speed = Float(r, 0x288 + i * 4);
				e.interval_ms = static_cast<uint32_t>(Int(r, 0x294 + i * 4));
				e.size = Float(r, 0x2a0 + i * 4);
			}
			stage.sprite_kind = Int(r, 0x220);
			// DoSpriteEffect's: as many as are named, in a row.
			for (int j = 0; j < 12; ++j) {
				std::string name = Text(r, 0xa0 + j * 0x20, 32);
				if (name.empty())
					break;
				OldSprite sprite;
				sprite.actor = name + "_ACTORDEF";
				const uint8_t *t = r + 0x2ac + j * 3;
				sprite.tint = static_cast<uint32_t>(t[0]) << 16 | static_cast<uint32_t>(t[1]) << 8 | t[2];
				sprite.turn = Short(r, 0x300 + j * 2);
				sprite.climb = Short(r, 0x318 + j * 2);
				sprite.radius = Float(r, 0x330 + j * 4);
				sprite.kind = Short(r, 0x360 + j * 2);
				sprite.size = Float(r, 0x378 + j * 4);
				stage.sprites.push_back(sprite);
			}
		}
		m_old.push_back(std::move(record));
	}
	if (m_old.size() < OldRecords)
		fprintf(stderr, "[spell effects] spells.eff: %zu of %zu records\n", m_old.size(), OldRecords);
	std::vector<uint8_t> wld;
	if (m_equipment.Open(eq_dir + "/gequip.s3d", true) && m_equipment.Read("gequip.wld", wld) &&
		m_equipment_wld.Load(std::move(wld)))
		m_textures.AddArchive(&m_equipment);
	else
		fprintf(stderr, "[spell effects] can't read gequip.wld: no old effects\n");
	return m_particles.Load(eq_dir, error);
}

void SpellEffects::Clear()
{
	m_particles.Clear();
	m_clouds.clear();
	m_sprites.clear();
	m_later.clear();
}

size_t SpellEffects::EmitterCount() const
{
	return m_particles.EmitterCount() + m_clouds.size() + m_sprites.size();
}

size_t SpellEffects::ParticleCount() const
{
	size_t count = m_particles.ParticleCount();
	for (const OldCloud &cloud : m_clouds)
		count += cloud.cloud->Count();
	return count;
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

bool SpellEffects::PlaceBone(uint32_t binding, const FindActor &find, render::Transform &bone)
{
	Actor actor;
	if (!find(static_cast<uint16_t>(binding >> 8), actor) || !actor.model)
		return false;
	std::vector<render::Transform> placed;
	actor.model->PlaceBones(actor.placement, actor.time_ms, actor.animation, actor.loop, {Bone(*actor.model, binding & 0xff)},
		placed);
	bone = placed[0];
	return true;
}

bool SpellEffects::Locate(uint32_t binding, const FindActor &find, render::SpellParticles::Frame &frame)
{
	Actor actor;
	render::Transform bone;
	if (!find(static_cast<uint16_t>(binding >> 8), actor) || !PlaceBone(binding, find, bone))
		return false;
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

bool SpellEffects::UsesNew(int effect) const
{
	// EQ_Spell's +0xf8: a record in the file with a name.
	return m_new_effects && effect >= 1 && static_cast<size_t>(effect) < m_records.size() && !m_records[effect].name.empty();
}

int SpellEffects::CastDuration(const Request &request, const Actor *caster) const
{
	int duration = request.duration_ms;
	if (request.casting_anim && caster && caster->model) {
		// The casting animation's length on top of the cast time.
		std::string code = render::AnimationCode(request.casting_anim);
		if (!caster->model->HasAnimation(code))
			code = render::AnimationCode(render::FallbackAnimation(request.casting_anim, caster->race));
		if (caster->model->HasAnimation(code))
			duration += static_cast<int>(caster->model->AnimationMs(code));
	}
	return duration;
}

void SpellEffects::Start(const Request &request, const FindActor &find)
{
	if (request.delay_ms > 0) {
		Request later = request;
		later.delay_ms = 0;
		m_later.emplace_back(m_now_ms + static_cast<uint64_t>(request.delay_ms), later);
		return;
	}
	if (request.stage < 0 || request.stage > 2)
		return;
	bool uses_new = UsesNew(request.effect);
	bool uses_old = !uses_new && request.old_effect >= 0 && static_cast<size_t>(request.old_effect) < m_old.size();
	if (!uses_new && !uses_old)
		return;
	// DoSpellEffect: the stage's sound first (none for the missile's; slot 0 has no wave).
	int sound = uses_new ? m_records[request.effect].sounds[request.stage] : m_old[request.old_effect].stages[request.stage].sound;
	uint16_t on = request.stage == 2 ? request.target : request.caster;
	if (request.stage != 1 && sound > 0 && on && m_play_sound)
		m_play_sound(request, on, sound);
	if (uses_new)
		StartNew(request, find);
	else
		StartOld(request, find);
}

void SpellEffects::FlashLights(std::vector<render::Vec3> &at) const
{
	for (const SpriteEffect &e : m_sprites)
		if (e.kind == 1)
			at.push_back(e.center + e.offset);
}

void SpellEffects::StartNew(const Request &request, const FindActor &find)
{
	if (m_density <= 0 || m_opacity <= 0)
		return;
	Actor caster;
	bool have_caster = request.caster && find(request.caster, caster);
	int level = have_caster && caster.player ? caster.level : 1;
	int duration = CastDuration(request, have_caster ? &caster : nullptr);
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

const SpellEffects::Blit *SpellEffects::FindBlit(const std::string &name)
{
	auto it = m_blits.find(name);
	if (it == m_blits.end()) {
		Blit blit;
		WldMaterial sprite;
		if (m_equipment_wld.FindBlitSprite(name, sprite)) {
			render::MaterialInfo info = render::DescribeRenderMethod(sprite.render_method);
			for (const std::string &texture : sprite.textures)
				if (const render::Texture *frame = m_textures.Get(texture, info.masked))
					blit.frames.push_back(frame);
			blit.frame_ms = sprite.frame_ms;
		}
		it = m_blits.emplace(name, std::move(blit)).first;
	}
	return it->second.frames.empty() ? nullptr : &it->second;
}

void SpellEffects::MakeCloud(int pcloud, const OldEmitter &emitter, const render::Vec3 &at, uint32_t binding, int duration_ms)
{
	// MakePCloudInstance: the PCLOUD with the emitter's own sprite and whatever else it sets.
	render::ParticleCloudDef def = Pcloud(pcloud);
	const Blit *blit = FindBlit(emitter.sprite);
	if (!blit)
		blit = FindBlit("I_SNOWFLAKESPRITE");
	if (!blit)
		return;
	def.frames = blit->frames;
	def.frame_ms = blit->frame_ms;
	if (emitter.gravity != 0)
		def.gravity = emitter.gravity;
	if (emitter.normal.x != 0 || emitter.normal.y != 0 || emitter.normal.z != 0)
		def.normal = emitter.normal;
	if (emitter.radius != 0)
		def.radius = emitter.radius;
	if (emitter.angle != 0)
		def.angle = emitter.angle;
	if (emitter.lifespan_ms)
		def.lifespan_ms = emitter.lifespan_ms;
	if (emitter.speed != 0)
		def.speed = emitter.speed;
	if (emitter.interval_ms)
		def.interval_ms = emitter.interval_ms;
	if (emitter.size != 0)
		def.size = emitter.size;
	if (emitter.color)
		def.color = emitter.color;
	if (duration_ms > 0)
		def.duration_ms = static_cast<uint32_t>(duration_ms);
	OldCloud cloud;
	cloud.cloud = std::make_unique<render::ParticleCloud>(def, 1.0f, nullptr, nullptr);
	cloud.binding = binding;
	render::Transform place;
	place.translation = at;
	cloud.cloud->SetEmitter(place); // a bound one's moved onto its bone before each Update
	m_clouds.push_back(std::move(cloud));
}

void SpellEffects::StartOld(const Request &request, const FindActor &find)
{
	if (request.stage == 1)
		return; // the missiles aren't done
	const OldStage &stage = m_old[request.old_effect].stages[request.stage];
	Actor caster, target;
	bool have_caster = request.caster && find(request.caster, caster);
	bool have_target = request.target && find(request.target, target);
	// CEverQuest_StartCasting's duration for the casting's clouds; the landing's keep their own.
	int duration = request.stage == 0 ? CastDuration(request, have_caster ? &caster : nullptr) : 0;

	// DoSpriteEffect: one sprite at the target (or the spot), or a ring of them around the target
	// (the caster, casting).
	if (stage.sprite_kind == 1 || stage.sprite_kind == 2) {
		const Actor *at = have_target ? &target : have_caster ? &caster : nullptr;
		size_t count = stage.sprite_kind == 1 ? std::min<size_t>(1, stage.sprites.size()) : stage.sprites.size();
		bool ring = stage.sprite_kind == 2;
		if ((ring && !have_target) || (!ring && !request.at_point && !at))
			count = 0;
		for (size_t j = 0; j < count; ++j) {
			const OldSprite &sprite = stage.sprites[j];
			SpriteEffect effect;
			effect.blit = FindBlit(sprite.actor);
			if (!effect.blit)
				continue;
			effect.tint = sprite.tint ? sprite.tint : 0xffffff;
			effect.kind = sprite.kind;
			effect.size = sprite.size;
			effect.center = ring || !request.at_point ? at->placement.translation : request.point;
			// EQEffect's: a kind 2 circles out `radius` (2 if none) at least 2 big; a ring's spaced
			// around the target, theirs.
			if (ring) {
				effect.on = request.target;
				effect.heading = static_cast<float>(j) * (512.0f / static_cast<float>(count));
			}
			if (effect.kind == 2) {
				effect.speed = sprite.radius != 0 ? sprite.radius : 2.0f;
				effect.turn = sprite.turn;
				effect.climb = sprite.climb;
				effect.size = std::max(effect.size, 2.0f);
			}
			// How long it lasts: kind 1 3/4 of a second, 2 two seconds on a spawn (forever
			// otherwise: not here), any other that many ms.
			if (effect.kind == 1)
				effect.end_ms = m_now_ms + 750;
			else if (effect.kind == 2)
				effect.end_ms = effect.on ? m_now_ms + 2000 : 0;
			else
				effect.end_ms = effect.kind > 0 ? m_now_ms + static_cast<uint64_t>(effect.kind) : 0;
			if (!effect.end_ms)
				continue;
			effect.physics_ms = m_now_ms;
			m_sprites.push_back(effect);
		}
	}

	// DoParticleEffect: the emitters on the caster (or the target), the later ones for the higher
	// levels.
	int level = have_caster && caster.player ? caster.level : 1;
	uint16_t on_id = SameText(stage.on, "Target") && have_target ? request.target : request.caster;
	Actor *on = on_id == request.target && have_target ? &target : have_caster ? &caster : nullptr;
	if (!on)
		return;
	for (int i = 0; i < 3; ++i) {
		const OldEmitter &e = stage.emitters[i];
		if (e.kind == -1)
			continue;
		if (level < 39 && (i == 2 || (i == 1 && level < 24)))
			continue;
		uint32_t dag = static_cast<uint32_t>(on_id) << 8 | static_cast<uint32_t>(DagAttach(e.dag));
		render::Vec3 at = request.at_point ? request.point : on->placement.translation;
		// On the floor under them (CDisplay::GetFloorHeight), or 10 over them when the normal's down.
		render::Vec3 floor = at;
		if (!request.at_point)
			floor.z = e.normal.z >= 0 ? (m_floor ? m_floor(at) : at.z) : at.z + 10;
		switch (e.kind) {
		case 0:
			MakeCloud(0, e, at, static_cast<uint32_t>(on_id) << 8 | 4, duration);
			MakeCloud(0, e, at, static_cast<uint32_t>(on_id) << 8 | 5, duration);
			break;
		case 1: MakeCloud(5, e, at, request.at_point ? 0 : dag, duration); break;
		case 2: MakeCloud(1, e, at, request.at_point ? 0 : dag, duration); break;
		case 3: MakeCloud(3, e, floor, 0, duration); break;
		case 4: MakeCloud(4, e, floor, 0, duration); break;
		case 5:
			MakeCloud(6, e, floor, 0, duration);
			MakeCloud(6, e, floor, request.at_point ? 0 : dag, duration);
			break;
		default: break;
		}
	}
}

void SpellEffects::Update(uint64_t now_ms, const render::Vec3 &camera, const FindActor &find)
{
	m_now_ms = now_ms;
	for (size_t i = 0; i < m_later.size();) {
		if (m_later[i].first > now_ms) {
			++i;
			continue;
		}
		Request request = m_later[i].second;
		m_later.erase(m_later.begin() + static_cast<std::ptrdiff_t>(i));
		Start(request, find);
	}
	m_particles.Update(now_ms, camera,
		[&](uint32_t binding, render::SpellParticles::Frame &frame) { return Locate(binding, find, frame); });

	static const render::ParticleCloud::Collide no_collision;
	for (size_t i = 0; i < m_clouds.size();) {
		OldCloud &cloud = m_clouds[i];
		render::Transform bone;
		bool gone = cloud.binding && !PlaceBone(cloud.binding, find, bone); // its spawn's gone
		if (!gone) {
			if (cloud.binding)
				cloud.cloud->SetEmitter(bone);
			cloud.cloud->Update(now_ms, camera, no_collision);
		}
		if (gone || cloud.cloud->Done(now_ms)) {
			m_clouds[i] = std::move(m_clouds.back());
			m_clouds.pop_back();
		}
		else
			++i;
	}
	UpdateSprites(now_ms, find);
}

void SpellEffects::UpdateSprites(uint64_t now_ms, const FindActor &find)
{
	for (size_t i = 0; i < m_sprites.size();) {
		SpriteEffect &e = m_sprites[i];
		bool gone = e.end_ms && now_ms > e.end_ms;
		if (!gone && e.on) {
			// CDisplay::ProcessEffects: a kind 2 back on its spawn, and no more pushing out in its
			// last fifth of a second (it falls back in).
			Actor actor;
			if (!find(e.on, actor))
				gone = true; // its spawn's gone, and EQPlayer's destructor takes it
			else if (e.kind == 2) {
				e.center = actor.placement.translation;
				if (e.end_ms - std::min(e.end_ms, now_ms) < 201)
					e.speed = 0;
			}
		}
		if (gone) {
			m_sprites[i] = m_sprites.back();
			m_sprites.pop_back();
			continue;
		}
		if (now_ms - std::min(now_ms, e.physics_ms) > 32) {
			// process_physics, for an effect: turned and climbed by the time (in 50 ms steps, at
			// most 3), pushed along its heading (and its climb, for kind 2) at 0.8 of its speed onto
			// 0.625 of its last push, and moved that far.
			float k = static_cast<float>(now_ms - e.physics_ms) * 0.02f;
			k = k < 0.001f ? 0 : std::min(k, 3.0f);
			e.physics_ms = now_ms;
			e.heading = std::fmod(e.heading + k * e.turn, 512.0f);
			if (e.heading < 0)
				e.heading += 512;
			e.pitch = std::clamp(e.pitch + k * e.climb, -128.0f, 128.0f);
			render::Vec3 push;
			if (e.speed != 0) {
				push.x = std::cos(e.heading * TurnStep) * e.speed;
				push.y = std::sin(e.heading * TurnStep) * e.speed;
				if (e.kind == 2 && e.pitch != 0)
					push.z = std::sin(e.pitch * TurnStep) * e.speed;
			}
			e.velocity = e.velocity * 0.625f + push * 0.8f;
			e.offset = e.on && e.kind == 2 ? e.velocity * k : e.offset + e.velocity * k;
		}
		++i;
	}
}

void SpellEffects::Draw(render::Renderer &renderer)
{
	m_particles.Draw(renderer);
	for (OldCloud &cloud : m_clouds)
		cloud.cloud->Draw(renderer, m_now_ms, 0, {1, 1, 1});
	DrawSprites(renderer);
}

void SpellEffects::DrawSprites(render::Renderer &renderer)
{
	if (m_sprites.empty())
		return;
	// renderPointLight: the blit sprite facing the camera, `size` out to its corners at 45 degrees,
	// in its tint, its frames stepped on with the time.
	const render::Camera &camera = renderer.CurrentCamera();
	render::Vec3 forward = render::Normalize(camera.forward);
	render::Vec3 right = render::Normalize(render::Cross(forward, camera.up));
	render::Vec3 up = render::Cross(right, forward);
	renderer.SetVertexLight({1, 1, 1}, {0, 0, 0});
	for (const SpriteEffect &e : m_sprites) {
		const Blit &blit = *e.blit;
		const render::Texture *texture = blit.frames[(m_now_ms / std::max<uint32_t>(blit.frame_ms, 1)) % blit.frames.size()];
		render::Vec3 at = e.center + e.offset;
		render::Vec3 r = right * (e.size * 0.7071068f), u = up * (e.size * 0.7071068f);
		const render::Vec3 corners[4] = {at - r + u, at + r + u, at + r - u, at - r - u};
		const float uv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
		render::Vertex vertices[4];
		for (int k = 0; k < 4; ++k) {
			vertices[k].position = corners[k];
			vertices[k].u = uv[k][0];
			vertices[k].v = uv[k][1];
			vertices[k].color = e.tint;
		}
		static const uint32_t indices[6] = {0, 1, 2, 0, 2, 3};
		render::Material material;
		material.blend = render::Blend::Additive;
		material.two_sided = true;
		material.fog = false; // as the clouds' (fogged, their dark squares would show the fog's color)
		material.texel_alpha = true;
		material.texture = texture;
		renderer.Draw(vertices, 4, indices, 6, material);
	}
}
