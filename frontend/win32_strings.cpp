#include "win32_strings.h"

#include <windows.h>

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
