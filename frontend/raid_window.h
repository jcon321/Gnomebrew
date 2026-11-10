#pragma once

// eqgame's raid window, CRaidWnd (@0042fc89 on, the RaidWindow screen), and raid options window,
// CRaidOptionsWnd (@0042f6db on, RaidOptionsWindow). CRaid (frontend/raid.*) keeps the raid and
// calls these as it changes; the lists hold the members as text, found again by name.
//   - CRaidWnd::CRaidWnd: both lists white with yellow under the mouse and gray selected
//     (SetColors), the headings 0x8aa3ff, Grp and Lvl centered, and the class colors
//     (LoadClassColors: [RaidWindow] ClassColor0..14 from the UI ini, -1 white, written when
//     missing);
//   - RAID_PlayerList is twelve groups of seven rows (ClearPlayerList: six rows with the group's
//     number and "empty" in gray, then a separator, 83 rows), a group's rows from group * 7, its
//     leader in the first; RAID_NotInGroupPlayerList one row a member outside the groups. Columns:
//     the group, the name, the level, the class title (in its class's color: SetPlayerClassColor?),
//     the rank ("Group Leader", "Raid Leader");
//   - AddPlayertoList: into the group's first "empty" row (FindEmptyRaidListItem), a group leader
//     into its first row (the one there moving to the empty row), or onto the end of the other list;
//     RemovePlayerFromList: "empty" again and the rows under it up one (ResortRaidGroupList), or the
//     row taken out; ChangePosition: out of one, into the other; ChangeRaidGroupLeader: the new leader
//     and the old (or the group's first row) swap rows; SetRaidMemberRank, UpdateRaidWindowDisplay (a
//     rename), SetRaidCount and UpdateRaidWindowLevelDisplay ("%d"), UpdateRaidTargetHighlight (the
//     member selected's name red, the one before white again);
//   - UpdateRaidWindowUI, as it's activated and every 250 ms (ProcessRaidWindowFrame): Accept and
//     Decline while invited, Invite and Disband otherwise; Disband and Options enabled in a raid;
//     with another player targeted, Invite out of a raid or as its leader with room, and Make Leader
//     as its leader; Add and Remove Looter as its leader;
//   - WndNotification: the buttons (CreateInviteRaid, SendInviteResponse, RemoveRaidMember,
//     SendLeadershipChange of the selection, AddRaidLooter, RemoveRaidLooter, the options window),
//     and a row clicked selects its member (CRaid::SetTargetRaidPlayer);
//   - CRaidOptionsWnd: the looter list (ClearLooterList, AddLooterToList, filled by
//     CRaid::UpdateOptionsWindow), and as it's activated (UpdateRaidOptionsUI) the loot type as a
//     number and in the combo box, which only the raid's leader can change (CRaid::SetLootType);
//     fifteen class buttons, each's text in its class color (InitializeClassColor), open the color
//     picker, whose color goes in the ini and the list (0x1e: CRaid::UpdateClassColor).

#include <cstdint>
#include <functional>
#include <string>

class GameValues;
class Raid;
class SidlWindow;
class WindowManager;

class RaidWindow
{
public:
	static constexpr const char *Screen = "RaidWindow";
	static constexpr const char *OptionsScreen = "RaidOptionsWindow";
	static constexpr int Classes = 15; // the class colors, alphabetical (bard first)

	struct Host
	{
		// ColorPickerWnd_Activate: up with a color (0xRRGGBB), `done` given the one accepted.
		std::function<void(uint32_t, std::function<void(uint32_t)>)> pick_color;
		// The UI ini's [RaidWindow] ClassColor<n>: "" when there's none.
		std::function<std::string(const std::string &)> load;
		std::function<void(const std::string &, const std::string &)> save;
	};

	// The constructors, for windows just built (a new zone's).
	void Attach(WindowManager &windows, Raid &raid, const GameValues &values, Host host);
	void Activate();        // CRaidWnd::Activate: UpdateRaidWindowUI, the lists again, up
	void Toggle();          // /raidwindow and Toggle Raid Window: Activate, or down
	void Frame(uint64_t now_ms); // ProcessRaidWindowFrame
	bool Click(const std::string &window, const std::string &control);

	// CRaidWnd's lists. A group is 0..11, or 0xffffffff for the other list.
	void ClearPlayerList();
	void AddPlayer(const std::string &rank, const std::string &name, const std::string &class_title,
		const std::string &level, uint32_t group, int class_id, bool group_leader);
	void RemovePlayer(const std::string &name, uint32_t group);
	void ChangePosition(const std::string &name, uint32_t from, uint32_t to, int class_id, bool group_leader);
	void ChangeGroupLeader(const std::string &name, uint32_t group, const std::string *previous);
	void SetRank(const std::string &name, const std::string &rank, uint32_t group);
	void Rename(const std::string &name, const std::string &old, uint32_t group);
	// UpdateRaidTargetHighlight: `name` red, `old` white again (either empty: none).
	void Highlight(const std::string &name, uint32_t group, const std::string &old, uint32_t old_group);
	void SetRaidCount(int count);
	void SetLevelAverage(int level);
	void SetClassColor(int index, uint32_t color) { m_class_colors[index] = color; }
	void SetPlayerClassColor(const std::string &name, int class_id, uint32_t group);

	// CRaidOptionsWnd's looter list.
	void ClearLooterList();
	void AddLooterToList(const std::string &name);

private:
	SidlWindow *Window() const;
	SidlWindow *Options() const;
	void UpdateUI();                 // UpdateRaidWindowUI
	void ActivateOptions();          // CRaidOptionsWnd::Activate
	int FindEmptyRow(uint32_t group) const;                          // FindEmptyRaidListItem
	int FindInGroup(const std::string &name, uint32_t group) const;  // FindPlayerIndexInGroup
	int FindNotInGroup(const std::string &name) const;               // FindIndexNotInGroupList
	void Resort(int row, uint32_t group);                            // ResortRaidGroupList
	uint32_t ClassColor(int class_id) const; // SetPlayerClassColor's switch; 0 for no class

	WindowManager *m_windows = nullptr;
	Raid *m_raid = nullptr;
	const GameValues *m_values = nullptr;
	Host m_host;
	uint32_t m_class_colors[Classes] = {}; // ClassColors, 0xRRGGBB
	uint64_t m_updated_ms = 0;             // 0x1ac
};
