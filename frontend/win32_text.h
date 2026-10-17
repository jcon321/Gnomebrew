#pragma once

// GDI text into the front end's DIB sections: Arial via CreateFontW, the same
// parameters as eqmain's CreateFontA calls (charset 0, default quality). Each DIB
// is registered with its memory DC; text goes to the DIB the canvas points into,
// offset by where the canvas starts in it (a canvas can be a sub-area).

#include "text.h"

#include <windows.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

class GdiTextRenderer : public TextRenderer
{
public:
	GdiTextRenderer() = default;
	~GdiTextRenderer() override;

	void AddSurface(HDC dc, const uint32_t *pixels, int stride, int height);

	TextSize Measure(const std::string &utf8, int height, int weight = 0) override;
	void Draw(Canvas &canvas, int x, int y, const std::string &utf8, int height, uint32_t rgb, int weight = 0) override;
	void DrawWrapped(Canvas &canvas, const Rect &area, const std::string &utf8, int height, uint32_t rgb,
		int weight = 0) override;

private:
	struct Surface
	{
		HDC dc;
		const uint32_t *pixels;
		int stride, height;
	};

	HFONT Font(int height, int weight);
	HDC Target(Canvas &canvas, int &x, int &y); // the canvas's DC, and its origin added to x, y

	std::vector<Surface> m_surfaces;
	std::map<std::pair<int, int>, HFONT> m_fonts;
};

std::wstring Widen(const std::string &utf8);
std::string Narrow(const std::wstring &wide);
