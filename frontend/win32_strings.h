#pragma once

// UTF-8 (the front end's strings) to and from UTF-16 (the Win32 W calls).

#include <string>

std::wstring Widen(const std::string &utf8);
std::string Narrow(const std::wstring &wide);
