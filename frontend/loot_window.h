#pragma once

// eqgame's loot window, CLootWnd (@004261b9 on):
//   - CEverQuest::RightClickedOnPlayer: the right button on a corpse is CEverQuest_LootCorpse
//     (@00547808): with nothing else going on (no request waiting, DAT_007d0258, nor anything
//     locked, DAT_007d0254, no camp, no corpse already), and the corpse within its
//     ModelHeightOffset * 1.6 + 14 (up and down counting 1 / 1.2), OP_LootRequest; else "You are
//     too far away to loot that corpse." (eqstr 12390), and it's considered as anything else is;
//   - DoLoot_handleLootCorpse (@00426308), OP_MoneyOnCorpse's answer: 2 "You may not loot this
//     corpse at this time." (12070) and 0 "Someone is already looting that corpse." (12073); 1 or 3
//     the corpse's coins into the purse, "You receive %1 from the corpse." (12072, BuildMoneyText,
//     chat color 0x11d) and wave 0x8a, us kneeling to loot (ChangeStance 105); then its name on
//     LW_CorpseName, the window up, and the inventory too if it wasn't;
//   - its items (OP_LootItemPacket, AddEquipmentToLootArray) fill LW_LootSlot0..29 (5000..5029)
//     in the order they come;
//   - a click on one (CInvSlot::HandleLButtonUp, CInvSlotMgr_MoveItem from 5000..5029) or the right
//     button on it (HandleRButtonUp, into the inventory) is RequestLootSlot (@00426b02): nothing
//     on the cursor, and for another's corpse a NO DROP item asks first (eqstr 2072, the
//     confirmation dialog, 20 s) unless /lootnodrop says not to; then OP_LootItem, and the
//     slots locked until the zone's answer (SlotLooted @00426c53): the slot emptied, or -1 "You
//     may not loot that item from this corpse." (1370). What's looted comes as its own packet;
//   - CheckLootDistanceAndClose (@00426708): every 500 ms, the corpse further than its
//     ModelHeightOffset * 1.5 + 20 across the ground ends it, as Done, the close box and Escape
//     do (Deactivate @0042651f: OP_EndLootRequest, once);
//   - the zone's OP_EndLootRequest or OP_LootComplete, or the corpse gone, closes it
//     (DoorLoading_ and fn_releaseloot @00426576): the window down, its items gone, our target
//     cleared, standing up from the kneel, and the inventory down if it put it up;
//   - DrawLootTooltips (@00426625): with Alt down, every slot's tooltip in view.

#include "zone_session.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class Canvas;
class GameValues;
class TextRenderer;
class WindowManager;
struct GameWindowState;

class LootWindow
{
public:
	struct Host
	{
		std::function<void(const std::string &, int)> chat; // a message, in a chat color type
		std::function<void(int)> sound;                     // EqSoundManager::WavePlay
		std::function<void(uint8_t)> change_stance;         // EQPlayer::ChangeStance
		std::function<void()> clear_target;
		std::function<float(uint16_t)> height;              // a spawn's ModelHeightOffset
	};

	void Attach(WindowManager &windows, ZoneSession &zone, GameWindowState &state, const GameValues &values, Host host);
	void Reset(); // a new zone

	bool Active() const { return m_open; }                 // the window's up (Unk1)
	bool Waiting() const { return m_corpse != 0 && !m_open; } // asked, no answer yet
	bool Busy() const { return m_busy > 0; }               // a slot asked for and not answered
	uint16_t Corpse() const { return m_corpse; }           // pinstActiveCorpse
	// CEverQuest_LootCorpse: false when it doesn't ask (too far, something else going on).
	bool LootCorpse(uint16_t corpse);
	void ZoneEvent(const ZoneSession::Event &event);
	void Deactivate(); // ask the zone to end it
	void Frame(uint64_t now_ms);
	// A click on a loot slot (left: onto the cursor; right: into the inventory) and on Done; the
	// confirmation dialog while it asks about a NO DROP item. False when it isn't one of these.
	bool Click(const std::string &window, const std::string &control, uint64_t now_ms);
	bool RightClick(const std::string &window, const std::string &control, uint64_t now_ms);
	void DrawTooltips(Canvas &canvas, TextRenderer &text, int mouse_x, int mouse_y);

	int loot_no_drop = 0; // /lootnodrop (DAT_00799674): 0 always ask, 1 ask for what we can't use, 2 never

private:
	std::string Text(uint32_t id, const std::vector<std::string> &args, const std::string &fallback) const;
	void RequestSlot(int index, bool auto_loot, uint64_t now_ms);
	void Release(); // DoorLoading_ with fn_releaseloot

	WindowManager *m_windows = nullptr;
	ZoneSession *m_zone = nullptr;
	GameWindowState *m_state = nullptr;
	const GameValues *m_values = nullptr;
	Host m_host;
	uint16_t m_corpse = 0;
	bool m_open = false;
	bool m_ended = false; // OP_EndLootRequest sent (Unk1's second byte)
	bool m_opened_inventory = false;
	bool m_kneeling = false;
	uint64_t m_next_check_ms = 0;
	int m_busy = 0;
	// The NO DROP item the confirmation dialog asks about: its loot slot (-1 none), how, until when.
	int m_confirm_index = -1;
	bool m_confirm_auto = false;
	uint64_t m_confirm_until_ms = 0;
};
