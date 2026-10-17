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

// The connection's health, from rdplib: round trip, and totals since it opened.
struct RdpStats
{
	uint32_t rtt_ms = 0, rtt_deviation_ms = 0;
	uint64_t bytes_in = 0, bytes_out = 0;
	uint64_t packets_in = 0, packets_out = 0;
	uint64_t resent = 0; // reliable packets sent again
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
	// Non-blocking: services the socket once, then returns a queued packet if there is one.
	Status Poll(AppPacket &out) { return Receive(out, 0); }

	void Close(uint32_t linger_ms = 1000);
	const std::string &Peer() const { return m_peer; }
	bool Stats(RdpStats &out) const; // false when not connected
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
const char *ZoneOpcodeName(uint16_t opcode);
void HexDump(const std::vector<uint8_t> &data, size_t max_bytes = 256);
