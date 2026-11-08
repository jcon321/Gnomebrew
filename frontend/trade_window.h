#pragma once

// eqgame's trade window, CTradeWnd (@004390da on), and the give window it shows instead for an
// NPC we hand things to, CGiveWnd (@0041eb3e on):
//   - CEverQuest::DropItemOrMoneyOnPlayer (@005327d8), a left click on a player or NPC with an
//     item or coins on the cursor: with no trade going, nothing else going on (IsOkToTransact)
//     and them within their ModelHeightOffset + 20, OP_TradeRequest (a NO DROP item to a player
//     says why not, CanDrop's eqstr 12921 or 12922); further, "You are too far away from %1 to
//     trade." (12294). With a trade going with them, the item or coins go in (DropItemIntoTrade,
//     DropMoneyIntoTrade);
//   - msg_begin_trade (@004e590c), the go-ahead to ours (OP_TradeRequestAck): Activate (@004392c9)
//     with what's on the cursor put in; for an NPC the give window instead of the trade window.
//     Busy, it turns it down: a tell "I'm busy right now." (1446) and OP_CancelTrade;
//   - msg_req_trade (@004e7231), someone asking us (OP_TradeRequest): Activate with
//     OP_TradeRequestAck, or turned down by "Allow trading with" ('b' no one, 'c' party members
//     only) or busy (1), OP_TradeRefused and "%1 is interested in making a trade." (1447);
//   - msg_refuse_trade (@004e7554): their no, 1444 ('b' nobody), 1445 ('c' only their group) or 1446;
//   - DropItemIntoTrade (@00439d48): onto the same stack in our slots, else into the first empty
//     one (8 for a player, 4 for an NPC), as moves to 3000 + n; none free, "All your trade slots
//     are full!" (12319). A click on an empty slot of ours does the same for it
//     (CInvSlot::HandleLButtonUp); nothing comes back out;
//   - the coins: a click on one of our money buttons with coins on the cursor puts them in
//     (HandleCurrencyTrade), as clicking them did; theirs come in OP_TradeCoins (SetHisMoney);
//   - UpdateTradeDisplay (@0043971d): each side's name red, or green once it's accepted, the coins,
//     the slots; anything changing takes both accepts back;
//   - the Trade (or Give) button: our accept (ClickedTradeButton, OP_TradeAcceptClick); theirs
//     (SetTradeReadyState); OP_FinishTrade closes it (CompleteTrade), what we get coming as its
//     own packets;
//   - Cancel (ClickedCancelButton): OP_CancelTrade, and the zone's OP_CancelTrade closes it
//     (msg_cancel_trade: "You have cancelled the trade." 1450, "%1 has cancelled the trade."
//     1448, "The trade has been cancelled." 1449, then Deactivate, which with the trade target
//     still set sends OP_CancelTrade again and says 1450 too); so do Escape, the close box and walking
//     further than their ModelHeightOffset + 20 (OnProcessFrame, every 500 ms), which cancel
//     (Deactivate @004393a7);
//   - closing, our items and coins in it are forgotten: the zone hands them back.

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

class TradeWindow
{
public:
	struct Host
	{
		std::function<void(const std::string &, int)> chat; // a message, in a chat color type
		std::function<void(uint16_t)> set_target;            // pinstTarget
		std::function<float(uint16_t)> height;               // a spawn's ModelHeightOffset
		std::function<bool()> ok_to_transact;                // CEverQuest_IsOkToTransact
		// msg_req_trade's turn-down from the options' "Allow trading with" (DAT_00798ad8) for a
		// player by name: 'b' (no one), 'c' (party members only, and not one), or 0.
		std::function<uint8_t(const std::string &)> refusal;
	};

	void Attach(WindowManager &windows, ZoneSession &zone, GameWindowState &state, const GameValues &values, Host host);
	void Reset(); // a new zone

	bool Active() const { return m_active; }    // Activated
	uint16_t Target() const { return m_target; } // pinstTradeTarget
	bool Giving() const { return m_give; }       // the give window's up rather than the trade window
	void DropOnPlayer(uint16_t spawn);           // DropItemOrMoneyOnPlayer
	void ZoneEvent(const ZoneSession::Event &event);
	void Deactivate();                           // cancel and close
	void Frame(uint64_t now_ms);
	bool Click(const std::string &window, const std::string &control); // Trade or Give, Cancel, money
	void ItemChanged() { Changed(false); }        // TradeItemChanged: both accepts back
	void DrawTooltips(Canvas &canvas, TextRenderer &text, int mouse_x, int mouse_y);

private:
	std::string Text(uint32_t id, const std::vector<std::string> &args, const std::string &fallback) const;
	std::string Name(uint16_t spawn) const;
	void Activate(uint16_t target, bool ours);
	void Close(bool cancel);
	void DropItem();
	void DropMoney();
	// UpdateTradeDisplay(flag): their accept as the zone says; anything else changing (0) takes ours back too.
	void Changed(bool his_ready)
	{
		m_his_ready = his_ready;
		if (!his_ready)
			m_my_ready = false;
	}
	const char *Screen() const { return m_give ? "GiveWnd" : "TradeWnd"; }

	WindowManager *m_windows = nullptr;
	ZoneSession *m_zone = nullptr;
	GameWindowState *m_state = nullptr;
	const GameValues *m_values = nullptr;
	Host m_host;
	bool m_active = false, m_give = false;
	uint16_t m_target = 0;
	bool m_my_ready = false, m_his_ready = false;
	int32_t m_his_coins[4] = {}; // platinum first
	bool m_opened_inventory = false;
	uint64_t m_next_check_ms = 0;
};
