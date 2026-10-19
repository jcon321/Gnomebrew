#pragma once

// A zone ready to draw: the region meshes of <zone>.s3d plus the objects that
// objects.wld places from <zone>_obj.s3d, all transformed into world space and
// lit once at load (lights.wld point lights over a flat ambient).

#include "occlusion.h"
#include "renderer.h"
#include "textures.h"
#include "zone_regions.h"

#include <string>
#include <vector>

namespace render
{
	// A static mesh in world space, split into one batch per material.
	struct StaticMesh
	{
		struct Batch
		{
			std::vector<const Texture *> frames; // animated when more than one
			uint32_t frame_ms = 0;
			Material material;
			std::vector<uint32_t> indices;
		};
		Vec3 center;
		float radius = 0;
		Vec3 low, high; // bounds
		// Umbra's write model: the solid triangles with an opaque, unmasked material, 3 vertices each.
		std::vector<Vec3> occluders;
		std::vector<Vertex> vertices; // empty once a ZoneScene has moved them into its StaticVertices
		size_t first_vertex = 0, vertex_count = 0; // where they went
		std::vector<Batch> batches;
		bool unlit = false; // full bright (fire): no dynamic lights
	};

	struct ZoneLighting
	{
		float ambient = 0.6f; // added to every vertex, 0..1
		bool point_lights = true; // add the lights.wld lights to the vertex colors
	};

	class ZoneScene
	{
	public:
		bool Load(const std::string &eq_dir, const std::string &zone, std::string &error, const ZoneLighting &lighting = ZoneLighting());
		// `lights`: the scene lights given to the renderer (SetSceneLights), which light the meshes
		// they reach, as the sun does when `sun`.
		// With occlusion culling on, only what the zone's meshes don't hide (OcclusionBuffer).
		void Draw(Renderer &renderer, uint64_t now_ms, const std::vector<PointLight> &lights = {}, bool sun = false);
		void SetOcclusionCulling(bool on) { m_occlusion_culling = on; }
		// After Draw: whether a sphere could be seen past the zone (spawns); always, with culling off.
		bool InView(const Vec3 &center, float radius) const;
		// The last Draw's meshes in the view and those of them the occlusion culling hid.
		size_t MeshesInView() const { return m_meshes_in_view; }
		size_t MeshesHidden() const { return m_meshes_hidden; }
		// Highest solid surface at or below `from` (searching down to `from.z - depth`), and how
		// exposed it is to the ambient light (its vertices' alpha, 1 without colors).
		bool FloorBelow(const Vec3 &from, float depth, float &floor_z, float *exposure = nullptr) const;
		// The first solid surface on the segment from `from` to `to`, as a fraction of its length,
		// and that surface's unit normal (facing `from`).
		bool SegmentHit(const Vec3 &from, const Vec3 &to, float &fraction, Vec3 *normal = nullptr) const;

		size_t TriangleCount() const;
		const std::vector<Vec3> &SolidTriangles() const { return m_collision; } // 3 vertices each
		size_t ObjectCount() const { return m_object_count; }
		const ZoneRegions &Regions() const { return m_regions; } // water, lava, zone lines
		// lights.wld's lights, which are baked into the zone's colors but light characters as
		// they go (createPointLightFromFileContext gives their flag 0x100 a scene influence), and
		// the regions each reaches.
		const std::vector<PointLight> &StaticLights() const { return m_static_lights; }
		const std::vector<std::vector<int>> &StaticLightRegions() const { return m_static_light_regions; }
		// The world's constant ambient (0x35), added to what has no baked colors; 0 in most zones.
		const Vec3 &ConstantAmbient() const { return m_constant_ambient; }
		const std::vector<std::string> &Warnings() const { return m_warnings; }

	private:
		void AddMesh(const WldMesh &mesh, const Transform &transform, bool solid, const std::vector<WldLight> &lights,
			const ZoneLighting &lighting);
		void BuildGrid();
		// The solid triangles (indices into m_collision / 3) a top-down box touches, in order.
		void Candidates(float x0, float y0, float x1, float y1, std::vector<uint32_t> &out) const;

		PfsArchive m_zone_archive, m_object_archive;
		TextureCache m_textures;
		std::vector<StaticMesh> m_meshes;
		bool m_occlusion_culling = true;
		OcclusionBuffer m_occlusion;
		bool m_occlusion_ready = false; // m_occlusion holds the last Draw's view
		std::vector<std::pair<float, uint32_t>> m_order; // meshes in view, nearest first
		std::vector<uint8_t> m_hidden; // per mesh, for the last Draw
		size_t m_meshes_in_view = 0, m_meshes_hidden = 0;
		StaticVertices m_vertices; // every mesh's, once loaded
		ZoneRegions m_regions;
		std::vector<PointLight> m_static_lights;
		std::vector<std::vector<int>> m_static_light_regions;
		Vec3 m_constant_ambient;
		std::vector<Vec3> m_collision; // solid triangles, 3 vertices each
		std::vector<float> m_collision_exposure; // each one's, s3dGetActorScaledAmbient's
		// m_collision's triangles listed by the top-down grid cells their bounds touch, so the
		// floor and wall tests look at the triangles nearby instead of the whole zone.
		float m_cell_size = 16;
		float m_grid_x = 0, m_grid_y = 0; // the grid's low corner
		int m_grid_w = 0, m_grid_h = 0;
		std::vector<uint32_t> m_cell_start; // per cell, into m_cell_triangles; one more at the end
		std::vector<uint32_t> m_cell_triangles;
		size_t m_object_count = 0;
		std::vector<std::string> m_warnings;
	};

	// pool: where the mesh's vertices are, when they've moved to a StaticVertices.
	void DrawStaticMesh(Renderer &renderer, const StaticMesh &mesh, uint64_t now_ms, const StaticVertices *pool = nullptr);
}
