#include "character_model.h"

#include "actor_tags.h"
#include "animations.h"
#include "skeleton.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
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

		// CDisplay_GetItemType (@004aa22c): what a held item (its IT number) is to the animations.
		// 0 nothing, 3 a shield (IT200..300), 4 IT4, 5 IT61, 2 these, 1 anything else.
		int HeldItemType(unsigned it)
		{
			switch (it) {
			case 1: case 2: case 3: case 5: case 7: case 9: case 16: case 17: case 19: case 20: case 23: case 24:
			case 25: case 30: case 34: case 35: case 37: case 39: case 40: case 41: case 42: case 43: case 44:
			case 50: case 51: case 53: case 57: case 58: case 59: case 60: case 62:
				return 2;
			case 4:
				return 4;
			case 61:
				return 5;
			default:
				return it == 0 ? 0 : it >= 200 && it < 301 ? 3 : 1;
			}
		}

		// EQPlayer::SetAnimVariation (@005098d8): the variant (0..7, A..H) a Luclin model's animations
		// play, by what it holds.
		int AnimVariation(unsigned primary, unsigned secondary)
		{
			int held = HeldItemType(primary), off = HeldItemType(secondary);
			if (off != 0)
				return held != 0 ? 3 : off == 4 ? 7 : off == 3 ? 2 : 5;
			if (held != 0)
				return held != 1 ? 1 : 4;
			return 0;
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

		// CDisplay_GetRaceOffsetForAttachmentITs: where a race's Luclin items start (IT1000 + this is
		// its hair, IT5000 + this its helms); women's 30 on.
		unsigned AttachmentOffset(uint16_t race, uint8_t gender)
		{
			unsigned offset = gender == 1 ? 30 : 0;
			switch (race) {
			case 1: return offset + 360;
			case 2: return offset + 540;
			case 3: return offset + 480;
			case 4: return offset + 240;
			case 5: return offset + 300;
			case 6: return offset + 180;
			case 7: return offset + 60;
			case 8: return offset + 120;
			case 9: return offset + 660;
			case 10: return offset + 720;
			case 11: return offset + 780;
			case 128: return offset + 420;
			case 130: return offset + 600;
			default: return offset; // the gnome's are first
			}
		}

		// The hair colors (the table at 00602850, B G R bytes) as 0xRRGGBB, if `color` is one the race
		// can have (EQPlayer::GetAllowedHairColorIndexRange); else 0, and the item keeps its own.
		uint32_t HairTint(uint16_t race, uint8_t color)
		{
			static const uint32_t colors[24] = {0x2e1a0c, 0x432916, 0x4e3123, 0x7f513b, 0x650b06, 0xb93714, 0xd75532,
				0x8b721e, 0xccb361, 0xe1dd6c, 0xfbff81, 0xfdfac9, 0xffffff, 0xdedede, 0x808080, 0x6f8690, 0x3e585a,
				0x293e40, 0x121214, 0xc9e5fd, 0xc9fdfd, 0xe9c9fd, 0xcefdc9, 0x559b48};
			int low = 0, high = 18;
			switch (race) {
			case 5: low = 4; high = 17; break;
			case 6: low = 11; high = 19; break;
			case 9:
			case 10: high = 23; break;
			case 12: high = 22; break;
			case 128:
			case 130: high = 0; break;
			}
			return color < 24 && color >= low && color <= high ? colors[color] : 0;
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

	int LuclinModels::Index(uint16_t race)
	{
		if (race >= 1 && race <= 12)
			return race - 1;
		return race == 128 ? 12 : race == 130 ? 13 : -1;
	}

	const char *LuclinModels::IniRaceName(uint16_t race)
	{
		static const char *const names[] = {"Human", "Barbarian", "Erudite", "WoodElf", "HighElf", "DarkElf", "HalfElf",
			"Dwarf", "Troll", "Ogre", "Halfling", "Gnome", "Iksar", "VahShir"};
		int index = Index(race);
		return index >= 0 ? names[index] : "";
	}

	bool LuclinModels::Wanted(uint16_t race, uint8_t gender) const
	{
		int index = Index(race);
		return index >= 0 && gender <= 1 && use[index][gender];
	}

	bool LuclinModels::ShouldLoad(uint16_t race, uint8_t gender) const
	{
		if (race == 130)
			return Wanted(130, gender);
		if (all_off)
			return false;
		if (Wanted(race, gender))
			return true;
		if (race == 1)
			return Wanted(3, gender);
		if (race == 4)
			return Wanted(6, gender) || Wanted(7, gender) || Wanted(5, gender);
		return false;
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
		if (name.find("_chr") != std::string::npos || name.find("equip") != std::string::npos)
			source->textures.SetMaskReference(0x10);
		source->name = name;
		to.push_back(std::move(source));
		return true;
	}

	bool CharacterModels::Skipped(const Source &source, const std::string &code) const
	{
		return m_old_skeletons && code == "SKE" && (source.name == "global6_chr" || source.name == "global6_chr2");
	}

	unsigned LuclinAttachmentOffset(uint16_t race, uint8_t gender)
	{
		return AttachmentOffset(race, gender);
	}

	bool CharacterModels::FindSkeleton(uint16_t race, uint8_t gender, std::string &code, WldSkeleton &skeleton)
	{
		for (const std::string &tag : {ActorTag(race, gender), ActorTag(75, gender)})
			for (auto &candidate : m_sources) {
				if (Skipped(*candidate, tag))
					continue;
				int actor = candidate->wld.Find(tag + "_ACTORDEF");
				if (actor >= 0 && candidate->wld.ActorSkeleton(actor) >= 0) {
					code = tag;
					return candidate->wld.ReadSkeleton(candidate->wld.ActorSkeleton(actor), skeleton);
				}
			}
		return false;
	}

	bool CharacterModels::HasPoint(uint16_t race, uint8_t gender, const std::string &point)
	{
		std::string code;
		WldSkeleton skeleton;
		if (!FindSkeleton(race, gender, code, skeleton))
			return false;
		for (const auto &bone : skeleton.bones)
			if (Upper(bone.name) == code + point)
				return true;
		return false;
	}

	bool CharacterModels::IsLuclin(uint16_t race, uint8_t gender)
	{
		// initializeHierarchicalSprite: a model with a <tag>TUNIC_POINT_DAG is a Luclin one.
		return HasPoint(race, gender, "TUNIC_POINT_DAG");
	}

	bool CharacterModels::HasOwnModel(uint16_t race, uint8_t gender) const
	{
		std::string tag = ActorTag(race, gender);
		for (const auto &candidate : m_sources) {
			if (Skipped(*candidate, tag))
				continue;
			int actor = candidate->wld.Find(tag + "_ACTORDEF");
			if (actor >= 0 && candidate->wld.ActorSkeleton(actor) >= 0)
				return true;
		}
		return false;
	}

	bool CharacterModels::HasItemModel(unsigned it) const
	{
		std::string tag = "IT" + std::to_string(it);
		for (const auto *list : {&m_items, &m_sources})
			for (const auto &from : *list) {
				int index = from->wld.Find(tag + "_HS_DEF");
				if (index >= 0 && from->wld.Fragments()[index].type == 0x10)
					return true;
				index = from->wld.Find(tag + "_DMSPRITEDEF");
				if (index >= 0 && from->wld.Fragments()[index].type == 0x36)
					return true;
			}
		return false;
	}

	bool CharacterModels::HasMaterial(const std::string &name) const
	{
		WldMaterial found;
		for (const auto *list : {&m_sources, &m_items})
			for (const auto &from : *list)
				if (from->wld.FindMaterial(name, found))
					return true;
		return false;
	}

	bool CharacterModels::Open(const std::string &eq_dir, std::string &error, bool creatures, bool velious_textures,
		const LuclinModels &luclin)
	{
		m_sources.clear();
		m_items.clear();
		m_armor.clear();
		m_animation_sets.clear();
		m_velious_textures = velious_textures;
		m_old_skeletons = luclin.old_skeletons;
		// CDisplay::InitWorld's order; the first model, track or material of a name loaded is the one
		// used. The weapons and shields first.
		for (const char *name : {"gequip", "gequip8", "gequip2", "gequip5", "gequip4", "gequip3"})
			AddSource(m_items, eq_dir, name);
		// The Luclin player models, each race and sex that ShouldLoad: global<tag>_chr2 (more of its
		// animations) and global<tag>_chr. VEquip goes with the Vah Shir's (their helms), the
		// LGEquip files (everyone else's hair, helms, robes and armor pieces) with the others'.
		m_luclin_pc_models = false;
		bool luclin_armor = false, vah_shir_items = false;
		for (uint16_t race : LuclinRaces) {
			for (uint8_t gender = 0; gender < 2; ++gender) {
				if (!luclin.ShouldLoad(race, gender))
					continue;
				std::string tag = ActorTag(race, gender);
				for (char &c : tag)
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				AddSource(m_sources, eq_dir, "global" + tag + "_chr2");
				if (!AddSource(m_sources, eq_dir, "global" + tag + "_chr"))
					continue;
				if (race == 130)
					vah_shir_items = vah_shir_items || gender == 0 || !luclin.Wanted(130, 0);
				else
					luclin_armor = true;
			}
		}
		if (vah_shir_items)
			AddSource(m_items, eq_dir, "vequip");
		// The Luclin elementals (races 209..212), with UseLuclinElementals.
		if (creatures && luclin.elementals) {
			AddSource(m_sources, eq_dir, "global5_chr2");
			AddSource(m_sources, eq_dir, "global5_chr");
		}
		m_luclin_pc_models = luclin_armor;
		if (luclin_armor)
			for (const char *name : {"lgequip_amr2", "lgequip_amr", "lgequip2", "lgequip"})
				AddSource(m_items, eq_dir, name);
		// The low-poly Vah Shir (and GEquip6, their helms) unless both of theirs are Luclin's.
		if (!luclin.Wanted(130, 0) || !luclin.Wanted(130, 1)) {
			AddSource(m_items, eq_dir, "gequip6");
			AddSource(m_sources, eq_dir, "global7_chr");
		}
		// Without the creatures, still the player races' own: global4_chr the Iksar, global_chr the
		// rest, global3_chr the old models' socials (S05..S28) and later idles.
		if (creatures) {
			AddSource(m_sources, eq_dir, "global6_chr2");
			AddSource(m_sources, eq_dir, "global6_chr");
		}
		AddSource(m_sources, eq_dir, "global4_chr");
		if (!AddSource(m_sources, eq_dir, "global_chr")) {
			error = "Couldn't read global_chr.s3d in " + eq_dir;
			return false;
		}
		if (creatures)
			AddSource(m_sources, eq_dir, "global2_chr");
		AddSource(m_sources, eq_dir, "global3_chr");
		// Global<n>_amr.s3d for each LoadArmor<n> that's on, only without Luclin models (bar the Vah
		// Shir) or with LoadVeliousArmorsWithLuclin. Without them the materials fall back (BodyMaterial).
		if (velious_textures && (!luclin_armor || luclin.velious_armor)) {
			for (int n = 17; n <= 23; ++n)
				if (!AddSource(m_armor, eq_dir, "global" + std::to_string(n) + "_amr"))
					m_velious_textures = false;
		}
		else
			m_velious_textures = false;
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
				if (Skipped(*candidate, tag))
					continue;
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
		auto bone_named = [&](const std::string &name) {
			for (size_t i = 0; i < skeleton.bones.size(); ++i)
				if (Upper(skeleton.bones[i].name) == name)
					return static_cast<int>(i);
			return -1;
		};
		// initializeHierarchicalSprite: a model with a <tag>TUNIC_POINT_DAG is a Luclin one.
		bool luclin = bone_named(code + "TUNIC_POINT_DAG") >= 0;
		// The Global<n>_amr Velious armor is cut for the old models: two chest pieces (a Luclin chest
		// has three) on the old UVs. eqgame swaps it in anyway with LoadVeliousArmorsWithLuclin, so
		// half the chest is the armor and the rest the bare skin's shirt. A Luclin model wears what
		// it stands in for (BodyMaterial without it) instead, as eqgame does with the key off.
		if (luclin && m_velious_textures && appearance.texture == 0xff)
			for (int slot = 1; slot <= 6; ++slot)
				appearance.equip[slot] = BodyMaterial(appearance.equip[slot], appearance.class_id, false);
		// CDisplay::CreatePlayerActor -> EQPlayer::SetDefaultFacialFeaturesByFace(-1, 0, 0) (@005204ee):
		// a Luclin model of a LegalPlayerRace (1..12, 128, 130) whose first eye color is unset (0xff, a
		// character made with the old models, most NPCs) gets the defaults. Its hair and beard are
		// none (a Vah Shir with face 7 hair style 0); then with HasAttachedHair 1 (not an Erudite,
		// Iksar, Vah Shir, or troll or ogre man) hair style 0 in GetAllowedHairColorIndexRange's
		// first color, and a dwarf man beard 0 in the hair's color.
		if (luclin && appearance.eye_color1 == 0xff &&
			(appearance.race <= 12 || appearance.race == 128 || appearance.race == 130)) {
			uint16_t race = appearance.race;
			appearance.hair_style = race == 130 && appearance.face % 10 == 7 ? 0 : 0xff;
			appearance.hair_color = appearance.beard = appearance.beard_color = 0xff;
			bool attached_hair = race != 3 && race != 128 && race != 130 &&
				!((race == 9 || race == 10) && appearance.gender == 0);
			if (attached_hair) {
				appearance.hair_style = 0;
				appearance.hair_color = race == 5 ? 4 : race == 6 ? 11 : 0;
			}
			if (race == 8 && appearance.gender == 0) {
				appearance.beard = 0;
				appearance.beard_color = appearance.hair_color;
			}
		}
		// Every animation the model has, each bone from its own tracks or its source model's,
		// shared by everyone with this model.
		// A Luclin model plays one variant of each animation, by what it holds (AnimVariation).
		int variation = luclin ? AnimVariation(appearance.equip[7], appearance.equip[8]) : 0;
		auto &animations = m_animation_sets[code + static_cast<char>('A' + variation)];
		if (!animations) {
			// A zone's model drawn over a global one of the same name (we search the zone's files first,
			// eqgame keeps the global's) takes no tracks from the global's file when the two are built on
			// other bones: global6's WOL (its root WOL_TRACK, L01WOL_TRACK...) would give misty_chr's old
			// WOL animations that move only the root, the rest of it stuck in its base pose.
			bool zone_model = source->name.rfind("global", 0) != 0;
			auto same_bones = [&](const Source &other) {
				if (!zone_model)
					return true;
				int other_actor = other.wld.Find(code + "_ACTORDEF");
				WldSkeleton other_skeleton;
				if (other_actor < 0 || other.wld.ActorSkeleton(other_actor) < 0 ||
					!other.wld.ReadSkeleton(other.wld.ActorSkeleton(other_actor), other_skeleton))
					return true;
				if (other_skeleton.bones.size() != skeleton.bones.size())
					return false;
				for (size_t i = 0; i < skeleton.bones.size(); ++i)
					if (Upper(other_skeleton.bones[i].track) != Upper(skeleton.bones[i].track))
						return false;
				return true;
			};
			std::vector<const WldFile *> wlds = {&source->wld};
			for (auto &other : m_sources)
				if (other.get() != source && !Skipped(*other, code) && same_bones(*other))
					wlds.push_back(&other->wld);
			auto set = std::make_shared<std::map<std::string, SkeletonAnimation>>();
			std::string from = AnimationSource(code, luclin);
			for (int id = 1; id < Anim::Count; ++id) {
				const char *name = AnimationCode(id);
				SkeletonAnimation animation;
				if (!luclin)
					animation = LoadAnimation(wlds, skeleton, name, code, from);
				else {
					// A Luclin model's tracks come in variants A..H ("C01A" + "HUMPEPELVIS_TRACK"). It plays
					// its variation's; without them toggleAnimationState's: B for A or C..H, A for B, and
					// then A; else a plain track (initializeHierarchicalSprite loads one as A).
					auto load = [&](int v) {
						return LoadAnimation(wlds, skeleton, std::string(name) + static_cast<char>('A' + v), code, from);
					};
					animation = load(variation);
					if (animation.length == 0) {
						int toggled = variation == 1 ? 0 : 1;
						animation = load(toggled);
						if (animation.length == 0 && variation != 0 && toggled != 0)
							animation = load(0);
					}
					if (animation.length == 0)
						animation = LoadAnimation(wlds, skeleton, name, code, from);
				}
				// PlayAnimationTrack2: a Luclin model plays S20 for S08 and S08 for S20 (50 and 62).
				const char *key = luclin && id == 50 ? AnimationCode(62) : luclin && id == 62 ? AnimationCode(50) : name;
				if (animation.length > 0)
					(*set)[key] = std::move(animation);
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
			// The head: <tag>HE_DAG, a Luclin model's <tag>HEHEAD_DAG (CDisplay_UpdateDAGPointers).
			if (name.size() > 6 && (name.compare(name.size() - 6, 6, "HE_DAG") == 0 || Upper(name) == code + "HEHEAD_DAG")) {
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
		// A Luclin model's head is SetDefaultITAttachments' (below).
		bool npc_texture = appearance.texture != 0xff;
		unsigned helm = appearance.equip[0];
		bool hair = true;
		unsigned helm_item = 0;
		unsigned face_base = appearance.face % 10, face_variant = appearance.face / 10; // EQPlayer::GetBaseFaceNbr
		if (luclin) {
			// SwapHead on a Luclin model only hangs the helm (SetDefaultITAttachments).
		}
		else if (appearance.race == 130) {
			// CDisplay_SwapFace: face 7 puts on the mane, helm 1, when there's no helm.
			unsigned m = helm == 240 ? 3 : helm;
			if (m == 0 && face_base == 7)
				m = 1;
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
		if (appearance.race != 130 && !luclin) {
			if (npc_texture && appearance.helm < 0xf0)
				head_mesh = head_named(appearance.helm);
			if (head_mesh < 0)
				head_mesh = head_named(HeadForMaterial(helm, appearance.race));
		}

		// A robe (chest 10 to 16) as CDisplay_SwapMaterial puts it on: the robed body
		// (<tag>01_DMSPRITEDEF, CDisplay_SwapBody) with the bare chest (material 00) under it, and its
		// cloth (CLK04nn..CLK10nn, ReplaceCloakMaterials: the robe - 6) in the chest's tint. Not on a Vah Shir.
		unsigned chest = appearance.equip[1];
		bool robe = !npc_texture && !luclin && appearance.race != 130 && chest >= 10 && chest <= 16;
		// CDisplay::HandleMaterialEx: on a Luclin model a robe is its own chest material (and a tunic,
		// below), and the arms and wrists wear it too, in its color.
		bool luclin_robe = !npc_texture && luclin && chest >= 10 && chest <= 16;
		if (luclin_robe)
			for (int slot : {2, 3}) {
				appearance.equip[slot] = chest;
				appearance.tint[slot] = appearance.tint[1];
			}
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
		// CDisplay_SwapFace: a Luclin model's eyes are both the first color (1 for none), but the faces
		// with an eye patch or scar (a human man's 3, a barbarian man's 6, 0xD1; a dwarf man's 5, a
		// barbarian man's 4, 0xCF) on the right.
		unsigned eye_left = appearance.eye_color1 == 0 || appearance.eye_color1 == 0xff ? 1u : appearance.eye_color1;
		unsigned eye_right = eye_left;
		if (appearance.gender == 0) {
			if ((appearance.race == 1 && face_base == 3) || (appearance.race == 2 && face_base == 6))
				eye_right = 0xd1;
			else if ((appearance.race == 8 && face_base == 5) || (appearance.race == 2 && face_base == 4))
				eye_right = 0xcf;
		}
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
			auto find_any = [&](const std::string &name, WldMaterial &found) {
				if (source->wld.FindMaterial(name, found))
					return true;
				for (auto *list : {&m_sources, &m_items})
					for (auto &other : *list)
						if (other->wld.FindMaterial(name, found)) {
							textures_from = other.get();
							return true;
						}
				return false;
			};
			// CDisplay_SetEyeMaterial: a Luclin model's eyes (<tag>L_EYE_MDF, <tag>R_EYE_MDF; a
			// halfling man's R_01) become CHR_EYE<n>_MDF, the color plus the race's first.
			if (luclin && upper.size() > 8 && upper.compare(0, 3, code) == 0 &&
				(upper.compare(3, 5, "L_EYE") == 0 || upper.compare(3, 5, "R_EYE") == 0 ||
					(appearance.race == 11 && appearance.gender == 0 && upper.compare(3, 4, "R_01") == 0))) {
				unsigned eye = upper[3] == 'L' ? eye_left : eye_right;
				if (appearance.race == 6)
					eye += 20;
				else if (appearance.race == 9)
					eye += 100;
				else if (appearance.race == 10)
					eye += 80;
				else if (appearance.race == 128)
					eye += 40;
				else if (appearance.race == 130)
					eye += 60;
				char name[32];
				snprintf(name, sizeof(name), "CHR_EYE%03u_MDF", eye);
				WldMaterial found;
				return find_any(name, found) ? found : material;
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
				std::string part = upper.substr(3, 2);
				slot = SlotForPart(part);
				// A Vah Shir's tail goes with the legs (CDisplay_SwapMaterial).
				if (appearance.race == 130 && part == "TA")
					slot = 5;
				if (slot < 0)
					return material;
				char name[16];
				// The face (CDisplay_SwapFace): <tag>HE<variant><face><piece>. A Vah Shir's face is its
				// fur too, on every part: <tag><part><material><face><piece>.
				std::string tail = upper.substr(7, 2);
				if (appearance.race == 130)
					tail[0] = static_cast<char>('0' + face_base);
				if (slot == 0)
					snprintf(name, sizeof(name), "%sHE%02u%u%c_MDF", code.c_str(), face_variant, face_base, upper[8]);
				else
					snprintf(name, sizeof(name), "%s%s%02u%s_MDF", code.c_str(), part.c_str(),
						slot == 1 && robe ? 0u : appearance.equip[slot] <= 99 ? appearance.equip[slot] : 0u, tail.c_str());
				swapped = name;
				WldMaterial found;
				// What's missing falls back: a face's variant to 0, a Vah Shir's part to its bare one.
				if (!source->wld.FindMaterial(swapped, found)) {
					if (slot == 0 && face_variant != 0)
						swapped.replace(5, 2, "00");
					else if (slot > 0 && appearance.race == 130)
						swapped.replace(5, 2, "00");
				}
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
						batch.material.texture = textures_from->textures.GetLayered(material.textures[0], material.layer, info.masked);
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
		// when it's a shield (AppendItem). Models without the point hold nothing; race 14 never does.
		auto append_item = [&](unsigned it, int point) {
			AppendItem(*model, "IT" + std::to_string(it), it, static_cast<uint16_t>(point));
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
		// CDisplay_SetDefaultITAttachments: a Luclin model's helm, tunic or robe, shoulders, gauntlets,
		// belt and boots are item models (IT<n> + the race's AttachmentOffset) on its point bones, in
		// the slot's color; under a robe only the robe and the helm. Then SetFHEB: the hair (gone under
		// a helm) and beard in their colors.
		if (luclin && !npc_texture) {
			uint16_t race = appearance.race;
			unsigned offset = AttachmentOffset(race, appearance.gender);
			auto slot_tint = [&](int slot) {
				uint32_t tint = appearance.tint[slot] & 0xffffff;
				return tint != 0 ? tint : 0xffffffu;
			};
			auto attach = [&](unsigned it, const char *point, uint32_t tint) {
				int bone = bone_named(code + point);
				return it != 0 && bone >= 0 && AppendAttachment(*model, skeleton, code, "IT" + std::to_string(it + offset), bone, tint);
			};
			bool iksar_or_vah_shir = race == 128 || race == 130;
			unsigned head = appearance.equip[0];
			unsigned helm_it = 0;
			if (race == 3 && luclin_robe)
				helm_it = 0; // an Erudite's robe has its hood
			else if (race == 2)
				helm_it = head == 241 || head > 2 ? 5029 : 5000 + head;
			else if (head == 240 || head == 3)
				helm_it = iksar_or_vah_shir ? 5003 : 5028;
			else if (head == 241)
				helm_it = 5029;
			else
				helm_it = 5000 + head;
			bool helmed = attach(helm_it, "HEAD_POINT_DAG", slot_tint(0));
			unsigned type = appearance.equip[1];
			if (type <= 3) {
				attach(type == 3 ? 6000 : 0, "CHEST_POINT_DAG", slot_tint(1));
				if (!(race == 12 && appearance.gender == 1 && type == 1))
					attach(4010 + type, "TUNIC_POINT_DAG", slot_tint(1));
			}
			else if (luclin_robe)
				attach(3990 + type, "TUNIC_POINT_DAG", slot_tint(1));
			if (!luclin_robe) {
				// Arms, wrists and feet in pairs (right, left), the legs a belt.
				const struct
				{
					int slot;
					unsigned base;
					const char *first, *second;
				} pairs[] = {{2, 7000, "SHOULDR_POINT_DAG", "SHOULDL_POINT_DAG"}, {3, 8000, "GAUNTR_POINT_DAG", "GAUNTL_POINT_DAG"},
					{6, 9000, "LEGL_POINT_DAG", "LEGR_POINT_DAG"}};
				for (const auto &pair : pairs) {
					unsigned t = appearance.equip[pair.slot];
					if (t > 3)
						continue;
					attach(pair.base + t * 2, pair.first, slot_tint(pair.slot));
					attach(pair.base + t * 2 + 1, pair.second, slot_tint(pair.slot));
				}
				attach(4020 + appearance.equip[5], "PELVIS_POINT_DAG", slot_tint(5));
			}
			// SetHairOrBeard: hair IT1000 + style, beard IT2000 + style. A Vah Shir has them only with
			// face 7 (its mane, under a helm too); anyone else's hair is gone under a helm.
			auto hair_tint = [&](uint8_t color) {
				uint32_t tint = HairTint(race, color);
				return tint != 0 ? tint : 0xffffffu;
			};
			if (race == 130) {
				if (face_base == 7) {
					attach(1000 + (appearance.hair_style == 0xff ? 0u : appearance.hair_style), "HAIR_POINT_DAG",
						hair_tint(appearance.hair_color));
					attach(2000u + appearance.beard, "BEARD_POINT_DAG", hair_tint(appearance.beard_color));
				}
			}
			else {
				if (!helmed)
					attach(1000u + appearance.hair_style, "HAIR_POINT_DAG", hair_tint(appearance.hair_color));
				attach(2000u + appearance.beard, "BEARD_POINT_DAG", hair_tint(appearance.beard_color));
			}
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
		// The neuter wolves' shrink is for global6's larger WOL and WOE, the ones eqgame draws; a zone's
		// old WOL (misty_chr's), which we draw over global6's, is built to the usual size.
		float height = appearance.height > 0 ? appearance.height : player_race ? RaceHeight(appearance.race) : 6;
		bool global6 = source->name.rfind("global6_chr", 0) == 0;
		if (appearance.race == 63 ||
			((appearance.race == 42 || appearance.race == 120) && appearance.gender == 2 && global6))
			height *= 0.45f;
		model->m_scale = height * 0.2f;
		model->m_min_z *= model->m_scale;
		model->m_max_z *= model->m_scale;
		model->m_head_z *= model->m_scale;
		model->m_origin_over_feet = 3.125f * model->m_scale;
		return model;
	}

	void CharacterModels::AppendItemMesh(CharacterModel &model, const WldMesh &mesh, Source &from, uint16_t point,
		uint32_t tint)
	{
		// All its vertices on the one bone it hangs from, its own materials untinted.
		uint32_t base = static_cast<uint32_t>(model.m_positions.size());
		model.m_bones.resize(base + mesh.positions.size(), point);
		for (size_t i = 0; i < mesh.positions.size(); ++i) {
			model.m_positions.push_back({mesh.positions[i].x, mesh.positions[i].y, mesh.positions[i].z});
			model.m_normals.push_back(Normalize({mesh.normals[i].x, mesh.normals[i].y, mesh.normals[i].z}));
			model.m_uvs.push_back(mesh.uvs[i * 2]);
			model.m_uvs.push_back(mesh.uvs[i * 2 + 1]);
		}
		std::map<uint16_t, size_t> batch_of; // SIZE_MAX: not drawn
		for (const auto &t : mesh.triangles) {
			if (t.material >= mesh.materials.size())
				continue;
			auto found = batch_of.find(t.material);
			if (found == batch_of.end()) {
				const WldMaterial &material = mesh.materials[t.material];
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
					batch.material.texture = from.textures.GetLayered(material.textures[0], material.layer, info.masked);
				batch.tint = tint;
				found = batch_of.emplace(t.material, model.m_batches.size()).first;
				model.m_batches.push_back(std::move(batch));
			}
			if (found->second == SIZE_MAX)
				continue;
			auto &indices = model.m_batches[found->second].indices;
			indices.insert(indices.end(), {base + t.v[0], base + t.v[1], base + t.v[2]});
		}
	}

	bool CharacterModels::AppendItem(CharacterModel &model, const std::string &tag, unsigned it, uint16_t point)
	{
		// CDisplay::ChangeDag hangs an item's skeleton (<tag>_HS_DEF, here in its base pose) on the
		// point, else its mesh (<tag>_DMSPRITEDEF), from gequip*.s3d and then the models' files.
		std::vector<Source *> order;
		for (auto &from : m_items)
			order.push_back(from.get());
		for (auto &from : m_sources)
			order.push_back(from.get());
		std::string name = tag + "_HS_DEF";
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
					AppendItemMesh(model, piece, *from, point);
				}
			}
			// Meshes a bone carries itself (a torch's IT36_O_DAG), all on that bone, and its
			// particle clouds (the flame and smoke on IT36_P1_DAG and IT36_P2_DAG).
			for (size_t b = 0; b < item.bones.size(); ++b) {
				WldMesh piece;
				if (item.bones[b].mesh >= 0 && from->wld.ReadMesh(item.bones[b].mesh, piece)) {
					piece.pieces = {{static_cast<uint16_t>(piece.positions.size()), static_cast<uint16_t>(b)}};
					SkinMesh(piece, pose);
					AppendItemMesh(model, piece, *from, point);
				}
				CharacterModel::Emitter emitter;
				if (item.bones[b].particle_cloud >= 0 && CloudDef(from->wld, from->textures, item.bones[b].particle_cloud, emitter.def)) {
					emitter.bone = point;
					emitter.item = it;
					emitter.item_bone = static_cast<uint16_t>(b);
					emitter.local = pose[b];
					model.m_emitters.push_back(std::move(emitter));
				}
			}
			return true;
		}
		name = tag + "_DMSPRITEDEF";
		for (Source *from : order) {
			int index = from->wld.Find(name);
			WldMesh item;
			if (index >= 0 && from->wld.Fragments()[index].type == 0x36 && from->wld.ReadMesh(index, item)) {
				AppendItemMesh(model, item, *from, point);
				return true;
			}
		}
		return false;
	}

	bool CharacterModels::AppendAttachment(CharacterModel &model, WldSkeleton &skeleton, const std::string &code,
		const std::string &tag, int point, uint32_t tint)
	{
		std::vector<Source *> order;
		for (auto &from : m_items)
			order.push_back(from.get());
		for (auto &from : m_sources)
			order.push_back(from.get());
		size_t body_bones = skeleton.bones.size();
		auto body_bone = [&](const std::string &name) {
			for (size_t i = 0; i < body_bones; ++i)
				if (Upper(skeleton.bones[i].name) == name)
					return static_cast<int>(i);
			return -1;
		};
		std::string name = tag + "_HS_DEF";
		for (Source *from : order) {
			int index = from->wld.Find(name);
			WldSkeleton item;
			if (index < 0 || from->wld.Fragments()[index].type != 0x10 || !from->wld.ReadSkeleton(index, item))
				continue;
			// Its bones under the point, in their own pose; each but its root plays the body's track of
			// the same bone (IT4370PEPELVIS_DAG: HUMPEPELVIS_DAG's), as UpdateAttachmentAnimations names
			// them from the body's animation.
			uint16_t base = static_cast<uint16_t>(skeleton.bones.size());
			for (size_t b = 0; b < item.bones.size(); ++b) {
				WldSkeleton::Bone bone = item.bones[b];
				bone.parent = bone.parent < 0 ? point : base + bone.parent;
				bone.children.clear();
				bone.mesh = -1;
				bone.particle_cloud = -1;
				std::string upper = Upper(bone.name);
				if (b > 0 && upper.size() > tag.size() && upper.compare(0, tag.size(), tag) == 0)
					bone.animation_bone = body_bone(code + upper.substr(tag.size()));
				skeleton.bones.push_back(std::move(bone));
			}
			auto append = [&](WldMesh &mesh) {
				size_t v = 0;
				std::vector<uint16_t> bones;
				for (const auto &piece : mesh.pieces)
					for (uint16_t k = 0; k < piece.count && v < mesh.positions.size(); ++k, ++v)
						bones.push_back(static_cast<uint16_t>(base + (piece.bone < item.bones.size() ? piece.bone : 0)));
				bones.resize(mesh.positions.size(), base);
				size_t first = model.m_bones.size();
				AppendItemMesh(model, mesh, *from, base, tint);
				std::copy(bones.begin(), bones.end(), model.m_bones.begin() + static_cast<long>(first));
			};
			for (int part : item.meshes) {
				WldMesh mesh;
				if (from->wld.ReadMesh(part, mesh))
					append(mesh);
			}
			for (size_t b = 0; b < item.bones.size(); ++b) {
				WldMesh mesh;
				if (item.bones[b].mesh >= 0 && from->wld.ReadMesh(item.bones[b].mesh, mesh)) {
					mesh.pieces = {{static_cast<uint16_t>(mesh.positions.size()), static_cast<uint16_t>(b)}};
					append(mesh);
				}
			}
			return true;
		}
		name = tag + "_DMSPRITEDEF";
		for (Source *from : order) {
			int index = from->wld.Find(name);
			WldMesh mesh;
			if (index >= 0 && from->wld.Fragments()[index].type == 0x36 && from->wld.ReadMesh(index, mesh)) {
				AppendItemMesh(model, mesh, *from, static_cast<uint16_t>(point), tint);
				return true;
			}
		}
		return false;
	}

	std::unique_ptr<CharacterModel> CharacterModels::BuildItem(const std::string &actor, std::string &error)
	{
		// An item on the ground (CDisplay::CreateActor's "<tag>_ACTORDEF"): the held item's model on a
		// skeleton of one bone, at scale 1, with no animations.
		std::string tag = Upper(actor);
		if (tag.size() > 9 && tag.compare(tag.size() - 9, 9, "_ACTORDEF") == 0)
			tag.resize(tag.size() - 9);
		auto model = std::make_unique<CharacterModel>();
		model->m_code = tag;
		model->m_skeleton.bones.resize(1);
		model->m_skeleton.bones[0].name = tag + "_DAG";
		static const auto no_animations = std::make_shared<const std::map<std::string, SkeletonAnimation>>();
		model->m_animations = no_animations;
		unsigned it = tag.size() > 2 && tag.compare(0, 2, "IT") == 0 ? static_cast<unsigned>(std::strtoul(tag.c_str() + 2, nullptr, 10)) : 0;
		if (!AppendItem(*model, tag, it, 0) || model->m_positions.empty()) {
			error = "no model " + tag;
			return nullptr;
		}
		model->m_min_z = 1e30f;
		model->m_max_z = -1e30f;
		for (const Vec3 &p : model->m_positions) {
			model->m_min_z = std::min(model->m_min_z, p.z);
			model->m_max_z = std::max(model->m_max_z, p.z);
		}
		model->m_scale = 1;
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
				else // at the options' density (t3dCreateParticleCloud)
					clouds.push_back(std::make_unique<ParticleCloud>(emitter.def, PCloudDensity(), nullptr, nullptr));
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

	bool CharacterModel::RayHit(const Transform &placement, uint64_t time_ms, const std::string &animation, bool loop,
		const Vec3 &from, const Vec3 &direction, float range, float &distance) const
	{
		auto it = m_animations->find(animation);
		if (it == m_animations->end())
			it = m_animations->find("P01");
		std::vector<Transform> bones = PoseSkeleton(m_skeleton, it == m_animations->end() ? nullptr : &it->second, time_ms, loop);
		Transform scaled = placement;
		scaled.scale *= m_scale;
		for (auto &bone : bones)
			bone = scaled * bone;
		std::vector<Vec3> posed(m_positions.size());
		for (size_t i = 0; i < m_positions.size(); ++i)
			posed[i] = bones[m_bones[i]].Apply(m_positions[i]);
		// Möller-Trumbore on each triangle.
		bool hit = false;
		float best = range;
		for (const auto &batch : m_batches) {
			for (size_t t = 0; t + 2 < batch.indices.size(); t += 3) {
				const Vec3 &a = posed[batch.indices[t]];
				Vec3 e1 = posed[batch.indices[t + 1]] - a, e2 = posed[batch.indices[t + 2]] - a;
				Vec3 p = Cross(direction, e2);
				float det = Dot(e1, p);
				if (std::fabs(det) < 1e-8f)
					continue;
				float inverse = 1 / det;
				Vec3 s = from - a;
				float u = Dot(s, p) * inverse;
				if (u < 0 || u > 1)
					continue;
				Vec3 q = Cross(s, e1);
				float v = Dot(direction, q) * inverse;
				if (v < 0 || u + v > 1)
					continue;
				float d = Dot(e2, q) * inverse;
				if (d >= 0 && d < best) {
					best = d;
					hit = true;
				}
			}
		}
		if (hit)
			distance = best;
		return hit;
	}

	size_t CharacterModel::TriangleCount() const
	{
		size_t count = 0;
		for (const auto &batch : m_batches)
			count += batch.indices.size() / 3;
		return count;
	}
}
