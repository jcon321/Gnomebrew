#pragma once

// A software rasterizer for the 3D view, drawing into the same 32-bit
// 0x00RRGGBB surface the front end uses. It stands in for eqgfx_dx8.dll's
// Direct3D 8 renderer: depth-buffered, perspective-correct textured triangles
// with mipmaps, masked and blended materials, vertex lighting and linear fog.
//
// Draw() transforms, clips (near plane) and sets up triangles; Finish()
// rasterizes them in horizontal strips spread over worker threads.

#include "math3d.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace render
{
	// A power-of-two texture with its mip chain; pixels are 0xAARRGGBB.
	struct Texture
	{
		int width = 0, height = 0; // level 0
		int width_shift = 0, height_shift = 0;
		std::vector<std::vector<uint32_t>> levels;
		bool has_alpha = false;
	};

	enum class Blend : uint8_t
	{
		Opaque,
		Masked,      // texels with alpha < 128 are skipped
		Translucent, // blended at Material::opacity, drawn after opaque, back to front
		Additive,
	};

	struct Material
	{
		const Texture *texture = nullptr; // nullptr: vertex color only
		Blend blend = Blend::Opaque;
		uint8_t opacity = 255;
		bool two_sided = false;
	};

	struct Vertex
	{
		Vec3 position; // world space
		float u = 0, v = 0;
		uint32_t color = 0xffffff; // 0x00RRGGBB light; 0xffffff draws the texture as is
	};

	struct Camera
	{
		Vec3 position;
		Vec3 forward{1, 0, 0};
		Vec3 up{0, 0, 1};
		float horizontal_fov = Pi / 2; // full angle, radians
		float near_plane = 0.25f;
		float far_plane = 5000;
		bool fog = false;
		float fog_start = 0, fog_end = 1;
		uint32_t fog_color = 0;
	};

	class Rasterizer
	{
	public:
		Rasterizer();
		~Rasterizer();
		Rasterizer(const Rasterizer &) = delete;
		Rasterizer &operator=(const Rasterizer &) = delete;

		void Begin(int width, int height, const Camera &camera);
		// Between these, Draw adds background (the sky): positions are relative to the
		// camera, and the triangles fill only pixels the solid triangles left empty, in
		// draw order, unfogged, before the translucent triangles blend over them. Hidden
		// sky costs nothing this way.
		void BeginBackground() { m_background = true; }
		void EndBackground() { m_background = false; }
		bool SphereVisible(const Vec3 &center, float radius) const;
		void Draw(const Vertex *vertices, size_t vertex_count, const uint32_t *indices, size_t index_count,
			const Material &material);
		// Clears to clear_color (unless clear is false: drawing over an earlier pass, such
		// as the sky) and rasterizes everything drawn since Begin.
		void Finish(uint32_t *pixels, int stride, uint32_t clear_color, bool clear = true);

		// Screen position of a world point under the camera given to the last Begin.
		bool Project(const Vec3 &point, float &x, float &y) const;

		size_t TriangleCount() const { return m_triangles.size(); }

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

		Vec3 Eye() const { return m_background ? Vec3{0, 0, 0} : m_camera.position; }
		void Setup(const ViewVertex &a, const ViewVertex &b, const ViewVertex &c, const Material &material);
		void RasterizeStrip(int strip, uint32_t *pixels, int stride, uint32_t clear_color, bool clear);

		int m_width = 0, m_height = 0;
		Camera m_camera;
		Vec3 m_right, m_up, m_forward;
		float m_focal = 1, m_tan_x = 1, m_tan_y = 1;
		float m_fog_scale = 0;
		bool m_background = false;

		std::vector<ViewVertex> m_view;
		std::vector<ScreenTriangle> m_triangles;
		std::vector<uint32_t> m_order; // opaque in draw order, then translucent back to front
		std::vector<std::vector<uint32_t>> m_bins; // triangle indices per strip
		std::vector<float> m_depth;
		std::unique_ptr<Workers> m_workers;
	};
}
