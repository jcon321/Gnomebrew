#include "face_pick.h"

#include "sidl.h"

#include <chrono>
#include <cstdio>

FacePick::FacePick()
	: m_random(static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count()))
{
}

// ---------------------------------------------------------------- the model's features (EQPlayer, CDisplay)

namespace
{
	// LegalPlayerRace: 1..12, Iksar and Vah Shir.
	bool LegalPlayerRace(uint16_t race)
	{
		return (race >= 1 && race <= 12) || race == 128 || race == 130;
	}

	// EQPlayer::GetBaseFaceNbr: the face 0..9, and its variant (the tens: a Barbarian's woad).
	uint8_t BaseFace(uint8_t face, uint8_t *variant = nullptr)
	{
		if (variant)
			*variant = face < 10 ? 0 : static_cast<uint8_t>(face / 10);
		return face < 10 ? face : static_cast<uint8_t>(face % 10);
	}
}

bool FacePick::ModelIsLuclin() const
{
	return m_models && m_models->IsLuclin(m_model_race, m_model_gender);
}

int FacePick::HasAttachedHair() const
{
	// EQPlayer::HasAttachedHair: an Erudite's head is a face variant; trolls and ogres, men none,
	// nor Iksar and Vah Shir.
	if (!ModelIsLuclin())
		return 0;
	uint16_t race = m_model_race;
	if (race == 3)
		return 2;
	if (race < 9)
		return 1;
	if (race < 11)
		return m_model_gender == 0 ? 0 : 1;
	if (race == 128 || race == 130)
		return 0;
	return 1;
}

int FacePick::HasAttachedBeard() const
{
	// EQPlayer::HasAttachedBeard: humans, barbarians, Erudites, dwarves, halflings and gnomes, men,
	// have beards; elves and half elves a face variant, and so does a dwarf woman.
	if (!ModelIsLuclin())
		return 0;
	uint16_t race = m_model_race;
	if (m_model_gender == 1)
		return race == 8 ? 2 : 0;
	if (race == 0 || race == 4 || race == 9 || race == 10 || race > 12)
		return 0;
	if (race >= 5 && race <= 7)
		return 2;
	return 1;
}

bool FacePick::IsFemaleTrollOrOgre() const
{
	return m_model_gender == 0 || m_model_race == 9 || m_model_race == 10;
}

void FacePick::AllowedHairColors(int &low, int &high) const
{
	// EQPlayer::GetAllowedHairColorIndexRange.
	low = 0;
	switch (m_model_race) {
	case 5: low = 4; high = 17; break;
	case 6: low = 11; high = 19; break;
	case 9:
	case 10: high = 23; break;
	case 12: high = 22; break;
	case 128:
	case 130: high = 0; break;
	default: high = 18; break;
	}
}

void FacePick::SwapFace(uint8_t face)
{
	// CDisplay_SwapFace: on a Luclin model the right eye goes with an eye patch or scar (a human
	// man's 3 and a barbarian man's 6: 0xd1; a dwarf man's 5 and a barbarian man's 4: 0xcf), else
	// it's the left's, both 1 when unset.
	m_player.face = face;
	if (!ModelIsLuclin())
		return;
	uint8_t base = BaseFace(face);
	uint16_t race = m_model_race;
	bool man = m_model_gender == 0;
	if (man && ((race == 1 && base == 3) || (race == 2 && base == 6)))
		m_player.eye2 = 0xd1;
	else if (man && ((race == 8 && base == 5) || (race == 2 && base == 4)))
		m_player.eye2 = 0xcf;
	else if (m_player.eye1 == 0xff || m_player.eye1 == 0)
		m_player.eye1 = m_player.eye2 = 1;
	else
		m_player.eye2 = m_player.eye1;
}

bool FacePick::SetEyeMaterial(bool right, uint8_t color)
{
	// CDisplay_SetEyeMaterial: CHR_EYE<color + the race's>_MDF, if there's one; the right eye with
	// an eye patch or scar is that instead of the color.
	if (!ModelIsLuclin() || color == 0xff)
		return false;
	uint16_t race = m_model_race;
	unsigned eye = color;
	if (right) {
		uint8_t base = BaseFace(m_player.face);
		bool man = m_model_gender == 0;
		if (man && ((race == 1 && base == 3) || (race == 2 && base == 6)))
			eye = 0xd1;
		else if (man && ((race == 8 && base == 5) || (race == 2 && base == 4)))
			eye = 0xcf;
	}
	unsigned offset = race == 6 ? 20 : race == 9 ? 100 : race == 10 ? 80 : race == 128 ? 40 : race == 130 ? 60 : 0;
	char name[32];
	snprintf(name, sizeof(name), "CHR_EYE%03u_MDF", eye + offset);
	if (!m_models->HasMaterial(name))
		return false;
	(right ? m_player.eye2 : m_player.eye1) = static_cast<uint8_t>(eye);
	return true;
}

bool FacePick::SetHairOrBeard(unsigned it)
{
	// CDisplay::SetHairOrBeard: IT<it + the race's offset> on the hair or beard point, if the model
	// has the point and there's such an item.
	if (!m_models || !ModelIsLuclin())
		return false;
	if (!m_models->HasPoint(m_model_race, m_model_gender, it < 2000 ? "HAIR_POINT_DAG" : "BEARD_POINT_DAG"))
		return false;
	return m_models->HasItemModel(it + render::LuclinAttachmentOffset(m_model_race, m_model_gender));
}

bool FacePick::SetFHEB(int value, int which)
{
	// CDisplay::SetFHEB: true when the model took it.
	switch (which) {
	case 1:
		SwapFace(static_cast<uint8_t>(value));
		if (ModelIsLuclin())
			SetEyeMaterial(true, m_player.eye2);
		return true;
	case 2:
		if (SetHairOrBeard(1000 + value)) {
			m_player.hair_style = static_cast<uint8_t>(value);
			return true;
		}
		m_player.hair_style = m_player.hair_color = 0xff;
		return false;
	case 3:
		if (!SetEyeMaterial(true, static_cast<uint8_t>(value)))
			return false;
		SetEyeMaterial(false, static_cast<uint8_t>(value));
		return true;
	default:
		if (SetHairOrBeard(2000 + value)) {
			m_player.beard = static_cast<uint8_t>(value);
			return true;
		}
		m_player.beard = m_player.beard_color = 0xff;
		return false;
	}
}

bool FacePick::SetFHEB_Color(int value, int which)
{
	// CDisplay::SetFHEB_Color: a hair or beard color in the race's range, tinting what's on its
	// point (SetDagSpriteTint: false when nothing is); for the face (1) its variant.
	if (value >= 24 && which != 1)
		return false;
	if (which != 1) {
		int low, high;
		AllowedHairColors(low, high);
		if (value < low || value > high)
			return false;
	}
	if (which == 1)
		return SetFHEB(BaseFace(m_player.face) + value * 10, 1);
	if (which == 4) {
		m_player.beard_color = static_cast<uint8_t>(value);
		return m_player.beard != 0xff && SetHairOrBeard(2000 + m_player.beard);
	}
	m_player.hair_color = static_cast<uint8_t>(value);
	return m_player.hair_style != 0xff && SetHairOrBeard(1000 + m_player.hair_style);
}

void FacePick::SetDefaultFacialFeaturesByFace(int face, bool force, bool random)
{
	// EQPlayer::SetDefaultFacialFeaturesByFace (@005204ee), on a Luclin model of a player race:
	// unless forced, only when the first eye color is unset. Hair (HasAttachedHair 1) style 0 in the
	// range's first color, a dwarf man beard 0 in the hair's color; or at random: eyes 1..9, a
	// Barbarian's woad 30% of the time, hair style 0..2 in any allowed color, an Erudite's head
	// variant, a beard 40% of the time (always on a dwarf) 0..4 or a face variant, in the hair's color.
	if (!force && m_player.eye1 != 0xff)
		return;
	if (!LegalPlayerRace(m_model_race) || !ModelIsLuclin())
		return;
	uint16_t race = m_model_race;
	uint8_t new_face = m_player.face;
	uint8_t base = BaseFace(face < 0 ? m_player.face : static_cast<uint8_t>(face));
	m_player.hair_style = race == 130 && base == 7 ? 0 : 0xff;
	m_player.hair_color = m_player.beard = m_player.beard_color = 0xff;
	auto random_hair = [&] {
		int low, high;
		AllowedHairColors(low, high);
		m_player.hair_style = static_cast<uint8_t>(Roll(3));
		int color = low;
		if (low < high)
			color = Roll(high + 1 - low) + low;
		m_player.hair_color = static_cast<uint8_t>(color);
	};
	if (HasAttachedHair() == 1) {
		if (!random) {
			int low, high;
			AllowedHairColors(low, high);
			m_player.hair_style = 0;
			m_player.hair_color = static_cast<uint8_t>(low);
		}
		else
			random_hair();
	}
	bool beard_color = true;
	if (!random) {
		if (m_model_gender != 0 || race != 8)
			beard_color = false;
		else
			m_player.beard = 0;
	}
	else {
		uint8_t eyes = static_cast<uint8_t>(Roll(9) + 1);
		m_player.eye1 = m_player.eye2 = eyes;
		new_face = base;
		if (race == 2 && Roll(100) + 1 < 31)
			new_face = static_cast<uint8_t>(Roll(9) * 10 + base);
		if (HasAttachedHair() == 1)
			random_hair();
		else if (HasAttachedHair() == 2) {
			int most = race == 3 ? (m_model_gender != 0 ? 8 : 5) : 5;
			new_face = static_cast<uint8_t>(Roll(most + 1) * 10 + base);
		}
		int beard = HasAttachedBeard();
		if (beard == 1) {
			if (race != 8 && Roll(100) + 1 > 40) {
				m_player.beard = m_player.beard_color = 0xff;
				beard_color = false;
			}
			else
				m_player.beard = static_cast<uint8_t>(Roll(5));
		}
		else if (beard == 2 && Roll(100) + 1 <= 40) {
			int most = race == 8 ? 1 : 4;
			new_face = static_cast<uint8_t>(Roll(most + 1) * 10 + base);
		}
		else
			beard_color = false;
	}
	if (beard_color)
		m_player.beard_color = m_player.hair_color;
	if (m_player.face != new_face)
		SwapFace(new_face);
	if (random)
		SetFaceSelectionsFromPlayer();
}

void FacePick::SetFaceSelectionsFromPlayer()
{
	m_selections.beard = m_player.beard;
	m_selections.beard_color = m_player.beard_color;
	m_selections.hair_style = m_player.hair_style;
	m_selections.hair_color = m_player.hair_color;
	m_selections.eye1 = m_player.eye1;
	m_selections.eye2 = m_player.eye2;
	m_selections.face = m_player.face;
	BaseFace(m_player.face, &m_selections.variant);
}

void FacePick::CycleThroughFHEB(bool up, int which, bool color)
{
	// CFacePick::CycleThroughFHEB (@0041a8d0): the selection steps up or down (wrapping), on until
	// the model takes one (SetFHEB, SetFHEB_Color).
	int mode = which;
	int low = 0, high = 29;
	uint8_t *selection = nullptr;
	auto variant = [&](int most) {
		high = most;
		low = 0;
		selection = &m_selections.variant;
	};
	if (mode == 1) {
		if (!color) {
			m_selections.face = BaseFace(m_selections.face);
			selection = &m_selections.face;
			high = 7;
		}
		else {
			if (m_model_race != 2)
				return;
			variant(8);
		}
	}
	else if (mode == 2) {
		if (HasAttachedHair() == 0)
			return;
		if (m_model_race == 3) {
			// An Erudite's hair is its head: the face's variant.
			if (color)
				return;
			mode = 1;
			color = true;
			variant(m_model_gender != 0 ? 8 : 5);
		}
		else if (!color) {
			selection = &m_selections.hair_style;
			high = (IsFemaleTrollOrOgre() ? 1 : 0) + 2;
		}
		else {
			selection = &m_selections.hair_color;
			high = 23;
		}
	}
	else if (mode == 3) {
		if (color)
			return;
		selection = &m_selections.eye2;
		low = 1;
		high = 20;
	}
	else if (mode == 4) {
		int beard = HasAttachedBeard();
		if (beard == 2 && !color) {
			mode = 1;
			color = true;
			variant(m_model_race == 8 ? 1 : 3);
		}
		else if (beard == 0)
			return;
		else if (!color)
			selection = &m_selections.beard;
		else {
			selection = &m_selections.beard_color;
			high = 23;
		}
	}
	else
		return;

	auto attempt = [&](int value) {
		if (color)
			return SetFHEB_Color(value, mode);
		int v = value;
		if (mode == 1 && m_selections.variant != 0)
			v = m_selections.variant * 10 + value;
		return SetFHEB(v, mode);
	};
	bool took = false;
	int value;
	if (!up) {
		if (*selection == low)
			*selection = static_cast<uint8_t>(high);
		else
			--*selection;
		value = *selection;
		if (low <= value) {
			for (;;) {
				if (high < value)
					value = high;
				if (attempt(value)) {
					took = true;
					break;
				}
				if (value == low)
					break;
				--value;
				if (value < low)
					break;
			}
		}
	}
	else {
		++*selection;
		if (high < *selection)
			*selection = static_cast<uint8_t>(low);
		value = *selection;
		int wraps = 0;
		if (value <= high) {
			for (;;) {
				if (attempt(value)) {
					took = true;
					break;
				}
				if (!color && value == high) {
					// Past the last: round once more from the first, for the eyes, the hair (but
					// a woman troll's or ogre's, or a man's) and a dwarf man's beard.
					bool again = mode == 3 || (mode == 2 && !IsFemaleTrollOrOgre()) ||
					             (mode == 4 && m_model_race == 8 && m_model_gender == 0);
					if (again) {
						value = low - 1;
						++wraps;
					}
				}
				if (wraps >= 2)
					break;
				++value;
				if (value > high)
					break;
			}
		}
	}
	*selection = static_cast<uint8_t>(value);
	if (mode == 3)
		m_selections.eye1 = m_selections.eye2;
	else if (mode == 1 && color) {
		if (!took)
			*selection = 0;
	}
	else if (took) {
		if (mode == 2)
			SetFHEB_Color(m_selections.hair_color, 2);
		else if (mode == 4)
			SetFHEB_Color(m_selections.beard_color, 4);
	}
	else if ((mode == 2 && IsFemaleTrollOrOgre()) || (mode == 4 && m_model_race != 8))
		*selection = 0xff; // none
}

void FacePick::ShowFacialFeatureRows(SidlWindow &window) const
{
	// CFacePick's rows: the face always; the eyes on a Luclin model; the hair and its color, the
	// beard and its color where the model has them; the woad on a Luclin Barbarian.
	static const char *const rows[7] = {"Face", "Hair", "Hair_Color", "Eyes", "Beard", "Beard_Color", "Wode"};
	bool luclin = ModelIsLuclin();
	bool shown[7] = {true, HasAttachedHair() != 0, HasAttachedHair() != 0, luclin, HasAttachedBeard() != 0,
		HasAttachedBeard() != 0, m_model_race == 2 && luclin};
	for (int i = 0; i < 7; ++i)
		for (const char *suffix : {"_Down_Button", "_Up_Button", "_Text"})
			if (auto *control = window.Find(std::string(rows[i]) + suffix))
				control->visible = shown[i];
}

void FacePick::Start(SidlWindow &window)
{
	// CFacePick::Activate (EQPlayer_SetDefaultFacialFeaturesByFace?? @0041a1bf): the selections from
	// the model and the rows it has, starting on the face.
	ShowFacialFeatureRows(window);
	SetFaceSelectionsFromPlayer();
	m_cycling = 1;
}

void FacePick::Accept()
{
	// WndNotification's Accept_Button: the model keeps what's selected (the face is the model's own).
	m_player.beard = m_selections.beard;
	m_player.beard_color = m_selections.beard_color;
	m_player.hair_style = m_selections.hair_style;
	m_player.hair_color = m_selections.hair_color;
	m_player.eye1 = m_selections.eye1;
	m_player.eye2 = m_selections.eye2;
}

void FacePick::Cancel(const Features &was)
{
	// WndNotification's Cancel_Button: EQ_PC's back on the model.
	SetFHEB(was.face, 1);
	m_player = was;
	SetFaceSelectionsFromPlayer();
}

FacePick::Action FacePick::Click(const std::string &control)
{
	// CFacePick::WndNotification (@0041a4b0): each row's buttons cycle its feature
	// (CycleThroughFHEB), Random rolls them all (SetDefaultFacialFeaturesByFace(-1, 1, 1)).
	static const struct
	{
		const char *row;
		int which;
		bool color;
	} rows[7] = {{"Face", 1, false}, {"Hair", 2, false}, {"Hair_Color", 2, true}, {"Eyes", 3, false},
		{"Beard", 4, false}, {"Beard_Color", 4, true}, {"Wode", 1, true}};
	for (const auto &row : rows) {
		bool down = control == std::string(row.row) + "_Down_Button";
		if (down || control == std::string(row.row) + "_Up_Button") {
			m_cycling = row.which;
			CycleThroughFHEB(!down, row.which, row.color);
			return Action::Changed;
		}
	}
	if (control == "Random_Button") {
		SetDefaultFacialFeaturesByFace(-1, true, true);
		return Action::Changed;
	}
	if (control == "Accept_Button")
		return Action::Accept;
	if (control == "Cancel_Button")
		return Action::Cancel;
	return Action::None;
}
