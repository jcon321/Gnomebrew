#pragma once

// eqgame's CFacePick (@00419e8d on, EQUI_FacePick.xml's "FacePickWindow"), over character creation and,
// with /facepick, in the zone: the face, and on a Luclin model the hair, hair color, eyes, beard, beard
// color and a Barbarian's woad (CycleThroughFHEB @0041a8d0), put on the model as they're picked
// (CDisplay::SetFHEB @0049f915, SetFHEB_Color @004a0015, EQPlayer::SetDefaultFacialFeaturesByFace
// @005204ee). What Accept and Cancel do with them, and the camera, are whoever's picking.

#include "character_model.h"

#include <cstdint>
#include <random>
#include <string>

class SidlWindow;

class FacePick
{
public:
	static constexpr const char *Screen = "FacePickWindow";

	// The faces and Luclin features, as EQPlayer and EQ_PC keep them.
	struct Features
	{
		uint8_t face = 0;
		uint8_t hair_style = 0, hair_color = 0, beard = 0, beard_color = 0, eye1 = 0, eye2 = 0;
	};
	enum class Action { None, Changed, Accept, Cancel };

	FacePick();
	FacePick(const FacePick &) = delete;
	FacePick &operator=(const FacePick &) = delete;

	// The models, for what SetFHEB asks of them; without, they're all old ones.
	void SetModels(render::CharacterModels *models) { m_models = models; }
	void SetModel(uint16_t race, uint8_t gender)
	{
		m_model_race = race;
		m_model_gender = gender;
	}
	uint16_t Race() const { return m_model_race; }
	uint8_t Gender() const { return m_model_gender; }
	Features &Model() { return m_player; } // pinstLocalPlayer's: what the model shows
	const Features &Model() const { return m_player; }

	void Start(SidlWindow &window); // Activate's: the selections from the model, and the rows it has
	Action Click(const std::string &control);
	void Accept();
	void Cancel(const Features &was);

	bool ModelIsLuclin() const;
	// CDisplay_SwapFace on the model: the face, and on a Luclin model its eyes' defaults.
	void SwapFace(uint8_t face);
	void SetDefaultFacialFeaturesByFace(int face, bool force, bool random);

private:
	int Roll(int n) { return static_cast<int>(m_random() % static_cast<uint32_t>(n)); }
	// EQPlayer's checks on the model shown.
	int HasAttachedHair() const;  // 0 none, 1 hair, 2 a head variant (an Erudite's)
	int HasAttachedBeard() const; // 0 none, 1 a beard, 2 a face variant
	bool IsFemaleTrollOrOgre() const; // EQPlayer::isFemaleTrollOrOgre_or_BeardRelated: also any man
	void AllowedHairColors(int &low, int &high) const; // GetAllowedHairColorIndexRange
	bool SetEyeMaterial(bool right, uint8_t color); // CDisplay_SetEyeMaterial
	bool SetHairOrBeard(unsigned it);               // CDisplay::SetHairOrBeard (1000 + style, 2000 + style)
	bool SetFHEB(int value, int which);             // which: 1 face, 2 hair, 3 eyes, 4 beard
	bool SetFHEB_Color(int value, int which);
	void SetFaceSelectionsFromPlayer();
	void CycleThroughFHEB(bool up, int which, bool color);
	void ShowFacialFeatureRows(SidlWindow &window) const;

	std::mt19937 m_random;
	render::CharacterModels *m_models = nullptr;
	Features m_player;
	uint16_t m_model_race = 1; // the model's race and gender
	uint8_t m_model_gender = 0;
	// The selections (+0x1a0..0x1a7) and the feature it's cycling (+0x19c).
	struct Selections
	{
		uint8_t face = 0, eye1 = 0, eye2 = 0, hair_style = 0, beard = 0, hair_color = 0, beard_color = 0, variant = 0;
	};
	Selections m_selections;
	int m_cycling = 1;
};
