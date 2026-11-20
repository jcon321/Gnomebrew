#pragma once

// Inspecting a player, eqgame's new UI side of it (CInspectWnd @00420e01 on, EQUI_InspectWnd.xml's
// "InspectWnd"):
//   - CEverQuest::RightClickedOnPlayer ends with CEverQuest_doInspect (@0054390e) when nothing's
//     on the cursor, /inspect is on (DAT_007cf28c) and our deity isn't 20000; ourselves clicked
//     goes straight there. doInspect takes a player (Type 0) only: 20 or farther says eqstr 12378
//     ("%1 is too far away to inspect.  Get closer.", print_chat_dsp_chat_2), else
//     OP_InspectRequest {their id, ours}, which the zone hands them.
//   - msg_req_inspect (@004e769d), their request for us (0x40b5): no more than one answer a second
//     (DAT_007f6f68); "%1 is looking at your equipment..." (1404, color 0x111) when it's someone
//     else and [Defaults] ShowInspectMessage is on (DAT_007cf208; on with no key); then
//     OP_InspectAnswer to them with our slots 0..21 and our inspect text (ZoneSession::InspectAnswer).
//   - The answer (0x40b6, CInspectWnd_Activate @004e781c, the new UI's msg_inspect @00420efe): the
//     window's items forgotten (InitializeInspectWindowData), the spawn at +2 looked up (none:
//     nothing); farther than 20, 1402 ("%1 is too far away to inspect.  Get closer.", 0x111); else the title "You are
//     inspecting %1." (1401), said in color 10 too when it isn't us, INSW_Edit editable only for us
//     (+0x51) with the text at +0x5b0, the 22 slots' names (+4, 64 each) and icons (+0x584), and
//     the window shown. InvSlot1..21 (EQTypes 8001..8021) show entries 1..21
//     (CInvSlotMgr::UpdateSlots: pinstCInspectWnd + 0x140 + (type - 8000) * 4); entry 0, the
//     cursor's, has no slot. Clicks on them do nothing (HandleLButtonUp and HandleRButtonUp return
//     for 8000..8030, MoveItem for 8000..8029), Alt included.
//   - CheckInspectionDistance (@00421112): once a second, the one inspected farther than 20 closes
//     it; PlayerBeingDeleted (@00421238) closes it when they go.
//   - WndNotification (@0042117c): Enter in INSW_Edit (6) is AcceptInspectText, DoneButton closes
//     it. Deactivate (@004210d3): AcceptInspectText, the items forgotten, hidden.
//   - AcceptInspectText (@004211c1): inspecting ourselves, the box's text is ours (DAT_007f69fa),
//     kept by CEverQuest_saveCustom as [InspectText] Text in the character's ini.
//   - INSW_Edit's limit (SetMaxChars): 254 in English (DAT_00799770 0), 84 otherwise.

#include "zone_session.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class GameValues;
class IniFile;
class WindowManager;

class InspectWindow
{
public:
	static constexpr const char *Screen = "InspectWnd";
	static constexpr const char *EditId = "INSW_Edit";
	static constexpr int Slots = 22;
	static constexpr float Range = 20;

	struct Host
	{
		// eqstr_en.txt's string `id` with its %1.. filled in, or `fallback`.
		std::function<std::string(uint32_t, const std::vector<std::string> &, const std::string &)> text;
		std::function<void(const std::string &, int)> chat; // print_chat_dsp_chat, in a color type
		std::function<void()> save;                          // CEverQuest_saveCustom
	};

	void Attach(WindowManager &windows, ZoneSession &zone, const GameValues &values, Host host);
	void Load(const IniFile &ini);  // loadCustomFromINI's [InspectText] Text
	void Store(IniFile &ini) const; // saveCustom's
	void Reset();                   // a new zone: closed

	const std::string &Text() const { return m_text; } // DAT_007f69fa
	bool show_message = true;                          // DAT_007cf208: [Defaults] ShowInspectMessage

	void DoInspect(uint16_t spawn);                // CEverQuest_doInspect
	void Requested(uint16_t from, uint64_t now_ms); // msg_req_inspect
	void Answered(const ZoneSession::Event &event); // msg_inspect
	void SpawnRemoved(uint16_t id);                 // PlayerBeingDeleted
	void Frame(uint64_t now_ms);                    // CheckInspectionDistance, the slots, the close box
	void Deactivate();
	bool Active() const { return m_active; }
	uint16_t Inspected() const { return m_inspected; }
	// Any click on the window (its slots do nothing); DoneButton closes it. False if not ours.
	bool Click(const std::string &window, const std::string &control);
	// Enter typed in INSW_Edit (WndNotification 6): AcceptInspectText. False when it hasn't the focus.
	bool Enter();

private:
	std::string Say(uint32_t id, const std::vector<std::string> &args, const std::string &fallback) const;
	void AcceptInspectText();
	float Distance(uint16_t id) const;
	void Forget(); // InitializeInspectWindowData

	WindowManager *m_windows = nullptr;
	ZoneSession *m_zone = nullptr;
	const GameValues *m_values = nullptr;
	Host m_host;
	std::string m_text;               // DAT_007f69fa: ours
	bool m_active = false;            // +0x134
	uint16_t m_inspected = 0;         // +0x13c
	uint64_t m_check_ms = 0;          // +0x138: the next distance check
	uint64_t m_answered_ms = 0;       // DAT_007f6f68: our last answer
	bool m_answered = false;
	std::array<ZoneItem, Slots> m_items; // +0x140: each a name and an icon (EQ_Equipment's +0xb6)
	std::array<bool, Slots> m_has{};
};
