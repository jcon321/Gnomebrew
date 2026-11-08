#pragma once

// The front end's 2D drawing over a GPU-drawn view, kept on the canvas from frame to frame. The
// Direct3D host composites the canvas over the view, so nothing on it depends on the view: what
// was drawn last frame is still right wherever nothing would draw differently.
//
// Each frame every piece of it (a window, the menus and tooltip, the stat card, the cursor) is
// drawn once into a CanvasRecording, which draws nothing but hashes every call and its arguments.
// A piece whose hash or box differs from last frame's (or that came or went) has its old and new
// boxes cleared to the view and drawn again, with every piece over them, in order and clipped to
// them; the rest of the canvas is left as it is (and isn't uploaded: CanvasDirty).

#include "canvas.h"

#include <cstdint>
#include <functional>
#include <vector>

class RetainedUi
{
public:
	struct Piece
	{
		uint64_t id = 0; // the same piece from frame to frame (a window's address, a fixed number)
		std::function<void(Canvas &)> draw; // leaves the canvas's alpha and clip as it found them
	};

	// Draws `pieces` (in order, the last on top) on `canvas`, a GPU host's tracked canvas (whose
	// Finish cleared its block this frame), redrawing only what changed.
	void Frame(Canvas &canvas, const std::vector<Piece> &pieces);
	// The canvas was drawn some other way this frame: all of it to draw next time.
	void Forget(Canvas &canvas);
	// Of the last Frame: the pixels cleared and drawn again, and the pieces drawn (not recorded).
	size_t RedrawnPixels() const { return m_redrawn_pixels; }
	size_t RedrawnPieces() const { return m_redrawn_pieces; }

private:
	struct Kept
	{
		uint64_t id = 0, hash = 0;
		Rect bounds;
	};
	std::vector<Kept> m_kept; // last frame's pieces, in order
	bool m_valid = false;     // and they're what's on the canvas
	size_t m_redrawn_pixels = 0, m_redrawn_pieces = 0;
};
