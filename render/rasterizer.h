#pragma once

// A software rasterizer for the 3D view, drawing into the same 32-bit
// 0x00RRGGBB surface the front end uses. It stands in for eqgfx_dx8.dll's
// Direct3D 8 renderer: depth-buffered, perspective-correct textured triangles
// with mipmaps, masked and blended materials, vertex lighting and linear fog.
//
// Draw() transforms, clips (near plane) and sets up triangles; Finish()
// rasterizes them in horizontal strips spread over worker threads.

#include "renderer.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace render
{
	class Rasterizer : public Renderer
	{
	public:
		Rasterizer();
		~Rasterizer() override;
		Rasterizer(const Rasterizer &) = delete;
		Rasterizer &operator=(const Rasterizer &) = delete;

		const char *Name() const override { return "software"; }
		void Begin(int width, int height, const Camera &camera) override;
		void Draw(const Vertex *vertices, size_t vertex_count, const uint32_t *indices, size_t index_count,
			const Material &material, bool reuse_vertices = false) override;
		void Finish(uint32_t *pixels, int stride, uint32_t clear_color, bool clear = true) override;
		bool Occluded(const Vec3 &point) const override;
		size_t TriangleCount() const override { return m_triangles.size(); }
		void DrawEdges(uint32_t *pixels, int stride, uint32_t color) const override;

	private:
		struct ViewVertex
		{
			float x, y, z; // view space: x right, y up, z forward
			float u, v;
			float r, g, b;
		};
		static constexpr int Attributes = 7; // 1/z, u/z, v/z, r, g, b, fog
		struct ScreenTriangle
		{
			float x[3], y[3]; // sorted by y
			float ref_x, ref_y;
			float a[Attributes], dx[Attributes], dy[Attributes]; // plane equations, relative to ref
			const Texture *texture;
			Blend blend;
			uint8_t opacity;
			int row_start, row_end;
			float depth; // farthest view depth, for sorting translucent triangles
			bool background;
		};
		class Workers;

		void Setup(const ViewVertex &a, const ViewVertex &b, const ViewVertex &c, const Material &material);
		void RasterizeStrip(int strip, uint32_t *pixels, int stride, uint32_t clear_color, bool clear);

		float m_fog_scale = 0;

		std::vector<ViewVertex> m_view;
		// What m_view was made from: reused by the next batch while the vertices and light are the same.
		const Vertex *m_view_source = nullptr;
		Vec3 m_view_scale, m_view_add;
		bool m_view_background = false;
		std::vector<ScreenTriangle> m_triangles;
		std::vector<uint32_t> m_order; // opaque in draw order, then translucent back to front
		std::vector<std::vector<uint32_t>> m_bins; // triangle indices per strip
		std::vector<float> m_depth;
		std::unique_ptr<Workers> m_workers;
	};
}
