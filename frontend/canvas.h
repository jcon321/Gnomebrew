#pragma once

// A 32-bit 0xTTRRGGBB pixel surface (a view over memory the platform owns, such
// as a Win32 DIB section) plus the 2D operations eqmain.dll uses on its
// DirectDraw surfaces: blits with a magenta color key, tiling and darkening.
//
// The top byte is how much of a GPU-drawn 3D view behind the canvas shows
// through (0 none, 255 all), with RRGGBB the canvas's own color added over it.
// A GPU Renderer::Finish sets its block to 0xff000000; everything drawn on the
// canvas writes 0 there, and blending and darkening scale it like a color, so
// the Windows host can composite the canvas over the view (win32_d3d11.cpp).
// Without a GPU view everything stays 0 and the byte is ignored.

#include "bitmap.h"

#include <cstdint>
#include <vector>

struct Rect
{
	int x = 0, y = 0, w = 0, h = 0;

	bool Contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
	int Right() const { return x + w; }
	int Bottom() const { return y + h; }
};

// eqmain passes Win32 COLORREFs (0x00BBGGRR); convert them to canvas pixels.
constexpr uint32_t FromColorRef(uint32_t colorref)
{
	return ((colorref & 0xff) << 16) | (colorref & 0xff00) | ((colorref >> 16) & 0xff);
}

constexpr uint32_t ColorKey = 0xff00ff; // transparent pixels in the front-end art

class Canvas
{
public:
	Canvas() = default;
	Canvas(uint32_t *pixels, int width, int height, int stride_pixels)
		: m_pixels(pixels), m_width(width), m_height(height), m_stride(stride_pixels)
	{
	}

	int Width() const { return m_width; }
	int Height() const { return m_height; }
	int Stride() const { return m_stride; } // in pixels
	uint32_t *Row(int y) { return m_pixels + static_cast<size_t>(y) * m_stride; }

	void Blit(const Image &image, int x, int y, bool color_key = false);
	void BlitPart(const Image &image, const Rect &source, int x, int y, bool color_key = false);
	void Tile(const Image &image, const Rect &area, bool color_key = false); // repeat across, clipped to area
	void Fill(const Rect &area, uint32_t color);
	void Frame(const Rect &area, uint32_t color);
	void Darken(const Rect &area); // halves every channel, like darkenSurfaceRegion
	void DarkenAll() { Darken({0, 0, m_width, m_height}); }

	// Drawing something at partial opacity (a faded SIDL window): keep what's under it, draw it
	// as usual, then blend the kept pixels back in at 255 - alpha. Every byte blends, so the 3D
	// view behind shows through as much as it should. The area is clipped to the canvas.
	void Keep(const Rect &area, std::vector<uint32_t> &kept) const;
	void BlendKept(const Rect &area, const std::vector<uint32_t> &kept, int alpha);

	// SIDL drawing: source rect of an image stretched (bilinear) into dest, alpha-blended
	// when the image has alpha. This is how eqgame draws Ui2DAnimation frames into controls.
	void Stretch(const Image &image, const Rect &source, const Rect &dest);
	// The same with every texel multiplied by `tint` (0xRRGGBB, white leaves it alone), drawing only
	// the part of dest inside `clip` (CUITexturePiece::Draw's color and clip rect).
	void Stretch(const Image &image, const Rect &source, const Rect &dest, uint32_t tint, const Rect &clip);
	// Repeat the source rect across dest at its natural size (window backgrounds).
	void TileRegion(const Image &image, const Rect &source, const Rect &dest);

private:
	uint32_t *m_pixels = nullptr;
	int m_width = 0;
	int m_height = 0;
	int m_stride = 0;
};
