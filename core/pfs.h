#pragma once

// PFS archive reader (.pak/.s3d). Layout: u32 directory offset, "PFS ", u32 version.
// The directory is u32 count + {crc, offset, size} entries. Each file is a chain
// of {u32 deflated, u32 inflated, zlib data} blocks. The entry with CRC
// 0x61580AC9 (0xFFFFFFFF in TAKP's repacked files) holds the file names, in the same order as the other entries
// sorted by offset.

#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

class PfsArchive
{
public:
	// lazy: read only the directory now and each file from disk when it's asked for, instead of
	// holding the whole archive in memory (the sound packs, snd1..14.pfs, are 113 MB together).
	bool Open(const std::string &path, bool lazy = false);
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
	// `size` bytes at `offset`, from memory or (lazy) the file.
	bool Bytes(size_t offset, size_t size, std::vector<uint8_t> &out) const;
	bool ReadU32At(size_t offset, uint32_t &value) const;

	std::string m_path;
	std::vector<uint8_t> m_data;
	std::unique_ptr<std::ifstream> m_file; // lazy
	size_t m_size = 0;
	std::map<std::string, Entry> m_entries; // lowercase name -> entry
};

std::string ToLower(std::string s);
