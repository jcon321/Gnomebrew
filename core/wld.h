#pragma once

// WLD world files (inside .s3d archives): the zone geometry, placed objects,
// lights, and the character models with their skeletons. Only the old (0x15500)
// format the TAKP client ships is handled.
//
// Layout: 28-byte header {magic 0x54503d02, version, fragment count, region
// count, max object bytes, string hash size, string count}, the string hash
// (XOR-encoded), then the fragments: {u32 size, u32 type, i32 name ref, data}.
// A fragment reference is 1-based; a negative reference is a string hash offset.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct WldVec3
{
	float x = 0, y = 0, z = 0;
};

// One texture slot of a mesh (fragment 0x30 -> 0x05 -> 0x04 -> 0x03).
struct WldMaterial
{
	std::string name;           // e.g. "BRICKFLO01_MDF"
	uint32_t render_method = 0; // 0x80000000 = user-defined method in the low bits
	std::vector<std::string> textures; // file names; more than one is an animation
	uint32_t frame_ms = 0;
};

// A DmSpriteDef2 (0x36): the zone's region meshes, objects and body parts.
struct WldMesh
{
	struct Triangle
	{
		uint16_t v[3];
		uint16_t flags; // 0x10 = passable (no collision)
		uint16_t material;
	};
	struct Piece
	{
		uint16_t count; // consecutive vertices ...
		uint16_t bone;  // ... attached to this skeleton bone
	};

	std::string name;
	WldVec3 center;
	float radius = 0;
	std::vector<WldVec3> positions; // center already added
	std::vector<float> uvs;         // 2 per vertex
	std::vector<WldVec3> normals;
	std::vector<uint32_t> colors;   // 0xAARRGGBB, one per vertex or empty
	std::vector<Triangle> triangles;
	std::vector<Piece> pieces;
	std::vector<WldMaterial> materials; // the mesh's palette (0x31)
};

// An object placement (0x15) from objects.wld.
struct WldPlacement
{
	std::string actor; // actor definition name in the zone's _obj.s3d
	WldVec3 position;
	WldVec3 rotation; // 0..512 units: [0] about z (heading), [1] about y, [2] about x
	float scale = 1;
};

// A point light (0x28 -> 0x1c -> 0x1b) from lights.wld.
struct WldLight
{
	WldVec3 position;
	float radius = 0;
	float r = 1, g = 1, b = 1;
};

// One keyframe of a bone track (0x12): rotation quaternion and translation,
// relative to the parent bone.
struct WldBoneTransform
{
	float w = 1, x = 0, y = 0, z = 0;
	WldVec3 shift;
};

// A skeleton (0x10). Bones are in parent-before-child order; the meshes are the
// skinned body parts.
struct WldSkeleton
{
	struct Bone
	{
		std::string name; // e.g. "HUMPE_DAG"
		std::string track; // base pose track name, e.g. "HUMPE_TRACK"
		WldBoneTransform pose;
		int parent = -1;
		std::vector<int> children;
	};
	std::string name;
	std::vector<Bone> bones;
	std::vector<int> meshes; // fragment indices of the 0x36 meshes
};

// The zone's BSP tree (0x21), which finds the region (0x22) a point is in, and the
// region groups (0x29) that say what some regions are: water, lava, zone lines...
struct WldRegionMap
{
	// A split plane: points with Dot(normal, p) < -distance go back, the rest front, until a
	// node with a region. Regions and nodes are 1-based; 0 is none.
	struct Node
	{
		WldVec3 normal;
		float distance = 0;
		int32_t region = 0;
		int32_t front = 0, back = 0;
	};
	struct Group
	{
		std::string name;      // e.g. "Z0002_ZONE"
		std::string user_data; // e.g. "WTN__01258000000000000000000000___000000000000"
		std::vector<uint32_t> regions; // 0-based
	};
	std::vector<Node> nodes;
	size_t region_count = 0;
	std::vector<Group> groups;
};

class WldFile
{
public:
	struct Fragment
	{
		uint32_t type;
		std::string name;
		size_t offset; // data, after the name reference
		size_t size;
	};

	bool Load(std::vector<uint8_t> data);

	const std::vector<Fragment> &Fragments() const { return m_fragments; }
	// Fragment by 1-based reference; nullptr when out of range.
	const Fragment *Ref(int32_t ref) const;
	int FragmentIndex(int32_t ref) const { return ref >= 1 && ref <= static_cast<int>(m_fragments.size()) ? ref - 1 : -1; }
	std::string String(int32_t ref) const; // a negative name reference
	int Find(const std::string &name) const; // fragment index by (case-insensitive) name, or -1

	bool ReadMesh(int index, WldMesh &mesh) const;
	// A material (0x30) by name, e.g. "GNNCH0201_MDF"; false when there's none.
	bool FindMaterial(const std::string &name, WldMaterial &material) const;
	// Meshes reached from an actor definition (0x14) through its 0x2d references.
	std::vector<int> ActorMeshes(int actor_index) const;
	// The skeleton (0x10) an actor definition uses through its 0x11 reference, or -1.
	int ActorSkeleton(int actor_index) const;
	bool ReadSkeleton(int index, WldSkeleton &skeleton) const;
	// First frame of a track instance (0x13) by name, e.g. "P01HUMPE_TRACK".
	bool ReadTrackPose(const std::string &track, WldBoneTransform &pose) const;
	// Every frame of a track instance, and its frame time (0 when not given).
	bool ReadTrack(const std::string &track, std::vector<WldBoneTransform> &frames, uint32_t &frame_ms) const;

	std::vector<WldPlacement> Placements() const;
	std::vector<WldLight> Lights() const;
	// The zone's BSP tree and region groups; false when there's no tree.
	bool ReadRegionMap(WldRegionMap &map) const;

private:
	bool ReadMaterial(int index, WldMaterial &material) const;
	bool ActorReferences(int index, size_t &refs, uint32_t &count) const;
	bool ReadTrackFrames(int track_instance, std::vector<WldBoneTransform> &frames, size_t limit) const;

	template <typename T>
	T Get(size_t offset) const;

	std::vector<uint8_t> m_data;
	std::vector<char> m_strings;
	std::vector<Fragment> m_fragments;
	std::map<std::string, int> m_by_name; // lowercase
};
