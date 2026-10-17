#pragma once

// PFS archive reader (.pak/.s3d). Layout: u32 directory offset, "PFS ", u32 version.
// The directory is u32 count + {crc, offset, size} entries. Each file is a chain
// of {u32 deflated, u32 inflated, zlib data} blocks. The entry with CRC
// 0x61580AC9 (0xFFFFFFFF in TAKP's repacked files) holds the file names, in the same order as the other entries
// sorted by offset.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

class PfsArchive
{
public:
	bool Open(const std::string &path);
	bool Read(const std::string &name, std::vector<uint8_t> &out) const; // name is case-insensitive
	bool Contains(const std::string &name) const;
	std::vector<std::string> Names() const;
	const std::string &Path() const { return m_path; }

private:
	struct Entry
	{
		uint32_t offset;
		uint32_t size;
	};

	bool ReadEntry(const Entry &entry, std::vector<uint8_t> &out) const;

	std::string m_path;
	std::vector<uint8_t> m_data;
	std::map<std::string, Entry> m_entries; // lowercase name -> entry
};

std::string ToLower(std::string s);
