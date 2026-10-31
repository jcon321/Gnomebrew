#pragma once

// Player race models and the creatures, dressed from the equipment list (or an NPC's body
// texture and helm), holding their weapons and shield (the IT models in gequip*.s3d) and playing
// any of their animations by track code (see animations.h). The player races are the classic
// models (global_chr.s3d, global4_chr.s3d for Iksar, global7_chr.s3d for Vah Shir) or, as
// LuclinModels picks them, the Luclin ones (global<tag>_chr.s3d), which wear their hair, beard,
// helm, robe and armor pieces as item models hung on their bones (lgequip*.s3d, vequip.s3d).

#include "particles.h"
#include "point_lights.h"
#include "renderer.h"
#include "skeleton.h"
#include "textures.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace render
{
	// Equipment slots as the server sends them: head, chest, arms, wrists, hands, legs, feet, primary, secondary.
	constexpr int EquipSlots = 9;

	struct CharacterAppearance
	{
		uint16_t race = 1;
		uint8_t gender = 0;
		uint8_t face = 0;
		uint32_t equip[EquipSlots] = {}; // material per armor slot (0 none, 1 leather, 2 chain, 3 plate, 10-16 robes);
		                                 // the IT model number for primary and secondary (0 empty)
		uint32_t tint[EquipSlots] = {};  // 0xFFRRGGBB when tinted
		float height = 0;                // the spawn's size; 0 = the race's default
		uint8_t texture = 0xff;          // 0xFF: dressed per slot; else an NPC's body texture (Spawn_Struct bodytexture)
		uint8_t helm = 0;                // an NPC's head (Spawn_Struct helm)
		uint8_t class_id = 0;            // a monk wears materials 4 and 23 as they are; anyone else as leather
		// The Luclin models' (Spawn_Struct, CharacterSelect_Struct): hair and beard style (IT1000+ and
		// IT2000+ models) and color (an index into the hair colors), and the eye colors. 0xFF: none.
		uint8_t hair_style = 0xff, hair_color = 0xff;
		uint8_t beard = 0xff, beard_color = 0xff;
		uint8_t eye_color1 = 0xff, eye_color2 = 0xff;
	};

	// Which player races use their Luclin model, as eqclient.ini's [Defaults] UseLuclin<Race>Male /
	// Female and friends say (my-client.ini keeps the same names). CDisplay::InitWorld loads
	// global<tag>_chr2.s3d and global<tag>_chr.s3d for each that ShouldLoad, before global_chr.s3d,
	// and the first model of a name loaded is the one used.
	struct LuclinModels
	{
		bool use[14][2] = {};       // [Human..Gnome (races 1..12), Iksar, Vah Shir][gender]
		bool elementals = false;    // UseLuclinElementals: global5_chr.s3d (races 209..212)
		bool all_off = false;       // AllLuclinPcModelsOff: none but the Vah Shir's
		bool velious_armor = true;  // LoadVeliousArmorsWithLuclin: the Velious armor files with them too
		// LoadOldSkeletons (ours, not eqgame's): the skeleton (SKE) from global_chr.s3d, not the later
		// one global6_chr.s3d defines first.
		bool old_skeletons = true;

		static int Index(uint16_t race);               // into use; -1 for a race without one
		static const char *IniRaceName(uint16_t race); // CDisplay_GetIniRaceName: "WoodElf", "VahShir"...
		bool Wanted(uint16_t race, uint8_t gender) const; // CDisplay::GetNewPCIniFlag
		// CDisplay::ShouldLoadNewPcModel: wanted, or another needs its animations (the Erudites play
		// the Human's, the High, Dark and Half Elves the Wood Elf's); AllLuclinPcModelsOff leaves the
		// Vah Shir only.
		bool ShouldLoad(uint16_t race, uint8_t gender) const;
	};
	// The races InitWorld goes through: 1..12, Iksar and Vah Shir.
	constexpr uint16_t LuclinRaces[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 128, 130};

	// Three-letter model code for a playable race ("HUM", "IKF", ...), or empty.
	std::string RaceModelCode(uint16_t race, uint8_t gender);
	float RaceHeight(uint16_t race); // a player race's default height

	struct ModelLighting
	{
		Vec3 ambient{0.5f, 0.5f, 0.5f};
		Vec3 sun_direction{0, 0, -1}; // the way it shines
		Vec3 sun_color;
		const std::vector<PointLight> *lights = nullptr;
	};

	class CharacterModel
	{
	public:
		// placement: model space -> world. Lit as calculateMeshLighting (@1004da40) lights an actor:
		// the ambient, the sun (color x the cosine) and `lights` (color x 0.05 radius^2 x cos /
		// distance^2 each), up to full. `time_ms` runs the animation (looping, or stopping on its
		// last frame); a missing one plays the idle.
		void Draw(Renderer &renderer, const Transform &placement, const ModelLighting &lighting, uint64_t time_ms,
			const std::string &animation = "P01", bool loop = true) const;
		bool HasAnimation(const std::string &animation) const { return m_animations->count(animation) != 0; }
		uint32_t AnimationMs(const std::string &animation) const; // one loop

		// Extent of the idle's first frame, scaled (model space).
		float MinZ() const { return m_min_z; }
		float MaxZ() const { return m_max_z; }
		float HeadZ() const { return m_head_z; } // the head bone
		// The head bone's height in an animation at time_ms, scaled like HeadZ (sitting down lowers it).
		float HeadZAt(uint64_t time_ms, const std::string &animation, bool loop) const;
		// How far the model's origin sits over its feet: placement = feet + (0, 0, OriginOverFeet()).
		float OriginOverFeet() const { return m_origin_over_feet; }
		const std::string &Code() const { return m_code; }
		float Scale() const { return m_scale; } // model units -> world
		size_t TriangleCount() const;

		// A bone by its name ("HUMCH_DAG", any case); -1 when the model has none.
		int FindBone(const std::string &name) const;
		// Where bones are (world space, turned the way they point) at time_ms of an animation, as
		// PlaceEmitters places its emitters; -1 is the model's root.
		void PlaceBones(const Transform &placement, uint64_t time_ms, const std::string &animation, bool loop,
			const std::vector<int> &bones, std::vector<Transform> &out) const;

		// A held item's particle cloud (a torch's flame and smoke): on the model's bone the item
		// hangs from, `local` its own bone's place there (IT36_P1_DAG in the item's base pose).
		struct Emitter
		{
			uint16_t bone = 0;
			unsigned item = 0;          // the IT number it came with
			uint16_t item_bone = 0;     // and that item's bone it's on
			Transform local;
			ParticleCloudDef def;
		};
		const std::vector<Emitter> &Emitters() const { return m_emitters; }
		// Where each emitter is (world space) for Draw's arguments.
		void PlaceEmitters(const Transform &placement, uint64_t time_ms, const std::string &animation, bool loop,
			std::vector<Transform> &out) const;
		// collideRayWithEntity (@10005a40): whether the ray from `from` along the unit `direction`
		// hits the model, posed as Draw poses it, within `range`, and how far along (its nearest
		// triangle, either side).
		bool RayHit(const Transform &placement, uint64_t time_ms, const std::string &animation, bool loop,
			const Vec3 &from, const Vec3 &direction, float range, float &distance) const;

	private:
		friend class CharacterModels;
		struct Batch
		{
			Material material;
			uint32_t tint = 0xffffff;
			std::vector<uint32_t> indices;
		};

		std::string m_code;
		WldSkeleton m_skeleton;
		std::shared_ptr<const std::map<std::string, SkeletonAnimation>> m_animations; // by code ("L01"), shared
		std::vector<Vec3> m_positions, m_normals; // bone space
		std::vector<uint16_t> m_bones;           // per vertex
		std::vector<float> m_uvs;
		std::vector<Batch> m_batches;
		std::vector<Emitter> m_emitters;
		float m_min_z = 0, m_max_z = 0, m_head_z = 0;
		int m_head_bone = -1;
		float m_scale = 1;
		float m_origin_over_feet = 0;
		// Draw's scratch: the posed bones as matrices, and the vertices' light.
		struct BoneMatrix
		{
			float m[3][3]; // rotation times scale
			float inverse_scale;
			Vec3 translation;
			Vec3 light; // toward the sun, in the bone's space
		};
		mutable std::vector<BoneMatrix> m_bone_matrices;
		mutable std::vector<Vec3> m_intensity;
		mutable std::vector<Vertex> m_vertices;
	};

	// The particle clouds of one actor's held items, made for the model it's drawn with (t3dCreateActorEx
	// -> t3dCreateParticleCloud, one set per actor), following their bones.
	class HeldParticles
	{
	public:
		// After the model's Draw, with its arguments: the clouds move on and are drawn. A model
		// rebuilt (armor put on) keeps the clouds of the items it still holds where they are; only a
		// new item's start over, as CDisplay::ChangeDag makes them for that item alone.
		void Draw(Renderer &renderer, const CharacterModel &model, const Transform &placement, uint64_t time_ms,
			const std::string &animation, bool loop, uint64_t now_ms, const Vec3 &ambient);
		void Clear(); // another character's: everything starts over

	private:
		struct Key
		{
			uint16_t bone;
			unsigned item;
			uint16_t item_bone;
			bool operator==(const Key &o) const { return bone == o.bone && item == o.item && item_bone == o.item_bone; }
		};
		std::vector<Key> m_keys; // each cloud's emitter
		std::vector<std::unique_ptr<ParticleCloud>> m_clouds;
		std::vector<Transform> m_emitters;
	};

	class CharacterModels
	{
	public:
		// global_chr.s3d (required), global4_chr.s3d (Iksar) and global7_chr.s3d (Vah Shir); with
		// `creatures` all of global2..7_chr.s3d. And the weapons and shields, gequip*.s3d. With
		// `velious_textures`, the Velious armor, materials 17..23 (global17..23_amr.s3d, as eqgame's
		// LoadArmor17..23 load them); without, they're the leather, chain and plate they stand in for.
		// `luclin`: the Luclin player models to load (InitWorld's order and rules); the Velious armor
		// isn't loaded with any but the Vah Shir's unless it says so.
		bool Open(const std::string &eq_dir, std::string &error, bool creatures = false, bool velious_textures = true,
			const LuclinModels &luclin = {});
		// A zone's own models, <zone>_chr.s3d and friends, searched first.
		bool AddZone(const std::string &eq_dir, const std::string &zone);
		std::unique_ptr<CharacterModel> Build(const CharacterAppearance &appearance, std::string &error);
		// An item's model on its own, from its actor ("IT63_ACTORDEF" or "IT63"): one bone, scale 1,
		// no animations (something on the ground). Null when there's no such item model.
		std::unique_ptr<CharacterModel> BuildItem(const std::string &actor, std::string &error);

	private:
		struct Source
		{
			std::string name; // "global6_chr"
			PfsArchive archive;
			WldFile wld;
			TextureCache textures;
		};
		bool AddSource(std::vector<std::unique_ptr<Source>> &to, const std::string &eq_dir, const std::string &name);
		// A held item's model (<tag>_HS_DEF or <tag>_DMSPRITEDEF) onto the model, on bone `point`;
		// `it` its number for its particle clouds. False when there's none.
		bool AppendItem(CharacterModel &model, const std::string &tag, unsigned it, uint16_t point);
		static void AppendItemMesh(CharacterModel &model, const WldMesh &mesh, Source &from, uint16_t point,
			uint32_t tint = 0xffffff);
		// A Luclin model's item (<tag>_HS_DEF, "IT4370") hung on bone `point` of `skeleton` as
		// CDisplay::ChangeDag hangs it: its bones join the skeleton, each playing the body bone of
		// the same name's animation, and its meshes go on them in `tint`. Else its mesh
		// (<tag>_DMSPRITEDEF) on the point. False when there's no such item.
		bool AppendAttachment(CharacterModel &model, WldSkeleton &skeleton, const std::string &code, const std::string &tag,
			int point, uint32_t tint);
		std::vector<std::unique_ptr<Source>> m_sources;
		std::vector<std::unique_ptr<Source>> m_items; // gequip*.s3d (lgequip*, vequip with Luclin models), searched after the models
		std::vector<std::unique_ptr<Source>> m_armor; // global17..23_amr.s3d: materials, searched after the model's own
		bool m_velious_textures = false;
		bool m_old_skeletons = true;
		bool Skipped(const Source &source, const std::string &code) const; // LoadOldSkeletons
		std::map<std::string, std::shared_ptr<const std::map<std::string, SkeletonAnimation>>> m_animation_sets; // by model code
	};
}
