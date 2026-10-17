#pragma once

// A zone ready to draw: the region meshes of <zone>.s3d plus the objects that
// objects.wld places from <zone>_obj.s3d, all transformed into world space and
// lit once at load (lights.wld point lights over a flat ambient).

#include "rasterizer.h"
#include "textures.h"

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
		std::vector<Vertex> vertices;
		std::vector<Batch> batches;
	};

	struct ZoneLighting
	{
		float ambient = 0.6f; // added to every vertex, 0..1
	};

	class ZoneScene
	{
	public:
		bool Load(const std::string &eq_dir, const std::string &zone, std::string &error, const ZoneLighting &lighting = ZoneLighting());
		void Draw(Rasterizer &rasterizer, uint64_t now_ms) const;
		// Highest solid surface at or below `from` (searching down to `from.z - depth`).
		bool FloorBelow(const Vec3 &from, float depth, float &floor_z) const;

		size_t TriangleCount() const;
		size_t ObjectCount() const { return m_object_count; }
		const std::vector<std::string> &Warnings() const { return m_warnings; }

	private:
		void AddMesh(const WldMesh &mesh, const Transform &transform, bool solid, const std::vector<WldLight> &lights,
			const ZoneLighting &lighting);

		PfsArchive m_zone_archive, m_object_archive;
		TextureCache m_textures;
		std::vector<StaticMesh> m_meshes;
		std::vector<Vec3> m_collision; // solid triangles, 3 vertices each
		size_t m_object_count = 0;
		std::vector<std::string> m_warnings;
	};

	void DrawStaticMesh(Rasterizer &rasterizer, const StaticMesh &mesh, uint64_t now_ms);
}
