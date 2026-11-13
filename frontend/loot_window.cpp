#include "loot_window.h"
#include "chat_text.h"
#include "eqstr.h"
#include "game_values.h"
#include "game_windows.h"
#include "inventory.h"
#include "window_manager.h"

#include <cctype>
#include <cmath>
#include <string>

namespace
{
	constexpr int LootSlots = 30;
	constexpr uint64_t CheckMs = 500;
	constexpr uint64_t ConfirmMs = 20000; // CConfirmationDialog::Activate's 20000
	constexpr uint8_t Kneeling = 105, Standing = 100;
	constexpr int CoinsColor = 0x11d;

	// CEverQuest::stripName: a corpse's name up to "'s" (its owner's), to tell our own corpse.
	bool OwnCorpse(const std::string &corpse, const std::string &me)
	{
		size_t end = corpse.find("'s");
		std::string owner = corpse.substr(0, end);
		if (owner.size() != me.size())
			return false;
		for (size_t i = 0; i < me.size(); ++i)
			if (std::tolower(static_cast<unsigned char>(owner[i])) != std::tolower(static_cast<unsigned char>(me[i])))
				return false;
		return true;
	}
}

void LootWindow::Attach(WindowManager &windows, ZoneSession &zone, GameWindowState &state, const GameValues &values,
	Host host)
{
	m_windows = &windows;
	m_zone = &zone;
	m_state = &state;
	m_values = &values;
	m_host = std::move(host);
	Reset();
}

void LootWindow::Reset()
{
	m_corpse = 0;
	m_open = m_ended = m_opened_inventory = m_kneeling = false;
	m_busy = 0;
	m_confirm_index = -1;
	if (m_windows)
		m_windows->Show("LootWnd", false);
}

std::string LootWindow::Text(uint32_t id, const std::vector<std::string> &args, const std::string &fallback) const
{
	const EqStr *eqstr = m_values ? m_values->eqstr : nullptr;
	return eqstr && eqstr->Has(id) ? eqstr->Format(id, args) : fallback;
}

bool LootWindow::LootCorpse(uint16_t corpse)
{
	auto it = m_zone->Spawns().find(corpse);
	if (it == m_zone->Spawns().end() || it->second.npc < 2 || m_corpse != 0 || m_busy != 0 ||
	    m_zone->CampRemainingMs() != 0 || (m_host.dead && m_host.dead()) || m_zone->Profile().deity == 20000)
		return false;
	// PlayerDistance(corpse, us, 1.2): up and down counts 1 / 1.2.
	const ZoneSpawn &body = it->second;
	const ZoneProfile &me = m_zone->Profile();
	float dz = (body.z - me.z) / 1.2f;
	float distance = std::sqrt((body.x - me.x) * (body.x - me.x) + (body.y - me.y) * (body.y - me.y) + dz * dz);
	float height = m_host.height ? m_host.height(corpse) : 0;
	if (distance > height * 1.6f + 14) {
		if (m_host.chat)
			m_host.chat(Text(12390, {}, "You are too far away to loot that corpse."), ChatType::Red);
		return false;
	}
	if (m_host.make_visible)
		m_host.make_visible();
	m_corpse = corpse;
	m_open = m_ended = false;
	m_zone->LootRequest(corpse);
	return true;
}

void LootWindow::ZoneEvent(const ZoneSession::Event &event)
{
	using Type = ZoneSession::Event::Type;
	switch (event.type) {
	case Type::LootOpened: {
		if (m_corpse == 0 || m_open)
			break;
		const ZoneLoot &loot = event.loot;
		if (loot.response == 2 || loot.response == 0) {
			if (m_host.chat)
				m_host.chat(loot.response == 2 ? Text(12070, {}, "You may not loot this corpse at this time.")
				                               : Text(12073, {}, "Someone is already looting that corpse."),
					ChatType::Default);
			m_corpse = 0;
			break;
		}
		if (loot.response == 1 || loot.response == 3) {
			ZoneProfile &me = m_zone->EditProfile();
			me.platinum += loot.platinum;
			me.gold += loot.gold;
			me.silver += loot.silver;
			me.copper += loot.copper;
			if (loot.platinum + loot.gold + loot.silver + loot.copper > 0) {
				if (m_host.sound)
					m_host.sound(0x8a);
				std::string coins = Inventory::MoneyText(loot.platinum, loot.gold, loot.silver, loot.copper);
				// 12071 "as your split" with autosplit on (IsAutoSplitEnabled), else 12072.
				if (m_host.chat)
					m_host.chat(me.autosplit ? Text(12071, {coins}, "You receive " + coins + " as your split.")
					                         : Text(12072, {coins}, "You receive " + coins + " from the corpse."),
						CoinsColor);
			}
			if (m_host.change_stance) {
				m_host.change_stance(Kneeling);
				m_kneeling = true;
			}
		}
		std::string name;
		if (auto it = m_zone->Spawns().find(m_corpse); it != m_zone->Spawns().end())
			name = it->second.DisplayName();
		if (SidlWindow *wnd = m_windows->Find("LootWnd")) {
			if (SidlWindow::Control *label = wnd->Find("LW_CorpseName"))
				label->text = name;
			if (SidlWindow::Control *slots = wnd->Find("LootInvWnd")) {
				slots->scroll_pos = 0;
				wnd->Move(wnd->Bounds());
			}
		}
		m_open = true;
		m_busy = 0;
		m_windows->Show("LootWnd", true);
		m_windows->Raise("LootWnd");
		m_opened_inventory = !m_windows->Shown("InventoryWindow");
		if (m_opened_inventory)
			m_windows->Show("InventoryWindow", true);
		m_next_check_ms = 0;
		break;
	}
	case Type::LootTaken:
		m_busy = std::max(m_busy - 1, 0);
		if (event.loot.slot < 0 && m_host.chat)
			m_host.chat(Text(1370, {}, "You may not loot that item from this corpse."), ChatType::Default);
		break;
	case Type::LootEnded:
		if (m_corpse != 0)
			Release();
		break;
	case Type::SpawnRemoved:
		if (event.id == m_corpse && m_corpse != 0)
			Release();
		break;
	default:
		break;
	}
}

void LootWindow::Deactivate()
{
	if (m_corpse == 0 || m_ended)
		return;
	m_ended = true;
	m_zone->EndLoot(m_corpse);
}

void LootWindow::Died()
{
	if (m_corpse == 0)
		return;
	Deactivate();
	Release();
}

void LootWindow::Release()
{
	if (m_host.save)
		m_host.save(); // CLootWnd::fn_releaseloot's SavePC
	m_windows->Show("LootWnd", false);
	m_zone->ClearLoot();
	if (m_host.clear_target)
		m_host.clear_target();
	if (m_kneeling && m_zone->Self().stance == Kneeling && m_host.change_stance)
		m_host.change_stance(Standing);
	if (m_opened_inventory)
		m_windows->Show("InventoryWindow", false);
	bool confirming = m_confirm_index >= 0;
	m_corpse = 0;
	m_open = m_ended = m_opened_inventory = m_kneeling = false;
	m_busy = 0;
	m_confirm_index = -1;
	if (confirming)
		m_windows->Show("ConfirmationDialogBox", false);
}

void LootWindow::RequestSlot(int index, bool auto_loot, uint64_t now_ms)
{
	const ZoneProfile &me = m_zone->Profile();
	const std::map<int16_t, ZoneItem> &items = m_zone->Items();
	if (Inventory::CursorMoneyType(me) != 0 || items.count(Inventory::Cursor) || index < 0 || index >= LootSlots)
		return;
	auto it = items.find(static_cast<int16_t>(Inventory::FirstLoot + index));
	if (it == items.end())
		return;
	const ZoneItem &item = it->second;
	if (loot_no_drop < 0 || loot_no_drop > 2)
		loot_no_drop = 0;
	std::string corpse_name;
	if (auto spawn = m_zone->Spawns().find(m_corpse); spawn != m_zone->Spawns().end())
		corpse_name = spawn->second.name;
	bool fv = m_values && m_values->fv_rules;
	if (!OwnCorpse(corpse_name, me.name) && !Inventory::CanDrop(items, item, it->first, fv) && loot_no_drop != 2 &&
	    (loot_no_drop == 0 || !Inventory::CanUse(me, item))) {
		// The confirmation dialog first (category 0x16), unless it's up already.
		SidlWindow *dialog = m_windows->Find("ConfirmationDialogBox");
		if (!dialog || m_windows->Shown("ConfirmationDialogBox"))
			return;
		for (const char *id : {"Yes_Button", "No_Button"})
			if (SidlWindow::Control *button = dialog->Find(id))
				button->visible = true;
		if (SidlWindow::Control *ok = dialog->Find("OK_Button"))
			ok->visible = false;
		if (SidlWindow::Control *text = dialog->Find("TextOutput")) {
			text->text = Text(2072, {}, "Are you sure you wish to loot this NODROP item?");
			text->scroll_pos = 0;
		}
		const Rect &r = dialog->Bounds();
		m_windows->MoveTo("ConfirmationDialogBox", (m_windows->ScreenWidth() - r.w) / 2, (m_windows->ScreenHeight() - r.h) / 2);
		m_windows->Show("ConfirmationDialogBox", true);
		m_windows->Raise("ConfirmationDialogBox");
		m_confirm_index = index;
		m_confirm_auto = auto_loot;
		m_confirm_until_ms = now_ms + ConfirmMs;
		return;
	}
	if (m_zone->LootItem(m_corpse, index, auto_loot))
		++m_busy;
}

bool LootWindow::Click(const std::string &window, const std::string &control, uint64_t now_ms)
{
	if (window == "ConfirmationDialogBox" && m_confirm_index >= 0) {
		// ProcessYes's 0x16: the OP_LootItem it kept.
		if (control == "Yes_Button" && m_open && m_zone->LootItem(m_corpse, m_confirm_index, m_confirm_auto))
			++m_busy;
		if (control == "Yes_Button" || control == "No_Button" || control == "OK_Button") {
			m_confirm_index = -1;
			m_windows->Show("ConfirmationDialogBox", false);
		}
		return true;
	}
	if (window != "LootWnd" || !m_open)
		return false;
	if (control == "DoneButton") {
		Deactivate();
		return true;
	}
	int slot = InvSlotAt(*m_windows, window, control);
	if (slot < Inventory::FirstLoot || slot > Inventory::LastLoot)
		return false;
	// HandleLButtonUp: not while the quantity window's up or a slot's locked, and only onto an
	// empty cursor (MoveItem from the corpse is RequestLootSlot).
	if (m_state->QuantityUp() || m_state->busy || m_busy)
		return true;
	RequestSlot(slot - Inventory::FirstLoot, false, now_ms);
	return true;
}

bool LootWindow::RightClick(const std::string &window, const std::string &control, uint64_t now_ms)
{
	if (window != "LootWnd" || !m_open)
		return false;
	int slot = InvSlotAt(*m_windows, window, control);
	if (slot < Inventory::FirstLoot || slot > Inventory::LastLoot)
		return false;
	if (!m_state->busy && !m_busy)
		RequestSlot(slot - Inventory::FirstLoot, true, now_ms);
	return true;
}

void LootWindow::Frame(uint64_t now_ms)
{
	if (m_confirm_index >= 0 && (now_ms >= m_confirm_until_ms || !m_windows->Shown("ConfirmationDialogBox"))) {
		m_confirm_index = -1; // ProcessNo, as the time runs out
		m_windows->Show("ConfirmationDialogBox", false);
	}
	if (!m_open)
		return;
	SidlWindow *wnd = m_windows->Find("LootWnd");
	// The close box: as Done (the zone's answer releases it).
	if (!wnd || !m_windows->Shown("LootWnd")) {
		Deactivate();
		return;
	}
	if (now_ms >= m_next_check_ms) {
		m_next_check_ms = now_ms + CheckMs;
		auto it = m_zone->Spawns().find(m_corpse);
		if (it != m_zone->Spawns().end()) {
			const ZoneProfile &me = m_zone->Profile();
			float distance = std::hypot(it->second.x - me.x, it->second.y - me.y);
			float height = m_host.height ? m_host.height(m_corpse) : 0;
			if (distance > height * 1.5f + 20)
				Deactivate();
		}
	}
	const std::map<int16_t, ZoneItem> &items = m_zone->Items();
	for (int n = 0; n < LootSlots; ++n)
		if (SidlWindow::Control *c = wnd->Find("LW_LootSlot" + std::to_string(n))) {
			auto it = items.find(static_cast<int16_t>(c->ItemSlot()));
			FillInvSlot(*c, it != items.end() ? &it->second : nullptr, *m_values);
		}
}

void LootWindow::DrawTooltips(Canvas &canvas, TextRenderer &text, int mouse_x, int mouse_y)
{
	if (!m_open || m_windows->ScreenAt(mouse_x, mouse_y) != "LootWnd")
		return;
	const SidlWindow *wnd = m_windows->Find("LootWnd");
	const SidlWindow::Control *area = wnd ? wnd->Find("LootInvWnd") : nullptr;
	if (!wnd || wnd->Minimized())
		return;
	int first_x = -1;
	for (int n = 0; n < LootSlots; ++n) {
		const SidlWindow::Control *c = wnd->Find("LW_LootSlot" + std::to_string(n));
		if (!c || !wnd->Shown(*c) || c->tooltip.empty() || c->cell < 0)
			continue;
		if (first_x < 0)
			first_x = c->rect.x;
		int y = c->rect.y + (c->rect.x == first_x ? 2 : 22);
		if (area && (y > area->rect.Bottom() - 15 || y < area->rect.y))
			continue;
		m_windows->DrawTooltip(canvas, text, c->tooltip, c->def->font, c->rect.Right() - 5, y, false);
	}
}
