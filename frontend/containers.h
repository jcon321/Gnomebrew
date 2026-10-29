#pragma once

// eqgame's bag windows, CContainerMgr (@00416345 on) and CContainerWnd (@00416ae1 on):
//   - CInvSlot::HandleRButtonUp (@00422804): the right button on a slot with a bag in it, with no
//     Shift, Ctrl or Alt down, opens its window (OpenContainer @004168bd), or closes it if it's
//     open (CloseContainer @004169e5); not while a combine waits for the zone, nor while we cast
//     (but for a bard's songs);
//   - opening and closing play WavePlay 0x91, or 0x86 and 0x85 for the bag types 6, 7, 10, 15,
//     18..20 and 22 (the tradeskill containers that open and shut: OpenContainer, CloseEQContainer);
//   - GetFreeContainerWnd: at most 17 up at once;
//   - SetContainer (@0041717d): the window is kept in the layout under a name from the bag's
//     slot (EnableIniStorage: BagInv1..8 for the general slots, BagBank1..8, BagTrade1..16,
//     BagWorld1), first shown where its screen says, moved over by the bag's place (a general
//     slot's 5 right and 2 down for each one after the first, a bank slot's 5 left and 2 down, a
//     trade slot's 5 left and 2 up); the bag's name on Container_Label (the title stays
//     "Container") and its icon as Container_Icon's decal; its first bag_slots slots shown, holding what's in it, the rest hidden;
//     Container_Combine under them for a tradeskill container (a type over 8 but not 51), the
//     Done button under that, each 4 below what's above it and centered; and the window sized
//     around them all, 14 wider and 36 taller than they take;
//   - WndNotification (@00416d9e): Done and the close box close it;
//   - HandleCombine (@00416e5b), for a bag in a general slot (or a world container: not yet):
//     the class and race checks of the class and racial containers (eqstr 12010..12023), nothing
//     on the cursor (12024) and something in it (12025); then OP_TradeSkillCombine with the
//     bag's type, slot and id and its items' ids, and the slots take no clicks (DAT_007d0254)
//     until the zone answers it;
//   - CloseContainer from CInvSlot::HandleLButtonUp: a bag picked up or swapped out closes its
//     window; so does one the zone moves away (msg_move_item);
//   - CloseAllContainers (@00416a43): Escape closes them all (CDisplay_NewUIProcessEscape, after
//     the skills window), and the bank window closing does;
//   - PostDraw (@00416cfc): with Alt down and the mouse over it, every slot's tooltip, at the
//     slot's right less 5, its top + 2 in the first column and + 22 in the second.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class Canvas;
class GameValues;
class TextRenderer;
class WindowManager;
class ZoneSession;
struct ZoneItem;

class Containers
{
public:
	struct Host
	{
		std::function<void(int)> sound;                     // EqSoundManager::WavePlay
		std::function<void(const std::string &, int)> chat; // a message, in a chat color type
	};

	// The bag windows go up with the others, hidden, before the layout's read; each is the
	// ContainerWindow screen under its layout name.
	static void AddWindows(WindowManager &windows);
	void Attach(WindowManager &windows, ZoneSession &zone, const GameValues &values, Host host);
	void Reset(); // none open (a new zone); the windows hidden

	// The window of the bag in `slot`: up if it isn't, closed if it is. False if there's no bag there.
	bool Toggle(int16_t slot);
	bool Open(int16_t slot);
	void Close(int16_t slot);
	bool CloseAll(); // true if any was open
	bool IsOpen(int16_t slot) const;
	// The window a bag's in, if it's up; "" otherwise.
	std::string WindowOf(int16_t slot) const;

	// Every frame: windows closed by their close box, bags gone from their slots, and what's in
	// the open ones' slots.
	void Update();
	// A click on Done or Combine in one of them; false if it isn't one of theirs.
	bool Click(const std::string &window, const std::string &control);
	// The zone answered a combine (an empty OP_TradeSkillCombine).
	void CombineAnswered() { m_combining = false; }
	bool Combining() const { return m_combining; }
	// PostDraw: with Alt down, every slot's tooltip in the window under the mouse.
	void DrawTooltips(Canvas &canvas, TextRenderer &text, int mouse_x, int mouse_y);

	static std::string WindowName(int16_t slot); // "" for a slot no bag window opens from

private:
	struct Bag
	{
		int16_t slot;
		std::string window;
	};
	void SetContainer(const std::string &window, const ZoneItem &bag, int16_t slot);
	void Combine(const Bag &bag);
	void PlayOpenClose(const ZoneItem *bag, bool open);

	WindowManager *m_windows = nullptr;
	ZoneSession *m_zone = nullptr;
	const GameValues *m_values = nullptr;
	Host m_host;
	std::vector<Bag> m_open;
	bool m_combining = false;
};
