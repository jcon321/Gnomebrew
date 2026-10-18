#pragma once

// GDI text into the front end's DIB sections: Arial via CreateFontW, the same
// parameters as eqmain's CreateFontA calls (charset 0, default quality). Each DIB
// is registered with its memory DC; text goes to the DIB the canvas points into,
// offset by where the canvas starts in it (a canvas can be a sub-area).
// Text over a GPU-drawn 3D view (see canvas.h) is drawn through a scratch DIB
// and blended in by hand, since GDI only knows the DIB's own colors; single lines
// keep that coverage (and every string its size), so name tags and chat lines
// cost no GDI calls after their first frame.

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

	HFONT Font(int height, int weight);
	const Surface *Target(Canvas &canvas, int &x, int &y); // the canvas's DIB, and its origin added to x, y
	// Text's coverage, white on black, for a string drawn over the view.
	struct Mask
	{
		int width = 0, height = 0;
		std::vector<uint32_t> coverage;
	};

	// Runs draw(dc, dx, dy) for text inside `box` (surface pixels), where dx, dy move surface
	// coordinates into the DC's: straight into the surface, or through the scratch DIB where
	// the box shows a 3D view.
	void Output(const Surface &surface, RECT box, uint32_t rgb, HFONT font, const std::function<void(HDC, int, int)> &draw);
	bool ShowsView(const Surface &surface, const RECT &box) const; // any of the 3D view in the box
	void BlendMask(const Surface &surface, const uint32_t *mask, int mask_stride, int width, int height, int x, int y,
		uint32_t rgb);
	bool ReserveScratch(int width, int height);
	static std::string TextKey(const std::string &utf8, int height, int weight);

	std::vector<Surface> m_surfaces;
	Scratch m_scratch;
	std::unordered_map<std::string, TextSize> m_sizes; // Measure, by TextKey
	std::unordered_map<std::string, Mask> m_masks;     // Draw over the view, by TextKey
	std::map<std::pair<int, int>, HFONT> m_fonts;
};

std::wstring Widen(const std::string &utf8);
std::string Narrow(const std::wstring &wide);
