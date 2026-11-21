#pragma once

// eqgame's bug report and feedback windows, made with the UI (InitializeGameUserInterface):
//
// CBugReportWnd (@004097ea on, EQUI_BugReportWnd.xml's "BugReportWindow"):
//   - The constructor: Edit's limit 1023 (SetMaxChars 0x3ff; 341 in another language,
//     DAT_00799770), BugTypes filled with the table at 0x5fc120 ({id, name eqstr, prompt eqstr},
//     11 of them: Other, Video, Audio, Pathing, Quest, Tradeskills, Spell stacking,
//     Doors/Portals, Items, NPC, Dialogs), the one whose id is 0 chosen and its prompt in Prompt.
//   - /bugreport (@004fb35a, the new UI) is ResetPositionAndUIState (@00409b68): our position
//     (Position.y, .x, .z, or 0s with no player) kept and shown in LocX, LocY and LocZ ("%.1f"),
//     DupButton, CrashButton, FlagsButton and TargetButton unchecked, TargetName emptied, the
//     window shown. LocationButton does nothing (nothing asks for it).
//   - WndNotification (@00409d08), on a click (1): BugTypes puts its choice's prompt in Prompt;
//     CancelButton closes it (vtbl 0x88); TargetButton unchecked empties TargetName, checked with
//     no target unchecks it again, else keeps the target's id, name and whether it's a player
//     (Type 0, +0x170) and shows its trimName. SendButton, with text in Edit: BugReport_Struct
//     (3340 bytes) as OP_Bug: the choice's name (eqstr) at 0, our name at 64, the kept position
//     at 160 (y, x, z as above, then 0), the minutes played (PCData+0x1358) at 176, the target's
//     id at 188 and name at 192 with flag 4, the flags at 256 (Crash 2, Dup 1, Flags 8), the text
//     (1024 at most) at 2312; then Edit emptied. Either way the window closes. EQMacEmu answers
//     "Thank you for the bug submission, %s." or that reporting is off.
//
// CFeedbackWnd (@0041acaf on, EQUI_FeedbackWnd.xml's "FeedbackWindow"):
//   - The constructor: FBKW_UserName our name, FBKW_FeedbackInput's limit 1023, hidden
//     (CFeedbackWnd::Activate @0041add7 is a Show(0)).
//   - /feedback (@004fb37c, the new UI) shows it (CXWnd::ShowWindow @0041adc2).
//   - WndNotification (@0041adec), on a click: FBKW_SendButton with text in the box sends
//     Feedback_Struct (1131 bytes, from DAT_007f6afb) as OP_Feedback: our name at 0, empty
//     strings at 64, 96 and 101, 0s at 105 and 106, the text at 107; then the box emptied and
//     the window closed. With no text it only closes. FBKW_CancelButton empties the box and
//     closes it.
//
// The old UI's /bugreport and /feedback (DAT_008092d8 0) are CEverQuest's own (@00545dec); not ours.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class WindowManager;

class BugReport
{
public:
	static constexpr const char *Screen = "BugReportWindow";
	static constexpr size_t PacketSize = 3340;

	struct Target
	{
		uint16_t id = 0;
		std::string name;    // pinstTarget->Name, as kept and sent
		std::string display; // trimName's, shown in TargetName
		bool player = false; // Type 0
	};
	struct Host
	{
		std::function<std::string(uint32_t, const std::string &)> text; // eqstr_en.txt's, or the fallback
		std::function<bool(float &, float &, float &)> position;        // Position.y, .x, .z; false with no player
		std::function<bool(Target &)> target;                            // false with none
		std::function<std::string()> name;                               // our name
		std::function<uint32_t()> minutes_played;                        // PCData+0x1358
		std::function<void(const std::vector<uint8_t> &)> send;          // OP_Bug
	};

	void Attach(WindowManager &windows, Host host); // the constructor's
	void Activate();                                // ResetPositionAndUIState
	// A click on the window's controls; false when it isn't ours.
	bool Click(const std::string &window, const std::string &control);

private:
	void SetPrompt(int choice);
	void Send();

	WindowManager *m_windows = nullptr;
	Host m_host;
	float m_y = 0, m_x = 0, m_z = 0; // +0x174, +0x178, +0x17c
	Target m_target;                 // +0x168, +0x16c, +0x170
};

class Feedback
{
public:
	static constexpr const char *Screen = "FeedbackWindow";
	static constexpr size_t PacketSize = 1131;

	struct Host
	{
		std::function<std::string()> name;                      // our name
		std::function<void(const std::vector<uint8_t> &)> send; // OP_Feedback
	};

	void Attach(WindowManager &windows, Host host); // the constructor's
	void Activate();                                // /feedback
	bool Click(const std::string &window, const std::string &control);

private:
	WindowManager *m_windows = nullptr;
	Host m_host;
};
