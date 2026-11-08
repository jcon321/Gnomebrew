#include "retained_ui.h"

#include <algorithm>
#include <unordered_map>

namespace
{
	bool Empty(const Rect &r)
	{
		return r.w <= 0 || r.h <= 0;
	}

	bool Same(const Rect &a, const Rect &b)
	{
		return (Empty(a) && Empty(b)) || (a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h);
	}

	bool Overlap(const Rect &a, const Rect &b)
	{
		return !Empty(a) && !Empty(b) && a.x < b.Right() && b.x < a.Right() && a.y < b.Bottom() && b.y < a.Bottom();
	}

	Rect Union(const Rect &a, const Rect &b)
	{
		int x0 = std::min(a.x, b.x), y0 = std::min(a.y, b.y);
		return {x0, y0, std::max(a.Right(), b.Right()) - x0, std::max(a.Bottom(), b.Bottom()) - y0};
	}

	// The rects merged where they overlap, so none is cleared and drawn twice.
	void Merge(std::vector<Rect> &rects)
	{
		for (bool merged = true; merged;) {
			merged = false;
			for (size_t i = 0; i < rects.size() && !merged; ++i)
				for (size_t j = i + 1; j < rects.size(); ++j)
					if (Overlap(rects[i], rects[j])) {
						rects[i] = Union(rects[i], rects[j]);
						rects.erase(rects.begin() + static_cast<long>(j));
						merged = true;
						break;
					}
		}
	}
}

void RetainedUi::Forget(Canvas &canvas)
{
	m_valid = false;
	m_kept.clear();
	if (CanvasDirty *dirty = canvas.Tracker())
		dirty->SetRetained(false);
}

void RetainedUi::Frame(Canvas &canvas, const std::vector<Piece> &pieces)
{
	CanvasDirty *dirty = canvas.Tracker();
	const Rect whole{0, 0, canvas.Width(), canvas.Height()};

	// What each piece would draw, and where.
	std::vector<Kept> now;
	now.reserve(pieces.size());
	canvas.ResetLimit();
	for (const Piece &piece : pieces) {
		CanvasRecording recording;
		canvas.Record(&recording);
		canvas.SetAlpha(255);
		canvas.ResetClip();
		piece.draw(canvas);
		canvas.Record(nullptr);
		now.push_back({piece.id, recording.hash, recording.bounds});
	}

	// Where it differs from what's on the canvas.
	std::vector<Rect> damage;
	bool all = !m_valid || !dirty || dirty->ClearedAll();
	if (!all) {
		// The pieces both frames have, in the same order (a window brought to the front changes it:
		// everything's drawn again).
		std::unordered_map<uint64_t, size_t> was;
		for (size_t i = 0; i < m_kept.size(); ++i)
			was[m_kept[i].id] = i;
		size_t last_index = 0;
		bool first = true;
		for (const Kept &piece : now) {
			auto it = was.find(piece.id);
			if (it == was.end()) {
				damage.push_back(piece.bounds);
				continue;
			}
			if (!first && it->second < last_index) {
				all = true;
				break;
			}
			first = false;
			last_index = it->second;
			const Kept &old = m_kept[it->second];
			if (old.hash != piece.hash || !Same(old.bounds, piece.bounds)) {
				damage.push_back(old.bounds);
				damage.push_back(piece.bounds);
			}
			was.erase(it);
		}
		for (const auto &[id, index] : was)
			damage.push_back(m_kept[index].bounds);
	}
	if (all)
		damage = {whole};
	damage.erase(std::remove_if(damage.begin(), damage.end(), Empty), damage.end());
	Merge(damage);

	// Each part cleared to the view and every piece over it drawn again, clipped to it.
	m_redrawn_pixels = m_redrawn_pieces = 0;
	for (const Rect &area : damage) {
		canvas.ClearToView(area);
		canvas.SetLimit(area);
		m_redrawn_pixels += static_cast<size_t>(canvas.Clip().w) * canvas.Clip().h;
		for (size_t i = 0; i < pieces.size(); ++i)
			if (Overlap(now[i].bounds, area)) {
				canvas.SetAlpha(255);
				canvas.ResetClip();
				pieces[i].draw(canvas);
				++m_redrawn_pieces;
			}
	}
	canvas.ResetLimit();
	canvas.SetAlpha(255);

	m_kept = std::move(now);
	m_valid = true;
	if (dirty)
		dirty->SetRetained(true);
}
