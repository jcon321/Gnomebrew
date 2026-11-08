#pragma once

// eqgame's bank window, CBankWnd (@00404605 on):
//   - CEverQuest::RightClickedOnPlayer (@00532949): the right button on a banker (class 16), no
//     more than 20 away, with no bank up already and nothing else going on (IsOkToTransact),
//     activates it;
//   - Activate (@00404758): visible again (MakeMeVisible), "%1 tells you, 'Welcome to my bank!'"
//     (eqstr 1465) with the banker's name, auto attack off (the front end's, after it), the bank's
//     music on (SetMusicSelection 3), the banker's name on BW_BankerName, the bank's coins on
//     BW_Money0..3 (UpdateMoneyDisplay), the window up, and the inventory with it if it wasn't;
//   - OnProcessFrame (@004049f4): every 500 ms, the banker more than 20 away leaves the bank
//     (CEverQuest_LeaveBankMode, saying so);
//   - WndNotification (@00404a60): BW_DoneButton leaves the bank; a money button is
//     ClickedMoneyButton (game_windows.h's MoneyClick): coins change into others at the bank;
//   - Deactivate (@00404877), from Done, Escape (first of the windows it closes after the item
//     display and quantity windows) and walking away: the window down, the music off, "%1 tells you, 'Come back
//     soon!'" (12143), the quantity window down if it was the bank's, the inventory down if the
//     bank put it up, and every bag window closed (CloseAllContainers);
//   - PostDraw (@0040495c): with Alt down and the mouse over it, every bank slot's tooltip, as
//     CContainerWnd's.
// Its slots are 2000..2007 (BW_BankSlot0..7's EQTypes); their bags open as BagBank1..8.

#include <cstdint>
#include <functional>
#include <string>

class Canvas;
class EqStr;
class GameValues;
class TextRenderer;
class WindowManager;
class ZoneSession;
struct GameWindowState;

class BankWindow
{
public:
	struct Host
	{
		std::function<void(const std::string &, int)> chat; // a message, in a chat color type
		std::function<void()> close_bags;                   // CloseAllContainers
		std::function<void(bool)> music;                    // SetMusicSelection(3, on)
		std::function<void()> make_visible;                 // EQ_Character::MakeMeVisible
	};

	void Attach(WindowManager &windows, ZoneSession &zone, GameWindowState &state, const GameValues &values, const EqStr *eqstr,
		Host host);
	void Reset(); // a new zone: not banking

	bool Active() const { return m_banker != 0; } // field200_0x134
	uint16_t Banker() const { return m_banker; }
	void Activate(uint16_t banker);
	void Deactivate(bool say);
	void Frame(uint64_t now_ms);
	bool Click(const std::string &window, const std::string &control); // Done
	void DrawTooltips(Canvas &canvas, TextRenderer &text, int mouse_x, int mouse_y);

private:
	std::string BankerName() const; // trimName: its name as shown

	WindowManager *m_windows = nullptr;
	ZoneSession *m_zone = nullptr;
	GameWindowState *m_state = nullptr;
	const GameValues *m_values = nullptr;
	const EqStr *m_eqstr = nullptr;
	Host m_host;
	uint16_t m_banker = 0;
	uint64_t m_next_check_ms = 0;
	bool m_opened_inventory = false; // field206_0x140: the inventory wasn't up
};
