#include "pfs.h"
#include "inflate.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

namespace
{
	constexpr uint32_t NameDirectoryCrc = 0x61580AC9;
	constexpr uint32_t RepackedNameDirectoryCrc = 0xFFFFFFFF; // TAKP's repacked archives (sky.s3d)

	bool ReadU32(const std::vector<uint8_t> &data, size_t offset, uint32_t &value)
	{
		if (offset + 4 > data.size())
			return false;
		value = static_cast<uint32_t>(data[offset]) | (static_cast<uint32_t>(data[offset + 1]) << 8) |
		        (static_cast<uint32_t>(data[offset + 2]) << 16) | (static_cast<uint32_t>(data[offset + 3]) << 24);
		return true;
	}
}

std::string ToLower(std::string s)
{
	for (auto &c : s)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

bool PfsArchive::Open(const std::string &path)
{
	m_path = path;
	m_entries.clear();

	std::ifstream file(path, std::ios::binary);
	if (!file)
		return false;
	m_data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());

	uint32_t dir_offset = 0, count = 0;
	if (!ReadU32(m_data, 0, dir_offset) || m_data.size() < 12 || memcmp(&m_data[4], "PFS ", 4) != 0)
		return false;
	if (!ReadU32(m_data, dir_offset, count))
		return false;

	std::vector<Entry> files;
	Entry names_entry{0, 0};
	bool have_names = false;
	for (uint32_t i = 0; i < count; ++i) {
		uint32_t crc, offset, size;
		size_t at = dir_offset + 4 + static_cast<size_t>(i) * 12;
		if (!ReadU32(m_data, at, crc) || !ReadU32(m_data, at + 4, offset) || !ReadU32(m_data, at + 8, size))
			return false;
		if (crc == NameDirectoryCrc || crc == RepackedNameDirectoryCrc) {
			names_entry = {offset, size};
			have_names = true;
		}
		else
			files.push_back({offset, size});
	}
	if (!have_names)
		return false;

	std::sort(files.begin(), files.end(), [](const Entry &a, const Entry &b) { return a.offset < b.offset; });

	std::vector<uint8_t> names;
	uint32_t name_count = 0;
	if (!ReadEntry(names_entry, names) || !ReadU32(names, 0, name_count))
		return false;

	size_t pos = 4;
	for (uint32_t i = 0; i < name_count && i < files.size(); ++i) {
		uint32_t length = 0;
		if (!ReadU32(names, pos, length) || pos + 4 + length > names.size())
			return false;
		std::string name(reinterpret_cast<const char *>(&names[pos + 4]), length);
		pos += 4 + length;
		name.erase(std::find(name.begin(), name.end(), '\0'), name.end());
		m_entries[ToLower(name)] = files[i];
	}
	return true;
}

bool PfsArchive::ReadEntry(const Entry &entry, std::vector<uint8_t> &out) const
{
	out.clear();
	out.reserve(entry.size);
	size_t pos = entry.offset;
	while (out.size() < entry.size) {
		uint32_t deflated = 0, inflated = 0;
		if (!ReadU32(m_data, pos, deflated) || !ReadU32(m_data, pos + 4, inflated))
			return false;
		pos += 8;
		if (pos + deflated > m_data.size())
			return false;
		size_t before = out.size();
		if (!Inflate::Zlib(&m_data[pos], deflated, out) || out.size() - before != inflated)
			return false;
		pos += deflated;
	}
	return out.size() == entry.size;
}

bool PfsArchive::Read(const std::string &name, std::vector<uint8_t> &out) const
{
	auto it = m_entries.find(ToLower(name));
	return it != m_entries.end() && ReadEntry(it->second, out);
}

bool PfsArchive::Contains(const std::string &name) const
{
	return m_entries.count(ToLower(name)) != 0;
}

std::vector<std::string> PfsArchive::Names() const
{
	std::vector<std::string> names;
	for (const auto &entry : m_entries)
		names.push_back(entry.first);
	return names;
}
