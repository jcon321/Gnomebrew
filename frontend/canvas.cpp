#include "canvas.h"

#include <algorithm>
#include <vector>

#if defined(__SSE2__)
#include <emmintrin.h>
#endif

void CanvasDirty::Resize(int width, int height)
{
	m_width = std::max(width, 0);
	m_height = std::max(height, 0);
	m_columns = (m_width + TileWidth - 1) / TileWidth;
	m_rows = (m_height + TileHeight - 1) / TileHeight;
	m_this.assign(static_cast<size_t>(m_columns) * m_rows, 1);
	m_last = m_this;
	m_cleared_frame = m_cleared_all_frame = ~0ull;
	m_retained = false;
}

void CanvasDirty::Mark(int x0, int y0, int x1, int y1)
{
	x0 = std::max(x0, 0), y0 = std::max(y0, 0);
	x1 = std::min(x1, m_width), y1 = std::min(y1, m_height);
	if (x1 <= x0 || y1 <= y0)
		return;
	for (int ty = y0 / TileHeight; ty <= (y1 - 1) / TileHeight; ++ty)
		std::fill_n(m_this.begin() + static_cast<ptrdiff_t>(ty) * m_columns + x0 / TileWidth,
			(x1 - 1) / TileWidth - x0 / TileWidth + 1, uint8_t{1});
}

void CanvasDirty::NextFrame()
{
	m_last.swap(m_this);
	std::fill(m_this.begin(), m_this.end(), uint8_t{0});
	++m_frames;
}

void CanvasDirty::ClearBlock(uint32_t *pixels, int stride, int x, int y, int w, int h)
{
	const bool same = m_cleared_frame + 1 == m_frames && m_cleared.x == x && m_cleared.y == y && m_cleared.w == w &&
	                  m_cleared.h == h;
	if (!same) {
		for (int row = 0; row < h; ++row)
			std::fill_n(pixels + static_cast<size_t>(row) * stride, w, 0xff000000u);
		Mark(x, y, x + w, y + h);
		m_cleared_all_frame = m_frames;
	}
	else if (m_retained) {
		// The front end's drawing is kept: it clears what it redraws itself.
	}
	else {
		// The tiles drawn on last frame, where they're in the block.
		for (int ty = std::max(y, 0) / TileHeight; ty < m_rows && ty * TileHeight < y + h; ++ty)
			for (int tx = std::max(x, 0) / TileWidth; tx < m_columns && tx * TileWidth < x + w; ++tx) {
				if (!m_last[static_cast<size_t>(ty) * m_columns + tx])
					continue;
				int x0 = std::max(tx * TileWidth, x) - x, x1 = std::min((tx + 1) * TileWidth, x + w) - x;
				int y0 = std::max(ty * TileHeight, y) - y, y1 = std::min((ty + 1) * TileHeight, y + h) - y;
				for (int row = y0; row < y1; ++row)
					std::fill(pixels + static_cast<size_t>(row) * stride + x0, pixels + static_cast<size_t>(row) * stride + x1,
						0xff000000u);
			}
	}
	m_cleared = {x, y, w, h};
	m_cleared_frame = m_frames;
}

void CanvasRecording::Add(const void *data, size_t size)
{
	// FNV-1a.
	const uint8_t *bytes = static_cast<const uint8_t *>(data);
	for (size_t i = 0; i < size; ++i)
		hash = (hash ^ bytes[i]) * 0x100000001b3ull;
}

void CanvasRecording::Cover(const Rect &area)
{
	if (area.w <= 0 || area.h <= 0)
		return;
	if (bounds.w <= 0 || bounds.h <= 0) {
		bounds = area;
		return;
	}
	int x0 = std::min(bounds.x, area.x), y0 = std::min(bounds.y, area.y);
	int x1 = std::max(bounds.Right(), area.Right()), y1 = std::max(bounds.Bottom(), area.Bottom());
	bounds = {x0, y0, x1 - x0, y1 - y0};
}

void Canvas::RecordText(int kind, const std::string &text, const Rect &box, int height, uint32_t rgb, int weight,
	float scale)
{
	if (!m_record)
		return;
	m_record->Add(kind + 100);
	m_record->Add(m_clip);
	m_record->Add(text.data(), text.size());
	m_record->Add(text.size());
	m_record->Add(box);
	m_record->Add(height);
	m_record->Add(rgb);
	m_record->Add(weight);
	m_record->Add(scale);
	int x0 = std::max(box.x, m_clip.x), y0 = std::max(box.y, m_clip.y);
	int x1 = std::min(box.Right(), m_clip.Right()), y1 = std::min(box.Bottom(), m_clip.Bottom());
	if (x1 > x0 && y1 > y0)
		m_record->Cover({x0, y0, x1 - x0, y1 - y0});
}

void Canvas::ClearToView(const Rect &area)
{
	int x0 = std::max(area.x, 0), y0 = std::max(area.y, 0);
	int x1 = std::min(area.Right(), m_width), y1 = std::min(area.Bottom(), m_height);
	if (x1 <= x0 || y1 <= y0)
		return;
	Drawn(x0, y0, x1, y1);
	for (int y = y0; y < y1; ++y)
		std::fill(Row(y) + x0, Row(y) + x1, 0xff000000u);
}

void Canvas::SetLimit(const Rect &limit)
{
	int x0 = std::max(limit.x, 0), y0 = std::max(limit.y, 0);
	int x1 = std::min(limit.Right(), m_width), y1 = std::min(limit.Bottom(), m_height);
	m_limit = {x0, y0, std::max(x1 - x0, 0), std::max(y1 - y0, 0)};
	m_clip = m_limit;
}

void Canvas::MarkDrawn(const Rect &area)
{
	Drawn(std::max(area.x, 0), std::max(area.y, 0), std::min(area.Right(), m_width), std::min(area.Bottom(), m_height));
}

Canvas Canvas::Sub(const Rect &area)
{
	int x0 = std::clamp(area.x, 0, m_width), y0 = std::clamp(area.y, 0, m_height);
	int x1 = std::clamp(area.Right(), x0, m_width), y1 = std::clamp(area.Bottom(), y0, m_height);
	Canvas sub(Row(y0) + x0, x1 - x0, y1 - y0, m_stride);
	sub.m_dirty = m_dirty;
	sub.m_origin_x = m_origin_x + x0;
	sub.m_origin_y = m_origin_y + y0;
	sub.m_record = m_record;
	sub.m_alpha = m_alpha;
	sub.SetLimit({m_limit.x - x0, m_limit.y - y0, m_limit.w, m_limit.h});
	return sub;
}

void Canvas::SetClip(const Rect &clip)
{
	int x0 = std::max(clip.x, m_limit.x), y0 = std::max(clip.y, m_limit.y);
	int x1 = std::min(clip.Right(), m_limit.Right()), y1 = std::min(clip.Bottom(), m_limit.Bottom());
	m_clip = {x0, y0, std::max(x1 - x0, 0), std::max(y1 - y0, 0)};
}

void Canvas::Blit(const Image &image, int x, int y, bool color_key)
{
	BlitPart(image, {0, 0, image.width, image.height}, x, y, color_key);
}

void Canvas::BlitPart(const Image &image, const Rect &source, int x, int y, bool color_key)
{
	int sx = source.x, sy = source.y, w = source.w, h = source.h;
	if (x < m_clip.x) { sx += m_clip.x - x; w -= m_clip.x - x; x = m_clip.x; }
	if (y < m_clip.y) { sy += m_clip.y - y; h -= m_clip.y - y; y = m_clip.y; }
	w = std::min({w, m_clip.Right() - x, image.width - sx});
	h = std::min({h, m_clip.Bottom() - y, image.height - sy});
	if (w <= 0 || h <= 0)
		return;
	if (Recorded(1, {x, y, w, h}, static_cast<const void *>(&image), sx, sy, x, y, w, h, color_key))
		return;
	Drawn(x, y, x + w, y + h);

	for (int row = 0; row < h; ++row) {
		const uint32_t *src = &image.pixels[static_cast<size_t>(sy + row) * image.width + sx];
		uint32_t *dst = Row(y + row) + x;
		if (!color_key) {
			for (int col = 0; col < w; ++col)
				dst[col] = src[col] & 0xffffff;
			continue;
		}
		for (int col = 0; col < w; ++col)
			if ((src[col] & 0xffffff) != ColorKey)
				dst[col] = src[col] & 0xffffff;
	}
}

void Canvas::Tile(const Image &image, const Rect &area, bool color_key)
{
	if (image.width <= 0 || image.height <= 0)
		return;
	for (int y = area.y; y < area.Bottom(); y += image.height)
		for (int x = area.x; x < area.Right(); x += image.width)
			BlitPart(image, {0, 0, std::min(image.width, area.Right() - x), std::min(image.height, area.Bottom() - y)}, x, y,
				color_key);
}

void Canvas::Fill(const Rect &area, uint32_t color)
{
	int x0 = std::max(area.x, m_clip.x), y0 = std::max(area.y, m_clip.y);
	int x1 = std::min(area.Right(), m_clip.Right()), y1 = std::min(area.Bottom(), m_clip.Bottom());
	if (x0 >= x1)
		return; // wholly left or right of the canvas
	if (Recorded(2, {x0, y0, x1 - x0, y1 - y0}, color & 0xffffff))
		return;
	Drawn(x0, y0, x1, y1);
	for (int y = y0; y < y1; ++y)
		std::fill(Row(y) + x0, Row(y) + x1, color & 0xffffff);
}

void Canvas::Frame(const Rect &area, uint32_t color)
{
	Fill({area.x, area.y, area.w, 1}, color);
	Fill({area.x, area.Bottom() - 1, area.w, 1}, color);
	Fill({area.x, area.y, 1, area.h}, color);
	Fill({area.Right() - 1, area.y, 1, area.h}, color);
}

namespace
{
	// A pixel's four bytes a 16-bit lane each (0x00AA00RR00GG00BB), and back.
	inline uint64_t Spread(uint32_t p)
	{
		uint64_t x = p;
		x = (x | (x << 16)) & 0x0000ffff0000ffffull;
		return (x | (x << 8)) & 0x00ff00ff00ff00ffull;
	}

	inline uint32_t Pack(uint64_t x)
	{
		x = (x | (x >> 8)) & 0x0000ffff0000ffffull;
		return static_cast<uint32_t>(x | (x >> 16));
	}

	inline uint32_t Blend(uint32_t dst, uint32_t src)
	{
		uint32_t a = src >> 24;
		if (a == 0xff)
			return src & 0xffffff;
		if (a == 0)
			return dst;
		uint32_t inv = 255 - a;
		// Each channel (s * a + d * inv) / 255, the four at once: a lane's sum is at most 255 * 255, so
		// none carries into the next, and (x + 1 + (x >> 8)) >> 8 is x / 255 exactly up to there. The
		// source's top byte counts as 0, so the 3D view behind (dst's) shows through less.
		uint64_t x = Spread(src & 0xffffff) * a + Spread(dst) * inv;
		x = ((x + 0x0001000100010001ull + ((x >> 8) & 0x00ff00ff00ff00ffull)) >> 8) & 0x00ff00ff00ff00ffull;
		return Pack(x);
	}

#if defined(__SSE2__)
	// Blend's sums four pixels at a time: src[i] over dst[i] while four of the `count` are left, each
	// at its alpha (has_alpha: its own, else 255) times scale / 256, its color channels first times
	// the tint's (out of 256: tr, tg, tb; 256 each for none). The same bytes as Blend: at alpha 255
	// the sum is the source's, at 0 the destination's. Returns how many it did.
	inline int BlendRow(uint32_t *dst, const uint32_t *src, int count, bool has_alpha, uint32_t scale,
		uint32_t tr = 256, uint32_t tg = 256, uint32_t tb = 256)
	{
		const bool tinted = tr != 256 || tg != 256 || tb != 256;
		// A pixel's lanes are B, G, R, A.
		const __m128i tint16 = _mm_setr_epi16(static_cast<short>(tb), static_cast<short>(tg), static_cast<short>(tr), 0,
			static_cast<short>(tb), static_cast<short>(tg), static_cast<short>(tr), 0);
		const __m128i zero = _mm_setzero_si128();
		const __m128i rgb_mask = _mm_set1_epi32(0x00ffffff);
		const __m128i opaque = _mm_set1_epi32(0xff);
		const __m128i scale32 = _mm_set1_epi32(static_cast<int>(scale));
		const __m128i one = _mm_set1_epi16(1);
		const __m128i full = _mm_set1_epi16(255);
		int x = 0;
		for (; x + 4 <= count; x += 4) {
			__m128i s = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src + x));
			__m128i d = _mm_loadu_si128(reinterpret_cast<const __m128i *>(dst + x));
			// Each pixel's alpha (a 32-bit lane's low 16 bits; a * scale fits them), then in all four
			// of its 16-bit channel lanes.
			__m128i a = has_alpha ? _mm_srli_epi32(s, 24) : opaque;
			a = _mm_srli_epi32(_mm_mullo_epi16(a, scale32), 8);
			a = _mm_or_si128(a, _mm_slli_epi32(a, 16));
			__m128i a_lo = _mm_unpacklo_epi32(a, a), a_hi = _mm_unpackhi_epi32(a, a);
			s = _mm_and_si128(s, rgb_mask); // the source's top byte counts as 0
			auto sum = [&](__m128i s16, __m128i d16, __m128i a16) {
				if (tinted)
					s16 = _mm_srli_epi16(_mm_mullo_epi16(s16, tint16), 8); // at most 255 * 256: fits
				__m128i v = _mm_add_epi16(_mm_mullo_epi16(s16, a16), _mm_mullo_epi16(d16, _mm_sub_epi16(full, a16)));
				return _mm_srli_epi16(_mm_add_epi16(_mm_add_epi16(v, one), _mm_srli_epi16(v, 8)), 8);
			};
			__m128i lo = sum(_mm_unpacklo_epi8(s, zero), _mm_unpacklo_epi8(d, zero), a_lo);
			__m128i hi = sum(_mm_unpackhi_epi8(s, zero), _mm_unpackhi_epi8(d, zero), a_hi);
			_mm_storeu_si128(reinterpret_cast<__m128i *>(dst + x), _mm_packus_epi16(lo, hi));
		}
		return x;
	}
#endif

	// Each channel (a * (256 - t) + b * t) >> 8, t in 0..256, on Spread pixels: a lane's sum is at
	// most 255 * 256, so none carries into the next.
	inline uint64_t LerpSpread(uint64_t a, uint64_t b, uint32_t t)
	{
		return ((a * (256 - t) + b * t) >> 8) & 0x00ff00ff00ff00ffull;
	}

	// Bilinear: across the top pair and the bottom pair by fx, then down by fy.
	inline uint32_t Bilinear(uint32_t top_left, uint32_t top_right, uint32_t bottom_left, uint32_t bottom_right,
		uint32_t fx, uint32_t fy)
	{
		uint64_t top = LerpSpread(Spread(top_left), Spread(top_right), fx);
		uint64_t bottom = LerpSpread(Spread(bottom_left), Spread(bottom_right), fx);
		return Pack(LerpSpread(top, bottom, fy));
	}
}

void Canvas::FillFaded(const Rect &area, uint32_t color)
{
	if (m_alpha >= 255) {
		Fill(area, color);
		return;
	}
	if (m_alpha <= 0)
		return;
	int x0 = std::max(area.x, m_clip.x), y0 = std::max(area.y, m_clip.y);
	int x1 = std::min(area.Right(), m_clip.Right()), y1 = std::min(area.Bottom(), m_clip.Bottom());
	uint32_t src = (static_cast<uint32_t>(m_alpha) << 24) | (color & 0xffffff);
	if (x1 <= x0)
		return;
	if (Recorded(5, {x0, y0, x1 - x0, y1 - y0}, color & 0xffffff))
		return;
	Drawn(x0, y0, x1, y1);
#if defined(__SSE2__)
	// A row of the color at the alpha, blended four pixels at a time.
	thread_local std::vector<uint32_t> line;
	line.assign(static_cast<size_t>(x1 - x0), src);
#endif
	for (int y = y0; y < y1; ++y) {
		uint32_t *row = Row(y);
		int x = x0;
#if defined(__SSE2__)
		x = x0 + BlendRow(row + x0, line.data(), x1 - x0, true, 256);
#endif
		for (; x < x1; ++x)
			row[x] = Blend(row[x], src);
	}
}

void Canvas::Stretch(const Image &image, const Rect &source, const Rect &dest)
{
	Stretch(image, source, dest, 0xffffff, dest);
}

void Canvas::Stretch(const Image &image, const Rect &source, const Rect &dest, uint32_t tint, const Rect &clip)
{
	if (source.w <= 0 || source.h <= 0 || dest.w <= 0 || dest.h <= 0 || image.width <= 0 || m_alpha <= 0)
		return;
	tint &= 0xffffff;
	bool whole = clip.x <= dest.x && clip.y <= dest.y && clip.Right() >= dest.Right() && clip.Bottom() >= dest.Bottom();
	// The texel's alpha scaled by the UI's (adjustColorAlphaAndRender), out of 256.
	const bool faded = m_alpha < 255;
	const uint32_t ui_alpha = static_cast<uint32_t>(m_alpha) + (static_cast<uint32_t>(m_alpha) >> 7);
	if (source.w == dest.w && source.h == dest.h && !image.has_alpha && !faded && tint == 0xffffff && whole) {
		BlitPart(image, source, dest.x, dest.y);
		return;
	}
	// Per channel out of 256, so a 255 leaves it as it is.
	uint32_t tr = ((tint >> 16) & 0xff) + ((tint >> 23) & 1), tg = ((tint >> 8) & 0xff) + ((tint >> 15) & 1),
	         tb = (tint & 0xff) + ((tint >> 7) & 1);

	int x0 = std::max({dest.x, clip.x, m_clip.x}), y0 = std::max({dest.y, clip.y, m_clip.y});
	int x1 = std::min({dest.Right(), clip.Right(), m_clip.Right()}), y1 = std::min({dest.Bottom(), clip.Bottom(), m_clip.Bottom()});
	int max_x = std::min(source.Right(), image.width) - 1;
	int max_y = std::min(source.Bottom(), image.height) - 1;
	if (x1 <= x0 || y1 <= y0 || max_x < source.x || max_y < source.y)
		return;
	if (Recorded(6, {x0, y0, x1 - x0, y1 - y0}, static_cast<const void *>(&image), source, dest, tint, x0, y0, x1, y1))
		return;
	Drawn(x0, y0, x1, y1);
	// By value, so the writes to the canvas can't be taken for changes to what it reads.
	const bool has_alpha = image.has_alpha, tinted = tint != 0xffffff;
	auto put = [=](uint32_t *dst, uint32_t src) {
		if (tinted)
			src = (src & 0xff000000) | ((((src >> 16) & 0xff) * tr >> 8) << 16) | ((((src >> 8) & 0xff) * tg >> 8) << 8) |
			      ((src & 0xff) * tb >> 8);
		if (faded) {
			uint32_t a = has_alpha ? src >> 24 : 0xff;
			*dst = Blend(*dst, ((a * ui_alpha >> 8) << 24) | (src & 0xffffff));
		}
		else
			*dst = has_alpha ? Blend(*dst, src) : src & 0xffffff;
	};
	// A pixel for a texel (most of the UI, and every faded window's pictures): the samples below land
	// on the texel centers, so each pixel is its texel as it is.
	if (source.w == dest.w && source.h == dest.h) {
		{
			// In a loop of its own: each texel tinted, at its own alpha (an opaque picture's 255)
			// scaled by the UI's, over what's there. Locals, so the writes to the canvas can't be
			// taken for changes to them.
			const uint32_t r = tr, g = tg, b = tb;
			const uint32_t scale = faded ? ui_alpha : 256;
			const int shift = source.x - dest.x;
			const int last = std::min(x1, max_x - shift + 1); // past it, the image's last column again
			for (int y = y0; y < y1; ++y) {
				int row = std::min(y - dest.y + source.y, max_y);
				const uint32_t *src = &image.pixels[static_cast<size_t>(row) * image.width];
				uint32_t *dst = Row(y);
				int x = x0;
#if defined(__SSE2__)
				if (x0 < last)
					x = x0 + BlendRow(dst + x0, src + x0 + shift, std::min(x1, last) - x0, has_alpha, scale, r, g, b);
#endif
				for (; x < x1; ++x) {
					uint32_t s = src[x < last ? x + shift : max_x];
					uint32_t a = ((has_alpha ? s >> 24 : 0xff) * scale) >> 8;
					if (tinted)
						s = ((((s >> 16) & 0xff) * r >> 8) << 16) | ((((s >> 8) & 0xff) * g >> 8) << 8) | ((s & 0xff) * b >> 8);
					dst[x] = Blend(dst[x], (a << 24) | (s & 0xffffff));
				}
			}
		}
		return;
	}
	// Each column's sample, worked out once rather than a division a pixel.
	thread_local std::vector<int> columns;
	columns.resize(static_cast<size_t>(x1 - x0));
	for (int x = x0; x < x1; ++x) {
		int sx = ((x - dest.x) * 2 + 1) * source.w * 128 / dest.w - 128 + source.x * 256;
		columns[static_cast<size_t>(x - x0)] = std::clamp(sx, source.x * 256, max_x * 256);
	}
	for (int y = y0; y < y1; ++y) {
		// Sample at pixel centers, 8 bits of fraction.
		int sy = ((y - dest.y) * 2 + 1) * source.h * 128 / dest.h - 128 + source.y * 256;
		sy = std::clamp(sy, source.y * 256, max_y * 256);
		int row0 = sy >> 8, row1 = std::min(row0 + 1, max_y);
		uint32_t fy = static_cast<uint32_t>(sy & 0xff);
		uint32_t *dst = Row(y);
		const uint32_t *top = image.pixels.data() + static_cast<size_t>(row0) * image.width;
		const uint32_t *bottom = image.pixels.data() + static_cast<size_t>(row1) * image.width;
		const int *column = columns.data();
		for (int x = x0; x < x1; ++x) {
			int sx = column[x - x0];
			int col0 = sx >> 8, col1 = std::min(col0 + 1, max_x);
			uint32_t fx = static_cast<uint32_t>(sx & 0xff);
			put(&dst[x], Bilinear(top[col0], top[col1], bottom[col0], bottom[col1], fx, fy));
		}
	}
}

void Canvas::TileRegion(const Image &image, const Rect &source, const Rect &dest)
{
	if (source.w <= 0 || source.h <= 0)
		return;
	for (int y = dest.y; y < dest.Bottom(); y += source.h) {
		for (int x = dest.x; x < dest.Right(); x += source.w) {
			Rect part{source.x, source.y, std::min(source.w, dest.Right() - x), std::min(source.h, dest.Bottom() - y)};
			Stretch(image, part, {x, y, part.w, part.h});
		}
	}
}

void Canvas::Keep(const Rect &area, std::vector<uint32_t> &kept) const
{
	if (m_record) {
		kept.clear(); // nothing to keep: nothing's drawn
		return;
	}
	int x0 = std::max(area.x, 0), y0 = std::max(area.y, 0);
	int x1 = std::min(area.Right(), m_width), y1 = std::min(area.Bottom(), m_height);
	kept.clear();
	if (x1 <= x0 || y1 <= y0)
		return;
	kept.reserve(static_cast<size_t>(x1 - x0) * (y1 - y0));
	for (int y = y0; y < y1; ++y) {
		const uint32_t *row = m_pixels + static_cast<size_t>(y) * m_stride;
		kept.insert(kept.end(), row + x0, row + x1);
	}
}

void Canvas::BlendKept(const Rect &area, const std::vector<uint32_t> &kept, int alpha)
{
	int x0 = std::max(area.x, 0), y0 = std::max(area.y, 0);
	int x1 = std::min(area.Right(), m_width), y1 = std::min(area.Bottom(), m_height);
	if (x1 > x0 && y1 > y0 && Recorded(4, {x0, y0, x1 - x0, y1 - y0}, alpha))
		return;
	if (x1 <= x0 || y1 <= y0 || kept.size() != static_cast<size_t>(x1 - x0) * (y1 - y0) || alpha >= 255)
		return;
	// Weights out of 256, so two channels blend at once in their 16-bit lanes (r and b, t and g).
	Drawn(x0, y0, x1, y1);
	uint32_t a = static_cast<uint32_t>(std::max(alpha, 0));
	a += a >> 7;
	uint32_t b = 256 - a;
	const uint32_t *old = kept.data();
	for (int y = y0; y < y1; ++y) {
		uint32_t *row = Row(y);
		for (int x = x0; x < x1; ++x, ++old) {
			uint32_t now = row[x], was = *old;
			if (now == was)
				continue;
			uint32_t rb = (((now & 0x00ff00ff) * a + (was & 0x00ff00ff) * b) >> 8) & 0x00ff00ff;
			uint32_t tg = ((((now >> 8) & 0x00ff00ff) * a + ((was >> 8) & 0x00ff00ff) * b) >> 8) & 0x00ff00ff;
			row[x] = rb | (tg << 8);
		}
	}
}

void Canvas::Darken(const Rect &area)
{
	int x0 = std::max(area.x, m_clip.x), y0 = std::max(area.y, m_clip.y);
	int x1 = std::min(area.Right(), m_clip.Right()), y1 = std::min(area.Bottom(), m_clip.Bottom());
	if (x1 > x0 && y1 > y0 && Recorded(3, {x0, y0, x1 - x0, y1 - y0}))
		return;
	Drawn(x0, y0, x1, y1);
	for (int y = y0; y < y1; ++y) {
		uint32_t *row = Row(y);
		for (int x = x0; x < x1; ++x)
			row[x] = (row[x] >> 1) & 0x7f7f7f7f; // the 3D view behind too
	}
}
