#include "bank_window.h"
#include "chat_text.h"
#include "eqstr.h"
#include "game_windows.h"
#include "inventory.h"
#include "window_manager.h"
#include "zone_session.h"

#include <map>
#include <string>

namespace
{
	constexpr float BankRange = 20; // RightClickedOnPlayer, OnProcessFrame
	constexpr uint64_t CheckMs = 500;
	constexpr int BankSlots = 8;

	std::string Said(const EqStr *eqstr, uint32_t id, const std::string &name, const std::string &fallback)
	{
		return eqstr && eqstr->Has(id) ? eqstr->Format(id, {name}) : name + fallback;
	}
}

void BankWindow::Attach(WindowManager &windows, ZoneSession &zone, GameWindowState &state, const GameValues &values,
	const EqStr *eqstr, Host host)
{
	m_windows = &windows;
	m_zone = &zone;
	m_state = &state;
	m_values = &values;
	m_eqstr = eqstr;
	m_host = std::move(host);
	Reset();
}

void BankWindow::Reset()
{
	if (Active() && m_host.music)
		m_host.music(false);
	m_banker = 0;
	m_opened_inventory = false;
	if (m_state)
		m_state->bank_open = false;
	if (m_windows)
		m_windows->Show("BankWnd", false);
}

std::string BankWindow::BankerName() const
{
	if (!m_zone)
		return "";
	auto it = m_zone->Spawns().find(m_banker);
	return it != m_zone->Spawns().end() ? it->second.DisplayName() : "";
}

void BankWindow::Activate(uint16_t banker)
{
	if (!m_windows || !m_zone || Active() || !m_windows->Find("BankWnd"))
		return;
	if (m_host.make_visible)
		m_host.make_visible();
	m_banker = banker;
	m_state->bank_open = true;
	std::string name = BankerName();
	if (m_host.chat)
		m_host.chat(Said(m_eqstr, 1465, name, " tells you, 'Welcome to my bank!'"), ChatType::Default);
	if (m_host.music)
		m_host.music(true);
	if (SidlWindow *wnd = m_windows->Find("BankWnd")) {
		if (SidlWindow::Control *label = wnd->Find("BW_BankerName"))
			label->text = name;
	}
	m_windows->Show("BankWnd", true);
	m_windows->Raise("BankWnd");
	m_opened_inventory = !m_windows->Shown("InventoryWindow");
	if (m_opened_inventory)
		m_windows->Show("InventoryWindow", true);
	m_next_check_ms = 0;
}

void BankWindow::Deactivate(bool say)
{
	if (!Active())
		return;
	m_windows->Show("BankWnd", false);
	if (m_host.music)
		m_host.music(false);
	if (say && m_host.chat)
		m_host.chat(Said(m_eqstr, 12143, BankerName(), " tells you, 'Come back soon!'"), ChatType::Default);
	m_banker = 0;
	m_state->bank_open = false;
	// The quantity window asking about the bank's coins goes with it.
	if (m_state->quantity_coin >= 0 && m_state->quantity_place == Inventory::Bank)
		m_windows->Show("QuantityWnd", false);
	if (m_opened_inventory)
		m_windows->Show("InventoryWindow", false);
	m_opened_inventory = false;
	if (m_host.close_bags)
		m_host.close_bags();
}

void BankWindow::Frame(uint64_t now_ms)
{
	if (!Active())
		return;
	// The close box hides it as Done would leave the bank.
	if (!m_windows->Shown("BankWnd")) {
		Deactivate(true);
		return;
	}
	if (now_ms >= m_next_check_ms) {
		m_next_check_ms = now_ms + CheckMs;
		float distance = m_zone->SpawnDistance(m_banker);
		if (distance > BankRange) {
			Deactivate(true);
			return;
		}
	}
	SidlWindow *wnd = m_windows->Find("BankWnd");
	if (!wnd)
		return;
	const ZoneProfile &me = m_zone->Profile();
	for (int coin = 0; coin < 4; ++coin)
		if (SidlWindow::Control *button = wnd->Find("BW_Money" + std::to_string(coin)))
			button->text = std::to_string(Inventory::Coins(me, Inventory::Bank, coin));
	// CInvSlot::SetItem: each slot shows what's in it.
	const std::map<int16_t, ZoneItem> &items = m_zone->Items();
	for (int n = 0; n < BankSlots; ++n)
		if (SidlWindow::Control *c = wnd->Find("BW_BankSlot" + std::to_string(n))) {
			auto it = items.find(static_cast<int16_t>(c->ItemSlot()));
			FillInvSlot(*c, it != items.end() ? &it->second : nullptr, *m_values);
		}
}

bool BankWindow::Click(const std::string &window, const std::string &control)
{
	if (window != "BankWnd" || !Active())
		return false;
	if (control == "DoneButton") {
		Deactivate(true);
		return true;
	}
	return false;
}

void BankWindow::DrawTooltips(Canvas &canvas, TextRenderer &text, int mouse_x, int mouse_y)
{
	if (!Active() || m_windows->ScreenAt(mouse_x, mouse_y) != "BankWnd")
		return;
	const SidlWindow *wnd = m_windows->Find("BankWnd");
	if (!wnd || wnd->Minimized())
		return;
	int first_x = -1;
	for (int n = 0; n < BankSlots; ++n) {
		const SidlWindow::Control *c = wnd->Find("BW_BankSlot" + std::to_string(n));
		if (!c || !wnd->Shown(*c) || c->tooltip.empty() || c->cell < 0)
			continue;
		if (first_x < 0)
			first_x = c->rect.x;
		int y = c->rect.y + (c->rect.x == first_x ? 2 : 22);
		m_windows->DrawTooltip(canvas, text, c->tooltip, c->def->font, c->rect.Right() - 5, y, false);
	}
}
