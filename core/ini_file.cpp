#include "ini_file.h"

#include <cctype>
#include <cstdlib>
#include <fstream>

namespace
{
	bool SameText(const std::string &a, const std::string &b)
	{
		if (a.size() != b.size())
			return false;
		for (size_t i = 0; i < a.size(); ++i)
			if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
				return false;
		return true;
	}

	std::string Trim(const std::string &s)
	{
		size_t start = s.find_first_not_of(" \t\r\n");
		if (start == std::string::npos)
			return "";
		size_t end = s.find_last_not_of(" \t\r\n");
		return s.substr(start, end - start + 1);
	}
}

bool IniFile::Load(const std::string &path)
{
	m_sections.clear();
	std::ifstream file(path);
	if (!file)
		return false;
	std::string line;
	while (std::getline(file, line)) {
		line = Trim(line);
		if (line.empty() || line[0] == ';')
			continue;
		if (line[0] == '[') {
			size_t close = line.find(']');
			m_sections.push_back({Trim(line.substr(1, close == std::string::npos ? std::string::npos : close - 1)), {}});
			continue;
		}
		size_t equals = line.find('=');
		if (equals == std::string::npos || m_sections.empty())
			continue;
		m_sections.back().entries.push_back({Trim(line.substr(0, equals)), Trim(line.substr(equals + 1))});
	}
	return true;
}

bool IniFile::Save(const std::string &path) const
{
	std::ofstream file(path, std::ios::trunc);
	if (!file)
		return false;
	for (const auto &section : m_sections) {
		file << '[' << section.name << "]\n";
		for (const auto &entry : section.entries)
			file << entry.key << '=' << entry.value << '\n';
	}
	return static_cast<bool>(file);
}

const IniFile::Entry *IniFile::Find(const std::string &section, const std::string &key) const
{
	for (const auto &s : m_sections)
		if (SameText(s.name, section))
			for (const auto &entry : s.entries)
				if (SameText(entry.key, key))
					return &entry;
	return nullptr;
}

bool IniFile::Has(const std::string &section, const std::string &key) const
{
	return Find(section, key) != nullptr;
}

std::string IniFile::Get(const std::string &section, const std::string &key, const std::string &fallback) const
{
	const Entry *entry = Find(section, key);
	return entry ? entry->value : fallback;
}

int IniFile::GetInt(const std::string &section, const std::string &key, int fallback) const
{
	const Entry *entry = Find(section, key);
	return entry && !entry->value.empty() ? atoi(entry->value.c_str()) : fallback;
}

bool IniFile::GetBool(const std::string &section, const std::string &key, bool fallback) const
{
	const Entry *entry = Find(section, key);
	if (!entry || entry->value.empty())
		return fallback;
	char c = entry->value[0];
	return c == 't' || c == 'T' || c == '1';
}

void IniFile::Set(const std::string &section, const std::string &key, const std::string &value)
{
	Section *target = nullptr;
	for (auto &s : m_sections)
		if (SameText(s.name, section))
			target = &s;
	if (!target) {
		m_sections.push_back({section, {}});
		target = &m_sections.back();
	}
	for (auto &entry : target->entries)
		if (SameText(entry.key, key)) {
			entry.value = value;
			return;
		}
	target->entries.push_back({key, value});
}
