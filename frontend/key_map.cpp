#include "key_map.h"

#include "ini_file.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace KeyMap
{
	namespace
	{
		static_assert(Hot1 == 31 && HotPage1 == 41 && Cast1 == 51 && Reply == 59 && SpellBook == 64 &&
		              HistoryUp == 79 && LockWindows == 89 && ToggleMainMenuWin == 97 && RTarget == 100 &&
		              ToggleCompassWin == 108 && ForwardCam == 111 && PitchDownCam == 116);

		// initkeyboardassignments: InitKeyBind(id, eqstr, categories), sorted by id as the page lists
		// them (UpdateKeyboardCategoryList walks the map). The last field is ours.
		const std::vector<Command> CommandTable = {
			{Autorun, 6504, "Autorun/walk", Movement, true},
			{Jump, 6505, "Jump", Movement, true},
			{Forward, 6500, "Move forward", Movement, true},
			{Back, 6501, "Move back", Movement, true},
			{Right, 6502, "Move right", Movement, true},
			{Left, 6503, "Move left", Movement, true},
			{MouseLook, 6539, "Toggle mouse look", Movement | Camera, false},
			{AutoAttack, 6510, "Autoattack", Commands, false},
			{Consider, 6512, "Consider", Commands, true},
			{Duck, 6506, "Duck", Movement, false},
			{Hail, 6529, "Say hail", Chat, true},
			{Inventory, 6585, "Toggle inventory window", UI, true},
			{Tell, 6530, "Send tell", Chat, false},
			{Use, 6511, "Use centerscreen", Commands, false},
			{PitchUp, 6507, "Look up", Movement, true},
			{PitchDown, 6508, "Look down", Movement, true},
			{CenterView, 6509, "Center view", Movement, false},
			{ZoomIn, 6537, "Zoom in", Camera, false},
			{ZoomOut, 6538, "Zoom out", Camera, false},
			{ToggleCam, 6536, "Toggle camera view", Camera, true},
			{FullScreen, 3153, "Toggle UI Display", UI, false},
			{TargetMe, 6543, "Target myself", Target, true},
			{Party1, 6544, "Target party member 1", Target, false},
			{Party2, 6545, "Target party member 2", Target, false},
			{Party3, 6546, "Target party member 3", Target, false},
			{Party4, 6547, "Target party member 4", Target, false},
			{Party5, 6548, "Target party member 5", Target, false},
			{TargetPc, 6549, "Target nearest PC", Target, false},
			{TargetNpc, 6550, "Target nearest NPC", Target, false},
			{NetStat, 6589, "Toggle netstat", UI, false},
			{Hot1, 6552, "Hot button 1", Macros, true},
			{Hot1 + 1, 6553, "Hot button 2", Macros, true},
			{Hot1 + 2, 6554, "Hot button 3", Macros, true},
			{Hot1 + 3, 6555, "Hot button 4", Macros, true},
			{Hot1 + 4, 6556, "Hot button 5", Macros, true},
			{Hot1 + 5, 6557, "Hot button 6", Macros, true},
			{Hot1 + 6, 6558, "Hot button 7", Macros, true},
			{Hot1 + 7, 6559, "Hot button 8", Macros, true},
			{Hot1 + 8, 6560, "Hot button 9", Macros, true},
			{Hot1 + 9, 6561, "Hot button 10", Macros, true},
			{HotPage1, 6562, "Hot button page 1", Macros, true},
			{HotPage1 + 1, 6563, "Hot button page 2", Macros, true},
			{HotPage1 + 2, 6564, "Hot button page 3", Macros, true},
			{HotPage1 + 3, 6565, "Hot button page 4", Macros, true},
			{HotPage1 + 4, 6566, "Hot button page 5", Macros, true},
			{HotPage1 + 5, 6567, "Hot button page 6", Macros, true},
			{HotPage1 + 6, 6568, "Hot button page 7", Macros, true},
			{HotPage1 + 7, 6569, "Hot button page 8", Macros, true},
			{HotPage1 + 8, 6570, "Hot button page 9", Macros, true},
			{HotPage1 + 9, 6571, "Hot button page 10", Macros, true},
			{Cast1, 6572, "Cast gem 1", Spells, true},
			{Cast1 + 1, 6573, "Cast gem 2", Spells, true},
			{Cast1 + 2, 6574, "Cast gem 3", Spells, true},
			{Cast1 + 3, 6575, "Cast gem 4", Spells, true},
			{Cast1 + 4, 6576, "Cast gem 5", Spells, true},
			{Cast1 + 5, 6577, "Cast gem 6", Spells, true},
			{Cast1 + 6, 6578, "Cast gem 7", Spells, true},
			{Cast1 + 7, 6579, "Cast gem 8", Spells, true},
			{Reply, 6532, "Reply", Chat, false},
			{CycleReply, 6533, "Cycle reply, forward", Chat, false},
			{CycleReplyBack, 6534, "Cycle reply, back", Chat, false},
			{ToggleTarget, 6551, "Toggle last two targets", Target, false},
			{SpellBook, 6590, "Toggle spellbook window", Spells | UI, true},
			{Abilities, 6582, "Open actions ability page", UI, true},
			{Combat, 6583, "Open actions combat page", UI, true},
			{Socials, 6584, "Open actions social page", UI, true},
			{Main, 6581, "Open actions main page", UI, true},
			{Who, 6513, "Who", Commands, false},
			{InviteFollow, 6514, "Invite/follow", Commands, false},
			{Disband, 6515, "Disband", Commands, false},
			{Camp, 6516, "Camp", Commands, true},
			{SitStand, 6517, "Sit/stand", Commands, true},
			{RunWalk, 6518, "Run/walk", Commands, true},
			{ClipIn, 6540, "Clip plane in", Camera, false},
			{ClipOut, 6541, "Clip plane out", Camera, false},
			{VoiceOn, 6535, "Toggle voice input", Chat, false},
			{ScreenCap, 6542, "Take a screenshot", Camera, false},
			{HistoryUp, 6523, "History, up", Chat, true},
			{HistoryDown, 6524, "History, down", Chat, true},
			{ChatLeft, 6519, "Cursor, left", Chat, false},
			{ChatRight, 6520, "Cursor, right", Chat, false},
			{ChatBegin, 6521, "Cursor, beginning", Chat, false},
			{ChatEnd, 6522, "Cursor, end", Chat, false},
			{PageUp, 6525, "Chat text, page up", Chat, false},
			{PageDown, 6526, "Chat text, page down", Chat, false},
			{CmdModeSay, 6527, "Command mode, say", Chat, false},
			{CmdModeEmote, 6528, "Command mode, emote", Chat, false},
			{TogglePlayerWin, 6593, "Toggle player window", UI, true},
			{TogglePartyWin, 6594, "Toggle party window", UI, true},
			{ToggleTargetWin, 6595, "Toggle target window", UI, true},
			{ToggleSpellsWin, 6586, "Toggle spellgems window", Spells | UI, true},
			{ToggleBuffWin, 6588, "Toggle buff window", UI, true},
			{ToggleHotboxWin, 6587, "Toggle hotbox window", UI | Macros, true},
			{ToggleMainMenuWin, 6580, "Toggle actions window", UI, true},
			{ToggleAltAdvWin, 6591, "Toggle Alt. Adv. window", UI, false},
			{ToggleBazaarWin, 6592, "Toggle bazaar window", UI, false},
			{RTarget, 6531, "Send tell to target", Target | Chat, false},
			{ToggleFriendsWin, 6599, "Toggle friend window", UI, true},
			{TogglePetInfoWin, 6600, "Toggle pet info window", UI, true},
			{ToggleOptionsWin, 6601, "Toggle options window", UI, true},
			{ToggleSelectorWin, 6603, "Toggle selector window", UI, true},
			{ToggleBazaarSearchWin, 6608, "Toggle bazaar search window", UI, false},
			{ToggleCompassWin, 3144, "Toggle Compass", UI, true},
			{ToggleRaidWin, 5107, "Toggle Raid Window", UI, false},
			{ToggleMusicPlayerWin, 3150, "Toggle Music Player Window", UI, false},
			{ForwardCam, 6692, "Raise camera", Camera, false},
			{BackCam, 6693, "Lower camera", Camera, false},
			{RightCam, 6694, "Tether-slide camera right", Camera, false},
			{LeftCam, 6695, "Tether-slide camera left", Camera, false},
			{PitchUpCam, 6696, "Pitch camera up", Camera, false},
			{PitchDownCam, 6697, "Pitch camera down", Camera, false},
		};

		// __BindList (@00611220), from 0: the ini's names of ExecuteCmd's numbers (*NULL* for 0).
		const char *const BindNames[] = {"*NULL*", "AUTORUN", "JUMP", "FORWARD", "BACK", "RIGHT", "LEFT", "MOUSELOOK",
			"AUTOPRIM", "CONSIDER", "DUCK", "HAIL", "INVENTORY", "TELL", "USE", "PITCHUP", "PITCHDOWN", "CENTERVIEW",
			"ZOOMIN", "ZOOMOUT", "TOGGLECAM", "FULLSCREEN", "TARGETME", "PARTY1", "PARTY2", "PARTY3", "PARTY4",
			"PARTY5", "TARGETPC", "TARGETNPC", "NETSTAT", "HOT1", "HOT2", "HOT3", "HOT4", "HOT5", "HOT6", "HOT7",
			"HOT8", "HOT9", "HOT10", "HOTPAGE1", "HOTPAGE2", "HOTPAGE3", "HOTPAGE4", "HOTPAGE5", "HOTPAGE6",
			"HOTPAGE7", "HOTPAGE8", "HOTPAGE9", "HOTPAGE10", "CAST1", "CAST2", "CAST3", "CAST4", "CAST5", "CAST6",
			"CAST7", "CAST8", "REPLY", "CYCLEREPLY", "CYCLEREPLY_BACK", "BACKDROP", "TOGGLETARGET", "SPELLBOOK",
			"ABILITIES", "COMBAT", "SOCIALS", "MAIN", "WHO", "INVITE_FOLLOW", "DISBAND", "CAMP", "SIT_STAND",
			"RUN_WALK", "CLIP_IN", "CLIP_OUT", "VOICE_ON", "SCREENCAP", "HISTORY_UP", "HISTORY_DOWN", "CHATLEFT",
			"CHATRIGHT", "CHATBEGIN", "CHATEND", "PAGEUP", "PAGEDOWN", "CMDMODE_SAY", "CMDMODE_EMOTE",
			"LOCK_WINDOWS", "TOGGLE_PLAYERWIN", "TOGGLE_PARTYWIN", "TOGGLE_TARGETWIN", "TOGGLE_SPELLSWIN",
			"TOGGLE_BUFFWIN", "TOGGLE_HOTBOXWIN", "TOGGLE_CHATWIN", "TOGGLE_MAINMENUWIN", "TOGGLE_ALTADVWIN",
			"TOGGLE_BAZAARWIN", "RTARGET", "TOGGLE_FRIENDSWIN", "TOGGLE_PETINFOWIN", "TOGGLE_OPTIONSWIN",
			"TOGGLE_HELPWIN", "TOGGLE_SELECTORWIN", "TOGGLE_VIDEOMODEWIN", "TOGGLE_BAZAARSEARCHWIN",
			"TOGGLE_COMPASSWIN", "TOGGLE_RAIDWIN", "TOGGLE_MUSICPLAYERWIN", "FORWARD_CAM", "BACK_CAM", "RIGHT_CAM",
			"LEFT_CAM", "PITCHUP_CAM", "PITCHDOWN_CAM", "TOGGLE_FPS", "TOGGLE_AVATAR", "TOGGLE_PETITION",
			"TOGGLE_MEMINFO", "FLY_UP", "FLY_DOWN", "TOGGLE_DEV_WINDOW", "ADD_ROUTE", "LAY_PPOINT", "LAY_LINK"};

		struct DefaultBinding
		{
			int id;
			uint32_t key, alternate;
		};
		// default_key_bindings, but for the first eight (ours: W A S D with the arrows, Q, F).
		const DefaultBinding Defaults[] = {
			{Autorun, 0x45, NoKey}, {Jump, 0x39, NoKey},
			{Forward, 0x11, 0xc8}, {Back, 0x1f, 0xd0}, {Right, 0x20, 0xcd}, {Left, 0x1e, 0xcb},
			{AutoAttack, 0x10, NoKey}, {Duck, 0x21, NoKey},
			{MouseLook, 0x58, NoKey}, {Consider, 0x2e, NoKey}, {Hail, 0x23, NoKey}, {Inventory, 0x17, NoKey},
			{Tell, 0x14, NoKey}, {Use, 0x16, NoKey}, {PitchUp, 0x49, 0xc9}, {PitchDown, 0x51, 0xd1},
			{CenterView, 0x4c, 0xc7}, {ZoomIn, 0x47, 0xd2}, {ZoomOut, 0x4f, 0xd3}, {ToggleCam, 0x43, NoKey},
			{FullScreen, 0x44, NoKey}, {TargetMe, 0x3b, NoKey}, {Party1, 0x3c, NoKey}, {Party2, 0x3d, NoKey},
			{Party3, 0x3e, NoKey}, {Party4, 0x3f, NoKey}, {Party5, 0x40, NoKey}, {TargetPc, 0x41, NoKey},
			{TargetNpc, 0x42, NoKey}, {NetStat, 0x57, NoKey},
			{Hot1, 0x02, NoKey}, {Hot1 + 1, 0x03, NoKey}, {Hot1 + 2, 0x04, NoKey}, {Hot1 + 3, 0x05, NoKey},
			{Hot1 + 4, 0x06, NoKey}, {Hot1 + 5, 0x07, NoKey}, {Hot1 + 6, 0x08, NoKey}, {Hot1 + 7, 0x09, NoKey},
			{Hot1 + 8, 0x0a, NoKey}, {Hot1 + 9, 0x0b, NoKey},
			{HotPage1, Shift | 0x02, NoKey}, {HotPage1 + 1, Shift | 0x03, NoKey}, {HotPage1 + 2, Shift | 0x04, NoKey},
			{HotPage1 + 3, Shift | 0x05, NoKey}, {HotPage1 + 4, Shift | 0x06, NoKey}, {HotPage1 + 5, Shift | 0x07, NoKey},
			{HotPage1 + 6, Shift | 0x08, NoKey}, {HotPage1 + 7, Shift | 0x09, NoKey}, {HotPage1 + 8, Shift | 0x0a, NoKey},
			{HotPage1 + 9, Shift | 0x0b, NoKey},
			{Cast1, Alt | 0x02, NoKey}, {Cast1 + 1, Alt | 0x03, NoKey}, {Cast1 + 2, Alt | 0x04, NoKey},
			{Cast1 + 3, Alt | 0x05, NoKey}, {Cast1 + 4, Alt | 0x06, NoKey}, {Cast1 + 5, Alt | 0x07, NoKey},
			{Cast1 + 6, Alt | 0x08, NoKey}, {Cast1 + 7, Alt | 0x09, NoKey},
			{Reply, 0x13, NoKey}, {CycleReply, 0x0f, NoKey}, {CycleReplyBack, Shift | 0x0f, NoKey},
			{Backdrop, 0x29, NoKey}, {ToggleTarget, 0x0f, NoKey},
			{SpellBook, Ctrl | 0x30, NoKey}, {Abilities, Ctrl | 0x1e, NoKey}, {Combat, Ctrl | 0x2e, NoKey},
			{Socials, Ctrl | 0x18, NoKey}, {Main, Ctrl | 0x32, NoKey}, {Who, Ctrl | 0x11, NoKey},
			{InviteFollow, Ctrl | 0x17, NoKey}, {Disband, Ctrl | 0x20, NoKey}, {Camp, Ctrl | 0x2d, NoKey},
			{SitStand, Ctrl | 0x1f, NoKey}, {RunWalk, Ctrl | 0x13, NoKey},
			{ClipIn, Shift | 0x0c, NoKey}, {ClipOut, Shift | 0x0d, NoKey}, {VoiceOn, 0x4e, NoKey},
			{ScreenCap, 0x4a, NoKey},
			{HistoryUp, Shift | 0x48, Shift | 0xc8}, {HistoryDown, Shift | 0x50, Shift | 0xd0},
			{ChatLeft, Shift | 0x4b, Shift | 0xcb}, {ChatRight, Shift | 0x4d, Shift | 0xcd},
			{ChatBegin, Shift | 0x47, Shift | 0xc7}, {ChatEnd, Shift | 0x4f, Shift | 0xcf},
			{PageUp, Shift | 0x49, Shift | 0xc9}, {PageDown, Shift | 0x51, Shift | 0xd1},
			{CmdModeSay, 0x28, NoKey}, {CmdModeEmote, Shift | 0x27, NoKey},
			{LockWindows, Alt | 0x26, NoKey}, {TogglePlayerWin, Alt | 0x15, NoKey}, {TogglePartyWin, Alt | 0x19, NoKey},
			{ToggleTargetWin, Alt | 0x14, NoKey}, {ToggleSpellsWin, Alt | 0x1f, NoKey},
			{ToggleBuffWin, Alt | 0x30, NoKey}, {ToggleHotboxWin, Alt | 0x23, NoKey},
			{ToggleChatWin, Alt | 0x2e, NoKey}, {ToggleMainMenuWin, Alt | 0x32, NoKey},
			{ToggleAltAdvWin, 0x2f, NoKey}, {ToggleBazaarWin, Alt | 0x17, NoKey}, {RTarget, Ctrl | 0x14, NoKey},
			{ToggleFriendsWin, Alt | 0x21, NoKey}, {TogglePetInfoWin, Alt | 0x3b, NoKey},
			{ToggleOptionsWin, Alt | 0x18, NoKey}, {ToggleSelectorWin, Alt | 0x11, NoKey},
			{ToggleBazaarSearchWin, Alt | 0x1e, NoKey}, {ToggleCompassWin, Alt | 0x20, NoKey},
			{ToggleRaidWin, Alt | 0x13, NoKey}, {ToggleMusicPlayerWin, Alt | 0x25, NoKey},
			{ForwardCam, Alt | 0x48, Alt | 0xc8}, {BackCam, Alt | 0x50, Alt | 0xd0}, {RightCam, Alt | 0x4d, Alt | 0xcd},
			{LeftCam, Alt | 0x4b, Alt | 0xcb}, {PitchUpCam, Alt | 0x49, Alt | 0xc9},
			{PitchDownCam, Alt | 0x51, Alt | 0xd1},
		};

		// GetKeyCharacter's table (@006255e8): what a key's character is, for the ones it reads it
		// for; 0 for the rest.
		int KeyCharacter(int scan)
		{
			static const char Row1[] = "1234567890-="; // 0x02..0x0d
			static const char Row2[] = "qwertyuiop[]"; // 0x10..0x1b
			static const char Row3[] = "asdfghjkl;'`"; // 0x1e..0x29
			static const char Row4[] = "\\zxcvbnm,./"; // 0x2b..0x35
			if (scan >= 0x02 && scan <= 0x0d)
				return Row1[scan - 0x02];
			if (scan >= 0x10 && scan <= 0x1b)
				return Row2[scan - 0x10];
			if (scan >= 0x1e && scan <= 0x29)
				return Row3[scan - 0x1e];
			if (scan >= 0x2b && scan <= 0x35)
				return Row4[scan - 0x2b];
			switch (scan) {
			case 0x8d: return '=';
			case 0x91: return '@';
			case 0x92: return ':';
			case 0x93: return '_';
			case 0xb3: return ',';
			case 0xb5: return '/';
			default: return 0;
			}
		}

		struct KeyName
		{
			int scan;
			const char *shown; // GetKeypressDescription's
			const char *ini;   // _writeKeyMapToIni's and readKeyMapFromIni's (@00611598)
		};
		const KeyName KeyNames[] = {
			{0x0e, "BACKSPACE", "BACKSPACE"}, {0x0f, "TAB", "TAB"}, {0x1d, "LCTRL", "LCONTROL"},
			{0x29, "TILDE", "TILDE"}, {0x2a, "LSHIFT", "LSHIFT"}, {0x36, "RSHIFT", "RSHIFT"},
			{0x37, "NPAD *", "NUMPAD*"}, {0x38, "LALT", "LALT"}, {0x39, "SPACE", "SPACE"},
			{0x3a, "CAPSLOCK", "CAPSLOCK"}, {0x3b, "F1", "F1"}, {0x3c, "F2", "F2"}, {0x3d, "F3", "F3"},
			{0x3e, "F4", "F4"}, {0x3f, "F5", "F5"}, {0x40, "F6", "F6"}, {0x41, "F7", "F7"}, {0x42, "F8", "F8"},
			{0x43, "F9", "F9"}, {0x44, "F10", "F10"}, {0x45, "NUMLOCK", "NUMLOCK"}, {0x47, "NPAD 7", "NUMPAD7"},
			{0x48, "NPAD 8", "NUMPAD8"}, {0x49, "NPAD 9", "NUMPAD9"}, {0x4a, "NPAD -", "NUMPAD-"},
			{0x4b, "NPAD 4", "NUMPAD4"}, {0x4c, "NPAD 5", "NUMPAD5"}, {0x4d, "NPAD 6", "NUMPAD6"},
			{0x4e, "NPAD +", "NUMPAD+"}, {0x4f, "NPAD 1", "NUMPAD1"}, {0x50, "NPAD 2", "NUMPAD2"},
			{0x51, "NPAD 3", "NUMPAD3"}, {0x52, "NPAD 0", "NUMPAD0"}, {0x53, "NPAD .", "NUMPAD."},
			{0x57, "F11", "F11"}, {0x58, "F12", "F12"}, {0x9c, "NPADENTER", "NPADENTER"},
			{0x9d, "RCTRL", "RCONTROL"}, {0xb5, "NPAD /", "NUMPAD/"}, {0xb8, "RALT", "RALT"},
			{0xc5, "PAUSE", "PAUSE"}, {0xc7, "HOME", "HOME"}, {0xc8, "UP", "UP"}, {0xc9, "PGUP", "PGUP"},
			{0xcb, "LEFT", "LEFT"}, {0xcd, "RIGHT", "RIGHT"}, {0xcf, "END", "END"}, {0xd0, "DOWN", "DOWN"},
			{0xd1, "PGDOWN", "PGDOWN"}, {0xd2, "INSERT", "INSERT"}, {0xd3, "DELETE", "DELETE"},
			{0xdb, "LWIN", "LWIN"}, {0xdc, "RWIN", "RWIN"},
		};

		const KeyName *NameOf(int scan)
		{
			for (const KeyName &name : KeyNames)
				if (name.scan == scan)
					return &name;
			return nullptr;
		}

		// readKeyMapFromIni's single characters (@00611420): the lowercase keys only.
		int ScanOfChar(char ch)
		{
			for (int scan = 0x02; scan <= 0x35; ++scan)
				if (KeyCharacter(scan) == ch)
					return scan;
			return -1;
		}

		bool EqualNoCase(const std::string &a, const char *b)
		{
			size_t n = std::strlen(b);
			if (a.size() != n)
				return false;
			for (size_t i = 0; i < n; ++i)
				if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i])))
					return false;
			return true;
		}

		bool StartsNoCase(const std::string &s, const char *prefix)
		{
			size_t n = std::strlen(prefix);
			return s.size() >= n && EqualNoCase(s.substr(0, n), prefix);
		}

		std::string IniKey(int id, bool alternate)
		{
			return std::string("KEYMAP_") + BindName(id) + (alternate ? "_2" : "");
		}

		constexpr uint32_t KeepDefault = 0xfffffffe; // readKeyMapFromIni's *NONE*
		const char *const Section = "KeyMaps";

		uint32_t ReadKey(const IniFile &ini, int id, bool alternate)
		{
			std::string key = IniKey(id, alternate);
			if (!ini.Has(Section, key))
				return KeepDefault;
			std::string value = ini.Get(Section, key);
			uint32_t mods = 0;
			// Up to three of SHIFT+, CONTROL+ and ALT+ in front, in any order.
			for (int i = 0; i < 3; ++i) {
				if (StartsNoCase(value, "SHIFT+")) {
					mods |= Shift;
					value.erase(0, 6);
				}
				else if (StartsNoCase(value, "CONTROL+")) {
					mods |= Ctrl;
					value.erase(0, 8);
				}
				else if (StartsNoCase(value, "ALT+")) {
					mods |= Alt;
					value.erase(0, 4);
				}
			}
			if (value.size() == 1) {
				int scan = ScanOfChar(value[0]);
				return scan < 0 ? NoKey : mods + static_cast<uint32_t>(scan);
			}
			if (EqualNoCase(value, "NOKEY"))
				return NoKey;
			for (const KeyName &name : KeyNames)
				if (EqualNoCase(value, name.ini))
					return mods + static_cast<uint32_t>(name.scan);
			return NoKey;
		}
	}

	uint32_t CategoryMask(int choice)
	{
		return choice >= 0 && choice < 8 ? 1u << choice : 0xff;
	}

	const std::vector<Command> &List()
	{
		return CommandTable;
	}

	const Command *Find(int id)
	{
		for (const Command &command : CommandTable)
			if (command.id == id)
				return &command;
		return nullptr;
	}

	const char *BindName(int id)
	{
		return id >= 0 && id < static_cast<int>(std::size(BindNames)) ? BindNames[id] : "";
	}

	uint32_t DefaultKey(int id, bool alternate)
	{
		for (const DefaultBinding &binding : Defaults)
			if (binding.id == id)
				return alternate ? binding.alternate : binding.key;
		return NoKey;
	}

	bool IsValidKey(int scan)
	{
		return scan != 0x01 && scan != 0x1c && scan != 0x35 && scan != 0xdb && scan != 0xdc;
	}

	bool IsModifierKey(int scan)
	{
		return scan == 0x1d || scan == 0x2a || scan == 0x36 || scan == 0x38 || scan == 0x9d || scan == 0xb8;
	}

	int TypedChar(int scan)
	{
		// ConvertScanCodeToChar reads the same keys as GetKeyCharacter, Backspace and Space too, from
		// the table at @006253e8; the ones it types are those isCharacterAllowed takes.
		if (scan == 0x39)
			return ' ';
		int ch = KeyCharacter(scan);
		return ch >= 0x20 && ch < 0x7f ? ch : 0;
	}

	std::string Describe(uint32_t key)
	{
		if (key == NoKey)
			return "-------";
		int scan = static_cast<int>(key & 0xff);
		if (scan == 0)
			return "";
		bool shift = (key & Shift) != 0, ctrl = (key & Ctrl) != 0, alt = (key & Alt) != 0;
		std::string out = shift && ctrl && alt ? "ShCtlAlt "
		                  : shift && ctrl      ? "ShCtl "
		                  : shift && alt       ? "ShAlt "
		                  : ctrl && alt        ? "CtlAlt "
		                  : shift              ? "Shift "
		                  : ctrl               ? "Ctrl "
		                  : alt                ? "Alt "
		                                       : "";
		if (int ch = KeyCharacter(scan))
			out += static_cast<char>(std::toupper(ch));
		else if (const KeyName *name = NameOf(scan))
			out += name->shown;
		return out;
	}
}

using namespace KeyMap;

void KeyBindings::Defaults()
{
	for (int alt = 0; alt < 2; ++alt)
		for (int id = 0; id < Count; ++id)
			m_keys[alt][id] = DefaultKey(id, alt != 0);
}

uint32_t KeyBindings::Get(int id, bool alternate) const
{
	return id >= 0 && id < Count ? m_keys[alternate ? 1 : 0][id] : NoKey;
}

void KeyBindings::Set(int id, bool alternate, uint32_t key)
{
	if (id >= 0 && id < Count)
		m_keys[alternate ? 1 : 0][id] = key;
}

void KeyBindings::Load(const IniFile &ini)
{
	for (int id = 1; id < static_cast<int>(std::size(BindNames)); ++id)
		for (int alt = 0; alt < 2; ++alt) {
			uint32_t key = ReadKey(ini, id, alt != 0);
			if (key != KeepDefault)
				m_keys[alt][id] = key;
		}
}

void KeyBindings::Store(IniFile &ini, int id, bool alternate, uint32_t key)
{
	std::string value;
	if (key == NoKey)
		value = "NOKEY";
	else {
		if (key & Shift)
			value += "SHIFT+";
		if (key & Ctrl)
			value += "CONTROL+";
		if (key & Alt)
			value += "ALT+";
		int scan = static_cast<int>(key & 0xff);
		int ch = KeyCharacter(scan);
		if (ch != 0 && scan != 0x39)
			value += static_cast<char>(ch);
		else if (const KeyName *name = NameOf(scan))
			value += name->ini;
	}
	ini.Set(Section, IniKey(id, alternate), value);
}

void KeyBindings::Remove(IniFile &ini, int id)
{
	ini.Remove(Section, IniKey(id, false));
	ini.Remove(Section, IniKey(id, true));
}

std::vector<int> KeyBindings::Down(uint32_t key) const
{
	std::vector<int> run;
	auto add = [&](int id) {
		if (std::find(run.begin(), run.end(), id) == run.end())
			run.push_back(id);
	};
	auto bound = [&](int id, uint32_t k) { return k == m_keys[0][id] || k == m_keys[1][id]; };
	uint32_t no_ctrl = key & 0xd00000ff;
	for (int id : {Right, Left, Back, Forward})
		if (bound(id, no_ctrl))
			add(id);
	uint32_t no_alt = key & 0xb00000ff;
	for (int id : {ZoomIn, ZoomOut})
		if (bound(id, no_alt))
			add(id);
	// The chat line's cursor keys take the key from the rest (DAT_007ce190, DAT_007ce194).
	if (bound(ChatLeft, key) || bound(ChatRight, key))
		return run;
	for (int id = 0; id <= 0x82; ++id)
		if (bound(id, key))
			add(id);
	return run;
}

std::vector<int> KeyBindings::Up(uint32_t key) const
{
	std::vector<int> stop;
	auto add = [&](int id) {
		if (std::find(stop.begin(), stop.end(), id) == stop.end())
			stop.push_back(id);
	};
	uint8_t scan = static_cast<uint8_t>(key);
	for (int id : {Right, Left, Back, Forward, ZoomIn, ZoomOut, BackCam, RightCam, LeftCam, ForwardCam, PitchUpCam, PitchDownCam})
		if (static_cast<uint8_t>(m_keys[0][id]) == scan || static_cast<uint8_t>(m_keys[1][id]) == scan)
			add(id);
	for (int id = 0; id < 0x83; ++id)
		if (key == m_keys[0][id] || key == m_keys[1][id])
			add(id);
	return stop;
}
