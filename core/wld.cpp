#include "wld.h"

#include "pfs.h"

#include <cmath>
#include <cstring>

namespace
{
	constexpr uint32_t Magic = 0x54503d02;
	constexpr uint32_t OldVersion = 0x00015500;
	const uint8_t HashKey[8] = {0x95, 0x3a, 0xc5, 0x2a, 0x95, 0x7a, 0x95, 0x6a};

	void Decode(char *data, size_t size)
	{
		for (size_t i = 0; i < size; ++i)
			data[i] = static_cast<char>(static_cast<uint8_t>(data[i]) ^ HashKey[i % 8]);
	}
}

template <typename T>
T WldFile::Get(size_t offset) const
{
	T value{};
	if (offset + sizeof(T) <= m_data.size())
		std::memcpy(&value, m_data.data() + offset, sizeof(T));
	return value;
}

bool WldFile::Load(std::vector<uint8_t> data)
{
	m_data = std::move(data);
	m_fragments.clear();
	m_by_name.clear();
	if (m_data.size() < 28 || Get<uint32_t>(0) != Magic || Get<uint32_t>(4) != OldVersion)
		return false;

	uint32_t count = Get<uint32_t>(8);
	uint32_t hash_size = Get<uint32_t>(20);
	if (28 + static_cast<size_t>(hash_size) > m_data.size())
		return false;
	m_strings.assign(m_data.begin() + 28, m_data.begin() + 28 + hash_size);
	Decode(m_strings.data(), m_strings.size());
	m_strings.push_back('\0');

	size_t p = 28 + hash_size;
	m_fragments.reserve(count);
	for (uint32_t i = 0; i < count; ++i) {
		if (p + 12 > m_data.size())
			return false;
		uint32_t size = Get<uint32_t>(p);
		uint32_t type = Get<uint32_t>(p + 4);
		if (size < 4 || p + 8 + size > m_data.size())
			return false;
		Fragment fragment{type, String(Get<int32_t>(p + 8)), p + 12, size - 4};
		if (!fragment.name.empty())
			m_by_name.emplace(ToLower(fragment.name), static_cast<int>(m_fragments.size()));
		m_fragments.push_back(std::move(fragment));
		p += 8 + size;
	}
	return true;
}

const WldFile::Fragment *WldFile::Ref(int32_t ref) const
{
	int index = FragmentIndex(ref);
	return index < 0 ? nullptr : &m_fragments[index];
}

std::string WldFile::String(int32_t ref) const
{
	if (ref >= 0 || static_cast<size_t>(-static_cast<int64_t>(ref)) >= m_strings.size())
		return {};
	return std::string(m_strings.data() + (-static_cast<int64_t>(ref)));
}

int WldFile::Find(const std::string &name) const
{
	auto it = m_by_name.find(ToLower(name));
	return it == m_by_name.end() ? -1 : it->second;
}

bool WldFile::FindMaterial(const std::string &name, WldMaterial &material) const
{
	int index = Find(name);
	return index >= 0 && m_fragments[index].type == 0x30 && ReadMaterial(index, material);
}

// 0x30 material -> 0x05 sprite -> 0x04 sprite definition -> 0x03 bitmap names.
bool WldFile::ReadMaterial(int index, WldMaterial &material) const
{
	const Fragment &f = m_fragments[index];
	if (f.type != 0x30 || f.size < 20)
		return false;
	material.name = f.name;
	material.render_method = Get<uint32_t>(f.offset + 4);

	ReadSpriteFrames(Ref(Get<int32_t>(f.offset + 20)), material); // none: untextured (invisible collision surfaces)
	return true;
}

void WldFile::ReadSpriteFrames(const Fragment *sprite, WldMaterial &material) const
{
	if (!sprite || sprite->type != 0x05)
		return;
	const Fragment *def = Ref(Get<int32_t>(sprite->offset));
	if (!def || def->type != 0x04)
		return;

	uint32_t flags = Get<uint32_t>(def->offset);
	uint32_t frames = Get<uint32_t>(def->offset + 4);
	size_t p = def->offset + 8;
	if (flags & 0x04)
		p += 4; // current frame
	if (flags & 0x08) {
		material.frame_ms = Get<uint32_t>(p);
		p += 4;
	}
	for (uint32_t i = 0; i < frames && i < 256; ++i, p += 4) {
		const Fragment *bitmap = Ref(Get<int32_t>(p));
		if (!bitmap || bitmap->type != 0x03)
			continue;
		// u32 count - 1, then {u16 length, encoded name}; only the first name is used.
		uint16_t length = Get<uint16_t>(bitmap->offset + 4);
		size_t at = bitmap->offset + 6;
		if (length == 0 || at + length > m_data.size())
			continue;
		std::vector<char> name(m_data.begin() + at, m_data.begin() + at + length);
		Decode(name.data(), name.size());
		material.textures.emplace_back(name.data(), strnlen(name.data(), name.size()));
	}
}

bool WldFile::ReadMesh(int index, WldMesh &mesh) const
{
	if (index < 0 || index >= static_cast<int>(m_fragments.size()))
		return false;
	const Fragment &f = m_fragments[index];
	if (f.type != 0x36 || f.size < 92)
		return false;

	size_t o = f.offset;
	mesh = WldMesh{};
	mesh.name = f.name;
	int32_t palette = Get<int32_t>(o + 4);
	mesh.center = {Get<float>(o + 20), Get<float>(o + 24), Get<float>(o + 28)};
	mesh.radius = Get<float>(o + 44);
	uint16_t vertices = Get<uint16_t>(o + 72);
	uint16_t uvs = Get<uint16_t>(o + 74);
	uint16_t normals = Get<uint16_t>(o + 76);
	uint16_t colors = Get<uint16_t>(o + 78);
	uint16_t polygons = Get<uint16_t>(o + 80);
	uint16_t pieces = Get<uint16_t>(o + 82);
	uint16_t polygon_textures = Get<uint16_t>(o + 84);
	int16_t scale_shift = Get<int16_t>(o + 90);
	float scale = 1.0f / static_cast<float>(1 << (scale_shift & 31));

	size_t need = 92 + vertices * 6u + uvs * 4u + normals * 3u + colors * 4u + polygons * 8u + pieces * 4u +
	              polygon_textures * 4u;
	if (need > f.size)
		return false;

	size_t p = o + 92;
	mesh.positions.resize(vertices);
	for (auto &v : mesh.positions) {
		v.x = mesh.center.x + Get<int16_t>(p) * scale;
		v.y = mesh.center.y + Get<int16_t>(p + 2) * scale;
		v.z = mesh.center.z + Get<int16_t>(p + 4) * scale;
		p += 6;
	}
	mesh.uvs.assign(static_cast<size_t>(vertices) * 2, 0.0f);
	for (uint16_t i = 0; i < uvs; ++i, p += 4) {
		if (i < vertices) {
			mesh.uvs[i * 2] = Get<int16_t>(p) / 256.0f;
			mesh.uvs[i * 2 + 1] = Get<int16_t>(p + 2) / 256.0f;
		}
	}
	mesh.normals.resize(vertices);
	for (uint16_t i = 0; i < normals; ++i, p += 3) {
		if (i < vertices)
			mesh.normals[i] = {Get<int8_t>(p) / 128.0f, Get<int8_t>(p + 1) / 128.0f, Get<int8_t>(p + 2) / 128.0f};
	}
	if (colors == vertices)
		mesh.colors.resize(vertices);
	for (uint16_t i = 0; i < colors; ++i, p += 4) {
		if (colors == vertices)
			mesh.colors[i] = Get<uint32_t>(p);
	}
	mesh.triangles.resize(polygons);
	for (auto &t : mesh.triangles) {
		t.flags = Get<uint16_t>(p);
		t.v[0] = Get<uint16_t>(p + 2);
		t.v[1] = Get<uint16_t>(p + 4);
		t.v[2] = Get<uint16_t>(p + 6);
		t.material = 0;
		p += 8;
	}
	mesh.pieces.resize(pieces);
	for (auto &piece : mesh.pieces) {
		piece.count = Get<uint16_t>(p);
		piece.bone = Get<uint16_t>(p + 2);
		p += 4;
	}
	// {u16 polygon count, u16 material}: consecutive polygons share a material.
	size_t next = 0;
	for (uint16_t i = 0; i < polygon_textures; ++i, p += 4) {
		uint16_t count = Get<uint16_t>(p);
		uint16_t material = Get<uint16_t>(p + 2);
		for (uint16_t k = 0; k < count && next < mesh.triangles.size(); ++k)
			mesh.triangles[next++].material = material;
	}
	for (auto &t : mesh.triangles) {
		if (t.v[0] >= vertices || t.v[1] >= vertices || t.v[2] >= vertices)
			t.v[0] = t.v[1] = t.v[2] = 0;
	}

	const Fragment *list = Ref(palette);
	if (list && list->type == 0x31) {
		uint32_t count = Get<uint32_t>(list->offset + 4);
		for (uint32_t i = 0; i < count && i < 4096; ++i) {
			WldMaterial material;
			int ref = FragmentIndex(Get<int32_t>(list->offset + 8 + i * 4));
			if (ref >= 0)
				ReadMaterial(ref, material);
			mesh.materials.push_back(std::move(material));
		}
	}
	return true;
}

// 0x14: flags, callback name, action count, reference count, bounds reference,
// [current action], [7 location words], actions {u32 lod count, u32, lod floats},
// then the references.
bool WldFile::ActorReferences(int index, size_t &refs, uint32_t &count) const
{
	if (index < 0 || index >= static_cast<int>(m_fragments.size()))
		return false;
	const Fragment &f = m_fragments[index];
	if (f.type != 0x14 || f.size < 20)
		return false;
	uint32_t flags = Get<uint32_t>(f.offset);
	uint32_t actions = Get<uint32_t>(f.offset + 8);
	count = Get<uint32_t>(f.offset + 12);
	size_t p = f.offset + 20;
	if (flags & 1)
		p += 4;
	if (flags & 2)
		p += 28;
	for (uint32_t i = 0; i < actions && i < 64; ++i)
		p += 8 + static_cast<size_t>(Get<uint32_t>(p)) * 4;
	refs = p;
	return count <= 64 && p + count * 4 <= f.offset + f.size;
}

std::vector<int> WldFile::ActorMeshes(int actor_index) const
{
	std::vector<int> meshes;
	size_t refs = 0;
	uint32_t count = 0;
	if (!ActorReferences(actor_index, refs, count))
		return meshes;
	for (uint32_t i = 0; i < count; ++i) {
		const Fragment *ref = Ref(Get<int32_t>(refs + i * 4));
		if (ref && ref->type == 0x2d) {
			int mesh = FragmentIndex(Get<int32_t>(ref->offset));
			if (mesh >= 0 && m_fragments[mesh].type == 0x36)
				meshes.push_back(mesh);
		}
	}
	return meshes;
}

int WldFile::ActorSkeleton(int actor_index) const
{
	size_t refs = 0;
	uint32_t count = 0;
	if (!ActorReferences(actor_index, refs, count))
		return -1;
	for (uint32_t i = 0; i < count; ++i) {
		const Fragment *ref = Ref(Get<int32_t>(refs + i * 4));
		if (ref && ref->type == 0x11) {
			int skeleton = FragmentIndex(Get<int32_t>(ref->offset));
			if (skeleton >= 0 && m_fragments[skeleton].type == 0x10)
				return skeleton;
		}
	}
	return -1;
}

// 0x13 track instance -> 0x12 track definition; frames are 8 x i16 when flag 0x08 is set:
// rotation {w, x, y, z} (a quaternion, normalized here) and shift {x, y, z} / denominator.
bool WldFile::ReadTrackFrames(int track_instance, std::vector<WldBoneTransform> &frames, size_t limit) const
{
	frames.clear();
	if (track_instance < 0 || m_fragments[track_instance].type != 0x13)
		return false;
	const Fragment *def = Ref(Get<int32_t>(m_fragments[track_instance].offset));
	if (!def || def->type != 0x12 || def->size < 8)
		return false;
	uint32_t flags = Get<uint32_t>(def->offset);
	uint32_t count = Get<uint32_t>(def->offset + 4);
	if (!(flags & 0x08) || count == 0 || 8 + static_cast<size_t>(count) * 16 > def->size)
		return false; // the float form isn't used by TAKP's files
	for (size_t i = 0; i < count && i < limit; ++i) {
		size_t p = def->offset + 8 + i * 16;
		float w = Get<int16_t>(p), x = Get<int16_t>(p + 2), y = Get<int16_t>(p + 4), z = Get<int16_t>(p + 6);
		// As processTransformationNode (EQGfx) reads it: the rotation / 16384, the shift / 256, and
		// last the bone's scale / 256 (not the shift's denominator).
		WldBoneTransform pose;
		pose.shift = {Get<int16_t>(p + 8) / 256.0f, Get<int16_t>(p + 10) / 256.0f, Get<int16_t>(p + 12) / 256.0f};
		pose.scale = Get<uint16_t>(p + 14) / 256.0f;
		if (pose.scale == 0)
			pose.scale = 1;
		float length = std::sqrt(w * w + x * x + y * y + z * z);
		if (length > 0) {
			pose.w = w / length;
			pose.x = x / length;
			pose.y = y / length;
			pose.z = z / length;
		}
		frames.push_back(pose);
	}
	return true;
}

bool WldFile::ReadTrackPose(const std::string &track, WldBoneTransform &pose) const
{
	std::vector<WldBoneTransform> frames;
	if (!ReadTrackFrames(Find(track), frames, 1))
		return false;
	pose = frames[0];
	return true;
}

bool WldFile::ReadTrack(const std::string &track, std::vector<WldBoneTransform> &frames, uint32_t &frame_ms) const
{
	int index = Find(track);
	if (!ReadTrackFrames(index, frames, 4096))
		return false;
	// 0x13: definition ref, flags, [frame ms when flag 0x01].
	uint32_t flags = Get<uint32_t>(m_fragments[index].offset + 4);
	frame_ms = (flags & 1) ? Get<uint32_t>(m_fragments[index].offset + 8) : 0;
	return true;
}

// 0x10: flags, bone count, polygon animation ref, [3 floats], [bounding radius],
// bones {name, flags, track instance, mesh/sprite, child count, children},
// [mesh count, meshes (0x2d refs), per-mesh words] when flag 0x200 is set.
bool WldFile::ReadSkeleton(int index, WldSkeleton &skeleton) const
{
	if (index < 0 || index >= static_cast<int>(m_fragments.size()) || m_fragments[index].type != 0x10)
		return false;
	const Fragment &f = m_fragments[index];
	skeleton = WldSkeleton{};
	skeleton.name = f.name;
	uint32_t flags = Get<uint32_t>(f.offset);
	uint32_t count = Get<uint32_t>(f.offset + 4);
	if (count > 512)
		return false;
	size_t p = f.offset + 12;
	if (flags & 1)
		p += 12;
	if (flags & 2)
		p += 4;
	skeleton.bones.resize(count);
	for (auto &bone : skeleton.bones) {
		bone.name = String(Get<int32_t>(p));
		int track = FragmentIndex(Get<int32_t>(p + 8));
		int model = FragmentIndex(Get<int32_t>(p + 12));
		if (model >= 0 && m_fragments[model].type == 0x2d) {
			int mesh = FragmentIndex(Get<int32_t>(m_fragments[model].offset));
			if (mesh >= 0 && m_fragments[mesh].type == 0x36)
				bone.mesh = mesh;
		}
		else if (model >= 0 && m_fragments[model].type == 0x34)
			bone.particle_cloud = model;
		uint32_t children = Get<uint32_t>(p + 16);
		p += 20;
		if (track >= 0) {
			bone.track = m_fragments[track].name;
			std::vector<WldBoneTransform> frames;
			if (ReadTrackFrames(track, frames, 1))
				bone.pose = frames[0];
		}
		for (uint32_t c = 0; c < children && c < 512; ++c, p += 4) {
			uint32_t child = Get<uint32_t>(p);
			if (child < count)
				bone.children.push_back(static_cast<int>(child));
		}
		if (p > f.offset + f.size)
			return false;
	}
	for (int i = 0; i < static_cast<int>(count); ++i)
		for (int child : skeleton.bones[i].children)
			skeleton.bones[child].parent = i;

	if (flags & 0x200) {
		uint32_t meshes = Get<uint32_t>(p);
		p += 4;
		for (uint32_t i = 0; i < meshes && i < 64; ++i, p += 4) {
			const Fragment *ref = Ref(Get<int32_t>(p));
			if (ref && ref->type == 0x2d) {
				int mesh = FragmentIndex(Get<int32_t>(ref->offset));
				if (mesh >= 0 && m_fragments[mesh].type == 0x36)
					skeleton.meshes.push_back(mesh);
			}
		}
	}
	return true;
}

bool WldFile::ReadParticleCloud(int index, WldParticleCloud &cloud) const
{
	if (index < 0 || index >= static_cast<int>(m_fragments.size()) || m_fragments[index].type != 0x34)
		return false;
	const Fragment &f = m_fragments[index];
	if (f.size < 80)
		return false;
	// u32 fragment flags (1: the box follows, 2: two more vectors, 4: the sprite), kind, spawn
	// type, cloud flags, count, gravity, its direction, a duration, radius, angle, lifespan,
	// speed, normal, interval, size, color; then by flag the box, the vectors and the sprite.
	auto vec = [&](size_t at) { return WldVec3{Get<float>(at), Get<float>(at + 4), Get<float>(at + 8)}; };
	size_t o = f.offset;
	uint32_t fragment_flags = Get<uint32_t>(o);
	cloud = WldParticleCloud{};
	cloud.kind = Get<uint32_t>(o + 4);
	cloud.spawn_type = Get<uint32_t>(o + 8);
	cloud.flags = Get<uint32_t>(o + 12);
	cloud.max_particles = Get<uint32_t>(o + 16);
	cloud.gravity = Get<float>(o + 20);
	cloud.gravity_direction = vec(o + 24);
	cloud.radius = Get<float>(o + 40);
	cloud.angle = Get<float>(o + 44);
	cloud.lifespan_ms = Get<uint32_t>(o + 48);
	cloud.speed = Get<float>(o + 52);
	cloud.normal = vec(o + 56);
	cloud.interval_ms = Get<uint32_t>(o + 68);
	cloud.size = Get<float>(o + 72);
	cloud.color = Get<uint32_t>(o + 76);
	size_t p = o + 80;
	if (fragment_flags & 1) {
		if (p + 24 > o + f.size)
			return false;
		cloud.low = vec(p);
		cloud.high = vec(p + 12);
		p += 24;
	}
	if (fragment_flags & 2)
		p += 24;
	if ((fragment_flags & 4) && p + 4 <= o + f.size) {
		// A blit sprite (0x26): flags, its sprite (0x05), the render method.
		const Fragment *blit = Ref(Get<int32_t>(p));
		if (blit && blit->type == 0x26 && blit->size >= 12) {
			cloud.sprite.name = blit->name;
			cloud.sprite.render_method = Get<uint32_t>(blit->offset + 8);
			ReadSpriteFrames(Ref(Get<int32_t>(blit->offset + 4)), cloud.sprite);
		}
	}
	return true;
}

bool WldFile::FindBlitSprite(const std::string &name, WldMaterial &sprite) const
{
	int index = Find(name);
	if (index < 0)
		return false;
	const Fragment *blit = &m_fragments[index];
	if (blit->type == 0x14) {
		// An actor definition: its blit sprite instance (0x27), then that's definition.
		size_t refs = 0;
		uint32_t count = 0;
		blit = nullptr;
		if (ActorReferences(index, refs, count))
			for (uint32_t i = 0; i < count && !blit; ++i) {
				const Fragment *ref = Ref(Get<int32_t>(refs + i * 4));
				if (ref && ref->type == 0x27 && ref->size >= 4)
					blit = Ref(Get<int32_t>(ref->offset));
			}
	}
	// 0x26: flags, its sprite (0x05), the render method.
	if (!blit || blit->type != 0x26 || blit->size < 12)
		return false;
	sprite = WldMaterial{};
	sprite.name = blit->name;
	sprite.render_method = Get<uint32_t>(blit->offset + 8);
	ReadSpriteFrames(Ref(Get<int32_t>(blit->offset + 4)), sprite);
	return true;
}

// 0x15 in objects.wld: actor name ref, flags, sphere ref, x y z, rotation[3], unknown,
// scale y, scale x, vertex color ref.
std::vector<WldPlacement> WldFile::Placements() const
{
	// 0x15: actor definition name, flags, sphere ref, then by flag: 0x01 current action,
	// 0x02 location (x y z, three rotations, one unused), 0x04 bounding radius, 0x08 scale,
	// 0x10 sound ref, 0x100 vertex color ref (0x33).
	std::vector<WldPlacement> placements;
	for (const auto &f : m_fragments) {
		if (f.type != 0x15 || f.size < 12)
			continue;
		WldPlacement placement;
		placement.actor = String(Get<int32_t>(f.offset));
		if (placement.actor.empty())
			continue;
		uint32_t flags = Get<uint32_t>(f.offset + 4);
		size_t p = f.offset + 12, end = f.offset + f.size;
		if (flags & 0x01)
			p += 4;
		if (flags & 0x02) {
			if (p + 28 > end)
				continue;
			placement.position = {Get<float>(p), Get<float>(p + 4), Get<float>(p + 8)};
			placement.rotation = {Get<float>(p + 12), Get<float>(p + 16), Get<float>(p + 20)};
			p += 28;
		}
		if (flags & 0x04)
			p += 4;
		if ((flags & 0x08) && p + 4 <= end) {
			placement.scale = Get<float>(p);
			p += 4;
		}
		if (!(placement.scale > 0))
			placement.scale = 1;
		if (flags & 0x10)
			p += 4;
		if ((flags & 0x100) && p + 4 <= end) {
			// 0x33: 0x32 ref, flags. 0x32: flags, count, three words, then the colors.
			const Fragment *instance = Ref(Get<int32_t>(p));
			const Fragment *track = instance && instance->type == 0x33 ? Ref(Get<int32_t>(instance->offset)) : nullptr;
			if (track && track->type == 0x32 && track->size >= 20) {
				uint32_t count = Get<uint32_t>(track->offset + 4);
				if (20 + static_cast<size_t>(count) * 4 <= track->size)
					for (uint32_t i = 0; i < count; ++i)
						placement.colors.push_back(Get<uint32_t>(track->offset + 20 + i * 4));
			}
		}
		placements.push_back(std::move(placement));
	}
	return placements;
}

bool WldFile::ConstantAmbient(uint32_t &color) const
{
	// 0x35 is only the color, where other fragments have their name: blue, green, red, alpha.
	for (const auto &f : m_fragments)
		if (f.type == 0x35 && f.offset >= 4) {
			color = Get<uint32_t>(f.offset - 4) & 0xffffff;
			return true;
		}
	return false;
}

// 0x28: light instance ref, flags, x y z, radius. 0x1c -> 0x1b: flags, frames,
// [current frame], [sleep], [levels], [colors r g b].
std::vector<WldLight> WldFile::Lights() const
{
	std::vector<WldLight> lights;
	for (const auto &f : m_fragments) {
		if (f.type != 0x28 || f.size < 24)
			continue;
		WldLight light;
		light.position = {Get<float>(f.offset + 8), Get<float>(f.offset + 12), Get<float>(f.offset + 16)};
		light.radius = Get<float>(f.offset + 20);
		const Fragment *instance = Ref(Get<int32_t>(f.offset));
		const Fragment *def = instance && instance->type == 0x1c ? Ref(Get<int32_t>(instance->offset)) : nullptr;
		if (def && def->type == 0x1b) {
			uint32_t flags = Get<uint32_t>(def->offset);
			uint32_t frames = Get<uint32_t>(def->offset + 4);
			size_t p = def->offset + 8;
			if (flags & 0x01)
				p += 4;
			if (flags & 0x02)
				p += 4;
			float level = 1;
			if (flags & 0x04) {
				level = Get<float>(p);
				p += 4 * static_cast<size_t>(frames);
			}
			if (flags & 0x10) {
				light.r = Get<float>(p) * level;
				light.g = Get<float>(p + 4) * level;
				light.b = Get<float>(p + 8) * level;
			}
			else
				light.r = light.g = light.b = level;
		}
		lights.push_back(light);
	}
	return lights;
}

// 0x21: node count, then {normal x y z, distance, region, front, back} (28 bytes) per node.
// 0x22: one per region. 0x29: flags, region count, 0-based regions, then a user data string
// (its length, then text encoded like the string hash).
bool WldFile::ReadRegionMap(WldRegionMap &map) const
{
	map = {};
	for (const auto &f : m_fragments) {
		if (f.type == 0x21 && map.nodes.empty() && f.size >= 4) {
			uint32_t count = Get<uint32_t>(f.offset);
			if (4 + static_cast<size_t>(count) * 28 > f.size)
				continue;
			map.nodes.resize(count);
			for (uint32_t i = 0; i < count; ++i) {
				size_t p = f.offset + 4 + static_cast<size_t>(i) * 28;
				WldRegionMap::Node &node = map.nodes[i];
				node.normal = {Get<float>(p), Get<float>(p + 4), Get<float>(p + 8)};
				node.distance = Get<float>(p + 12);
				node.region = Get<int32_t>(p + 16);
				node.front = Get<int32_t>(p + 20);
				node.back = Get<int32_t>(p + 24);
			}
		}
		else if (f.type == 0x22)
			++map.region_count;
		else if (f.type == 0x29 && f.size >= 8) {
			WldRegionMap::Group group;
			group.name = f.name;
			uint32_t count = Get<uint32_t>(f.offset + 4);
			size_t p = f.offset + 8;
			if (p + static_cast<size_t>(count) * 4 > f.offset + f.size)
				continue;
			for (uint32_t i = 0; i < count; ++i, p += 4)
				group.regions.push_back(Get<uint32_t>(p));
			if (p + 4 <= f.offset + f.size) {
				uint32_t length = Get<uint32_t>(p);
				p += 4;
				if (length > 0 && p + length <= f.offset + f.size) {
					std::string text(reinterpret_cast<const char *>(m_data.data() + p), length);
					Decode(text.data(), text.size());
					group.user_data = text.c_str(); // up to the terminator
				}
			}
			map.groups.push_back(std::move(group));
		}
	}
	return !map.nodes.empty();
}
