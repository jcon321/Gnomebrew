#include "zone_scene.h"

#include "skeleton.h"

#include <algorithm>
#include <cmath>
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

	void DrawStaticMesh(Renderer &renderer, const StaticMesh &mesh, uint64_t now_ms, const StaticVertices *pool)
	{
		if (!renderer.SphereVisible(mesh.center, mesh.radius))
			return;
		bool first = true;
		for (const auto &batch : mesh.batches) {
			Material material = batch.material;
			if (!batch.frames.empty()) {
				size_t frame = batch.frames.size() > 1 && batch.frame_ms ? (now_ms / batch.frame_ms) % batch.frames.size() : 0;
				material.texture = batch.frames[frame];
			}
			if (pool)
				renderer.DrawStatic(*pool, mesh.first_vertex, mesh.vertex_count, batch.indices.data(), batch.indices.size(),
					material, !first);
			else
				renderer.Draw(mesh.vertices.data(), mesh.vertices.size(), batch.indices.data(), batch.indices.size(), material, !first);
			first = false;
		}
	}

	bool ZoneScene::Load(const std::string &eq_dir, const std::string &zone, std::string &error, const ZoneLighting &lighting)
	{
		m_meshes.clear();
		m_vertices = {};
		m_regions = {};
		m_collision.clear();
		m_collision_exposure.clear();
		m_cell_start.clear();
		m_cell_triangles.clear();
		m_grid_w = m_grid_h = 0;
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
		WldRegionMap region_map;
		if (zone_wld.ReadRegionMap(region_map))
			m_regions.Load(region_map);
		m_static_lights.clear();
		m_static_light_regions.clear();
		for (const auto &light : lights) {
			m_static_lights.push_back({ToVec(light.position), {light.r, light.g, light.b}, light.radius});
			m_static_light_regions.push_back(m_regions.RegionsTouched(ToVec(light.position), light.radius));
		}
		uint32_t constant = 0;
		zone_wld.ConstantAmbient(constant);
		m_constant_ambient = {((constant >> 16) & 0xff) / 256.0f, ((constant >> 8) & 0xff) / 256.0f, (constant & 0xff) / 256.0f};
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
				// The placement's vertex colors run through the meshes in turn.
				size_t vertices = 0;
				for (const auto &m : meshes)
					vertices += m.positions.size();
				if (!placement.colors.empty() && placement.colors.size() == vertices) {
					size_t next = 0;
					for (auto &m : meshes) {
						m.colors.assign(placement.colors.begin() + next, placement.colors.begin() + next + m.positions.size());
						next += m.positions.size();
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

		// Every mesh's vertices in one array, which a GPU renderer keeps.
		for (auto &mesh : m_meshes) {
			mesh.first_vertex = m_vertices.vertices.size();
			mesh.vertex_count = mesh.vertices.size();
			m_vertices.vertices.insert(m_vertices.vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
			mesh.vertices = {};
		}
		BuildGrid();
		return true;
	}

	void ZoneScene::BuildGrid()
	{
		if (m_collision.empty())
			return;
		float x0 = m_collision[0].x, y0 = m_collision[0].y, x1 = x0, y1 = y0;
		for (const Vec3 &v : m_collision) {
			x0 = std::min(x0, v.x);
			y0 = std::min(y0, v.y);
			x1 = std::max(x1, v.x);
			y1 = std::max(y1, v.y);
		}
		// 16-unit cells, coarser for zones that would need more than 1024 a side.
		m_cell_size = std::max({16.0f, (x1 - x0) / 1024, (y1 - y0) / 1024});
		m_grid_x = x0;
		m_grid_y = y0;
		m_grid_w = static_cast<int>((x1 - x0) / m_cell_size) + 1;
		m_grid_h = static_cast<int>((y1 - y0) / m_cell_size) + 1;

		// Count each cell's triangles, then fill the lists in triangle order.
		auto cells = [&](size_t t, int &cx0, int &cy0, int &cx1, int &cy1) {
			const Vec3 &a = m_collision[t * 3], &b = m_collision[t * 3 + 1], &c = m_collision[t * 3 + 2];
			cx0 = static_cast<int>((std::min({a.x, b.x, c.x}) - m_grid_x) / m_cell_size);
			cy0 = static_cast<int>((std::min({a.y, b.y, c.y}) - m_grid_y) / m_cell_size);
			cx1 = std::min(m_grid_w - 1, static_cast<int>((std::max({a.x, b.x, c.x}) - m_grid_x) / m_cell_size));
			cy1 = std::min(m_grid_h - 1, static_cast<int>((std::max({a.y, b.y, c.y}) - m_grid_y) / m_cell_size));
		};
		size_t triangles = m_collision.size() / 3;
		m_cell_start.assign(static_cast<size_t>(m_grid_w) * m_grid_h + 1, 0);
		for (size_t t = 0; t < triangles; ++t) {
			int cx0, cy0, cx1, cy1;
			cells(t, cx0, cy0, cx1, cy1);
			for (int cy = cy0; cy <= cy1; ++cy)
				for (int cx = cx0; cx <= cx1; ++cx)
					++m_cell_start[static_cast<size_t>(cy) * m_grid_w + cx + 1];
		}
		for (size_t i = 1; i < m_cell_start.size(); ++i)
			m_cell_start[i] += m_cell_start[i - 1];
		m_cell_triangles.resize(m_cell_start.back());
		std::vector<uint32_t> fill(m_cell_start.begin(), m_cell_start.end() - 1);
		for (size_t t = 0; t < triangles; ++t) {
			int cx0, cy0, cx1, cy1;
			cells(t, cx0, cy0, cx1, cy1);
			for (int cy = cy0; cy <= cy1; ++cy)
				for (int cx = cx0; cx <= cx1; ++cx)
					m_cell_triangles[fill[static_cast<size_t>(cy) * m_grid_w + cx]++] = static_cast<uint32_t>(t);
		}
	}

	void ZoneScene::Candidates(float x0, float y0, float x1, float y1, std::vector<uint32_t> &out) const
	{
		out.clear();
		if (m_grid_w == 0)
			return;
		int cx0 = std::max(0, static_cast<int>(std::floor((x0 - m_grid_x) / m_cell_size)));
		int cy0 = std::max(0, static_cast<int>(std::floor((y0 - m_grid_y) / m_cell_size)));
		int cx1 = std::min(m_grid_w - 1, static_cast<int>(std::floor((x1 - m_grid_x) / m_cell_size)));
		int cy1 = std::min(m_grid_h - 1, static_cast<int>(std::floor((y1 - m_grid_y) / m_cell_size)));
		if (cx0 > cx1 || cy0 > cy1)
			return;
		for (int cy = cy0; cy <= cy1; ++cy) {
			const uint32_t *begin = m_cell_triangles.data() + m_cell_start[static_cast<size_t>(cy) * m_grid_w + cx0];
			const uint32_t *end = m_cell_triangles.data() + m_cell_start[static_cast<size_t>(cy) * m_grid_w + cx1 + 1];
			out.insert(out.end(), begin, end);
		}
		// Cells share big triangles; test each once, in the zone's order (ties come out as before).
		if (cx0 != cx1 || cy0 != cy1) {
			std::sort(out.begin(), out.end());
			out.erase(std::unique(out.begin(), out.end()), out.end());
		}
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
			if (i < mesh.normals.size())
				v.normal = Normalize(transform.ApplyDirection(ToVec(mesh.normals[i])));
			v.u = mesh.uvs[i * 2];
			v.v = mesh.uvs[i * 2 + 1];
			low = {std::min(low.x, v.position.x), std::min(low.y, v.position.y), std::min(low.z, v.position.z)};
			high = {std::max(high.x, v.position.x), std::max(high.y, v.position.y), std::max(high.z, v.position.z)};

			// Flat ambient, the mesh's own (small, additive) vertex colors, and the point lights.
			// The colors' alpha is how much of the zone's ambient light reaches the vertex; it rides
			// along in the top byte for Renderer::SetVertexLight.
			// applyLightingToMesh adds the world's constant ambient to meshes without colors only.
			float r = lighting.ambient, g = lighting.ambient, b = lighting.ambient;
			uint32_t exposure = 0xff;
			if (mesh.colors.empty()) {
				r += m_constant_ambient.x;
				g += m_constant_ambient.y;
				b += m_constant_ambient.z;
			}
			else {
				uint32_t c = mesh.colors[i];
				r += ((c >> 16) & 0xff) / 255.0f;
				g += ((c >> 8) & 0xff) / 255.0f;
				b += (c & 0xff) / 255.0f;
				exposure = c >> 24;
			}
			for (const auto &light : lighting.point_lights ? lights : std::vector<WldLight>{}) {
				Vec3 d = ToVec(light.position) - v.position;
				float distance = Length(d);
				if (distance >= light.radius)
					continue;
				float attenuation = 1 - distance / light.radius;
				r += light.r * attenuation;
				g += light.g * attenuation;
				b += light.b * attenuation;
			}
			v.color = PackLight(r, g, b) | exposure << 24;
		}
		if (out.vertices.empty())
			return;
		out.center = (low + high) * 0.5f;
		for (const auto &v : out.vertices)
			out.radius = std::max(out.radius, Length(v.position - out.center));

		// Unlit materials (fire) are full bright: their triangles get copies of the vertices,
		// white and out of the ambient light's reach.
		StaticMesh unlit;
		unlit.center = out.center;
		unlit.radius = out.radius;
		std::map<uint16_t, uint32_t> unlit_vertex;
		std::map<uint16_t, size_t> batch_of;
		for (const auto &t : mesh.triangles) {
			if (solid && !(t.flags & 0x10)) {
				for (int k = 0; k < 3; ++k)
					m_collision.push_back(out.vertices[t.v[k]].position);
				// s3dGetActorScaledAmbient: the third of its vertices' alphas / 256, 1 without colors.
				float exposure = 1;
				if (!mesh.colors.empty()) {
					exposure = 0;
					for (int k = 0; k < 3; ++k)
						exposure += (out.vertices[t.v[k]].color >> 24) * (1.0f / 256) / 3;
				}
				m_collision_exposure.push_back(exposure);
			}

			if (t.material >= mesh.materials.size())
				continue;
			const WldMaterial &source = mesh.materials[t.material];
			MaterialInfo info = DescribeRenderMethod(source.render_method);
			if (!info.visible)
				continue;
			StaticMesh &target = info.unlit ? unlit : out;
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
				found = batch_of.emplace(t.material, target.batches.size()).first;
				target.batches.push_back(std::move(batch));
			}
			auto &indices = target.batches[found->second].indices;
			for (int k = 0; k < 3; ++k) {
				if (!info.unlit) {
					indices.push_back(t.v[k]);
					continue;
				}
				auto copy = unlit_vertex.find(t.v[k]);
				if (copy == unlit_vertex.end()) {
					copy = unlit_vertex.emplace(t.v[k], static_cast<uint32_t>(unlit.vertices.size())).first;
					Vertex v = out.vertices[t.v[k]];
					v.color = 0x00ffffff;
					unlit.vertices.push_back(v);
				}
				indices.push_back(copy->second);
			}
		}
		if (!out.batches.empty())
			m_meshes.push_back(std::move(out));
		if (!unlit.batches.empty()) {
			unlit.unlit = true;
			m_meshes.push_back(std::move(unlit));
		}
	}

	void ZoneScene::Draw(Renderer &renderer, uint64_t now_ms, const std::vector<PointLight> &lights, bool sun) const
	{
		// A light reaches a mesh when their spheres meet (the client: when its sphere reaches
		// the mesh's region).
		uint32_t set = ~0u;
		for (const auto &mesh : m_meshes) {
			uint32_t mask = 0;
			if (!mesh.unlit)
				for (size_t i = 0; i < lights.size() && i < Renderer::SceneLightLimit; ++i)
					if (Length(lights[i].position - mesh.center) < lights[i].radius + mesh.radius)
						mask |= 1u << i;
			uint32_t state = mask | (sun && !mesh.unlit ? 1u << 31 : 0);
			if (state != set) {
				renderer.SetDynamicLights(sun && !mesh.unlit, mask);
				set = state;
			}
			DrawStaticMesh(renderer, mesh, now_ms, &m_vertices);
		}
		renderer.SetDynamicLights(false, 0);
	}

	bool ZoneScene::FloorBelow(const Vec3 &from, float depth, float &floor_z, float *exposure) const
	{
		thread_local std::vector<uint32_t> nearby;
		Candidates(from.x, from.y, from.x, from.y, nearby);
		bool found = false;
		for (uint32_t t : nearby) {
			const Vec3 &a = m_collision[t * 3], &b = m_collision[t * 3 + 1], &c = m_collision[t * 3 + 2];
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
				if (exposure)
					*exposure = t < m_collision_exposure.size() ? m_collision_exposure[t] : 1;
			}
		}
		return found;
	}

	bool ZoneScene::SegmentHit(const Vec3 &from, const Vec3 &to, float &fraction, Vec3 *normal) const
	{
		// Moller-Trumbore against the solid triangles near the segment, both sides.
		Vec3 d = to - from;
		Vec3 low{std::min(from.x, to.x), std::min(from.y, to.y), std::min(from.z, to.z)};
		Vec3 high{std::max(from.x, to.x), std::max(from.y, to.y), std::max(from.z, to.z)};
		thread_local std::vector<uint32_t> nearby;
		Candidates(low.x, low.y, high.x, high.y, nearby);
		bool found = false;
		fraction = 1;
		for (uint32_t triangle : nearby) {
			const Vec3 &a = m_collision[triangle * 3], &b = m_collision[triangle * 3 + 1], &c = m_collision[triangle * 3 + 2];
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
				if (normal) {
					*normal = Normalize(Cross(e1, e2));
					if (Dot(*normal, d) > 0)
						*normal = *normal * -1.0f;
				}
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
