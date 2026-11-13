#include "friends.h"

#include "ini_file.h"
#include "window_manager.h"

#include <cctype>

namespace
{
	constexpr int Red = 13, Yellow = 15, Default = 0x111;

	bool Same(const std::string &a, const std::string &b) // stricmp
	{
		if (a.size() != b.size())
			return false;
		for (size_t i = 0; i < a.size(); ++i)
			if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
				return false;
		return true;
	}
}

void Friends::Attach(WindowManager &windows, Host host)
{
	m_windows = &windows;
	m_host = std::move(host);
	m_shown = false;
	m_name_text.clear();
	m_ignore_text.clear();
	// CFriendsWnd::CFriendsWnd: Add, Delete and the ignore page's two off to start with.
	for (const char *id : {"AddButton", "DeleteButton", "IgnoreAddButton", "IgnoreDeleteButton"})
		Enable(id, false);
}

void Friends::Load(const IniFile &ini)
{
	// ProcessLocalPCIni(1): a value starting with '*' (*NULL*, or none) is an empty slot; the rest
	// is read back 31 characters at most.
	auto load = [&](std::array<std::string, Slots> &list, const char *section, const char *key) {
		for (int i = 0; i < Slots; ++i) {
			std::string value = ini.Get(section, key + std::to_string(i), "*NULL*");
			list[i] = value.empty() || value[0] == '*' ? "" : value.substr(0, 31);
		}
	};
	load(m_friends, "Friends", "Friend");
	load(m_ignored, "Ignored", "Ignored");
}

void Friends::Store(IniFile &ini) const
{
	for (int i = 0; i < Slots; ++i) {
		ini.Set("Friends", "Friend" + std::to_string(i), m_friends[i].empty() ? "*NULL*" : m_friends[i]);
		ini.Set("Ignored", "Ignored" + std::to_string(i), m_ignored[i].empty() ? "*NULL*" : m_ignored[i]);
	}
}

std::string Friends::Text(uint32_t id, const std::vector<std::string> &args, const std::string &fallback) const
{
	return m_host.text ? m_host.text(id, args, fallback) : fallback;
}

void Friends::Say(const std::string &text, int type) const
{
	if (m_host.chat)
		m_host.chat(text, type);
}

void Friends::FriendsCommand(const std::string &name)
{
	if (name.empty()) {
		Say(Text(12979, {}, "List of Friends"), Default);
		Say("-----------------", Default);
		int count = 0;
		for (const std::string &friend_name : m_friends)
			if (!friend_name.empty()) {
				++count;
				Say(friend_name, Default);
			}
		if (count == 0) {
			Say(Text(12977, {}, "You have no friends! Awww, how sad..."), Default);
			Say(Text(12978, {}, "Maybe you should add one by typing: /friend <name>"), Default);
		}
		else
			Say(Text(12980, {std::to_string(count)}, "You have %1 friend(s)."), Default);
	}
	else {
		if (name.size() > 15) {
			Say(Text(12981, {}, "That is too long to be the name of a friend!"), Red);
			return;
		}
		// One on the list (any case) comes off it; else the last empty slot takes it.
		int empty = -1;
		bool removed = false;
		for (int i = 0; i < Slots && !removed; ++i) {
			if (!m_friends[i].empty() && Same(m_friends[i], name)) {
				Say(Text(12982, {name}, "%1 is no longer your friend."), Default);
				m_friends[i].clear();
				removed = true;
			}
			else if (m_friends[i].empty())
				empty = i;
		}
		if (!removed) {
			if (empty < 0) {
				Say(Text(12983, {std::to_string(Slots)},
				        "You may only have %1 friends in your list at once, please delete one before adding another."),
					Default);
				return;
			}
			m_friends[empty] = name;
			Say(Text(12984, {name}, "%1 is now your friend."), Default);
		}
		if (m_host.save)
			m_host.save();
	}
	UpdateFriendsList();
}

void Friends::IgnoreCommand(const std::string &name)
{
	if (name.empty()) {
		Say(Text(13240, {}, "NAMES OF THOSE CURRENTLY BEING IGNORED:"), Red);
		for (const std::string &ignored : m_ignored)
			if (!ignored.empty())
				Say(ignored, Yellow);
	}
	else {
		// The last slot with the name (any case), and the last empty one.
		int match = -1, empty = -1;
		for (int i = 0; i < Slots; ++i) {
			if (Same(m_ignored[i], name))
				match = i;
			if (m_ignored[i].empty())
				empty = i;
		}
		if (match >= 0) {
			m_ignored[match].clear();
			if (m_host.save)
				m_host.save();
			Say(Text(13241, {name}, "You have removed %1 from your IGNORE list."), Yellow);
		}
		else if (empty < 0)
			Say(Text(13244, {std::to_string(Slots)}, "You have all %1 ignore slots full already.  You must remove some to add new ones."),
				Yellow);
		else if (const ZoneSpawn *player = m_host.player ? m_host.player(name) : nullptr; player && player->gm)
			Say(Text(13242, {}, "That person cannot be ignored."), Yellow);
		else {
			m_ignored[empty] = name.substr(0, 63);
			if (m_host.save)
				m_host.save();
			Say(Text(13243, {name}, "You have added %1 to your IGNORE list."), Yellow);
		}
	}
	UpdateIgnoreList();
}

bool Friends::BeingIgnored(const std::string &name) const
{
	for (const std::string &ignored : m_ignored)
		if (!ignored.empty() && Same(ignored, name))
			return true;
	return false;
}

bool Friends::ChatIgnored(const std::string &sender, int channel)
{
	if (channel == 6 || channel == 14)
		return false;
	for (std::string &ignored : m_ignored)
		if (Same(ignored, sender)) {
			const ZoneSpawn *player = m_host.player ? m_host.player(sender) : nullptr;
			if (!player || !player->gm)
				return true;
			ignored.clear(); // a GM can't be ignored: off the list (not saved), and heard
			return false;
		}
	return false;
}

std::string Friends::FriendsWho() const
{
	std::string list;
	for (const std::string &name : m_friends)
		if (!name.empty() && name.size() < 16 && name.size() + 2 + list.size() < 0x800)
			list += (list.empty() ? "" : ",") + name;
	return list;
}

void Friends::Enable(const char *control, bool on)
{
	if (SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr)
		if (SidlWindow::Control *c = wnd->Find(control))
			c->enabled = on;
}

std::string Friends::InputText(const char *control) const
{
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	const SidlWindow::Control *c = wnd ? wnd->Find(control) : nullptr;
	return c ? c->text : "";
}

std::string Friends::Selected(const char *list) const
{
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	const SidlWindow::Control *c = wnd ? wnd->Find(list) : nullptr;
	if (!c || c->selected_row < 0 || c->selected_row >= static_cast<int>(c->rows.size()) || c->rows[c->selected_row].empty())
		return "";
	return c->rows[c->selected_row][0];
}

void Friends::UpdateFriendsList()
{
	// CFriendsWnd::UpdateFriendsList: none selected, the names in slot order, Delete off.
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	if (SidlWindow::Control *list = wnd ? wnd->Find("FriendsList") : nullptr) {
		list->selected_row = -1;
		list->rows.clear();
		for (const std::string &name : m_friends)
			if (!name.empty())
				list->rows.push_back({name});
		wnd->UpdateListScroll(*list);
	}
	Enable("DeleteButton", false);
}

void Friends::UpdateIgnoreList()
{
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	if (SidlWindow::Control *list = wnd ? wnd->Find("IgnoreList") : nullptr) {
		list->selected_row = -1;
		list->rows.clear();
		for (const std::string &name : m_ignored)
			if (!name.empty())
				list->rows.push_back({name});
		wnd->UpdateListScroll(*list);
	}
	Enable("IgnoreDeleteButton", false);
}

void Friends::AddFriend()
{
	// CFriendsWnd::AddFriend: the box's name to /friends, the box emptied; then Add by the box.
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	SidlWindow::Control *input = wnd ? wnd->Find("NameInput") : nullptr;
	if (!input || !wnd->Find("FriendsList"))
		return;
	wnd->Find("FriendsList")->selected_row = -1;
	if (!input->text.empty()) {
		std::string name = input->text;
		FriendsCommand(name);
		input->text.clear();
	}
	UpdateFriendsList();
	m_name_text = input->text;
	Enable("AddButton", !input->text.empty());
}

void Friends::AddIgnore()
{
	// CFriendsWnd::AddIgnore: the same with the ignore page's box; it too sets the friends' Add
	// (not its own) by the box just emptied.
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	SidlWindow::Control *input = wnd ? wnd->Find("IgnoreNameInput") : nullptr;
	if (!input || !wnd->Find("IgnoreList"))
		return;
	wnd->Find("IgnoreList")->selected_row = -1;
	if (!input->text.empty()) {
		std::string name = input->text;
		IgnoreCommand(name);
		input->text.clear();
	}
	UpdateIgnoreList();
	m_ignore_text = input->text;
	Enable("AddButton", !input->text.empty());
}

void Friends::Frame()
{
	if (!m_windows)
		return;
	bool shown = m_windows->Shown(Screen);
	if (shown && !m_shown) {
		// CFriendsWnd::Activate.
		UpdateFriendsList();
		UpdateIgnoreList();
	}
	m_shown = shown;
	// WndNotification 0xd, a name box's text changed: its Add on while there's a name.
	std::string name = InputText("NameInput"), ignore = InputText("IgnoreNameInput");
	if (name != m_name_text) {
		m_name_text = name;
		Enable("AddButton", !name.empty());
	}
	if (ignore != m_ignore_text) {
		m_ignore_text = ignore;
		Enable("IgnoreAddButton", !ignore.empty());
	}
}

bool Friends::Enter()
{
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	if (!wnd || !m_windows->Shown(Screen))
		return false;
	if (wnd->Focused("NameInput")) {
		AddFriend();
		return true;
	}
	if (wnd->Focused("IgnoreNameInput")) {
		AddIgnore();
		return true;
	}
	return false;
}

bool Friends::Click(const std::string &window, const std::string &control)
{
	if (window != Screen)
		return false;
	if (control == "Subwindows") {
		UpdateFriendsList();
		UpdateIgnoreList();
	}
	else if (control == "WhoButton") {
		// CEverQuest::Who("ALL <the friend>"), or "ALL friend" with none selected.
		std::string name = Selected("FriendsList");
		if (m_host.who)
			m_host.who(Text(13205, {}, "ALL") + " " + (name.empty() ? Text(3112, {}, "friend") : name));
	}
	else if (control == "FriendsList")
		Enable("DeleteButton", true);
	else if (control == "IgnoreList")
		Enable("IgnoreDeleteButton", true);
	else if (control == "AddButton")
		AddFriend();
	else if (control == "IgnoreAddButton")
		AddIgnore();
	else if (control == "DeleteButton") {
		std::string name = Selected("FriendsList");
		if (!name.empty())
			FriendsCommand(name);
		UpdateFriendsList();
	}
	else if (control == "IgnoreDeleteButton") {
		std::string name = Selected("IgnoreList");
		if (!name.empty())
			IgnoreCommand(name);
		UpdateIgnoreList();
	}
	else if (control == "ContactButton") {
		// The active chat window's line: "/tell <the friend> ", focused.
		std::string name = Selected("FriendsList");
		if (!name.empty() && m_host.contact)
			m_host.contact(Text(13974, {}, "/tell") + " " + name + " ");
	}
	else
		return false;
	return true;
}
