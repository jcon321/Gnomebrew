#pragma once

// eqgame's CChatManager (@004100e2..@00413439): up to 32 chat windows (CChatWindow, chat_window.h),
// all on the "ChatWindow" SIDL screen.
//   - Window 0 is the main one ("Main Chat", eqstr 6623; its layout in the UI ini's [MainChat]),
//     with no close or minimize box. CreateChatWindow (@00410c5a, @00410e84) puts another in the
//     first free slot after it, or a new one past the last (none past 32: eqstr 6648 into the main
//     one), titled "Chat <n>" (eqstr 6624) and laid out from [Chat<n>] (the number padded to two
//     characters with spaces: "Chat 1", "Chat10"), its language the main one's.
//   - Closing one but the main one frees it (CChatWindow's Deactivate @00413b17:
//     UpdateChannelReferences, the active window back to the main one, FreeChatWindow).
//   - The channel map (+0x90): which window each of 41 kinds of line goes to
//     (GetChatWindowIndexFromChannelID @00411425 turns a line's color type into its kind;
//     ProcessChatMessage @0041115f drops one whose window is gone). They all start on the main one.
//   - The active window (+0x84, SetActiveChatChannel: the one whose input last had the focus) and
//     the locked one (+0x88, Always Chat Here; -1 none): GetActiveChatWindow (@0041114a) is the
//     locked one if there is one, else the active one. Enter opens its input, and /channel,
//     /language and /chatfontsize change it.
//   - Each window's default channel for what's typed into it without a command (+0x140), chat
//     channel number (+0x144), tell target (+0x148) and language (+0x188, an index into the
//     languages we know, as /language numbers them).
// The UI ini's [ChatManager] keeps them (CChatManager::Deactivate @00410871, read back by the
// constructor): NumWindows (the windows past the main one), LockedActiveWindow, for each window past
// the main one ChatWindow<n>_Language ("@42" for a freed one, recreated and freed again so the rest
// keep their numbers), _DefaultChannel, _ChatChannel and _TellTarget; for every window _FontStyle and
// _Name (its title); ChannelMap0..40 (the window's number); HitMode0..7. A key that's missing keeps
// what's there (the main window's, for a new one's channel and language).

#include "chat_window.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class IniFile;
class WindowManager;

class ChatManager
{
public:
	ChatManager();

	static constexpr int MaxWindows = 32;
	static constexpr int ChannelMaps = 41;
	static constexpr int HitModes = 8;
	static constexpr const char *Screen = "ChatWindow"; // the SIDL screen they're all on
	static constexpr const char *MainScreen = "MainChat";

	struct Window
	{
		std::unique_ptr<ChatWindow> chat; // null: freed
		std::string screen;               // its name in the WindowManager, and its ini section
		int channel = 8;                  // 0 guild, 2 group, 3 shout, 4 auction, 5 OOC, 7 tell, 8 say, 15 raid, 19 chat
		int chat_channel = 0;
		std::string tell_target;
		int language = 0;
	};
	// What CChatManager's constructor reads before the ini: SetDefaultGameValues' DefaultChannel,
	// DefaultChannelTell and DefaultLanguage (the main window's).
	struct Defaults
	{
		int channel = 8;
		std::string tell_target;
		int language = 0;
	};

	// The windows from the UI ini's [ChatManager] (null: none), each with its layout. `text` is
	// the eqstr lookup (id, fallback); `link_rgb` the links' color.
	// `color` a color type's color (ChatColors).
	void Build(WindowManager &windows, const IniFile *layout, const Defaults &defaults,
		std::function<std::string(uint32_t, const char *)> text, std::function<uint32_t(int)> color,
		uint32_t link_rgb);
	void SetMenu(int menu); // the menu a right click on any of them opens (CChatManager::InitContextMenu's)
	void Detach(); // the windows gone (the window manager reset); the lines kept
	void ShowAll(); // Activate: every window shown (their layouts don't keep Show)
	void Store(IniFile &ini) const;

	int Count() const { return static_cast<int>(m_windows.size()); } // MaxChatWindows: the slots
	Window *At(int index);
	const Window *At(int index) const;
	int IndexOf(const std::string &screen) const; // -1 none
	ChatWindow &Main() { return *m_windows[0].chat; }
	const ChatWindow &Main() const { return *m_windows[0].chat; }
	ChatWindow *Typing(); // the window whose input has the focus, or null
	int TypingIndex() const;

	int ActiveIndex() const; // GetActiveChatWindow
	Window &Active() { return *At(ActiveIndex()); }
	void SetActive(int index); // SetActiveChatChannel
	int Locked() const { return m_locked; }
	void SetLocked(int index) { m_locked = index; } // SetLockedActiveChatWindow (-1: none)

	// CreateChatWindow: the new window's number, or -1 when there's no room (and it said so). Its
	// layout from `layout`'s [Chat<n>] (EnableIniStorage), if it has one.
	int Create(const IniFile *layout, const std::string &name = "");
	void SetTitle(int index, const std::string &title); // SetWindowText (Rename Window)
	std::string Title(int index) const;
	// The window's close: freed (the main one isn't).
	void Free(int index);

	// GetChatWindowIndexFromChannelID: the kind of line (0..40) a color type is.
	static int ChannelIndex(int type);
	int ChannelMap(int kind) const; // the window, -1 none
	void SetChannelMap(int kind, int index);
	void ClearChannelMap(int kind) { SetChannelMap(kind, 0); }
	// ProcessChatMessage: the line into the window its kind goes to.
	void Add(const std::string &text, uint32_t rgb, int type);

	int HitMode(int which) const { return m_hit_modes[which]; }
	void SetHitMode(int which, int mode) { m_hit_modes[which] = mode; }

	void Layout(TextRenderer &text);
	void SetLinkColor(uint32_t rgb);

private:
	void Attach(int index);
	static std::string ScreenName(int index);

	WindowManager *m_manager = nullptr;
	std::function<std::string(uint32_t, const char *)> m_text;
	std::function<uint32_t(int)> m_color;
	int m_menu = -1;
	uint32_t m_link_rgb = 0xff00ff;
	std::vector<Window> m_windows; // the main one always (its lines live on across Build)
	int m_active = 0;
	int m_locked = -1;
	std::array<int, ChannelMaps> m_map{};
	std::array<int, HitModes> m_hit_modes{};
};
