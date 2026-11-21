#include "chat_manager.h"

#include "ini_file.h"
#include "window_manager.h"

#include <algorithm>
#include <cstdlib>

ChatManager::ChatManager()
{
	m_windows.resize(1);
	m_windows[0].chat = std::make_unique<ChatWindow>();
	m_windows[0].screen = MainScreen;
}

std::string ChatManager::ScreenName(int index)
{
	// EnableIniStorage's name: "MainChat", or "Chat" and the number in a CXStr two wide, spaces in
	// front.
	if (index == 0)
		return MainScreen;
	std::string number = std::to_string(index);
	if (number.size() < 2)
		number.insert(0, 2 - number.size(), ' ');
	return "Chat" + number;
}

ChatManager::Window *ChatManager::At(int index)
{
	if (index < 0 || index >= Count() || !m_windows[index].chat)
		return nullptr;
	return &m_windows[index];
}

const ChatManager::Window *ChatManager::At(int index) const
{
	if (index < 0 || index >= Count() || !m_windows[index].chat)
		return nullptr;
	return &m_windows[index];
}

int ChatManager::IndexOf(const std::string &screen) const
{
	for (int i = 0; i < Count(); ++i)
		if (m_windows[i].chat && m_windows[i].screen == screen)
			return i;
	return -1;
}

void ChatManager::Attach(int index)
{
	Window &w = m_windows[index];
	SidlWindow *wnd = m_manager ? m_manager->Find(w.screen) : nullptr;
	w.chat->Attach(wnd);
	w.chat->SetLinkColor(m_link_rgb);
	if (wnd && m_menu >= 0)
		m_manager->SetWindowMenu(w.screen, m_menu);
}

void ChatManager::SetMenu(int menu)
{
	m_menu = menu;
	if (!m_manager)
		return;
	for (const Window &w : m_windows)
		if (w.chat)
			m_manager->SetWindowMenu(w.screen, menu);
}

void ChatManager::Cleanup()
{
	m_windows.resize(1);
	m_windows[0].chat = std::make_unique<ChatWindow>();
}

void ChatManager::Build(WindowManager &windows, const IniFile *layout, const Defaults &defaults,
	std::function<std::string(uint32_t, const char *)> text, std::function<uint32_t(int)> color, uint32_t link_rgb)
{
	m_manager = &windows;
	m_text = std::move(text);
	m_color = std::move(color);
	m_link_rgb = link_rgb;
	m_menu = -1;
	m_windows.resize(1);
	m_active = 0;
	m_locked = -1;
	m_map.fill(0);
	m_hit_modes.fill(0);

	// The main window: "Main Chat", no close or minimize box, SetDefaultGameValues' channel and
	// language.
	Window &main = m_windows[0];
	main.screen = MainScreen;
	main.channel = defaults.channel;
	main.chat_channel = 0;
	main.tell_target = defaults.tell_target;
	main.language = defaults.language;
	std::string error;
	if (SidlWindow *wnd = windows.Add(Screen, true, &error, MainScreen)) {
		wnd->Root().text = m_text(6623, "Main Chat");
		wnd->RemoveTitleBoxes();
	}
	Attach(0);
	main.chat->SetFont(3); // CChatManager's constructor: SetChatFont(3)
	if (!layout)
		return;

	auto get = [&](const std::string &key) { return layout->Get("ChatManager", key, "*NULL*"); };
	auto key = [](int index, const char *what) { return "ChatWindow" + std::to_string(index) + "_" + what; };
	auto set = [](const std::string &value) { return !value.empty() && value[0] != '*'; };
	if (std::string num = get("NumWindows"); set(num)) {
		int count = std::min(std::atoi(num.c_str()), 32);
		if (std::string font = get(key(0, "FontStyle")); set(font))
			main.chat->SetFont(std::atoi(font.c_str()));
		if (std::string name = get(key(0, "Name")); set(name))
			SetTitle(0, name);
		std::vector<int> freed;
		for (int i = 1; i <= count; ++i) {
			std::string language = get(key(i, "Language"));
			if (language == "@42") {
				// A window freed before: made, then freed again, so the rest keep their numbers.
				if (int index = Create(nullptr); index >= 0)
					freed.push_back(index);
				continue;
			}
			std::string channel = get(key(i, "DefaultChannel")), chat_channel = get(key(i, "ChatChannel"));
			std::string tell = get(key(i, "TellTarget")), name = get(key(i, "Name")), font = get(key(i, "FontStyle"));
			int index = Create(nullptr, set(name) ? name : "");
			if (index < 0)
				continue;
			Window &w = m_windows[index];
			w.language = set(language) ? std::atoi(language.c_str()) : main.language;
			w.channel = set(channel) ? std::atoi(channel.c_str()) : main.channel;
			w.chat_channel = set(chat_channel) ? std::atoi(chat_channel.c_str()) : main.chat_channel;
			w.tell_target = (set(tell) ? tell : main.tell_target).substr(0, 63);
			if (set(font))
				w.chat->SetFont(std::atoi(font.c_str()));
		}
		for (int index : freed)
			Free(index);
	}
	if (std::string locked = get("LockedActiveWindow"); set(locked)) {
		int index = std::atoi(locked.c_str());
		m_locked = index >= 0 && index < Count() ? index : -1;
	}
	for (int i = 0; i < ChannelMaps; ++i)
		if (std::string at = get("ChannelMap" + std::to_string(i)); set(at)) {
			int index = std::atoi(at.c_str());
			if (index >= 0 && index < Count())
				m_map[i] = index; // a freed one's lines go nowhere (ProcessChatMessage)
		}
	for (int i = 0; i < HitModes; ++i)
		if (std::string mode = get("HitMode" + std::to_string(i)); set(mode)) {
			int value = std::atoi(mode.c_str());
			if (value >= 0 && value < 3)
				m_hit_modes[i] = value;
		}
}

void ChatManager::Detach()
{
	m_windows.resize(1);
	m_windows[0].chat->Attach(nullptr);
	m_manager = nullptr;
	m_active = 0;
	m_locked = -1;
	m_map.fill(0);
}

void ChatManager::ShowAll()
{
	if (!m_manager)
		return;
	for (const Window &w : m_windows)
		if (w.chat)
			m_manager->Show(w.screen, true);
}

int ChatManager::Create(const IniFile *layout, const std::string &name)
{
	if (!m_manager)
		return -1;
	int slot = -1;
	for (int i = 1; i < Count() && slot < 0; ++i)
		if (!m_windows[i].chat)
			slot = i;
	if (slot < 0) {
		if (Count() >= MaxWindows) {
			Main().Add(m_text(6648, "Cannot create any more chat windows!"), m_color ? m_color(13) : 0xf00000);
			return -1;
		}
		slot = Count();
		m_windows.emplace_back();
	}
	Window &w = m_windows[slot];
	w = Window{};
	w.chat = std::make_unique<ChatWindow>();
	w.screen = ScreenName(slot);
	w.language = m_windows[0].language;
	std::string error;
	SidlWindow *wnd = m_manager->Add(Screen, true, &error, w.screen);
	if (!wnd) {
		w.chat.reset();
		return -1;
	}
	wnd->Root().text = m_text(6624, "Chat") + " " + std::to_string(slot);
	if (layout)
		m_manager->Load(*layout, w.screen);
	m_manager->Show(w.screen, true);
	if (!name.empty())
		wnd->Root().text = name;
	Attach(slot);
	return slot;
}

void ChatManager::Free(int index)
{
	if (index <= 0 || !At(index))
		return;
	// UpdateChannelReferences: its lines to the main window; the active one the main one.
	for (int &at : m_map)
		if (at == index)
			at = 0;
	m_active = 0;
	if (m_locked == index)
		m_locked = -1;
	Window &w = m_windows[index];
	if (m_manager)
		m_manager->Remove(w.screen);
	w.chat.reset();
}

void ChatManager::SetTitle(int index, const std::string &title)
{
	Window *w = At(index);
	SidlWindow *wnd = w && m_manager ? m_manager->Find(w->screen) : nullptr;
	if (wnd)
		wnd->Root().text = title;
}

std::string ChatManager::Title(int index) const
{
	const Window *w = At(index);
	const SidlWindow *wnd = w && m_manager ? static_cast<const WindowManager *>(m_manager)->Find(w->screen) : nullptr;
	return wnd ? wnd->Root().text : std::string();
}

void ChatManager::Store(IniFile &ini) const
{
	const char *section = "ChatManager";
	auto key = [](int index, const char *what) { return "ChatWindow" + std::to_string(index) + "_" + what; };
	ini.SetInt(section, "NumWindows", Count() - 1);
	ini.SetInt(section, "LockedActiveWindow", m_locked);
	for (int i = 1; i < Count(); ++i) {
		const Window &w = m_windows[i];
		if (!w.chat) {
			ini.Set(section, key(i, "Language"), "@42");
			continue;
		}
		ini.SetInt(section, key(i, "Language"), w.language);
		ini.SetInt(section, key(i, "DefaultChannel"), w.channel);
		ini.SetInt(section, key(i, "ChatChannel"), w.chat_channel);
		ini.Set(section, key(i, "TellTarget"), w.tell_target);
	}
	for (int i = 0; i < Count(); ++i) {
		if (!m_windows[i].chat)
			continue;
		ini.SetInt(section, key(i, "FontStyle"), m_windows[i].chat->Font());
		ini.Set(section, key(i, "Name"), Title(i));
	}
	for (int i = 0; i < ChannelMaps; ++i)
		if (At(m_map[i]))
			ini.SetInt(section, "ChannelMap" + std::to_string(i), m_map[i]);
	for (int i = 0; i < HitModes; ++i)
		ini.SetInt(section, "HitMode" + std::to_string(i), m_hit_modes[i]);
}

ChatWindow *ChatManager::Typing()
{
	int index = TypingIndex();
	return index >= 0 ? m_windows[index].chat.get() : nullptr;
}

int ChatManager::TypingIndex() const
{
	for (int i = 0; i < Count(); ++i)
		if (m_windows[i].chat && m_windows[i].chat->Typing())
			return i;
	return -1;
}

int ChatManager::ActiveIndex() const
{
	int index = m_locked != -1 ? m_locked : m_active;
	return At(index) ? index : 0;
}

void ChatManager::SetActive(int index)
{
	if (At(index))
		m_active = index;
}

int ChatManager::ChannelIndex(int type)
{
	switch (type) {
	case 0x100: case 0x133: return 0;  // say
	case 0x101: case 0x134: return 1;  // tell
	case 0x102: case 0x135: return 2;  // group
	case 0x147: return 3;              // raid
	case 0x103: case 0x136: return 4;  // guild
	case 0x104: case 0x137: return 5;  // OOC
	case 0x105: case 0x138: return 6;  // auction
	case 0x106: case 0x139: return 7;  // shout
	case 0x107: case 0x13a: return 8;  // emotes
	case 0x109: return 9;              // your hits
	case 0x108: return 10;             // spells: mine
	case 0x10e: return 11;             // skills
	case 0x123: case 0x13b: return 12; // chat channels 1..10
	case 0x124: case 0x13c: return 13;
	case 0x125: case 0x13d: return 14;
	case 0x126: case 0x13e: return 15;
	case 0x127: case 0x13f: return 16;
	case 0x128: case 0x140: return 17;
	case 0x129: case 0x141: return 18;
	case 0x12a: case 0x142: return 19;
	case 0x12b: case 0x143: return 20;
	case 0x12c: case 0x144: return 21;
	case 0x10b: return 23; // your misses
	case 0x10a: return 24; // you being hit
	case 0x10c: return 25; // you being missed
	case 0x117: return 26; // others' hits
	case 0x118: return 27; // others' misses
	case 0x115: return 28; // my death
	case 0x116: return 29; // others' deaths
	case 0x12d: return 30; // critical hits
	case 0x10f: return 31; // disciplines
	case 0x12f: return 32; // too far away
	case 0x130: return 33; // NPC rampage
	case 0x131: return 34; // NPC flurry
	case 0x132: return 35; // NPC enrage
	case 0x120: return 36; // spells: others'
	case 0x121: return 37; // spells: failures
	case 0x12e: return 38; // spells: criticals
	case 0x11c: return 39; // spells: worn off
	case 0x11b: return 40; // non-melee hits
	default: return 22;    // other
	}
}

int ChatManager::ChannelMap(int kind) const
{
	if (kind < 0 || kind >= ChannelMaps)
		return -1;
	return At(m_map[kind]) ? m_map[kind] : -1;
}

void ChatManager::SetChannelMap(int kind, int index)
{
	if (kind >= 0 && kind < ChannelMaps)
		m_map[kind] = index;
}

void ChatManager::Add(const std::string &text, uint32_t rgb, int type)
{
	if (Window *w = At(m_map[ChannelIndex(type)]))
		w->chat->Add(text, rgb);
}

void ChatManager::Layout(TextRenderer &text)
{
	for (Window &w : m_windows)
		if (w.chat)
			w.chat->Layout(text);
}

void ChatManager::SetLinkColor(uint32_t rgb)
{
	m_link_rgb = rgb;
	for (Window &w : m_windows)
		if (w.chat)
			w.chat->SetLinkColor(rgb);
}
