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

void CharacterCreation::Activate(int width, int height, uint32_t expansions)
{
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
	RandomizeFace();
	std::vector<uint8_t> classes;
	for (uint8_t c : CreationRules::ClassButtons)
		if (m_rules.CanBeClass(m_race, c))
			classes.push_back(c);
	m_class = classes.empty() ? 1 : classes[classes.size() > 1 ? Roll(static_cast<int>(classes.size())) : 0];
	UpdateDeityButtons();
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
		if (!name || !CreationRules::IsValidName(name->text))
			return Show(0x628); // Name is not valid.
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
	c.face = m_face;
	return c;
}

render::CharacterAppearance CharacterCreation::Appearance() const
{
	render::CharacterAppearance appearance;
	appearance.race = m_race;
	appearance.gender = m_gender;
	appearance.face = m_face;
	// What our create packet leaves them (0), as the new character's spawn will have them; picking
	// them (a Luclin model's hair, beard and eyes) isn't done.
	appearance.hair_style = appearance.hair_color = appearance.beard = appearance.beard_color = 0;
	appearance.eye_color1 = appearance.eye_color2 = 0;
	return appearance;
}

// ---------------------------------------------------------------- face picker

void CharacterCreation::StartFacePick()
{
	// CCharacterCreation hides itself and CFacePick::Activate shows the picker.
	m_picking_face = true;
	m_face_before = m_face;
}

void CharacterCreation::EndFacePick(bool accept)
{
	if (!accept)
		m_face = m_face_before;
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
		// CycleThroughFHEB with the face: 0..7, wrapping. The old models have no separate hair,
		// eyes or beard, so the other rows don't change anything.
		if (clicked == "Face_Down_Button")
			m_face = m_face == 0 ? 7 : static_cast<uint8_t>(m_face - 1);
		else if (clicked == "Face_Up_Button")
			m_face = m_face >= 7 ? 0 : static_cast<uint8_t>(m_face + 1);
		else if (clicked == "Random_Button")
			RandomizeFace();
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
		RandomizeFace();
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
