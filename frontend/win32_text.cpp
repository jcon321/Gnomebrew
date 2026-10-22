#include "win32_text.h"

#include <algorithm>
#include <cmath>

std::wstring Widen(const std::string &utf8)
{
	if (utf8.empty())
		return {};
	int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
	std::wstring wide(static_cast<size_t>(n), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), n);
	return wide;
}

std::string Narrow(const std::wstring &wide)
{
	if (wide.empty())
		return {};
	int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
	std::string utf8(static_cast<size_t>(n), '\0');
	WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), n, nullptr, nullptr);
	return utf8;
}

GdiTextRenderer::~GdiTextRenderer()
{
	for (auto &entry : m_fonts)
		DeleteObject(entry.second);
	if (m_scratch.dc) {
		SelectObject(m_scratch.dc, m_scratch.old_bitmap);
		DeleteDC(m_scratch.dc);
	}
	if (m_scratch.dib)
		DeleteObject(m_scratch.dib);
}

HFONT GdiTextRenderer::Font(int height, int weight, bool antialiased)
{
	auto key = std::make_pair(height, weight + (antialiased ? 0x10000 : 0));
	auto it = m_fonts.find(key);
	if (it != m_fonts.end())
		return it->second;

	HFONT font = CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS, antialiased ? ANTIALIASED_QUALITY : DEFAULT_QUALITY, DEFAULT_PITCH, L"Arial");
	m_fonts.emplace(key, font);
	return font;
}

void GdiTextRenderer::AddSurface(HDC dc, uint32_t *pixels, int stride, int height)
{
	m_surfaces.push_back({dc, pixels, stride, height});
}

const GdiTextRenderer::Surface *GdiTextRenderer::Target(Canvas &canvas, int &x, int &y)
{
	const uint32_t *origin = canvas.Row(0);
	for (const auto &surface : m_surfaces) {
		if (origin >= surface.pixels && origin < surface.pixels + static_cast<size_t>(surface.stride) * surface.height) {
			ptrdiff_t offset = origin - surface.pixels;
			x += static_cast<int>(offset % surface.stride);
			y += static_cast<int>(offset / surface.stride);
			return &surface;
		}
	}
	return nullptr;
}

bool GdiTextRenderer::ReserveScratch(int width, int height)
{
	if (width <= m_scratch.width && height <= m_scratch.height)
		return true;
	width = std::max(width, m_scratch.width);
	height = std::max(height, m_scratch.height);
	if (!m_scratch.dc) {
		HDC screen = GetDC(nullptr);
		m_scratch.dc = CreateCompatibleDC(screen);
		ReleaseDC(nullptr, screen);
		if (!m_scratch.dc)
			return false;
	}
	BITMAPINFO info{};
	info.bmiHeader.biSize = sizeof(info.bmiHeader);
	info.bmiHeader.biWidth = width;
	info.bmiHeader.biHeight = -height; // top-down
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	info.bmiHeader.biCompression = BI_RGB;
	void *bits = nullptr;
	HBITMAP dib = CreateDIBSection(m_scratch.dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
	if (!dib || !bits)
		return false;
	HGDIOBJ previous = SelectObject(m_scratch.dc, dib);
	if (m_scratch.dib)
		DeleteObject(m_scratch.dib);
	else
		m_scratch.old_bitmap = previous;
	m_scratch.dib = dib;
	m_scratch.pixels = static_cast<uint32_t *>(bits);
	m_scratch.width = width;
	m_scratch.height = height;
	return true;
}

bool GdiTextRenderer::ShowsView(const Surface &surface, const RECT &box) const
{
	int x0 = std::max<int>(box.left, 0), y0 = std::max<int>(box.top, 0);
	int x1 = std::min<int>(box.right, surface.stride), y1 = std::min<int>(box.bottom, surface.height);
	for (int y = y0; y < y1; ++y) {
		const uint32_t *row = surface.pixels + static_cast<size_t>(y) * surface.stride;
		for (int x = x0; x < x1; ++x)
			if (row[x] >> 24)
				return true;
	}
	return false;
}

void GdiTextRenderer::BlendMask(const Surface &surface, const uint32_t *mask, int mask_stride, int width, int height,
	int x, int y, uint32_t rgb)
{
	// The mask is white text on black: each pixel's coverage (per channel with ClearType). Blend
	// the color in by it, and hide that much of the view.
	const uint32_t text[3] = {(rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff};
	int x0 = std::max(0, -x), y0 = std::max(0, -y);
	int x1 = std::min(width, surface.stride - x), y1 = std::min(height, surface.height - y);
	for (int my = y0; my < y1; ++my) {
		const uint32_t *source = mask + static_cast<size_t>(my) * mask_stride;
		uint32_t *row = surface.pixels + static_cast<size_t>(y + my) * surface.stride + x;
		for (int mx = x0; mx < x1; ++mx) {
			uint32_t m = source[mx] & 0xffffff;
			if (m == 0)
				continue;
			uint32_t dst = row[mx], out = 0, most = 0;
			for (int c = 0; c < 3; ++c) {
				int shift = 16 - c * 8;
				uint32_t coverage = (m >> shift) & 0xff;
				most = std::max(most, coverage);
				out |= ((text[c] * coverage + ((dst >> shift) & 0xff) * (255 - coverage)) / 255) << shift;
			}
			row[mx] = ((((dst >> 24) * (255 - most)) / 255) << 24) | out;
		}
	}
}

void GdiTextRenderer::Output(const Surface &surface, RECT box, uint32_t rgb, HFONT font,
	const std::function<void(HDC, int, int)> &draw)
{
	box.left = std::max<LONG>(box.left, 0);
	box.top = std::max<LONG>(box.top, 0);
	box.right = std::min<LONG>(box.right, surface.stride);
	box.bottom = std::min<LONG>(box.bottom, surface.height);
	int width = box.right - box.left, height = box.bottom - box.top;
	if (width <= 0 || height <= 0)
		return;
	if (!ShowsView(surface, box) || !ReserveScratch(width, height)) {
		SelectObject(surface.dc, font);
		SetTextColor(surface.dc, RGB((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff));
		SetBkMode(surface.dc, TRANSPARENT);
		draw(surface.dc, 0, 0);
		GdiFlush(); // the canvas is the DIB's memory; finish before the next CPU blit
		return;
	}
	for (int y = 0; y < height; ++y)
		std::fill_n(m_scratch.pixels + static_cast<size_t>(y) * m_scratch.width, width, 0u);
	SelectObject(m_scratch.dc, font);
	SetTextColor(m_scratch.dc, RGB(255, 255, 255));
	SetBkMode(m_scratch.dc, TRANSPARENT);
	draw(m_scratch.dc, -box.left, -box.top);
	GdiFlush();
	BlendMask(surface, m_scratch.pixels, m_scratch.width, width, height, box.left, box.top, rgb);
}

TextSize GdiTextRenderer::Measure(const std::string &utf8, int height, int weight)
{
	if (m_surfaces.empty())
		return {};
	std::string key = TextKey(utf8, height, weight);
	auto it = m_sizes.find(key);
	if (it != m_sizes.end())
		return it->second;
	HDC dc = m_surfaces.front().dc;
	std::wstring text = Widen(utf8);
	SelectObject(dc, Font(height, weight));
	SIZE size{};
	GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
	if (m_sizes.size() >= 4096)
		m_sizes.clear();
	return m_sizes[key] = {size.cx, size.cy};
}

std::string GdiTextRenderer::TextKey(const std::string &utf8, int height, int weight)
{
	return std::to_string(height) + ',' + std::to_string(weight) + ',' + utf8;
}

void GdiTextRenderer::Draw(Canvas &canvas, int x, int y, const std::string &utf8, int height, uint32_t rgb, int weight)
{
	const Surface *surface = Target(canvas, x, y);
	if (!surface)
		return;
	TextSize size = Measure(utf8, height, weight);
	RECT box{x - 1, y, x + size.w + 2, y + size.h}; // a little slack for overhangs
	if (!ShowsView(*surface, box)) {
		std::wstring text = Widen(utf8);
		SelectObject(surface->dc, Font(height, weight));
		SetTextColor(surface->dc, RGB((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff));
		SetBkMode(surface->dc, TRANSPARENT);
		SetTextAlign(surface->dc, TA_LEFT | TA_TOP);
		TextOutW(surface->dc, x, y, text.c_str(), static_cast<int>(text.size()));
		GdiFlush(); // the canvas is the DIB's memory; finish before the next CPU blit
		return;
	}

	// Over the view: the text's coverage, kept per string (name tags and chat lines come back
	// every frame), blended in.
	if (const Mask *mask = TextMask(utf8, height, weight, false))
		BlendMask(*surface, mask->coverage.data(), mask->width, mask->width, mask->height, box.left, box.top, rgb);
}

const GdiTextRenderer::Mask *GdiTextRenderer::TextMask(const std::string &utf8, int height, int weight, bool antialiased)
{
	std::string key = (antialiased ? "aa," : "") + TextKey(utf8, height, weight);
	auto it = m_masks.find(key);
	if (it != m_masks.end())
		return &it->second;
	TextSize size = Measure(utf8, height, weight);
	Mask mask;
	mask.width = size.w + 3; // a little slack for overhangs, a pixel of it on the left
	mask.height = size.h;
	if (mask.width <= 0 || mask.height <= 0 || !ReserveScratch(mask.width, mask.height))
		return nullptr;
	for (int row = 0; row < mask.height; ++row)
		std::fill_n(m_scratch.pixels + static_cast<size_t>(row) * m_scratch.width, mask.width, 0u);
	std::wstring text = Widen(utf8);
	SelectObject(m_scratch.dc, Font(height, weight, antialiased));
	SetTextColor(m_scratch.dc, RGB(255, 255, 255));
	SetBkMode(m_scratch.dc, TRANSPARENT);
	SetTextAlign(m_scratch.dc, TA_LEFT | TA_TOP);
	TextOutW(m_scratch.dc, 1, 0, text.c_str(), static_cast<int>(text.size()));
	GdiFlush();
	mask.coverage.resize(static_cast<size_t>(mask.width) * mask.height);
	for (int row = 0; row < mask.height; ++row)
		std::copy_n(m_scratch.pixels + static_cast<size_t>(row) * m_scratch.width, mask.width,
			mask.coverage.begin() + static_cast<size_t>(row) * mask.width);
	if (m_masks.size() >= 1024)
		m_masks.clear();
	return &m_masks.emplace(key, std::move(mask)).first->second;
}

void GdiTextRenderer::DrawScaled(Canvas &canvas, int x, int y, const std::string &utf8, int height, float scale,
	uint32_t rgb, int weight)
{
	const Surface *surface = Target(canvas, x, y);
	const Mask *mask = surface && scale > 0 ? TextMask(utf8, height, weight, true) : nullptr;
	if (!mask)
		return;
	// Stretch the coverage (grey: antialiased, not ClearType) bilinearly, as the glyphs' texture is.
	int width = std::max(1, static_cast<int>(mask->width * scale + 0.5f));
	int rows = std::max(1, static_cast<int>(mask->height * scale + 0.5f));
	m_stretched.assign(static_cast<size_t>(width) * rows, 0);
	auto at = [mask](int sx, int sy) -> int {
		if (sx < 0 || sy < 0 || sx >= mask->width || sy >= mask->height)
			return 0;
		return (mask->coverage[static_cast<size_t>(sy) * mask->width + sx] >> 8) & 0xff;
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
			uint32_t c = static_cast<uint32_t>(top * (1 - ty) + bottom * ty + 0.5f);
			m_stretched[static_cast<size_t>(dy) * width + dx] = c << 16 | c << 8 | c;
		}
	}
	int left = x - static_cast<int>(scale + 0.5f); // the mask's slack pixel, stretched
	BlendMask(*surface, m_stretched.data(), width, width, rows, left, y, rgb);
}

void GdiTextRenderer::DrawWrapped(Canvas &canvas, const Rect &area, const std::string &utf8, int height, uint32_t rgb, int weight)
{
	int x = area.x, y = area.y;
	const Surface *surface = Target(canvas, x, y);
	if (!surface)
		return;
	std::wstring text = Widen(utf8);
	RECT box{x + 2, y, x + area.w, y + area.h};
	Output(*surface, box, rgb, Font(height, weight), [&](HDC dc, int dx, int dy) {
		RECT rect{box.left + dx, box.top + dy, box.right + dx, box.bottom + dy};
		DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
	});
}
