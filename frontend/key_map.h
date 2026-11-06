#pragma once

// eqgame's keymap: the commands a key can be bound to (ExecuteCmd's numbers), each with a key
// and an alternate key (instKeypressHandler and the alternate map after it, @007cd84c and
// @007cdc4c). A binding is a DirectInput key code (the scan code, 0x80 more for an extended key)
// with 0x10000000 for Shift, 0x20000000 for Ctrl and 0x40000000 for Alt; NoKey for none.
//
// The Options window's keyboard page lists them (initkeyboardassignments @0042a9d7: the eqstr name
// and the categories of each), they're kept in my-client.ini's [KeyMaps] the way eqgame keeps them
// in eqclient.ini's (_writeKeyMapToIni @0052505a, readKeyMapFromIni @00525520), and a key pressed
// in the zone runs whatever it's bound to (ProcessMovementKeys @005257fa, ProcessKeyUp @0052462a).

#include <cstdint>
#include <string>
#include <vector>

class IniFile;

namespace KeyMap
{
	constexpr uint32_t NoKey = 0xffffffff;
	constexpr uint32_t Shift = 0x10000000, Ctrl = 0x20000000, Alt = 0x40000000;
	constexpr int Count = 256;     // entries in each map (ExecuteCmd's numbers)
	constexpr int Assignable = 0x74; // KeyboardPageHandleKeyboardMsg: past this, a click just cancels
	                                 // (eqgame's; our own commands, from OwnFirst, can be assigned)

	// ExecuteCmd's numbers for the commands the zone runs (the map's entries).
	enum Cmd
	{
		Autorun = 1, Jump, Forward, Back, Right, Left, MouseLook, AutoAttack, Consider, Duck, Hail, Inventory, Tell,
		Use, PitchUp, PitchDown, CenterView, ZoomIn, ZoomOut, ToggleCam, FullScreen, TargetMe, Party1, Party2, Party3,
		Party4, Party5, TargetPc, TargetNpc, NetStat, Hot1, Hot10 = Hot1 + 9, HotPage1, HotPage10 = HotPage1 + 9,
		Cast1, Cast8 = Cast1 + 7, Reply, CycleReply, CycleReplyBack, Backdrop, ToggleTarget, SpellBook, Abilities, Combat,
		Socials, Main, Who, InviteFollow, Disband, Camp, SitStand, RunWalk, ClipIn, ClipOut, VoiceOn, ScreenCap,
		HistoryUp, HistoryDown, ChatLeft, ChatRight, ChatBegin, ChatEnd, PageUp, PageDown, CmdModeSay, CmdModeEmote,
		LockWindows, TogglePlayerWin, TogglePartyWin, ToggleTargetWin, ToggleSpellsWin, ToggleBuffWin, ToggleHotboxWin,
		ToggleChatWin, ToggleMainMenuWin, ToggleAltAdvWin, ToggleBazaarWin, RTarget, ToggleFriendsWin,
		TogglePetInfoWin, ToggleOptionsWin, ToggleHelpWin, ToggleSelectorWin, ToggleVideoModeWin,
		ToggleBazaarSearchWin, ToggleCompassWin, ToggleRaidWin, ToggleMusicPlayerWin, ForwardCam, BackCam, RightCam,
		LeftCam, PitchUpCam, PitchDownCam,
		// Ours from here, commands eqgame doesn't have (Zeal's numbers where it has them).
		OwnFirst = 200,
		CycleNearestTarget = 213, // Zeal's "Cycle through nearest NPCs" (CycleTarget::handle_next_target)
	};

	// The keyboard page's categories (its combo box's choices, in order, the last All).
	enum Category : uint32_t
	{
		Movement = 1, Commands = 2, Spells = 4, Target = 8, Camera = 0x10, Chat = 0x20, UI = 0x40, Macros = 0x80,
	};
	// COptionsWnd::UpdateKeyboardCategoryList: the combo box's choice to the categories it shows.
	uint32_t CategoryMask(int choice);

	struct Command
	{
		int id;              // ExecuteCmd's number
		int eqstr;           // its name on the keyboard page (0: ours, `name` it is)
		const char *name;    // that name in English
		uint32_t categories; // Category bits
		bool implemented;    // ours: my-client does what it does (the page's temporary 4th column)
	};
	// The keyboard page's commands, in ExecuteCmd's order (how the page lists them).
	const std::vector<Command> &List();
	const Command *Find(int id);

	// __BindList: the command's name in the ini's keys (KEYMAP_<name>, KEYMAP_<name>_2), "" for none.
	const char *BindName(int id);

	// default_key_bindings (@0055a83b), with the movement keys on W, A, S and D and the arrows
	// their alternates, Autoattack on Q and Duck on F, Toggle last two targets on no key (not Tab)
	// and our Cycle nearest target on Tab (ours).
	uint32_t DefaultKey(int id, bool alternate);

	// CEverQuest_IsValidKeyMap: Escape, Enter, / and the Windows keys can't be bound.
	bool IsValidKey(int scan);
	// The modifier keys themselves, which don't end an assignment (KeyboardPageHandleKeyboardMsg).
	bool IsModifierKey(int scan);
	// ConvertScanCodeToChar, the US table: the character a key types, 0 for none. A key that types
	// one runs no command while the chat line has the keys (ExecuteCmd's isCharacterAllowed).
	int TypedChar(int scan);
	// GetKeypressDescription: "Shift A", "CtlAlt NPAD 8", "-------" for none.
	std::string Describe(uint32_t key);
}

// The two maps, as eqgame keeps them.
class KeyBindings
{
public:
	KeyBindings() { Defaults(); }

	void Defaults(); // default_key_bindings
	uint32_t Get(int id, bool alternate) const;
	void Set(int id, bool alternate, uint32_t key);

	// CEverQuest_loadOptions' readKeyMapFromIni: each command's keys from [KeyMaps], the default
	// where there's none.
	void Load(const IniFile &ini);
	// _writeKeyMapToIni: one key into [KeyMaps]; DeleteKeyBindingFromConfig: both of a command's
	// keys out of it (ResetKeysToDefault).
	static void Store(IniFile &ini, int id, bool alternate, uint32_t key);
	static void Remove(IniFile &ini, int id);

	// The commands a key down runs (ProcessMovementKeys): the turns and moves match without
	// Ctrl, the zooms without Alt, the rest as they are. `key` has its modifier bits.
	std::vector<int> Down(uint32_t key) const;
	// The commands a key let go of stops (ProcessKeyUp): the moves, zooms and camera moves by the
	// key alone, the rest with the modifiers held as it comes up.
	std::vector<int> Up(uint32_t key) const;

private:
	uint32_t m_keys[2][KeyMap::Count];
};
