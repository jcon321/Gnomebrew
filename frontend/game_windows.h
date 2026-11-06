#pragma once

// What eqgame's in-game windows do for themselves every frame, beyond what their SIDL says (their
// OnProcessFrame), for the windows we put up:
//   - CPlayerWnd::UpdatePlayerWindowState (@0042ee3a): the pet's HP gauge only while we have a pet.
//   - CGroupWnd::UpdateButtons (@0041f06e): Invite and Disband, with Follow and Decline (for an
//     invitation) hidden where they overlap them; Invite only with another player targeted, Disband
//     only in a group; a member's pet gauge only while that member has a pet. We have no group yet.
//   - CBuffWindow::RefreshIconArrangement and RefreshBuffDisplay (@004090e9, @00409334): the buff
//     buttons stacked in columns from the top right, as many as fit (the rest hidden); a buff's
//     icon (the spell's, a cell of A_SpellIcons: the window's BuffIcons aren't used) on RedIconBackground for a detrimental spell or
//     BlueIconBackground (SetBuffIcon), its name the tooltip, and under 4 ticks left (but for a
//     bard's own short songs) blinking, fading out over each second of GameTimeMs; an empty slot
//     drawing nothing (SetBuffIcon clears its picture).
//   - CHotButtonWnd::OnProcessFrame (@0041fae9): a hot button shows the text of the button it
//     holds (the Actions window's, or a social's name); an empty one has no text and is checked,
//     and the item slots and spell gems are hidden (we have no item or spell hot buttons yet).
//   - CHotButtonWnd::Activate_HotkeyPage (@00420969): the page number between the arrows.
//   - CActionsWnd::MainPageOnProcessFrame (@00403213): of the main page's buttons that share a place,
//     Follow for an invitation (else Invite), Stand while sitting (else Sit), Walk while running
//     (else Run); Disband only in a group or invited, Invite only with another player targeted.
// And what their page arrows do: CActionsWnd::SocialsPageWndNotification (@00403a68) and
// CHotButtonWnd::WndNotification (@00420462) step through ten pages, round from the last to the first.
//
// Hot buttons, and the button on the cursor (CCursorAttachment):
//   - holding a button on any of the Actions window's pages puts it on the cursor (the pages'
//     WndNotification on 0x16, CCursorAttachment::Activate @00418abb);
//   - holding a hot button takes what's on it onto the cursor, when the cursor has nothing;
//   - clicking a hot button with a button on the cursor puts it there, and whatever was there
//     onto the cursor; without one, it does what the button it holds does (DoHotButton @004209bd);
//   - clicking the world or pressing Escape drops it (CEverQuest_LMouseUp, CDisplay_NewUIProcessEscape
//     through CCursorAttachment::RemoveAttachment @00419474).
// They're kept in <character>_<server>.ini's [HotButtons] as eqgame's CEverQuest_saveCustom and
// loadCustomFromINI (@00548348, @0054870d) keep them: Page<p>Button<b>=<type letter, from A><value>.
//
// Socials (ten pages of twelve, @007d02f0), each a name, a color and five lines of commands:
//   - CEverQuest_initCustom (@00548faf): the first page's are eqstr 6652..6663 ("Afk"...), each
//     with one line, 6664..6675 ("/afk"...); the ini's take their places;
//   - CActionsWnd::SocialsPageOnProcessFrame (@004039f4) and CHotButtonWnd::OnProcessFrame: a
//     social's button shows its name, in the TextColor of the editor's SEW_TC<color>Button;
//   - clicking one on the Actions window starts it, over one that's going
//     (SocialsPageWndNotification); a hot button starts it only when none is (DoHotButton);
//   - right clicking either opens CSocialEditWnd (@00432c80, Activate @00432e1f) on it: the name
//     (15 characters), the lines (255 each) and the color buttons; Accept keeps them
//     (SaveSocialCommandData @00432efc) and closes it, Clear empties the boxes;
//   - EQ_Character::DoPassageOfTime (@004c131d) does the one going, a step every 101 ms or more:
//     a wait to count down, or the next line that isn't empty, through InterpretCmd, and a step's
//     wait after it. A line from "/pau" on waits its number of steps (1..600, from its eighth
//     character on), doing what's after its first comma, if anything is.
// They're kept in [Socials]: Page<p>Button<b>Name, ...Color and ...Line1..5, written only for the
// ones changed since (an empty name takes them all out); the names are cut to 8 as they're read.
//
// The inventory window (CInventoryWnd, @0042128f on):
//   - Activate: ClassAnim shows A_ClassAnim<class, 2 digits>;
//   - ProcessInventoryWindowFrame: the money buttons' counts (UpdateCurrencyDisplays), and the
//     Alt. Adv. label and gauge only from level 51;
//   - CInvSlot::SetItem: a slot with an item shows its icon, A_DragItem's cell icon - 500, and a
//     stack how many are in it;
//   - WndNotification: Done closes it, Skills opens the skills window. Destroy, Alt. Advance and
//     the money buttons (coins onto the cursor) do nothing yet.
// Its slots, and the item on the cursor (core/inventory.h has how the items move):
//   - CInvSlot::HandleLButtonUp (@00421e48), with nothing on the cursor: a click on a slot picks
//     up its item, but a stack of more than one opens the quantity window to say how many (all
//     of it with Shift down, one with Ctrl: CQuantityWnd::Activate @0042f28a doesn't open then);
//     with an item on the cursor, a click puts it in the slot, taking what was there (or tops up
//     the same stack). A worn slot takes only what can go there (CanItemEquipInSlot), and only
//     what our class, race and deity can use ("Your class, deity and/or race may not equip that
//     item."). A button on the cursor goes, and the click goes on. Nothing while the quantity
//     window's up;
//   - CQuantityWnd (@0042f1a0 on): the slider from 0 to the stack's count, starting at all of it,
//     the number in the edit box under it, each following the other (no more than the count can
//     be typed); Accept or Enter takes that many (CInvSlot::SliderComplete), its close box or
//     Escape none; the window opens with its top left at the slot's middle, kept on the screen;
//   - CInventoryWnd::HandleLButtonUp (@004217c8): a click on IW_CharacterView with an item on
//     the cursor puts it away (AutoInventory);
//   - CCursorAttachment::Activate's item (2): its icon (CA_Anim, A_DragItem) and a stack's count
//     in font 1 in a dark box at its corner (DrawCursorAttachmentText). Escape and clicking the
//     world leave it there (RemoveAttachment won't take an item; the world click would drop it,
//     which we don't do yet), and nothing goes on the cursor over it (IsOkToActivate).
// The skills window (CSkillsWnd, @0043273a on): Activate lists the skills we have, by number,
// then the languages we know some of; UpdateSkill puts each one's name, rank and value in the list
// (white, the headings 0x8aa3ff, rows 16 high, the rank and value justified 1). We go by what
// the server sends: a skill (0..73) that isn't 254 or 255; eqgame's EQ_Character::HasSkill asks
// its class and level table, and also lists racial skills (a halfling's sneak) the server marks 255.
//
// The item display (CItemDisplayWnd, @00423331 on), on the item in an inventory slot:
//   - CInvSlot::HandleLButtonUp (@00421e48): a click with Alt down shows it, and does nothing else;
//   - CInvSlotWnd::HandleRButtonHeld (@005a7f30, DisplayItemIfValid @00422987): the right button
//     held on a slot shows it, and HandleRButtonUpAfterHeld (@005a7fc0,
//     CheckItemDisplayWindowAndActivat @004229cf) closes it when the button's let go over a slot;
//   - SetItem and Activate (@00423640, @00423606): the item's name for the title, its description
//     (item_display.h) in ItemDescription, its icon (A_DragItem's cell icon - 500) on IconButton,
//     and the window shown on top. It stays as it was shown until it's shown again;
//   - msg_request_inspect_item: a link's item, when the zone sends it for a click on the link;
//   - WndNotification (@00425cf6): a click on the icon puts the item's tag in the chat input.

#include "sidl.h"
#include "zone_session.h"

#include <cstdint>
#include <functional>
#include <string>

class Canvas;
class EqStr;
class GameValues;
class IniFile;
class TextRenderer;
class WindowManager;
class ZoneSession;

// A hot button (eqgame's ten pages of ten @007f6860): which kind of button it holds, and which one.
struct HotButton
{
	enum Type : uint8_t
	{
		Empty,
		Combat,      // the combat page's Melee Attack (0) and Range Attack (1)
		CombatSkill, // the combat page's four skill buttons, 0..3
		Ability,     // the abilities page's six, 0..5
		Social,      // page * 12 + button
		Item,        // an inventory slot
		Action,      // the main page's Who, Invite, Disband, Camp, Sit, Run (0..5); 7.. the menu buttons
		Spell,       // a spell gem
		PetCommand,  // a pet window button
	};
	Type type = Empty;
	int value = -1;
};

constexpr int HotButtonPages = 10;
constexpr int HotButtonsPerPage = 10;

struct Social
{
	std::string name;
	std::string lines[5];
	int color = 0;        // which of the editor's SEW_TC<n>Button's colors (0..19)
	bool changed = false; // since saved (@007d0278)
};

constexpr int SocialPages = 10;
constexpr int SocialsPerPage = 12;
constexpr int SocialLines = 5;

// The game windows' own state: the page of socials the Actions window shows and the page of hot
// buttons, 0..9 (eqgame's globals @007f67f9 and __HotkeyPage), the hot buttons, the button on
// the cursor, and the socials.
struct GameWindowState
{
	int social_page = 0;
	int hot_button_page = 0;
	HotButton hot_buttons[HotButtonPages * HotButtonsPerPage];
	HotButton cursor;        // Empty: nothing on the cursor
	std::string cursor_text; // its text when it was picked up
	// A spell from the book on the cursor (CCursorAttachment type 1): its book slot (-1: none) and
	// its icon, a cell of A_SpellIcons. A button and a spell are never on it together.
	int cursor_spell = -1, cursor_spell_icon = -1;
	bool hot_buttons_changed = false; // since they were last saved

	Social socials[SocialPages * SocialsPerPage];
	bool socials_changed = false;
	// __CurrentSocial: the social going (-1: none), its next line, the steps to wait before it,
	// and when the last step was (@0063db18).
	int current_social = -1, social_line = 0, social_wait = 0;
	uint64_t social_step_ms = 0;
	// The social editor's: the social it's on, and the color picked.
	int edit_social = 0, edit_color = 0;

	void ClearCursor() { cursor = {}; cursor_text.clear(); cursor_spell = cursor_spell_icon = -1; } // RemoveAttachment
	bool Attached() const { return cursor.type != HotButton::Empty || cursor_spell >= 0; } // a button or a spell

	// The quantity window's: the slot it takes from (-1: it isn't up), the stack's count, and
	// the slider's value and the edit box's text as last seen, to tell which one changed.
	int quantity_slot = -1, quantity_max = 0;
	// Or the coin a money button opened it for (0 platinum .. 3 copper; -1 none) and where the
	// coins are (Inventory::Place: the purse, the bank): CQuantityWnd's window to notify.
	int quantity_coin = -1, quantity_place = 0;
	// Or another window's: what it does with the number (the merchant's Buy and Sell).
	std::function<void(int)> quantity_done;
	bool QuantityUp() const { return quantity_slot >= 0 || quantity_coin >= 0 || quantity_done != nullptr; }
	int quantity_value = 0;
	std::string quantity_text;

	ZoneItem display_item; // the item display's (id 0: none yet)

	// DAT_007d0254: a request the zone has yet to answer (a combine); the slots take no clicks.
	bool busy = false;
	// The bank window's up: coins change into others (CEverQuest_MoveMoney).
	bool bank_open = false;
	// A trade (or the give window) is up, and with an NPC: our empty trade slots take items.
	bool trading = false, trade_npc = false;
	bool fv_rules = false; // OP_LogServer's: anything can be dropped (CanDrop)
};

// A button clicked in one of the windows: the page arrows and the hot buttons. A hot button that
// holds one of the Actions window's main page buttons returns that button's ScreenID, to be done
// as if it had been clicked there; otherwise "".
std::string GameWindowClick(WindowManager &windows, GameWindowState &state, const std::string &window, const std::string &control);
// A button held down: onto the cursor.
void GameWindowHold(WindowManager &windows, GameWindowState &state, const std::string &window, const std::string &control);
// CHotButtonWnd::DoHotButton from the keymap: the showing page's button `slot` (0..9) run as a
// click on it with nothing on the cursor would; what it returns is as GameWindowClick's.
std::string DoHotButton(WindowManager &windows, GameWindowState &state, int slot);
// A button right clicked: a social's opens the social editor on it.
void GameWindowRightClick(WindowManager &windows, GameWindowState &state, const std::string &window, const std::string &control);
// CInvSlot::SetItem: an InvSlot showing `item` (null: none), its icon, its stack's count and its tooltip.
void FillInvSlot(SidlWindow::Control &control, const ZoneItem *item, const GameValues &values);
// The item slot a shown InvSlot shows (its window's, or its EQType); -1 if the control isn't one.
int InvSlotAt(WindowManager &windows, const std::string &window, const std::string &control);
// The item in an inventory slot; null if the control isn't a slot with an item.
const ZoneItem *SlotItem(WindowManager &windows, const GameValues &values, const std::string &window, const std::string &control);
// An inventory slot's item in the item display; false if the control isn't a slot with an item.
bool ShowItemDisplay(WindowManager &windows, const GameValues &values, GameWindowState &state, const std::string &window,
	const std::string &control);
void ShowItemDisplay(WindowManager &windows, const GameValues &values, GameWindowState &state, const ZoneItem &item);
// The right button let go over a slot after holding it: the item display closes.
void GameWindowRightRelease(WindowManager &windows, const std::string &window, const std::string &control);

// A click in the inventory window (a slot or the character view) or the quantity window, with
// what it did: whether to play eqgame's WavePlay 0x92 (an item put somewhere, or picked up whole)
// and an eqstr message for the chat (0 none). False when it wasn't one of theirs.
struct InventoryClickResult
{
	bool sound = false;
	uint32_t message = 0;
	int16_t close_bag = -1; // a bag picked up or swapped out: its window closes (CloseContainer)
	bool coin_sound = false; // WavePlay 0x8a: coins onto the cursor
	int message_color = 13;  // the message's chat color type (ChatType::Red)
	bool trade_changed = false; // an item went into the trade window (TradeItemChanged)
};
// A money button (the inventory's IW_Money0..3, the bank's BW_Money0..3), or the inventory's
// character view with coins on the cursor: CInventoryWnd and CBankWnd::ClickedMoneyButton. Coins on
// the cursor go onto its pile; without, the quantity window asks how many come off it onto the
// cursor. False when it isn't one.
bool MoneyClick(WindowManager &windows, GameWindowState &state, ZoneSession &zone, const std::string &window,
	const std::string &control, bool shift, bool ctrl, InventoryClickResult &result);
bool InventoryClick(WindowManager &windows, GameWindowState &state, ZoneSession &zone, const std::string &window,
	const std::string &control, bool shift, bool ctrl, InventoryClickResult &result);
// Enter in the quantity window's edit box: as Accept.
bool QuantityEnter(WindowManager &windows, GameWindowState &state, ZoneSession &zone, InventoryClickResult &result);
// CQuantityWnd::Activate for another window: how many of `count`, the window at (x, y) (-1: centered
// on the screen); `done` gets the number, at once when Shift, Ctrl or a count of one says it, or
// when the window's accepted. The slider starts at `start` (-1: all of it).
void AskQuantity(WindowManager &windows, GameWindowState &state, int count, int x, int y, bool shift, bool ctrl,
	std::function<void(int)> done, int start = -1);
void UpdateGameWindows(WindowManager &windows, const GameValues &values, GameWindowState &state);

// A skill's name (eqgame's __SkillDict, from InitializeSkillData), from eqstr (null: English).
std::string SkillName(const EqStr *eqstr, int skill);

// The item or button on the cursor, drawn at the mouse (x, y), over the windows and under the cursor.
void DrawCursorAttachment(Canvas &canvas, TextRenderer &text, const WindowManager &windows, const GameWindowState &state,
	const GameValues &values, int x, int y, uint64_t now_ms);

void LoadHotButtons(const IniFile &ini, GameWindowState &state);
void StoreHotButtons(IniFile &ini, const GameWindowState &state);

// The first page's socials, from eqstr (null: its English).
void DefaultSocials(GameWindowState &state, const EqStr *eqstr);
void LoadSocials(const IniFile &ini, GameWindowState &state);
void StoreSocials(IniFile &ini, GameWindowState &state); // the changed ones, which aren't any more

// The social going's step, if it's time for one: the line to do (InterpretCmd), or "".
std::string SocialStep(GameWindowState &state, uint64_t now_ms);
