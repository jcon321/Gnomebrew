#include "character_model.h"

#include "actor_tags.h"
#include "animations.h"
#include "skeleton.h"

#include <algorithm>
#include <cctype>
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

		// CDisplay_SwapHead2 (@004a1512): the Velious helms, 240 and 241, as each race's own item
		// model by gender (IT530..IT656 in gequip2.s3d; 241 is a barbarian's or an ogre's, plate on
		// the rest). `hair` goes false for those that hide a woman's hair (and an Erudite's hood).
		unsigned VeliousHelm(unsigned material, uint16_t race, uint8_t gender, bool &hair)
		{
			hair = true;
			bool female = gender != 0;
			auto hides = [&](unsigned it) {
				hair = false;
				return it;
			};
			if (material == 241) {
				if (race == 2)
					return female ? hides(530) : 537;
				if (race == 10)
					return female ? 641 : 646;
				return race == 0 || (race > 12 && race != 128) ? 241 : 3;
			}
			if (material != 240)
				return material;
			switch (race) {
			case 1: return female ? hides(620) : 627;
			case 2: return female ? hides(530) : 537;
			case 3: return hides(female ? 570 : 575);
			case 4: return female ? hides(561) : 565;
			case 5: return female ? 600 : 605;
			case 6: return female ? hides(540) : 545;
			case 7: return female ? 590 : 595;
			case 8: return female ? 550 : 557;
			case 9: return female ? 650 : 655;
			case 10: return female ? 640 : 645;
			case 11: return female ? 610 : 615;
			case 12: return female ? 580 : 585;
			case 128: return female ? 630 : 635;
			default: return 240;
			}
		}

		// CDisplay_SwapMaterial (@004a1eb7)'s body materials: the Velious armor (17..23) as the
		// older armor it stands in for when it isn't loaded (leather, chain, plate, then again, and
		// 23 a monk's 4); and 4 and 23, the monks' own, as leather on anyone else.
		unsigned BodyMaterial(unsigned material, uint8_t class_id, bool velious)
		{
			bool monk = class_id == 7;
			if (material == 4 || material == 23)
				return !monk ? 1 : material == 23 && !velious ? 4 : material;
			if (material >= 17 && material <= 22 && !velious)
				return (material - 17) % 3 + 1;
			return material;
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

		// IsShieldModel (@004a9e6b): the IT numbers that are shields.
		bool IsShieldModel(unsigned it)
		{
			return (it >= 200 && it <= 228) || (it >= 10530 && it <= 10544) || (it >= 10664 && it <= 10671) ||
			       (it >= 11001 && it <= 11020);
		}

		// A 0x34 as t3dDefineParticleCloud keeps it, with its sprite's frames.
		bool CloudDef(const WldFile &wld, TextureCache &textures, int index, ParticleCloudDef &def)
		{
			WldParticleCloud cloud;
			if (!wld.ReadParticleCloud(index, cloud) || cloud.kind < 1 || cloud.kind > 4 || cloud.spawn_type > 5)
				return false;
			auto vec = [](const WldVec3 &v) { return Vec3{v.x, v.y, v.z}; };
			def.kind = static_cast<ParticleCloudDef::Kind>(cloud.kind);
			def.spawn = static_cast<ParticleCloudDef::Spawn>(cloud.spawn_type);
			def.flags = cloud.flags;
			def.max_particles = cloud.max_particles;
			def.low = vec(cloud.low);
			def.high = vec(cloud.high);
			def.normal = vec(cloud.normal);
			def.radius = cloud.radius;
			def.angle = cloud.angle;
			def.speed = cloud.speed;
			def.lifespan_ms = cloud.lifespan_ms;
			def.interval_ms = cloud.interval_ms;
			def.gravity = cloud.gravity;
			def.gravity_direction = vec(cloud.gravity_direction);
			def.size = cloud.size;
			def.color = cloud.color & 0xffffff;
			MaterialInfo info = DescribeRenderMethod(cloud.sprite.render_method);
			for (const std::string &name : cloud.sprite.textures)
				if (const Texture *texture = textures.Get(name, info.masked))
					def.frames.push_back(texture);
			def.frame_ms = cloud.sprite.frame_ms;
			return true;
		}

		uint32_t PackLight(float r, float g, float b)
		{
			auto channel = [](float c) { return static_cast<uint32_t>(std::clamp(c, 0.0f, 1.0f) * 255.0f + 0.5f); };
			return (channel(r) << 16) | (channel(g) << 8) | channel(b);
		}
	}

	// Default player heights (SetHeights' table, and the spawn size the server sends for each race).
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

	bool CharacterModels::AddSource(std::vector<std::unique_ptr<Source>> &to, const std::string &eq_dir, const std::string &name)
	{
		auto source = std::make_unique<Source>();
		std::vector<uint8_t> data;
		if (!source->archive.Open(eq_dir + "/" + name + ".s3d"))
			return false;
		if (!source->archive.Read(name + ".wld", data) || !source->wld.Load(std::move(data)))
			return false;
		source->textures.AddArchive(&source->archive);
		to.push_back(std::move(source));
		return true;
	}

	bool CharacterModels::Open(const std::string &eq_dir, std::string &error, bool creatures, bool velious_textures)
	{
		m_sources.clear();
		m_items.clear();
		m_armor.clear();
		m_velious_textures = velious_textures;
		if (!AddSource(m_sources, eq_dir, "global_chr")) {
			error = "Couldn't read global_chr.s3d in " + eq_dir;
			return false;
		}
		// Without the creatures, still the player races' own: global3_chr's the old models' socials
		// (S05..S28) and later idles, global4_chr the Iksar, global7_chr the Vah Shir.
		for (const char *name : {"global2_chr", "global3_chr", "global4_chr", "global5_chr", "global6_chr", "global7_chr"}) {
			if (creatures || std::string(name) == "global3_chr" || std::string(name) == "global4_chr" ||
				std::string(name) == "global7_chr")
				AddSource(m_sources, eq_dir, name);
		}
		// CDisplay::InitWorld's order (GEquip6 goes with the low-poly Vah Shir we use).
		for (const char *name : {"gequip", "gequip8", "gequip2", "gequip5", "gequip4", "gequip3", "gequip6"})
			AddSource(m_items, eq_dir, name);
		// CDisplay::InitWorld: Global<n>_amr.s3d for each LoadArmor<n> that's on. Without them the
		// materials fall back (BodyMaterial).
		if (velious_textures) {
			for (int n = 17; n <= 23; ++n)
				if (!AddSource(m_armor, eq_dir, "global" + std::to_string(n) + "_amr"))
					m_velious_textures = false;
		}
		return true;
	}

	bool CharacterModels::AddZone(const std::string &eq_dir, const std::string &zone)
	{
		// CDisplay::StartWorldDisplay loads <zone>_chr2 and <zone>_chr (and <zone>2_chr for a few zones).
		// Zone models are searched before the global ones.
		size_t before = m_sources.size();
		for (const std::string &name : {zone + "_chr", zone + "_chr2", zone + "2_chr"})
			AddSource(m_sources, eq_dir, name);
		std::rotate(m_sources.begin(), m_sources.begin() + static_cast<long>(before), m_sources.end());
		return m_sources.size() > before;
	}

	std::unique_ptr<CharacterModel> CharacterModels::Build(const CharacterAppearance &requested, std::string &error)
	{
		// EQPlayer::EQPlayer: on a player race (LegalPlayerRace) an NPC's body texture dresses the chest
		// to the feet instead, and from then on it's dressed slot by slot; the head and the weapons stay
		// as sent.
		CharacterAppearance appearance = requested;
		if (appearance.texture < 0x80 && (appearance.race <= 12 || appearance.race == 128 || appearance.race == 130)) {
			for (int slot = 1; slot <= 6; ++slot)
				appearance.equip[slot] = appearance.texture;
			appearance.texture = 0xff;
		}
		if (appearance.texture == 0xff)
			for (int slot = 1; slot <= 6; ++slot)
				appearance.equip[slot] = BodyMaterial(appearance.equip[slot], appearance.class_id, m_velious_textures);

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
		model->m_head_bone = -1;
		for (size_t i = 0; i < skeleton.bones.size(); ++i) {
			const std::string &name = skeleton.bones[i].name;
			if (name.size() > 6 && name.compare(name.size() - 6, 6, "HE_DAG") == 0) {
				model->m_head_z = bones[i].translation.z;
				model->m_head_bone = static_cast<int>(i);
			}
		}

		// The head: the helm's mesh in place of the bare head, where the model has one. An NPC's
		// helm byte picks it, else the helm material does. CDisplay::SwapHead: the Velious helms
		// (VeliousHelm) and anything else from 240 are item models (IT<n>) on the bare head, hung
		// on the head itself (<tag>HE_DAG: CDisplay_GetDagPointerByITS's slot 0 is ActorInfo's
		// DagHead, not DagHeadPoint), where they're made to fit; a Vah Shir's helms all are, 1..3
		// (and 240 as 3) IT661..663 on a woman, IT666..668 on a man.
		bool npc_texture = appearance.texture != 0xff;
		unsigned helm = appearance.equip[0];
		bool hair = true;
		unsigned helm_item = 0;
		if (appearance.race == 130) {
			unsigned m = helm == 240 ? 3 : helm;
			if (m >= 1 && m <= 3)
				helm_item = m + (appearance.gender == 1 ? 660 : 665);
		}
		else {
			if (helm >= 240 && helm < 256)
				helm = VeliousHelm(helm, appearance.race, appearance.gender, hair);
			if (helm >= 240)
				helm_item = helm;
		}
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
				head_mesh = head_named(HeadForMaterial(helm, appearance.race));
		}

		// A robe (chest 10 to 16) as CDisplay_SwapMaterial puts it on: the robed body
		// (<tag>01_DMSPRITEDEF, CDisplay_SwapBody) with the bare chest (material 00) under it, and its
		// cloth (CLK04nn..CLK10nn, ReplaceCloakMaterials: the robe - 6) in the chest's tint. Not on a Vah Shir.
		unsigned chest = appearance.equip[1];
		bool robe = !npc_texture && appearance.race != 130 && chest >= 10 && chest <= 16;
		std::string bare_body = code + "_DMSPRITEDEF";
		int robe_body = -1;
		if (robe) {
			robe_body = source->wld.Find(code + "01_DMSPRITEDEF");
			if (robe_body >= 0 && source->wld.Fragments()[robe_body].type != 0x36)
				robe_body = -1;
		}

		// Material names follow RRRPPMMVV_MDF (race, part, material, variant). A player's slots each
		// swap in their material (CDisplay_SwapMaterial), and the face goes on the head
		// (CDisplay_SwapFace); a creature's body texture goes on every part instead (SwapNPCMaterials),
		// untinted. Skeletons (race 60) only have the one.
		unsigned body_texture = appearance.texture < 31 && !(appearance.race == 60) ? appearance.texture : 0;
		// The material's textures are in `textures_from`'s archive: the model's own, or the Velious
		// armor's.
		auto material_for = [&](const WldMaterial &material, int &slot, Source *&textures_from) {
			slot = -1;
			textures_from = source;
			std::string upper = Upper(material.name);
			if (robe && upper.size() > 9 && upper.compare(0, 3, "CLK") == 0 && Digits(upper, 3, 2)) {
				slot = 1;
				char name[64];
				snprintf(name, sizeof(name), "CLK%02u%s", chest - 6, upper.c_str() + 5);
				WldMaterial found;
				return source->wld.FindMaterial(name, found) ? found : material;
			}
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
						slot == 1 && robe ? 0u : appearance.equip[slot] <= 99 ? appearance.equip[slot] : 0u,
						upper.substr(7, 2).c_str());
				swapped = name;
			}
			WldMaterial found;
			if (swapped == upper)
				return material;
			if (source->wld.FindMaterial(swapped, found))
				return found;
			for (auto &armor : m_armor)
				if (armor->wld.FindMaterial(swapped, found)) {
					textures_from = armor.get();
					return found;
				}
			return material;
		};
		auto tint_for = [&](const WldMaterial &material, int slot) {
			uint32_t tint = 0;
			if (slot >= 1 && slot < EquipSlots)
				tint = appearance.tint[slot];
			else if (slot < 0 && TintedByHelm(Upper(material.name), appearance.race))
				tint = appearance.tint[0];
			// CDisplay::ReplaceMaterial: untinted (white) only when the RGB is all zero. The
			// use_tint byte isn't checked (crafted armor is 0x007387C8).
			return (tint & 0xffffff) != 0 ? tint & 0xffffff : 0xffffffu;
		};

		model->m_min_z = 1e30f;
		model->m_max_z = -1e30f;
		// SwapHead: under a helm that hides it (VeliousHelm), a woman's hair (<tag>HE00<face>2) and
		// an Erudite's hood (CLKER*) aren't drawn.
		bool appending_head = false;
		auto hidden_by_helm = [&](const WldMaterial &material) {
			if (hair || !appending_head)
				return false;
			std::string upper = Upper(material.name);
			return (upper.size() > 8 && upper.compare(2, 5, "FHE00") == 0 && upper[8] == '2') ||
			       (appearance.race == 3 && upper.compare(0, 5, "CLKER") == 0);
		};
		// A mesh's vertices on their bones (the body), or all on the one `point` an item hangs from.
		auto append = [&](const WldMesh &mesh, Source &from, int point) {
			bool body = point < 0;
			uint32_t base = static_cast<uint32_t>(model->m_positions.size());
			if (body) {
				size_t v = 0;
				for (const auto &piece : mesh.pieces)
					for (uint16_t k = 0; k < piece.count && v < mesh.positions.size(); ++k, ++v)
						model->m_bones.push_back(piece.bone < bones.size() ? piece.bone : 0);
			}
			model->m_bones.resize(base + mesh.positions.size(), body ? 0 : static_cast<uint16_t>(point));
			for (size_t i = 0; i < mesh.positions.size(); ++i) {
				Vec3 p{mesh.positions[i].x, mesh.positions[i].y, mesh.positions[i].z};
				model->m_positions.push_back(p);
				model->m_normals.push_back(Normalize({mesh.normals[i].x, mesh.normals[i].y, mesh.normals[i].z}));
				model->m_uvs.push_back(mesh.uvs[i * 2]);
				model->m_uvs.push_back(mesh.uvs[i * 2 + 1]);
				if (body) {
					float z = bones[model->m_bones[base + i]].Apply(p).z;
					model->m_min_z = std::min(model->m_min_z, z);
					model->m_max_z = std::max(model->m_max_z, z);
				}
			}
			std::map<uint16_t, size_t> batch_of; // SIZE_MAX: not drawn
			for (const auto &t : mesh.triangles) {
				if (t.material >= mesh.materials.size())
					continue;
				auto found = batch_of.find(t.material);
				if (found == batch_of.end()) {
					int slot = -1;
					Source *textures_from = &from;
					WldMaterial material = body ? material_for(mesh.materials[t.material], slot, textures_from) : mesh.materials[t.material];
					MaterialInfo info = DescribeRenderMethod(material.render_method);
					if (!info.visible || (body && hidden_by_helm(mesh.materials[t.material]))) {
						batch_of.emplace(t.material, SIZE_MAX);
						continue;
					}
					CharacterModel::Batch batch;
					batch.material.blend = info.blend;
					batch.material.opacity = info.opacity;
					batch.material.two_sided = info.blend != Blend::Opaque;
					if (!material.textures.empty())
						batch.material.texture = textures_from->textures.Get(material.textures[0], info.masked);
					batch.tint = body ? tint_for(material, slot) : 0xffffffu;
					found = batch_of.emplace(t.material, model->m_batches.size()).first;
					model->m_batches.push_back(std::move(batch));
				}
				if (found->second == SIZE_MAX)
					continue;
				auto &indices = model->m_batches[found->second].indices;
				indices.insert(indices.end(), {base + t.v[0], base + t.v[1], base + t.v[2]});
			}
		};
		WldMesh mesh;
		for (int &index : skeleton.meshes) {
			std::string name = Upper(source->wld.Fragments()[index].name);
			if (head_mesh >= 0 && name == bare_head)
				index = head_mesh;
			else if (robe_body >= 0 && name == bare_body)
				index = robe_body;
			appending_head = name == bare_head;
			if (source->wld.ReadMesh(index, mesh))
				append(mesh, *source, -1);
		}
		appending_head = false;

		// EQPlayer::DisplayWeaponsAndArmor -> CDisplay_UpdateItemSlot2: IT<primary> in the right hand
		// (<tag>R_POINT_DAG), IT<secondary> in the left (L_POINT_DAG), or on the arm (SHIELD_POINT_DAG)
		// when it's a shield. CDisplay::ChangeDag hangs an item's skeleton (IT<n>_HS_DEF, here in its
		// base pose) there, else its mesh (IT<n>_DMSPRITEDEF). Models without the point hold nothing;
		// race 14 never does.
		auto append_item = [&](unsigned it, int point) {
			std::vector<Source *> order;
			for (auto &from : m_items)
				order.push_back(from.get());
			for (auto &from : m_sources)
				order.push_back(from.get());
			char name[64];
			snprintf(name, sizeof(name), "IT%u_HS_DEF", it);
			for (Source *from : order) {
				int index = from->wld.Find(name);
				WldSkeleton item;
				if (index < 0 || from->wld.Fragments()[index].type != 0x10 || !from->wld.ReadSkeleton(index, item))
					continue;
				std::vector<Transform> pose = PoseSkeleton(item, nullptr, 0);
				for (int part : item.meshes) {
					WldMesh piece;
					if (from->wld.ReadMesh(part, piece)) {
						SkinMesh(piece, pose);
						append(piece, *from, point);
					}
				}
				// Meshes a bone carries itself (a torch's IT36_O_DAG), all on that bone, and its
				// particle clouds (the flame and smoke on IT36_P1_DAG and IT36_P2_DAG).
				for (size_t b = 0; b < item.bones.size(); ++b) {
					WldMesh piece;
					if (item.bones[b].mesh >= 0 && from->wld.ReadMesh(item.bones[b].mesh, piece)) {
						piece.pieces = {{static_cast<uint16_t>(piece.positions.size()), static_cast<uint16_t>(b)}};
						SkinMesh(piece, pose);
						append(piece, *from, point);
					}
					CharacterModel::Emitter emitter;
					if (item.bones[b].particle_cloud >= 0 && CloudDef(from->wld, from->textures, item.bones[b].particle_cloud, emitter.def)) {
						emitter.bone = static_cast<uint16_t>(point);
						emitter.item = it;
						emitter.item_bone = static_cast<uint16_t>(b);
						emitter.local = pose[b];
						model->m_emitters.push_back(std::move(emitter));
					}
				}
				return;
			}
			snprintf(name, sizeof(name), "IT%u_DMSPRITEDEF", it);
			for (Source *from : order) {
				int index = from->wld.Find(name);
				WldMesh item;
				if (index >= 0 && from->wld.Fragments()[index].type == 0x36 && from->wld.ReadMesh(index, item)) {
					append(item, *from, point);
					return;
				}
			}
		};
		auto bone_named = [&](const std::string &name) {
			for (size_t i = 0; i < skeleton.bones.size(); ++i)
				if (Upper(skeleton.bones[i].name) == name)
					return static_cast<int>(i);
			return -1;
		};
		if (helm_item != 0 && !npc_texture && appearance.race != 14) {
			int bone = bone_named(code + "HE_DAG");
			if (bone >= 0)
				append_item(helm_item, bone);
		}
		for (int slot : {7, 8}) {
			unsigned it = appearance.equip[slot];
			if (it == 0 || appearance.race == 14)
				continue;
			const char *point = slot == 7 ? "R_POINT_DAG" : IsShieldModel(it) ? "SHIELD_POINT_DAG" : "L_POINT_DAG";
			int bone = bone_named(code + point);
			if (bone >= 0)
				append_item(it, bone);
		}
		if (model->m_positions.empty()) {
			error = "model " + code + " has no meshes";
			return nullptr;
		}
		model->m_skeleton = std::move(skeleton);
		// EQPlayer::SetHeights: the actor's scale is the height / 5 (the race's default when it's not
		// given; races 63, and 42 and 120 neuter, a little under half that), its origin 3.125 * scale
		// over the feet. The models are built for that, feet ~3.125 under the origin and ~6.5 tall
		// once their bones' scales are applied (the halfling male's pelvis scales him by 1.6).
		float height = appearance.height > 0 ? appearance.height : player_race ? RaceHeight(appearance.race) : 6;
		if (appearance.race == 63 || ((appearance.race == 42 || appearance.race == 120) && appearance.gender == 2))
			height *= 0.45f;
		model->m_scale = height * 0.2f;
		model->m_min_z *= model->m_scale;
		model->m_max_z *= model->m_scale;
		model->m_head_z *= model->m_scale;
		model->m_origin_over_feet = 3.125f * model->m_scale;
		return model;
	}

	float CharacterModel::HeadZAt(uint64_t time_ms, const std::string &animation, bool loop) const
	{
		auto it = m_animations->find(animation);
		if (m_head_bone < 0 || it == m_animations->end())
			return m_head_z;
		std::vector<Transform> bones = PoseSkeleton(m_skeleton, &it->second, time_ms, loop);
		return static_cast<size_t>(m_head_bone) < bones.size() ? bones[m_head_bone].translation.z * m_scale : m_head_z;
	}

	int CharacterModel::FindBone(const std::string &name) const
	{
		for (size_t i = 0; i < m_skeleton.bones.size(); ++i) {
			const std::string &bone = m_skeleton.bones[i].name;
			if (bone.size() == name.size() &&
				std::equal(bone.begin(), bone.end(), name.begin(), [](char a, char b) { return std::toupper(a) == std::toupper(b); }))
				return static_cast<int>(i);
		}
		return -1;
	}

	void CharacterModel::PlaceBones(const Transform &placement, uint64_t time_ms, const std::string &animation, bool loop,
		const std::vector<int> &bones, std::vector<Transform> &out) const
	{
		out.clear();
		auto it = m_animations->find(animation);
		if (it == m_animations->end())
			it = m_animations->find("P01");
		std::vector<Transform> posed = PoseSkeleton(m_skeleton, it == m_animations->end() ? nullptr : &it->second, time_ms, loop);
		Transform scaled = placement;
		scaled.scale *= m_scale;
		for (int bone : bones)
			out.push_back(scaled * (bone >= 0 && static_cast<size_t>(bone) < posed.size() ? posed[bone] : Transform{}));
	}

	void CharacterModel::PlaceEmitters(const Transform &placement, uint64_t time_ms, const std::string &animation, bool loop,
		std::vector<Transform> &out) const
	{
		out.clear();
		if (m_emitters.empty())
			return;
		auto it = m_animations->find(animation);
		if (it == m_animations->end())
			it = m_animations->find("P01");
		std::vector<Transform> bones = PoseSkeleton(m_skeleton, it == m_animations->end() ? nullptr : &it->second, time_ms, loop);
		Transform scaled = placement;
		scaled.scale *= m_scale;
		for (const Emitter &emitter : m_emitters)
			out.push_back(scaled * (emitter.bone < bones.size() ? bones[emitter.bone] : Transform{}) * emitter.local);
	}

	void HeldParticles::Draw(Renderer &renderer, const CharacterModel &model, const Transform &placement, uint64_t time_ms,
		const std::string &animation, bool loop, uint64_t now_ms, const Vec3 &ambient)
	{
		const std::vector<CharacterModel::Emitter> &emitters = model.Emitters();
		bool same = m_keys.size() == emitters.size();
		for (size_t i = 0; same && i < emitters.size(); ++i)
			same = m_keys[i] == Key{emitters[i].bone, emitters[i].item, emitters[i].item_bone};
		if (!same) {
			std::vector<Key> keys;
			std::vector<std::unique_ptr<ParticleCloud>> clouds;
			for (const CharacterModel::Emitter &emitter : emitters) {
				Key key{emitter.bone, emitter.item, emitter.item_bone};
				auto old = std::find(m_keys.begin(), m_keys.end(), key);
				if (old != m_keys.end() && m_clouds[old - m_keys.begin()])
					clouds.push_back(std::move(m_clouds[old - m_keys.begin()]));
				else // at CDisplay's highest density
					clouds.push_back(std::make_unique<ParticleCloud>(emitter.def, 1.0f, nullptr, nullptr));
				keys.push_back(key);
			}
			m_keys = std::move(keys);
			m_clouds = std::move(clouds);
		}
		if (m_clouds.empty())
			return;
		model.PlaceEmitters(placement, time_ms, animation, loop, m_emitters);
		static const ParticleCloud::Collide no_collision;
		Vec3 camera = renderer.CurrentCamera().position;
		for (size_t i = 0; i < m_clouds.size() && i < m_emitters.size(); ++i) {
			m_clouds[i]->SetEmitter(m_emitters[i]);
			m_clouds[i]->Update(now_ms, camera, no_collision);
			m_clouds[i]->Draw(renderer, now_ms, 0, ambient);
		}
	}

	void HeldParticles::Clear()
	{
		m_keys.clear();
		m_clouds.clear();
	}

	uint32_t CharacterModel::AnimationMs(const std::string &animation) const
	{
		auto it = m_animations->find(animation);
		return it == m_animations->end() ? 0 : static_cast<uint32_t>(it->second.length * it->second.frame_ms);
	}

	void CharacterModel::Draw(Renderer &renderer, const Transform &placement, const ModelLighting &lighting,
		uint64_t time_ms, const std::string &animation, bool loop) const
	{
		const std::vector<PointLight> *lights = lighting.lights;
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
		Vec3 light = Normalize(lighting.sun_direction * -1.0f);
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
			out.inverse_scale = bones[b].scale != 0 ? 1 / bones[b].scale : 1;
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
			m_intensity[i] = lighting.ambient + lighting.sun_color * std::max(0.0f, Dot(m_normals[i], bone.light));
		}
		if (lights && !lights->empty()) {
			std::vector<Vec3> strengths;
			for (const PointLight &light : *lights)
				strengths.push_back(light.Strength());
			for (size_t i = 0; i < m_positions.size(); ++i) {
				const BoneMatrix &bone = m_bone_matrices[m_bones[i]];
				const Vec3 &n = m_normals[i];
				Vec3 normal{(bone.m[0][0] * n.x + bone.m[0][1] * n.y + bone.m[0][2] * n.z) * bone.inverse_scale,
					(bone.m[1][0] * n.x + bone.m[1][1] * n.y + bone.m[1][2] * n.z) * bone.inverse_scale,
					(bone.m[2][0] * n.x + bone.m[2][1] * n.y + bone.m[2][2] * n.z) * bone.inverse_scale};
				Vec3 added{};
				for (size_t l = 0; l < lights->size(); ++l) {
					Vec3 d = (*lights)[l].position - m_vertices[i].position;
					float facing = Dot(d, normal);
					if (facing <= 0)
						continue;
					float distance2 = Dot(d, d);
					added = added + strengths[l] * (facing / (distance2 * std::sqrt(distance2)));
				}
				m_intensity[i] = m_intensity[i] + added;
			}
		}
		// The client's light stops at full (the texture as it is).
		for (Vec3 &v : m_intensity)
			v = {std::min(v.x, 1.0f), std::min(v.y, 1.0f), std::min(v.z, 1.0f)};
		// The light at half (it reaches past 1 in full light), doubled back with each batch's tint
		// by the renderer's vertex light, so every batch draws the same vertices.
		for (size_t i = 0; i < m_vertices.size(); ++i)
			m_vertices[i].color = PackLight(m_intensity[i].x * 0.5f, m_intensity[i].y * 0.5f, m_intensity[i].z * 0.5f);
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
