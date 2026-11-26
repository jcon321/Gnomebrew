#pragma once

// Text drawn with FreeType (third_party/freetype) from Arial's own TrueType files, on every
// platform, laid out as GDI lays out eqmain's and eqgfx's CreateFont(A/W) fonts:
//
// - A height is CreateFont's lfHeight: positive is the cell height. GDI picks the largest ppem
//   whose VDMX yMax - yMin fits it (the font's VDMX table, for the 1:1 ratio), and tmAscent,
//   tmDescent are that entry's; a font without one is scaled by usWinAscent + usWinDescent.
//   Negative is the ppem itself.
// - Glyphs are hinted by the TrueType bytecode (the v35 interpreter, full hinting, as GDI hints)
//   and advance by their hinted (hdmx) widths, so a string is as wide as GetTextExtentPoint32
//   says. Weight 600 and up is the bold file (arialbd.ttf).
// - Coverage is FreeType's 8-bit greyscale, not ClearType's per channel.
//
// Weight 0 is one of eqmain's fonts, a string drawn whole; any other weight is one of eqgfx's
// cached fonts (see text.h, README "In-game text"): each glyph cut to its advance and the font's
// height, its coverage kept to four bits.

#include "text.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class FontTextRenderer : public TextRenderer
{
public:
	FontTextRenderer();
	~FontTextRenderer() override;
	FontTextRenderer(const FontTextRenderer &) = delete;
	FontTextRenderer &operator=(const FontTextRenderer &) = delete;

	// Arial's regular and bold TrueType files (bold may be empty: the regular stands in).
	bool Open(const std::string &regular, const std::string &bold, std::string &error);
	// Where Arial is: $MYCLIENT_FONT_DIR, Windows' Fonts folder (%WINDIR%), then the usual Linux
	// font folders (msttcorefonts' Arial, else Liberation Sans, which has Arial's metrics).
	static bool FindArial(std::string &regular, std::string &bold);

	TextSize Measure(const std::string &utf8, int height, int weight = 0) override;
	void Draw(Canvas &canvas, int x, int y, const std::string &utf8, int height, uint32_t rgb, int weight = 0) override;
	void DrawScaled(Canvas &canvas, int x, int y, const std::string &utf8, int height, float scale, uint32_t rgb,
		int weight = 0) override;
	bool Coverage(const std::string &utf8, int height, int weight, std::vector<uint8_t> &alpha, int &w, int &h,
		int &left) override;
	// DrawText's DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX, 2 pixels in from the area's left.
	void DrawWrapped(Canvas &canvas, const Rect &area, const std::string &utf8, int height, uint32_t rgb,
		int weight = 0) override;

	// The UTF-8 string's code points (a malformed byte is U+FFFD, as MultiByteToWideChar makes it).
	static std::u32string Decode(const std::string &utf8);

private:
	struct Face;
	struct Glyph
	{
		int advance = 0;        // hinted, whole pixels
		int left = 0, top = 0;  // the bitmap's offset from the pen and the baseline (up)
		int width = 0, rows = 0;
		std::vector<uint8_t> alpha; // width x rows
		// eqgfx's cached glyph: advance x the font's height, cut from the bitmap at pen 0, top 0,
		// four bits of coverage (made the first time it's asked for).
		bool cell_made = false, ink = false;
		std::vector<uint8_t> cell;
	};
	struct Font
	{
		Face *face = nullptr;
		int ppem = 0;
		int ascent = 0, descent = 0, height = 0; // tmAscent, tmDescent, tmHeight
		std::unordered_map<char32_t, Glyph> glyphs;
	};

	Font *GetFont(int height, int weight);
	const Glyph &GetGlyph(Font &font, char32_t ch);
	const Glyph &CellGlyph(Font &font, char32_t ch);
	int Advance(Font &font, const std::u32string &text, size_t begin, size_t end);

	// Blends `alpha` (w x h, 0..255) at x, y in `rgb`, inside the canvas's clip, hiding that much
	// of the 3D view behind (the top byte).
	static void Blend(Canvas &canvas, const uint8_t *alpha, int stride, int w, int h, int x, int y, uint32_t rgb);
	// A string's glyphs at the pen, on the baseline `ascent` below y.
	void DrawRun(Canvas &canvas, Font &font, int x, int y, const std::u32string &text, size_t begin, size_t end,
		uint32_t rgb);
	void DrawCells(Canvas &canvas, Font &font, int x, int y, const std::u32string &text, uint32_t rgb);

	// The string's coverage with a pixel of slack on its left, Measure's width + 3 wide (kept).
	struct Mask
	{
		int width = 0, height = 0;
		std::vector<uint8_t> alpha;
	};
	const Mask *TextMask(const std::string &utf8, int height, int weight);

	struct Library;
	std::unique_ptr<Library> m_library;
	std::unique_ptr<Face> m_regular, m_bold;
	std::map<std::pair<int, int>, Font> m_fonts; // by lfHeight and regular (0) or bold (1)
	std::unordered_map<std::string, Mask> m_masks;
	std::vector<uint8_t> m_stretched; // DrawScaled's mask, stretched
};
