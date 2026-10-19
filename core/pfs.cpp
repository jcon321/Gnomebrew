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

bool PfsArchive::Open(const std::string &path, bool lazy)
{
	m_path = path;
	m_entries.clear();
	m_data.clear();
	m_file.reset();

	auto file = std::make_unique<std::ifstream>(path, std::ios::binary);
	if (!*file)
		return false;
	if (lazy) {
		file->seekg(0, std::ios::end);
		m_size = static_cast<size_t>(file->tellg());
		m_file = std::move(file);
	}
	else {
		m_data.assign(std::istreambuf_iterator<char>(*file), std::istreambuf_iterator<char>());
		m_size = m_data.size();
	}

	uint32_t dir_offset = 0, count = 0;
	std::vector<uint8_t> magic;
	if (!ReadU32At(0, dir_offset) || !Bytes(4, 4, magic) || memcmp(magic.data(), "PFS ", 4) != 0)
		return false;
	if (!ReadU32At(dir_offset, count))
		return false;

	std::vector<uint8_t> directory;
	if (!Bytes(dir_offset + 4, static_cast<size_t>(count) * 12, directory))
		return false;
	std::vector<Entry> files;
	Entry names_entry{0, 0};
	bool have_names = false;
	for (uint32_t i = 0; i < count; ++i) {
		uint32_t crc, offset, size;
		size_t at = static_cast<size_t>(i) * 12;
		if (!ReadU32(directory, at, crc) || !ReadU32(directory, at + 4, offset) || !ReadU32(directory, at + 8, size))
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

bool PfsArchive::Bytes(size_t offset, size_t size, std::vector<uint8_t> &out) const
{
	if (offset > m_size || size > m_size - offset)
		return false;
	if (!m_file) {
		out.assign(m_data.begin() + static_cast<long>(offset), m_data.begin() + static_cast<long>(offset + size));
		return true;
	}
	out.resize(size);
	m_file->clear();
	m_file->seekg(static_cast<std::streamoff>(offset));
	m_file->read(reinterpret_cast<char *>(out.data()), static_cast<std::streamsize>(size));
	return static_cast<size_t>(m_file->gcount()) == size;
}

bool PfsArchive::ReadU32At(size_t offset, uint32_t &value) const
{
	if (!m_file)
		return ReadU32(m_data, offset, value);
	std::vector<uint8_t> bytes;
	return Bytes(offset, 4, bytes) && ReadU32(bytes, 0, value);
}

bool PfsArchive::ReadEntry(const Entry &entry, std::vector<uint8_t> &out) const
{
	out.clear();
	out.reserve(entry.size);
	size_t pos = entry.offset;
	std::vector<uint8_t> block; // lazy: the compressed block, read from the file
	while (out.size() < entry.size) {
		uint32_t deflated = 0, inflated = 0;
		if (!ReadU32At(pos, deflated) || !ReadU32At(pos + 4, inflated))
			return false;
		pos += 8;
		const uint8_t *data;
		if (m_file) {
			if (!Bytes(pos, deflated, block))
				return false;
			data = block.data();
		}
		else {
			if (pos + deflated > m_data.size())
				return false;
			data = &m_data[pos];
		}
		size_t before = out.size();
		if (!Inflate::Zlib(data, deflated, out) || out.size() - before != inflated)
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
