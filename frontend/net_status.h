#pragma once

// eqgame's netstat (ExecuteCmd 30, on by default: __NetStatusToggle): DrawNetStatus (@0054d3ae) and
// DrawPacketLossBar (@0054d1a8) over the top left of the 3D view, from the zone connection's
// connection_get_perf_stats and the endpoint's rdp_get_input_rate:
//   - the last round trip in ms (green under 600, yellow under 900, else red);
//   - how many of the last 64 packets didn't come, in percent (green under 35, yellow under 45,
//     else red), over a bar of the 64, the newest on the right; a packet missing every 500 ms past the
//     last round trip and 500 ms since anything came counts as lost;
//   - unless nothing came for 15 s, the same without those, its bar the newest on the left;
//   - the bytes a second coming in, in the color before it.
// It's taken anew only in the frames a packet came since the last time, or every frame once none
// has for 1 s. The bars are in a 128x32 overlay (DAT_007f9514), which stays up; eqgame's texts are
// deferred only in those frames, so they blink out between packets. Ours stay up as the bars do.

#include <cstdint>

class Canvas;
class TextRenderer;
struct RdpStats;

namespace NetStatus
{
	constexpr int RefreshMs = 1000;
	// At (x, y), DrawNetStatus's 5 in from the view's corner.
	void Draw(Canvas &canvas, TextRenderer &text, int x, int y, const RdpStats &stats);
}
