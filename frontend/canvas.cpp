#include "canvas.h"

#include <algorithm>

void Canvas::Blit(const Image &image, int x, int y, bool color_key)
{
	BlitPart(image, {0, 0, image.width, image.height}, x, y, color_key);
}

void Canvas::BlitPart(const Image &image, const Rect &source, int x, int y, bool color_key)
{
	int sx = source.x, sy = source.y, w = source.w, h = source.h;
	if (x < 0) { sx -= x; w += x; x = 0; }
	if (y < 0) { sy -= y; h += y; y = 0; }
	w = std::min({w, m_width - x, image.width - sx});
	h = std::min({h, m_height - y, image.height - sy});
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
	int x0 = std::max(area.x, 0), y0 = std::max(area.y, 0);
	int x1 = std::min(area.Right(), m_width), y1 = std::min(area.Bottom(), m_height);
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

void Canvas::Stretch(const Image &image, const Rect &source, const Rect &dest)
{
	if (source.w <= 0 || source.h <= 0 || dest.w <= 0 || dest.h <= 0 || image.width <= 0)
		return;
	if (source.w == dest.w && source.h == dest.h && !image.has_alpha) {
		BlitPart(image, source, dest.x, dest.y);
		return;
	}

	int x0 = std::max(dest.x, 0), y0 = std::max(dest.y, 0);
	int x1 = std::min(dest.Right(), m_width), y1 = std::min(dest.Bottom(), m_height);
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

void Canvas::Darken(const Rect &area)
{
	int x0 = std::max(area.x, 0), y0 = std::max(area.y, 0);
	int x1 = std::min(area.Right(), m_width), y1 = std::min(area.Bottom(), m_height);
	for (int y = y0; y < y1; ++y) {
		uint32_t *row = Row(y);
		for (int x = x0; x < x1; ++x)
			row[x] = (row[x] >> 1) & 0x7f7f7f7f; // the 3D view behind too
	}
}
