#include "win32_text.h"

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
}

HFONT GdiTextRenderer::Font(int height, int weight)
{
	auto key = std::make_pair(height, weight);
	auto it = m_fonts.find(key);
	if (it != m_fonts.end())
		return it->second;

	HFONT font = CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Arial");
	m_fonts.emplace(key, font);
	return font;
}

TextSize GdiTextRenderer::Measure(const std::string &utf8, int height, int weight)
{
	std::wstring text = Widen(utf8);
	SelectObject(m_dc, Font(height, weight));
	SIZE size{};
	GetTextExtentPoint32W(m_dc, text.c_str(), static_cast<int>(text.size()), &size);
	return {size.cx, size.cy};
}

void GdiTextRenderer::Draw(Canvas &, int x, int y, const std::string &utf8, int height, uint32_t rgb, int weight)
{
	std::wstring text = Widen(utf8);
	SelectObject(m_dc, Font(height, weight));
	SetTextColor(m_dc, RGB((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff));
	SetBkMode(m_dc, TRANSPARENT);
	SetTextAlign(m_dc, TA_LEFT | TA_TOP);
	TextOutW(m_dc, x, y, text.c_str(), static_cast<int>(text.size()));
	GdiFlush(); // the canvas is the DIB's memory; finish before the next CPU blit
}

void GdiTextRenderer::DrawWrapped(Canvas &, const Rect &area, const std::string &utf8, int height, uint32_t rgb, int weight)
{
	std::wstring text = Widen(utf8);
	SelectObject(m_dc, Font(height, weight));
	SetTextColor(m_dc, RGB((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff));
	SetBkMode(m_dc, TRANSPARENT);
	RECT rect{area.x + 2, area.y, area.x + area.w, area.y + area.h};
	DrawTextW(m_dc, text.c_str(), static_cast<int>(text.size()), &rect, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
	GdiFlush();
}
