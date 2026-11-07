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

#include <algorithm>
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

// What has been drawn on a canvas, by tiles of TileWidth x TileHeight, this frame and the one
// before: the Direct3D host keeps the canvas on the GPU and clears (under the view) and uploads
// only those tiles, not all of it.
class CanvasDirty
{
public:
	static constexpr int TileWidth = 64, TileHeight = 16;

	void Resize(int width, int height); // everything drawn on, this frame and the last
	int Width() const { return m_width; }
	int Height() const { return m_height; }
	void Mark(int x0, int y0, int x1, int y1); // [x0, x1) x [y0, y1), clipped to the canvas

	// A GPU Renderer::Finish's block of the canvas (`pixels` its first row) set to 0xff000000, the
	// view showing through: when the last frame cleared this same block, only the tiles drawn on
	// since; else all of it, which then counts as drawn.
	void ClearBlock(uint32_t *pixels, int stride, int x, int y, int w, int h);
	// Each run of tiles side by side drawn on this frame or the last: what has to go up to the
	// GPU's copy. `upload(x, y, w, h)`, on the canvas.
	template <typename Upload>
	void ForEachRun(Upload upload) const
	{
		for (int ty = 0; ty < m_rows; ++ty)
			for (int tx = 0; tx < m_columns;) {
				if (!Drawn(tx, ty)) {
					++tx;
					continue;
				}
				int first = tx;
				while (tx < m_columns && Drawn(tx, ty))
					++tx;
				int x = first * TileWidth, y = ty * TileHeight;
				upload(x, y, std::min(tx * TileWidth, m_width) - x, std::min(y + TileHeight, m_height) - y);
			}
	}
	void NextFrame(); // a frame shown: this frame's become the last's, and none this frame yet

private:
	bool Drawn(int tx, int ty) const
	{
		size_t i = static_cast<size_t>(ty) * m_columns + tx;
		return m_this[i] || m_last[i];
	}

	int m_width = 0, m_height = 0, m_columns = 0, m_rows = 0;
	std::vector<uint8_t> m_this, m_last; // a tile each, row by row
	uint64_t m_frames = 0;            // NextFrames so far
	uint64_t m_cleared_frame = ~0ull; // and when ClearBlock last cleared m_cleared
	Rect m_cleared;
};

class Canvas
{
public:
	Canvas() = default;
	Canvas(uint32_t *pixels, int width, int height, int stride_pixels)
		: m_pixels(pixels), m_width(width), m_height(height), m_stride(stride_pixels), m_clip{0, 0, width, height}
	{
	}

	int Width() const { return m_width; }
	int Height() const { return m_height; }
	int Stride() const { return m_stride; } // in pixels
	// The pixels themselves. Whoever writes them other than through the calls here says where
	// with MarkDrawn.
	uint32_t *Row(int y) { return m_pixels + static_cast<size_t>(y) * m_stride; }

	// Everything drawn on the canvas goes into `dirty` (null: not kept), and a Sub canvas's into its
	// parent's.
	void Track(CanvasDirty *dirty) { m_dirty = dirty; }
	void MarkDrawn(const Rect &area); // clipped to the canvas
	// The part of this canvas at `area` (within it), as a canvas of its own.
	Canvas Sub(const Rect &area);

	// Where drawing lands (CXWnd's clip rects): Blit, Tile, Fill, Frame, Darken, Stretch and the
	// text renderers all stop at it. The whole canvas unless set; SetClip keeps it on the canvas.
	void SetClip(const Rect &clip);
	void ResetClip() { m_clip = {0, 0, m_width, m_height}; }
	const Rect &Clip() const { return m_clip; }

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

	// The UI's alpha (eqgame's DAT_0062a864, 0..255), which CXWnd::DoAllDrawing scales by a window's
	// Alpha and fade while it draws it. Only textured quads take it (CUITexturePiece's and
	// CTextureAnimation's draws: Stretch and TileRegion here) and a window's color-only background
	// (FillFaded); text and plain colored rects (list selections, the caret) draw as they are, so
	// a window faded to nothing still shows its text.
	void SetAlpha(int alpha) { m_alpha = alpha < 0 ? 0 : alpha > 255 ? 255 : alpha; }
	int Alpha() const { return m_alpha; }
	void FillFaded(const Rect &area, uint32_t color); // Fill at Alpha()

	// SIDL drawing: source rect of an image stretched (bilinear) into dest, alpha-blended
	// when the image has alpha. This is how eqgame draws Ui2DAnimation frames into controls.
	void Stretch(const Image &image, const Rect &source, const Rect &dest);
	// The same with every texel multiplied by `tint` (0xRRGGBB, white leaves it alone), drawing only
	// the part of dest inside `clip` (CUITexturePiece::Draw's color and clip rect).
	void Stretch(const Image &image, const Rect &source, const Rect &dest, uint32_t tint, const Rect &clip);
	// Repeat the source rect across dest at its natural size (window backgrounds).
	void TileRegion(const Image &image, const Rect &source, const Rect &dest);

private:
	// [x0, x1) x [y0, y1), already on the canvas, into the tracker.
	void Drawn(int x0, int y0, int x1, int y1)
	{
		if (m_dirty && x1 > x0 && y1 > y0)
			m_dirty->Mark(x0 + m_origin_x, y0 + m_origin_y, x1 + m_origin_x, y1 + m_origin_y);
	}

	uint32_t *m_pixels = nullptr;
	int m_width = 0;
	int m_height = 0;
	int m_stride = 0;
	Rect m_clip;
	int m_alpha = 255;
	CanvasDirty *m_dirty = nullptr;
	int m_origin_x = 0, m_origin_y = 0; // where it is on the tracked canvas
};
