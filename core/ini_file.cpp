#include "ini_file.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>

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
	std::string raw;
	while (std::getline(file, raw)) {
		if (!raw.empty() && raw.back() == '\r')
			raw.pop_back();
		std::string line = Trim(raw);
		if (!line.empty() && line[0] == '[') {
			size_t close = line.find(']');
			m_sections.push_back({Trim(line.substr(1, close == std::string::npos ? std::string::npos : close - 1)), {}, true, {}});
			if (m_sections.size() > 1)
				TakeLead(m_sections[m_sections.size() - 2], m_sections.back());
			continue;
		}
		if (m_sections.empty())
			m_sections.push_back({"", {}, false, {}});
		size_t equals = line.find('=');
		if (line.empty() || line[0] == ';' || equals == std::string::npos || equals == 0)
			m_sections.back().entries.push_back({"", "", raw});
		else
			m_sections.back().entries.push_back({Trim(line.substr(0, equals)), Trim(line.substr(equals + 1)), raw});
	}
	return true;
}

bool IniFile::Save(const std::string &path) const
{
	std::ofstream file(path, std::ios::trunc);
	if (!file)
		return false;
	for (const auto &section : m_sections) {
		for (const auto &entry : section.lead)
			file << entry.line << '\n';
		if (section.header)
			file << '[' << section.name << "]\n";
		for (const auto &entry : section.entries) {
			if (entry.key.empty() || !entry.line.empty())
				file << entry.line << '\n';
			else
				file << entry.key << '=' << entry.value << '\n';
		}
	}
	return static_cast<bool>(file);
}

const IniFile::Entry *IniFile::Find(const std::string &section, const std::string &key) const
{
	for (const auto &s : m_sections)
		if (s.header && SameText(s.name, section))
			for (const auto &entry : s.entries)
				if (!entry.key.empty() && SameText(entry.key, key))
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

bool IniFile::IsBlank(const Entry &entry)
{
	return entry.key.empty() && Trim(entry.line).empty();
}

void IniFile::TakeLead(Section &previous, Section &next)
{
	// The comments right above the [section] line, with the blank lines above them; when they
	// touch the line above's [section] (no keys or blank line between), they're that one's.
	auto &entries = previous.entries;
	size_t keys = entries.size(); // after the last key
	while (keys > 0 && entries[keys - 1].key.empty())
		--keys;
	size_t at = entries.size();
	while (at > keys && IsBlank(entries[at - 1]))
		--at;
	size_t comments = at;
	while (comments > keys && !IsBlank(entries[comments - 1]))
		--comments;
	if (comments > keys || keys > 0)
		at = comments;
	while (at > keys && IsBlank(entries[at - 1]))
		--at;
	next.lead.assign(entries.begin() + at, entries.end());
	entries.erase(entries.begin() + at, entries.end());
}

IniFile::Section *IniFile::FindSection(const std::string &section)
{
	for (auto &s : m_sections)
		if (s.header && SameText(s.name, section))
			return &s;
	return nullptr;
}

IniFile::Section &IniFile::MakeSection(const std::string &section, size_t index)
{
	// A blank line between it and the lines above and below.
	auto blank_after = [&](const Section &s) {
		return s.entries.empty() ? s.header || !s.lead.empty() : !IsBlank(s.entries.back());
	};
	Section made{section, {}, true, {}};
	if (index > 0 && blank_after(m_sections[index - 1]))
		made.lead.push_back({"", "", ""});
	if (index < m_sections.size() && (m_sections[index].lead.empty() || !IsBlank(m_sections[index].lead.front())))
		m_sections[index].lead.insert(m_sections[index].lead.begin(), {"", "", ""});
	return *m_sections.insert(m_sections.begin() + index, made);
}

IniFile::Section &IniFile::SectionFor(const std::string &section)
{
	Section *found = FindSection(section);
	return found ? *found : MakeSection(section, m_sections.size());
}

void IniFile::AddSection(const std::string &section, const std::string &after)
{
	if (FindSection(section))
		return;
	size_t index = !m_sections.empty() && !m_sections.front().header ? 1 : 0; // after the lines at the top
	if (!after.empty()) {
		Section *previous = FindSection(after);
		index = previous ? static_cast<size_t>(previous - m_sections.data()) + 1 : m_sections.size();
	}
	MakeSection(section, index);
}

std::vector<IniFile::Entry>::iterator IniFile::InsertPoint(Section &target)
{
	// After the section's last key (the comments above the next [section] line are that one's);
	// with no keys yet, after any comments under the [section] line.
	auto &entries = target.entries;
	auto at = entries.end();
	while (at != entries.begin() && std::prev(at)->key.empty())
		--at;
	if (at == entries.begin())
		for (at = entries.end(); at != entries.begin() && IsBlank(*std::prev(at)); --at) {
		}
	return at;
}

void IniFile::Set(const std::string &section, const std::string &key, const std::string &value)
{
	Section &target = SectionFor(section);
	for (auto &entry : target.entries)
		if (!entry.key.empty() && SameText(entry.key, key)) {
			if (entry.value != value) {
				entry.value = value;
				entry.line.clear();
			}
			return;
		}
	target.entries.insert(InsertPoint(target), {key, value, ""});
}

void IniFile::Remove(const std::string &section, const std::string &key)
{
	if (Section *target = FindSection(section))
		for (auto it = target->entries.begin(); it != target->entries.end(); ++it)
			if (!it->key.empty() && SameText(it->key, key)) {
				target->entries.erase(it);
				return;
			}
}

bool IniFile::Add(const std::string &section, const std::string &key, const std::string &value, const std::string &comment)
{
	if (Has(section, key))
		return false;
	Section &target = SectionFor(section);
	auto at = InsertPoint(target);
	std::vector<Entry> lines;
	if (!comment.empty()) {
		// A blank line between it and what's above, unless that's the [section] line.
		if (at != target.entries.begin() && !IsBlank(*std::prev(at)))
			lines.push_back({"", "", ""});
		size_t start = 0;
		while (start <= comment.size()) {
			size_t end = std::min(comment.find('\n', start), comment.size());
			lines.push_back({"", "", "; " + comment.substr(start, end - start)});
			start = end + 1;
		}
	}
	lines.push_back({key, value, ""});
	target.entries.insert(at, lines.begin(), lines.end());
	return true;
}
