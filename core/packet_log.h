#pragma once

// A zone session's packets on disk (.eqr), to play a session back without a server.
// Both directions are kept, as they crossed the wire (before decryption and inflating):
//   header  "EQREPLAY" (8), uint32 version, uint64 unix time it started,
//           uint16 length + character name, uint16 length + the zone's address
//   record  uint32 ms since the start, uint8 direction (0 in, 1 out), uint16 opcode,
//           uint32 size, then the body

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

struct LoggedPacket
{
	uint32_t ms = 0;
	bool outgoing = false;
	uint16_t opcode = 0;
	std::vector<uint8_t> data;
};

class PacketLogWriter
{
public:
	PacketLogWriter() = default;
	~PacketLogWriter() { Close(); }
	PacketLogWriter(const PacketLogWriter &) = delete;
	PacketLogWriter &operator=(const PacketLogWriter &) = delete;

	// Creates `<dir>/<character>-<yyyymmdd-hhmmss>.eqr` (and dir, one level, if it's missing).
	bool Open(const std::string &dir, const std::string &character, const std::string &peer, std::string &error);
	void Write(bool outgoing, uint16_t opcode, const void *data, size_t size);
	void Close();
	bool IsOpen() const { return m_file != nullptr; }
	const std::string &Path() const { return m_path; }

private:
	FILE *m_file = nullptr;
	std::string m_path;
	uint64_t m_start_ms = 0;
};

struct PacketLog
{
	uint64_t started = 0; // unix time
	std::string character;
	std::string peer;
	std::vector<LoggedPacket> packets;

	// A truncated last record (the client didn't close the file) is dropped, not an error.
	bool Load(const std::string &path, std::string &error);
};
