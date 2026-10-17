#include "zone_scene.h"

#include "skeleton.h"

#include <algorithm>
#include <map>

namespace render
{
	namespace
	{
		Vec3 ToVec(const WldVec3 &v) { return {v.x, v.y, v.z}; }

		uint32_t PackLight(float r, float g, float b)
		{
			auto channel = [](float c) { return static_cast<uint32_t>(std::clamp(c, 0.0f, 1.0f) * 255.0f + 0.5f); };
			return (channel(r) << 16) | (channel(g) << 8) | channel(b);
		}

		// Placement rotations are in 512ths of a turn: [0] about z (heading), then y, then x.
		Quat PlacementRotation(const WldVec3 &rotation)
		{
			Quat z = AxisAngle({0, 0, 1}, HeadingToRadians(rotation.x));
			Quat y = AxisAngle({0, 1, 0}, HeadingToRadians(rotation.y));
			Quat x = AxisAngle({1, 0, 0}, HeadingToRadians(rotation.z));
			return z * y * x;
		}
	}

	void DrawStaticMesh(Rasterizer &rasterizer, const StaticMesh &mesh, uint64_t now_ms)
	{
		if (!rasterizer.SphereVisible(mesh.center, mesh.radius))
			return;
		for (const auto &batch : mesh.batches) {
			Material material = batch.material;
			if (!batch.frames.empty()) {
				size_t frame = batch.frames.size() > 1 && batch.frame_ms ? (now_ms / batch.frame_ms) % batch.frames.size() : 0;
				material.texture = batch.frames[frame];
			}
			rasterizer.Draw(mesh.vertices.data(), mesh.vertices.size(), batch.indices.data(), batch.indices.size(), material);
		}
	}

	bool ZoneScene::Load(const std::string &eq_dir, const std::string &zone, std::string &error, const ZoneLighting &lighting)
	{
		m_meshes.clear();
		m_collision.clear();
		m_warnings.clear();
		m_object_count = 0;

		std::string base = eq_dir + "/" + zone;
		if (!m_zone_archive.Open(base + ".s3d")) {
			error = "Couldn't open " + base + ".s3d";
			return false;
		}
		std::vector<uint8_t> data;
		WldFile zone_wld, objects_wld, lights_wld, object_models;
		if (!m_zone_archive.Read(zone + ".wld", data) || !zone_wld.Load(std::move(data))) {
			error = "Couldn't read " + zone + ".wld";
			return false;
		}
		m_textures.AddArchive(&m_zone_archive);
		std::vector<WldLight> lights;
		if (m_zone_archive.Read("lights.wld", data) && lights_wld.Load(std::move(data)))
			lights = lights_wld.Lights();
		bool have_objects = m_object_archive.Open(base + "_obj.s3d");
		if (have_objects) {
			m_textures.AddArchive(&m_object_archive);
			if (!m_object_archive.Read(zone + "_obj.wld", data) || !object_models.Load(std::move(data))) {
				m_warnings.push_back("couldn't read " + zone + "_obj.wld");
				have_objects = false;
			}
		}

		WldMesh mesh;
		for (int i = 0; i < static_cast<int>(zone_wld.Fragments().size()); ++i) {
			if (zone_wld.Fragments()[i].type == 0x36 && zone_wld.ReadMesh(i, mesh))
				AddMesh(mesh, Transform{}, true, lights, lighting);
		}

		if (have_objects && m_zone_archive.Read("objects.wld", data) && objects_wld.Load(std::move(data))) {
			std::map<std::string, int> unknown;
			for (const auto &placement : objects_wld.Placements()) {
				int actor = object_models.Find(placement.actor);
				if (actor < 0) {
					++unknown[placement.actor];
					continue;
				}
				Transform transform;
				transform.rotation = PlacementRotation(placement.rotation);
				transform.translation = ToVec(placement.position);
				transform.scale = placement.scale;

				std::vector<WldMesh> meshes;
				for (int index : object_models.ActorMeshes(actor)) {
					if (object_models.ReadMesh(index, mesh))
						meshes.push_back(mesh);
				}
				int skeleton_index = object_models.ActorSkeleton(actor);
				WldSkeleton skeleton;
				if (skeleton_index >= 0 && object_models.ReadSkeleton(skeleton_index, skeleton)) {
					auto bones = PoseSkeleton(skeleton, nullptr, 0);
					for (int index : skeleton.meshes) {
						if (object_models.ReadMesh(index, mesh)) {
							SkinMesh(mesh, bones);
							meshes.push_back(mesh);
						}
					}
				}
				for (const auto &m : meshes)
					AddMesh(m, transform, true, lights, lighting);
				++m_object_count;
			}
			for (const auto &[name, count] : unknown)
				m_warnings.push_back("object " + name + " not in " + zone + "_obj.wld (" + std::to_string(count) + "x)");
		}
		for (const auto &name : m_textures.Missing())
			m_warnings.push_back("missing texture " + name);
		return true;
	}

	void ZoneScene::AddMesh(const WldMesh &mesh, const Transform &transform, bool solid, const std::vector<WldLight> &lights,
		const ZoneLighting &lighting)
	{
		StaticMesh out;
		out.vertices.resize(mesh.positions.size());
		Vec3 low{1e30f, 1e30f, 1e30f}, high{-1e30f, -1e30f, -1e30f};
		for (size_t i = 0; i < mesh.positions.size(); ++i) {
			Vertex &v = out.vertices[i];
			v.position = transform.Apply(ToVec(mesh.positions[i]));
			v.u = mesh.uvs[i * 2];
			v.v = mesh.uvs[i * 2 + 1];
			low = {std::min(low.x, v.position.x), std::min(low.y, v.position.y), std::min(low.z, v.position.z)};
			high = {std::max(high.x, v.position.x), std::max(high.y, v.position.y), std::max(high.z, v.position.z)};

			// Flat ambient, the mesh's own (small, additive) vertex colors, and the point lights.
			float r = lighting.ambient, g = lighting.ambient, b = lighting.ambient;
			if (!mesh.colors.empty()) {
				uint32_t c = mesh.colors[i];
				r += ((c >> 16) & 0xff) / 255.0f;
				g += ((c >> 8) & 0xff) / 255.0f;
				b += (c & 0xff) / 255.0f;
			}
			for (const auto &light : lights) {
				Vec3 d = ToVec(light.position) - v.position;
				float distance = Length(d);
				if (distance >= light.radius)
					continue;
				float attenuation = 1 - distance / light.radius;
				r += light.r * attenuation;
				g += light.g * attenuation;
				b += light.b * attenuation;
			}
			v.color = PackLight(r, g, b);
		}
		if (out.vertices.empty())
			return;
		out.center = (low + high) * 0.5f;
		for (const auto &v : out.vertices)
			out.radius = std::max(out.radius, Length(v.position - out.center));

		std::map<uint16_t, size_t> batch_of;
		for (const auto &t : mesh.triangles) {
			if (solid && !(t.flags & 0x10))
				for (int k = 0; k < 3; ++k)
					m_collision.push_back(out.vertices[t.v[k]].position);

			if (t.material >= mesh.materials.size())
				continue;
			const WldMaterial &source = mesh.materials[t.material];
			MaterialInfo info = DescribeRenderMethod(source.render_method);
			if (!info.visible)
				continue;
			auto found = batch_of.find(t.material);
			if (found == batch_of.end()) {
				StaticMesh::Batch batch;
				batch.material.blend = info.blend;
				batch.material.opacity = info.opacity;
				batch.material.two_sided = info.blend != Blend::Opaque;
				batch.frame_ms = source.frame_ms;
				for (const auto &name : source.textures)
					if (const Texture *texture = m_textures.Get(name, info.masked))
						batch.frames.push_back(texture);
				found = batch_of.emplace(t.material, out.batches.size()).first;
				out.batches.push_back(std::move(batch));
			}
			auto &indices = out.batches[found->second].indices;
			indices.insert(indices.end(), {t.v[0], t.v[1], t.v[2]});
		}
		if (!out.batches.empty())
			m_meshes.push_back(std::move(out));
	}

	void ZoneScene::Draw(Rasterizer &rasterizer, uint64_t now_ms) const
	{
		for (const auto &mesh : m_meshes)
			DrawStaticMesh(rasterizer, mesh, now_ms);
	}

	bool ZoneScene::FloorBelow(const Vec3 &from, float depth, float &floor_z) const
	{
		bool found = false;
		for (size_t i = 0; i + 2 < m_collision.size(); i += 3) {
			const Vec3 &a = m_collision[i], &b = m_collision[i + 1], &c = m_collision[i + 2];
			// Barycentric coordinates of (x, y) in the triangle's top-down projection.
			float d = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
			if (std::fabs(d) < 1e-6f)
				continue;
			float l1 = ((b.y - c.y) * (from.x - c.x) + (c.x - b.x) * (from.y - c.y)) / d;
			float l2 = ((c.y - a.y) * (from.x - c.x) + (a.x - c.x) * (from.y - c.y)) / d;
			float l3 = 1 - l1 - l2;
			if (l1 < 0 || l2 < 0 || l3 < 0)
				continue;
			float z = l1 * a.z + l2 * b.z + l3 * c.z;
			if (z <= from.z + 0.01f && z >= from.z - depth && (!found || z > floor_z)) {
				floor_z = z;
				found = true;
			}
		}
		return found;
	}

	bool ZoneScene::SegmentHit(const Vec3 &from, const Vec3 &to, float &fraction) const
	{
		// Moller-Trumbore against every solid triangle, both sides.
		Vec3 d = to - from;
		Vec3 low{std::min(from.x, to.x), std::min(from.y, to.y), std::min(from.z, to.z)};
		Vec3 high{std::max(from.x, to.x), std::max(from.y, to.y), std::max(from.z, to.z)};
		bool found = false;
		fraction = 1;
		for (size_t i = 0; i + 2 < m_collision.size(); i += 3) {
			const Vec3 &a = m_collision[i], &b = m_collision[i + 1], &c = m_collision[i + 2];
			if (std::max({a.x, b.x, c.x}) < low.x || std::min({a.x, b.x, c.x}) > high.x || std::max({a.y, b.y, c.y}) < low.y ||
				std::min({a.y, b.y, c.y}) > high.y || std::max({a.z, b.z, c.z}) < low.z || std::min({a.z, b.z, c.z}) > high.z)
				continue;
			Vec3 e1 = b - a, e2 = c - a;
			Vec3 p = Cross(d, e2);
			float det = Dot(e1, p);
			if (std::fabs(det) < 1e-8f)
				continue;
			float inv = 1 / det;
			Vec3 s = from - a;
			float u = Dot(s, p) * inv;
			if (u < 0 || u > 1)
				continue;
			Vec3 q = Cross(s, e1);
			float v = Dot(d, q) * inv;
			if (v < 0 || u + v > 1)
				continue;
			float t = Dot(e2, q) * inv;
			if (t >= 0 && t < fraction) {
				fraction = t;
				found = true;
			}
		}
		return found;
	}

	size_t ZoneScene::TriangleCount() const
	{
		size_t count = 0;
		for (const auto &mesh : m_meshes)
			for (const auto &batch : mesh.batches)
				count += batch.indices.size() / 3;
		return count;
	}
}
