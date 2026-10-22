#pragma once

// Text drawing is the one platform-specific piece of rendering. On Windows it's
// GDI with Arial, the same CreateFontA calls eqmain makes (win32_text.cpp).
// The Linux preview tool substitutes placeholder boxes.

#include "canvas.h"

#include <algorithm>
#include <string>

struct TextSize
{
	int w = 0;
	int h = 0;
};

class TextRenderer
{
public:
	virtual ~TextRenderer() = default;

	// height is CreateFont's lfHeight in pixels; weight 0 means the default (normal).
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
	// Word-wrapped inside area; '\n' breaks lines.
	virtual void DrawWrapped(Canvas &canvas, const Rect &area, const std::string &utf8, int height, uint32_t rgb,
		int weight = 0) = 0;
};
