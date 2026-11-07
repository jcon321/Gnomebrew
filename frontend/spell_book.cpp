#include "spell_book.h"
#include "chat_text.h"
#include "game_values.h"
#include "game_windows.h"
#include "inventory.h"
#include "spell_cast.h"
#include "spell_file.h"
#include "window_manager.h"
#include "zone_session.h"

#include <algorithm>

namespace
{
	constexpr int SlotButtons = 16;  // SBW_Spell0..15: two pages
	constexpr int SlotsPerPage = 8;
	constexpr int LastPage = 30;     // TurnToPage's: the left page of the last two
	constexpr int Ticks = 92;        // to scribe or memorize
	constexpr int GemCount = ZoneProfile::Gems;
	constexpr uint64_t CheckMs = 200; // OnProcessFrame's
	constexpr uint64_t ConfirmMs = 10000;
	// eqgame steps the selected slot's flashing and the gems' fading once a frame; we step them
	// every 33 ms, about the frame rate it ran at, so they look the same at any frame rate.
	constexpr uint64_t StepMs = 33;
	constexpr int Meditate = 31;
	constexpr int Bard = 8;

	// The stances that matter here: ChangeStance's 100 standing, 110 sitting, and the ones the book
	// won't open in (105 kneeling, 115 lying, 102 feigning).
	constexpr uint8_t Standing = 100, Sitting = 110;

	bool Valid(int spell) { return spell >= 1 && spell <= 3999; } // IsValidSpellIndex

	// UpdateSpellGems: the gem's color from the spell's target type (0xRRGGBB): red for a target,
	// blue for the caster's area, purple for a group, yellow for ourselves, and for the area round a
	// target green when it's over in under 3 seconds, orange when it lasts; black for the rest.
	uint32_t GemColor(const SpellInfo &spell)
	{
		switch (spell.target_type) {
		case 0: case 1: case 5: case 7: case 9: case 10: case 11: case 12: case 13: case 14: case 15: case 16:
		case 17: case 18: case 19: case 20: case 27:
			return 0xde2121;
		case 2: case 4: case 24: case 25: case 26: case 29: case 31:
			return 0x5242de;
		case 3: case 40: case 41:
			return 0x9c31de;
		case 6:
			return 0xcede29;
		case 8: case 21: case 22: case 23: case 28: case 30:
			return spell.ae_duration < 3000 ? 0x42de63 : 0xde7b21;
		default:
			return 0x000000;
		}
	}

	std::string SlotId(const char *prefix, int n) { return prefix + std::to_string(n); }
}

void SpellBook::Attach(WindowManager &windows, GameWindowState &state, ZoneSession &zone, const GameValues &values,
	SpellCast &cast, const SpellFile *spells, const EqStr *eqstr, Host host)
{
	m_windows = &windows;
	m_state = &state;
	m_zone = &zone;
	m_values = &values;
	m_cast = &cast;
	m_spells = spells;
	m_eqstr = eqstr;
	m_host = std::move(host);
}

void SpellBook::Reset()
{
	Host host = std::move(m_host);
	WindowManager *windows = m_windows;
	GameWindowState *state = m_state;
	ZoneSession *zone = m_zone;
	const GameValues *values = m_values;
	SpellCast *cast = m_cast;
	const SpellFile *spells = m_spells;
	const EqStr *eqstr = m_eqstr;
	*this = SpellBook{};
	m_windows = windows;
	m_state = state;
	m_zone = zone;
	m_values = values;
	m_cast = cast;
	m_spells = spells;
	m_eqstr = eqstr;
	m_host = std::move(host);
}

const SpellInfo *SpellBook::Spell(int id) const
{
	return m_spells && Valid(id) ? m_spells->Find(id) : nullptr;
}

std::string SpellBook::Text(uint32_t id, const std::string &fallback, const std::vector<std::string> &args) const
{
	return FormatEqString(m_eqstr, id, fallback, args);
}

void SpellBook::Say(uint32_t id, const std::string &fallback, int type, const std::vector<std::string> &args)
{
	if (m_host.chat)
		m_host.chat(Text(id, fallback, args), type);
}

uint8_t SpellBook::Stance() const { return m_zone ? m_zone->Self().stance : Standing; }
int SpellBook::Level() const { return m_zone ? m_zone->Profile().level : 0; }
int SpellBook::ClassId() const { return m_zone ? m_zone->Profile().class_id : 0; }

int SpellBook::ClassLevel(int spell) const
{
	const SpellInfo *info = Spell(spell);
	return info ? info->ClassLevel(ClassId()) : 255;
}

bool SpellBook::HasSpell(int spell) const
{
	const ZoneProfile &me = m_zone->Profile();
	return std::find(me.spell_book, me.spell_book + ZoneProfile::BookSlots, spell) != me.spell_book + ZoneProfile::BookSlots;
}

bool SpellBook::Memorized(int spell) const
{
	const ZoneProfile &me = m_zone->Profile();
	return std::find(me.mem_spells, me.mem_spells + GemCount, spell) != me.mem_spells + GemCount;
}

void SpellBook::Activate(uint64_t now_ms)
{
	if (!m_zone || !m_windows)
		return;
	int c = ClassId();
	if (c == 1 || c == 7 || c == 9) // IsSpellcaster
		return;
	uint8_t stance = Stance();
	if (m_host.moving_forward && m_host.moving_forward())
		Say(12085, "You must be completely stopped before doing this action.", ChatType::Default);
	else if (stance != 105 && stance != 115 && stance != 102) {
		m_was_sitting = stance == Sitting;
		if (!m_was_sitting && m_host.change_stance)
			m_host.change_stance(Sitting);
		// UpdateSpellBookDisplay's wrapper: nothing going on.
		m_scribe_ticks = 0;
		m_selected = m_mem_gem = m_mem_spell = m_scribe_slot = -1;
		m_checked_ms = now_ms;
		m_windows->Show("SpellBookWnd", true);
		m_windows->Raise("SpellBookWnd");
		m_activated = true;
		return;
	}
	// SpellBookDeactivating: the gems' book button pops back up (Frame does it).
}

void SpellBook::Deactivate()
{
	if (!m_activated)
		return;
	m_activated = false;
	if (m_windows)
		m_windows->Show("SpellBookWnd", false);
	if (Stance() == Sitting && !m_was_sitting && m_host.change_stance)
		m_host.change_stance(Standing);
	StopAction();
}

void SpellBook::StoodUp()
{
	if (m_activated)
		Deactivate();
}

void SpellBook::StopAction()
{
	m_selected = -1;
	if (m_state && m_state->cursor_spell >= 0)
		m_state->ClearCursor();
	if (m_mem_gem >= 0) {
		Say(12045, "Aborting memorization of spell.", ChatType::Spells);
		m_mem_gem = -1;
	}
	else if (m_scribe_slot >= 0) {
		Say(12044, "Aborting scribing of spell.", ChatType::Spells);
		m_scribe_slot = -1;
	}
}

void SpellBook::TurnToPage(int page)
{
	if (m_activated)
		m_page = std::clamp(page, 0, LastPage);
}

bool SpellBook::Click(const std::string &window, const std::string &control, bool alt, uint64_t now_ms)
{
	if (!m_zone || !m_windows)
		return false;
	if (window == "ConfirmationDialogBox") {
		if (control == "Yes_Button" && m_confirm_slot >= 0)
			m_zone->DeleteSpell(m_confirm_slot); // ProcessYes's 0x22: RequestSpellDeletion
		if (control == "Yes_Button" || control == "No_Button" || control == "OK_Button")
			CloseConfirmation();
		return true;
	}
	if (window == "SpellBookWnd") {
		if (control == "DoneButton") {
			Deactivate();
			return true;
		}
		if (alt) { // GetKeyboardFlags' 4: Done, or a slot's spell shown (DisplaySpellInfo)
			ShowSlotInfo(control);
			return true;
		}
		if (control == "SBW_PageUp_Button")
			TurnToPage(m_page + 2);
		else if (control == "SBW_PageDown_Button")
			TurnToPage(m_page - 2);
		for (int n = 0; n < SlotButtons; ++n)
			if (control == SlotId("SBW_Spell", n)) {
				ClickSlot(m_page * SlotsPerPage + n);
				break;
			}
		return true;
	}
	if (window == "CastSpellWnd") {
		if (alt) {
			ShowGemInfo(control);
			return true;
		}
		if (control == "CSPW_SpellBook") {
			if (m_activated)
				Deactivate();
			else
				Activate(now_ms);
			return true;
		}
		for (int gem = 0; gem < GemCount; ++gem)
			if (control == SlotId("CSPW_Spell", gem)) {
				GemLeftClick(gem, now_ms);
				break;
			}
		return true;
	}
	return false;
}

void SpellBook::GemLeftClick(int gem, uint64_t now_ms)
{
	// A book spell on the cursor memorizes; otherwise the gem's spell is cast.
	if (!m_zone || gem < 0 || gem >= GemCount)
		return;
	if (m_state->cursor_spell >= 0)
		BeginMemorize(m_state->cursor_spell, gem);
	else
		m_cast->GemClicked(gem, now_ms);
}

bool SpellBook::RightClick(const std::string &window, const std::string &control)
{
	if (!m_zone)
		return false;
	if (window == "SpellBookWnd") {
		for (int n = 0; n < SlotButtons; ++n)
			if (control == SlotId("SBW_Spell", n))
				RightClickSlot(m_page * SlotsPerPage + n);
		return true;
	}
	if (window == "CastSpellWnd") {
		// CCastSpellWnd_HandleSpellRightClick_MemorizeSpellFromGem: forget it, if the bar isn't busy.
		for (int gem = 0; gem < GemCount; ++gem)
			if (control == SlotId("CSPW_Spell", gem)) {
				int spell = m_zone->Profile().mem_spells[gem];
				if (Valid(spell) && !m_cast->Waiting() && m_zone->MemorizeSpell(gem, spell, 2))
					m_cast->Wait();
			}
		return true;
	}
	return false;
}

bool SpellBook::ShowSlotInfo(const std::string &control)
{
	// CSpellBookWnd::DisplaySpellInfo and DisplaySpellDetails: a slot with a spell toggles the item
	// display.
	for (int n = 0; n < SlotButtons; ++n)
		if (control == SlotId("SBW_Spell", n)) {
			int slot = m_page * SlotsPerPage + n;
			if (slot < ZoneProfile::BookSlots && Valid(m_zone->Profile().spell_book[slot]))
				ToggleSpellDisplay(*m_windows, *m_values, *m_state, m_zone->Profile().spell_book[slot], true);
			return true;
		}
	return false;
}

bool SpellBook::ShowGemInfo(const std::string &control)
{
	// CCastSpellWnd::DisplaySpellInfoForGemSlot: a gem with a spell toggles the item display.
	for (int gem = 0; gem < GemCount; ++gem)
		if (control == SlotId("CSPW_Spell", gem)) {
			int spell = m_zone->Profile().mem_spells[gem];
			if (Valid(spell))
				ToggleSpellDisplay(*m_windows, *m_values, *m_state, spell, true);
			return true;
		}
	return false;
}

bool SpellBook::RightHold(const std::string &window, const std::string &control)
{
	// WndNotification's 0x17, the right button held on a slot or a gem: as alt+click.
	if (!m_zone || !m_windows || !m_values || !m_state)
		return false;
	if (window == "SpellBookWnd")
		return ShowSlotInfo(control);
	if (window == "CastSpellWnd")
		return ShowGemInfo(control);
	return false;
}

bool SpellBook::RightRelease(const std::string &window, const std::string &control)
{
	// 0x19, let go after holding: the book toggles again (DisplaySpellInfo), the gems close the
	// item display if it's showing.
	if (!m_zone || !m_windows || !m_values || !m_state)
		return false;
	if (window == "SpellBookWnd")
		return ShowSlotInfo(control);
	if (window == "CastSpellWnd") {
		for (int gem = 0; gem < GemCount; ++gem)
			if (control == SlotId("CSPW_Spell", gem)) {
				if (m_windows->Shown("ItemDisplayWindow"))
					m_windows->Show("ItemDisplayWindow", false);
				return true;
			}
	}
	return false;
}

bool SpellBook::Key(InputEvent::Key key, uint64_t now_ms)
{
	if (!m_activated)
		return false;
	switch (key) {
	case InputEvent::Key::Left:
		TurnToPage(m_page - 2);
		return true;
	case InputEvent::Key::Right:
		TurnToPage(m_page + 2);
		return true;
	case InputEvent::Key::Delete:
		ConfirmDelete(m_selected, now_ms);
		return false;
	default:
		return false;
	}
}

void SpellBook::ClickSlot(int slot)
{
	// Only sitting (or on a mount, which we don't have).
	if (Stance() != Sitting || slot < 0 || slot >= ZoneProfile::BookSlots)
		return;
	if (m_zone->Items().count(Inventory::Cursor)) {
		if (!Valid(m_zone->Profile().spell_book[slot]))
			StartScribe(slot, ZoneSession::Now());
	}
	else
		StartMemorizationDrag(slot);
}

void SpellBook::StartMemorizationDrag(int slot)
{
	// IsOkToActivate: not over an item, nor with the quantity window up.
	if (m_cast->Waiting() || m_state->QuantityUp() || m_mem_gem >= 0 || m_scribe_slot >= 0)
		return;
	int spell = m_zone->Profile().spell_book[slot];
	if (!Valid(spell))
		return;
	if (Memorized(spell)) {
		Say(12052, "You cannot memorize a spell you already have memorized.", ChatType::Default);
		return;
	}
	const SpellInfo *info = Spell(spell);
	if (!info)
		return;
	if (Level() < info->ClassLevel(ClassId())) {
		Say(12054, "You are not high enough level to memorize that spell.", ChatType::Default);
		return;
	}
	// CCursorAttachment::Activate: the slot's icon, type 1, the book slot.
	m_state->ClearCursor();
	m_state->cursor_spell = slot;
	m_state->cursor_spell_icon = std::max(info->icon, 0);
}

void SpellBook::StartScribe(int slot, uint64_t now_ms)
{
	if (m_cast->Waiting() || m_mem_gem >= 0 || m_scribe_slot >= 0)
		return;
	auto held = m_zone->Items().find(Inventory::Cursor);
	if (held == m_zone->Items().end() || held->second.item_class != 0)
		return;
	const ZoneItem &scroll = held->second;
	if (scroll.item_type != 20 || !Inventory::CanUse(m_zone->Profile(), scroll)) {
		Say(181, "Your race, class, or deity cannot use this item.", ChatType::Default);
		return;
	}
	if (Valid(m_zone->Profile().spell_book[slot])) {
		Say(12051, "You already have a spell in that slot.", ChatType::Default);
		return;
	}
	int spell = scroll.effect1;
	const SpellInfo *info = Spell(spell);
	std::string name = info ? info->name : "";
	if (HasSpell(spell)) {
		Say(12050, "You already have scribed the %1 in your book.", ChatType::Spells, {scroll.name});
		return;
	}
	int needs = ClassLevel(spell);
	if (needs == 255) {
		Say(12047, "Your class can never use the %1.", ChatType::Spells, {scroll.name});
		return;
	}
	if (needs > Level()) {
		Say(12048, "You will have to achieve level %1 before you can scribe the %2.", ChatType::Spells,
			{std::to_string(needs), name});
		return;
	}
	m_scribe_slot = slot;
	m_scribe_spell = spell;
	m_scribe_ticks = Ticks;
	Say(12049, "Beginning to scribe %1...", ChatType::Spells, {name});
	(void)now_ms;
}

bool SpellBook::BeginMemorize(int slot, int gem)
{
	if (m_cast->Waiting() || m_mem_gem >= 0 || m_scribe_slot >= 0 || slot < 0 || slot >= ZoneProfile::BookSlots)
		return false;
	const ZoneProfile &me = m_zone->Profile();
	if (Valid(me.mem_spells[gem])) // a gem with a spell takes none: forget it first
		return false;
	int spell = me.spell_book[slot];
	if (Memorized(spell)) {
		Say(12052, "You cannot memorize a spell you already have memorized.", ChatType::Default);
		return false;
	}
	const SpellInfo *info = Spell(spell);
	Say(12053, "Beginning to memorize %1...", ChatType::Spells, {info ? info->name : ""});
	m_mem_gem = gem;
	m_mem_spell = spell;
	m_mem_ticks = Ticks;
	return true;
}

void SpellBook::RightClickSlot(int slot)
{
	if (Stance() != Sitting)
		return;
	if (slot == m_selected) {
		m_selected = -1;
		return;
	}
	if (m_selected >= 0) {
		m_zone->SwapSpells(m_selected, slot); // they trade places when the zone answers
		return;
	}
	m_selected = slot;
	Say(12046, "Right click on another Scribe Slot in your Spell Book to swap this Spell position with the new one.",
		ChatType::Yellow);
	const SpellInfo *info = Spell(m_zone->Profile().spell_book[slot]);
	if (info && info->deletable)
		Say(4027, "The currently selected spell can be permanently removed from your spellbook by pressing the delete key "
			"on your keyboard.", ChatType::Yellow);
}

void SpellBook::ConfirmDelete(int slot, uint64_t now_ms)
{
	if (slot < 0 || slot >= ZoneProfile::BookSlots)
		return;
	const SpellInfo *info = Spell(m_zone->Profile().spell_book[slot]);
	if (!info || !info->deletable)
		return;
	SidlWindow *dialog = m_windows->Find("ConfirmationDialogBox");
	if (!dialog)
		return;
	// CConfirmationDialog::Activate: Yes and No (OK only for category 0x1d), the text, centered.
	if (SidlWindow::Control *yes = dialog->Find("Yes_Button"))
		yes->visible = true;
	if (SidlWindow::Control *no = dialog->Find("No_Button"))
		no->visible = true;
	if (SidlWindow::Control *ok = dialog->Find("OK_Button"))
		ok->visible = false;
	if (SidlWindow::Control *text = dialog->Find("TextOutput")) {
		text->text = Text(10019, "Are you absolutely sure you want to delete %1?", {info->name});
		text->scroll_pos = 0;
	}
	const Rect &r = dialog->Bounds();
	m_windows->MoveTo("ConfirmationDialogBox", (m_windows->ScreenWidth() - r.w) / 2, (m_windows->ScreenHeight() - r.h) / 2);
	m_windows->Show("ConfirmationDialogBox", true);
	m_windows->Raise("ConfirmationDialogBox");
	m_confirm_slot = slot;
	m_confirm_until_ms = now_ms + ConfirmMs;
}

void SpellBook::CloseConfirmation()
{
	m_confirm_slot = -1;
	if (m_windows)
		m_windows->Show("ConfirmationDialogBox", false);
}

int SpellBook::TickMs(int spell) const
{
	return std::max((ClassLevel(spell) - Level() + 5) * 100, 100);
}

void SpellBook::MemorizeTicks(uint64_t now_ms)
{
	if (m_mem_gem < 0)
		return;
	if (now_ms - m_mem_tick_ms <= static_cast<uint64_t>(TickMs(m_mem_spell)))
		return;
	m_mem_tick_ms = now_ms;
	int step = 1;
	if (ClassId() == Bard) {
		int base;
		step = (m_values->Stat(10, base) + m_values->Stat(7, base)) / 30 + 1; // INT and DEX
	}
	if (m_zone->Profile().skills[Meditate] < 254) // HasSkill (a bard's AA 0x7b doubling it: not yet)
		step += 2 + m_values->Skill(Meditate) / 10;
	m_mem_ticks = std::max(m_mem_ticks - step, 0);
	if (m_mem_ticks > 0)
		return;
	m_cast->Wait();
	m_zone->MemorizeSpell(m_mem_gem, m_mem_spell, 1);
	m_mem_tick_ms = 0;
	if (m_state->cursor_spell >= 0)
		m_state->ClearCursor();
	m_mem_gem = m_mem_spell = -1;
}

void SpellBook::ScribeTicks(uint64_t now_ms)
{
	if (m_scribe_slot < 0)
		return;
	if (now_ms - m_scribe_tick_ms > static_cast<uint64_t>(TickMs(m_scribe_spell))) {
		m_scribe_tick_ms = now_ms;
		--m_scribe_ticks;
	}
	if (m_scribe_ticks > 0)
		return;
	m_cast->Wait();
	m_zone->MemorizeSpell(m_scribe_slot, m_scribe_spell, 0);
	m_scribe_slot = m_scribe_spell = -1;
	m_scribe_ticks = 0;
	m_scribe_tick_ms = 0;
}

int SpellBook::MemorizeGauge() const
{
	return m_mem_gem >= 0 ? m_mem_ticks * 1000 / Ticks : 0;
}

int SpellBook::ScribeGauge() const
{
	return m_scribe_slot >= 0 ? m_scribe_ticks * 1000 / Ticks : 0;
}

void SpellBook::ZoneEvent(int type, int id, int value)
{
	using Type = ZoneSession::Event::Type;
	const SpellInfo *info = Spell(value);
	std::string name = info ? info->name : "";
	switch (static_cast<Type>(type)) {
	case Type::SpellScribed:
		m_cast->Answered();
		Say(12006, "You have finished scribing %1.", ChatType::Spells, {name});
		if (m_state && m_state->cursor_spell >= 0) // FinishScribing's RemoveAttachment
			m_state->ClearCursor();
		break;
	case Type::SpellMemorized: // SpellCast starts its recast
		m_cast->Answered();
		Say(12007, "You have finished memorizing %1.", ChatType::Spells, {name});
		if (id < GemCount)
			m_gem_shown[id] = false;
		break;
	case Type::SpellForgotten:
		m_cast->Answered();
		if (!name.empty())
			Say(12496, "You forget %1.", ChatType::Spells, {name});
		if (id < GemCount)
			m_gem_shown[id] = false;
		break;
	case Type::SpellBarUnlocked:
		m_cast->Answered();
		break;
	case Type::SpellsSwapped:
		Say(1388, "Swapping Spell Book Scribe slots.", ChatType::Yellow);
		m_selected = -1;
		break;
	case Type::SpellDeleted:
		if (value) {
			Say(4028, "The spell has been removed from your spellbook.", ChatType::Yellow);
			m_selected = -1;
		}
		else
			Say(4029, "That spell could not be removed from your spellbook.", ChatType::Yellow);
		break;
	default:
		break;
	}
}

void SpellBook::Frame(uint64_t now_ms)
{
	if (!m_zone || !m_windows)
		return;
	// The close box hides it as Done would close it.
	if (m_activated && !m_windows->Shown("SpellBookWnd"))
		Deactivate();
	SidlWindow *book = m_windows->Find("SpellBookWnd");
	if (m_activated && now_ms - m_checked_ms > CheckMs) {
		bool scroll_gone = !m_zone->Items().count(Inventory::Cursor) && m_scribe_slot >= 0;
		if (!book || book->Minimized() || scroll_gone)
			StopAction();
		m_checked_ms = now_ms;
	}
	// The gauges draw what's left; eqgame counts the ticks as it draws them.
	if (m_activated && book && !book->Minimized()) {
		MemorizeTicks(now_ms);
		ScribeTicks(now_ms);
	}
	if (m_confirm_slot >= 0 && (now_ms >= m_confirm_until_ms || !m_windows->Shown("ConfirmationDialogBox")))
		CloseConfirmation(); // ProcessNo, as the time runs out
	if (book && m_activated)
		UpdateBook(now_ms);
	UpdateGems(now_ms);
}

void SpellBook::UpdateBook(uint64_t now_ms)
{
	SidlWindow &book = *m_windows->Find("SpellBookWnd");
	if (SidlWindow::Control *left = book.Find("SBW_LeftPageNum"))
		left->text = std::to_string(m_page + 1);
	if (SidlWindow::Control *right = book.Find("SBW_RightPageNum"))
		right->text = std::to_string(m_page + 2);
	if (m_selected >= 0 && now_ms - m_flash_ms >= StepMs) {
		m_flash_ms = now_ms;
		m_flash = m_flash >= 1 ? -1 : m_flash + 1;
	}
	const ZoneProfile &me = m_zone->Profile();
	for (int n = 0; n < SlotButtons; ++n) {
		SidlWindow::Control *button = book.Find(SlotId("SBW_Spell", n));
		SidlWindow::Control *label = book.Find(SlotId("SBW_SpellName", n));
		if (!button)
			continue;
		int slot = m_page * SlotsPerPage + n;
		const SpellInfo *info = slot < ZoneProfile::BookSlots ? Spell(me.spell_book[slot]) : nullptr;
		button->decal = "A_SpellIcons";
		if (info) {
			// SetBuffIcon: the icon on red (detrimental) or blue; the selected one flashing, none of them too.
			int look = slot == m_selected ? m_flash : info->good_effect;
			button->cell = std::max(info->icon, 0);
			button->no_background = look == -1;
			button->background = look == 0 ? "RedIconBackground" : "BlueIconBackground";
			if (label)
				label->text = info->name;
		}
		else {
			button->cell = -1;
			button->no_background = false;
			button->background = "A_SpellBookSlot";
			if (label)
				label->text.clear();
		}
	}
}

void SpellBook::UpdateGems(uint64_t now_ms)
{
	SidlWindow *gems = m_windows->Find("CastSpellWnd");
	if (!gems)
		return;
	if (SidlWindow::Control *button = gems->Find("CSPW_SpellBook"))
		button->checked = m_activated; // SpellBookDeactivating unchecks it
	bool step = now_ms - m_fade_ms >= StepMs;
	if (step)
		m_fade_ms = now_ms;
	const ZoneProfile &me = m_zone->Profile();
	for (int gem = 0; gem < GemCount; ++gem) {
		SidlWindow::Control *control = gems->Find(SlotId("CSPW_Spell", gem));
		if (!control)
			continue;
		const SpellInfo *info = Spell(me.mem_spells[gem]);
		// UpdateSpellGems: the icon and the color, when the spell's new. RefreshSpellGemButtons
		// (setGaugeState): dimming (1) while it can't be cast (nor an empty gem), and once it can,
		// brightening back (2) from the darkest, for a spell just dimmed or just shown.
		bool fresh = !m_gem_shown[gem];
		if (fresh) {
			m_gem_shown[gem] = true;
			control->cell = info ? std::max(info->icon, 0) : -1;
			control->gem_color = info ? GemColor(*info) : 0;
			// UpdateSpellGemTooltips: "%d) %s", the gem from 1 and the spell; none for an empty gem.
			control->tooltip = info ? std::to_string(gem + 1) + ") " + info->name : "";
		}
		bool ready = info && m_cast->GemReady(gem, now_ms);
		if (!ready)
			m_gem_state[gem] = 1;
		else if ((fresh || m_gem_state[gem] == 1) && m_gem_state[gem] != 2) {
			m_gem_state[gem] = 2;
			m_gem_fade[gem] = 10;
		}
		if (step) {
			if (m_gem_state[gem] == 1)
				m_gem_fade[gem] = std::min(m_gem_fade[gem] + 1, 10);
			else if (m_gem_state[gem] == 2 && --m_gem_fade[gem] <= 0) {
				m_gem_fade[gem] = 0;
				m_gem_state[gem] = 0;
			}
		}
		control->gem_fade = m_gem_fade[gem];
	}
}
