#pragma once

// What eqgame does with the ground (CEverQuest_LMouseUp @00531614 with the new UI), after a click
// on a spawn (LeftClickedOnPlayer) and before one on a door (ClickedSwitch):
//   - an item on the cursor goes on the ground where we stand, unless it's a common item of type
//     12, 33 or 39 (those click the doors): NO DROP says why not (CanDrop, eqstr 12921, 12922);
//     /fastdrop's setting (DAT_0079966c, 0 to start with: eqOptions2.opt) drops it at once (1),
//     refuses (-1, 12320), or asks first (0: the confirmation dialog, category 0x15, 20 s:
//     "ARE YOU SURE you wish to drop your %1 on the ground?", 10011). DropHeldItemOnGround
//     (@00530d7e) says "You just dropped your %1." (10012, color 15) and sends OP_GroundSpawn;
//   - with coins on the cursor, nothing;
//   - else, with no tradeskill container open, the ground item under the cursor (GetClickedActor,
//     EQItemList_is_item_actor) within 20 of us is asked for (OP_ClickObject), the slots locked
//     and the transactions held (DAT_007d0254, DAT_007d0258) until the zone's OP_ClickObject
//     answers (its "who" us, or 0), and the inventory comes up. A dropped item comes back on the
//     cursor (SummonItem); a tradeskill container answers OP_ClickObjectAction instead
//     (msg_chest_lock @004e38da): not ours, nothing; 0 "Someone else is using that. Try again
//     later." (1406); another already open, 1443; else it opens (CContainerMgr::OpenWorldContainer).
// No click goes through while a transaction waits (DAT_007d0254).

#include "zone_session.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class GameValues;
class WindowManager;
struct GameWindowState;

class GroundItems
{
public:
	struct Host
	{
		std::function<void(const std::string &, int)> chat; // a message, in a chat color type
		std::function<void()> open_inventory;               // CInventoryWnd::Activate
		std::function<bool()> world_container_open;         // CContainerMgr's pWorldItems
		std::function<void(const ZoneWorldContainer &)> open_world_container; // OpenWorldContainer
	};

	void Attach(WindowManager &windows, ZoneSession &zone, GameWindowState &state, const GameValues &values, Host host);
	void Reset();

	bool Busy() const { return m_busy > 0; }       // DAT_007d0254: a pickup or container asked for
	bool Waiting() const { return m_waiting > 0; } // DAT_007d0258
	// LMouseUp with an item on the cursor: true when the click's taken (dropped, asked about, or
	// said no to); false for the items that click the doors instead.
	bool DropRequest(uint64_t now_ms);
	// LMouseUp on a ground item with nothing on the cursor: true when it's asked for.
	bool PickUp(uint32_t drop_id);
	bool Click(const std::string &window, const std::string &control); // the confirmation dialog
	void Frame(uint64_t now_ms);
	void ZoneEvent(const ZoneSession::Event &event);

	int fast_drop = 0; // /fastdrop (DAT_0079966c): 1 drop at once, 0 ask, -1 never

private:
	std::string Text(uint32_t id, const std::vector<std::string> &args, const std::string &fallback) const;
	void Drop();

	WindowManager *m_windows = nullptr;
	ZoneSession *m_zone = nullptr;
	GameWindowState *m_state = nullptr;
	const GameValues *m_values = nullptr;
	Host m_host;
	int m_busy = 0, m_waiting = 0;
	bool m_confirming = false;
	uint64_t m_confirm_until_ms = 0;
};
