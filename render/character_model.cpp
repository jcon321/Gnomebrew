#include "character_model.h"

#include "actor_tags.h"
#include "skeleton.h"

#include <algorithm>
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

		// Models without their own idle borrow another model's (by model code).
		std::string AnimationSource(const std::string &code)
		{
			static const std::map<std::string, std::string> sources = {{"HUF", "ELF"}, {"ERF", "ELF"}, {"HIF", "ELF"},
				{"DAF", "ELF"}, {"HAF", "ELF"}, {"BAF", "ELF"}, {"ERM", "HUM"}, {"HIM", "HUM"}, {"DAM", "HUM"},
				{"HAM", "HUM"}, {"GNM", "DWM"}, {"HOM", "DWM"}, {"GNF", "DWF"}, {"HOF", "DWF"}, {"OGM", "OGF"},
				{"TRM", "OGF"}, {"TRF", "OGF"}, {"IKF", "IKM"}};
			auto it = sources.find(code);
			return it == sources.end() ? code : it->second;
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
		// Each animation from the model itself, else (player races) from the model it borrows the
		// idle from, else from the elf of the same gender, the classic models with every animation.
		std::vector<std::string> sources = {code};
		if (player_race)
			sources = {code, AnimationSource(code), code.back() == 'F' ? "ELF" : "ELM"};
		for (const char *name : {"P01", "L01", "L02", "L03", "L04", "L05", "D05"}) {
			for (const auto &from : sources) {
				SkeletonAnimation animation = LoadAnimation(source->wld, skeleton, name, code, from);
				if (animation.length == 0)
					continue;
				if (from != code) {
					// A borrowed animation keeps this model's bone lengths; only the rotations carry over.
					for (size_t i = 0; i < animation.frames.size(); ++i)
						for (auto &frame : animation.frames[i])
							frame.shift = skeleton.bones[i].pose.shift;
				}
				model->m_animations[name] = std::move(animation);
				break;
			}
		}
		auto idle = model->m_animations.find("P01");
		std::vector<Transform> bones = PoseSkeleton(skeleton, idle == model->m_animations.end() ? nullptr : &idle->second, 0);
		model->m_head_z = 0;
		for (size_t i = 0; i < skeleton.bones.size(); ++i) {
			const std::string &name = skeleton.bones[i].name;
			if (name.size() > 6 && name.compare(name.size() - 6, 6, "HE_DAG") == 0)
				model->m_head_z = bones[i].translation.z;
		}

		// Texture names follow RRRPPMMVV.BMP (race, part, material, variant); swap in the
		// equipped material where the model has one, and the face on the head.
		auto texture_for = [&](const std::string &name, int &slot) {
			slot = -1;
			std::string upper = Upper(name);
			if (upper.size() != 13 || upper.compare(9, 4, ".BMP") != 0 || upper.compare(0, 3, code) != 0)
				return name;
			slot = SlotForPart(upper.substr(3, 2));
			if (slot < 0)
				return name;
			unsigned material = appearance.equip[slot] <= 99 ? appearance.equip[slot] : 0;
			char swapped[16];
			if (slot == 0) {
				snprintf(swapped, sizeof(swapped), "%sHE%02u%u%c.BMP", code.c_str(), 0u, appearance.face % 10u, upper[8]);
				return source->textures.Exists(swapped) ? std::string(swapped) : name;
			}
			snprintf(swapped, sizeof(swapped), "%s%s%02u%s.BMP", code.c_str(), upper.substr(3, 2).c_str(), material,
				upper.substr(7, 2).c_str());
			return source->textures.Exists(swapped) ? std::string(swapped) : name;
		};

		model->m_min_z = 1e30f;
		model->m_max_z = -1e30f;
		WldMesh mesh;
		for (int index : skeleton.meshes) {
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
			std::map<uint16_t, size_t> batch_of;
			for (const auto &t : mesh.triangles) {
				if (t.material >= mesh.materials.size())
					continue;
				const WldMaterial &material = mesh.materials[t.material];
				MaterialInfo info = DescribeRenderMethod(material.render_method);
				if (!info.visible)
					continue;
				auto found = batch_of.find(t.material);
				if (found == batch_of.end()) {
					CharacterModel::Batch batch;
					batch.material.blend = info.blend;
					batch.material.opacity = info.opacity;
					batch.material.two_sided = info.blend != Blend::Opaque;
					int slot = -1;
					if (!material.textures.empty())
						batch.material.texture = source->textures.Get(texture_for(material.textures[0], slot), info.masked);
					if (slot >= 0 && (appearance.tint[slot] >> 24) != 0)
						batch.tint = appearance.tint[slot] & 0xffffff;
					found = batch_of.emplace(t.material, model->m_batches.size()).first;
					model->m_batches.push_back(std::move(batch));
				}
				auto &indices = model->m_batches[found->second].indices;
				indices.insert(indices.end(), {base + t.v[0], base + t.v[1], base + t.v[2]});
			}
		}
		if (model->m_positions.empty()) {
			error = "model " + code + " has no meshes";
			return nullptr;
		}
		model->m_skeleton = std::move(skeleton);
		if (player_race) {
			// Player race models come in assorted sizes; each is scaled to the race's height.
			if (model->m_max_z > model->m_min_z)
				model->m_scale = (appearance.height > 0 ? appearance.height : RaceHeight(appearance.race)) /
				                 (model->m_max_z - model->m_min_z);
		}
		else {
			// EQPlayer::SetHeights: the actor's scale is the height / 5 (6 when it's not given;
			// races 63, and 42 and 120 neuter, a little under half that), its origin 3.125 * scale over the feet.
			float height = appearance.height > 0 ? appearance.height : 6;
			if (appearance.race == 63 || ((appearance.race == 42 || appearance.race == 120) && appearance.gender == 2))
				height *= 0.45f;
			model->m_scale = height * 0.2f;
			model->m_origin_over_feet = 3.125f * model->m_scale;
		}
		model->m_min_z *= model->m_scale;
		model->m_max_z *= model->m_scale;
		model->m_head_z *= model->m_scale;
		if (player_race)
			model->m_origin_over_feet = -model->m_min_z;
		return model;
	}

	uint32_t CharacterModel::AnimationMs(const std::string &animation) const
	{
		auto it = m_animations.find(animation);
		return it == m_animations.end() ? 0 : static_cast<uint32_t>(it->second.length * it->second.frame_ms);
	}

	void CharacterModel::Draw(Rasterizer &rasterizer, const Transform &placement, const Vec3 &light_direction, float ambient,
		float diffuse, uint64_t time_ms, const std::string &animation) const
	{
		auto it = m_animations.find(animation);
		if (it == m_animations.end())
			it = m_animations.find("P01");
		std::vector<Transform> bones = PoseSkeleton(m_skeleton, it == m_animations.end() ? nullptr : &it->second, time_ms);
		Transform scaled = placement;
		scaled.scale *= m_scale;
		for (auto &bone : bones)
			bone = scaled * bone;
		m_vertices.resize(m_positions.size());
		Vec3 light = Normalize(light_direction);
		std::vector<float> intensity(m_positions.size());
		for (size_t i = 0; i < m_positions.size(); ++i) {
			const Transform &bone = bones[m_bones[i]];
			m_vertices[i].position = bone.Apply(m_positions[i]);
			m_vertices[i].u = m_uvs[i * 2];
			m_vertices[i].v = m_uvs[i * 2 + 1];
			intensity[i] = ambient + diffuse * std::max(0.0f, Dot(bone.ApplyDirection(m_normals[i]), light));
		}
		for (const auto &batch : m_batches) {
			// Tints multiply the light, per batch, so recolor the batch's vertices first.
			float tr = ((batch.tint >> 16) & 0xff) / 255.0f, tg = ((batch.tint >> 8) & 0xff) / 255.0f,
			      tb = (batch.tint & 0xff) / 255.0f;
			for (uint32_t index : batch.indices)
				m_vertices[index].color = PackLight(intensity[index] * tr, intensity[index] * tg, intensity[index] * tb);
			rasterizer.Draw(m_vertices.data(), m_vertices.size(), batch.indices.data(), batch.indices.size(), batch.material);
		}
	}

	size_t CharacterModel::TriangleCount() const
	{
		size_t count = 0;
		for (const auto &batch : m_batches)
			count += batch.indices.size() / 3;
		return count;
	}
}
