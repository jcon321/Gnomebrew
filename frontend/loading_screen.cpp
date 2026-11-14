#include "loading_screen.h"

#include "canvas.h"
#include "pfs.h"
#include "text.h"

#include <algorithm>
#include <ctime>
#include <vector>

const char *const LoadingScreen::Jokes[] = {
	"Attaching Beards to Dwarves",
	"Stupidificationing Ogres",
	"Ensuring Gnomes Are Still Short",
	"Looking Up Barbarian Kilts",
	"Whacking Trolls With Ugly Stick",
	"Filling Halflings With Pie",
	"Polishing Erudite Foreheads",
	"Hiding Catnip From Vah Shir",
	"Have You Hugged An Iksar Today?",
	"Sanding Wood Elves... now 32% smoother.",
	"Starching High Elf Robes",
	"Warning: Half Elves Are Now .49999 Elves.",
	"If You Squeeze Dark Elves You Don't Get Wine",
	"Stringing Bows",
	"Sharpening Swords",
	"Sharpening Claws",
	"Dusting Off Spellbooks",
	"Adding Vanilla Flavor to Ice Giants",
	"Refreshing Death Touch Ammunition",
	"Teaching Snakes to Kick",
	"Looking For Graphics <LFG>",
	"Always Frisky Kerrans <AFK>",
	"Now Spawning Fippy_Darkpaw_432,326,312",
	"Buy:LurN Tu Tok liK Da OgUr iN Ayt DaYz by OG",
	"DING!",
	"Oiling Clockworks",
	"Ruining My Own Lands",
	"Isn't It About Time You Washed Your Armor?",
	"Wonder If Phinegal Autropos Tires of Seafood?",
	"Bristlebane Was Here",
	"Why Do Clerics Always Burn the Bread?",
	"In The Grey, No One Can Hear You Scream",
	"Delivering the Lion Meat to Halas",
	"You Have Gotten Better At Loading! (13)",
	"Don't Panic",
	"Have You Tried Batwing Crunchies Cereal?",
	"Preparing to Spin You Around Rapidly",
	"Does Anyone Actually Read This?",
	"Doing Something You Don't Wanna Know About",
	"Look Out Behind You",
	"Creating Randomly Generated Feature",
	"Checking Anti-Camp Radius",
	"Spawning Your_Characters01",
	"Adding Randomly Mispeled Words Into Text",
	"Loading, Don't Wait If You Don't Want To",
	"Ensuring Everything Works Perfektly",
	"Hitting Your Keyboard Won't Make This Faster",
	"Told You It Wasn't Made of Cheese",
	"Generating Plans for Faster-Than-Light Travel",
	"Doing The Impossible",
	"Grrr.  Bark.  Bark.  Grrr.",
	"Karnor's... Over 40 Billion Trains Served",
	"Honk if You Eat Gnomes",
	"Pizza...The Other Other White Meat",
	"Outfitting Pigs With Wings",
	"You Have Gotten Better At Fizzling! (45)",
};
const int LoadingScreen::JokeCount = static_cast<int>(sizeof(Jokes) / sizeof(Jokes[0]));

namespace
{
	constexpr int Width = 640, Height = 480;
}

bool LoadingScreen::Open(const std::string &eq_dir)
{
	// CDisplay::LoadBMPFile opens bmpwad.s3d .. bmpwad8.s3d; the constructor takes the newest
	// expansion's picture whose archive is there, and CDisplay::LoadOverlay reads it from that
	// archive (Luclin's from bmpwad7, else bmpwad6).
	struct Choice
	{
		const char *archive, *picture, *fallback;
	};
	const Choice choices[] = {
		{"bmpwad8.s3d", "eqpload.bmp", nullptr},
		{"bmpwad7.s3d", "eqlload.bmp", "bmpwad6.s3d"},
		{"bmpwad5.s3d", "eqvload.bmp", nullptr},
		{"bmpwad4.s3d", "eqkload.bmp", nullptr},
		{"bmpwad.s3d", "logo03.bmp", nullptr},
	};
	m_image = Image{};
	m_picture.clear();
	for (const Choice &choice : choices) {
		PfsArchive archive;
		if (!archive.Open(eq_dir + "/" + choice.archive))
			continue;
		std::vector<uint8_t> data;
		bool read = archive.Read(choice.picture, data);
		if (!read && choice.fallback) {
			PfsArchive fallback;
			read = fallback.Open(eq_dir + "/" + choice.fallback) && fallback.Read(choice.picture, data);
		}
		// The constructor names the picture by the archive alone; a missing one is no picture.
		if (read && DecodeBmp(data, m_image))
			m_picture = choice.picture;
		else
			m_image = Image{};
		break;
	}
	m_seed = static_cast<uint32_t>(std::time(nullptr)); // EQ_LoadingS_draw's first srand(time)
	return Loaded();
}

void LoadingScreen::Reset()
{
	m_percent = 0;
	m_text.clear();
}

void LoadingScreen::SetProgress(int percent, const std::string &what, uint64_t now_ms)
{
	auto next = [this] {
		m_seed = m_seed * 214013 + 2531011; // the C runtime's rand()
		return static_cast<int>((m_seed >> 16) & 0x7fff);
	};
	m_percent = std::min(percent, 100);
	int roll = next();
	if (what.empty())
		m_text.clear();
	else if (!m_joke || m_joke_ms + 3000 <= now_ms) {
		if (roll % 20 == 1) {
			m_text = std::string(Jokes[next() % JokeCount]) + "...";
			m_joke = true;
			m_joke_ms = now_ms;
		}
		else {
			m_text = what + "...";
			m_joke = false;
		}
	}
}

void LoadingScreen::Draw(Canvas &canvas, TextRenderer &text) const
{
	int width = canvas.Width(), height = canvas.Height();
	canvas.Fill({0, 0, width, height}, 0x000000);
	// The new UI's offset: the 640x480 picture in the middle of a larger screen.
	int x0 = width > Width ? (width - Width) / 2 : 0;
	int y0 = width > Width ? (height - Height) / 2 : 0;
	if (Loaded())
		canvas.Blit(m_image, x0, y0);
	if (m_percent > 0 && m_percent <= 100) {
		const uint32_t light = 0xc8c800, dark = 0x646400;
		canvas.Fill({x0 + 179, y0 + 465, 293, 1}, light); // top
		canvas.Fill({x0 + 179, y0 + 465, 1, 15}, light);  // left
		canvas.Fill({x0 + 179, y0 + 479, 293, 1}, dark);  // bottom
		canvas.Fill({x0 + 471, y0 + 465, 1, 15}, dark);   // right
		int end = x0 + 180 + m_percent * 290 / 100;
		canvas.Fill({x0 + 180, y0 + 466, end - (x0 + 180), 13}, 0x3232c8);
		canvas.Fill({end + 1, y0 + 466, x0 + 471 - (end + 1), 13}, 0x000000);
		canvas.Fill({end, y0 + 466, 1, 13}, 0x323264);
	}
	if (!m_text.empty()) {
		text.Draw(canvas, x0 + 211, y0 + 466, m_text, 14, 0x000000);
		text.Draw(canvas, x0 + 210, y0 + 465, m_text, 14, 0xf0f000);
	}
}
