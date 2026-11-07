#pragma once

// The spellbook and the spell gems: eqgame's CSpellBookWnd (@0043412e on) and CCastSpellWnd
// (@0040a192 on), and what they ask of the zone.
//
// The book (SpellBookWnd):
//   - Activate (@0043441f): casters only (EQ_Character::IsSpellcaster: not warriors, monks or
//     rogues), not while moving forward ("You must be completely stopped..."), and not kneeling,
//     lying or feigning (stances 105, 115, 102). It sits us down, remembering whether we were
//     sitting already, and opens where it was.
//   - Deactivate (@0043450a): closes it, stands us back up if it sat us, and stops what it was
//     doing (StopSpellBookAction @00435531: "Aborting memorization of spell." and so on). Done, the
//     close box, Escape (CDisplay_NewUIProcessEscape) and CSPW_SpellBook close it, and so does
//     standing up any other way (EQPlayer::ChangeStance's DeacSpellScreen).
//   - Its 256 slots are pages of eight, two pages showing (SpellBookIndex, the left page: 0..30);
//     SBW_Spell<n> is slot page * 8 + n. The arrows and the Left and Right keys turn two pages
//     (TurnToPage, HandleKeys), and SBW_LeftPageNum and RightPageNum show the pages from 1.
//   - UpdateSpellBookDisplay (@004352ff): a slot with a spell shows its icon (A_SpellIcons' cell
//     new_icon) over RedIconBackground for a detrimental spell, BlueIconBackground for the rest
//     (CBuffWindow::SetBuffIcon), and its name in SBW_SpellName<n>; an empty slot is
//     A_SpellBookSlot. The selected slot (a right click) flashes: none, red, blue.
//   - Clicking a slot (WndNotification @0043472e), only while sitting: with a scroll on the cursor
//     and the slot empty, StartSpellScribe (@00434da1, its checks and their messages); with
//     anything else, StartSpellMemorizationDrag (@004348f8): the spell onto the cursor
//     (CCursorAttachment type 1), unless it's memorized already or above our level.
//   - Scribing and memorizing take 92 ticks, a tick every (the spell's level - ours + 5) * 100 ms,
//     at least 100 (GetSpellScribeTicksLeft @004350b9, GetSpellMemTicksLeft @00434c05, which the
//     gauges' EQTypes 10 and 9 call); memorizing takes 1 a tick, 3 + meditate / 10 with meditate,
//     and a bard's (INT + DEX) / 30 + 1 more. At the end OP_MemorizeSpell goes (0 scribe, 1
//     memorize), and the spell bar waits for the zone's answer (DAT_007d0254); FinishScribing and
//     FinishMemorizing (@0043501f, @00434b38) on the answer. A memorized spell can be cast a second
//     and its recast time later.
//   - Right clicking a slot (HandleRightClickOnSpell @004347a1), sitting: selects it (or the
//     selected one again: no more); with one selected, OP_SwapSpell for the two. The zone sends
//     it back and the two trade places (SwapSpellBookSlots @004351b9).
//   - Delete with a deletable spell selected (HandleKeys, ConfirmSpellMemorization @004355c0): the
//     confirmation dialog (category 0x22, 10 seconds), and Yes asks OP_DeleteSpell
//     (RequestSpellDeletion @004356a1); the zone's answer empties the slot (DeleteSpellFromBook).
//   - OnProcessFrame (@00434554): every 200 ms, what it's doing stops if it's hidden or minimized,
//     or the scroll it's scribing left the cursor.
// The gems (CastSpellWnd):
//   - a click with a book spell on the cursor memorizes it into an empty gem (HandleSpellLeftClick
//     @0040a4f5, BeginMemorize @00434a05: a gem with a spell takes none); without one it casts the
//     gem's spell (SpellCast), and so does /cast n;
//   - a right click forgets the gem's spell (OP_MemorizeSpell 2; ForgetMemorizedSpell @0040a662);
//   - CSPW_SpellBook opens and closes the book, and is down while it's open;
//   - alt+click or the right button held on a gem (DisplaySpellInfoForGemSlot @0040a480), and on a
//     book slot (DisplaySpellInfo, DisplaySpellDetails @0043525a), shows its spell in the item
//     display, or closes it if it's showing anything (CItemDisplayWnd::SetSpell @00425957); the
//     held button let go closes it again (the book's toggling it once more);
//   - each gem shows its spell's icon (A_SpellGems' cell new_icon) on a ring in the color of the
//     spell's target type (UpdateSpellGems @0040a8b7), dimming while it can't be cast and
//     brightening back once it can (RefreshSpellGemButtons @0040a7d3, ProcessFrameAnimation): its
//     recast and the last cast's recovery past, and nothing being cast (SpellCast::GemReady).

#include "widgets.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class EqStr;
class GameValues;
class SpellCast;
class SpellFile;
class WindowManager;
class ZoneSession;
struct GameWindowState;
struct SpellInfo;

class SpellBook
{
public:
	struct Host
	{
		std::function<void(uint8_t)> change_stance;        // EQPlayer::ChangeStance, for us
		std::function<bool()> moving_forward;              // our MovementSpeed > 0
		std::function<void(const std::string &, int)> chat; // a line in a ChatType's color
	};

	void Attach(WindowManager &windows, GameWindowState &state, ZoneSession &zone, const GameValues &values,
		SpellCast &cast, const SpellFile *spells, const EqStr *eqstr, Host host);
	void Reset(); // a new character: closed, at the first page, nothing going

	bool Activated() const { return m_activated; }
	void Activate(uint64_t now_ms);
	void Deactivate();
	void StoodUp(); // ChangeStance from sitting (DeacSpellScreen)
	void TurnToPage(int page); // the left page, 0..30 (two pages showing)

	// A click in the book, the gems or the confirmation dialog; false if it wasn't one of theirs.
	bool Click(const std::string &window, const std::string &control, bool alt, uint64_t now_ms);
	bool RightClick(const std::string &window, const std::string &control);
	// The right button held on a book slot or a gem, and let go after holding: the spell in the
	// item display (as alt+click), then closed. False if it wasn't one of theirs.
	bool RightHold(const std::string &window, const std::string &control);
	bool RightRelease(const std::string &window, const std::string &control);
	// CCastSpellWnd::HandleSpellLeftClick: a gem's left click, or /cast's (0..7).
	void GemLeftClick(int gem, uint64_t now_ms);
	// HandleKeys: Left and Right turn the pages (true: the key's taken), Delete asks to delete the
	// selected spell.
	bool Key(InputEvent::Key key, uint64_t now_ms);
	// What happens every frame: OnProcessFrame, the timers, and what the windows show.
	void Frame(uint64_t now_ms);
	void ZoneEvent(int type, int id, int value); // a ZoneSession::Event's

	// The gauges: 0..1000, how much of the memorizing (EQType 9) or scribing (10) is left.
	int MemorizeGauge() const;
	int ScribeGauge() const;

private:
	const SpellInfo *Spell(int id) const;
	std::string Text(uint32_t id, const std::string &fallback, const std::vector<std::string> &args = {}) const;
	void Say(uint32_t id, const std::string &fallback, int type, const std::vector<std::string> &args = {});
	uint8_t Stance() const;
	int Level() const;
	int ClassId() const;
	int ClassLevel(int spell) const; // ours for the spell: 255 never
	bool HasSpell(int spell) const;
	bool Memorized(int spell) const;
	void StopAction();
	void ClickSlot(int slot);
	bool ShowSlotInfo(const std::string &control); // a book slot's spell in the item display
	bool ShowGemInfo(const std::string &control);  // a gem's
	void RightClickSlot(int slot);
	void StartMemorizationDrag(int slot);
	void StartScribe(int slot, uint64_t now_ms);
	bool BeginMemorize(int slot, int gem);
	void ConfirmDelete(int slot, uint64_t now_ms);
	void CloseConfirmation();
	void MemorizeTicks(uint64_t now_ms);
	void ScribeTicks(uint64_t now_ms);
	void UpdateBook(uint64_t now_ms);
	void UpdateGems(uint64_t now_ms);
	int TickMs(int spell) const;

	WindowManager *m_windows = nullptr;
	GameWindowState *m_state = nullptr;
	ZoneSession *m_zone = nullptr;
	const GameValues *m_values = nullptr;
	SpellCast *m_cast = nullptr; // the spell bar's waiting (DAT_007d0254) and the gems' readiness
	const SpellFile *m_spells = nullptr;
	const EqStr *m_eqstr = nullptr;
	Host m_host;

	bool m_activated = false;
	bool m_was_sitting = false; // CachedSittingState
	int m_page = 0;             // SpellBookIndex
	int m_selected = -1;        // the right clicked slot
	int m_flash = -1;           // the selected slot's background: -1 none, 0 red, 1 blue
	uint64_t m_flash_ms = 0;
	int m_mem_gem = -1, m_mem_spell = -1, m_mem_ticks = 0;          // MemorizingSpellIndex, Id, MemTicksLeft
	int m_scribe_slot = -1, m_scribe_spell = -1, m_scribe_ticks = 0; // ScribeIndex, its spell, ScribeTicksLeft
	uint64_t m_mem_tick_ms = 0, m_scribe_tick_ms = 0;              // @006306dc, @006306e0
	uint64_t m_checked_ms = 0;  // OnProcessFrame's Timestamp
	// The confirmation dialog: the slot it asks about (-1: it isn't up) and when it gives up.
	int m_confirm_slot = -1;
	uint64_t m_confirm_until_ms = 0;
	// The gems: the state RefreshSpellGemButtons gives each (1 dimming, 2 brightening, 0 neither)
	// and the step it's at, 0..10.
	int m_gem_state[8] = {}, m_gem_fade[8] = {};
	bool m_gem_shown[8] = {};   // its icon set since the spell changed (spellicon != -1)
	uint64_t m_fade_ms = 0;
};
