// FrontEnd's side of the chat windows (chat_manager.h): building them, their right-click menu
// (CChatManager::InitContextMenu @0041179c, UpdateContextMenus @00412f9b, UpdateTellMenus
// @00413439) and its picks (CChatWindow::WndNotification @00413be9's 0x13), the rename's text entry
// (its 0x1f), sending what's typed into one (its Enter), and closing one.

#include "frontend.h"

#include "ini_file.h"
#include "item_tags.h"

#include <cstdlib>

namespace
{
	// The menus' item ids (InitContextMenu's).
	constexpr int NewWindowId = 0x2a, AlwaysChatHereId = 0x2b, RenameId = 0x2c;
	constexpr int FilterFirst = 100, FilterLast = 0x8c;     // the channel map's kind + 100
	constexpr int ChannelFirst = 200, ChannelLast = 0xd9;   // a default channel
	constexpr int LanguageFirst = 300, LanguageLast = 0x14b; // the known language's place in the list + 300
	constexpr int ReplyFirst = 500, RaidFirst = 0x226, FriendFirst = 0x28a, TellLast = 0x2ed;
	constexpr int MessageColor = 10; // AddOutputText's color for what the picks say
}

void FrontEnd::SetChatDefaults()
{
	// SetDefaultGameValues (@0053b28f): DefaultChannel (8, say, when there's none), with 7 (a tell)
	// DefaultChannelTell (none: 8 again), and DefaultLanguage (0); ours in my-client.ini's [Defaults].
	m_chat_defaults = {};
	IniFile ini;
	if (m_config.settings_ini.empty() || !ini.Load(m_config.settings_ini))
		return;
	m_chat_defaults.channel = ini.GetInt("Defaults", "DefaultChannel", 8) & 0xff;
	if (m_chat_defaults.channel == 7) {
		m_chat_defaults.tell_target = ini.Get("Defaults", "DefaultChannelTell", "").substr(0, 63);
		if (m_chat_defaults.tell_target.empty())
			m_chat_defaults.channel = 8;
	}
	m_chat_defaults.language = ini.GetInt("Defaults", "DefaultLanguage", 0);
}

void FrontEnd::BuildChats(const IniFile *layout)
{
	SetChatDefaults();
	m_chats.Build(m_windows, layout, m_chat_defaults,
		[this](uint32_t id, const char *fallback) { return EqString(id, fallback); },
		[this](int type) { return m_chat_colors.Get(type); }, m_chat_colors.Get(ItemTags::ColorType));
	BuildChatMenus();
}

void FrontEnd::BuildChatMenus()
{
	// The menus as places for UpdateChatMenus to fill as one opens; the root the windows' own.
	m_chat_menus = {};
	auto add = [this] { return m_windows.AddMenu({}); };
	m_chat_menus.filters = add();
	m_chat_menus.melee = add();
	m_chat_menus.spells = add();
	m_chat_menus.chat_filter = add();
	m_chat_menus.channel = add();
	m_chat_menus.chat_channel = add();
	m_chat_menus.language = add();
	m_chat_menus.reply = add();
	m_chat_menus.friends = add();
	m_chat_menus.raid = add();
	m_chat_menus.root = add();
	m_windows.menu_opening = [this](const std::string &screen) { UpdateChatMenus(screen); };
	m_chats.SetMenu(m_chat_menus.root);
}

void FrontEnd::UpdateChatMenus(const std::string &screen)
{
	int index = m_chats.IndexOf(screen);
	const ChatManager::Window *window = m_chats.At(index);
	if (!window || m_chat_menus.root < 0)
		return;
	using Item = WindowManager::MenuItem;
	auto item = [](const std::string &text, int id, bool checked = false) {
		Item i;
		i.text = text;
		i.id = id;
		i.checked = checked;
		return i;
	};
	auto submenu = [](const std::string &text, int menu, bool enabled = true) {
		Item i;
		i.text = text;
		i.submenu = menu;
		i.enabled = enabled;
		return i;
	};
	Item separator;
	separator.separator = true;
	// A filter is checked when its kind of line goes to this window.
	auto filter = [&](uint32_t text_id, const char *fallback, int id) {
		return item(EqString(text_id, fallback), id, m_chats.ChannelMap(id - FilterFirst) == index);
	};
	// The chat channels (+0x70.., +0xcf..) come from the chat server, which we don't have: their
	// items are disabled (UpdateContextMenus without a ChatService).
	m_windows.SetMenu(m_chat_menus.melee, {
		filter(15014, "Your Hits", 0x6d), filter(15015, "Your Misses", 0x7b), filter(15016, "You Being Hit", 0x7c),
		filter(15017, "You Being Missed", 0x7d), filter(15018, "Others Hits", 0x7e), filter(15019, "Others MIsses", 0x7f),
		filter(15020, "Critical Hits", 0x82), filter(15021, "Disciplines", 0x83), filter(15022, "Too Far Away", 0x84),
		filter(15023, "NPC Rampage", 0x85), filter(15024, "NPC Flurry", 0x86), filter(15025, "NPC Enrage", 0x87),
		filter(15026, "My Death", 0x80), filter(15027, "Others Death", 0x81)});
	m_windows.SetMenu(m_chat_menus.spells, {
		filter(15028, "Mine", 0x6e), filter(15029, "Others", 0x88), filter(15030, "Failures", 0x89),
		filter(15031, "Criticals", 0x8a), filter(15032, "Worn Off", 0x8b), filter(15033, "Non Melee Hits", 0x8c)});
	m_windows.SetMenu(m_chat_menus.filters, {
		filter(6630, "Say", 0x64), filter(6631, "Tell", 0x65), filter(6632, "Group", 0x66), filter(5113, "Raid", 0x67),
		filter(6633, "Guild", 0x68), filter(6634, "OOC", 0x69), filter(6635, "Auction", 0x6a), filter(6636, "Shout", 0x6b),
		filter(6637, "Emotes", 0x6c), submenu(EqString(6638, "Chat Channel"), m_chat_menus.chat_filter, false),
		submenu(EqString(6639, "Melee "), m_chat_menus.melee), submenu(EqString(6640, "Spells"), m_chat_menus.spells),
		filter(6641, "Skills", 0x6f), filter(6642, "Other", 0x7a)});
	// The default channel's item checked.
	auto channel = [&](uint32_t text_id, const char *fallback, int id, int value) {
		return item(EqString(text_id, fallback), id, window->channel == value);
	};
	m_windows.SetMenu(m_chat_menus.channel, {
		channel(6630, "Say", 0xc8, 8), channel(6631, "Tell", 0xc9, 7), channel(6632, "Group", 0xca, 2),
		channel(5113, "Raid", 0xd9, 15), channel(6633, "Guild", 0xcb, 0), channel(6634, "OOC", 0xcc, 5),
		channel(6635, "Auction", 0xcd, 4), submenu(EqString(6638, "Chat Channel"), m_chat_menus.chat_channel, false)});
	// The languages we know (skill not 0), numbered by their place in that list, as /language
	// numbers them; this window's checked.
	std::vector<Item> languages;
	for (int language = 0; language < 32; ++language) {
		if (m_zone.Profile().languages[language] == 0)
			continue;
		int place = static_cast<int>(languages.size());
		uint32_t id = LanguageNameId(language);
		languages.push_back(item(id ? EqString(id, "") : std::string(), LanguageFirst + place, window->language == place));
	}
	m_windows.SetMenu(m_chat_menus.language, languages);
	// UpdateTellMenus: who sent us tells (__LastTell), the friends list, the raid; each disabled
	// when it's empty.
	std::vector<Item> reply, friends, raid;
	for (size_t i = 0; i < m_tell_list.size() && i < 50; ++i)
		reply.push_back(item(m_tell_list[i], ReplyFirst + static_cast<int>(i)));
	const auto &friend_names = m_friends.FriendsList();
	for (size_t i = 0; i < friend_names.size(); ++i)
		if (!friend_names[i].empty())
			friends.push_back(item(friend_names[i], FriendFirst + static_cast<int>(i)));
	for (int i = 0; i < Raid::MaxMembers; ++i)
		if (m_raid.Used(i))
			raid.push_back(item(m_raid.MemberAt(i).name, RaidFirst + i));
	bool any_reply = !reply.empty(), any_friend = !friends.empty(), any_raid = !raid.empty();
	m_windows.SetMenu(m_chat_menus.reply, std::move(reply));
	m_windows.SetMenu(m_chat_menus.friends, std::move(friends));
	m_windows.SetMenu(m_chat_menus.raid, std::move(raid));

	m_windows.SetMenu(m_chat_menus.root, {
		submenu(EqString(6643, "Window"), WindowManager::WindowMenu), separator,
		item(EqString(6644, "New Chat Window"), NewWindowId), item(EqString(3140, "Rename Window"), RenameId),
		item(EqString(15009, "Always Chat Here"), AlwaysChatHereId, m_chats.Locked() == index), separator,
		submenu(EqString(3147, "Reply to..."), m_chat_menus.reply, any_reply),
		submenu(EqString(3148, "Tell a friend..."), m_chat_menus.friends, any_friend),
		submenu(EqString(3149, "Tell a raidmember..."), m_chat_menus.raid, any_raid), separator,
		submenu(EqString(6645, "Filters"), m_chat_menus.filters), submenu(EqString(6646, "Channel"), m_chat_menus.channel),
		submenu(EqString(6647, "Language"), m_chat_menus.language)});
}

void FrontEnd::ChatMenuPick(const WindowManager::MenuPick &pick)
{
	int index = m_chats.IndexOf(pick.window);
	ChatManager::Window *window = m_chats.At(index);
	if (!window)
		return;
	const int id = pick.id;
	auto say = [&](const std::string &text) { window->chat->Add(text, m_chat_colors.Get(MessageColor)); };
	if (id == NewWindowId) {
		// CreateChatWindow, its layout from the UI ini if it has one.
		IniFile ini;
		bool have = !m_ui_layout_path.empty() && ini.Load(m_ui_layout_path);
		m_chats.Create(have ? &ini : nullptr);
		SaveUiLayout();
	}
	else if (id == RenameId) {
		// CTextEntryWnd::Activate: eqstr 3141, the title to start with, 1 to 15 characters.
		m_text_entry.Activate(pick.window, RenameId, EqString(3141, "Enter the new name of the window:"),
			m_chats.Title(index), 1, 15);
	}
	else if (id == AlwaysChatHereId) {
		// Locked here, or unlocked when it already is.
		if (m_chats.Locked() == index) {
			m_chats.SetLocked(-1);
			say(EqString(15010, "Your chat input will go to the current active chat window."));
		}
		else {
			m_chats.SetLocked(index);
			say(EqString(15011, "Your chat input will always go to this chat window."));
		}
		SaveUiLayout();
	}
	else if (id >= LanguageFirst && id <= LanguageLast) {
		// The window's language; the main window's is DefaultLanguage too (eqgame's eqclient.ini).
		window->language = id - LanguageFirst;
		if (index == 0 && !m_config.settings_ini.empty()) {
			IniFile ini;
			ini.Load(m_config.settings_ini);
			ini.SetInt("Defaults", "DefaultLanguage", window->language);
			if (!ini.Save(m_config.settings_ini))
				Log("chat: couldn't write " + m_config.settings_ini);
		}
		say(FormatEqString(m_ui.eqstr, 3133, "Language set to %1", {pick.text}));
		SaveUiLayout();
	}
	else if (id >= FilterFirst && id <= FilterLast) {
		// The kind of line to this window, or back to the main one when it's here already.
		int kind = id - FilterFirst;
		std::string text = pick.text + " ";
		if (m_chats.ChannelMap(kind) == index) {
			m_chats.ClearChannelMap(kind);
			text += EqString(3136, "will go to the main window.");
		}
		else {
			m_chats.SetChannelMap(kind, index);
			text += EqString(3135, "will go to this window.");
		}
		say(text);
		SaveUiLayout();
	}
	else if (id >= ChannelFirst && id <= ChannelLast) {
		// The default channel: Tell's is a tell to "a" (eqgame's +0x148 set to "a").
		switch (id) {
		case 0xc8: window->channel = 8; break;
		case 0xc9: window->channel = 7; window->tell_target = "a"; break;
		case 0xca: window->channel = 2; break;
		case 0xcb: window->channel = 0; break;
		case 0xcc: window->channel = 5; break;
		case 0xcd: window->channel = 4; break;
		case 0xce: window->channel = 3; break;
		case 0xd9: window->channel = 15; break;
		default: window->channel = 19; window->chat_channel = id - 0xcf; break; // 0xcf..0xd8
		}
		say(pick.text + EqString(3138, " is now the default channel for this window."));
		SaveUiLayout();
	}
	else if (id >= ReplyFirst && id <= TellLast) {
		// "/tell <name> " (eqstr 13974) in the active window's input.
		OpenChat(EqString(13974, "/tell") + " " + pick.text + " ");
	}
}

void FrontEnd::OpenChat(const std::string &text)
{
	int active = m_chats.ActiveIndex();
	ChatManager::Window *window = m_chats.At(active);
	if (!window)
		return;
	m_windows.Show(window->screen, true);
	for (int i = 0; i < m_chats.Count(); ++i)
		if (ChatManager::Window *other = m_chats.At(i); other && i != active)
			other->chat->Close();
	window->chat->Open(text);
}

void FrontEnd::SendChatLine(int index, const std::string &line)
{
	// CChatWindow::WndNotification's Enter: the window's default channel, tell target and
	// language are DAT_00798598, DAT_0079859c and DAT_0079859a while InterpretCmd runs, then
	// they're back as they were (the main window's).
	const ChatManager::Window *window = m_chats.At(index);
	if (!window)
		return;
	m_chat_sender = index;
	m_zone.SetChatLanguage(window->language);
	InterpretCommand(line);
	m_chat_sender = 0;
	if (const ChatManager::Window *main = m_chats.At(0))
		m_zone.SetChatLanguage(main->language);
}

void FrontEnd::CheckChatWindows()
{
	// The rename: CChatWindow's 0x1f, the title set (SetWindowText) on OK.
	m_text_entry.Update();
	for (const TextEntry::Result &result : m_text_entry.TakeResults()) {
		int index = m_chats.IndexOf(result.owner);
		if (result.id == RenameId && result.ok && index >= 0) {
			m_chats.SetTitle(index, result.text);
			SaveUiLayout();
		}
	}
	// A chat window closed (its box, or the window menu's Close) is freed (CChatWindow's
	// Deactivate); the main one can't be (no close box).
	bool freed = false;
	for (int i = 1; i < m_chats.Count(); ++i)
		if (const ChatManager::Window *window = m_chats.At(i); window && !m_windows.Shown(window->screen)) {
			m_chats.Free(i);
			freed = true;
		}
	if (freed)
		SaveUiLayout();
	// What anything else interprets (hot buttons, socials) goes by the main window's language
	// (EQ_Character::DoPassageOfTime puts it in DAT_0079859a).
	if (const ChatManager::Window *main = m_chats.At(0))
		m_zone.SetChatLanguage(main->language);
}
