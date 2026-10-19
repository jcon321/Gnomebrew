#include "xmi.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace audio
{
	namespace
	{
		uint32_t BigU32(const uint8_t *p) { return (static_cast<uint32_t>(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

		bool ReadVlq(const uint8_t *data, size_t end, size_t &pos, uint32_t &value)
		{
			value = 0;
			for (int i = 0; i < 4; ++i) {
				if (pos >= end)
					return false;
				uint8_t b = data[pos++];
				value = (value << 7) | (b & 0x7f);
				if (!(b & 0x80))
					return true;
			}
			return false;
		}

		bool ParseEvents(const uint8_t *data, size_t pos, size_t end, XmiSequence &out)
		{
			uint32_t tick = 0;
			bool ended = false;
			while (pos < end && !ended) {
				uint8_t b = data[pos];
				if (b < 0x80) { // a delay: the bytes under 0x80 add up
					tick += b;
					++pos;
					continue;
				}
				++pos;
				uint8_t type = b & 0xf0;
				uint32_t length;
				if (b == 0xff) {
					if (pos >= end)
						return false;
					uint8_t meta = data[pos++];
					if (!ReadVlq(data, end, pos, length) || length > end - pos)
						return false;
					pos += length;
					if (meta == 0x2f) {
						out.end_tick = tick;
						ended = true;
					}
				}
				else if (b == 0xf0 || b == 0xf7) {
					if (!ReadVlq(data, end, pos, length) || length > end - pos)
						return false;
					pos += length;
				}
				else if (type == 0x90) {
					if (pos + 2 > end)
						return false;
					XmiEvent event{tick, static_cast<uint32_t>(b | (data[pos] << 8) | (data[pos + 1] << 16)), 0};
					pos += 2;
					if (!ReadVlq(data, end, pos, event.duration))
						return false;
					out.events.push_back(event);
				}
				else {
					int count = type == 0xc0 || type == 0xd0 ? 1 : 2;
					if (pos + count > end)
						return false;
					uint32_t message = b | (data[pos] << 8);
					if (count == 2)
						message |= data[pos + 1] << 16;
					pos += count;
					out.events.push_back({tick, message, 0});
				}
			}
			if (!ended)
				out.end_tick = tick;
			return true;
		}

		// FORM XMID: its EVNT.
		bool ParseForm(const uint8_t *data, size_t pos, size_t end, std::vector<XmiSequence> &out)
		{
			while (pos + 8 <= end) {
				uint32_t length = BigU32(data + pos + 4);
				size_t body = pos + 8;
				if (length > end - body)
					return false;
				if (memcmp(data + pos, "EVNT", 4) == 0) {
					XmiSequence sequence;
					if (!ParseEvents(data, body, body + length, sequence))
						return false;
					out.push_back(std::move(sequence));
					return true;
				}
				pos = body + length + (length & 1);
			}
			return false;
		}

		void WriteVlq(std::vector<uint8_t> &out, uint32_t value)
		{
			uint8_t bytes[5];
			int n = 0;
			do {
				bytes[n++] = value & 0x7f;
				value >>= 7;
			} while (value && n < 5);
			while (n-- > 0)
				out.push_back(static_cast<uint8_t>(bytes[n] | (n ? 0x80 : 0)));
		}

		constexpr uint8_t ForLoopController = 116;  // Miles: FOR, its value the count (0 forever)
		constexpr uint8_t NextLoopController = 117; // NEXT (64 and up) or BREAK
		constexpr uint8_t DefaultChannelVolume = 100;
	}

	bool ParseXmi(const uint8_t *data, size_t size, std::vector<XmiSequence> &out)
	{
		out.clear();
		size_t pos = 0;
		while (pos + 12 <= size) {
			uint32_t length = BigU32(data + pos + 4);
			size_t body = pos + 8;
			if (length > size - body)
				length = static_cast<uint32_t>(size - body);
			if (memcmp(data + pos, "CAT ", 4) == 0 && memcmp(data + body, "XMID", 4) == 0) {
				size_t at = body + 4, end = body + length;
				while (at + 12 <= end) {
					uint32_t form_length = BigU32(data + at + 4);
					if (form_length > end - at - 8)
						return false;
					if (memcmp(data + at, "FORM", 4) == 0 && memcmp(data + at + 8, "XMID", 4) == 0 &&
						!ParseForm(data, at + 12, at + 8 + form_length, out))
						return false;
					at += 8 + form_length + (form_length & 1);
				}
			}
			else if (memcmp(data + pos, "FORM", 4) == 0 && memcmp(data + body, "XMID", 4) == 0) {
				if (!ParseForm(data, body + 4, body + length, out))
					return false;
			}
			pos = body + length + (length & 1);
		}
		return !out.empty();
	}

	std::vector<uint8_t> ToStandardMidi(const XmiSequence &sequence)
	{
		struct Timed
		{
			uint32_t tick;
			int order; // note-offs first at a tick
			uint32_t message;
		};
		std::vector<Timed> timed;
		uint32_t end = sequence.end_tick;
		for (const auto &event : sequence.events) {
			uint8_t status = event.message & 0xff;
			uint8_t controller = (event.message >> 8) & 0xff;
			if ((status & 0xf0) == 0xb0 && controller >= 110 && controller <= 120)
				continue; // Miles' own controllers
			timed.push_back({event.tick, 1, event.message});
			if ((status & 0xf0) == 0x90 && (event.message >> 16) != 0) {
				uint32_t off = event.tick + event.duration;
				timed.push_back({off, 0, (event.message & 0xff0f) | 0x80});
				end = std::max(end, off);
			}
		}
		std::stable_sort(timed.begin(), timed.end(),
			[](const Timed &a, const Timed &b) { return a.tick != b.tick ? a.tick < b.tick : a.order < b.order; });

		std::vector<uint8_t> track = {0x00, 0xff, 0x51, 0x03, 0x07, 0xa1, 0x20}; // 500000 us a quarter
		uint32_t last = 0;
		for (const auto &event : timed) {
			WriteVlq(track, event.tick - last);
			last = event.tick;
			uint8_t status = event.message & 0xff;
			track.push_back(status);
			track.push_back((event.message >> 8) & 0x7f);
			if ((status & 0xf0) != 0xc0 && (status & 0xf0) != 0xd0)
				track.push_back((event.message >> 16) & 0x7f);
		}
		WriteVlq(track, end - last);
		track.insert(track.end(), {0xff, 0x2f, 0x00});

		std::vector<uint8_t> out = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 60, 'M', 'T', 'r', 'k'};
		uint32_t length = static_cast<uint32_t>(track.size());
		for (int shift = 24; shift >= 0; shift -= 8)
			out.push_back(static_cast<uint8_t>(length >> shift));
		out.insert(out.end(), track.begin(), track.end());
		return out;
	}

	void XmiPlayer::Start(std::shared_ptr<const XmiSequence> sequence, int loops, float volume, const Send &send)
	{
		if (m_sequence)
			Stop(send);
		m_sequence = std::move(sequence);
		m_index = 0;
		m_position = 0;
		m_elapsed = 0;
		m_fraction = 0;
		m_forever = loops <= 0;
		m_loops_left = loops;
		m_volume = volume;
		m_offs.clear();
		m_for.clear();
		// AIL_init_sequence resets the channels.
		for (int channel = 0; channel < 16; ++channel) {
			send(0xb0 | channel | (121 << 8));         // reset all controllers
			send(0xe0 | channel | (0x40 << 16));       // pitch bend centered
			m_channel_volume[channel] = DefaultChannelVolume;
			m_sent_volume[channel] = 0xff;
			SendVolume(channel, send);
		}
		const auto &events = m_sequence->events;
		while (m_sequence && m_index < events.size() && events[m_index].tick == 0)
			Dispatch(events[m_index++], send);
	}

	bool XmiPlayer::Advance(double seconds, const Send &send)
	{
		if (!m_sequence)
			return false;
		double ticks = seconds * XmiTicksPerSecond + m_fraction;
		int steps = static_cast<int>(ticks);
		m_fraction = ticks - steps;
		for (int step = 0; step < steps && m_sequence; ++step) {
			++m_elapsed;
			++m_position;
			for (size_t i = 0; i < m_offs.size();) {
				if (m_offs[i].tick <= m_elapsed) {
					send(0x80 | m_offs[i].channel | (m_offs[i].key << 8));
					m_offs[i] = m_offs.back();
					m_offs.pop_back();
				}
				else
					++i;
			}
			const auto &events = m_sequence->events;
			while (m_index < events.size() && events[m_index].tick <= m_position)
				Dispatch(events[m_index++], send);
			if (m_index >= events.size() && m_position >= m_sequence->end_tick) {
				if (m_forever || --m_loops_left > 0) {
					m_index = 0;
					m_position = 0;
					m_for.clear();
					while (m_index < events.size() && events[m_index].tick == 0)
						Dispatch(events[m_index++], send);
				}
				else
					Stop(send);
			}
		}
		return m_sequence != nullptr;
	}

	void XmiPlayer::Dispatch(const XmiEvent &event, const Send &send)
	{
		uint8_t status = event.message & 0xff;
		uint8_t channel = status & 0x0f;
		uint8_t data1 = (event.message >> 8) & 0x7f, data2 = (event.message >> 16) & 0x7f;
		switch (status & 0xf0) {
		case 0x90:
			send(event.message);
			if (data2 != 0)
				m_offs.push_back({m_elapsed + event.duration, channel, data1});
			return;
		case 0xb0:
			if (data1 == ForLoopController) {
				if (m_for.size() >= 4)
					m_for.erase(m_for.begin());
				m_for.push_back({m_index, event.tick, data2});
				return;
			}
			if (data1 == NextLoopController) {
				if (m_for.empty())
					return;
				ForLoop &loop = m_for.back();
				// (a loop with nothing in it would never let time pass: dropped)
				if (data2 >= 64 && loop.tick != event.tick && (loop.remaining == 0 || --loop.remaining > 0)) {
					m_index = loop.index;
					m_position = loop.tick;
				}
				else
					m_for.pop_back();
				return;
			}
			if (data1 >= 110 && data1 <= 120)
				return; // Miles' channel locks, timbre and patch bank selects, callbacks, branch points
			if (data1 == 7) {
				m_channel_volume[channel] = data2;
				SendVolume(channel, send);
				return;
			}
			send(event.message);
			return;
		default:
			send(event.message);
			return;
		}
	}

	void XmiPlayer::SetVolume(float volume, const Send &send)
	{
		m_volume = std::clamp(volume, 0.0f, 1.0f);
		if (!m_sequence)
			return;
		for (int channel = 0; channel < 16; ++channel)
			SendVolume(channel, send);
	}

	void XmiPlayer::SendVolume(int channel, const Send &send)
	{
		uint8_t value = static_cast<uint8_t>(std::lround(m_channel_volume[channel] * m_volume));
		if (value == m_sent_volume[channel])
			return;
		m_sent_volume[channel] = value;
		send(0xb0 | channel | (7 << 8) | (value << 16));
	}

	void XmiPlayer::NotesOff(const Send &send)
	{
		for (const auto &off : m_offs)
			send(0x80 | off.channel | (off.key << 8));
		m_offs.clear();
		for (int channel = 0; channel < 16; ++channel) {
			send(0xb0 | channel | (64 << 8));  // sustain off
			send(0xb0 | channel | (123 << 8)); // all notes off
		}
	}

	void XmiPlayer::Stop(const Send &send)
	{
		if (!m_sequence)
			return;
		NotesOff(send);
		m_sequence.reset();
	}
}
