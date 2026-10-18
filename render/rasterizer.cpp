#include "rasterizer.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace render
{
	namespace
	{
		constexpr int StripHeight = 16;

		// Which screen winding (y down) faces the camera. WLD polygons are wound
		// so that front faces come out clockwise.
		constexpr bool FrontIsClockwise = true;
	}

	// A small pool: Run(n, job) calls job(0..n-1) across the workers and the caller.
	class Rasterizer::Workers
	{
	public:
		Workers()
		{
			unsigned count = std::thread::hardware_concurrency();
			count = count == 0 ? 2 : std::min(count, 8u);
			for (unsigned i = 1; i < count; ++i)
				m_threads.emplace_back([this] { Loop(); });
		}

		~Workers()
		{
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_stop = true;
			}
			m_wake.notify_all();
			for (auto &thread : m_threads)
				thread.join();
		}

		void Run(int count, const std::function<void(int)> &job)
		{
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_job = &job;
				m_count = count;
				m_next = 0;
				m_busy = static_cast<int>(m_threads.size());
				++m_generation;
			}
			m_wake.notify_all();
			Work(job, count);
			std::unique_lock<std::mutex> lock(m_mutex);
			m_done.wait(lock, [this] { return m_busy == 0; });
			m_job = nullptr;
		}

	private:
		void Work(const std::function<void(int)> &job, int count)
		{
			for (int i = m_next++; i < count; i = m_next++)
				job(i);
		}

		void Loop()
		{
			uint64_t seen = 0;
			for (;;) {
				const std::function<void(int)> *job;
				int count;
				{
					std::unique_lock<std::mutex> lock(m_mutex);
					m_wake.wait(lock, [&] { return m_stop || m_generation != seen; });
					if (m_stop)
						return;
					seen = m_generation;
					job = m_job;
					count = m_count;
				}
				Work(*job, count);
				{
					std::lock_guard<std::mutex> lock(m_mutex);
					if (--m_busy == 0)
						m_done.notify_one();
				}
			}
		}

		std::vector<std::thread> m_threads;
		std::mutex m_mutex;
		std::condition_variable m_wake, m_done;
		const std::function<void(int)> *m_job = nullptr;
		int m_count = 0;
		std::atomic<int> m_next{0};
		int m_busy = 0;
		uint64_t m_generation = 0;
		bool m_stop = false;
	};

	Rasterizer::Rasterizer() : m_workers(std::make_unique<Workers>()) {}
	Rasterizer::~Rasterizer() = default;

	void Rasterizer::Begin(int width, int height, const Camera &camera)
	{
		Renderer::Begin(width, height, camera);
		m_fog_scale = camera.fog && camera.fog_end > camera.fog_start ? 1.0f / (camera.fog_end - camera.fog_start) : 0;
		m_triangles.clear();
		m_view_source = nullptr;
		m_depth.resize(static_cast<size_t>(width) * height);
		m_bins.resize((height + StripHeight - 1) / StripHeight);
	}

	bool Rasterizer::Occluded(const Vec3 &point) const
	{
		float x, y;
		if (!Project(point, x, y))
			return true;
		int px = static_cast<int>(x), py = static_cast<int>(y);
		if (px < 0 || py < 0 || px >= m_width || py >= m_height || m_depth.size() < static_cast<size_t>(m_width) * m_height)
			return false;
		float z = Dot(point - m_camera.position, m_forward);
		// The depth buffer holds 1/z; something more than 2% nearer hides the point.
		return m_depth[static_cast<size_t>(py) * m_width + px] > (1.0f / z) * 1.02f;
	}

	void Rasterizer::Draw(const Vertex *vertices, size_t vertex_count, const uint32_t *indices, size_t index_count,
		const Material &material, bool reuse_vertices)
	{
		auto same = [](const Vec3 &a, const Vec3 &b) { return a.x == b.x && a.y == b.y && a.z == b.z; };
		bool transformed = reuse_vertices && m_view_source == vertices && m_view.size() == vertex_count &&
		                   m_view_background == m_background && same(m_view_scale, m_light_scale) && same(m_view_add, m_light_add);
		m_view.resize(vertex_count);
		m_view_source = vertices;
		m_view_scale = m_light_scale;
		m_view_add = m_light_add;
		m_view_background = m_background;
		Vec3 eye = Eye();
		for (size_t i = 0; i < vertex_count && !transformed; ++i) {
			const Vertex &in = vertices[i];
			Vec3 d = in.position - eye;
			ViewVertex &out = m_view[i];
			out.x = Dot(d, m_right);
			out.y = Dot(d, m_up);
			out.z = Dot(d, m_forward);
			out.u = in.u;
			out.v = in.v;
			LightVertex(in.color, out.r, out.g, out.b);
		}

		const float near_z = m_camera.near_plane, far_z = m_camera.far_plane;
		for (size_t t = 0; t + 2 < index_count; t += 3) {
			if (indices[t] >= vertex_count || indices[t + 1] >= vertex_count || indices[t + 2] >= vertex_count)
				continue;
			const ViewVertex *v[3] = {&m_view[indices[t]], &m_view[indices[t + 1]], &m_view[indices[t + 2]]};

			// Trivial rejects against the frustum planes.
			if (v[0]->z < near_z && v[1]->z < near_z && v[2]->z < near_z)
				continue;
			if (v[0]->z > far_z && v[1]->z > far_z && v[2]->z > far_z)
				continue;
			auto outside = [&](auto test) { return test(*v[0]) && test(*v[1]) && test(*v[2]); };
			if (outside([&](const ViewVertex &p) { return p.x > p.z * m_tan_x; }) ||
			    outside([&](const ViewVertex &p) { return -p.x > p.z * m_tan_x; }) ||
			    outside([&](const ViewVertex &p) { return p.y > p.z * m_tan_y; }) ||
			    outside([&](const ViewVertex &p) { return -p.y > p.z * m_tan_y; }))
				continue;

			if (v[0]->z >= near_z && v[1]->z >= near_z && v[2]->z >= near_z) {
				Setup(*v[0], *v[1], *v[2], material);
				continue;
			}

			// Clip against the near plane: a triangle becomes one or two.
			ViewVertex poly[4];
			int count = 0;
			for (int i = 0; i < 3; ++i) {
				const ViewVertex &a = *v[i], &b = *v[(i + 1) % 3];
				bool a_in = a.z >= near_z, b_in = b.z >= near_z;
				if (a_in)
					poly[count++] = a;
				if (a_in != b_in) {
					float s = (near_z - a.z) / (b.z - a.z);
					ViewVertex &m = poly[count++];
					m.x = a.x + (b.x - a.x) * s;
					m.y = a.y + (b.y - a.y) * s;
					m.z = near_z;
					m.u = a.u + (b.u - a.u) * s;
					m.v = a.v + (b.v - a.v) * s;
					m.r = a.r + (b.r - a.r) * s;
					m.g = a.g + (b.g - a.g) * s;
					m.b = a.b + (b.b - a.b) * s;
				}
			}
			for (int i = 1; i + 1 < count; ++i)
				Setup(poly[0], poly[i], poly[i + 1], material);
		}
	}

	void Rasterizer::Setup(const ViewVertex &a, const ViewVertex &b, const ViewVertex &c, const Material &material)
	{
		const ViewVertex *in[3] = {&a, &b, &c};
		float sx[3], sy[3], attr[3][Attributes];
		float half_w = m_width * 0.5f, half_h = m_height * 0.5f;
		float tex_w = material.texture ? static_cast<float>(material.texture->width) : 1;
		float tex_h = material.texture ? static_cast<float>(material.texture->height) : 1;

		// Texture coordinates in level-0 texels, shifted by whole repeats so the
		// first vertex is in the first tile (keeps the per-pixel values small).
		float base_u = std::floor(a.u) * tex_w, base_v = std::floor(a.v) * tex_h;
		float far_depth = 0;
		for (int i = 0; i < 3; ++i) {
			float iw = 1.0f / in[i]->z;
			sx[i] = half_w + in[i]->x * iw * m_focal;
			sy[i] = half_h - in[i]->y * iw * m_focal;
			attr[i][0] = iw;
			attr[i][1] = (in[i]->u * tex_w - base_u) * iw;
			attr[i][2] = (in[i]->v * tex_h - base_v) * iw;
			attr[i][3] = in[i]->r;
			attr[i][4] = in[i]->g;
			attr[i][5] = in[i]->b;
			attr[i][6] = m_fog_scale > 0 && !m_background ? std::clamp((in[i]->z - m_camera.fog_start) * m_fog_scale, 0.0f, 1.0f) : 0;
			far_depth = std::max(far_depth, in[i]->z);
		}

		float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
		if (area == 0 || !(std::fabs(area) < 1e12f))
			return;
		if (!material.two_sided && ((area > 0) != FrontIsClockwise))
			return;

		float min_y = std::min({sy[0], sy[1], sy[2]}), max_y = std::max({sy[0], sy[1], sy[2]});
		float min_x = std::min({sx[0], sx[1], sx[2]}), max_x = std::max({sx[0], sx[1], sx[2]});
		int row_start = std::max(0, static_cast<int>(std::ceil(min_y - 0.5f)));
		int row_end = std::min(m_height, static_cast<int>(std::ceil(max_y - 0.5f)));
		if (row_start >= row_end || max_x < -0.5f || min_x > m_width + 0.5f)
			return;

		ScreenTriangle tri;
		int order[3] = {0, 1, 2};
		std::sort(order, order + 3, [&](int i, int j) { return sy[i] < sy[j]; });
		for (int i = 0; i < 3; ++i) {
			tri.x[i] = sx[order[i]];
			tri.y[i] = sy[order[i]];
		}
		tri.ref_x = sx[0];
		tri.ref_y = sy[0];
		float inv = 1.0f / area;
		float ex1 = sx[1] - sx[0], ey1 = sy[1] - sy[0], ex2 = sx[2] - sx[0], ey2 = sy[2] - sy[0];
		for (int k = 0; k < Attributes; ++k) {
			float d1 = attr[1][k] - attr[0][k], d2 = attr[2][k] - attr[0][k];
			tri.a[k] = attr[0][k];
			tri.dx[k] = (d1 * ey2 - d2 * ey1) * inv;
			tri.dy[k] = (d2 * ex1 - d1 * ex2) * inv;
		}
		tri.texture = material.texture;
		tri.blend = material.blend;
		tri.opacity = material.opacity;
		tri.row_start = row_start;
		tri.row_end = row_end;
		tri.depth = far_depth;
		tri.background = m_background;
		m_triangles.push_back(tri);
	}

	namespace
	{
		inline uint32_t Shade(uint32_t texel, int r, int g, int b)
		{
			uint32_t tr = (((texel >> 16) & 0xff) * r) >> 8;
			uint32_t tg = (((texel >> 8) & 0xff) * g) >> 8;
			uint32_t tb = ((texel & 0xff) * b) >> 8;
			return (tr << 16) | (tg << 8) | tb;
		}

		inline uint32_t Fog(uint32_t c, uint32_t fog, int f) // f 0..256
		{
			if (f <= 0)
				return c;
			int r = (c >> 16) & 0xff, g = (c >> 8) & 0xff, b = c & 0xff;
			int fr = (fog >> 16) & 0xff, fg = (fog >> 8) & 0xff, fb = fog & 0xff;
			r += ((fr - r) * f) >> 8;
			g += ((fg - g) * f) >> 8;
			b += ((fb - b) * f) >> 8;
			return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
		}

		inline uint32_t Over(uint32_t src, uint32_t dst, int alpha) // alpha 0..256
		{
			int r = (dst >> 16) & 0xff, g = (dst >> 8) & 0xff, b = dst & 0xff;
			r += ((static_cast<int>((src >> 16) & 0xff) - r) * alpha) >> 8;
			g += ((static_cast<int>((src >> 8) & 0xff) - g) * alpha) >> 8;
			b += ((static_cast<int>(src & 0xff) - b) * alpha) >> 8;
			return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
		}

		inline uint32_t Add(uint32_t src, uint32_t dst)
		{
			uint32_t r = std::min(255u, ((src >> 16) & 0xff) + ((dst >> 16) & 0xff));
			uint32_t g = std::min(255u, ((src >> 8) & 0xff) + ((dst >> 8) & 0xff));
			uint32_t b = std::min(255u, (src & 0xff) + (dst & 0xff));
			return (r << 16) | (g << 8) | b;
		}

		// Packed 0xAARRGGBB lerp, t 0..256: two channels per multiply.
		inline uint32_t Lerp(uint32_t a, uint32_t b, uint32_t t)
		{
			uint32_t rb = (((a & 0xff00ff) * (256 - t) + (b & 0xff00ff) * t) >> 8) & 0xff00ff;
			uint32_t ag = (((a >> 8) & 0xff00ff) * (256 - t) + ((b >> 8) & 0xff00ff) * t) & 0xff00ff00;
			return rb | ag;
		}

		// Bilinear sample, like D3DTEXF_LINEAR. u, v are level-0 texels plus the positive
		// bias that keeps them out of negative numbers; texel centers are at +0.5.
		inline uint32_t Sample(const uint32_t *texels, float u, float v, float scale, int width_mask, int height_mask,
			int row_shift)
		{
			int fu = static_cast<int>(u * scale) - 128; // 8.8 fixed point at this level
			int fv = static_cast<int>(v * scale) - 128;
			int x0 = (fu >> 8) & width_mask, y0 = (fv >> 8) & height_mask;
			int x1 = (x0 + 1) & width_mask, y1 = (y0 + 1) & height_mask;
			const uint32_t *row0 = texels + (y0 << row_shift), *row1 = texels + (y1 << row_shift);
			uint32_t tx = static_cast<uint32_t>(fu & 0xff), ty = static_cast<uint32_t>(fv & 0xff);
			return Lerp(Lerp(row0[x0], row0[x1], tx), Lerp(row1[x0], row1[x1], tx), ty);
		}

		struct Span
		{
			float iw, uw, vw, r, g, b, fog;
			float diw, duw, dvw, dr, dg, db, dfog;
		};

		// Filtered: bilinear (where the texture is magnified); otherwise the nearest texel
		// of the mip level, which is enough once texels are pixel-sized or smaller.
		template <Blend Mode, bool Textured, bool Filtered, bool Background>
		void DrawSpan(Span s, int count, uint32_t *pixels, float *depth, const uint32_t *texels, int level, int width_mask,
			int height_mask, int row_shift, float bias_u, float bias_v, uint32_t fog_color, int opacity)
		{
			float scale = 256.0f / static_cast<float>(1 << level);
			for (int i = 0; i < count; ++i) {
				if (Background ? depth[i] == 0 : s.iw > depth[i]) {
					uint32_t c;
					if (Textured) {
						float w = 1.0f / s.iw;
						uint32_t texel;
						if (Filtered)
							texel = Sample(texels, s.uw * w + bias_u, s.vw * w + bias_v, scale, width_mask, height_mask,
								row_shift);
						else {
							int tx = (static_cast<int>(s.uw * w + bias_u) >> level) & width_mask;
							int ty = (static_cast<int>(s.vw * w + bias_v) >> level) & height_mask;
							texel = texels[(ty << row_shift) + tx];
						}
						if (Mode == Blend::Masked && (texel >> 24) < 0x80)
							goto next;
						c = Shade(texel, static_cast<int>(s.r) + 1, static_cast<int>(s.g) + 1, static_cast<int>(s.b) + 1);
					}
					else
						c = (static_cast<uint32_t>(s.r) << 16) | (static_cast<uint32_t>(s.g) << 8) | static_cast<uint32_t>(s.b);
					c = Fog(c, fog_color, static_cast<int>(s.fog * 256));
					if (Mode == Blend::Translucent)
						pixels[i] = Over(c, pixels[i], opacity);
					else if (Mode == Blend::Additive)
						pixels[i] = Add(c, pixels[i]);
					else {
						pixels[i] = c;
						if (!Background)
							depth[i] = s.iw;
					}
				}
			next:
				s.iw += s.diw;
				s.uw += s.duw;
				s.vw += s.dvw;
				s.r += s.dr;
				s.g += s.dg;
				s.b += s.db;
				s.fog += s.dfog;
			}
		}

		using SpanFunction = void (*)(Span, int, uint32_t *, float *, const uint32_t *, int, int, int, int, float, float,
			uint32_t, int);

		template <Blend Mode, bool Background>
		SpanFunction PickSpan(bool textured, bool filtered)
		{
			if (!textured)
				return DrawSpan<Mode == Blend::Masked ? Blend::Opaque : Mode, false, false, Background>;
			return filtered ? DrawSpan<Mode, true, true, Background> : DrawSpan<Mode, true, false, Background>;
		}

		template <Blend Mode>
		SpanFunction PickSpan(bool textured, bool filtered, bool background)
		{
			return background ? PickSpan<Mode, true>(textured, filtered) : PickSpan<Mode, false>(textured, filtered);
		}

		SpanFunction PickSpan(Blend blend, bool textured, bool filtered, bool background)
		{
			switch (blend) {
			case Blend::Masked:
				return PickSpan<Blend::Masked>(textured, filtered, background);
			case Blend::Translucent:
				return PickSpan<Blend::Translucent>(textured, filtered, background);
			case Blend::Additive:
				return PickSpan<Blend::Additive>(textured, filtered, background);
			default:
				return PickSpan<Blend::Opaque>(textured, filtered, background);
			}
		}
	}

	void Rasterizer::RasterizeStrip(int strip, uint32_t *pixels, int stride, uint32_t clear_color, bool clear)
	{
		int y0 = strip * StripHeight, y1 = std::min(m_height, y0 + StripHeight);
		for (int y = y0; y < y1; ++y) {
			if (clear)
				std::fill_n(pixels + static_cast<size_t>(y) * stride, m_width, clear_color);
			std::fill_n(m_depth.data() + static_cast<size_t>(y) * m_width, m_width, 0.0f);
		}

		for (uint32_t index : m_bins[strip]) {
			const ScreenTriangle &t = m_triangles[index];
			const Texture *texture = t.texture;
			int max_level = texture ? static_cast<int>(texture->levels.size()) - 1 : 0;
			float bias_u = texture ? static_cast<float>(texture->width) * 256 : 0;
			float bias_v = texture ? static_cast<float>(texture->height) * 256 : 0;

			int row_start = std::max(y0, t.row_start), row_end = std::min(y1, t.row_end);
			for (int y = row_start; y < row_end; ++y) {
				float yc = y + 0.5f;
				// x where the long edge (0-2) and the current short edge cross this row.
				float xa = t.x[0] + (t.x[2] - t.x[0]) * ((yc - t.y[0]) / (t.y[2] - t.y[0]));
				float xb;
				if (yc < t.y[1])
					xb = t.x[0] + (t.x[1] - t.x[0]) * ((yc - t.y[0]) / (t.y[1] - t.y[0]));
				else
					xb = t.y[2] > t.y[1] ? t.x[1] + (t.x[2] - t.x[1]) * ((yc - t.y[1]) / (t.y[2] - t.y[1])) : t.x[1];
				float left = std::min(xa, xb), right = std::max(xa, xb);
				int xs = std::max(0, static_cast<int>(std::ceil(left - 0.5f)));
				int xe = std::min(m_width, static_cast<int>(std::ceil(right - 0.5f)));
				if (xs >= xe)
					continue;

				float ox = xs + 0.5f - t.ref_x, oy = yc - t.ref_y;
				Span s;
				s.iw = t.a[0] + t.dx[0] * ox + t.dy[0] * oy;
				s.uw = t.a[1] + t.dx[1] * ox + t.dy[1] * oy;
				s.vw = t.a[2] + t.dx[2] * ox + t.dy[2] * oy;
				s.r = t.a[3] + t.dx[3] * ox + t.dy[3] * oy;
				s.g = t.a[4] + t.dx[4] * ox + t.dy[4] * oy;
				s.b = t.a[5] + t.dx[5] * ox + t.dy[5] * oy;
				s.fog = t.a[6] + t.dx[6] * ox + t.dy[6] * oy;
				s.diw = t.dx[0];
				s.duw = t.dx[1];
				s.dvw = t.dx[2];
				s.dr = t.dx[3];
				s.dg = t.dx[4];
				s.db = t.dx[5];
				s.dfog = t.dx[6];
				// Keep interpolated colors inside 0..255 at both span ends.
				auto clamp_channel = [&](float &value, float &step) {
					float end = value + step * (xe - xs - 1);
					if (value < 0 || value > 255 || end < 0 || end > 255) {
						value = std::clamp(value, 0.0f, 255.0f);
						end = std::clamp(end, 0.0f, 255.0f);
						step = xe - xs > 1 ? (end - value) / (xe - xs - 1) : 0;
					}
				};
				clamp_channel(s.r, s.dr);
				clamp_channel(s.g, s.dg);
				clamp_channel(s.b, s.db);
				float fog_end = s.fog + s.dfog * (xe - xs - 1);
				if (s.fog < 0 || s.fog > 1 || fog_end < 0 || fog_end > 1) {
					s.fog = std::clamp(s.fog, 0.0f, 1.0f);
					fog_end = std::clamp(fog_end, 0.0f, 1.0f);
					s.dfog = xe - xs > 1 ? (fog_end - s.fog) / (xe - xs - 1) : 0;
				}

				const uint32_t *texels = nullptr;
				int level = 0, width_mask = 0, height_mask = 0, row_shift = 0;
				bool filtered = false;
				if (texture) {
					// Mip level from the texel footprint of a pixel at the middle of the span.
					float mx = (xs + xe) * 0.5f - t.ref_x;
					float iw = t.a[0] + t.dx[0] * mx + t.dy[0] * oy;
					if (iw > 0) {
						float w = 1.0f / iw;
						float u = (t.a[1] + t.dx[1] * mx + t.dy[1] * oy) * w;
						float v = (t.a[2] + t.dx[2] * mx + t.dy[2] * oy) * w;
						float dudx = (t.dx[1] - u * t.dx[0]) * w, dvdx = (t.dx[2] - v * t.dx[0]) * w;
						float dudy = (t.dy[1] - u * t.dy[0]) * w, dvdy = (t.dy[2] - v * t.dy[0]) * w;
						float rho = std::max(dudx * dudx + dvdx * dvdx, dudy * dudy + dvdy * dvdy);
						if (rho > 1)
							level = std::min(max_level, static_cast<int>(0.5f * std::log2(rho)));
						filtered = rho < 1;
					}
					texels = texture->levels[level].data();
					int lw = std::max(1, texture->width >> level), lh = std::max(1, texture->height >> level);
					width_mask = lw - 1;
					height_mask = lh - 1;
					row_shift = std::max(0, texture->width_shift - level);
				}
				uint32_t *row = pixels + static_cast<size_t>(y) * stride + xs;
				float *depth = m_depth.data() + static_cast<size_t>(y) * m_width + xs;
				PickSpan(t.blend, texture != nullptr, filtered, t.background)(s, xe - xs, row, depth, texels, level, width_mask, height_mask, row_shift, bias_u, bias_v,
					m_camera.fog_color, t.opacity + 1);
			}
		}
	}

	void Rasterizer::Finish(uint32_t *pixels, int stride, uint32_t clear_color, bool clear)
	{
		// Opaque and masked first in draw order, then the background in draw order, then
		// the rest back to front.
		auto solid_blend = [](Blend blend) { return blend == Blend::Opaque || blend == Blend::Masked; };
		m_order.clear();
		for (uint32_t i = 0; i < m_triangles.size(); ++i)
			if (!m_triangles[i].background && solid_blend(m_triangles[i].blend))
				m_order.push_back(i);
		for (uint32_t i = 0; i < m_triangles.size(); ++i)
			if (m_triangles[i].background)
				m_order.push_back(i);
		size_t solid = m_order.size();
		for (uint32_t i = 0; i < m_triangles.size(); ++i)
			if (!m_triangles[i].background && !solid_blend(m_triangles[i].blend))
				m_order.push_back(i);
		std::stable_sort(m_order.begin() + solid, m_order.end(),
			[&](uint32_t a, uint32_t b) { return m_triangles[a].depth > m_triangles[b].depth; });

		for (auto &bin : m_bins)
			bin.clear();
		for (uint32_t index : m_order) {
			const ScreenTriangle &t = m_triangles[index];
			for (int strip = t.row_start / StripHeight; strip <= (t.row_end - 1) / StripHeight; ++strip)
				m_bins[strip].push_back(index);
		}

		m_workers->Run(static_cast<int>(m_bins.size()),
			[&](int strip) { RasterizeStrip(strip, pixels, stride, clear_color, clear); });
	}

	void Rasterizer::DrawEdges(uint32_t *pixels, int stride, uint32_t color) const
	{
		for (const ScreenTriangle &t : m_triangles) {
			if (t.background)
				continue;
			DrawLine(pixels, stride, m_width, m_height, t.x[0], t.y[0], t.x[1], t.y[1], color);
			DrawLine(pixels, stride, m_width, m_height, t.x[1], t.y[1], t.x[2], t.y[2], color);
			DrawLine(pixels, stride, m_width, m_height, t.x[2], t.y[2], t.x[0], t.y[0], color);
		}
	}
}
