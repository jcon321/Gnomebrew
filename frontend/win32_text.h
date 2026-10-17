#pragma once

// GDI text into the front end's DIB section: Arial via CreateFontW, the same
// parameters as eqmain's CreateFontA calls (charset 0, default quality).

#include "text.h"

#include <windows.h>

#include <map>
#include <string>
#include <utility>

class GdiTextRenderer : public TextRenderer
{
public:
	explicit GdiTextRenderer(HDC dc) : m_dc(dc) {}
	~GdiTextRenderer() override;

	TextSize Measure(const std::string &utf8, int height, int weight = 0) override;
	void Draw(Canvas &canvas, int x, int y, const std::string &utf8, int height, uint32_t rgb, int weight = 0) override;
	void DrawWrapped(Canvas &canvas, const Rect &area, const std::string &utf8, int height, uint32_t rgb,
		int weight = 0) override;

private:
	HFONT Font(int height, int weight);

	HDC m_dc;
	std::map<std::pair<int, int>, HFONT> m_fonts;
};

std::wstring Widen(const std::string &utf8);
std::string Narrow(const std::wstring &wide);
