#pragma once

// What the 3D view draws through: the software Rasterizer (portable, used by the
// Linux preview tools and as the Windows fallback) or the Windows build's
// Direct3D 11 renderer (frontend/win32_d3d11.cpp). Both take world-space
// triangles a batch at a time between Begin and Finish and share the camera math.

#include "math3d.h"
#include "point_lights.h"

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
		// The GPU copy, made by the Direct3D renderer the first time it draws the texture
		// and released with it.
		mutable std::shared_ptr<void> device_texture;
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
		bool fog = true; // false: drawn clear of the fog (eqgfx's particle points and lines)
		// Translucent and additive only: the texture's alpha blends too, as eqgfx's particle system
		// draws (SRCALPHA over INVSRCALPHA, or ONE for additive), and texels whose alpha comes to
		// under 8 are dropped (its alpha test).
		bool texel_alpha = false;
	};

	struct Vertex
	{
		Vec3 position; // world space
		float u = 0, v = 0;
		// 0xAARRGGBB: RRGGBB is the light (0xffffff draws the texture as is), AA how much of the
		// light set with SetVertexLight's `add` reaches it (a zone vertex's exposure to the ambient light).
		uint32_t color = 0xffffff;
		Vec3 normal; // for the sun and the carried lights (SetDynamicLights)
	};

	// Vertices that never change once made, such as all of a zone's meshes in one array: a GPU
	// renderer copies them to the GPU once (into device_buffer, released with them) and draws
	// from there.
	struct StaticVertices
	{
		std::vector<Vertex> vertices;
		mutable std::shared_ptr<void> device_buffer;
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

	// A one-pixel line clipped to a width x height surface (debug overlays).
	void DrawLine(uint32_t *pixels, int stride, int width, int height, float x0, float y0, float x1, float y1, uint32_t color);

	class Renderer
	{
	public:
		virtual ~Renderer() = default;
		virtual const char *Name() const = 0;

		virtual void Begin(int width, int height, const Camera &camera);
		// Between these, Draw adds background (the sky): positions are relative to the
		// camera, and the triangles fill only pixels the solid triangles left empty, in
		// draw order, unfogged, before the translucent triangles blend over them. Hidden
		// sky costs nothing this way.
		void BeginBackground() { m_background = true; }
		void EndBackground() { m_background = false; }
		bool SphereVisible(const Vec3 &center, float radius) const;
		// Light applied to the vertex colors of the triangles drawn after it, channel by channel:
		// color * scale + add * AA / 255 + base (0..1). Begin resets it to (1, 0, 0): the colors as
		// given. The time of day uses it to add the ambient light to a zone's baked lighting (as
		// eqgfx's calculateMeshLighting does: vertex color + vertex alpha * ambient), and the
		// world's special ambient (infravision, ultravision) as base; and to dim the sky.
		void SetVertexLight(const Vec3 &scale, const Vec3 &add, const Vec3 &base = {})
		{
			m_light_scale = scale;
			m_light_add = add * 255.0f;
			m_light_base = base * 255.0f;
		}
		// The lights that change as the game runs, once a frame: the sun (a directional light,
		// shining along sun_direction, colored with the time of day) and the lights spawns carry
		// (SceneLightLimit at most).
		static constexpr size_t SceneLightLimit = 8;
		void SetSceneLights(const Vec3 &sun_direction, const Vec3 &sun_color, const std::vector<PointLight> &points);
		// Which of them light the vertices drawn after it, added to SetVertexLight's light as
		// calculateMeshLighting adds them: the sun, color x the cosine x AA (like the ambient), and
		// the scene lights whose bits are set in point_mask, Strength x the cosine / distance^2.
		// Begin turns them off.
		void SetDynamicLights(bool sun, uint32_t point_mask)
		{
			m_dynamic_sun = sun;
			m_point_mask = point_mask & ((1u << m_point_count) - 1);
			++m_light_generation;
		}
		// reuse_vertices: the same vertex array as the previous Draw, unchanged since (the next
		// batch of a mesh), so the renderer can keep what it made of it instead of taking it again.
		virtual void Draw(const Vertex *vertices, size_t vertex_count, const uint32_t *indices, size_t index_count,
			const Material &material, bool reuse_vertices = false) = 0;
		// Draw for the vertex_count static vertices from first_vertex on (the indices count from there).
		virtual void DrawStatic(const StaticVertices &pool, size_t first_vertex, size_t vertex_count, const uint32_t *indices,
			size_t index_count, const Material &material, bool reuse_vertices = false)
		{
			Draw(pool.vertices.data() + first_vertex, vertex_count, indices, index_count, material, reuse_vertices);
		}
		// Draws everything since Begin into the width x height block of canvas pixels at
		// `pixels`, cleared to clear_color first (unless clear is false: drawing over an
		// earlier pass, such as the sky).
		virtual void Finish(uint32_t *pixels, int stride, uint32_t clear_color, bool clear = true) = 0;

		// The view given to the last Begin.
		const Camera &CurrentCamera() const { return m_camera; }
		int Width() const { return m_width; }
		int Height() const { return m_height; }
		// Screen position of a world point under the camera given to the last Begin.
		bool Project(const Vec3 &point, float &x, float &y) const;
		// The same, with how far in front of the camera the point is.
		bool Project(const Vec3 &point, float &x, float &y, float &depth) const;
		// Pixels a unit at depth 1 covers on the screen.
		float Focal() const { return m_focal; }
		// The unit direction from the camera through a screen position (Project's inverse).
		Vec3 ViewRay(float x, float y) const
		{
			return Normalize(m_forward + m_right * ((x - m_width * 0.5f) / m_focal) - m_up * ((y - m_height * 0.5f) / m_focal));
		}
		// After Finish: whether something drawn is in front of the point (name tags behind walls).
		virtual bool Occluded(const Vec3 &point) const = 0;

		virtual size_t TriangleCount() const = 0;
		// After Finish: the outline of every solid triangle drawn (the debug wireframe).
		virtual void DrawEdges(uint32_t *pixels, int stride, uint32_t color) const = 0;

	protected:
		Vec3 Eye() const { return m_background ? Vec3{0, 0, 0} : m_camera.position; }
		// The lit 0..255 color of a vertex under SetVertexLight and SetDynamicLights.
		void LightVertex(const Vertex &vertex, float &r, float &g, float &b) const;

		int m_width = 0, m_height = 0;
		Camera m_camera;
		Vec3 m_right, m_up, m_forward;
		float m_focal = 1, m_tan_x = 1, m_tan_y = 1;
		bool m_background = false;
		Vec3 m_light_scale{1, 1, 1}, m_light_add{0, 0, 0}, m_light_base{0, 0, 0};
		Vec3 m_sun_direction{0, 0, -1}, m_sun_color;
		PointLight m_points[SceneLightLimit];
		size_t m_point_count = 0;
		bool m_dynamic_sun = false;
		uint32_t m_point_mask = 0;
		uint32_t m_light_generation = 0; // changes with the dynamic lights
	};
}
