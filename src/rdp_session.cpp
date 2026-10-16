#include "rdp_session.h"
#include "opcodes.h"

#include <chrono>
#include <cstdio>
#include <cstring>

namespace
{
	constexpr uint32_t ClientStream = 1;         // send_message/OpCode passes stream 1
	constexpr uint32_t ClientDataRate = 0x1400;  // net_connect: connection_set_max_data_rate
	constexpr uint32_t ClientSendBuffer = 0x40000; // net_connect: connection_set_send_buffer_size
}

RdpSession::RdpSession() = default;

RdpSession::~RdpSession()
{
	Close(0);
	if (m_endpoint)
		rdplib_endpoint_destroy(m_endpoint);
	if (m_runtime)
		rdplib_runtime_destroy(m_runtime);
}

bool RdpSession::Connect(const std::string &host, uint16_t port)
{
	m_peer = host + ":" + std::to_string(port);

	int result = rdplib_runtime_create(&m_runtime, 1024 * 1024);
	if (result != RDPLIB_OK) {
		fprintf(stderr, "[net] rdplib_runtime_create failed: %d\n", result);
		return false;
	}

	// Local port 0 = ephemeral, like the client.
	result = rdplib_endpoint_create(m_runtime, &m_endpoint, 0, 1, RDPLIB_USE_CRC);
	if (result != RDPLIB_OK) {
		fprintf(stderr, "[net] rdplib_endpoint_create failed: %d\n", result);
		return false;
	}

	result = rdplib_connect(m_endpoint, &m_connection, host.c_str(), port);
	if (result != RDPLIB_OK || m_connection == nullptr) {
		fprintf(stderr, "[net] rdplib_connect %s failed: %d\n", m_peer.c_str(), result);
		return false;
	}

	rdplib_connection_set_data_rate(m_connection, ClientDataRate);
	rdplib_connection_set_send_buffer_size(m_connection, ClientSendBuffer);
	printf("[net] RDP connection to %s created\n", m_peer.c_str());
	return true;
}

bool RdpSession::Send(uint16_t opcode, const void *body, size_t size, bool reliable)
{
	if (!m_connection || m_closed)
		return false;

	std::vector<uint8_t> payload(size + 2);
	payload[0] = static_cast<uint8_t>(opcode);
	payload[1] = static_cast<uint8_t>(opcode >> 8);
	if (size)
		memcpy(payload.data() + 2, body, size);

	int result = rdplib_connection_send(m_connection, payload.data(), static_cast<uint32_t>(payload.size()), ClientStream,
		reliable ? RDPLIB_SEND_RELIABLE : RDPLIB_SEND_UNRELIABLE);
	if (result != RDPLIB_OK) {
		fprintf(stderr, "[net] send 0x%04x failed: %d\n", opcode, result);
		return false;
	}

	// Keepalive can only be enabled once our direction is established by a reliable SYN.
	if (reliable && !m_keepalive_on)
		m_keepalive_on = rdplib_connection_enable_keepalive(m_connection) == RDPLIB_OK;

	return true;
}

bool RdpSession::PopOne(AppPacket &out, Status &status)
{
	for (;;) {
		rdplib_message_t *message = rdplib_connection_pop_message(m_connection);
		if (!message)
			return false;

		if (rdplib_message_is_disconnect(message)) {
			rdplib_disconnect_info_t info{};
			if (rdplib_connection_get_disconnect_info(m_connection, &info) == RDPLIB_OK)
				m_disconnect_reason = info.reason;
			rdplib_message_release(message);
			m_closed = true;
			status = Status::Closed;
			return true;
		}

		bool fin = rdplib_message_has_fin(message) != 0;
		uint32_t bytes = rdplib_message_size(message);
		auto data = static_cast<const uint8_t *>(rdplib_message_data(message));

		if (bytes >= 2 && data) {
			out.opcode = static_cast<uint16_t>(data[0] | (data[1] << 8));
			out.data.assign(data + 2, data + bytes);
			rdplib_message_release(message);
			if (fin)
				m_closed = true;
			status = Status::Packet;
			return true;
		}

		rdplib_message_release(message);
		if (fin) {
			m_closed = true;
			status = Status::Closed;
			return true;
		}
	}
}

RdpSession::Status RdpSession::Receive(AppPacket &out, int timeout_ms)
{
	if (!m_connection)
		return Status::Closed;

	auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
	for (;;) {
		Status status;
		if (PopOne(out, status))
			return status;
		if (m_closed)
			return Status::Closed;

		auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
		if (left <= 0)
			return Status::Timeout;

		rdplib_endpoint_process(m_endpoint, static_cast<int32_t>(left < 50 ? left : 50));
		while (rdplib_message_t *stray = rdplib_endpoint_pop_connectionless(m_endpoint))
			rdplib_message_release(stray);
	}
}

void RdpSession::Close(uint32_t linger_ms)
{
	if (!m_connection)
		return;

	while (rdplib_message_t *message = rdplib_connection_pop_message(m_connection))
		rdplib_message_release(message);

	rdplib_connection_begin_close(m_connection, linger_ms);
	rdplib_connection_release(m_connection);
	m_connection = nullptr;
}

const char *OpcodeName(uint16_t opcode, bool world)
{
	const OpcodeEntry *table = world ? WorldOpcodes : LoginOpcodes;
	for (; table->name; ++table)
		if (table->value == opcode)
			return table->name;
	return "?";
}

void HexDump(const std::vector<uint8_t> &data, size_t max_bytes)
{
	size_t n = data.size() < max_bytes ? data.size() : max_bytes;
	for (size_t row = 0; row < n; row += 16) {
		printf("    %04zx: ", row);
		for (size_t i = 0; i < 16; ++i) {
			if (row + i < n)
				printf("%02x ", data[row + i]);
			else
				printf("   ");
		}
		printf(" ");
		for (size_t i = 0; i < 16 && row + i < n; ++i) {
			uint8_t c = data[row + i];
			putchar(c >= 0x20 && c < 0x7f ? c : '.');
		}
		putchar('\n');
	}
	if (n < data.size())
		printf("    ... (%zu more bytes)\n", data.size() - n);
}
