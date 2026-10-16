#pragma once

// One RDP connection carrying EQ application packets, set up the way the
// client's net_connect (eqgame.exe @0054cfb3) does it:
//   rdp_create flags 0x40000002 (CRC + IPv4), stream 1, data rate 0x1400,
//   send buffer 0x40000.  Payload = little-endian uint16 opcode + body.

#include <rdplib.h>

#include <cstdint>
#include <string>
#include <vector>

struct AppPacket
{
	uint16_t opcode = 0;
	std::vector<uint8_t> data;
};

class RdpSession
{
public:
	enum class Status { Packet, Timeout, Closed };

	RdpSession();
	~RdpSession();
	RdpSession(const RdpSession &) = delete;
	RdpSession &operator=(const RdpSession &) = delete;

	bool Connect(const std::string &host, uint16_t port);
	bool Send(uint16_t opcode, const void *body, size_t size, bool reliable = true);
	bool Send(uint16_t opcode, const std::vector<uint8_t> &body, bool reliable = true)
	{
		return Send(opcode, body.data(), body.size(), reliable);
	}

	// Waits up to timeout_ms for the next application packet.
	Status Receive(AppPacket &out, int timeout_ms);

	void Close(uint32_t linger_ms = 1000);
	const std::string &Peer() const { return m_peer; }
	uint32_t DisconnectReason() const { return m_disconnect_reason; }

private:
	bool PopOne(AppPacket &out, Status &status);

	rdplib_runtime_t *m_runtime = nullptr;
	rdplib_endpoint_t *m_endpoint = nullptr;
	rdplib_connection_t *m_connection = nullptr;
	bool m_keepalive_on = false;
	bool m_closed = false;
	uint32_t m_disconnect_reason = 0;
	std::string m_peer;
};

const char *OpcodeName(uint16_t opcode, bool world);
void HexDump(const std::vector<uint8_t> &data, size_t max_bytes = 256);
