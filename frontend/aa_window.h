#pragma once

// eqgame's alternate advancement window, CAAWnd (@00401000 on, the AAWindow screen), over the
// abilities of core/alt_adv.h:
//   - CAAWnd::CAAWnd: its five lists (List1..5, on the TabBox Subwindows' pages Page1..5) white with
//     yellow under the mouse and gray selected (SetColors), the headings 0x8aa3ff, the name
//     left-justified and the rank and cost centered; Train and Hotkey disabled; nothing selected
//     on the first page; then Init;
//   - Init: each ability the class has (Add_MGB_for_Beastlords_fix) and that's in use (0x1c) onto
//     its category's page (0x21: general, archetype, class, PoP advance, PoP ability), its name a
//     row of the page's list;
//   - Update, as it's activated and as the zone's AA packets come: ExpCount the share of experience
//     ("%d%%"), "-" and "+" (LessExpButton, MoreExpButton) by it, from level 51 with Luclin or the
//     Planes of Power (the world's OP_ExpansionInfo bits 4 and 8: parseUIRefreshFlags); each row's
//     rank ("%d/%d") and next rank's cost (none at the last rank), white if it can be trained now
//     (CanTrainAbility) and 0xc0c0c0 if not; CurrentCount our points to spend, TotalCount those
//     spent (TotalPointsInSkill); then UpdateSelected;
//   - UpdateSelected: Description (an STML box) eqstr 3108 with nothing selected; else "(ID#n) "
//     for a GM, "Type: Activated, Refresh Time:" (3109) and its reuse " h:mm:ss" or 3110 for a
//     passive one, <BR>, the requirements (GetAbilityReqs), <BR>, its description; Train enabled
//     as it can be trained; UpdateTimer;
//   - UpdateTimer, and every 500 ms while an activated ability we have is selected
//     (OnProcessFrame): Timer "Ready" (3111) or the reuse left "%02d:%02d:%02d"; Hotkey enabled
//     once we have it;
//   - WndNotification: Done; "-" and "+" a tenth of our experience less or more to AAs (OP_AAAction
//     "on <n>" or "off": CAAWnd_WndNotification_AAAction); Train ("buy <id>", Train disabled till
//     the zone answers); Hotkey (the ability onto the cursor: CCursorAttachment::Activate type 4,
//     a social of 0x78 + its id); a row clicked selects its ability; another page selects nothing.
//   - ShowAbility, from an ability's hot button right-clicked (DoHotButtonRightClick): the window
//     up and the ability selected, as eqgame does it (the list that was showing gets the row; the
//     tab stays).

#include "../core/alt_adv.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class GameValues;
class SidlWindow;
class WindowManager;

class AAWindow
{
public:
	static constexpr const char *Screen = "AAWindow";
	static constexpr int Pages = 5; // 0x138

	struct Host
	{
		std::function<void(const std::string &)> send; // OP_AAAction's text
		std::function<void(int)> hotkey;               // Hotkey: the ability (its id) onto the cursor
		std::function<uint32_t()> world_expansions;    // OP_ExpansionInfo's bits
		std::function<void(int)> set_percentage;       // our share of experience to AAs, as eqgame sets its own
	};

	// The constructor, for windows just built (a new zone's).
	void Attach(WindowManager &windows, const GameValues &values, Host host);
	void Activate();             // CAAWnd::Activate: Init if it hasn't been, Update, up
	void Toggle();               // the inventory's button, the keymap's command: Activate, or down
	bool Shown() const;
	void Update();               // CAAWnd::Update
	void Frame(uint64_t now_ms); // CAAWnd::OnProcessFrame
	bool Click(const std::string &window, const std::string &control);
	void ShowAbility(int id);    // CAAWnd::ShowAbility

private:
	SidlWindow *Window() const;
	AltAdv::Character Character() const;
	void Init();
	void UpdateSelected();
	void UpdateTimer();
	int SelectedId() const; // the selected ability's id; -1 none

	struct Entry
	{
		int id = 0;
		int row = 0; // its row in the page's list
	};

	WindowManager *m_windows = nullptr;
	const GameValues *m_values = nullptr;
	Host m_host;
	bool m_inited = false;            // 0x13c
	std::vector<Entry> m_pages[Pages]; // 0x1a8 on, 50 a page; their counts at 0x194
	int m_page = 0;                   // 0x97c
	int m_selected = -1;              // 0x978: an index into the page's entries
	int m_shown_unspent = -1, m_shown_total = -1, m_shown_percent = -1; // 0x980, 0x984, 0x988
	uint64_t m_now_ms = 0, m_timer_ms = 0; // 0x98c
};
