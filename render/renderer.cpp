#include "renderer.h"

#include <algorithm>
#include <cmath>

namespace render
{
	void Renderer::Begin(int width, int height, const Camera &camera)
	{
		m_width = width;
		m_height = height;
		m_camera = camera;
		m_forward = Normalize(camera.forward);
		m_right = Normalize(Cross(m_forward, camera.up));
		m_up = Cross(m_right, m_forward);
		m_tan_x = std::tan(camera.horizontal_fov * 0.5f);
		m_tan_y = m_tan_x * height / width;
		m_focal = (width * 0.5f) / m_tan_x;
		m_background = false;
		m_light_scale = {1, 1, 1};
		m_light_add = {0, 0, 0};
	}

	bool Renderer::SphereVisible(const Vec3 &center, float radius) const
	{
		Vec3 d = center - Eye();
		float x = Dot(d, m_right), y = Dot(d, m_up), z = Dot(d, m_forward);
		if (z + radius < m_camera.near_plane || z - radius > m_camera.far_plane)
			return false;
		float nx = 1.0f / std::sqrt(1 + m_tan_x * m_tan_x), ny = 1.0f / std::sqrt(1 + m_tan_y * m_tan_y);
		if ((z * m_tan_x - x) * nx < -radius || (z * m_tan_x + x) * nx < -radius)
			return false;
		if ((z * m_tan_y - y) * ny < -radius || (z * m_tan_y + y) * ny < -radius)
			return false;
		return true;
	}

	bool Renderer::Project(const Vec3 &point, float &x, float &y) const
	{
		Vec3 d = point - m_camera.position;
		float z = Dot(d, m_forward);
		if (z < m_camera.near_plane)
			return false;
		x = m_width * 0.5f + Dot(d, m_right) / z * m_focal;
		y = m_height * 0.5f - Dot(d, m_up) / z * m_focal;
		return true;
	}

	void Renderer::LightVertex(uint32_t color, float &r, float &g, float &b) const
	{
		float exposure = static_cast<float>(color >> 24) * (1.0f / 255);
		r = std::min(255.0f, static_cast<float>((color >> 16) & 0xff) * m_light_scale.x + m_light_add.x * exposure);
		g = std::min(255.0f, static_cast<float>((color >> 8) & 0xff) * m_light_scale.y + m_light_add.y * exposure);
		b = std::min(255.0f, static_cast<float>(color & 0xff) * m_light_scale.z + m_light_add.z * exposure);
	}

	void DrawLine(uint32_t *pixels, int stride, int width, int height, float x0, float y0, float x1, float y1, uint32_t color)
	{
		// Liang-Barsky against the surface, then one pixel a step along the longer axis.
		float t0 = 0, t1 = 1, dx = x1 - x0, dy = y1 - y0;
		const float p[4] = {-dx, dx, -dy, dy};
		const float q[4] = {x0, width - 1 - x0, y0, height - 1 - y0};
		for (int i = 0; i < 4; ++i) {
			if (p[i] == 0) {
				if (q[i] < 0)
					return;
				continue;
			}
			float t = q[i] / p[i];
			if (p[i] < 0)
				t0 = std::max(t0, t);
			else
				t1 = std::min(t1, t);
			if (t0 > t1)
				return;
		}
		float ax = x0 + t0 * dx, ay = y0 + t0 * dy, bx = x0 + t1 * dx, by = y0 + t1 * dy;
		int steps = static_cast<int>(std::max(std::fabs(bx - ax), std::fabs(by - ay)));
		float sx = steps ? (bx - ax) / steps : 0, sy = steps ? (by - ay) / steps : 0;
		for (int i = 0; i <= steps; ++i, ax += sx, ay += sy) {
			int px = static_cast<int>(ax + 0.5f), py = static_cast<int>(ay + 0.5f);
			if (px >= 0 && py >= 0 && px < width && py < height)
				pixels[static_cast<size_t>(py) * stride + px] = color;
		}
	}
}
