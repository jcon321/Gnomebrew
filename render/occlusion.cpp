#include "occlusion.h"

#include <algorithm>
#include <cmath>

namespace render
{
	void OcclusionBuffer::Begin(const Camera &camera, float aspect)
	{
		// The renderer's view (Renderer::Begin), at Width pixels across.
		m_height = std::max(1, static_cast<int>(std::lround(Width / std::max(aspect, 0.01f))));
		m_eye = camera.position;
		m_forward = Normalize(camera.forward);
		m_right = Normalize(Cross(m_forward, camera.up));
		m_up = Cross(m_right, m_forward);
		m_focal = (Width * 0.5f) / std::tan(camera.horizontal_fov * 0.5f);
		m_near = camera.near_plane;
		m_depth.assign(static_cast<size_t>(Width) * m_height, 0.0f);
	}

	OcclusionBuffer::ViewPoint OcclusionBuffer::ToView(const Vec3 &p) const
	{
		Vec3 d = p - m_eye;
		return {Dot(d, m_right), Dot(d, m_up), Dot(d, m_forward)};
	}

	bool OcclusionBuffer::BoxVisible(const Vec3 &low, const Vec3 &high) const
	{
		if (m_depth.empty())
			return true;
		float min_x = 1e30f, min_y = 1e30f, max_x = -1e30f, max_y = -1e30f, near_z = 1e30f;
		for (int i = 0; i < 8; ++i) {
			ViewPoint v = ToView({i & 1 ? high.x : low.x, i & 2 ? high.y : low.y, i & 4 ? high.z : low.z});
			if (v.z < m_near)
				return true; // reaches the camera
			float sx = Width * 0.5f + v.x / v.z * m_focal, sy = m_height * 0.5f - v.y / v.z * m_focal;
			min_x = std::min(min_x, sx);
			max_x = std::max(max_x, sx);
			min_y = std::min(min_y, sy);
			max_y = std::max(max_y, sy);
			near_z = std::min(near_z, v.z);
		}
		// Every pixel the box's outline touches must have an occluder nearer than its nearest corner.
		int x0 = std::max(0, static_cast<int>(std::floor(min_x))), x1 = std::min(Width, static_cast<int>(std::ceil(max_x)));
		int y0 = std::max(0, static_cast<int>(std::floor(min_y))), y1 = std::min(m_height, static_cast<int>(std::ceil(max_y)));
		float nearest = 1.0f / near_z;
		for (int y = y0; y < y1; ++y) {
			const float *row = m_depth.data() + static_cast<size_t>(y) * Width;
			for (int x = x0; x < x1; ++x)
				if (row[x] <= nearest)
					return true;
		}
		return false;
	}

	void OcclusionBuffer::Write(const Vec3 *vertices, size_t triangle_count)
	{
		for (size_t t = 0; t < triangle_count; ++t) {
			ViewPoint in[3] = {ToView(vertices[t * 3]), ToView(vertices[t * 3 + 1]), ToView(vertices[t * 3 + 2])};
			if (in[0].z >= m_near && in[1].z >= m_near && in[2].z >= m_near) {
				Rasterize(in[0], in[1], in[2]);
				continue;
			}
			// Clipped to the near plane: up to a quad.
			ViewPoint poly[4];
			int count = 0;
			for (int i = 0; i < 3; ++i) {
				const ViewPoint &a = in[i], &b = in[(i + 1) % 3];
				if (a.z >= m_near)
					poly[count++] = a;
				if ((a.z >= m_near) != (b.z >= m_near)) {
					float s = (m_near - a.z) / (b.z - a.z);
					poly[count++] = {a.x + (b.x - a.x) * s, a.y + (b.y - a.y) * s, m_near};
				}
			}
			for (int i = 1; i + 1 < count; ++i)
				Rasterize(poly[0], poly[i], poly[i + 1]);
		}
	}

	void OcclusionBuffer::Rasterize(const ViewPoint &a, const ViewPoint &b, const ViewPoint &c)
	{
		float sx[3], sy[3], iz[3];
		const ViewPoint *in[3] = {&a, &b, &c};
		for (int i = 0; i < 3; ++i) {
			iz[i] = 1.0f / in[i]->z;
			sx[i] = Width * 0.5f + in[i]->x * iz[i] * m_focal;
			sy[i] = m_height * 0.5f - in[i]->y * iz[i] * m_focal;
		}
		// Front faces only (the rasterizer's clockwise), as only they hide what's behind them.
		float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
		if (!(area > 0) || !(area < 1e12f))
			return;
		int x0 = std::max(0, static_cast<int>(std::ceil(std::min({sx[0], sx[1], sx[2]}) - 0.5f)));
		int x1 = std::min(Width, static_cast<int>(std::ceil(std::max({sx[0], sx[1], sx[2]}) - 0.5f)));
		int y0 = std::max(0, static_cast<int>(std::ceil(std::min({sy[0], sy[1], sy[2]}) - 0.5f)));
		int y1 = std::min(m_height, static_cast<int>(std::ceil(std::max({sy[0], sy[1], sy[2]}) - 0.5f)));
		if (x0 >= x1 || y0 >= y1)
			return;

		// Edge functions at pixel centers, each opposite a vertex; 1/z is linear in them.
		struct Edge
		{
			float dx, dy, start;
		} edges[3];
		for (int i = 0; i < 3; ++i) {
			int j = (i + 1) % 3, k = (i + 2) % 3;
			edges[i].dx = -(sy[k] - sy[j]);
			edges[i].dy = sx[k] - sx[j];
			edges[i].start = (sx[k] - sx[j]) * (y0 + 0.5f - sy[j]) - (sy[k] - sy[j]) * (x0 + 0.5f - sx[j]);
		}
		float inv = 1.0f / area;
		float dz_dx = (edges[0].dx * iz[0] + edges[1].dx * iz[1] + edges[2].dx * iz[2]) * inv;
		float dz_dy = (edges[0].dy * iz[0] + edges[1].dy * iz[1] + edges[2].dy * iz[2]) * inv;
		float z_start = (edges[0].start * iz[0] + edges[1].start * iz[1] + edges[2].start * iz[2]) * inv;
		for (int y = y0; y < y1; ++y) {
			int row = y - y0;
			float e[3];
			// The row's span: where each edge function, linear in x, is >= 0.
			float lo = static_cast<float>(x0), hi = static_cast<float>(x1);
			for (int i = 0; i < 3; ++i) {
				e[i] = edges[i].start + edges[i].dy * row;
				if (edges[i].dx > 0)
					lo = std::max(lo, x0 + std::ceil(-e[i] / edges[i].dx));
				else if (edges[i].dx < 0)
					hi = std::min(hi, x0 + std::floor(e[i] / -edges[i].dx) + 1);
				else if (e[i] < 0)
					hi = lo;
			}
			if (lo >= hi)
				continue;
			int from = static_cast<int>(lo), to = static_cast<int>(hi);
			float step = static_cast<float>(from - x0);
			float e0 = e[0] + edges[0].dx * step, e1 = e[1] + edges[1].dx * step, e2 = e[2] + edges[2].dx * step;
			float z = z_start + dz_dy * row + dz_dx * step;
			float *out = m_depth.data() + static_cast<size_t>(y) * Width;
			for (int x = from; x < to; ++x, e0 += edges[0].dx, e1 += edges[1].dx, e2 += edges[2].dx, z += dz_dx)
				if (e0 >= 0 && e1 >= 0 && e2 >= 0 && z > out[x])
					out[x] = z;
		}
	}
}
