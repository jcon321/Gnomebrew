#pragma once

// Character creation as CEverQuest::DoNewCharacterCreation (@0053ae99) runs it:
// CCharacterCreation (EQUI_CharacterCreate.xml) over the character select
// scene, four screens behind Back and Next:
//   0  race and class, abilities, name, face and gender
//   1  deity
//   2  starting city
//   3  the welcome text; Next sends the name to the world server
// "Set Face" swaps in CFacePick (EQUI_FacePick.xml): the face, and on a Luclin model the hair,
// hair color, eyes, beard, beard color and a Barbarian's woad (CycleThroughFHEB). The rules come
// from CreationRules; this class is the screens' state and button handling.
//
// As in eqgame there are two of everything facial: the new character's (EQ_PC, what's sent) and
// the model's (pinstLocalPlayer, what's shown, which FinalizeNewPCAndSendToWorld copies in when it's
// a Luclin model). RandomizeFacialFeatures rolls them on the model before it changes race.

#include "character_model.h"
#include "creation_rules.h"
#include "sidl.h"
#include "world_client.h"

#include <random>
#include <string>

class EqStr;

class CharacterCreation
{
public:
	enum class Action
	{
		None,
		Back,    // Back on the first screen: return to character select
		Submit,  // Next on the last screen: send Character() to the world server
		Message, // show Message() in a dialog
	};

	CharacterCreation(const CreationRules &rules, const EqStr *strings);

	bool Build(const SidlDefinitions &defs, std::string &error);
	// CCharacterCreation::Activate: a random character, the first screen, laid out for the screen size.
	// `shown`: the character select model it starts from (null: none).
	void Activate(int width, int height, uint32_t expansions, const render::CharacterAppearance *shown = nullptr);
	// The models, for what CFacePick asks of them (CDisplay::SetFHEB); without, they're all old ones.
	void SetModels(render::CharacterModels *models) { m_models = models; }
	Action OnEvent(const InputEvent &event);
	void Draw(Canvas &canvas, TextRenderer &text, SidlTextures &textures, uint64_t now_ms) const;

	const std::string &Message() const { return m_message; }
	// How long its confirmation dialog stays up: 5 s, 45 s for the name's (HandleNameApprovalResponse).
	uint32_t MessageMs() const { return m_message_ms; }
	NewCharacter Character() const;
	// HandleNameApprovalResponse with a rejection: back to the first screen.
	void NameRejected();
	// The string shown while the name is being approved (0x62c).
	std::string SubmittingText() const { return Text(0x62c); }
	std::string RejectedText() const { return Text(0x62d); }

	// For the 3D scene.
	render::CharacterAppearance Appearance() const;
	uint8_t ClassId() const { return m_class; }
	bool Rotate() const { return m_rotate; }
	bool PickingFace() const { return m_picking_face; }
	const SidlWindow &Window() const { return m_picking_face ? m_face_window : m_window; } // what's showing

private:
	std::string Text(uint32_t id) const;
	int Roll(int n) { return static_cast<int>(m_random() % static_cast<uint32_t>(n)); }

	void RandomizeCharacter(bool race, bool gender);
	void RandomizeFacialFeatures();
	void SetLoadZonePlayerLocation(); // CCharacterSelect::SetLoadZonePlayerLocation: the model from EQ_PC
	void InitializeStats();
	void UpdateDeityButtons();
	void SetDeity(int button);
	void UpdateCityButtons();
	void SetCity(int city);
	void UpdateScreenZero(bool set_name);
	bool LackingExpansion(bool race, bool class_id);
	Action DoNext();
	Action DoBack();
	void ActivateScreen(int screen);
	Action Show(uint32_t id);
	Action ShowText(const std::string &text);

	void StartFacePick();
	void EndFacePick(bool accept);

	// The faces and Luclin features, as EQPlayer and EQ_PC keep them.
	struct Features
	{
		uint8_t face = 0;
		uint8_t hair_style = 0, hair_color = 0, beard = 0, beard_color = 0, eye1 = 0, eye2 = 0;
	};
	// EQPlayer's checks on the model shown.
	bool ModelIsLuclin() const;
	int HasAttachedHair() const;  // 0 none, 1 hair, 2 a head variant (an Erudite's)
	int HasAttachedBeard() const; // 0 none, 1 a beard, 2 a face variant
	bool IsFemaleTrollOrOgre() const; // EQPlayer::isFemaleTrollOrOgre_or_BeardRelated: also any man
	void AllowedHairColors(int &low, int &high) const; // GetAllowedHairColorIndexRange
	// CDisplay_SwapFace on the model: the face, and on a Luclin model its eyes' defaults.
	void SwapFace(uint8_t face);
	bool SetEyeMaterial(bool right, uint8_t color); // CDisplay_SetEyeMaterial
	bool SetHairOrBeard(unsigned it);               // CDisplay::SetHairOrBeard (1000 + style, 2000 + style)
	bool SetFHEB(int value, int which);             // which: 1 face, 2 hair, 3 eyes, 4 beard
	bool SetFHEB_Color(int value, int which);
	void SetDefaultFacialFeaturesByFace(int face, bool force, bool random);
	void SetFaceSelectionsFromPlayer();
	void CycleThroughFHEB(bool up, int which, bool color);
	void ShowFacialFeatureRows(); // CFacePick's Activate (TAKP's SetDefaultFacialFeaturesByFace__)

	const CreationRules &m_rules;
	const EqStr *m_strings;
	std::mt19937 m_random;
	SidlWindow m_window;
	SidlWindow m_face_window;
	std::string m_message;
	uint32_t m_message_ms = 5000;
	int m_width = 640, m_height = 480;
	uint32_t m_expansions = 0;

	int m_screen = 0;
	bool m_rotate = true;
	uint16_t m_race = 1;
	uint8_t m_class = 1;
	uint8_t m_gender = 0;
	Features m_pc;     // EQ_PC's: what's sent
	Features m_player; // the model's
	uint16_t m_model_race = 1; // the model's race and gender (it changes after RandomizeFacialFeatures)
	uint8_t m_model_gender = 0;
	render::CharacterModels *m_models = nullptr;
	// CFacePick's selections (+0x1a0..0x1a7) and the feature it's cycling (+0x19c).
	struct Selections
	{
		uint8_t face = 0, eye1 = 0, eye2 = 0, hair_style = 0, beard = 0, hair_color = 0, beard_color = 0, variant = 0;
	};
	Selections m_selections;
	int m_cycling = 1;
	int m_deity = 0; // CC1_Deity button
	int m_city = -1;
	int m_stats[CreationRules::StatCount] = {};
	int m_min[CreationRules::StatCount] = {};
	int m_max[CreationRules::StatCount] = {};
	bool m_bonus[CreationRules::StatCount] = {};
	int m_points = 0;

	std::string m_name_buffer; // CreateName's result, put in the name box

	bool m_picking_face = false;
};
