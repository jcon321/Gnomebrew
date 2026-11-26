#include "font_text.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_TABLES_H
#include FT_TRUETYPE_TAGS_H

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>

namespace
{
	// Adds the time until it goes out of scope, and a call, to the renderer's stats.
	struct TextTimer
	{
		TextStats &stats;
		std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
		explicit TextTimer(TextStats &s) : stats(s) { ++stats.calls; }
		~TextTimer() { stats.ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(); }
	};

	bool ReadFile(const std::string &path, std::vector<uint8_t> &data)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
			return false;
		data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
		return !data.empty();
	}

	bool Exists(const std::string &path)
	{
		std::ifstream file(path, std::ios::binary);
		return static_cast<bool>(file);
	}

	int RoundDiv(long long a, long long b) { return static_cast<int>((a * 2 + b) / (b * 2)); }

	Rect Intersect(const Rect &a, const Rect &b)
	{
		int x0 = std::max(a.x, b.x), y0 = std::max(a.y, b.y);
		int x1 = std::min(a.Right(), b.Right()), y1 = std::min(a.Bottom(), b.Bottom());
		return {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
	}
}

struct FontTextRenderer::Library
{
	FT_Library library = nullptr;
	~Library()
	{
		if (library)
			FT_Done_FreeType(library);
	}
};

struct FontTextRenderer::Face
{
	std::vector<uint8_t> data; // FT_New_Memory_Face reads it for as long as the face lives
	FT_Face face = nullptr;
	int units_per_em = 2048;
	int win_ascent = 0, win_descent = 0;
	// The VDMX table's records for the 1:1 aspect ratio, by ppem: yMax, yMin.
	struct Vdmx
	{
		int ppem, y_max, y_min;
	};
	std::vector<Vdmx> vdmx;

	~Face()
	{
		if (face)
			FT_Done_Face(face);
	}

	void ReadVdmx()
	{
		FT_ULong length = 0;
		if (FT_Load_Sfnt_Table(face, TTAG_VDMX, 0, nullptr, &length) || length < 6)
			return;
		std::vector<uint8_t> t(length);
		if (FT_Load_Sfnt_Table(face, TTAG_VDMX, 0, t.data(), &length))
			return;
		auto u16 = [&t](size_t at) -> int { return at + 2 <= t.size() ? (t[at] << 8) | t[at + 1] : 0; };
		auto s16 = [&](size_t at) -> int { return static_cast<int16_t>(u16(at)); };
		int ratios = u16(4);
		// The first ratio range that takes 1:1 (x 1, y from start to end), or 0:0, which takes any.
		int group = -1;
		for (int i = 0; i < ratios && group < 0; ++i) {
			size_t r = 6 + static_cast<size_t>(i) * 4;
			if (r + 4 > t.size())
				return;
			int x_ratio = t[r + 1], y_start = t[r + 2], y_end = t[r + 3];
			if ((x_ratio == 0 && y_start == 0 && y_end == 0) || (x_ratio == 1 && y_start <= 1 && y_end >= 1))
				group = u16(6 + static_cast<size_t>(ratios) * 4 + static_cast<size_t>(i) * 2);
		}
		if (group <= 0)
			return;
		int records = u16(static_cast<size_t>(group));
		for (int i = 0; i < records; ++i) {
			size_t e = static_cast<size_t>(group) + 4 + static_cast<size_t>(i) * 6;
			if (e + 6 > t.size())
				break;
			vdmx.push_back({u16(e), s16(e + 2), s16(e + 4)});
		}
	}
};

FontTextRenderer::FontTextRenderer() : m_library(std::make_unique<Library>()) {}

FontTextRenderer::~FontTextRenderer()
{
	// Faces go before the library.
	m_fonts.clear();
	m_regular.reset();
	m_bold.reset();
}

bool FontTextRenderer::Open(const std::string &regular, const std::string &bold, std::string &error)
{
	if (!m_library->library && FT_Init_FreeType(&m_library->library)) {
		error = "FreeType didn't start";
		return false;
	}
	auto load = [&](const std::string &path, std::unique_ptr<Face> &out) -> bool {
		auto face = std::make_unique<Face>();
		if (!ReadFile(path, face->data)) {
			error = "Couldn't read " + path;
			return false;
		}
		if (FT_New_Memory_Face(m_library->library, face->data.data(), static_cast<FT_Long>(face->data.size()), 0,
				&face->face) ||
			!FT_IS_SFNT(face->face)) {
			error = path + " isn't a TrueType font";
			return false;
		}
		face->units_per_em = face->face->units_per_EM ? face->face->units_per_EM : 2048;
		if (auto *os2 = static_cast<TT_OS2 *>(FT_Get_Sfnt_Table(face->face, FT_SFNT_OS2)); os2 && os2->version != 0xffff) {
			face->win_ascent = os2->usWinAscent;
			face->win_descent = os2->usWinDescent;
		} else {
			face->win_ascent = face->face->ascender;
			face->win_descent = -face->face->descender;
		}
		face->ReadVdmx();
		out = std::move(face);
		return true;
	};
	m_fonts.clear();
	m_masks.clear();
	if (!load(regular, m_regular))
		return false;
	m_bold.reset();
	if (!bold.empty() && !load(bold, m_bold))
		return false;
	return true;
}

bool FontTextRenderer::FindArial(std::string &regular, std::string &bold)
{
	std::vector<std::string> dirs;
	if (const char *dir = std::getenv("MYCLIENT_FONT_DIR"))
		dirs.push_back(dir);
	for (const char *var : {"WINDIR", "SystemRoot"})
		if (const char *dir = std::getenv(var))
			dirs.push_back(std::string(dir) + "/Fonts");
	if (const char *home = std::getenv("HOME")) {
		dirs.push_back(std::string(home) + "/.local/share/fonts");
		dirs.push_back(std::string(home) + "/.fonts");
	}
	for (const char *dir : {"/usr/share/fonts/truetype/msttcorefonts", "/usr/share/fonts/TTF",
			 "/usr/share/fonts/truetype/liberation", "/usr/share/fonts/truetype/liberation2",
			 "/usr/share/fonts/liberation-sans", "/usr/share/fonts/liberation", "/usr/local/share/fonts",
			 "/Library/Fonts", "/System/Library/Fonts/Supplemental"})
		dirs.push_back(dir);
	// Regular and bold, side by side in one folder.
	static const std::pair<const char *, const char *> names[] = {
		{"arial.ttf", "arialbd.ttf"},
		{"Arial.ttf", "Arial_Bold.ttf"},
		{"Arial.ttf", "Arial Bold.ttf"},
		{"LiberationSans-Regular.ttf", "LiberationSans-Bold.ttf"},
	};
	for (const auto &dir : dirs)
		for (const auto &[r, b] : names) {
			std::string path = dir + "/" + r;
			if (!Exists(path))
				continue;
			regular = path;
			std::string bold_path = dir + "/" + b;
			bold = Exists(bold_path) ? bold_path : std::string();
			return true;
		}
	return false;
}

std::u32string FontTextRenderer::Decode(const std::string &utf8)
{
	std::u32string out;
	out.reserve(utf8.size());
	const auto *s = reinterpret_cast<const unsigned char *>(utf8.data());
	size_t n = utf8.size();
	for (size_t i = 0; i < n;) {
		unsigned c = s[i];
		int extra = c < 0x80 ? 0 : (c & 0xe0) == 0xc0 ? 1 : (c & 0xf0) == 0xe0 ? 2 : (c & 0xf8) == 0xf0 ? 3 : -1;
		if (extra < 0) {
			out.push_back(0xfffd);
			++i;
			continue;
		}
		char32_t cp = extra == 0 ? c : c & (0x3f >> extra);
		bool ok = true;
		for (int k = 1; k <= extra; ++k) {
			if (i + k >= n || (s[i + k] & 0xc0) != 0x80) {
				ok = false;
				break;
			}
			cp = (cp << 6) | (s[i + k] & 0x3f);
		}
		static const char32_t least[] = {0, 0x80, 0x800, 0x10000};
		if (!ok || cp < least[extra] || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
			out.push_back(0xfffd);
			++i;
			continue;
		}
		out.push_back(cp);
		i += static_cast<size_t>(extra) + 1;
	}
	return out;
}

FontTextRenderer::Font *FontTextRenderer::GetFont(int height, int weight)
{
	if (!m_regular || height == 0)
		return nullptr;
	int bold = weight >= 600 && m_bold ? 1 : 0;
	auto key = std::make_pair(height, bold);
	auto it = m_fonts.find(key);
	if (it != m_fonts.end())
		return &it->second;
	Face *face = bold ? m_bold.get() : m_regular.get();
	Font font;
	font.face = face;
	if (height < 0) {
		font.ppem = -height;
	} else {
		// The cell height: the biggest ppem whose VDMX entry fits it.
		const Face::Vdmx *best = nullptr;
		for (const auto &v : face->vdmx)
			if (v.y_max - v.y_min <= height && (!best || v.ppem > best->ppem))
				best = &v;
		bool past = !face->vdmx.empty() && best && best->ppem == std::max_element(face->vdmx.begin(), face->vdmx.end(),
			[](const auto &a, const auto &b) { return a.ppem < b.ppem; })->ppem;
		if (best && !past) {
			font.ppem = best->ppem;
			font.ascent = best->y_max;
			font.descent = -best->y_min;
		} else {
			// No VDMX entry for it: scale usWinAscent + usWinDescent to the cell, as far as it fits.
			int cell = std::max(1, face->win_ascent + face->win_descent);
			font.ppem = std::max(1, RoundDiv(static_cast<long long>(height) * face->units_per_em, cell));
			while (font.ppem > 1 && RoundDiv(static_cast<long long>(face->win_ascent) * font.ppem, face->units_per_em) +
					RoundDiv(static_cast<long long>(face->win_descent) * font.ppem, face->units_per_em) > height)
				--font.ppem;
		}
	}
	if (font.ascent == 0 && font.descent == 0) {
		const Face::Vdmx *exact = nullptr;
		for (const auto &v : face->vdmx)
			if (v.ppem == font.ppem)
				exact = &v;
		if (exact) {
			font.ascent = exact->y_max;
			font.descent = -exact->y_min;
		} else {
			font.ascent = RoundDiv(static_cast<long long>(face->win_ascent) * font.ppem, face->units_per_em);
			font.descent = RoundDiv(static_cast<long long>(face->win_descent) * font.ppem, face->units_per_em);
		}
	}
	font.height = font.ascent + font.descent;
	return &m_fonts.emplace(key, std::move(font)).first->second;
}

const FontTextRenderer::Glyph &FontTextRenderer::GetGlyph(Font &font, char32_t ch)
{
	auto it = font.glyphs.find(ch);
	if (it != font.glyphs.end())
		return it->second;
	Glyph &glyph = font.glyphs[ch];
	FT_Face face = font.face->face;
	if (FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(font.ppem)))
		return glyph;
	FT_UInt index = FT_Get_Char_Index(face, ch);
	if (FT_Load_Glyph(face, index, FT_LOAD_DEFAULT | FT_LOAD_TARGET_NORMAL))
		return glyph;
	glyph.advance = static_cast<int>((face->glyph->advance.x + 32) >> 6);
	if (face->glyph->format != FT_GLYPH_FORMAT_BITMAP && FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL))
		return glyph;
	++stats.rasterized;
	const FT_Bitmap &bitmap = face->glyph->bitmap;
	glyph.left = face->glyph->bitmap_left;
	glyph.top = face->glyph->bitmap_top;
	glyph.width = static_cast<int>(bitmap.width);
	glyph.rows = static_cast<int>(bitmap.rows);
	glyph.alpha.resize(static_cast<size_t>(glyph.width) * glyph.rows);
	for (int row = 0; row < glyph.rows; ++row) {
		const unsigned char *source = bitmap.buffer + static_cast<ptrdiff_t>(row) * bitmap.pitch;
		uint8_t *out = glyph.alpha.data() + static_cast<size_t>(row) * glyph.width;
		if (bitmap.pixel_mode == FT_PIXEL_MODE_MONO)
			for (int x = 0; x < glyph.width; ++x)
				out[x] = (source[x >> 3] & (0x80 >> (x & 7))) ? 255 : 0;
		else
			std::copy_n(source, glyph.width, out);
	}
	return glyph;
}

const FontTextRenderer::Glyph &FontTextRenderer::CellGlyph(Font &font, char32_t ch)
{
	GetGlyph(font, ch);
	Glyph &glyph = font.glyphs[ch];
	if (glyph.cell_made)
		return glyph;
	glyph.cell_made = true;
	// createGlyphCacheEntry / updateGlyphTexture: the glyph drawn alone at 0, 0 into a cell its
	// advance wide and the font's height tall (what overhangs is cut off), alpha the top four bits.
	int w = std::max(glyph.advance, 0), h = font.height;
	if (w == 0 || h <= 0)
		return glyph;
	glyph.cell.assign(static_cast<size_t>(w) * h, 0);
	int ox = glyph.left, oy = font.ascent - glyph.top;
	for (int row = 0; row < glyph.rows; ++row) {
		int cy = oy + row;
		if (cy < 0 || cy >= h)
			continue;
		for (int x = 0; x < glyph.width; ++x) {
			int cx = ox + x;
			if (cx < 0 || cx >= w)
				continue;
			uint8_t a = glyph.alpha[static_cast<size_t>(row) * glyph.width + x] >> 4;
			glyph.cell[static_cast<size_t>(cy) * w + cx] = static_cast<uint8_t>(a * 17);
			glyph.ink |= a != 0;
		}
	}
	return glyph;
}

int FontTextRenderer::Advance(Font &font, const std::u32string &text, size_t begin, size_t end)
{
	int width = 0;
	for (size_t i = begin; i < end; ++i)
		width += GetGlyph(font, text[i]).advance;
	return width;
}

TextSize FontTextRenderer::Measure(const std::string &utf8, int height, int weight)
{
	Font *font = GetFont(height, weight);
	if (!font)
		return {};
	std::u32string text = Decode(utf8);
	if (weight == 0) // GetTextExtentPoint32: every character's advance, on one line
		return {Advance(*font, text, 0, text.size()), font->height};
	// calculateTextWidth: the sum of the glyphs' widths, the widest line's.
	int width = 0, line = 0;
	for (char32_t ch : text) {
		if (ch == U'\n' || ch == U'\r') {
			line = 0;
			continue;
		}
		line += CellGlyph(*font, ch).advance;
		width = std::max(width, line);
	}
	return {width, font->height};
}

void FontTextRenderer::Blend(Canvas &canvas, const uint8_t *alpha, int stride, int w, int h, int x, int y, uint32_t rgb)
{
	// Each pixel's coverage blends the color in, and hides that much of the view.
	const Rect &clip = canvas.Clip();
	int x0 = std::max(0, clip.x - x), y0 = std::max(0, clip.y - y);
	int x1 = std::min(w, clip.Right() - x), y1 = std::min(h, clip.Bottom() - y);
	const uint32_t tr = (rgb >> 16) & 0xff, tg = (rgb >> 8) & 0xff, tb = rgb & 0xff;
	for (int my = y0; my < y1; ++my) {
		const uint8_t *source = alpha + static_cast<size_t>(my) * stride;
		uint32_t *row = canvas.Row(y + my) + x;
		for (int mx = x0; mx < x1; ++mx) {
			uint32_t a = source[mx];
			if (a == 0)
				continue;
			uint32_t dst = row[mx], keep = 255 - a;
			uint32_t r = (tr * a + ((dst >> 16) & 0xff) * keep) / 255;
			uint32_t g = (tg * a + ((dst >> 8) & 0xff) * keep) / 255;
			uint32_t b = (tb * a + (dst & 0xff) * keep) / 255;
			row[mx] = (((dst >> 24) * keep / 255) << 24) | r << 16 | g << 8 | b;
		}
	}
}

void FontTextRenderer::DrawRun(Canvas &canvas, Font &font, int x, int y, const std::u32string &text, size_t begin,
	size_t end, uint32_t rgb)
{
	const Rect &clip = canvas.Clip();
	for (size_t i = begin; i < end && x < clip.Right(); ++i) {
		const Glyph &glyph = GetGlyph(font, text[i]);
		if (glyph.width > 0 && glyph.rows > 0)
			Blend(canvas, glyph.alpha.data(), glyph.width, glyph.width, glyph.rows, x + glyph.left,
				y + font.ascent - glyph.top, rgb);
		x += glyph.advance;
	}
}

void FontTextRenderer::DrawCells(Canvas &canvas, Font &font, int x, int y, const std::u32string &text, uint32_t rgb)
{
	// renderGlyphWithClipping: each glyph's quad at the pen, a pixel for a texel, blended in by its alpha.
	const Rect &clip = canvas.Clip();
	for (char32_t ch : text) {
		if (x >= clip.Right())
			break;
		const Glyph &glyph = CellGlyph(font, ch);
		if (glyph.ink && x + glyph.advance > clip.x)
			Blend(canvas, glyph.cell.data(), glyph.advance, glyph.advance, font.height, x, y, rgb);
		x += glyph.advance;
	}
}

void FontTextRenderer::Draw(Canvas &canvas, int x, int y, const std::string &utf8, int height, uint32_t rgb, int weight)
{
	TextTimer timer(stats);
	TextSize drawn = Measure(utf8, height, weight);
	const Rect covers{x - 2, y - 2, drawn.w + 6, std::max(drawn.h, height) + 4};
	if (canvas.Recording()) {
		canvas.RecordText(0, utf8, covers, height, rgb, weight, 1);
		return;
	}
	canvas.MarkDrawn(covers);
	Font *font = GetFont(height, weight);
	if (!font)
		return;
	std::u32string text = Decode(utf8);
	if (weight != 0)
		DrawCells(canvas, *font, x, y, text, rgb);
	else
		DrawRun(canvas, *font, x, y, text, 0, text.size(), rgb);
}

const FontTextRenderer::Mask *FontTextRenderer::TextMask(const std::string &utf8, int height, int weight)
{
	std::string key = std::to_string(height) + ',' + std::to_string(weight) + ',' + utf8;
	auto it = m_masks.find(key);
	if (it != m_masks.end())
		return &it->second;
	Font *font = GetFont(height, weight);
	if (!font)
		return nullptr;
	TextSize size = Measure(utf8, height, weight);
	Mask mask;
	mask.width = size.w + 3; // a little slack for overhangs, a pixel of it on the left
	mask.height = size.h;
	if (size.w <= 0 || mask.height <= 0)
		return nullptr;
	// The string whole (TextOutW at 1, 0 into the mask), antialiased.
	mask.alpha.assign(static_cast<size_t>(mask.width) * mask.height, 0);
	std::u32string text = Decode(utf8);
	int pen = 1;
	for (char32_t ch : text) {
		const Glyph &glyph = GetGlyph(*font, ch);
		int gx = pen + glyph.left, gy = font->ascent - glyph.top;
		for (int row = 0; row < glyph.rows; ++row) {
			int my = gy + row;
			if (my < 0 || my >= mask.height)
				continue;
			for (int col = 0; col < glyph.width; ++col) {
				int mx = gx + col;
				if (mx < 0 || mx >= mask.width)
					continue;
				uint8_t &out = mask.alpha[static_cast<size_t>(my) * mask.width + mx];
				out = std::max(out, glyph.alpha[static_cast<size_t>(row) * glyph.width + col]);
			}
		}
		pen += glyph.advance;
	}
	if (m_masks.size() >= 1024)
		m_masks.clear();
	return &m_masks.emplace(std::move(key), std::move(mask)).first->second;
}

void FontTextRenderer::DrawScaled(Canvas &canvas, int x, int y, const std::string &utf8, int height, float scale,
	uint32_t rgb, int weight)
{
	TextTimer timer(stats);
	if (canvas.Recording()) {
		// The coverage (Measure's size and its slack) stretched, from the slack pixel on.
		TextSize size = Measure(utf8, height, weight);
		int slack = static_cast<int>(scale + 0.5f);
		canvas.RecordText(1, utf8,
			{x - slack - 1, y - 1, static_cast<int>((size.w + 3) * scale) + 4, static_cast<int>(size.h * scale) + 4}, height,
			rgb, weight, scale);
		return;
	}
	const Mask *mask = scale > 0 ? TextMask(utf8, height, weight) : nullptr;
	if (!mask)
		return;
	// Stretch the coverage bilinearly, as the glyphs' texture is.
	int width = std::max(1, static_cast<int>(mask->width * scale + 0.5f));
	int rows = std::max(1, static_cast<int>(mask->height * scale + 0.5f));
	m_stretched.assign(static_cast<size_t>(width) * rows, 0);
	auto at = [mask](int sx, int sy) -> int {
		if (sx < 0 || sy < 0 || sx >= mask->width || sy >= mask->height)
			return 0;
		return mask->alpha[static_cast<size_t>(sy) * mask->width + sx];
	};
	for (int dy = 0; dy < rows; ++dy) {
		float fy = (dy + 0.5f) / scale - 0.5f;
		int y0 = static_cast<int>(std::floor(fy));
		float ty = fy - y0;
		for (int dx = 0; dx < width; ++dx) {
			float fx = (dx + 0.5f) / scale - 0.5f;
			int x0 = static_cast<int>(std::floor(fx));
			float tx = fx - x0;
			float top = at(x0, y0) * (1 - tx) + at(x0 + 1, y0) * tx;
			float bottom = at(x0, y0 + 1) * (1 - tx) + at(x0 + 1, y0 + 1) * tx;
			m_stretched[static_cast<size_t>(dy) * width + dx] = static_cast<uint8_t>(top * (1 - ty) + bottom * ty + 0.5f);
		}
	}
	int left = x - static_cast<int>(scale + 0.5f); // the mask's slack pixel, stretched
	canvas.MarkDrawn({left, y, width, rows});
	Blend(canvas, m_stretched.data(), width, width, rows, left, y, rgb);
}

bool FontTextRenderer::Coverage(const std::string &utf8, int height, int weight, std::vector<uint8_t> &alpha, int &w,
	int &h, int &left)
{
	const Mask *mask = TextMask(utf8, height, weight);
	if (!mask)
		return false;
	w = mask->width;
	h = mask->height;
	left = 1;
	alpha = mask->alpha;
	return true;
}

void FontTextRenderer::DrawWrapped(Canvas &canvas, const Rect &area, const std::string &utf8, int height, uint32_t rgb,
	int weight)
{
	TextTimer timer(stats);
	if (canvas.Recording()) {
		canvas.RecordText(2, utf8, {area.x - 2, area.y - 2, area.w + 4, area.h + 4}, height, rgb, weight, 1);
		return;
	}
	canvas.MarkDrawn({area.x - 2, area.y - 2, area.w + 4, area.h + 4});
	Font *font = GetFont(height, weight);
	if (!font)
		return;
	const Rect box{area.x + 2, area.y, area.w - 2, area.h};
	const Rect saved = canvas.Clip();
	Rect clip = Intersect(saved, box);
	if (clip.w <= 0 || clip.h <= 0)
		return;
	canvas.SetClip(clip);
	// DrawText's DT_WORDBREAK: a line ends at "\n" ("\r\n"), or before the word that would take it
	// past the box's right edge; the spaces there are dropped. A word wider than the box is left
	// whole on its own line (cut off by the clip).
	std::u32string text = Decode(utf8);
	auto is_space = [](char32_t c) { return c == U' ' || c == U'\t'; };
	int y = box.y;
	size_t i = 0, n = text.size();
	while (i < n && y < box.Bottom()) {
		size_t line_end = i;
		while (line_end < n && text[line_end] != U'\n' && text[line_end] != U'\r')
			++line_end;
		// Fit words from i onto this line.
		size_t start = i, end = i, pos = i;
		int width = 0;
		bool broke = false;
		while (pos < line_end) {
			size_t word_end = pos;
			while (word_end < line_end && is_space(text[word_end]))
				++word_end;
			while (word_end < line_end && !is_space(text[word_end]))
				++word_end;
			int w = Advance(*font, text, pos, word_end);
			size_t ink_end = word_end; // the word without the spaces before it, for the fit
			int trimmed = width + w;
			if (end > start && trimmed > box.w) {
				broke = true;
				break;
			}
			width = trimmed;
			end = ink_end;
			pos = word_end;
		}
		DrawRun(canvas, *font, box.x, y, text, start, end, rgb);
		y += font->height;
		if (broke) {
			i = end;
			while (i < line_end && is_space(text[i]))
				++i;
			continue;
		}
		i = line_end;
		if (i < n && text[i] == U'\r')
			++i;
		if (i < n && text[i] == U'\n')
			++i;
	}
	canvas.SetClip(saved);
}
