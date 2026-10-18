#include "character_model.h"

#include "actor_tags.h"
#include "animations.h"
#include "skeleton.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>

namespace render
{
	namespace
	{
		// Body part codes in texture names (e.g. HUMCH0201.BMP: chest, material 02, piece 01).
		int SlotForPart(const std::string &part)
		{
			static const std::map<std::string, int> slots = {
				{"HE", 0}, {"CH", 1}, {"UA", 2}, {"FA", 3}, {"HN", 4}, {"LG", 5}, {"FT", 6}};
			auto it = slots.find(part);
			return it == slots.end() ? -1 : it->second;
		}

		std::string Upper(std::string s)
		{
			for (char &c : s)
				if (c >= 'a' && c <= 'z')
					c = static_cast<char>(c - 'a' + 'A');
			return s;
		}

		// SwapHead: the head mesh (<tag>HE<nn>_DMSPRITEDEF) a helm material puts on. Material 7 is
		// chain (plate on an Iksar); crowns and the like (240 and up) are item models over the bare head.
		int HeadForMaterial(unsigned material, uint16_t race)
		{
			if (material == 7)
				return race == 128 ? 3 : 2;
			return material < 240 ? static_cast<int>(material) : 0;
		}

		// SwapHead tints only these of the head's materials with the helm's color, not the face.
		bool TintedByHelm(const std::string &name, uint16_t race)
		{
			if (race == 2)
				return name.compare(0, 9, "BAMHE0003") == 0 || name.compare(0, 9, "BAMHE0004") == 0 ||
				       name.compare(0, 7, "CHAIN01") == 0;
			return name.compare(0, 4, "HELM") == 0 || name.compare(0, 5, "CHAIN") == 0;
		}

		bool Digits(const std::string &s, size_t from, size_t count)
		{
			for (size_t i = from; i < from + count; ++i)
				if (i >= s.size() || s[i] < '0' || s[i] > '9')
					return false;
			return true;
		}

		// Whether a model is built on a player race's skeleton, its own or the one it plays
		// the animations of (the Qeynos citizens' is the elf male's, the Rivervale citizens' the dwarf's).
		bool OnPlayerSkeleton(const std::string &code)
		{
			static const char *const players[] = {"HUM", "HUF", "BAM", "BAF", "ERM", "ERF", "ELM", "ELF", "HIM", "HIF",
				"DAM", "DAF", "HAM", "HAF", "DWM", "DWF", "TRM", "TRF", "OGM", "OGF", "HOM", "HOF", "GNM", "GNF", "IKM",
				"IKF", "KEM", "KEF"};
			std::string source = AnimationSource(code);
			for (const char *player : players)
				if (code == player || source == player)
					return true;
			return false;
		}

		uint32_t PackLight(float r, float g, float b)
		{
			auto channel = [](float c) { return static_cast<uint32_t>(std::clamp(c, 0.0f, 1.0f) * 255.0f + 0.5f); };
			return (channel(r) << 16) | (channel(g) << 8) | channel(b);
		}
	}

	// Default player heights (the spawn size the server sends for each race). The models are
	// built at assorted sizes (the dwarf female is as tall as a human), so each is scaled to fit.
	float RaceHeight(uint16_t race)
	{
		switch (race) {
		case 2: return 7;    // barbarian
		case 4: return 5;    // wood elf
		case 6: return 5;    // dark elf
		case 7: return 5.5f; // half elf
		case 8: return 4;    // dwarf
		case 9: return 8;    // troll
		case 10: return 9;   // ogre
		case 11: return 3.5f; // halfling
		case 12: return 3;   // gnome
		case 130: return 7;  // vah shir
		default: return 6;   // human, erudite, high elf, iksar
		}
	}

	std::string RaceModelCode(uint16_t race, uint8_t gender)
	{
		return IsPlayerRace(race) ? ActorTag(race, gender) : std::string();
	}

	bool CharacterModels::AddSource(const std::string &eq_dir, const std::string &name)
	{
		auto source = std::make_unique<Source>();
		std::vector<uint8_t> data;
		if (!source->archive.Open(eq_dir + "/" + name + ".s3d"))
			return false;
		if (!source->archive.Read(name + ".wld", data) || !source->wld.Load(std::move(data)))
			return false;
		source->textures.AddArchive(&source->archive);
		m_sources.push_back(std::move(source));
		return true;
	}

	bool CharacterModels::Open(const std::string &eq_dir, std::string &error, bool creatures)
	{
		m_sources.clear();
		if (!AddSource(eq_dir, "global_chr")) {
			error = "Couldn't read global_chr.s3d in " + eq_dir;
			return false;
		}
		for (const char *name : {"global2_chr", "global3_chr", "global4_chr", "global5_chr", "global6_chr", "global7_chr"}) {
			if (creatures || std::string(name) == "global4_chr" || std::string(name) == "global7_chr")
				AddSource(eq_dir, name);
		}
		return true;
	}

	bool CharacterModels::AddZone(const std::string &eq_dir, const std::string &zone)
	{
		// CDisplay::StartWorldDisplay loads <zone>_chr2 and <zone>_chr (and <zone>2_chr for a few zones).
		// Zone models are searched before the global ones.
		size_t before = m_sources.size();
		for (const std::string &name : {zone + "_chr", zone + "_chr2", zone + "2_chr"})
			AddSource(eq_dir, name);
		std::rotate(m_sources.begin(), m_sources.begin() + static_cast<long>(before), m_sources.end());
		return m_sources.size() > before;
	}

	std::unique_ptr<CharacterModel> CharacterModels::Build(const CharacterAppearance &appearance, std::string &error)
	{
		// CDisplay::CreatePlayerActor: the race's actor, or race 75's when the zone doesn't have it.
		auto find = [this](const std::string &tag, Source *&source, int &actor) {
			for (auto &candidate : m_sources) {
				actor = candidate->wld.Find(tag + "_ACTORDEF");
				if (actor >= 0 && candidate->wld.ActorSkeleton(actor) >= 0) {
					source = candidate.get();
					return true;
				}
			}
			return false;
		};
		std::string code = ActorTag(appearance.race, appearance.gender);
		Source *source = nullptr;
		int actor = -1;
		if (!find(code, source, actor)) {
			std::string fallback = ActorTag(75, appearance.gender);
			if (!find(fallback, source, actor)) {
				error = "model " + code + " not found";
				return nullptr;
			}
			code = fallback;
		}
		WldSkeleton skeleton;
		if (!source->wld.ReadSkeleton(source->wld.ActorSkeleton(actor), skeleton)) {
			error = "model " + code + " has no skeleton";
			return nullptr;
		}

		auto model = std::make_unique<CharacterModel>();
		model->m_code = code;
		bool player_race = IsPlayerRace(appearance.race) && code.size() == 3;
		// Every animation the model has, each bone from its own tracks or its source model's,
		// shared by everyone with this model.
		auto &animations = m_animation_sets[code];
		if (!animations) {
			std::vector<const WldFile *> wlds = {&source->wld};
			for (auto &other : m_sources)
				if (other.get() != source)
					wlds.push_back(&other->wld);
			auto set = std::make_shared<std::map<std::string, SkeletonAnimation>>();
			std::string from = AnimationSource(code);
			for (int id = 1; id < Anim::Count; ++id) {
				const char *name = AnimationCode(id);
				SkeletonAnimation animation = LoadAnimation(wlds, skeleton, name, code, from);
				if (animation.length > 0)
					(*set)[name] = std::move(animation);
			}
			animations = std::move(set);
		}
		model->m_animations = animations;
		auto idle = model->m_animations->find("P01");
		std::vector<Transform> bones = PoseSkeleton(skeleton, idle == model->m_animations->end() ? nullptr : &idle->second, 0);
		model->m_head_z = 0;
		for (size_t i = 0; i < skeleton.bones.size(); ++i) {
			const std::string &name = skeleton.bones[i].name;
			if (name.size() > 6 && name.compare(name.size() - 6, 6, "HE_DAG") == 0)
				model->m_head_z = bones[i].translation.z;
		}

		// The head: the helm's mesh in place of the bare head, where the model has one (Vah Shir
		// helms are item models). An NPC's helm byte picks it, else the helm material does.
		bool npc_texture = appearance.texture != 0xff;
		std::string bare_head = code + "HE00_DMSPRITEDEF";
		int head_mesh = -1;
		auto head_named = [&](int head) {
			char name[64];
			snprintf(name, sizeof(name), "%sHE%02d_DMSPRITEDEF", code.c_str(), head);
			int index = head > 0 ? source->wld.Find(name) : -1;
			return index >= 0 && source->wld.Fragments()[index].type == 0x36 ? index : -1;
		};
		if (appearance.race != 130) {
			if (npc_texture && appearance.helm < 0xf0)
				head_mesh = head_named(appearance.helm);
			if (head_mesh < 0)
				head_mesh = head_named(HeadForMaterial(appearance.equip[0], appearance.race));
		}

		// Material names follow RRRPPMMVV_MDF (race, part, material, variant). A player's slots each
		// swap in their material (CDisplay_SwapMaterial), and the face goes on the head
		// (CDisplay_SwapFace); an NPC's body texture goes on every part instead (SwapNPCMaterials),
		// untinted. Skeletons (race 60) only have the one.
		unsigned body_texture = appearance.texture < 31 && !(appearance.race == 60) ? appearance.texture : 0;
		auto material_for = [&](const WldMaterial &material, int &slot) {
			slot = -1;
			std::string upper = Upper(material.name);
			if (upper.size() <= 8 || upper.compare(0, 3, code) != 0 || !Digits(upper, 5, 4))
				return material;
			std::string swapped = upper;
			if (npc_texture) {
				char digits[4];
				snprintf(digits, sizeof(digits), "%02u", body_texture);
				swapped.replace(5, 2, digits);
			}
			else {
				if (upper.size() != 13 || upper.compare(9, 4, "_MDF") != 0)
					return material;
				slot = SlotForPart(upper.substr(3, 2));
				if (slot < 0)
					return material;
				char name[16];
				if (slot == 0)
					snprintf(name, sizeof(name), "%sHE%02u%u%c_MDF", code.c_str(), 0u, appearance.face % 10u, upper[8]);
				else
					snprintf(name, sizeof(name), "%s%s%02u%s_MDF", code.c_str(), upper.substr(3, 2).c_str(),
						appearance.equip[slot] <= 99 ? appearance.equip[slot] : 0u, upper.substr(7, 2).c_str());
				swapped = name;
			}
			WldMaterial found;
			return swapped != upper && source->wld.FindMaterial(swapped, found) ? found : material;
		};
		auto tint_for = [&](const WldMaterial &material, int slot) {
			uint32_t tint = 0;
			if (slot >= 1 && slot < EquipSlots)
				tint = appearance.tint[slot];
			else if (slot < 0 && TintedByHelm(Upper(material.name), appearance.race))
				tint = appearance.tint[0];
			return (tint >> 24) != 0 ? tint & 0xffffff : 0xffffffu;
		};

		model->m_min_z = 1e30f;
		model->m_max_z = -1e30f;
		WldMesh mesh;
		for (int &index : skeleton.meshes) {
			if (head_mesh >= 0 && Upper(source->wld.Fragments()[index].name) == bare_head)
				index = head_mesh;
			if (!source->wld.ReadMesh(index, mesh))
				continue;
			uint32_t base = static_cast<uint32_t>(model->m_positions.size());
			size_t v = 0;
			for (const auto &piece : mesh.pieces)
				for (uint16_t k = 0; k < piece.count && v < mesh.positions.size(); ++k, ++v)
					model->m_bones.push_back(piece.bone < bones.size() ? piece.bone : 0);
			model->m_bones.resize(base + mesh.positions.size(), 0);
			for (size_t i = 0; i < mesh.positions.size(); ++i) {
				Vec3 p{mesh.positions[i].x, mesh.positions[i].y, mesh.positions[i].z};
				model->m_positions.push_back(p);
				model->m_normals.push_back(Normalize({mesh.normals[i].x, mesh.normals[i].y, mesh.normals[i].z}));
				model->m_uvs.push_back(mesh.uvs[i * 2]);
				model->m_uvs.push_back(mesh.uvs[i * 2 + 1]);
				float z = bones[model->m_bones[base + i]].Apply(p).z;
				model->m_min_z = std::min(model->m_min_z, z);
				model->m_max_z = std::max(model->m_max_z, z);
			}
			std::map<uint16_t, size_t> batch_of; // SIZE_MAX: not drawn
			for (const auto &t : mesh.triangles) {
				if (t.material >= mesh.materials.size())
					continue;
				auto found = batch_of.find(t.material);
				if (found == batch_of.end()) {
					int slot = -1;
					WldMaterial material = material_for(mesh.materials[t.material], slot);
					MaterialInfo info = DescribeRenderMethod(material.render_method);
					if (!info.visible) {
						batch_of.emplace(t.material, SIZE_MAX);
						continue;
					}
					CharacterModel::Batch batch;
					batch.material.blend = info.blend;
					batch.material.opacity = info.opacity;
					batch.material.two_sided = info.blend != Blend::Opaque;
					if (!material.textures.empty())
						batch.material.texture = source->textures.Get(material.textures[0], info.masked);
					batch.tint = tint_for(material, slot);
					found = batch_of.emplace(t.material, model->m_batches.size()).first;
					model->m_batches.push_back(std::move(batch));
				}
				if (found->second == SIZE_MAX)
					continue;
				auto &indices = model->m_batches[found->second].indices;
				indices.insert(indices.end(), {base + t.v[0], base + t.v[1], base + t.v[2]});
			}
		}
		if (model->m_positions.empty()) {
			error = "model " + code + " has no meshes";
			return nullptr;
		}
		model->m_skeleton = std::move(skeleton);
		if (player_race || OnPlayerSkeleton(code)) {
			// Player race models come in assorted sizes (the female halfling's is half again the
			// male's); each is scaled to the race's height, its feet on the floor. Ours, and also for
			// the citizens made from them (the Rivervale citizens are the halfling's, the Qeynos ones
			// the human's), which SetHeights' size / 5 below would leave as mismatched.
			float height = appearance.height > 0 ? appearance.height : player_race ? RaceHeight(appearance.race) : 6;
			if (model->m_max_z > model->m_min_z)
				model->m_scale = height / (model->m_max_z - model->m_min_z);
			model->m_min_z *= model->m_scale;
			model->m_max_z *= model->m_scale;
			model->m_head_z *= model->m_scale;
			model->m_origin_over_feet = -model->m_min_z;
		}
		else {
			// EQPlayer::SetHeights: the actor's scale is the height / 5 (6 when it's not given;
			// races 63, and 42 and 120 neuter, a little under half that), its origin 3.125 * scale over the feet.
			float height = appearance.height > 0 ? appearance.height : 6;
			if (appearance.race == 63 || ((appearance.race == 42 || appearance.race == 120) && appearance.gender == 2))
				height *= 0.45f;
			model->m_scale = height * 0.2f;
			model->m_min_z *= model->m_scale;
			model->m_max_z *= model->m_scale;
			model->m_head_z *= model->m_scale;
			model->m_origin_over_feet = 3.125f * model->m_scale;
		}
		return model;
	}

	uint32_t CharacterModel::AnimationMs(const std::string &animation) const
	{
		auto it = m_animations->find(animation);
		return it == m_animations->end() ? 0 : static_cast<uint32_t>(it->second.length * it->second.frame_ms);
	}

	void CharacterModel::Draw(Renderer &renderer, const Transform &placement, const Vec3 &light_direction, float ambient,
		float diffuse, uint64_t time_ms, const std::string &animation, bool loop) const
	{
		auto it = m_animations->find(animation);
		if (it == m_animations->end())
			it = m_animations->find("P01");
		std::vector<Transform> bones = PoseSkeleton(m_skeleton, it == m_animations->end() ? nullptr : &it->second, time_ms, loop);
		Transform scaled = placement;
		scaled.scale *= m_scale;
		for (auto &bone : bones)
			bone = scaled * bone;
		// Each bone as a matrix, once (Quat::Rotate expanded), with the light turned into its
		// space: a vertex is then a matrix multiply and a dot product.
		Vec3 light = Normalize(light_direction);
		m_bone_matrices.resize(bones.size());
		for (size_t b = 0; b < bones.size(); ++b) {
			const Quat &q = bones[b].rotation;
			float r[3][3] = {
				{1 - 2 * (q.y * q.y + q.z * q.z), 2 * (q.x * q.y - q.w * q.z), 2 * (q.x * q.z + q.w * q.y)},
				{2 * (q.x * q.y + q.w * q.z), 1 - 2 * (q.x * q.x + q.z * q.z), 2 * (q.y * q.z - q.w * q.x)},
				{2 * (q.x * q.z - q.w * q.y), 2 * (q.y * q.z + q.w * q.x), 1 - 2 * (q.x * q.x + q.y * q.y)},
			};
			BoneMatrix &out = m_bone_matrices[b];
			for (int i = 0; i < 3; ++i)
				for (int j = 0; j < 3; ++j)
					out.m[i][j] = r[i][j] * bones[b].scale;
			out.translation = bones[b].translation;
			out.light = {r[0][0] * light.x + r[1][0] * light.y + r[2][0] * light.z,
				r[0][1] * light.x + r[1][1] * light.y + r[2][1] * light.z,
				r[0][2] * light.x + r[1][2] * light.y + r[2][2] * light.z};
		}
		m_vertices.resize(m_positions.size());
		m_intensity.resize(m_positions.size());
		for (size_t i = 0; i < m_positions.size(); ++i) {
			const BoneMatrix &bone = m_bone_matrices[m_bones[i]];
			const Vec3 &p = m_positions[i];
			m_vertices[i].position = {bone.m[0][0] * p.x + bone.m[0][1] * p.y + bone.m[0][2] * p.z + bone.translation.x,
				bone.m[1][0] * p.x + bone.m[1][1] * p.y + bone.m[1][2] * p.z + bone.translation.y,
				bone.m[2][0] * p.x + bone.m[2][1] * p.y + bone.m[2][2] * p.z + bone.translation.z};
			m_vertices[i].u = m_uvs[i * 2];
			m_vertices[i].v = m_uvs[i * 2 + 1];
			m_intensity[i] = ambient + diffuse * std::max(0.0f, Dot(m_normals[i], bone.light));
		}
		// The light at half (it reaches past 1 in full light), doubled back with each batch's tint
		// by the renderer's vertex light, so every batch draws the same vertices.
		for (size_t i = 0; i < m_vertices.size(); ++i)
			m_vertices[i].color = PackLight(m_intensity[i] * 0.5f, m_intensity[i] * 0.5f, m_intensity[i] * 0.5f);
		bool first = true;
		for (const auto &batch : m_batches) {
			float tr = ((batch.tint >> 16) & 0xff) / 255.0f, tg = ((batch.tint >> 8) & 0xff) / 255.0f,
			      tb = (batch.tint & 0xff) / 255.0f;
			renderer.SetVertexLight({tr * 2, tg * 2, tb * 2}, {0, 0, 0});
			renderer.Draw(m_vertices.data(), m_vertices.size(), batch.indices.data(), batch.indices.size(), batch.material, !first);
			first = false;
		}
		renderer.SetVertexLight({1, 1, 1}, {0, 0, 0});
	}

	size_t CharacterModel::TriangleCount() const
	{
		size_t count = 0;
		for (const auto &batch : m_batches)
			count += batch.indices.size() / 3;
		return count;
	}
}
