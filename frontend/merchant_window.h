#pragma once

// eqgame's merchant window, CMerchantWnd (@00426d4c on):
//   - CEverQuest::RightClickedOnPlayer (@00532949): the right button on a merchant (class 32) no
//     more than 20 away, with none going, asks the zone (OP_ShopRequest) and waits (DAT_007d0258:
//     nothing else is dealt with meanwhile);
//   - msg_req_merchantitems (@004e5531): the zone's answer opens the window (Activate @00426f18:
//     the merchant's name on MW_MerchantName, the rate it sent kept as MerchantGreed, the
//     inventory too if it wasn't up), or with command 0 just stops waiting; its items come in
//     OP_ShopInventoryPacket and OP_MerchantItemPacket, each in its slot (AddEquipmentToMercArray
//     and the others: item 0 is "Got a bogus item, deleting it", eqstr 1366), and
//     OP_ShopDelItem takes one out (ClearMerchantSlot);
//   - CInvSlot::HandleLButtonUp: while it's up, a click on any slot (the merchant's or ours)
//     selects it (CInvSlotMgr_SelectSlot: a yellow outline) and SelectBuySellSlot (@00427614) shows
//     it on MW_SelectedItem and says its price (DisplayBuyOrSellPrice @00427735): a merchant's slot
//     shows Buy, ours Sell (UpdateMerchantWindowVisibility); a bag's contents are priced first;
//     Buy is disabled when we can't afford one, Sell for a NO RENT, NO DROP or worthless item;
//     only our general and worn slots and their bags can be sold from;
//   - OnProcessFrame (@004271bf): every 500 ms, the merchant more than 20 away across the ground
//     closes it; otherwise, with nothing selected, its first slot is;
//   - HandleBuy (@00427321): a lore item we have (eqstr 1573), or no room in the general slots
//     or their bags (an empty one, or the same stacking item: 1571), says so; otherwise the
//     quantity window for a stack (up to 20; one for anything else) and RequestBuyItem (@00427902):
//     more than we have on us (12060, 12061) says so, else OP_ShopPlayerBuy and the slots locked
//     (DAT_007d0254) until FinishBuyingItem (@00427ad6): the price out of the purse (HandleMoney)
//     and "You give%1 to %2." (1437), or "Could not purchase..." (1378) for slot -1 and "already
//     been sold" (1376) for an empty one. The item itself comes as its own packet;
//   - HandleSell (@0042751c): the quantity window for a stack; RequestSellItem (@00427c83): one
//     thing at a time (12063), not NO DROP (12062), a bag empty first (12064), then
//     OP_ShopPlayerSell and the slots locked until FinishSellingItem (@00427df8): what it's worth
//     (ValueSell) into the purse, "You receive%1 from %2 for the %3(s)." (1361), and the item (or
//     that many of a stack) gone from its slot, as the zone doesn't say; -1 is "not interested"
//     (1364);
//   - Deactivate (@00426faf), from Done, the close box, Escape and walking away: the window down,
//     its items forgotten, OP_ShopEnd, our target cleared, the inventory down if it put it up;
//   - PostDraw (@004270dc): with Alt down, every merchant slot's tooltip that's in view.

#include "zone_session.h"

#include <cstdint>
#include <functional>
#include <string>

class Canvas;
class GameValues;
class TextRenderer;
class WindowManager;
struct GameWindowState;

class MerchantWindow
{
public:
	struct Host
	{
		std::function<void(const std::string &, int)> chat; // a message, in a chat color type
		std::function<void(int)> sound;                     // EqSoundManager::WavePlay
		std::function<void(int16_t)> close_bag;              // CContainerMgr::CloseContainer
		std::function<void()> clear_target;
	};

	void Attach(WindowManager &windows, ZoneSession &zone, GameWindowState &state, const GameValues &values, Host host);
	void Reset(); // a new zone

	bool Active() const { return m_active; }       // the window's up (Activated)
	bool Waiting() const { return m_merchant != 0 && !m_active; } // asked, no answer yet
	bool Busy() const { return m_busy > 0; }        // DAT_007d0254: a buy or sell the zone hasn't answered
	void Request(uint16_t merchant);                // OP_ShopRequest
	void ZoneEvent(const ZoneSession::Event &event);
	void Deactivate();
	void Frame(uint64_t now_ms);
	// A click on a slot anywhere while it's up: selects it. False when it isn't up, or the
	// control isn't a slot.
	bool SlotClick(const std::string &window, const std::string &control);
	bool Click(const std::string &window, const std::string &control, bool shift, bool ctrl); // Buy, Sell, Done
	void DrawTooltips(Canvas &canvas, TextRenderer &text, int mouse_x, int mouse_y);

private:
	std::string MerchantName() const;
	std::string Text(uint32_t id, const std::vector<std::string> &args, const std::string &fallback) const;
	std::string Coins(int32_t copper, bool selling) const; // ValueSBuy / ValueSSell's " 1 gold 5 copper"
	void Select(int slot);                                 // SelectBuySellSlot
	void Highlight(const std::string &window, const std::string &control); // CInvSlotMgr_SelectSlot
	void ShowPrice(bool buying, const ZoneItem &item);      // DisplayBuyOrSellPrice
	void HandleBuy(int count);
	void HandleSell(int count);
	void FinishBuying(const ZoneMerchant &answer);
	void FinishSelling(const ZoneMerchant &answer);
	const ZoneItem *Selected() const;

	WindowManager *m_windows = nullptr;
	ZoneSession *m_zone = nullptr;
	GameWindowState *m_state = nullptr;
	const GameValues *m_values = nullptr;
	Host m_host;
	uint16_t m_merchant = 0;   // pinstActiveMerchant
	bool m_active = false;
	float m_greed = 1;         // MerchantGreed: the rate OP_ShopRequest sent
	int m_selected = -1;       // InventoryItemSlot: a merchant's 6000.., or ours; -1 none
	bool m_opened_inventory = false;
	uint64_t m_next_check_ms = 0;
	int m_busy = 0;
	bool m_shift = false, m_ctrl = false; // as Buy or Sell was clicked
	std::string m_highlight_window, m_highlight_control;
};
