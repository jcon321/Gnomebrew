#pragma once

// Text drawing is the one platform-specific piece of rendering. On Windows it's
// GDI with Arial, the same CreateFontA calls eqmain makes (win32_text.cpp).
// The Linux preview tool substitutes placeholder boxes.

#include "canvas.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

struct TextSize
{
	int w = 0;
	int h = 0;
};

class TextRenderer
{
public:
	virtual ~TextRenderer() = default;

	// height is CreateFont's lfHeight in pixels. weight 0 is one of eqmain's fonts (default weight),
	// drawn by GDI a string at a time. Any other weight is one of eqgfx's cached fonts, the in-game
	// SIDL fonts (100 or 700, SidlFontWeight), built and drawn glyph by glyph as eqgfx does.
	virtual TextSize Measure(const std::string &utf8, int height, int weight = 0) = 0;
	// Top-left aligned, transparent background.
	virtual void Draw(Canvas &canvas, int x, int y, const std::string &utf8, int height, uint32_t rgb, int weight = 0) = 0;
	// The text drawn at `height` and then stretched by `scale`, as eqgfx draws name sprites: the
	// small font's shapes, made bigger (a large font's own glyphs are thinner). Its size is
	// Measure(utf8, height, weight) times scale. Without a renderer of its own, the nearest size.
	virtual void DrawScaled(Canvas &canvas, int x, int y, const std::string &utf8, int height, float scale, uint32_t rgb,
		int weight = 0)
	{
		Draw(canvas, x, y, utf8, std::max(1, static_cast<int>(height * scale + 0.5f)), rgb, weight);
	}
	// The coverage DrawScaled stretches, as it is: 0..255 a texel, rows top first, `w` x `h` with
	// `left` texels of slack before the text (Measure's width) starts. Without a renderer of its
	// own, Draw's text white on black.
	virtual bool Coverage(const std::string &utf8, int height, int weight, std::vector<uint8_t> &alpha, int &w, int &h,
		int &left)
	{
		TextSize size = Measure(utf8, height, weight);
		w = size.w + 3;
		h = size.h;
		left = 1;
		if (size.w <= 0 || h <= 0)
			return false;
		std::vector<uint32_t> pixels(static_cast<size_t>(w) * h, 0);
		Canvas canvas(pixels.data(), w, h, w);
		Draw(canvas, left, 0, utf8, height, 0xffffff, weight);
		alpha.resize(pixels.size());
		for (size_t i = 0; i < pixels.size(); ++i)
			alpha[i] = static_cast<uint8_t>(pixels[i] >> 8);
		return true;
	}
	// Word-wrapped inside area; '\n' breaks lines.
	virtual void DrawWrapped(Canvas &canvas, const Rect &area, const std::string &utf8, int height, uint32_t rgb,
		int weight = 0) = 0;
};
