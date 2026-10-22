#include "packet_log.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

namespace
{
	constexpr char Magic[8] = {'E', 'Q', 'R', 'E', 'P', 'L', 'A', 'Y'};
	constexpr uint32_t Version = 1;

	uint64_t NowMs()
	{
		return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
	}

	void MakeDir(const std::string &dir)
	{
#ifdef _WIN32
		_mkdir(dir.c_str());
#else
		mkdir(dir.c_str(), 0755);
#endif
	}

	void WriteString(FILE *f, const std::string &s)
	{
		uint16_t length = static_cast<uint16_t>(std::min<size_t>(s.size(), 0xffff));
		fwrite(&length, 2, 1, f);
		fwrite(s.data(), 1, length, f);
	}

	template <typename T>
	bool ReadValue(FILE *f, T &value)
	{
		return fread(&value, sizeof(T), 1, f) == 1;
	}

	bool ReadString(FILE *f, std::string &s)
	{
		uint16_t length;
		if (!ReadValue(f, length))
			return false;
		s.resize(length);
		return length == 0 || fread(&s[0], 1, length, f) == length;
	}
}

bool PacketLogWriter::Open(const std::string &dir, const std::string &character, const std::string &peer, std::string &error)
{
	Close();
	MakeDir(dir);
	time_t now = time(nullptr);
	struct tm local{};
#ifdef _WIN32
	localtime_s(&local, &now);
#else
	localtime_r(&now, &local);
#endif
	char stamp[32];
	strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);
	m_path = dir + "/" + character + "-" + stamp + ".eqr";
	m_file = fopen(m_path.c_str(), "wb");
	if (!m_file) {
		error = "can't write " + m_path;
		return false;
	}
	m_start_ms = NowMs();
	uint64_t started = static_cast<uint64_t>(now);
	fwrite(Magic, 1, sizeof(Magic), m_file);
	fwrite(&Version, 4, 1, m_file);
	fwrite(&started, 8, 1, m_file);
	WriteString(m_file, character);
	WriteString(m_file, peer);
	fflush(m_file);
	return true;
}

void PacketLogWriter::Write(bool outgoing, uint16_t opcode, const void *data, size_t size)
{
	if (!m_file)
		return;
	uint32_t ms = static_cast<uint32_t>(NowMs() - m_start_ms);
	uint8_t direction = outgoing ? 1 : 0;
	uint32_t length = static_cast<uint32_t>(size);
	fwrite(&ms, 4, 1, m_file);
	fwrite(&direction, 1, 1, m_file);
	fwrite(&opcode, 2, 1, m_file);
	fwrite(&length, 4, 1, m_file);
	if (size)
		fwrite(data, 1, size, m_file);
	fflush(m_file); // a crash keeps everything up to it
}

void PacketLogWriter::Close()
{
	if (m_file)
		fclose(m_file);
	m_file = nullptr;
}

bool PacketLog::Load(const std::string &path, std::string &error)
{
	FILE *f = fopen(path.c_str(), "rb");
	if (!f) {
		error = "can't open " + path;
		return false;
	}
	char magic[8];
	uint32_t version = 0;
	if (fread(magic, 1, 8, f) != 8 || memcmp(magic, Magic, 8) != 0 || !ReadValue(f, version) || version != Version ||
		!ReadValue(f, started) || !ReadString(f, character) || !ReadString(f, peer)) {
		fclose(f);
		error = path + " isn't a packet log (version 1)";
		return false;
	}
	packets.clear();
	for (;;) {
		LoggedPacket p;
		uint8_t direction;
		uint32_t size;
		if (!ReadValue(f, p.ms) || !ReadValue(f, direction) || !ReadValue(f, p.opcode) || !ReadValue(f, size))
			break;
		p.outgoing = direction != 0;
		p.data.resize(size);
		if (size && fread(p.data.data(), 1, size, f) != size)
			break;
		packets.push_back(std::move(p));
	}
	fclose(f);
	return true;
}
