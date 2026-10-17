#pragma once

// Text drawing is the one platform-specific piece of rendering. On Windows it's
// GDI with Arial, the same CreateFontA calls eqmain makes (win32_text.cpp).
// The Linux preview tool substitutes placeholder boxes.

#include "canvas.h"

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
	// Word-wrapped inside area; '\n' breaks lines.
	virtual void DrawWrapped(Canvas &canvas, const Rect &area, const std::string &utf8, int height, uint32_t rgb,
		int weight = 0) = 0;
};
