#pragma once

// eqgame's friends and ignore lists, and the friends window that shows them:
//   - `__FriendsList` and `__IgnoreList`, 30 names each, kept in the character's ini (<char>_<server>.ini)
//     as [Friends] Friend0..29 and [Ignored] Ignored0..29, "*NULL*" for an empty one
//     (CEverQuest_ProcessLocalPCIni @0053dd81: a value starting with '*' is empty, 31 characters at most
//     read back); saved each time either list changes;
//   - /friends (@004f5a37): nothing, the list (12979, a line of dashes, the names, then 12977 and 12978
//     for none or 12980 with the count); a name over 15 characters, 12981 in red; a name on the list
//     (any case) comes off it (12982); else it goes in the last empty slot (12984), or 12983 with 30
//     when there's none;
//   - /ignore (@004fc7d6): nothing, 13240 in red and the names in color 15; a name on the list comes
//     off it (13241); else, no empty slot, 13244 with 30; a GM in the zone (EQPlayer_get_player,
//     IsGameMaster), 13242; else into the last empty slot (13243); all in color 15;
//   - CEverQuest_BeingIgnored (@0053767e): the name on the ignore list, any case. msg_new_text leaves
//     out a channel message from it (but channels 6 and 14), unless it's a GM in the zone, who comes
//     off the list (not saved); OP_Emote, OP_RandomReply and msg_consent_verify leave theirs out;
//   - CFriendsWnd (@0041c285): the FW_FriendsPage (NameInput, AddButton, ContactButton, WhoButton,
//     DeleteButton, FriendsList) and FW_IgnorePage (IgnoreNameInput, IgnoreAddButton,
//     IgnoreDeleteButton, IgnoreList) tabs. Activate (shown) and a tab clicked fill the lists
//     (UpdateFriendsList, UpdateIgnoreList: no row selected, Delete off); typing in a name box turns
//     its Add on or off, and Add or Enter there runs /friends or /ignore with it and empties it
//     (AddFriend, AddIgnore: both then set the friends' Add by the box just emptied); a row selected
//     turns its Delete on, which runs /friends or /ignore with that name; Who does "/who ALL <name>"
//     (or "ALL friend" with none selected) and Contact puts "/tell <name> " in the chat line.
//   - CEverQuest::Who's "/who all friend": the friends of 15 characters or fewer, comma separated, as
//     OP_FriendsWho (12307 and dashes), or 12308 with none.

#include "game_windows.h"
#include "settings_file.h"
#include "zone_session.h"

#include <array>
#include <functional>
#include <string>
#include <vector>

class WindowManager;

class Friends
{
public:
	static constexpr int Slots = 30;
	static constexpr const char *Screen = "FriendsWindow";

	struct Host
	{
		// eqstr_en.txt's string `id` with its %1.. filled in, or `fallback`.
		std::function<std::string(uint32_t, const std::vector<std::string> &, const std::string &)> text;
		std::function<void(const std::string &, int)> chat; // print_chat_dsp_chat, in a color type
		std::function<const ZoneSpawn *(const std::string &)> player; // EQPlayer_get_player: a player here
		std::function<void()> save;                                    // CEverQuest_ProcessLocalPCIni(0)
		std::function<void(const std::string &)> who;                  // CEverQuest::Who
		std::function<void(const std::string &)> contact;              // the chat line opened with this
	};

	void Attach(WindowManager &windows, Host host);
	void Load(const IniFile &ini);  // ProcessLocalPCIni(1)
	void Store(IniFile &ini) const; // ProcessLocalPCIni(0)

	void FriendsCommand(const std::string &name); // /friends
	void IgnoreCommand(const std::string &name);  // /ignore
	bool BeingIgnored(const std::string &name) const;
	// msg_new_text's test of a channel message's sender: true to leave it out. An ignored GM in the
	// zone comes off the list instead (unsaved) and is heard.
	bool ChatIgnored(const std::string &sender, int channel);
	// CEverQuest::Who's list for OP_FriendsWho: the names of 15 characters or fewer, ',' between them.
	std::string FriendsWho() const;

	// The window: shown since the last frame (Activate), the name boxes' text changing.
	void Frame();
	bool Click(const std::string &window, const std::string &control);
	// Enter typed in one of its name boxes (WndNotification 6): Add. False when neither has the focus.
	bool Enter();

	const std::array<std::string, Slots> &FriendsList() const { return m_friends; }
	const std::array<std::string, Slots> &IgnoreList() const { return m_ignored; }

private:
	std::string Text(uint32_t id, const std::vector<std::string> &args, const std::string &fallback) const;
	void Say(const std::string &text, int type) const;
	void UpdateFriendsList();
	void UpdateIgnoreList();
	void AddFriend();
	void AddIgnore();
	std::string Selected(const char *list) const; // the selected row's name, or ""
	void Enable(const char *control, bool on);
	std::string InputText(const char *control) const;

	WindowManager *m_windows = nullptr;
	Host m_host;
	std::array<std::string, Slots> m_friends, m_ignored;
	bool m_shown = false;
	std::string m_name_text, m_ignore_text; // the boxes' text as last seen
};
