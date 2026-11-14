#pragma once

// eqgame's Bazaar search window, CBazaarSearchWnd (@00404e21 on), and the welcome
// (CBazaarWnd_HandleBazaarMsg @004e34b4).
//   - /bazaar (@004ff4b0) and Toggle bazaar search window: InitializeBazaarSearchWindow (@00405686),
//     only in the Bazaar: Init (the results cleared, the traders' combo filled, the price boxes "0",
//     "To begin searching for items..." in the chat), the window up, every column sorting
//     ascending; then UpdateBazaarSearchUIState either way. Deactivate (@004056e7): the window down.
//   - The combos (Any Type, Any Slot, Any Race, Any Class, Any Stat: eqgame's tables @005e3844 on, a
//     string and the value sent for each) and All Traders plus every trader here, by name
//     (UpdatePlayerCombo); a trader's mode changing (OP_BecomeTrader, SpawnAppearance 31) turns
//     Update Players on, and Update Players fills it again and turns itself off.
//   - UpdateBazaarSearchUIState (@00405e27): Find on with any combo chosen or two letters of a name,
//     and anyone in the traders' combo; Request Item on with a result selected.
//   - Find, or Enter in the name box (PerformBazaarSearch @0040614c): the results cleared, Find off,
//     the stat's name as the fourth column's heading, and OP_BazaarSearch 7 (BazaarSearch_Struct:
//     the trader's id or 0, class, race, stat, slot, type (-1 any), the name, min and max price).
//   - HandleBazaarMsg (@004060b5): a result (7) onto the list (AddItemtoList: up to 100; the name,
//     the price, the seller's name, and the int at 16 as the stat for a fourth column, which with
//     EQMacEmu's struct is four of the name's letters, but the default skin's list has three
//     columns); 12 the results done: sorted by the last column clicked, Find back on.
//   - A column's heading clicked (notification 0xe): its direction flipped, the list sorted by it
//     (SortItemList @0040581e: the name without case, the price, the seller, the stat).
//   - Request Item, or the right button on a result's name: OP_ItemLinkResponse {its item id, its
//     name up to the last '('}, the item display with the zone's answer. The right button on its
//     seller: we turn to face them (EQPlayer::FacePlayer).
//   - Default: every combo back to its first, the name empty, the prices "0". Welcome: OP_BazaarSearch
//     9, whose answer (9) is "Welcome to The Bazaar!" and the counts.

#include "zone_session.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class WindowManager;

class BazaarSearch
{
public:
	static constexpr const char *Screen = "BazaarSearchWnd";
	static constexpr int MaxResults = 100;

	struct Host
	{
		std::function<void(const std::string &, int)> chat; // print_chat_dsp_chat / text_to_client
		std::function<void(uint16_t)> face;                 // EQPlayer::FacePlayer
	};

	void Attach(WindowManager &windows, ZoneSession &zone, Host host);
	bool Active() const { return m_active; }
	void Activate(); // InitializeBazaarSearchWindow
	void Deactivate();
	void Toggle() { m_active ? Deactivate() : Activate(); }
	void TraderChanged(); // UpdatePlayerUpdateButton(1)
	// OP_BazaarSearch from the zone (CBazaarWnd_HandleBazaarMsg): the welcome, results and their end.
	void BazaarMessage(const std::vector<uint8_t> &data);
	bool Click(const std::string &window, const std::string &control);
	bool RightClick(const std::string &window, const std::string &control);
	bool Enter(); // Enter in the name box (notification 6); false when it hasn't the focus
	void Frame(); // the edit boxes' text changing (notification 0xd), the close box

	struct Result
	{
		std::string seller, name;
		int32_t cost = 0;
		int32_t item = 0; // the int at 4: the item id, and the seller's id over it
		int32_t stat = 0;
	};
	const std::vector<Result> &Results() const { return m_results; }

private:
	void Init();
	void UpdatePlayerCombo();
	void UpdateUIState();
	void Perform();
	void AddResult(const Result &result);
	void Sort(int column);
	void RequestItem(int row);
	std::string InputText(const char *control) const;
	void SetText(const char *control, const std::string &text);
	int Choice(const char *control) const;
	void Enable(const char *control, bool on);

	WindowManager *m_windows = nullptr;
	ZoneSession *m_zone = nullptr;
	Host m_host;
	bool m_active = false;
	std::vector<Result> m_results;
	bool m_descending[4] = {}; // 0x138..0x144
	int m_sort_column = 0;     // 0x148
	std::string m_name_text, m_min_text, m_max_text;
};
