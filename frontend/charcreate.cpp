#include "charcreate.h"
#include "eqstr.h"

#include <algorithm>
#include <cctype>
#include <chrono>

namespace
{
	std::string Numbered(const char *format, int n)
	{
		char name[64];
		snprintf(name, sizeof(name), format, n);
		return name;
	}

	constexpr uint32_t White = 0xffffff, Green = 0x00ff00;
}

CharacterCreation::CharacterCreation(const CreationRules &rules, const EqStr *strings)
	: m_rules(rules), m_strings(strings),
	  m_random(static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count()))
{
}

std::string CharacterCreation::Text(uint32_t id) const
{
	return m_strings ? m_strings->Format(id, {}) : std::string();
}

bool CharacterCreation::Build(const SidlDefinitions &defs, std::string &error)
{
	if (!m_window.Build(defs, "CharacterCreation", error) || !m_face_window.Build(defs, "FacePickWindow", error))
		return false;
	// The constructor's SetMaxChars: 15 (10 on servers that limit names).
	if (auto *name = m_window.Find("CC0_NameInput"))
		name->max_chars = 15;
	return true;
}

void CharacterCreation::Activate(int width, int height, uint32_t expansions, const render::CharacterAppearance *shown)
{
	// The model starts as character select left it.
	m_model_race = shown ? shown->race : 1;
	m_model_gender = shown ? shown->gender : 0;
	m_player = {};
	if (shown) {
		m_player.face = shown->face;
		m_player.hair_style = shown->hair_style;
		m_player.hair_color = shown->hair_color;
		m_player.beard = shown->beard;
		m_player.beard_color = shown->beard_color;
		m_player.eye1 = shown->eye_color1;
		m_player.eye2 = shown->eye_color2;
	}
	m_width = width;
	m_height = height;
	m_expansions = expansions;
	// InitializeCharacterCreation.
	m_screen = 0;
	m_rotate = true;
	if (auto *rotate = m_window.Find("CC_Rotate_Button"))
		rotate->checked = true;
	m_points = 0;
	if (auto *name = m_window.Find("CC0_NameInput"))
		name->text.clear();
	m_window.Focus("CC0_NameInput");
	m_picking_face = false;

	RandomizeCharacter(true, true);
	for (int i = 0; i < 4; ++i)
		if (auto *screen = m_window.Find(Numbered("CharacterCreationScreen%d", i)))
			screen->visible = false;
	ActivateScreen(0);
}

// ---------------------------------------------------------------- choices

void CharacterCreation::RandomizeCharacter(bool race, bool gender)
{
	if (race)
		m_race = CreationRules::RaceButtons[Roll(CreationRules::RaceCount)];
	if (gender)
		m_gender = static_cast<uint8_t>(Roll(2));
	RandomizeFacialFeatures();
	std::vector<uint8_t> classes;
	for (uint8_t c : CreationRules::ClassButtons)
		if (m_rules.CanBeClass(m_race, c))
			classes.push_back(c);
	m_class = classes.empty() ? 1 : classes[classes.size() > 1 ? Roll(static_cast<int>(classes.size())) : 0];
	SetLoadZonePlayerLocation();
	UpdateDeityButtons();
}

void CharacterCreation::RandomizeFacialFeatures()
{
	// A face 0..7 for the new character, put on the model, which then rolls the rest of its
	// features (SetDefaultFacialFeaturesByFace(-1, 1, 1)); a Luclin model's go to the new
	// character too. In eqgame the model is still the one from before a race or gender button
	// changed it, so it rolls by the old race's rules: from a model without hair (an Iksar, a Vah
	// Shir, an Erudite, a troll or ogre man) the new one comes up bald. Ours (see README, Custom
	// changes): the model takes the new race and gender first, so it rolls by theirs.
	m_model_race = m_race;
	m_model_gender = m_gender;
	m_pc.face = static_cast<uint8_t>(Roll(8));
	SwapFace(m_pc.face);
	SetDefaultFacialFeaturesByFace(-1, true, true);
	if (ModelIsLuclin()) {
		m_pc.hair_style = m_player.hair_style;
		m_pc.hair_color = m_player.hair_color;
		m_pc.beard = m_player.beard;
		m_pc.beard_color = m_player.beard_color;
		m_pc.eye1 = m_player.eye1;
		m_pc.eye2 = m_player.eye2;
	}
}

void CharacterCreation::SetLoadZonePlayerLocation()
{
	// CCharacterCreation_SetPlayerAppearanceFromNewPC: the model takes the new character's
	// features, becomes its race and gender (CCharacterSelect_SwitchModel, face 0; CreatePlayerActor's
	// SetDefaultFacialFeaturesByFace(-1, 0, 0) when the first eye color is unset) and its face.
	m_player.hair_style = m_pc.hair_style;
	m_player.hair_color = m_pc.hair_color;
	m_player.beard = m_pc.beard;
	m_player.beard_color = m_pc.beard_color;
	m_player.eye1 = m_pc.eye1;
	m_player.eye2 = m_pc.eye2;
	m_model_race = m_race;
	m_model_gender = m_gender;
	m_player.face = 0;
	SetDefaultFacialFeaturesByFace(-1, false, false);
	SwapFace(m_pc.face);
}

void CharacterCreation::InitializeStats()
{
	// InitializeCharacterStats: the race's base plus the class's bonus, which comes out of
	// 50 points; each stat can go up to 25 over that (150 at most).
	m_points = 50;
	for (int i = 0; i < CreationRules::StatCount; ++i) {
		auto stat = static_cast<CreationRules::Stat>(i);
		int bonus = m_rules.ClassBonus(m_class, stat);
		m_stats[i] = m_min[i] = m_rules.RaceBase(m_race, stat) + bonus;
		m_max[i] = std::min(m_stats[i] + 25, 150);
		m_bonus[i] = bonus != 0;
		m_points -= bonus;
	}
}

void CharacterCreation::UpdateDeityButtons()
{
	InitializeStats();
	int first = -1;
	for (int i = 0; i < CreationRules::DeityCount; ++i) {
		auto *button = m_window.Find(Numbered("CC1_Deity%d_Button", i));
		bool allowed = m_rules.CanWorship(m_race, m_class, i);
		if (allowed && first < 0)
			first = i;
		if (button) {
			button->checked = false;
			button->enabled = allowed;
		}
	}
	SetDeity(first < 0 ? 0 : first);
	if (auto *button = m_window.Find(Numbered("CC1_Deity%d_Button", m_deity)))
		button->checked = true;
	UpdateCityButtons();
	UpdateScreenZero(false);
}

void CharacterCreation::SetDeity(int button)
{
	// SetNewPCDeityFromBtnIndex: the description is string 0xfa2 + index.
	m_deity = button;
	if (auto *text = m_window.Find("CC1_DeityDescription"))
		text->text = Text(0xfa2 + static_cast<uint32_t>(button));
}

void CharacterCreation::UpdateCityButtons()
{
	SetCity(-1);
	int first = -1;
	for (int i = 0; i < CreationRules::CityCount; ++i) {
		bool allowed = m_rules.CityCanStart(i, m_race, m_deity, m_class);
		if (auto *button = m_window.Find(Numbered("CC2_City%d_Button", i)))
			button->enabled = allowed;
		if (allowed && first < 0)
			first = i;
	}
	if (first >= 0)
		SetCity(first);

	// Humans pick Qeynos or Freeport at random when both are open; Half Elves any of Qeynos,
	// Freeport, Kelethin and Felwithe.
	auto can = [&](int city) { return m_rules.CityCanStart(city, m_race, m_deity, m_class); };
	if (m_race == 1 && can(1) && can(4))
		SetCity(Roll(100) + 1 < 51 ? 1 : 4);
	else if (m_race == 7 && can(1) && can(4) && can(9) && can(10)) {
		int roll = Roll(100) + 1;
		SetCity(roll < 26 ? 1 : roll < 51 ? 4 : roll < 76 ? 9 : 10);
	}
	if (auto *button = m_window.Find(Numbered("CC2_City%d_Button", m_city)))
		button->checked = true;
}

void CharacterCreation::SetCity(int city)
{
	// StartingCity: unchecks the old button; the description is string 0x751 + city.
	if (auto *old = m_window.Find(Numbered("CC2_City%d_Button", m_city)))
		old->checked = false;
	m_city = city;
	if (auto *text = m_window.Find("CC2_CityText"))
		text->text = city >= 0 && city < CreationRules::CityCount ? Text(0x751 + static_cast<uint32_t>(city)) : "";
}

void CharacterCreation::UpdateScreenZero(bool set_name)
{
	// UpdateCharacterCreationUI_screenzerobuttons.
	for (int i = 0; i < CreationRules::RaceCount; ++i)
		if (auto *button = m_window.Find(Numbered("CC0_Race%d_Button", i)))
			button->checked = CreationRules::RaceButtons[i] == m_race;
	for (int i = 0; i < CreationRules::ClassCount; ++i) {
		if (auto *button = m_window.Find(Numbered("CC0_Class%d_Button", i))) {
			uint8_t c = CreationRules::ClassButtons[i];
			button->enabled = m_rules.CanBeClass(m_race, c);
			button->checked = button->enabled && c == m_class;
		}
	}
	if (auto *male = m_window.Find("CC0_Male_Button"))
		male->checked = m_gender == 0;
	if (auto *female = m_window.Find("CC0_Female_Button"))
		female->checked = m_gender == 1;
	if (auto *points = m_window.Find("CC0_Points_Left_Display_Button"))
		points->text = std::to_string(m_points);
	for (int i = 0; i < CreationRules::StatCount; ++i) {
		if (auto *value = m_window.Find(Numbered("CC0_Stat%d_Display_Button", i))) {
			value->text = std::to_string(m_stats[i]);
			value->text_color = m_bonus[i] ? Green : White; // the class's stats in green
		}
		if (auto *label = m_window.Find(Numbered("CC0_Stat%d_Button", i)))
			label->enabled = m_points != 0;
	}
	if (set_name)
		if (auto *name = m_window.Find("CC0_NameInput"))
			name->text = m_name_buffer;
}

bool CharacterCreation::LackingExpansion(bool race, bool class_id)
{
	// IsLackingExpansion: Iksar need Kunark; Vah Shir and Beastlords need Luclin.
	uint32_t id = 0;
	if (race && m_race == 128 && !(m_expansions & 1))
		id = 0x62e;
	else if (race && m_race == 130 && !(m_expansions & 4))
		id = 0x631;
	else if (class_id && m_class == 15 && !(m_expansions & 4))
		id = 0x634;
	if (!id)
		return false;
	m_message = Text(id);
	return true;
}

// ---------------------------------------------------------------- screens

void CharacterCreation::ActivateScreen(int screen)
{
	if (screen < 0 || screen > 3)
		screen = 0;
	if (screen != m_screen)
		if (auto *old = m_window.Find(Numbered("CharacterCreationScreen%d", m_screen)))
			old->visible = false;
	m_screen = screen;

	if (screen == 0)
		UpdateScreenZero(false);
	else if (screen == 2)
		UpdateCityButtons();
	else if (screen == 3) {
		std::string welcome;
		for (uint32_t id : CreationRules::WelcomeText(m_race, m_class, m_deity, m_city))
			welcome += Text(id);
		size_t at = welcome.find('@');
		if (at != std::string::npos)
			welcome.replace(at, 1, "<BR><BR>");
		if (auto *text = m_window.Find("CC3_WelcomeText"))
			text->text = welcome;
	}

	// Laid out for the screen size: the window fills it. The first screen spreads its three
	// panels to the edges (race and class left, name top center, abilities right); the others
	// are 640x440, centered when the screen is bigger.
	m_window.Move({0, 0, m_width, m_height});
	std::string id = Numbered("CharacterCreationScreen%d", screen);
	if (screen == 0) {
		m_window.Move(id, {0, 0, m_width, m_height - 40});
		auto place = [&](const char *panel, auto position) {
			if (auto *p = m_window.Find(panel)) {
				Rect r = p->rect;
				position(r);
				m_window.Move(panel, r);
			}
		};
		int middle = m_height - 40;
		place("CharacterCreationScreen0c", [&](Rect &r) { r.x = (m_width - r.w) / 2; r.y = 0; });
		place("CharacterCreationScreen0a", [&](Rect &r) { r.x = 0; r.y = (middle - r.h) / 2; });
		place("CharacterCreationScreen0b", [&](Rect &r) { r.x = m_width - r.w; r.y = (middle - r.h) / 2; });
	}
	else {
		Rect r{0, 0, 640, 440};
		if (m_height != 480) {
			r.x = (m_width - 640) / 2;
			r.y = (m_height - 440) / 2;
		}
		m_window.Move(id, r);
	}
	if (auto *shown = m_window.Find(id))
		shown->visible = true;
}

CharacterCreation::Action CharacterCreation::Show(uint32_t id)
{
	return ShowText(Text(id));
}

CharacterCreation::Action CharacterCreation::ShowText(const std::string &text)
{
	m_message = text;
	m_message_ms = 5000;
	return Action::Message;
}

CharacterCreation::Action CharacterCreation::DoNext()
{
	if (m_screen == 0) {
		if (m_points > 0)
			return ShowText(Text(0x629) + " " + Text(0x62a)); // spend all of your ability points
		if (LackingExpansion(true, true))
			return Action::Message;
		const auto *name = m_window.Find("CC0_NameInput");
		if (!name || !CreationRules::IsValidName(name->text)) {
			// HandleNameApprovalResponse(-1): "Name is not valid." for 45 s, on the first screen.
			Show(0x628);
			m_message_ms = 45000;
			ActivateScreen(0);
			return Action::Message;
		}
	}
	if (m_screen + 1 < 4) {
		ActivateScreen(m_screen + 1);
		return Action::None;
	}
	return Action::Submit;
}

CharacterCreation::Action CharacterCreation::DoBack()
{
	if (m_screen < 1)
		return Action::Back;
	ActivateScreen(m_screen - 1);
	return Action::None;
}

void CharacterCreation::NameRejected()
{
	ActivateScreen(0);
}

NewCharacter CharacterCreation::Character() const
{
	NewCharacter c;
	if (const auto *name = m_window.Find("CC0_NameInput"))
		c.name = name->text;
	// World only approves "Name": a capital, then lower case.
	for (size_t i = 0; i < c.name.size(); ++i)
		c.name[i] = static_cast<char>(i == 0 ? std::toupper(static_cast<unsigned char>(c.name[i]))
		                                     : std::tolower(static_cast<unsigned char>(c.name[i])));
	c.race = m_race;
	c.class_id = m_class;
	c.gender = m_gender;
	c.deity = CreationRules::Deities[m_deity];
	c.start = CreationRules::Start(m_race, m_class, m_city, c.deity);
	c.str = static_cast<int16_t>(m_stats[CreationRules::Str]);
	c.sta = static_cast<int16_t>(m_stats[CreationRules::Sta]);
	c.agi = static_cast<int16_t>(m_stats[CreationRules::Agi]);
	c.dex = static_cast<int16_t>(m_stats[CreationRules::Dex]);
	c.wis = static_cast<int16_t>(m_stats[CreationRules::Wis]);
	c.int_ = static_cast<int16_t>(m_stats[CreationRules::Int]);
	c.cha = static_cast<int16_t>(m_stats[CreationRules::Cha]);
	// FinalizeNewPCAndSendToWorld: a Luclin model's features, and the model's face.
	Features f = m_pc;
	if (ModelIsLuclin()) {
		f = m_player;
	}
	c.face = m_player.face;
	c.hair_style = f.hair_style;
	c.hair_color = f.hair_color;
	c.beard = f.beard;
	c.beard_color = f.beard_color;
	c.eye_color1 = f.eye1;
	c.eye_color2 = f.eye2;
	return c;
}

render::CharacterAppearance CharacterCreation::Appearance() const
{
	render::CharacterAppearance appearance;
	appearance.race = m_model_race;
	appearance.gender = m_model_gender;
	appearance.face = m_player.face;
	appearance.hair_style = m_player.hair_style;
	appearance.hair_color = m_player.hair_color;
	appearance.beard = m_player.beard;
	appearance.beard_color = m_player.beard_color;
	appearance.eye_color1 = m_player.eye1;
	appearance.eye_color2 = m_player.eye2;
	return appearance;
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

bool CharacterCreation::ModelIsLuclin() const
{
	return m_models && m_models->IsLuclin(m_model_race, m_model_gender);
}

int CharacterCreation::HasAttachedHair() const
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

int CharacterCreation::HasAttachedBeard() const
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

bool CharacterCreation::IsFemaleTrollOrOgre() const
{
	return m_model_gender == 0 || m_model_race == 9 || m_model_race == 10;
}

void CharacterCreation::AllowedHairColors(int &low, int &high) const
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

void CharacterCreation::SwapFace(uint8_t face)
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

bool CharacterCreation::SetEyeMaterial(bool right, uint8_t color)
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

bool CharacterCreation::SetHairOrBeard(unsigned it)
{
	// CDisplay::SetHairOrBeard: IT<it + the race's offset> on the hair or beard point, if the model
	// has the point and there's such an item.
	if (!m_models || !ModelIsLuclin())
		return false;
	if (!m_models->HasPoint(m_model_race, m_model_gender, it < 2000 ? "HAIR_POINT_DAG" : "BEARD_POINT_DAG"))
		return false;
	return m_models->HasItemModel(it + render::LuclinAttachmentOffset(m_model_race, m_model_gender));
}

bool CharacterCreation::SetFHEB(int value, int which)
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

bool CharacterCreation::SetFHEB_Color(int value, int which)
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

void CharacterCreation::SetDefaultFacialFeaturesByFace(int face, bool force, bool random)
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

void CharacterCreation::SetFaceSelectionsFromPlayer()
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

void CharacterCreation::CycleThroughFHEB(bool up, int which, bool color)
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

void CharacterCreation::ShowFacialFeatureRows()
{
	// CFacePick's rows: the face always; the eyes on a Luclin model; the hair and its color, the
	// beard and its color where the model has them; the woad on a Luclin Barbarian.
	static const char *const rows[7] = {"Face", "Hair", "Hair_Color", "Eyes", "Beard", "Beard_Color", "Wode"};
	bool luclin = ModelIsLuclin();
	bool shown[7] = {true, HasAttachedHair() != 0, HasAttachedHair() != 0, luclin, HasAttachedBeard() != 0,
		HasAttachedBeard() != 0, m_model_race == 2 && luclin};
	for (int i = 0; i < 7; ++i)
		for (const char *suffix : {"_Down_Button", "_Up_Button", "_Text"})
			if (auto *control = m_face_window.Find(std::string(rows[i]) + suffix))
				control->visible = shown[i];
}

// ---------------------------------------------------------------- face picker

void CharacterCreation::StartFacePick()
{
	// CCharacterCreation hides itself and CFacePick::Activate shows the picker, its rows for the
	// model and its selections from it; it starts on the face.
	m_picking_face = true;
	ShowFacialFeatureRows();
	SetFaceSelectionsFromPlayer();
	m_cycling = 1;
}

void CharacterCreation::EndFacePick(bool accept)
{
	// CFacePick::WndNotification: Accept keeps the model's features for the new character, Cancel
	// puts the new character's back on the model.
	if (accept) {
		m_pc.hair_style = m_player.hair_style;
		m_pc.hair_color = m_player.hair_color;
		m_pc.beard = m_player.beard;
		m_pc.beard_color = m_player.beard_color;
		m_pc.face = m_player.face;
		m_pc.eye1 = m_player.eye1;
		m_pc.eye2 = m_player.eye2;
	}
	else {
		SetFHEB(m_pc.face, 1);
		m_player = m_pc;
		SetFaceSelectionsFromPlayer();
	}
	m_picking_face = false;
}

// ---------------------------------------------------------------- input

CharacterCreation::Action CharacterCreation::OnEvent(const InputEvent &event)
{
	m_message.clear();
	if (m_picking_face) {
		if (event.type == InputEvent::Type::Key) {
			if (event.key == InputEvent::Key::Escape)
				EndFacePick(false);
			else if (event.key == InputEvent::Key::Enter)
				EndFacePick(true);
			return Action::None;
		}
		std::string clicked = m_face_window.OnMouse(event);
		// CFacePick::WndNotification: each row's buttons cycle its feature (CycleThroughFHEB), Random
		// rolls them all (SetDefaultFacialFeaturesByFace(-1, 1, 1)).
		static const struct
		{
			const char *row;
			int which;
			bool color;
		} rows[7] = {{"Face", 1, false}, {"Hair", 2, false}, {"Hair_Color", 2, true}, {"Eyes", 3, false},
			{"Beard", 4, false}, {"Beard_Color", 4, true}, {"Wode", 1, true}};
		for (const auto &row : rows) {
			bool down = clicked == std::string(row.row) + "_Down_Button";
			if (down || clicked == std::string(row.row) + "_Up_Button") {
				m_cycling = row.which;
				CycleThroughFHEB(!down, row.which, row.color);
				return Action::None;
			}
		}
		if (clicked == "Random_Button")
			SetDefaultFacialFeaturesByFace(-1, true, true);
		else if (clicked == "Accept_Button")
			EndFacePick(true);
		else if (clicked == "Cancel_Button")
			EndFacePick(false);
		return Action::None;
	}

	if (event.type == InputEvent::Type::Char || event.type == InputEvent::Type::Key) {
		if (event.type == InputEvent::Type::Key) {
			if (event.key == InputEvent::Key::Enter)
				return DoNext();
			if (event.key == InputEvent::Key::Escape)
				return DoBack();
		}
		if (m_screen == 0)
			m_window.OnKey(event);
		return Action::None;
	}

	// CCharacterCreation::WndNotification.
	std::string clicked = m_window.OnMouse(event);
	if (clicked.empty())
		return Action::None;
	if (clicked == "CC_Back_Button")
		return DoBack();
	if (clicked == "CC_Next_Button")
		return DoNext();
	if (clicked == "CC_Rotate_Button") {
		m_rotate = !m_rotate;
		return Action::None;
	}

	for (int i = 0; i < CreationRules::DeityCount; ++i)
		if (clicked == Numbered("CC1_Deity%d_Button", i)) {
			SetDeity(i);
			return Action::None;
		}
	for (int i = 0; i < CreationRules::CityCount; ++i)
		if (clicked == Numbered("CC2_City%d_Button", i)) {
			SetCity(i);
			if (auto *button = m_window.Find(clicked))
				button->checked = true;
			return Action::None;
		}

	if (clicked == "CC0_Get_Name_Button") {
		m_name_buffer = m_rules.RandomName(m_race, m_gender, m_random);
		UpdateScreenZero(true);
		return Action::None;
	}
	if (clicked == "CC0_Face_Button") {
		StartFacePick();
		return Action::None;
	}
	if (clicked == "CC0_Male_Button" || clicked == "CC0_Female_Button") {
		m_gender = clicked == "CC0_Male_Button" ? 0 : 1;
		RandomizeFacialFeatures();
		SetLoadZonePlayerLocation();
		UpdateScreenZero(false);
		return Action::None;
	}
	for (int i = 0; i < CreationRules::StatCount; ++i) {
		if (clicked == Numbered("CC0_Stat%d_Up_Button", i)) {
			if (m_points >= 1 && m_stats[i] + 1 <= m_max[i]) {
				++m_stats[i];
				--m_points;
			}
		}
		else if (clicked == Numbered("CC0_Stat%d_Down_Button", i)) {
			if (m_stats[i] - 1 >= m_min[i]) {
				--m_stats[i];
				++m_points;
			}
		}
		else if (clicked == Numbered("CC0_Stat%d_Button", i)) {
			// MaxOutStatPoints: everything left into this stat, up to its maximum.
			m_stats[i] += m_points;
			m_points = 0;
			if (m_stats[i] > m_max[i]) {
				m_points = m_stats[i] - m_max[i];
				m_stats[i] = m_max[i];
			}
		}
		else
			continue;
		UpdateScreenZero(false);
		return Action::None;
	}
	for (int i = 0; i < CreationRules::RaceCount; ++i)
		if (clicked == Numbered("CC0_Race%d_Button", i)) {
			// SetRaceAndRandomizeCharacter: a new face and class for the race.
			m_race = CreationRules::RaceButtons[i];
			RandomizeCharacter(false, false);
			return LackingExpansion(true, false) ? Action::Message : Action::None;
		}
	for (int i = 0; i < CreationRules::ClassCount; ++i)
		if (clicked == Numbered("CC0_Class%d_Button", i)) {
			// SetClassAndUpdateUI.
			m_class = CreationRules::ClassButtons[i];
			UpdateDeityButtons();
			return LackingExpansion(false, true) ? Action::Message : Action::None;
		}
	return Action::None;
}

void CharacterCreation::Draw(Canvas &canvas, TextRenderer &text, SidlTextures &textures, uint64_t now_ms) const
{
	if (m_picking_face)
		m_face_window.Draw(canvas, text, textures, now_ms);
	else
		m_window.Draw(canvas, text, textures, now_ms);
}
