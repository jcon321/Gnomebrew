#include "net_status.h"

#include "canvas.h"
#include "rdp_session.h"
#include "text.h"

#include <cstdio>
#include <string>

namespace
{
	// CDisplay::WriteTextHD2's colors 13, 14 and 15.
	enum class Shade { Red, Green, Yellow };

	uint32_t TextColor(Shade shade)
	{
		return shade == Shade::Red ? 0xf00000 : shade == Shade::Green ? 0x00f000 : 0xf0f000;
	}

	int CountBits(uint64_t v) // LODWORD_countSetBits
	{
		int n = 0;
		for (; v; v >>= 1)
			n += static_cast<int>(v & 1);
		return n;
	}

	float LossPercent(uint64_t history)
	{
		return (1.0f - static_cast<float>(CountBits(history)) * 0.015625f) * 100.0f;
	}

	Shade LossShade(float percent)
	{
		return percent < 35.0f ? Shade::Green : percent < 45.0f ? Shade::Yellow : Shade::Red;
	}

	// DrawPacketLossBar's overlay pixel: A1R5G5B5, as the screen shows it.
	uint32_t From555(int r5, int g5)
	{
		return static_cast<uint32_t>(r5 << 3 | r5 >> 2) << 16 | static_cast<uint32_t>(g5 << 3 | g5 >> 2) << 8;
	}

	// 64 packets a pixel each: a row of ticks every 8 from `tick`, and three rows of the packets that
	// came, the middle one brighter along a run and all of it dimmer at a run's ends.
	void DrawBar(Canvas &canvas, int x, int y, int tick, uint64_t history, bool newest_right, Shade shade)
	{
		bool bits[65] = {};
		for (int i = 0; i < 64; ++i)
			bits[i] = (history >> (newest_right ? 63 - i : i)) & 1;
		if (newest_right)
			tick = 7 - tick;
		int r = shade == Shade::Green ? 0 : 0xff, g = shade == Shade::Red ? 0 : shade == Shade::Green ? 200 : 0xff;
		const uint32_t full = From555(r >> 3, g >> 3);
		const uint32_t end = From555((r * 3 * 32 & 0xfc00) >> 10, (g * 3 & 0x3e0) >> 5); // 3/4
		const uint32_t half = From555((r & 0xf0) >> 4, (g & 0x1f0) >> 4);
		auto pixel = [&](int px, int row, uint32_t color) { canvas.Fill({x + px, y + row, 1, 1}, color); };
		for (int px = tick; px < 64; px += 8)
			pixel(px, 0, full);
		for (int px = 0; px < 64; ++px) {
			if (!bits[px])
				continue;
			bool before = px > 0 && bits[px - 1], after = bits[px + 1];
			uint32_t middle = before && after ? full : before || after ? end : half;
			uint32_t edge = before && after ? end : half;
			pixel(px, 1, edge);
			pixel(px, 2, middle);
			pixel(px, 3, edge);
		}
	}
}

void NetStatus::Draw(Canvas &canvas, TextRenderer &text, int x, int y, const RdpStats &stats, bool texts)
{
	constexpr int Column = 0x20, BarTop = 0xe, Font = 14; // Arial 14 (DAT_0063d3b0)
	auto write = [&](int column, const std::string &s, Shade shade) {
		if (texts)
			text.Draw(canvas, x + column * Column, y, s, Font, TextColor(shade), 0);
	};
	auto number = [](const char *format, auto value) {
		char s[32];
		std::snprintf(s, sizeof s, format, value);
		return std::string(s);
	};
	uint64_t history = stats.sequence_history;
	uint32_t sequence = stats.last_sequence;
	if (stats.arrival_age_ms > stats.last_ping_ms + 500) {
		uint32_t missing = (stats.arrival_age_ms - stats.last_ping_ms) / 500;
		history = missing < 64 ? history << missing : 0;
		sequence += missing;
	}
	write(0, number("%3u", stats.last_ping_ms),
		stats.last_ping_ms < 600 ? Shade::Green : stats.last_ping_ms < 900 ? Shade::Yellow : Shade::Red);
	float loss = LossPercent(history);
	Shade shade = LossShade(loss);
	DrawBar(canvas, x, y + BarTop, static_cast<int>((sequence & 0xffff) % 8), history, true, shade);
	write(1, number("%2.1f%%", static_cast<double>(loss)), shade);
	int column = 2;
	if (stats.arrival_age_ms < 15000) {
		loss = LossPercent(stats.sequence_history);
		shade = LossShade(loss);
		write(2, number("%2.1f%%", static_cast<double>(loss)), shade);
		DrawBar(canvas, x + 2 * Column, y + BarTop, stats.last_sequence % 8, stats.sequence_history, false, shade);
		column = 3;
	}
	write(column, number("%3u", stats.input_rate), shade);
}
