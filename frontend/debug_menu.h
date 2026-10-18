#pragma once

// The debug menu: our own overlay in the zone, not part of eqgame's UI. F11 toggles it.
//   Top left, the stat card, in sections that a click on their title folds away:
//     the zone and Norrath's clock (or the camp countdown), and our character;
//     PERFORMANCE frame rate, world render time, triangles, models;
//     MOVEMENT /loc, heading, speed, the floor under us, animation;
//     SERVER SYNC where the zone last heard we are (our latest OP_ClientUpdate) against where we are;
//     CAMERA; NETWORK ping, traffic, resends; PACKETS the latest in and out;
//     ZONE the zone header (fog, clip, sky, gravity, safe point) and the light;
//     RENDER toggles: wireframe, no fog, collision walls, floor marker;
//     TARGET a spawn clicked in the world or in NEARBY (else the nearest), in detail;
//     NEARBY the nearest spawns.
//   Top right, the radar: spawns within RadarRange, north up.
// Frame rates and traffic are averaged over a second, so they read steadily.

#include "canvas.h"
#include "rdp_session.h"
#include "text.h"
#include "widgets.h"
#include "zone_session.h"
#include "zone_view.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

class DebugMenu
{
public:
	bool open = true;

	// Where a frame's CPU time went, ms: the zone's meshes, the character models (posing and
	// skinning), handing the frame to the renderer (its Finish: the GPU upload and draw calls, or
	// the software rasterizer), the name tags, the rest of the UI, and presenting (with Direct3D
	// the canvas upload and the swap, which waits while the GPU is behind).
	struct FrameTimes
	{
		double zone = 0, models = 0, submit = 0, tags = 0, ui = 0, present = 0;
	};
	// Once a frame in the zone, whether or not the menu is open.
	void Frame(const FrameTimes &times);
	void Reset(); // entering a zone: start the averages over, no target

	// `bottom`: where the stat card has to stop (the chat log is below).
	void Draw(Canvas &canvas, TextRenderer &text, const ZoneSession &zone, const ZoneView *view, int bottom);
	// A mouse event in the zone; true when it's on the stat card (clicks there fold sections,
	// flip toggles and pick targets, and shouldn't reach the world).
	bool OnMouse(const InputEvent &event);

	void SelectTarget(uint16_t id) { m_target = id; } // 0: back to the nearest
	const ZoneView::DebugDraw &Drawing() const { return m_draw; }

private:
	enum Section { Performance, Movement, Sync, Camera, Network, Packets, ZoneHeader, Render, Target, Nearby, SectionCount };
	struct Line
	{
		Line(std::string line, int text_height, uint32_t rgb, int below = 3)
			: text(std::move(line)), height(text_height), color(rgb), gap(below)
		{
		}
		std::string text;
		int height;
		uint32_t color;
		int gap = 3; // below
		enum class Action { None, Fold, Toggle, Pick, Vision } action = Action::None;
		int value = 0; // the section, the toggle, the spawn id, or the vision
		std::string right; // right-aligned, dim
		bool highlight = false;
	};
	struct Hit
	{
		int top, bottom;
		Line::Action action;
		int value;
	};

	void DrawCard(Canvas &canvas, TextRenderer &text, const ZoneSession &zone, const ZoneView *view, int bottom);
	void DrawRadar(Canvas &canvas, TextRenderer &text, const ZoneSession &zone);
	void SampleNetwork(const ZoneSession &zone);
	bool *Toggle(int index);

	using Clock = std::chrono::steady_clock;
	Clock::time_point m_last_frame{};
	Clock::time_point m_window_start{};
	int m_window_frames = 0;
	double m_window_ms = 0, m_window_worst_ms = 0;
	FrameTimes m_window_times; // sums over the window
	// The last full second.
	double m_fps = 0, m_frame_ms = 0, m_worst_ms = 0;
	FrameTimes m_times; // averages

	bool m_have_network = false;
	RdpStats m_network;         // the latest
	RdpStats m_network_sample;  // at the start of the second
	Clock::time_point m_network_sample_time{};
	double m_in_rate = 0, m_out_rate = 0; // bytes a second
	double m_packets_in_rate = 0, m_packets_out_rate = 0;

	bool m_folded[SectionCount] = {false, false, true, true, false, true, true, false, false, false};
	ZoneView::DebugDraw m_draw;
	uint16_t m_target = 0;
	Rect m_card{0, 0, 0, 0}; // where the card was drawn
	std::vector<Hit> m_hits;
};
