#pragma once

// GDI text into the front end's DIB sections: Arial via CreateFontW, the same
// parameters as eqmain's CreateFontA calls (charset 0, default quality). Each DIB
// is registered with its memory DC; text goes to the DIB the canvas points into,
// offset by where the canvas starts in it (a canvas can be a sub-area).
// Text over a GPU-drawn 3D view (see canvas.h) is drawn through a scratch DIB
// and blended in by hand, since GDI only knows the DIB's own colors; single lines
// keep that coverage (and every string its size), so name tags and chat lines
// cost no GDI calls after their first frame.
// The in-game fonts (a weight other than 0, see text.h) are eqgfx's cached fonts instead: GDI
// draws each glyph once, alone, and the text is those glyphs put side by side.

#include "text.h"

#include <windows.h>

#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class GdiTextRenderer : public TextRenderer
{
public:
	GdiTextRenderer() = default;
	~GdiTextRenderer() override;

	void AddSurface(HDC dc, uint32_t *pixels, int stride, int height);

	TextSize Measure(const std::string &utf8, int height, int weight = 0) override;
	void Draw(Canvas &canvas, int x, int y, const std::string &utf8, int height, uint32_t rgb, int weight = 0) override;
	void DrawScaled(Canvas &canvas, int x, int y, const std::string &utf8, int height, float scale, uint32_t rgb,
		int weight = 0) override;
	void DrawWrapped(Canvas &canvas, const Rect &area, const std::string &utf8, int height, uint32_t rgb,
		int weight = 0) override;

private:
	struct Surface
	{
		HDC dc;
		uint32_t *pixels;
		int stride, height;
	};
	struct Scratch
	{
		HDC dc = nullptr;
		HBITMAP dib = nullptr;
		HGDIOBJ old_bitmap = nullptr;
		uint32_t *pixels = nullptr;
		int width = 0, height = 0;
	};

	// antialiased: ANTIALIASED_QUALITY, as eqgfx makes the fonts it stretches; else DEFAULT_QUALITY.
	HFONT Font(int height, int weight, bool antialiased = false);
	// The canvas's DIB, with the canvas's origin added to x, y and its clip rect in the DIB's pixels.
	const Surface *Target(Canvas &canvas, int &x, int &y, RECT &clip);
	// Text's coverage, white on black, for a string drawn over the view.
	struct Mask
	{
		int width = 0, height = 0;
		std::vector<uint32_t> coverage;
	};
	// The string's mask, made once and kept; it starts a pixel left of where the text does.
	const Mask *TextMask(const std::string &utf8, int height, int weight, bool antialiased);

	// Runs draw(dc, dx, dy) for text inside `box` (surface pixels), where dx, dy move surface
	// coordinates into the DC's: straight into the surface, or through the scratch DIB where
	// the box shows a 3D view. Nothing lands outside `clip`.
	void Output(const Surface &surface, RECT box, const RECT &clip, uint32_t rgb, HFONT font,
		const std::function<void(HDC, int, int)> &draw);
	bool ShowsView(const Surface &surface, const RECT &box) const; // any of the 3D view in the box
	void BlendMask(const Surface &surface, const uint32_t *mask, int mask_stride, int width, int height, int x, int y,
		uint32_t rgb, const RECT &clip);
	bool ReserveScratch(int width, int height);
	static std::string TextKey(const std::string &utf8, int height, int weight);

	// eqgfx's glyph cache (createGlyphCacheEntry, updateGlyphTexture): a glyph is ExtTextOutW'd
	// alone at 0, 0, white on black, and kept only as wide as GetTextExtentPoint32W says, which is
	// also how far the text moves on (what overhangs that is cut off). Its alpha is the blue
	// channel's top four bits, so it's grey, not ClearType, and the faintest edges drop out.
	struct Glyph
	{
		int advance = 0;
		bool ink = false;
		std::vector<uint32_t> coverage; // advance x the font's height, white on black
	};
	struct CachedFont
	{
		int height = 0; // tmHeight: every glyph's rows
		std::unordered_map<wchar_t, Glyph> glyphs;
	};
	CachedFont *Cached(int height, int weight);
	const Glyph &CachedGlyph(CachedFont &font, int height, int weight, wchar_t ch);
	TextSize MeasureCached(const std::string &utf8, int height, int weight);
	void DrawCached(Canvas &canvas, int x, int y, const std::string &utf8, int height, uint32_t rgb, int weight);

	std::vector<Surface> m_surfaces;
	Scratch m_scratch;
	std::unordered_map<std::string, TextSize> m_sizes; // Measure, by TextKey
	std::unordered_map<std::string, Mask> m_masks;     // Draw over the view, by TextKey
	std::vector<uint32_t> m_stretched;                 // DrawScaled's mask, stretched
	std::map<std::pair<int, int>, HFONT> m_fonts;      // by height and weight (+ 0x10000 antialiased)
	std::map<std::pair<int, int>, CachedFont> m_cached; // by height and weight
};

std::wstring Widen(const std::string &utf8);
std::string Narrow(const std::wstring &wide);
