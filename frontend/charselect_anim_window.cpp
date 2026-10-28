#include "charselect_anim_window.h"

#include "animations.h"

#include <algorithm>

namespace
{
	// The window, in the default UI's style (its templates and button art).
	const char *const Xml = R"(<?xml version = "1.0"?>
<XML ID = "EQInterfaceDefinitionLanguage">
	<Button item = "CSAW_CombatButton">
		<ScreenID>CombatButton</ScreenID>
		<RelativePosition>true</RelativePosition>
		<Location><X>4</X><Y>4</Y></Location>
		<Size><CX>100</CX><CY>24</CY></Size>
		<Style_Checkbox>true</Style_Checkbox>
		<RadioGroup>CSAW_Mode</RadioGroup>
		<Text>Combat only</Text>
		<TooltipReference>Now and then, one of the class's own animations (as eqgame)</TooltipReference>
		<TextColor><R>255</R><G>255</G><B>255</B></TextColor>
		<ButtonDrawTemplate>
			<Normal>A_BtnNormal</Normal>
			<Pressed>A_BtnPressed</Pressed>
			<Flyby>A_BtnFlyby</Flyby>
			<Disabled>A_BtnDisabled</Disabled>
			<PressedFlyby>A_BtnPressedFlyby</PressedFlyby>
		</ButtonDrawTemplate>
	</Button>
	<Button item = "CSAW_AllButton">
		<ScreenID>AllButton</ScreenID>
		<RelativePosition>true</RelativePosition>
		<Location><X>108</X><Y>4</Y></Location>
		<Size><CX>100</CX><CY>24</CY></Size>
		<Style_Checkbox>true</Style_Checkbox>
		<RadioGroup>CSAW_Mode</RadioGroup>
		<Text>All + emotes</Text>
		<TooltipReference>Now and then, the class's animations or a social's</TooltipReference>
		<TextColor><R>255</R><G>255</G><B>255</B></TextColor>
		<ButtonDrawTemplate>
			<Normal>A_BtnNormal</Normal>
			<Pressed>A_BtnPressed</Pressed>
			<Flyby>A_BtnFlyby</Flyby>
			<Disabled>A_BtnDisabled</Disabled>
			<PressedFlyby>A_BtnPressedFlyby</PressedFlyby>
		</ButtonDrawTemplate>
	</Button>
	<Listbox item = "CSAW_AnimList">
		<ScreenID>AnimList</ScreenID>
		<DrawTemplate>WDT_Inner</DrawTemplate>
		<AutoStretch>true</AutoStretch>
		<RightAnchorToLeft>false</RightAnchorToLeft>
		<BottomAnchorToTop>false</BottomAnchorToTop>
		<LeftAnchorOffset>4</LeftAnchorOffset>
		<TopAnchorOffset>32</TopAnchorOffset>
		<RightAnchorOffset>4</RightAnchorOffset>
		<BottomAnchorOffset>34</BottomAnchorOffset>
		<Style_Border>true</Style_Border>
		<Style_VScroll>true</Style_VScroll>
		<Columns><Width>40</Width><Heading>Code</Heading></Columns>
		<Columns><Width>150</Width><Heading>Animation</Heading></Columns>
	</Listbox>
	<Button item = "CSAW_PlayButton">
		<ScreenID>PlayButton</ScreenID>
		<RelativePosition>true</RelativePosition>
		<AutoStretch>true</AutoStretch>
		<LeftAnchorOffset>104</LeftAnchorOffset>
		<TopAnchorOffset>28</TopAnchorOffset>
		<RightAnchorOffset>4</RightAnchorOffset>
		<BottomAnchorOffset>4</BottomAnchorOffset>
		<TopAnchorToTop>false</TopAnchorToTop>
		<BottomAnchorToTop>false</BottomAnchorToTop>
		<RightAnchorToLeft>false</RightAnchorToLeft>
		<LeftAnchorToLeft>false</LeftAnchorToLeft>
		<Text>Play</Text>
		<TooltipReference>Play the animation selected (or double click it)</TooltipReference>
		<TextColor><R>255</R><G>255</G><B>255</B></TextColor>
		<ButtonDrawTemplate>
			<Normal>A_BtnNormal</Normal>
			<Pressed>A_BtnPressed</Pressed>
			<Flyby>A_BtnFlyby</Flyby>
			<Disabled>A_BtnDisabled</Disabled>
			<PressedFlyby>A_BtnPressedFlyby</PressedFlyby>
		</ButtonDrawTemplate>
	</Button>
	<Screen item = "CharSelectAnimWindow">
		<RelativePosition>false</RelativePosition>
		<Location><X>412</X><Y>20</Y></Location>
		<Size><CX>228</CX><CY>320</CY></Size>
		<Text>Animations</Text>
		<DrawTemplate>WDT_Rounded</DrawTemplate>
		<Style_Titlebar>true</Style_Titlebar>
		<Style_Closebox>false</Style_Closebox>
		<Style_Minimizebox>false</Style_Minimizebox>
		<Style_Border>true</Style_Border>
		<Style_Sizable>false</Style_Sizable>
		<Pieces>CSAW_CombatButton</Pieces>
		<Pieces>CSAW_AllButton</Pieces>
		<Pieces>CSAW_AnimList</Pieces>
		<Pieces>CSAW_PlayButton</Pieces>
	</Screen>
</XML>
)";

	// What an animation is, for the list. The socials' are the commands that play them
	// (CEverQuest_send_social's animation for each).
	std::string AnimationName(int id)
	{
		switch (id) {
		case 1: return "Kick";
		case 2: return "Piercing";
		case 3: return "Two-hand slashing";
		case 4: return "Two-hand blunt";
		case 5: return "Throwing";
		case 6: return "Off hand";
		case 7: return "Bash";
		case 8: return "Main hand";
		case 9: return "Archery";
		case 10: return "Swimming attack";
		case 11: return "Round kick";
		case 12: return "Hit";
		case 13: return "Hit hard";
		case 14: return "Hit (another way)";
		case 15: return "Drowning";
		case 16: return "Death";
		case 17: return "Walk";
		case 18: return "Run";
		case 19: return "Running jump";
		case 20: return "Standing jump";
		case 21: return "Fall";
		case 22: return "Crouching walk";
		case 23: return "Climb";
		case 24: return "Crouch";
		case 25: return "Tread water";
		case 26: return "Fidget";
		case 27: return "/cheer, /roar, /happy";
		case 28: return "/sigh, /cry, /mourn";
		case 29: return "/wave, /bye, /welcome";
		case 30: return "/rude";
		case 31: return "/yawn, /bored";
		case 32: return "Stand";
		case 33: return "Sit down";
		case 34: return "Shuffle";
		case 36: return "Kneel";
		case 37: return "Swim";
		case 38: return "Sitting";
		case 39: case 40: case 41: return "Playing an instrument";
		case 42: case 43: case 44: return "Casting";
		case 45: case 46: case 47: return "Monk strike";
		case 48: return "/agree, /nod";
		case 49: return "/gasp, /amaze, /boggle";
		case 50: return "/plead, /apologize, /grovel";
		case 51: return "/applaud, /clap";
		case 52: return "/bleed, /hungry";
		case 53: return "/blush";
		case 54: return "/giggle, /chuckle, /cackle";
		case 55: return "/cough, /burp";
		case 56: return "/cringe, /duck";
		case 57: return "/puzzle, /curious, /stare";
		case 58: return "/dance";
		case 59: return "/blink, /veto";
		case 60: return "/glare, /eye, /snarl";
		case 61: return "/whistle, /peer, /drool";
		case 62: return "/kneel";
		case 63: return "/laugh";
		case 64: return "/point";
		case 65: return "/shrug, /ponder";
		case 66: return "/ready, /raise";
		case 67: return "/salute";
		case 68: return "/shiver";
		case 69: return "/tap";
		case 70: return "/bow, /thank";
		case 71: return "Stand (after stopping)";
		case 72: return "Fidget 2";
		case 73: return "Sitting fidget";
		case 77: return "/smile";
		default: return "";
		}
	}
}

void CharSelectAnimWindow::AddDefinitions(SidlDefinitions &defs)
{
	if (!defs.WindowTemplate("WDT_Rounded") || defs.Piece("CharSelectAnimWindow"))
		return;
	defs.LoadText(Xml, "CharSelectAnimWindow");
}

bool CharSelectAnimWindow::Build(const SidlDefinitions &defs, std::string &error)
{
	if (!m_window.Build(defs, "CharSelectAnimWindow", error))
		return false;
	SetAllAnimations(m_all);
	return true;
}

void CharSelectAnimWindow::Place(int width, int height)
{
	Rect bounds = m_window.Bounds();
	bounds.x = std::max(width - bounds.w, 0);
	bounds.h = std::min(bounds.h, std::max(height - bounds.y, 0));
	m_window.Move(bounds);
}

void CharSelectAnimWindow::SetAllAnimations(bool all)
{
	m_all = all;
	m_window.Check(all ? "AllButton" : "CombatButton");
}

void CharSelectAnimWindow::SetAnimations(const std::vector<int> &ids)
{
	SidlWindow::Control *list = m_window.Find("AnimList");
	if (!list)
		return;
	int selected = Selected();
	m_ids = ids;
	list->rows.clear();
	list->selected_row = -1;
	for (size_t i = 0; i < m_ids.size(); ++i) {
		int id = m_ids[i];
		std::string name = AnimationName(id);
		list->rows.push_back({render::AnimationCode(id), name.empty() ? "Animation " + std::to_string(id) : name});
		if (id == selected)
			list->selected_row = static_cast<int>(i);
	}
	m_window.UpdateListScroll(*list);
}

int CharSelectAnimWindow::Selected() const
{
	const SidlWindow::Control *list = m_window.Find("AnimList");
	if (!list || list->selected_row < 0 || list->selected_row >= static_cast<int>(m_ids.size()))
		return -1;
	return m_ids[list->selected_row];
}

CharSelectAnimWindow::Action CharSelectAnimWindow::OnMouse(const InputEvent &event)
{
	if (event.type == InputEvent::Type::Wheel) {
		m_window.OnWheel(event.x, event.y, event.wheel);
		return Action::None;
	}
	std::string clicked = m_window.OnMouse(event);
	if (clicked == "CombatButton" || clicked == "AllButton") {
		bool all = clicked == "AllButton";
		SetAllAnimations(all); // a radio button clicked again stays checked
		return Action::ModeChanged;
	}
	if (clicked == "PlayButton" || (clicked == "AnimList" && event.type == InputEvent::Type::DoubleClick))
		return Selected() >= 0 ? Action::Play : Action::None;
	return Action::None;
}

void CharSelectAnimWindow::Draw(Canvas &canvas, TextRenderer &text, SidlTextures &textures, uint64_t now_ms) const
{
	m_window.Draw(canvas, text, textures, now_ms);
}
