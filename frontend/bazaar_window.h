#pragma once

// eqgame's Bazaar manager, CBazaarWnd (its constructor is the decompile's
// CBazaarSearchWnd::InitializeBazaarWindow @004068b8; several of its methods are named
// CBazaarSearchWnd's there too). A trader's own window: the items in their Trader's Satchels
// (item 17899) with the prices they set, and trader mode on and off.
//   - /trader (@004ff4d4) and Toggle bazaar window (ExecuteCmd's): Activate (@00407550), only in
//     the Bazaar (zone 151) and IsOkToTransact: Init's empty list (80 entries: item id, price, the
//     item, on sale), no item selected, a price of 0; "Welcome to your Bazaar Manager Window."; then
//     BuildBazaarItemArray (@004073a3): every item in every satchel in the general slots, in order,
//     onto BZR_BazaarSlot0..79 (slots 7000..7079), but a NO DROP one (unless CanDrop under the FV
//     rules), each with its stored price ([ItemToSell] <name>= in .\BZR_<name>_<server>.ini, "*NULL*"
//     none: then written 0); and "<n> items were found in Trader Satchels." or why none.
//   - A click on a slot (CInvSlot::HandleLButtonUp's 7000..7079: ProcessBazaarItemSelection
//     @004079c5): it's selected, its stored price on the money buttons (UpdateCoinDisplayButtons).
//   - The money buttons BZW_Money0..3 (platinum .. copper): with an item selected, the quantity
//     window (up to 1000000 platinum, 100 of the rest) sets that coin (SetMoneyButton: the price
//     made of all four, at most 1000000000 copper, and the buttons redrawn from it).
//   - UpdatePriceButtons (@004078ac): Start shown out of trader mode, End in it (with List on);
//     Set Price with an item selected; Clear with a price, and in trader mode Add for an item not on
//     sale or Remove for one that is.
//   - Set Price: the selected item's price stored (StoreSelectedPrice: each entry of that item, and
//     the ini), and in trader mode either "Click Add..." or OP_Trader 3 with the new price. Clear: a
//     price of 0 stored the same way, and in trader mode OP_Trader 3 with 0 and the item off sale.
//     Add: OP_Trader 3 with the price. Remove: OP_Trader 3 with 0, off sale.
//   - Start (SubmitBazaarItems @00407cea): under a row of trader stalls
//     (EQ_Character::IsSpecialBazaarSpot @004bd927) every entry with a price goes (OP_Trader 1:
//     their ids and prices) and is marked on sale; else why not. End: OP_Trader 2, nothing on sale.
//     List: OP_Trader 11, our list as the zone has it.
//   - HandleTraderMsg (@00406f95), OP_Trader from the zone: 1 trader mode on (or "You must be in
//     one of the bazaar merchant areas..."), every entry on sale green, and OP_Trader 11 asked;
//     2 off ("Bazaar Trader Mode is now *OFF*" in the chat too); 3 a price's answer for the selected
//     item (0 couldn't, 1 updated, 2 removed, 3 added); 10 a customer's purchase, and the list made
//     again from the satchels keeping each item's price and sale; 11 our list (each item's name by
//     its id in the satchels, and price); 13 a customer browsing.
//   - AddBazaarText (@00406cbd): each line onto BZW_BazaarText in a chat color, and appended to
//     bzrlog_<server>_<name>.txt as "[ctime] line" (bazaarlog @00524219).
//   - Deactivate (@004075d0), from the toggle, Escape and the close box: the window down and
//     OP_Trader 2 (trader mode ends with it).

#include "zone_session.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>

class GameValues;
class WindowManager;
struct GameWindowState;

class BazaarWindow
{
public:
	static constexpr const char *Screen = "BazaarWnd";
	static constexpr int Slots = 80;
	static constexpr int FirstSlot = 7000; // BZR_BazaarSlot0's item slot
	static constexpr uint16_t Satchel = 17899; // a Trader's Satchel (0x45eb)

	struct Host
	{
		std::function<uint32_t(int)> color;                 // CChatManager::GetRGBAFromIndex: 0xRRGGBB
		std::function<void(const std::string &, int)> chat; // text_to_client
		std::function<bool()> ok_to_transact;               // CEverQuest_IsOkToTransact
	};

	void Attach(WindowManager &windows, ZoneSession &zone, GameWindowState &state, const GameValues &values, Host host);
	// BZR_<name>_<server>.ini and bzrlog_<server>_<name>.txt (InitializeBazaarSearchFilePath,
	// bazaarlog: eqgame's in its own folder; ours next to the exe), once the character's known.
	void SetFiles(const std::string &ini_path, const std::string &log_path);

	bool Active() const { return m_active; }
	bool TraderMode() const { return m_trader_mode; }
	void Activate();
	void Deactivate();
	void Toggle() { m_active ? Deactivate() : Activate(); }
	void TraderMessage(const std::vector<uint8_t> &data); // HandleTraderMsg
	// A click on one of its slots (ProcessBazaarItemSelection); false for any other control.
	bool SlotClick(const std::string &window, const std::string &control);
	bool Click(const std::string &window, const std::string &control, bool shift, bool ctrl);
	void Frame(); // the slots' icons, and the window closed by its close box

	// EQ_Character::IsSpecialBazaarSpot: in the Bazaar under one of its rows of trader stalls.
	static bool IsSpecialBazaarSpot(uint32_t zone_id, float y, float x);

private:
	struct Entry
	{
		int32_t item = 0;  // its item id (+0xb4)
		int32_t price = 0; // copper; 0 none
		int16_t slot = -1; // where it is (the satchel's slot)
		bool active = false; // on sale (green)
	};

	void Init();
	void AddText(const std::string &text, int color);
	void BuildItemArray();
	void AddItem(int index, int16_t slot, int32_t price); // AddItemToBazaarList
	void UpdateFromInventory();                         // UpdateBazaarItemsFromInventory
	void ToggleActive(int32_t item, bool on);           // ToggleBzrItemActive (-1 every entry)
	void UpdateCoinButtons();
	void UpdatePriceButtons();
	bool StoreSelectedPrice();
	int32_t ButtonPrice() const;                        // GetQtyFromCoinType(): the four buttons
	int ButtonValue(int coin) const;
	void SetMoneyButton(int coin, int value);
	void ClickedMoneyButton(int coin, int value, bool shift, bool ctrl);
	int SubmitItems();
	void SendPrice(int32_t item, int32_t price);        // OP_Trader 3
	void SendCode(uint16_t code);                       // OP_Trader 2 or 11 (20 bytes)
	const ZoneItem *SelectedItem() const;
	const ZoneItem *FindInSatchels(int32_t id) const;   // CBazaarSearchWnd_FindItemInBazaarBag
	void Enable(const char *control, bool on);
	void Show(const char *control, bool on);

	WindowManager *m_windows = nullptr;
	ZoneSession *m_zone = nullptr;
	GameWindowState *m_state = nullptr;
	const GameValues *m_values = nullptr;
	Host m_host;
	std::string m_ini_path, m_log_path;
	std::array<Entry, Slots> m_entries;
	bool m_active = false;      // 0x134
	bool m_trader_mode = false; // 0x800
	int m_selected = -1;        // 0x138: the selected entry
	int16_t m_selected_slot = -1; // 0x148: the selected item, where it is
	int32_t m_price = 0;        // 0x140: the buttons' price, copper
};
