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
#include <string>
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

	// The block holds the front end's own drawing over the view, kept from frame to frame (the
	// RetainedUi redraws what changes): ClearBlock then leaves it as it is.
	void SetRetained(bool retained) { m_retained = retained; }
	// ClearBlock set all of its block to the view this frame (anything kept there is gone), and
	// the block it last cleared.
	bool ClearedAll() const { return m_cleared_all_frame == m_frames; }
	const Rect &Block() const { return m_cleared; }
	bool BlockThisFrame() const { return m_cleared_frame == m_frames; }

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
	uint64_t m_cleared_all_frame = ~0ull;
	Rect m_cleared;
	bool m_retained = false;
};

// What a canvas would have drawn, while it only records (Canvas::Record): a hash of every call and
// its arguments (the images by address, the text by content), and the box around it all.
struct CanvasRecording
{
	uint64_t hash = 0xcbf29ce484222325ull;
	Rect bounds; // empty when nothing would be drawn
	void Add(const void *data, size_t size);
	template <typename T>
	void Add(const T &value)
	{
		Add(&value, sizeof value);
	}
	void Cover(const Rect &area); // grows bounds to take in area (already clipped)
};

class Canvas
{
public:
	Canvas() = default;
	Canvas(uint32_t *pixels, int width, int height, int stride_pixels)
		: m_pixels(pixels), m_width(width), m_height(height), m_stride(stride_pixels), m_clip{0, 0, width, height},
		  m_limit{0, 0, width, height}
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
	CanvasDirty *Tracker() const { return m_dirty; }
	void MarkDrawn(const Rect &area); // clipped to the canvas
	// The part of this canvas at `area` (within it), as a canvas of its own (sharing its tracker,
	// recording and limit).
	Canvas Sub(const Rect &area);

	// Where drawing lands (CXWnd's clip rects): Blit, Tile, Fill, Frame, Darken, Stretch and the
	// text renderers all stop at it. The whole canvas unless set; SetClip keeps it on the canvas,
	// and within the limit.
	void SetClip(const Rect &clip);
	void ResetClip() { m_clip = m_limit; }
	const Rect &Clip() const { return m_clip; }
	// A clip no SetClip gets out of (the part a RetainedUi redraws); the whole canvas to lift it.
	void SetLimit(const Rect &limit);
	void ResetLimit() { SetLimit({0, 0, m_width, m_height}); }

	// While `recording` is set, nothing is drawn: every call (and the text renderers' draws) goes
	// into it instead. Null to draw again.
	void Record(CanvasRecording *recording) { m_record = recording; }
	CanvasRecording *Recording() const { return m_record; }
	// A text renderer's draw while recording: its kind, the string and how, and the box it covers.
	void RecordText(int kind, const std::string &text, const Rect &box, int height, uint32_t rgb, int weight, float scale);
	// The view showing through `area` again (0xff000000), as a GPU Renderer::Finish leaves it.
	void ClearToView(const Rect &area);

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
	Rect m_limit{0, 0, 0x3fffffff, 0x3fffffff}; // set to the canvas by the constructor
	CanvasRecording *m_record = nullptr;
	// While recording: the call (a kind and its arguments) into the hash, and what it covers.
	template <typename... Args>
	bool Recorded(int kind, const Rect &covers, const Args &...args)
	{
		if (!m_record)
			return false;
		m_record->Add(kind);
		m_record->Add(m_alpha);
		m_record->Add(m_clip);
		m_record->Add(covers);
		(m_record->Add(args), ...);
		m_record->Cover(covers);
		return true;
	}
};
