#include "ground_items.h"
#include "chat_text.h"
#include "eqstr.h"
#include "game_values.h"
#include "game_windows.h"
#include "inventory.h"
#include "window_manager.h"

#include <cmath>
#include <string>

namespace
{
	constexpr float PickUpRange = 20; // LMouseUp's TrueDistance
	constexpr uint64_t ConfirmMs = 20000;
	constexpr int DroppedColor = 15;
}

void GroundItems::Attach(WindowManager &windows, ZoneSession &zone, GameWindowState &state, const GameValues &values,
	Host host)
{
	m_windows = &windows;
	m_zone = &zone;
	m_state = &state;
	m_values = &values;
	m_host = std::move(host);
	Reset();
}

void GroundItems::Reset()
{
	m_busy = m_waiting = 0;
	m_confirming = false;
}

std::string GroundItems::Text(uint32_t id, const std::vector<std::string> &args, const std::string &fallback) const
{
	const EqStr *eqstr = m_values ? m_values->eqstr : nullptr;
	return eqstr && eqstr->Has(id) ? eqstr->Format(id, args) : fallback;
}

bool GroundItems::DropRequest(uint64_t now_ms)
{
	const std::map<int16_t, ZoneItem> &items = m_zone->Items();
	auto held = items.find(Inventory::Cursor);
	if (held == items.end())
		return false;
	const ZoneItem &item = held->second;
	if (item.item_class == 0 && (item.item_type == 12 || item.item_type == 33 || item.item_type == 39))
		return false;
	if (!Inventory::CanDrop(items, item, Inventory::Cursor, m_values && m_values->fv_rules)) {
		if (m_host.chat)
			m_host.chat(item.no_drop != 0 ? Text(12922, {}, "That container cannot be dropped, traded, or sold, as it contains "
			                                               "an item which cannot be dropped, traded or sold.")
			                              : Text(12921, {}, "That item cannot be dropped, traded, or sold."),
				ChatType::Yellow);
		return true;
	}
	if (fast_drop == 1) {
		Drop();
		return true;
	}
	if (fast_drop == -1) {
		if (m_host.chat)
			m_host.chat(Text(12320, {}, "You currently cannot drop items.  Goto General Options or use /fastdrop to enable "
				"dropping."), ChatType::Default);
		return true;
	}
	SidlWindow *dialog = m_windows->Find("ConfirmationDialogBox");
	if (!dialog || m_windows->Shown("ConfirmationDialogBox"))
		return true;
	for (const char *id : {"Yes_Button", "No_Button"})
		if (SidlWindow::Control *button = dialog->Find(id))
			button->visible = true;
	if (SidlWindow::Control *ok = dialog->Find("OK_Button"))
		ok->visible = false;
	if (SidlWindow::Control *text = dialog->Find("TextOutput")) {
		text->text = Text(10011, {item.name}, "ARE YOU SURE you wish to drop your " + item.name + " on the ground?");
		text->scroll_pos = 0;
	}
	const Rect &r = dialog->Bounds();
	m_windows->MoveTo("ConfirmationDialogBox", (m_windows->ScreenWidth() - r.w) / 2, (m_windows->ScreenHeight() - r.h) / 2);
	m_windows->Show("ConfirmationDialogBox", true);
	m_windows->Raise("ConfirmationDialogBox");
	m_confirming = true;
	m_confirm_until_ms = now_ms + ConfirmMs;
	return true;
}

void GroundItems::Drop()
{
	// DropHeldItemOnGround(1).
	auto held = m_zone->Items().find(Inventory::Cursor);
	if (held == m_zone->Items().end())
		return;
	std::string name = held->second.name;
	if (m_zone->DropCursorItem() && m_host.chat)
		m_host.chat(Text(10012, {name}, "You just dropped your " + name + "."), DroppedColor);
}

bool GroundItems::PickUp(uint32_t drop_id)
{
	if (m_host.world_container_open && m_host.world_container_open())
		return false;
	auto it = m_zone->GroundItems().find(drop_id);
	if (it == m_zone->GroundItems().end())
		return false;
	const ZoneProfile &me = m_zone->Profile();
	const ZoneGroundItem &item = it->second;
	float dx = item.x - me.x, dy = item.y - me.y, dz = item.z - me.z;
	if (std::sqrt(dx * dx + dy * dy + dz * dz) >= PickUpRange)
		return false;
	++m_busy;
	++m_waiting;
	m_zone->PickUp(drop_id);
	if (m_host.open_inventory)
		m_host.open_inventory();
	return true;
}

bool GroundItems::Click(const std::string &window, const std::string &control)
{
	if (window != "ConfirmationDialogBox" || !m_confirming)
		return false;
	// ProcessYes's 0x15: DropHeldItemOnGround(1).
	if (control == "Yes_Button")
		Drop();
	if (control == "Yes_Button" || control == "No_Button" || control == "OK_Button") {
		m_confirming = false;
		m_windows->Show("ConfirmationDialogBox", false);
	}
	return true;
}

void GroundItems::Frame(uint64_t now_ms)
{
	if (m_confirming && (now_ms >= m_confirm_until_ms || !m_windows->Shown("ConfirmationDialogBox"))) {
		m_confirming = false; // ProcessNo, as the time runs out
		m_windows->Show("ConfirmationDialogBox", false);
	}
}

void GroundItems::ZoneEvent(const ZoneSession::Event &event)
{
	using Type = ZoneSession::Event::Type;
	switch (event.type) {
	case Type::GroundItemRemoved:
		// The 0x402b case: an answer to us (or to nobody) lets the transactions go.
		if (event.id == 1) {
			m_busy = std::max(m_busy - 1, 0);
			m_waiting = std::max(m_waiting - 1, 0);
		}
		break;
	case Type::WorldContainerOpened: {
		// msg_chest_lock.
		m_busy = std::max(m_busy - 1, 0);
		const ZoneWorldContainer &world = event.world;
		if (world.player != m_zone->MySpawnId()) {
			m_waiting = 0;
			break;
		}
		if (world.open == 0) {
			if (m_host.chat)
				m_host.chat(Text(1406, {}, "Someone else is using that. Try again later."), ChatType::Default);
		}
		else if (m_host.world_container_open && m_host.world_container_open()) {
			if (m_host.chat)
				m_host.chat(Text(1443, {}, "ERROR. Tried to lock a chest when one is locked already."), ChatType::Default);
		}
		else if (m_host.open_world_container)
			m_host.open_world_container(world);
		m_waiting = 0;
		break;
	}
	default:
		break;
	}
}
