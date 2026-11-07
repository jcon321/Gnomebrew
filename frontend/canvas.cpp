#include "canvas.h"

#include <algorithm>

void Canvas::SetClip(const Rect &clip)
{
	int x0 = std::max(clip.x, 0), y0 = std::max(clip.y, 0);
	int x1 = std::min(clip.Right(), m_width), y1 = std::min(clip.Bottom(), m_height);
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
	inline uint32_t Blend(uint32_t dst, uint32_t src)
	{
		uint32_t a = src >> 24;
		if (a == 0xff)
			return src & 0xffffff;
		if (a == 0)
			return dst;
		uint32_t inv = 255 - a;
		uint32_t r = (((src >> 16) & 0xff) * a + ((dst >> 16) & 0xff) * inv) / 255;
		uint32_t g = (((src >> 8) & 0xff) * a + ((dst >> 8) & 0xff) * inv) / 255;
		uint32_t b = ((src & 0xff) * a + (dst & 0xff) * inv) / 255;
		uint32_t t = ((dst >> 24) * inv) / 255; // the 3D view behind shows through less
		return (t << 24) | (r << 16) | (g << 8) | b;
	}

	inline uint32_t Lerp(uint32_t a, uint32_t b, uint32_t t) // t in 0..256
	{
		uint32_t out = 0;
		for (int shift = 0; shift < 32; shift += 8) {
			uint32_t ca = (a >> shift) & 0xff, cb = (b >> shift) & 0xff;
			out |= ((ca * (256 - t) + cb * t) >> 8) << shift;
		}
		return out;
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
	for (int y = y0; y < y1; ++y) {
		uint32_t *row = Row(y);
		for (int x = x0; x < x1; ++x)
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
	for (int y = y0; y < y1; ++y) {
		// Sample at pixel centers, 8 bits of fraction.
		int sy = ((y - dest.y) * 2 + 1) * source.h * 128 / dest.h - 128 + source.y * 256;
		sy = std::clamp(sy, source.y * 256, max_y * 256);
		int row0 = sy >> 8, row1 = std::min(row0 + 1, max_y);
		uint32_t fy = static_cast<uint32_t>(sy & 0xff);
		uint32_t *dst = Row(y);
		for (int x = x0; x < x1; ++x) {
			int sx = ((x - dest.x) * 2 + 1) * source.w * 128 / dest.w - 128 + source.x * 256;
			sx = std::clamp(sx, source.x * 256, max_x * 256);
			int col0 = sx >> 8, col1 = std::min(col0 + 1, max_x);
			uint32_t fx = static_cast<uint32_t>(sx & 0xff);
			uint32_t top = Lerp(image.At(col0, row0), image.At(col1, row0), fx);
			uint32_t bottom = Lerp(image.At(col0, row1), image.At(col1, row1), fx);
			uint32_t src = Lerp(top, bottom, fy);
			if (tint != 0xffffff)
				src = (src & 0xff000000) | ((((src >> 16) & 0xff) * tr >> 8) << 16) | ((((src >> 8) & 0xff) * tg >> 8) << 8) |
				      ((src & 0xff) * tb >> 8);
			if (faded) {
				uint32_t a = image.has_alpha ? src >> 24 : 0xff;
				dst[x] = Blend(dst[x], ((a * ui_alpha >> 8) << 24) | (src & 0xffffff));
			}
			else
				dst[x] = image.has_alpha ? Blend(dst[x], src) : src & 0xffffff;
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
	if (x1 <= x0 || y1 <= y0 || kept.size() != static_cast<size_t>(x1 - x0) * (y1 - y0) || alpha >= 255)
		return;
	// Weights out of 256, so two channels blend at once in their 16-bit lanes (r and b, t and g).
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
	for (int y = y0; y < y1; ++y) {
		uint32_t *row = Row(y);
		for (int x = x0; x < x1; ++x)
			row[x] = (row[x] >> 1) & 0x7f7f7f7f; // the 3D view behind too
	}
}
