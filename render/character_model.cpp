#include "character_model.h"

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
		static const std::map<uint16_t, const char *> codes = {{1, "HU"}, {2, "BA"}, {3, "ER"}, {4, "EL"}, {5, "HI"},
			{6, "DA"}, {7, "HA"}, {8, "DW"}, {9, "TR"}, {10, "OG"}, {11, "HO"}, {12, "GN"}, {128, "IK"}, {130, "KE"}};
		auto it = codes.find(race);
		if (it == codes.end())
			return {};
		return std::string(it->second) + (gender == 1 ? "F" : "M");
	}

	bool CharacterModels::Open(const std::string &eq_dir, std::string &error)
	{
		m_sources.clear();
		for (const char *name : {"global_chr", "global4_chr", "global7_chr"}) {
			auto source = std::make_unique<Source>();
			std::vector<uint8_t> data;
			if (!source->archive.Open(eq_dir + "/" + name + ".s3d"))
				continue;
			if (!source->archive.Read(std::string(name) + ".wld", data) || !source->wld.Load(std::move(data)))
				continue;
			source->textures.AddArchive(&source->archive);
			m_sources.push_back(std::move(source));
		}
		if (m_sources.empty()) {
			error = "Couldn't read global_chr.s3d in " + eq_dir;
			return false;
		}
		return true;
	}

	std::unique_ptr<CharacterModel> CharacterModels::Build(const CharacterAppearance &appearance, std::string &error)
	{
		std::string code = RaceModelCode(appearance.race, appearance.gender);
		if (code.empty()) {
			error = "no model for race " + std::to_string(appearance.race);
			return nullptr;
		}
		Source *source = nullptr;
		int actor = -1;
		for (auto &candidate : m_sources) {
			actor = candidate->wld.Find(code + "_ACTORDEF");
			if (actor >= 0) {
				source = candidate.get();
				break;
			}
		}
		WldSkeleton skeleton;
		if (!source || !source->wld.ReadSkeleton(source->wld.ActorSkeleton(actor), skeleton)) {
			error = "model " + code + " not found";
			return nullptr;
		}

		auto model = std::make_unique<CharacterModel>();
		model->m_code = code;
		model->m_idle = LoadAnimation(source->wld, skeleton, "P01", code, code);
		if (model->m_idle.length == 0) {
			model->m_idle = LoadAnimation(source->wld, skeleton, "P01", code, AnimationSource(code));
			// A borrowed animation keeps this model's bone lengths; only the rotations carry over.
			for (size_t i = 0; i < model->m_idle.frames.size(); ++i)
				for (auto &frame : model->m_idle.frames[i])
					frame.shift = skeleton.bones[i].pose.shift;
		}
		std::vector<Transform> bones = PoseSkeleton(skeleton, &model->m_idle, 0);
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
		if (model->m_max_z > model->m_min_z)
			model->m_scale = (appearance.height > 0 ? appearance.height : RaceHeight(appearance.race)) / (model->m_max_z - model->m_min_z);
		model->m_min_z *= model->m_scale;
		model->m_max_z *= model->m_scale;
		model->m_head_z *= model->m_scale;
		return model;
	}

	void CharacterModel::Draw(Rasterizer &rasterizer, const Transform &placement, const Vec3 &light_direction, float ambient,
		float diffuse, uint64_t now_ms) const
	{
		std::vector<Transform> bones = PoseSkeleton(m_skeleton, &m_idle, now_ms);
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
