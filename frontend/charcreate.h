#pragma once

// Character creation as CEverQuest::DoNewCharacterCreation (@0053ae99) runs it:
// CCharacterCreation (EQUI_CharacterCreate.xml) over the character select
// scene, four screens behind Back and Next:
//   0  race and class, abilities, name, face and gender
//   1  deity
//   2  starting city
//   3  the welcome text; Next sends the name to the world server
// "Set Face" swaps in CFacePick (EQUI_FacePick.xml). The rules come from
// CreationRules; this class is the screens' state and button handling.

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
	void Activate(int width, int height, uint32_t expansions);
	Action OnEvent(const InputEvent &event);
	void Draw(Canvas &canvas, TextRenderer &text, SidlTextures &textures, uint64_t now_ms) const;

	const std::string &Message() const { return m_message; }
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
	void RandomizeFace() { m_face = static_cast<uint8_t>(Roll(8)); }
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

	const CreationRules &m_rules;
	const EqStr *m_strings;
	std::mt19937 m_random;
	SidlWindow m_window;
	SidlWindow m_face_window;
	std::string m_message;
	int m_width = 640, m_height = 480;
	uint32_t m_expansions = 0;

	int m_screen = 0;
	bool m_rotate = true;
	uint16_t m_race = 1;
	uint8_t m_class = 1;
	uint8_t m_gender = 0;
	uint8_t m_face = 0;
	int m_deity = 0; // CC1_Deity button
	int m_city = -1;
	int m_stats[CreationRules::StatCount] = {};
	int m_min[CreationRules::StatCount] = {};
	int m_max[CreationRules::StatCount] = {};
	bool m_bonus[CreationRules::StatCount] = {};
	int m_points = 0;

	std::string m_name_buffer; // CreateName's result, put in the name box

	bool m_picking_face = false;
	uint8_t m_face_before = 0;
};
