#pragma once

#include "bitmap.h"

#include <cstdint>
#include <string>

class Canvas;
class TextRenderer;

// eqgame's loading screen (EQ_LoadingS, CEverQuest_DoLoadScreen and DoLoadScreenProgressBar).
//   - EQ_LoadingS::EQ_LoadingS (@00452912): a 640x480 overlay, the newest expansion's picture
//     CDisplay::LoadBMPFile found an archive for: eqpload.bmp (bmpwad8.s3d), eqlload.bmp
//     (bmpwad7, else bmpwad6), eqvload.bmp (bmpwad5), eqkload.bmp (bmpwad4), else logo03.bmp
//     (bmpwad.s3d).
//   - EQ_LoadingS_draw (@00452a3b): the picture in the middle of the screen (black round it), and
//     while the progress is 1..100 the bar under it: a frame from (179, 465) to (471, 479), its top
//     and left (200, 200, 0) and its bottom and right (100, 100, 0), filled blue (50, 50, 200) from
//     180 for progress * 290 / 100 with a (50, 50, 100) line at its end and black to 470. The text
//     (English only) is Arial 14 at (210, 465), (240, 240, 0) over a black shadow 1 down and right
//     (EQ_LoadingS_WriteTextHD's color 15).
//   - EQ_LoadingS::SetProgressBar (@00452ff7): the progress (at most 100) and "%s..." of what's
//     loading; one time in twenty (and no more than once in 3 s) one of eqgame's jokes in its
//     place ("Attaching Beards to Dwarves...", EQ_LoadingS_SetProgressBar2), kept for 3 s.
// eqgame shows it as the game starts (InitWorld's global files, with the bar) and when entering
// the world from character select (the picture alone). Ours is up whenever a zone loads, the bar
// following our loader (see README, Custom changes).
class LoadingScreen
{
public:
	// The picture by the bmpwad archives there are (EQ_LoadingS::EQ_LoadingS).
	bool Open(const std::string &eq_dir);
	bool Loaded() const { return m_image.width > 0; }
	const std::string &Picture() const { return m_picture; }

	// EQ_LoadingS::SetProgressBar: `what` is the line before its "...", empty for none.
	void SetProgress(int percent, const std::string &what, uint64_t now_ms);
	void Reset(); // a new EQ_LoadingS: no bar, no text
	int Percent() const { return m_percent; }
	const std::string &Text() const { return m_text; }

	// EQ_LoadingS_draw over the whole canvas.
	void Draw(Canvas &canvas, TextRenderer &text) const;

	// The jokes EQ_LoadingS_SetProgressBar2 picks from (0x005ff6a0, 56 of them).
	static const char *const Jokes[];
	static const int JokeCount;

private:
	Image m_image;
	std::string m_picture;
	int m_percent = 0;
	std::string m_text;
	uint32_t m_seed = 1;
	// DAT_0063b334 and DAT_0063b338: a joke is up, and since when.
	bool m_joke = false;
	uint64_t m_joke_ms = 0;
};
