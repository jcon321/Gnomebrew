#include "frontend.h"

#include "eqstr.h"
#include "game_windows.h"
#include "ini_file.h"
#include "inventory.h"
#include "skills.h"
#include "spell_casting.h"
#include "zone_names.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <limits>

namespace
{
	// createStandardButton styles: bitmap triples (normal, over, down) from the table at 0x100550e8.
	const char *const ButtonStyles[4][3] = {
		{"sbutton1.bmp", "sbutton2.bmp", "sbutton3.bmp"},    // 0: 88x34
		{"mbutton1.bmp", "mbutton2.bmp", "mbutton3.bmp"},    // 1: 112x34
		{"button1.bmp", "button2.bmp", "button3.bmp"},       // 2: 228x34
		{"eqbutton1.bmp", "eqbutton2.bmp", "eqbutton3.bmp"}, // 3: 192x80 "PLAY EverQuest"
	};

	constexpr uint64_t ServerListRefreshMs = 60000; // displayServerSelectionScreen re-requests every 0x3c seconds

	std::string Lower(const std::string &s)
	{
		std::string out = s;
		for (auto &c : out)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return out;
	}

	// Our model's look: our spawn, as the zone entered us and our items since have it.
	render::CharacterAppearance PlayerAppearance(const ZoneSession &zone, bool shooting = false)
	{
		const ZoneSpawn &me = zone.Self();
		render::CharacterAppearance appearance;
		appearance.race = me.race ? me.race : zone.Profile().race;
		appearance.gender = me.gender;
		appearance.face = me.face;
		appearance.height = me.size;
		appearance.class_id = me.class_id ? me.class_id : static_cast<uint8_t>(zone.Profile().class_id);
		appearance.hair_style = me.hair_style;
		appearance.hair_color = me.hair_color;
		appearance.beard = me.beard;
		appearance.beard_color = me.beard_color;
		appearance.eye_color1 = me.eye_color1;
		appearance.eye_color2 = me.eye_color2;
		for (int k = 0; k < render::EquipSlots; ++k) {
			appearance.equip[k] = me.equipment[k];
			appearance.tint[k] = me.colors[k];
		}
		// DoAttack(11)'s UpdateItemSlot: shooting, nothing in the primary hand and the range item in
		// the secondary.
		if (shooting) {
			auto range = zone.Items().find(Inventory::Range);
			appearance.equip[7] = 0;
			appearance.equip[8] = range != zone.Items().end() && range->second.idfile.size() > 2
			                          ? static_cast<uint16_t>(std::atoi(range->second.idfile.c_str() + 2))
			                          : 0;
		}
		return appearance;
	}
}

FrontEnd::FrontEnd(FrontEndArt &art, const EqText &strings, TextRenderer &text, const FrontEndConfig &config, GameData ui)
	: m_art(art), m_strings(strings), m_text(text), m_config(config), m_ui(std::move(ui))
{
	m_session.SetLogger([this](const std::string &line) { Log(line); });
	m_world.SetLogger([this](const std::string &line) { Log(line); });
	m_zone.SetLogger([this](const std::string &line) { Log(line); });
	m_combat.SetStrings(m_ui.eqstr);
	m_combat.SetFilters(&m_filters);
	m_zone.SetStringTable(m_ui.eqstr);
	m_zone.SetSpellFile(m_ui.spells);
	m_zone.SetRecordDir(config.record_dir);
	m_debug.SetVSync(m_ui.vsync, m_ui.set_vsync);
	m_user.text = config.username;
	if (!m_ui.eq_dir.empty() && m_ui.definitions) {
		// Character select's zone takes a moment to load; start while the player logs in.
		m_charselect_view = std::make_unique<CharacterSelectView>(m_ui.eq_dir, MakeRenderer());
		m_charselect_view->SetVeliousTextures(config.velious_textures);
		m_charselect_view->SetLuclinModels(config.luclin_models);
		m_charselect_view->SetEmotes(config.char_select_emotes);
		m_charselect_view->StartLoading();
		if (config.char_select_anim_window)
			CharSelectAnimWindow::AddDefinitions(*m_ui.definitions);
	}
	if (m_ui.definitions)
		WindowManager::AddDefinitions(*m_ui.definitions); // the context menus' screen
	m_rules.CheckInstall(m_ui.eq_dir);
	if (m_ui.mixer && !m_ui.eq_dir.empty()) {
		m_sound = std::make_unique<audio::EqSound>(*m_ui.mixer, m_ui.eq_dir, m_ui.sound);
		m_debug.SetSound(m_sound.get());
	}
	m_debug.SetWeather(&m_weather);
	m_debug.SetSpells(m_ui.spells);
	if (!m_config.settings_ini.empty()) {
		m_chat_colors.Load(m_config.settings_ini);
		IniFile ini;
		if (ini.Load(m_config.settings_ini)) {
			m_debug.Load(ini);
			m_key_bindings.Load(ini); // CEverQuest_loadOptions' readKeyMapFromIni
			m_filters.Load(ini);
			// The General page's settings (CEverQuest_loadOptions' eqOptions1.opt, its defaults).
			auto number = [&](const char *section, const char *key, int fallback, int low, int high) {
				std::string value = ini.Get(section, key);
				int n = fallback;
				try {
					if (!value.empty())
						n = std::stoi(value);
				}
				catch (...) {
				}
				return std::clamp(n, low, high);
			};
			m_fast_item_destroy = number("Defaults", "FastItemDestroy", 0, 0, 1) == 1;
			m_guild_invites = number("Defaults", "GuildInvites", 1, 0, 1) == 1;
			m_trade_with = number("Defaults", "PlayerTrade", 2, 0, 2);
			m_sound_realism = number("Sound", "SoundRealism", 0, 0, 20);
			m_music_volume = number("Sound", "MusicVolume", 10, 0, 20);
			m_sound_volume = number("Sound", "SoundVolume", 10, 0, 20);
			// The Display page's.
			auto real = [&](const char *key, float fallback, float low, float high) {
				std::string value = ini.Get("Defaults", key);
				float f = fallback;
				try {
					if (!value.empty())
						f = std::stof(value);
				}
				catch (...) {
				}
				return std::clamp(f, low, high);
			};
			m_pc_names = number("Defaults", "PCNames", 1, 0, 1) == 1;
			m_npc_names = number("Defaults", "NPCNames", 1, 0, 1) == 1;
			m_level_of_detail = number("Defaults", "LevelOfDetail", 1, 0, 1) == 1;
			m_sky = number("Defaults", "Sky", 2, 0, 2); // both layers to start with (eqgame: 1)
			m_particle_density = number("Defaults", "ParticleDensity", 3, 0, 3); // 3
			m_gamma = real("Gamma", 3, 1, 8);                                     // 4 if out of range
			m_clip_slider = number("Defaults", "ClipPlane", 20, 0, 20);
			m_clip_plane = std::max(m_clip_slider * 0.2f, 0.1f);
			m_spell_density = real("SpellParticleDensity", 1, 0, 1);
			m_spell_opacity = real("SpellParticleOpacity", 1, 0, 1);
			m_spell_near_clip = real("SpellParticleNearClipPlane", 2, 1, 8);
			// SetEffectsLevel with Sound Realism's hundredths.
			m_ui.sound.effects_level = (m_sound_realism * 100 / 20) * 0.01f;
			if (m_sound)
				m_sound->SetVolumes(m_ui.sound);
		}
	}
	m_zone.SetServerFilters(m_filters.Packet());
	m_game_values.zone = &m_zone;
	m_game_values.eqstr = m_ui.eqstr;
	m_game_values.group = &m_group;
	Group::Host group_host;
	group_host.chat = [this](const std::string &text, int type) { ChatOut(text, type); };
	group_host.set_target = [this](uint16_t id) { SetTarget(id); };
	// The deity team servers' rule (DAT_006ec740 4).
	group_host.other_team = [this](const ZoneSpawn &spawn) { return m_world.ServerType() == 4 && NotOnSameDeity(spawn); };
	group_host.lfg_off = [this] { Lfg(true, "off"); };
	m_group.Attach(m_zone, m_game_values, std::move(group_host));
	Raid::Host raid_host;
	raid_host.chat = [this](const std::string &text, int type) { ChatOut(text, type); };
	raid_host.other_team = [this](const ZoneSpawn &spawn) { return m_world.ServerType() == 4 && NotOnSameDeity(spawn); };
	raid_host.target = [this](const std::string &name) { InterpretCommand("/target " + name); };
	m_raid.Attach(m_zone, m_game_values, m_group, std::move(raid_host));
	m_raid.SetWindow(&m_raid_window);
	m_group.SetRaid(&m_raid);
	Guild::Host guild_host;
	guild_host.chat = [this](const std::string &text, int type) { ChatOut(text, type); };
	guild_host.target = [this] { return m_game_values.Target(); };
	guild_host.other_team = [this](const ZoneSpawn &spawn) { return m_world.ServerType() == 4 && NotOnSameDeity(spawn); };
	guild_host.receive_invites = [this] { return m_guild_invites; };
	guild_host.asking = [this] { return m_windows.Shown("ConfirmationDialogBox"); };
	guild_host.ask = [this](int, uint32_t timeout_ms, const std::string &text, std::function<void(bool)> answer) {
		return AskInGame(text, timeout_ms, std::move(answer));
	};
	m_guild.Attach(m_zone, m_ui.eqstr, std::move(guild_host));
	EnterStart();
}

void FrontEnd::Log(const std::string &line)
{
	if (log)
		log(line);
}

ButtonArt FrontEnd::Art(int style)
{
	return {&m_art.Get(ButtonStyles[style][0]), &m_art.Get(ButtonStyles[style][1]), &m_art.Get(ButtonStyles[style][2])};
}

// ---------------------------------------------------------------- screens

void FrontEnd::EnterStart()
{
	m_screen = Screen::Start;
	m_session.Close();
	m_world.Close();
	m_zone.Close();
	m_zone_view.reset();
	m_weather.Kill(m_sound.get()); // StopWorldDisplay; then eqgame is gone, and its weather
	m_weather = Weather();
	m_entering_world = false;
	m_logging_in = false;
	// Out of eqgame: its music and the zone's sounds go with it (eqmain has none).
	if (m_sound) {
		m_sound->SetMusicSelection(audio::EqSound::CharacterSelect, false);
		m_sound->SetMusicSelection(audio::EqSound::Opening, false);
		m_sound->SetCurrentZone("");
	}
	m_opening_played = false;

	// displayLuclinOptionsScreen: five 228x34 buttons in a column at x=400.
	const char *keys[] = {"SISATEXT_CONNECTBUTTON", "SISATEXT_ACCOUNTBUTTON", "SISATEXT_CREDITSBUTTON",
		"SISATEXT_CONFIGBUTTON", "SISATEXT_EXITBUTTON"};
	const int ys[] = {0xd2, 0xff, 300, 0x159, 0x186};
	m_start_buttons.assign(5, Button{});
	std::vector<std::string> labels;
	for (int i = 0; i < 5; ++i) {
		m_start_buttons[i].Place(400, ys[i], Art(2));
		m_start_buttons[i].label = m_strings.Get(keys[i]);
		labels.push_back(m_start_buttons[i].label);
	}
	int height = FitTextHeight(m_text, labels, 0xd2, 0x22);
	for (auto &button : m_start_buttons)
		button.text_height = height;
}

void FrontEnd::EnterLogin()
{
	m_screen = Screen::Login;
	m_login_status.clear();
	m_logging_in = false;

	// displayLoginScreen @100226b0: flag 4 puts the buttons at x=10.
	m_connect.Place(10, 0x118, Art(2));
	m_connect.label = m_strings.Get("SISATEXT_CONNECTBUTTON");
	m_cancel.Place(10, 0x14a, Art(2));
	m_cancel.label = m_strings.Get("SISATEXT_CANCELBUTTON");
	m_help.Place(10, 0x17c, Art(2));
	m_help.label = m_strings.Get("SISATEXT_HELPBUTTON");
	for (Button *b : {&m_connect, &m_cancel, &m_help})
		b->text_height = FontSize::Large;

	// createInputField(x=0x127, y, w=0x127, h=0x1a, maxlen incl. terminator 0x14 / 0x11, large
	// font, flag 1: centered)
	m_user.rect = {0x127, 0x104, 0x127, 0x1a};
	m_user.max_chars = 0x13;
	m_user.centered = true;
	m_password.rect = {0x127, 0x16d, 0x127, 0x1a};
	m_password.max_chars = 0x10;
	m_password.password = true;
	m_password.centered = true;
	m_password.text.clear();
	m_user.focused = m_user.text.empty();
	m_password.focused = !m_user.focused;
}

void FrontEnd::EnterServerSelect()
{
	m_screen = Screen::ServerSelect;
	m_submitting = false;
	m_overlay_message.clear();

	m_exit.Place(0xb, 0x1b1, Art(1));
	m_exit.label = m_strings.Get("SISATEXT_EXITBUTTON");
	m_news.Place(0x109, 0x1b1, Art(1));
	m_news.label = m_strings.Get("SISATEXT_NEWSBUTTON");
	int height = FitTextHeight(m_text, {m_exit.label, m_news.label, m_strings.Get("SISATEXT_CHATBUTTON")}, 100, 0x22);
	m_exit.text_height = m_news.text_height = height;

	m_play.Place(0x1a5, 0x185, Art(3));
	m_play.label.clear();

	m_names.rect = {0x82, 100, 0x117, 0xd4};
	m_names.highlight = &m_art.Get("highlight.bmp");
	m_statuses.rect = {0x19a, 100, 0x78, 0xd4};
	m_statuses.highlight = nullptr;

	m_scrollbar.rect = {0x36, 0x37, 0x10, 0x128};
	m_scrollbar.up = &m_art.Get("sbup1.bmp");
	m_scrollbar.up_pressed = &m_art.Get("sbup2.bmp");
	m_scrollbar.down = &m_art.Get("sbdown1.bmp");
	m_scrollbar.down_pressed = &m_art.Get("sbdown2.bmp");
	m_scrollbar.thumb = &m_art.Get("sbthumb.bmp");

	for (CheckBox *box : {&m_sort_name, &m_sort_population}) {
		box->unchecked = &m_art.Get("chekbox1.bmp");
		box->checked = &m_art.Get("chekbox2.bmp");
	}
	m_sort_name.rect = {0xbe, 0x52, 16, 16};
	m_sort_population.rect = {0x136, 0x52, 16, 16};

	m_last_list_request_ms = m_now_ms;
	RebuildServerLists();
	m_overlay_message = m_strings.Get("GREEN_SERVER_SHORT");
}

// ---------------------------------------------------------------- dialogs

void FrontEnd::ShowConfirmation(int category, uint32_t timeout_ms, const std::string &text, std::function<void(bool)> on_close)
{
	// CConfirmationDialog::Activate: one already up goes as No first (its timeout handler); Yes and
	// No, or OK alone for 0x1d; the text; centered on the screen.
	if (!m_ui.definitions) {
		ShowMessage(text, on_close);
		return;
	}
	if (!m_confirm.built) {
		std::string error;
		if (!m_confirm.window.Build(*m_ui.definitions, "ConfirmationDialogBox", error)) {
			Log("confirmation dialog: " + error);
			ShowMessage(text, on_close);
			return;
		}
		m_confirm.built = true;
	}
	if (m_confirm.open)
		CloseConfirmation(false);
	SidlWindow &window = m_confirm.window;
	for (const char *id : {"Yes_Button", "No_Button"})
		if (SidlWindow::Control *button = window.Find(id))
			button->visible = category != 0x1d;
	if (SidlWindow::Control *ok = window.Find("OK_Button"))
		ok->visible = category == 0x1d;
	if (SidlWindow::Control *output = window.Find("TextOutput")) {
		output->text = text;
		output->scroll_pos = 0;
	}
	Rect r = window.Bounds();
	r.x = (m_game_width - r.w) / 2;
	r.y = (m_game_height - r.h) / 2;
	window.Move(r);
	m_confirm.category = category;
	m_confirm.until_ms = timeout_ms ? m_now_ms + timeout_ms : 0;
	m_confirm.on_close = std::move(on_close);
	m_confirm.open = true;
}

void FrontEnd::CloseConfirmation(bool yes)
{
	// ProcessYes or ProcessNo for its category, then Deactivate.
	if (!m_confirm.open)
		return;
	m_confirm.open = false;
	auto on_close = std::move(m_confirm.on_close);
	m_confirm.on_close = nullptr;
	if (on_close)
		on_close(yes);
}

bool FrontEnd::ConfirmationEvent(const InputEvent &event)
{
	if (!m_confirm.open || !event.IsMouse())
		return false;
	const Rect &r = m_confirm.window.Bounds();
	bool over = event.x >= r.x && event.y >= r.y && event.x < r.x + r.w && event.y < r.y + r.h;
	std::string clicked = m_confirm.window.OnMouse(event);
	if (clicked == "Yes_Button" || clicked == "OK_Button")
		CloseConfirmation(true);
	else if (clicked == "No_Button")
		CloseConfirmation(false);
	return over || !clicked.empty();
}

void FrontEnd::ShowMessage(const std::string &text, std::function<void(bool)> on_close)
{
	m_dialog = Dialog{};
	m_dialog.open = true;
	m_dialog.text = text;
	m_dialog.on_close = std::move(on_close);
	// showConfirmationDialog: dialog.bmp for up to 220 characters, else dialogmedium.bmp.
	m_dialog.medium = text.size() > 0xdc;
	int button_y = (m_dialog.medium ? 0x1a9 : 0x11c) - 10 - 34; // flag 0x10: bottom-aligned
	m_dialog.ok.Place(320 - 44, button_y, Art(0));                 // flag 0x01: centered
	m_dialog.ok.label = m_strings.Get("SISATEXT_OKBUTTON");
	m_dialog.label_height = FitTextHeight(m_text, {m_dialog.ok.label}, 0x50, 0x22);
	m_dialog.ok.text_height = m_dialog.label_height;
	Rect area = DialogArea();
	m_dialog.ok.OnMouse({InputEvent::Type::MouseMove, m_mouse_x - area.x, m_mouse_y - area.y});
}

void FrontEnd::ShowQuestion(const std::string &text, std::function<void(bool)> on_close)
{
	ShowMessage(text, std::move(on_close));
	m_dialog.question = true;
	int button_y = m_dialog.ok.rect.y;
	m_dialog.yes.Place((m_dialog.medium ? 0x55 : 0xaa) + 10, button_y, Art(0));          // flag 0x14
	m_dialog.no.Place((m_dialog.medium ? 0x22b : 0x1d6) - 10 - 88, button_y, Art(0));    // flag 0x18
	m_dialog.yes.label = m_strings.Get("SISATEXT_YESBUTTON");
	m_dialog.no.label = m_strings.Get("SISATEXT_NOBUTTON");
	m_dialog.label_height = FitTextHeight(m_text, {m_dialog.yes.label, m_dialog.no.label}, 0x50, 0x22);
	m_dialog.yes.text_height = m_dialog.no.text_height = m_dialog.label_height;
}

void FrontEnd::CloseDialog(bool result)
{
	auto on_close = std::move(m_dialog.on_close);
	m_dialog = Dialog{};
	if (on_close)
		on_close(result);
}

void FrontEnd::DialogEvent(const InputEvent &event)
{
	if (event.type == InputEvent::Type::Key) {
		if (event.key == InputEvent::Key::Enter)
			CloseDialog(true);
		else if (event.key == InputEvent::Key::Escape)
			CloseDialog(!m_dialog.question);
		return;
	}
	if (m_dialog.question) {
		bool yes = m_dialog.yes.OnMouse(event);
		bool no = m_dialog.no.OnMouse(event);
		if (yes || no)
			CloseDialog(yes);
	}
	else if (m_dialog.ok.OnMouse(event))
		CloseDialog(true);
}

// ---------------------------------------------------------------- input

namespace
{
	// The DirectInput code of a key a host gave without one (the preview tools').
	int ScanOf(InputEvent::Key key)
	{
		using Key = InputEvent::Key;
		switch (key) {
		case Key::Enter: return 0x1c;
		case Key::Escape: return 0x01;
		case Key::Tab: return 0x0f;
		case Key::Backspace: return 0x0e;
		case Key::Up: return 0xc8;
		case Key::Down: return 0xd0;
		case Key::Left: return 0xcb;
		case Key::Right: return 0xcd;
		case Key::PageUp: return 0xc9;
		case Key::PageDown: return 0xd1;
		case Key::F1: return 0x3b;
		case Key::F9: return 0x43;
		case Key::F11: return 0x57;
		case Key::W: return 0x11;
		case Key::A: return 0x1e;
		case Key::S: return 0x1f;
		case Key::D: return 0x20;
		case Key::R: return 0x13;
		case Key::Space: return 0x39;
		case Key::Delete: return 0xd3;
		case Key::Alt: return 0x38;
		default: return 0;
		}
	}
}

void FrontEnd::OnEvent(const InputEvent &given)
{
	InputEvent event = given;
	if ((event.type == InputEvent::Type::Key || event.type == InputEvent::Type::KeyUp) && event.scan == 0)
		event.scan = ScanOf(event.key);
	// The character of a key that opened the chat line (OpenChatLine) isn't typed into it.
	if (event.type == InputEvent::Type::Key)
		m_chat_opened_by_key = false;
	else if (event.type == InputEvent::Type::Char && m_chat_opened_by_key) {
		m_chat_opened_by_key = false;
		return;
	}
	if (event.IsMouse()) {
		m_mouse_x = event.x;
		m_mouse_y = event.y;
	}

	// Held movement keys are let go even while a dialog has the input.
	bool alt_key = event.key == InputEvent::Key::Alt || event.scan == 0x38 || event.scan == 0xb8;
	if (alt_key && (event.type == InputEvent::Type::Key || event.type == InputEvent::Type::KeyUp)) {
		m_alt_down = event.type == InputEvent::Type::Key;
		return;
	}
	// ProcessKeyUp: what the key's bound to lets go, even while a dialog has the input.
	if (event.type == InputEvent::Type::KeyUp) {
		ZoneKey(event);
		return;
	}
	if (event.type == InputEvent::Type::FocusLost) {
		m_alt_down = false;
		m_keys = HeldKeys{};
		m_mouse_turn = m_mouse_pan = m_mouse_grabbed = false;
		return;
	}
	// Mouse drags end even if a dialog came up in the middle of one.
	if (event.type == InputEvent::Type::RightUp)
		m_mouse_turn = false;
	if (event.type == InputEvent::Type::MouseUp)
		m_mouse_pan = false;
	if (!m_mouse_turn && !m_mouse_pan)
		m_mouse_grabbed = false;

	// Handlers see positions relative to the area they're laid out in.
	Rect area = m_dialog.open ? DialogArea() : ScreenArea();
	InputEvent local = event;
	local.x -= area.x;
	local.y -= area.y;
	if (m_dialog.open) {
		DialogEvent(local);
		return;
	}

	switch (m_screen) {
	case Screen::Start:
		StartEvent(local);
		break;
	case Screen::Login:
		LoginEvent(local);
		break;
	case Screen::ServerSelect:
		ServerSelectEvent(local);
		break;
	case Screen::CharacterSelect:
		CharacterSelectEvent(local);
		break;
	case Screen::CharacterCreate:
		CharacterCreateEvent(local);
		break;
	case Screen::Zone:
		ZoneEvent(local);
		break;
	}
}

void FrontEnd::StartEvent(const InputEvent &event)
{
	auto quit_prompt = [this]() {
		ShowQuestion(m_strings.Get("SISATEXT_QUIT_PROMPT"), [this](bool yes) { m_quit = yes; });
	};

	if (event.type == InputEvent::Type::Key) {
		if (event.key == InputEvent::Key::Enter)
			EnterLogin();
		else if (event.key == InputEvent::Key::Escape)
			quit_prompt();
		return;
	}

	int clicked = -1;
	for (int i = 0; i < static_cast<int>(m_start_buttons.size()); ++i)
		if (m_start_buttons[i].OnMouse(event))
			clicked = i;

	switch (clicked) {
	case 0:
		EnterLogin();
		break;
	case 1:
	case 2:
	case 3:
		ShowMessage(m_strings.Get("SISATEXT_NOT_IMPLEMENTED"));
		break;
	case 4:
		quit_prompt();
		break;
	default:
		break;
	}
}

void FrontEnd::LoginEvent(const InputEvent &event)
{
	InputField *focused = m_user.focused ? &m_user : (m_password.focused ? &m_password : nullptr);

	if (event.type == InputEvent::Type::Char) {
		if (focused && !m_logging_in)
			focused->OnChar(event.ch);
		return;
	}
	if (event.type == InputEvent::Type::Key) {
		switch (event.key) {
		case InputEvent::Key::Backspace:
			if (focused && !m_logging_in)
				focused->OnBackspace();
			break;
		case InputEvent::Key::Tab:
		case InputEvent::Key::Up:
		case InputEvent::Key::Down:
			m_user.focused = !m_user.focused;
			m_password.focused = !m_user.focused;
			break;
		case InputEvent::Key::Enter:
			// Enter in the name field moves to the password; in the password it connects.
			if (m_user.focused) {
				m_user.focused = false;
				m_password.focused = true;
			}
			else
				SubmitLogin();
			break;
		case InputEvent::Key::Escape:
			EnterStart();
			break;
		default:
			break;
		}
		return;
	}

	if (event.type == InputEvent::Type::MouseDown) {
		if (m_user.rect.Contains(event.x, event.y)) {
			m_user.focused = true;
			m_password.focused = false;
		}
		else if (m_password.rect.Contains(event.x, event.y)) {
			m_user.focused = false;
			m_password.focused = true;
		}
	}

	bool connect = m_connect.OnMouse(event);
	bool cancel = m_cancel.OnMouse(event);
	bool help = m_help.OnMouse(event);
	if (connect)
		SubmitLogin();
	else if (cancel)
		EnterStart();
	else if (help)
		ShowMessage(m_strings.Get("SISATEXT_NOT_IMPLEMENTED"));
}

void FrontEnd::ServerSelectEvent(const InputEvent &event)
{
	int page = std::max(1, m_names.VisibleRows() - 1);

	if (event.type == InputEvent::Type::Key) {
		int move = 0;
		switch (event.key) {
		case InputEvent::Key::Up: move = -1; break;
		case InputEvent::Key::Down: move = 1; break;
		case InputEvent::Key::PageUp: move = -page; break;
		case InputEvent::Key::PageDown: move = page; break;
		case InputEvent::Key::Enter: PlaySelected(); return;
		case InputEvent::Key::Escape: EnterStart(); return;
		default: return;
		}
		if (!m_worlds.empty()) {
			m_names.selected = std::clamp(m_names.selected + move, 0, static_cast<int>(m_worlds.size()) - 1);
			m_selected_name = m_worlds[m_names.selected].name;
			m_names.EnsureVisible(m_names.selected);
			m_statuses.top = m_names.top;
		}
		return;
	}

	if (event.type == InputEvent::Type::Wheel) {
		m_names.top -= event.wheel * 3;
		m_names.ClampTop();
		m_statuses.top = m_names.top;
		return;
	}

	if (event.type == InputEvent::Type::MouseDown || event.type == InputEvent::Type::DoubleClick) {
		int max_top = std::max(0, static_cast<int>(m_worlds.size()) - m_names.VisibleRows());
		int delta = m_scrollbar.OnMouseDown(event.x, event.y, m_names.top, max_top, page);
		if (delta) {
			m_names.top += delta;
			m_names.ClampTop();
			m_statuses.top = m_names.top;
		}

		int row = m_names.RowAt(event.x, event.y);
		if (row < 0)
			row = m_statuses.RowAt(event.x, event.y);
		if (row >= 0) {
			m_names.selected = row;
			m_selected_name = m_worlds[row].name;
			if (event.type == InputEvent::Type::DoubleClick)
				PlaySelected();
		}

		CheckBox *boxes[2] = {&m_sort_name, &m_sort_population};
		for (int mode = 0; mode < 2; ++mode) {
			if (boxes[mode]->rect.Contains(event.x, event.y) && m_sort_mode != mode) {
				m_sort_mode = mode;
				m_names.top = 0;
				RebuildServerLists();
			}
		}
	}
	if (event.type == InputEvent::Type::MouseUp)
		m_scrollbar.OnMouseUp();

	m_play.visible = !m_worlds.empty();
	bool exit = m_exit.OnMouse(event);
	bool news = m_news.OnMouse(event);
	bool play = m_play.OnMouse(event);
	if (exit)
		EnterStart();
	else if (news)
		ShowMessage(m_strings.Get("SISATEXT_NOT_IMPLEMENTED"));
	else if (play)
		PlaySelected();
}

// ---------------------------------------------------------------- login session

void FrontEnd::SubmitLogin()
{
	if (m_logging_in)
		return;
	if (m_user.text.empty() || m_password.text.empty()) {
		// The login screen only connects when both fields have text.
		(m_user.text.empty() ? m_user : m_password).focused = true;
		(m_user.text.empty() ? m_password : m_user).focused = false;
		return;
	}

	m_login_status = m_strings.Get("CONNECT_LOGIN");
	m_logging_in = true;
	if (!m_session.Begin(m_config.login_host, m_config.login_port, m_user.text, m_password.text)) {
		m_logging_in = false;
		m_login_status.clear();
		ShowMessage(m_strings.Get("SISATEXT_CONNECT_FAILED"));
	}
}

void FrontEnd::PlaySelected()
{
	if (m_submitting || m_names.selected < 0 || m_names.selected >= static_cast<int>(m_worlds.size()))
		return;
	m_submitting = true;
	m_overlay_message = m_strings.Get("SUBMITING_PLAYER");
	m_session.Play(m_worlds[m_names.selected]);
}

void FrontEnd::RebuildServerLists()
{
	m_worlds = m_session.Worlds();
	if (m_sort_mode == 0)
		std::stable_sort(m_worlds.begin(), m_worlds.end(),
			[](const WorldListing &a, const WorldListing &b) { return Lower(a.name) < Lower(b.name); });
	else
		std::stable_sort(m_worlds.begin(), m_worlds.end(),
			[](const WorldListing &a, const WorldListing &b) { return a.status > b.status; });
	m_sort_name.value = m_sort_mode == 0;
	m_sort_population.value = m_sort_mode == 1;

	m_names.items.clear();
	m_statuses.items.clear();
	m_names.selected = m_worlds.empty() ? -1 : 0;
	for (size_t i = 0; i < m_worlds.size(); ++i) {
		const auto &w = m_worlds[i];
		if (w.name == m_selected_name)
			m_names.selected = static_cast<int>(i);
		m_names.items.push_back({w.name, w.green_name ? Colors::Preferred : Colors::Entry});

		// updateServerLists: -1 DOWN (red), -2 LOCKED (blue), else UP (green) or the count.
		if (w.status == -1)
			m_statuses.items.push_back({m_strings.Get("TXT_DOWN"), Colors::Red});
		else if (w.status == -2)
			m_statuses.items.push_back({m_strings.Get("TXT_LOCKED"), Colors::Blue});
		else if (m_session.ShowUserCounts())
			m_statuses.items.push_back({std::to_string(w.status), Colors::Entry});
		else
			m_statuses.items.push_back({m_strings.Get("UP"), Colors::Preferred});
	}
	if (m_names.selected >= 0)
		m_selected_name = m_worlds[m_names.selected].name;
	m_names.ClampTop();
	m_statuses.top = m_names.top;
	m_play.visible = !m_worlds.empty();
}

void FrontEnd::HandleSessionEvents(const std::vector<LoginSession::Event> &events)
{
	using Type = LoginSession::Event::Type;
	using Error = LoginSession::Event::Error;

	for (const auto &event : events) {
		switch (event.type) {
		case Type::Status:
			m_login_status = m_strings.Get("LOGIN_QUEUE");
			break;
		case Type::LoggedIn:
			break;
		case Type::ServerList:
			if (m_screen == Screen::Login) {
				m_logging_in = false;
				EnterServerSelect();
			}
			else if (m_screen == Screen::ServerSelect)
				RebuildServerLists();
			break;
		case Type::PlayApproved: {
			// eqmain hands over to eqgame here, which runs WorldAuthenticate against the chosen server.
			const LoginResult &result = m_session.Result();
			if (!m_ui.definitions) {
				ShowMessage("Character select needs the EQ UI files.\n\n" + m_ui.error, [this](bool) { EnterStart(); });
				break;
			}
			if (!m_world.Begin(result.world.address, m_config.world_port, result.session_name, result.world_key)) {
				m_submitting = false;
				ShowMessage(m_strings.Get("SISATEXT_CONNECT_FAILED"), [this](bool) { EnterStart(); });
			}
			break;
		}
		case Type::Error: {
			m_logging_in = false;
			m_login_status.clear();
			std::string text = event.text;
			if (event.error == Error::ConnectFailed)
				text = m_strings.Get("SISATEXT_CONNECT_FAILED");
			else if (event.error == Error::ConnectionLost)
				text = m_strings.Get("SISATEXT_LOSTCON");

			if (event.error == Error::PlayRejected) {
				m_submitting = false;
				m_overlay_message = m_strings.Get("GREEN_SERVER_SHORT");
				ShowMessage(text);
			}
			else if (m_screen == Screen::ServerSelect)
				ShowMessage(text, [this](bool) { EnterLogin(); });
			else
				ShowMessage(text);
			break;
		}
		}
	}
}

void FrontEnd::Update(uint64_t now_ms)
{
	m_now_ms = now_ms;
	// CConfirmationDialog's timeout: No.
	if (m_confirm.open && m_confirm.until_ms && now_ms >= m_confirm.until_ms)
		CloseConfirmation(false);
	// CCharacterSelect's frame (vtable +0xac, @0040f23b): with the confirmation dialog gone and the
	// first character button off (Delete's), SetButtonsEnabledState(1).
	if (m_screen == Screen::CharacterSelect && !m_confirm.open && !m_buttons_enabled)
		SetButtonsEnabledState(true);
	std::vector<LoginSession::Event> events;
	m_session.Pump(0, events);
	if (!events.empty())
		HandleSessionEvents(events);

	std::vector<WorldSession::Event> world_events;
	m_world.Pump(0, world_events);
	if (!world_events.empty())
		HandleWorldEvents(world_events);

	std::vector<ZoneSession::Event> zone_events;
	m_zone.Pump(0, zone_events);
	if (!zone_events.empty())
		HandleZoneEvents(zone_events);
	if (m_zone_view && m_screen == Screen::Zone) {
		// Our movement, then OP_ClientUpdate (ZoneSession paces it like DoMainLoop).
		int hour;
		float minute;
		if (m_zone.GameTime(hour, minute))
			m_zone_view->SetGameTime(hour, minute);
		m_zone_view->UpdateSpawns(m_zone.Spawns(), m_zone.MySpawnId(), now_ms);
		// What we put on or took off (UpdatePlayerAppearance), and the light we carry.
		const ZoneSpawn &me = m_zone.Self();
		// ProcessControls' swings of auto attack (one puts the weapons back in our hands), then
		// DoPassageOfTime readying the hands and the Range button.
		MeleeControls();
		RangedPassageOfTime(now_ms);
		HungerAndThirst(now_ms);
		if (me.look_serial != m_my_look_serial || m_ranged_look) {
			m_my_look_serial = me.look_serial;
			m_ranged_look = false;
			m_zone_view->SetPlayer(PlayerAppearance(m_zone, m_last_attack == 11), me.walk_speed, me.run_speed);
		}
		m_zone_view->SetMyLight(me.light);
		m_zone_view->SetMyLevel(m_zone.Profile().level);
		m_zone_view->SetDebugDraw(m_debug.Drawing());
		CameraControls(now_ms);
		MoveControls controls = ZoneControls();
		// Sitting (kneeling, lying...) nothing moves us, a jump included (ProcessControls takes the
		// keys only standing or crouching). Forward stands us up from sitting (ZoneEvent), and we go
		// at once.
		uint8_t stance = m_zone.Self().stance;
		if ((stance != 100 && stance != 111) || m_dead) {
			controls.forward = controls.back = controls.left = controls.right = false;
			controls.strafe = controls.jump = false;
		}
		bool jumped = m_zone_view->Move(controls, now_ms, m_mouse_turn || m_mouse_pan);
		if (m_zone_view->Placed())
			m_zone.SetMovement(m_zone_view->Movement());
		if (jumped)
			m_zone.Jump();
		if (m_zone_view->Crouching() != m_crouch_sent) {
			m_crouch_sent = !m_crouch_sent;
			m_zone.SetStance(m_crouch_sent ? 111 : 100);
		}
		std::string zone_line = m_zone_view->TakeZoneLine();
		if (!zone_line.empty())
			HandleZoneLine(zone_line);
		UpdateDeath(now_ms);
	}
	if (m_screen == Screen::Zone) {
		Rect area = ScreenArea();
		m_windows.Update(now_ms, m_mouse_x - area.x, m_mouse_y - area.y);
		m_window_state.busy = m_containers.Combining() || m_merchant.Busy() || m_loot.Busy() || m_ground.Busy() ||
		                      m_abilities.Busy();
		m_window_state.fv_rules = m_game_values.fv_rules;
		HandleWindowClicks();
		m_bank.Frame(now_ms);
		m_merchant.Frame(now_ms);
		m_train.Frame(now_ms);
		m_raid_window.Frame(now_ms);
		m_loot.Frame(now_ms);
		m_trade.Frame(now_ms);
		m_ground.Frame(now_ms);
		// The Destroy question gives up after its 10 s (ProcessNo).
		if (m_destroy_until_ms && (now_ms >= m_destroy_until_ms || !m_windows.Shown("ConfirmationDialogBox"))) {
			m_destroy_until_ms = 0;
			m_windows.Show("ConfirmationDialogBox", false);
		}
		// Another question's time running out is its No (CConfirmationDialog::Draw's ProcessNo).
		if (m_question.open && (now_ms >= m_question.until_ms || !m_windows.Shown("ConfirmationDialogBox")))
			AnswerInGame(false);
		m_options.Frame(now_ms);
		m_spell_book.Frame(now_ms);
		m_spell_cast.Frame(now_ms);
		m_abilities.Frame(now_ms);
		m_buffs.Frame(now_ms);
		if (m_zone_view) {
			m_zone_view->SetSpeedModifier(m_buffs.SpeedModifier(m_zone.Self().run_speed));
			// EQPlayer::InitSneakMod: half the walk speed sneaking, a rogue's 0.1 more a level past 50.
			float sneak = 0;
			if (m_zone.Self().sneaking != 0) {
				const ZoneProfile &me = m_zone.Profile();
				sneak = 0.5f;
				if (me.class_id == 9 && me.level > 50)
					sneak += std::min(static_cast<float>(me.level - 50), 10.0f) * 0.1f;
			}
			m_zone_view->SetSneakModifier(sneak);
		}
		// DoPassageOfTime: auto attack goes off once we're not standing (ExecuteCmd 8), and the zone's
		// told whenever it's changed.
		if (m_attack && m_zone.Self().stance != 100)
			ToggleAutoAttack();
		if (m_attack != m_attack_sent && m_zone.GetState() == ZoneSession::State::InZone)
			m_attack_sent = m_zone.SendAutoAttack(m_attack) ? m_attack : m_attack_sent;
		if (m_zone.GetState() == ZoneSession::State::InZone) {
			// DoPassageOfTime: walking or running told as it changes (__RunWalkState, taken the
			// first time without telling); every 181 six-second ticks, SavePC.
			if (m_run_walk_state < 0)
				m_run_walk_state = m_run ? 1 : 0;
			else if (m_run_walk_state != (m_run ? 1 : 0)) {
				m_run_walk_state = m_run ? 1 : 0;
				m_zone.SetRunMode(m_run);
			}
			if (now_ms - m_save_tick_ms > 6000) {
				m_save_tick_ms = now_ms;
				if (++m_save_ticks > 180)
					SavePC();
			}
			// DoMainLoop: [Defaults] TargetGroupBuff, if it's set, as we come in.
			if (!m_tgb_sent) {
				m_tgb_sent = true;
				if (!m_config.settings_ini.empty()) {
					IniFile ini;
					std::string tgb;
					if (ini.Load(m_config.settings_ini) && !(tgb = ini.Get("Defaults", "TargetGroupBuff")).empty())
						m_zone.TargetGroupBuff(static_cast<uint32_t>(atoi(tgb.c_str())));
				}
			}
		}
		// DoPassageOfTime: the social going's next line.
		std::string social_line = SocialStep(m_window_state, now_ms);
		if (!social_line.empty())
			InterpretCommand(social_line);
		m_game_values.running = m_run;
		m_game_values.attacking = m_attack;
		m_game_values.range_ready = m_range_ready;
		m_game_values.now_ms = now_ms;
		if (m_zone_view)
			m_game_values.heading = m_zone_view->Movement().heading;
		UpdateGameWindows(m_windows, m_game_values, m_window_state);
		WindowMenuPicks();
		m_color_picker.Update();
		m_compass.Frame(m_windows, m_game_values.heading, m_zone.Profile().skills[40] /* Sense Heading */, now_ms);
		m_containers.Update(now_ms);
		if (m_windows.TakeChanged())
			SaveUiLayout();
	}

	if (m_screen == Screen::ServerSelect && !m_submitting && now_ms - m_last_list_request_ms > ServerListRefreshMs) {
		m_last_list_request_ms = now_ms;
		m_session.RequestServerList();
	}
	// RealRender_World's weather, in the world whether there's sound or not.
	if (m_screen == Screen::Zone && m_zone_view && m_zone_view->Loaded() && m_zone_view->Error().empty() &&
		m_zone_view->Placed())
		WeatherFrame(now_ms);
	SoundFrame(now_ms);
}

void FrontEnd::SoundFrame(uint64_t now_ms)
{
	if (!m_sound)
		return;
	bool world = m_screen == Screen::Zone && m_zone_view && m_zone_view->Loaded() && m_zone_view->Error().empty() &&
	             m_zone_view->Placed();
	if (world) {
		if (!m_main_loop_started) { // DoMainLoop
			m_main_loop_started = true;
			m_sound->SetMusicSelection(audio::EqSound::Opening, false);
		}
		// SetupEmitterEnvironment, every half second: day or night, covered or not; and
		// SetGenericEnvironment's ceiling (the client's every frame).
		bool surroundings = now_ms - m_sound_surroundings_ms >= 500;
		ZoneView::Hearing hearing = m_zone_view->SoundListener(surroundings);
		m_sound->SetListener({hearing.position.x, hearing.position.y, hearing.position.z},
			{hearing.forward.x, hearing.forward.y, hearing.forward.z});
		if (surroundings) {
			m_sound_surroundings_ms = now_ms;
			int hour;
			float minute;
			if (m_zone.GameTime(hour, minute))
				m_sound->SetNight(hour < 5 || hour > 18);
			using Environment = audio::EqSound::Environment;
			float ceiling = hearing.ceiling < 0 ? 1e6f : hearing.ceiling;
			m_sound->SetEnvironment(ceiling <= 20 ? Environment::LowCeiling
			                        : ceiling <= 500 && hearing.covered ? Environment::HighCeiling
			                                                            : Environment::Outside);
			m_sound->SetCovered(hearing.covered); // setSoundEmitterUnderwater: no rain or wind under cover
		}
	}
	// GiveTime runs at character select and in the world (not while a zone loads).
	if (world || m_screen == Screen::CharacterSelect || m_screen == Screen::CharacterCreate)
		m_sound->GiveTime(now_ms);
}

// ---------------------------------------------------------------- rendering

void FrontEnd::RenderStart(Canvas &canvas)
{
	canvas.Blit(m_art.Get("eqlstart.bmp"), 0, 0);

	// Two 640x13 overlay strips with the trademark lines, centered, near-white, 14px.
	const Image &strip = m_art.Get("eqlstart_overlay.bmp");
	const char *lines[2] = {"OVERALY1", "OVERLAY2"};
	const int ys[2] = {0x1bd, 0x1cb};
	for (int i = 0; i < 2; ++i) {
		canvas.Blit(strip, 0, ys[i], true);
		std::string text = m_strings.Get(lines[i]);
		TextSize size = m_text.Measure(text, FontSize::Normal);
		m_text.Draw(canvas, 320 - size.w / 2, ys[i] + 13 / 2 - size.h / 2, text, FontSize::Normal, Colors::Light);
	}

	for (const auto &button : m_start_buttons)
		button.Draw(canvas, m_text);
}

void FrontEnd::RenderLogin(Canvas &canvas, uint64_t now_ms)
{
	canvas.Blit(m_art.Get("eqklogin.bmp"), 0, 0);

	m_text.Draw(canvas, 0x19a, 0xf0, m_strings.Get("SISATEXT_TAB_HANDLE"), FontSize::Normal, Colors::Label);
	m_text.Draw(canvas, 400, 0x159, m_strings.Get("SISATEXT_TAB_PASSWORD"), FontSize::Normal, Colors::Label);
	m_user.Draw(canvas, m_text, now_ms);
	m_password.Draw(canvas, m_text, now_ms);

	m_connect.Draw(canvas, m_text);
	m_cancel.Draw(canvas, m_text);
	m_help.Draw(canvas, m_text);

	// displayStatusMessage: "Status:" in blue at (0x136, 400), the message in red 8px after it.
	if (!m_login_status.empty()) {
		std::string prefix = m_strings.Get("STATUS2");
		m_text.Draw(canvas, 0x136, 400, prefix, FontSize::Normal, Colors::Blue);
		int x = 0x136 + m_text.Measure(prefix, FontSize::Normal).w + 8;
		m_text.Draw(canvas, x, 400, m_login_status, FontSize::Normal, Colors::Red);
	}
}

void FrontEnd::RenderServerSelect(Canvas &canvas)
{
	canvas.Blit(m_art.Get("selserver.bmp"), 0, 0);

	m_text.Draw(canvas, 0xd2, 0x52, m_strings.Get("SISATEXT_SERVER_NAME"), FontSize::Normal, Colors::Label);
	m_text.Draw(canvas, 0x14a, 0x52, m_strings.Get("SISATEXT_SERVER_PLAYERS"), FontSize::Normal, Colors::Label);
	m_sort_name.Draw(canvas);
	m_sort_population.Draw(canvas);

	m_names.Draw(canvas, m_text);
	m_statuses.Draw(canvas, m_text);
	int max_top = std::max(0, static_cast<int>(m_worlds.size()) - m_names.VisibleRows());
	m_scrollbar.Draw(canvas, m_names.top, max_top);
	if (m_worlds.size() > 0xf)
		m_text.Draw(canvas, 0x82, 0x139, m_strings.Get("SCROLL_FOR_MORE_SERVERS"), FontSize::Normal, Colors::Red);

	// The status bar under the list: an overlay strip with a centered message.
	const Image &overlay = m_art.Get("selserver_overlay.bmp");
	canvas.Blit(overlay, 0xf0, 0x168, true);
	if (m_submitting)
		m_text.Draw(canvas, 0x74, 0x16a, m_strings.Get("CONNECT_STATUS"), FontSize::Normal, Colors::Light);
	if (!m_overlay_message.empty()) {
		TextSize size = m_text.Measure(m_overlay_message, FontSize::Normal);
		m_text.Draw(canvas, 0xf0 + overlay.width / 2 - size.w / 2, 0x168 + overlay.height / 2 - size.h / 2, m_overlay_message,
			FontSize::Normal, Colors::Overlay);
	}

	m_exit.Draw(canvas, m_text);
	m_news.Draw(canvas, m_text);
	m_play.Draw(canvas, m_text);
}

void FrontEnd::SetGameResolution(int width, int height)
{
	m_game_width = std::max(width, static_cast<int>(Width));
	m_game_height = std::max(height, static_cast<int>(Height));
}

Rect FrontEnd::DialogArea() const
{
	return {(m_game_width - Width) / 2, (m_game_height - Height) / 2, Width, Height};
}

Rect FrontEnd::ScreenArea() const
{
	if (m_screen == Screen::CharacterSelect || m_screen == Screen::CharacterCreate || m_screen == Screen::Zone)
		return {0, 0, m_game_width, m_game_height};
	return DialogArea();
}

void FrontEnd::RenderDialog(Canvas &canvas)
{
	const Image &image = m_art.Get(m_dialog.medium ? "dialogmedium.bmp" : "dialog.bmp");
	canvas.Blit(image, m_dialog.medium ? 0x55 : 0xaa, 0x32, true);

	Rect text_box = m_dialog.medium ? Rect{0x87, 100, 0x172, 0x113} : Rect{0xdc, 100, 200, 0x86};
	m_text.DrawWrapped(canvas, text_box, m_dialog.text, FontSize::Normal, Colors::Label);

	if (m_dialog.question) {
		m_dialog.yes.Draw(canvas, m_text);
		m_dialog.no.Draw(canvas, m_text);
	}
	else
		m_dialog.ok.Draw(canvas, m_text);
}

void FrontEnd::Render(Canvas &canvas, uint64_t now_ms)
{
	// The 640x480 screens sit in the middle of the window, black around them.
	Rect area = ScreenArea();
	if (area.w < canvas.Width() || area.h < canvas.Height())
		canvas.Fill({0, 0, canvas.Width(), canvas.Height()}, 0x000000);
	Canvas screen = canvas.Sub(area);
	m_screen_fills_window = area.x == 0 && area.y == 0 && area.w == canvas.Width() && area.h == canvas.Height();
	m_cursor_drawn = false;
	switch (m_screen) {
	case Screen::Start:
		RenderStart(screen);
		break;
	case Screen::Login:
		RenderLogin(screen, now_ms);
		break;
	case Screen::ServerSelect:
		RenderServerSelect(screen);
		break;
	case Screen::CharacterSelect:
		RenderCharacterSelect(screen, now_ms);
		break;
	case Screen::CharacterCreate:
		RenderCharacterCreate(screen, now_ms);
		break;
	case Screen::Zone:
		RenderZone(screen, now_ms);
		break;
	}

	if (m_dialog.open) {
		canvas.DarkenAll();
		Rect area = DialogArea();
		Canvas dialog = canvas.Sub(area);
		RenderDialog(dialog);
	}

	// The item or button on the cursor, over everything but the cursor (RenderZone's own, when it
	// drew them).
	if (m_cursor_drawn)
		return;
	if (m_screen == Screen::Zone && m_ui_hidden && !m_dialog.open)
		return; // no DrawCursor either
	if (m_cursor_visible && !m_mouse_grabbed && m_screen == Screen::Zone && !m_dialog.open)
		DrawCursorAttachment(screen, m_text, m_windows, m_window_state, m_game_values, m_mouse_x - area.x,
			m_mouse_y - area.y, now_ms);
	// In the zone, the windows' own cursors while moving or sizing one (or over a border that sizes).
	if (m_cursor_visible && !m_mouse_grabbed &&
	    !(m_screen == Screen::Zone && !m_dialog.open && m_windows.DrawCursor(screen, m_mouse_x - area.x, m_mouse_y - area.y, now_ms)))
		canvas.Blit(m_art.Get("cursor.bmp"), m_mouse_x, m_mouse_y, true);
}

bool FrontEnd::MouseGrab(int &x, int &y) const
{
	if (!m_mouse_grabbed)
		return false;
	Rect area = ScreenArea();
	x = area.x + m_look_x;
	y = area.y + m_look_y;
	return true;
}

// ---------------------------------------------------------------- character select (eqgame)

namespace
{
	constexpr int CharacterButtons = 8; // EQUI_CharacterSelect.xml has Char1_Button .. Char8_Button

	std::string SlotButton(int slot)
	{
		return "Char" + std::to_string(slot + 1) + "_Button";
	}
}

void FrontEnd::HandleWorldEvents(const std::vector<WorldSession::Event> &events)
{
	using Type = WorldSession::Event::Type;
	for (const auto &event : events) {
		switch (event.type) {
		case Type::Approved:
			break;
		case Type::Characters:
			if (m_screen != Screen::CharacterSelect)
				EnterCharacterSelect();
			else
				RefreshCharacterButtons();
			break;
		case Type::ZoneServer:
			m_entering_world = false;
			if (!m_zoning)
				m_guild.EnteringGame(); // SetDefaultGameValues
			m_zone.SetGuilds(m_world.Guilds());
			if (!m_zone.Begin(m_world.ZoneAddress(), m_world.ZonePort(), m_world.EnteringCharacter())) {
				ShowMessage(m_strings.Get("SISATEXT_CONNECT_FAILED"), [this](bool) { EnterStart(); });
				break;
			}
			EnterZoneScreen(m_zoning);
			break;
		case Type::CharacterCreated: {
			// The new list is already in; character select starts on the new character.
			// HandleNameApprovalResponse(1): the dialog goes.
			m_creating = false;
			CloseConfirmation(false);
			const auto &slots = m_world.Characters();
			for (int i = 0; i < static_cast<int>(slots.size()); ++i)
				if (slots[i].name == event.text)
					m_config.last_character = i;
			break;
		}
		case Type::CreateFailed:
			if (m_creating && m_creation) {
				// HandleNameApprovalResponse: "Name has been rejected", back to the first screen.
				m_creating = false;
				m_creation->NameRejected();
				ShowConfirmation(0x1d, 45000, m_creation->RejectedText());
			}
			else
				ShowMessage("Character creation failed: " + event.text);
			break;
		case Type::ZoneUnavailable:
			// 0x8005: back in character select, eqstr 1954 in the confirmation dialog (0x1d, 4 s).
			m_entering_world = false;
			if (m_charselect_view)
				m_charselect_view->SetUserLight(true);
			RefreshCharacterButtons();
			ShowConfirmation(0x1d, 4000, EqString(1954, "That zone is unavailable."));
			break;
		case Type::Error:
			m_entering_world = false;
			ShowMessage(m_strings.Get("SISATEXT_LOSTCON") + "\n\n(" + event.text + ")", [this](bool) { EnterStart(); });
			break;
		}
	}
}

void FrontEnd::EnterCharacterSelect()
{
	std::string error;
	if (!m_charselect.Build(*m_ui.definitions, "CharacterSelectWindow", error)) {
		m_world.Logout();
		ShowMessage(error, [this](bool) { EnterStart(); });
		return;
	}
	m_screen = Screen::CharacterSelect;
	m_submitting = false;
	m_entering_world = false;
	if (m_config.char_select_anim_window && m_charselect_view && !m_anim_window) {
		auto window = std::make_unique<CharSelectAnimWindow>();
		window->SetAllAnimations(m_config.char_select_emotes);
		if (window->Build(*m_ui.definitions, error))
			m_anim_window = std::move(window);
		else
			Log("animations window: " + error);
	}
	m_anim_serial = 0;
	// CDisplay::InitDDraw starts the opening music when eqgame starts, and initializeNetworkGame
	// character select's whenever it's entered (the same eqtheme.mp3).
	if (m_sound) {
		if (!m_opening_played)
			m_sound->SetMusicSelection(audio::EqSound::Opening, true);
		m_opening_played = true;
		m_sound->SetMusicSelection(audio::EqSound::CharacterSelect, true);
	}

	// CCharacterSelect::AlternateBackgroundAvailable: Rotate starts checked, not exploring. The
	// buttons start on.
	if (auto *rotate = m_charselect.Find("Rotate_Button"))
		rotate->checked = true;
	if (m_charselect_view) {
		m_charselect_view->SetRotate(true);
		m_charselect_view->SetCloseUp(false);
		m_charselect_view->SetUserLight(true);
		if (m_charselect_view->Exploring())
			m_charselect_view->LeaveExplore();
	}
	m_exploring = false;
	m_buttons_enabled = true;
	if (m_explore_window.Empty() && !m_explore_window.Build(*m_ui.definitions, "ExploreModeWindow", error))
		Log("explore window: " + error);

	// SelectCharacter(1002): the remembered slot (LastCharSel), else the first; none when the first
	// is empty.
	m_selected_slot = -1;
	const auto &slots = m_world.Characters();
	auto filled = [&](int i) { return i >= 0 && i < CharacterButtons && i < static_cast<int>(slots.size()) && !slots[i].Empty(); };
	int last = m_config.last_character;
	if (filled(last))
		m_selected_slot = last;
	else if (filled(0))
		m_selected_slot = 0;
	if (m_selected_slot >= 0)
		m_config.last_character = m_selected_slot;
	RefreshCharacterButtons();
}

void FrontEnd::RefreshCharacterButtons()
{
	const auto &slots = m_world.Characters();
	if (m_selected_slot >= 0 && (m_selected_slot >= static_cast<int>(slots.size()) || slots[m_selected_slot].Empty()))
		m_selected_slot = -1;

	// UpdateButtonNames: a character's name, or eqstr 1947 for an empty slot.
	std::string create = EqString(1947, "Create a New Character");
	for (int i = 0; i < CharacterButtons; ++i) {
		SidlWindow::Control *button = m_charselect.Find(SlotButton(i));
		if (!button)
			continue;
		bool filled = i < static_cast<int>(slots.size()) && !slots[i].Empty();
		button->text = filled ? slots[i].name : create;
		button->checked = i == m_selected_slot;
		button->enabled = m_buttons_enabled && !m_entering_world;
	}

	bool have_character = m_selected_slot >= 0 && !m_entering_world;
	if (auto *enter = m_charselect.Find("Enter_World_Button"))
		enter->enabled = m_buttons_enabled && have_character;
	if (auto *del = m_charselect.Find("Delete_Button"))
		del->enabled = m_buttons_enabled && have_character;
	if (auto *explore = m_charselect.Find("Explore_Button"))
		explore->enabled = m_buttons_enabled && !m_entering_world;
	if (auto *quit = m_charselect.Find("Quit_Button"))
		quit->enabled = !m_entering_world;

	if (m_charselect_view) {
		// SelectCharacter: the model takes the character's look, class, deity and zone; with none
		// selected it's "Newbie" (eqstr 4000), a level 1 warrior, looking as it did (at first
		// initializeNetworkGame's human man).
		render::CharacterAppearance appearance;
		uint8_t class_id = 1;
		uint16_t deity = 0;
		if (m_selected_slot >= 0) {
			const CharacterSlot &c = slots[m_selected_slot];
			appearance.race = c.race;
			appearance.gender = c.gender;
			appearance.face = c.face;
			appearance.class_id = static_cast<uint8_t>(c.class_id);
			appearance.hair_style = c.hair_style;
			appearance.hair_color = c.hair_color;
			appearance.beard = c.beard;
			appearance.beard_color = c.beard_color;
			appearance.eye_color1 = c.eye_color1;
			appearance.eye_color2 = c.eye_color2;
			for (int k = 0; k < render::EquipSlots; ++k) {
				appearance.equip[k] = c.equip[k];
				appearance.tint[k] = c.tint[k];
			}
			class_id = c.class_id;
			deity = c.deity;
		}
		else if (const render::CharacterAppearance *shown = m_charselect_view->Shown())
			appearance = *shown;
		else {
			appearance.race = 1;
			appearance.gender = 0;
		}
		if (m_selected_slot < 0)
			appearance.class_id = 1;
		m_charselect_view->SetCharacter(&appearance, class_id, deity);
	}
}

void FrontEnd::SetButtonsEnabledState(bool enabled)
{
	m_buttons_enabled = enabled;
	RefreshCharacterButtons();
}

void FrontEnd::SelectSlot(int slot)
{
	// CCharacterSelect::SelectCharacter: nothing while the selected one's button is off; exploring,
	// it leaves explore mode and comes back into it with the new character.
	const auto &slots = m_world.Characters();
	if (slot < 0 || slot >= CharacterButtons || !m_buttons_enabled)
		return;
	if (slot >= static_cast<int>(slots.size()) || slots[slot].Empty()) {
		// An empty slot is "Create New Character" (WndNotification).
		if (m_exploring)
			LeaveExplorationMode();
		RefreshCharacterButtons();
		EnterCharacterCreate();
		return;
	}
	bool exploring = m_exploring;
	if (exploring)
		LeaveExplorationMode();
	m_selected_slot = slot;
	m_config.last_character = slot; // DoCharacterSelection writes LastCharSel when the selection changes
	RefreshCharacterButtons();
	if (exploring)
		EnterExplorationMode();
}

void FrontEnd::EnterWorldSelected()
{
	// CCharacterSelect::EnterWorld: with a character selected, the user light goes and
	// DoCharacterSelection sends the name (0x8001).
	const auto &slots = m_world.Characters();
	if (m_entering_world || m_selected_slot < 0 || m_selected_slot >= static_cast<int>(slots.size()))
		return;
	if (m_exploring)
		LeaveExplorationMode();
	if (m_world.EnterWorld(slots[m_selected_slot].name)) {
		m_entering_world = true;
		if (m_charselect_view)
			m_charselect_view->SetUserLight(false);
		RefreshCharacterButtons();
	}
}

void FrontEnd::DeleteSelected()
{
	// CCharacterSelect::DeleteCharacter (ProcessYes, category 0x1b): the character's option file
	// (CEverQuest_RemoveCharacterOptionFile: <name>_<server>.ini, our hot buttons and socials) and
	// 0x405a with the name; the world answers with the new list.
	const auto &slots = m_world.Characters();
	if (m_selected_slot < 0 || m_selected_slot >= static_cast<int>(slots.size()))
		return;
	std::string name = slots[m_selected_slot].name;
	if (!m_config.ui_dir.empty())
		std::remove((m_config.ui_dir + "/" + name + "_" + m_config.login_host.substr(0, 7) + ".ini").c_str());
	m_world.DeleteCharacter(name);
}

void FrontEnd::LeaveWorld()
{
	m_world.Logout();
	EnterStart();
}

std::string FrontEnd::CharacterNameTag(int slot) const
{
	// CDisplay::SetNameSpriteState with the character select window up: "%s [%d %s]\n%s" (name,
	// level, GetClassDesc, GetFullZoneName). Nobody selected: "Newbie", level 1, a warrior, in the
	// load zone.
	const auto &slots = m_world.Characters();
	if (slot >= 0 && slot < static_cast<int>(slots.size()) && !slots[slot].Empty()) {
		const CharacterSlot &c = slots[slot];
		return c.name + " [" + std::to_string(c.level) + " " + GameValues::ClassDesc(m_ui.eqstr, c.class_id) + "]\n" +
		       FullZoneName(c.zone, m_ui.eqstr);
	}
	uint32_t zone = m_charselect_view && m_charselect_view->ZoneName() == "load" ? 184 : 185;
	return EqString(4000, "Newbie") + " [1 " + GameValues::ClassDesc(m_ui.eqstr, 1) + "]\n" + FullZoneName(zone, m_ui.eqstr);
}

void FrontEnd::EnterExplorationMode()
{
	// CCharacterSelect::EnterExplorationMode: the window goes for ExploreModeWindow (its label the
	// character: eqstr 6681 "%1 [%2 %3] in %4"), the default camera, the other characters round us.
	if (!m_charselect_view || !m_charselect_view->Loaded())
		return;
	m_exploring = true;
	std::vector<CharacterSelectView::ExploreCharacter> others;
	const auto &slots = m_world.Characters();
	for (int i = 0; i < CharacterButtons && i < static_cast<int>(slots.size()); ++i) {
		if (i == m_selected_slot || slots[i].Empty())
			continue;
		const CharacterSlot &c = slots[i];
		CharacterSelectView::ExploreCharacter other;
		other.slot = i;
		other.class_id = static_cast<uint8_t>(c.class_id);
		render::CharacterAppearance &a = other.appearance;
		a.race = c.race;
		a.gender = c.gender;
		a.face = c.face;
		a.class_id = other.class_id;
		a.hair_style = c.hair_style;
		a.hair_color = c.hair_color;
		a.beard = c.beard;
		a.beard_color = c.beard_color;
		a.eye_color1 = c.eye_color1;
		a.eye_color2 = c.eye_color2;
		for (int k = 0; k < render::EquipSlots; ++k) {
			a.equip[k] = c.equip[k];
			a.tint[k] = c.tint[k];
		}
		others.push_back(other);
	}
	m_charselect_view->EnterExplore(others);
	if (SidlWindow::Control *label = m_explore_window.Find("EXW_CharInfoLabel")) {
		std::string tag = CharacterNameTag(m_selected_slot);
		size_t newline = tag.find('\n');
		std::string first = tag.substr(0, newline);
		std::string name = first.substr(0, first.find(" ["));
		std::string inside = first.substr(first.find('[') + 1);
		inside = inside.substr(0, inside.size() - 1);
		std::string level = inside.substr(0, inside.find(' '));
		std::string class_desc = inside.substr(inside.find(' ') + 1);
		std::string zone = newline == std::string::npos ? "" : tag.substr(newline + 1);
		label->text = m_ui.eqstr ? m_ui.eqstr->Format(6681, {name, level, class_desc, zone})
		                         : name + " [" + level + " " + class_desc + "] in " + zone;
	}
}

void FrontEnd::LeaveExplorationMode()
{
	// LeaveExplorationMode: the explore window goes, the others too, the character back in place and
	// the window back.
	m_exploring = false;
	m_keys = HeldKeys{};
	m_mouse_turn = m_mouse_grabbed = false;
	if (m_charselect_view)
		m_charselect_view->LeaveExplore();
}

void FrontEnd::ExploreEvent(const InputEvent &event)
{
	// ProcessNewUICharacterSelectKeys exploring: Escape leaves, Enter enters the world, the rest
	// are ProcessMovementKeys' (the movement keys, F9's view). The right button looks around
	// (ProcessControls' mouse look), a left click on another character selects it (LeftClickedOnPlayer),
	// and the wheel zooms the camera (ours, as in a zone).
	switch (event.type) {
	case InputEvent::Type::Key:
		if (event.key == InputEvent::Key::Escape)
			LeaveExplorationMode();
		else if (event.key == InputEvent::Key::Enter)
			EnterWorldSelected();
		else if (event.scan != 0 && !event.repeat)
			for (int cmd : m_key_bindings.Down(KeyWithModifiers(event)))
				if (cmd == KeyMap::Forward || cmd == KeyMap::Back || cmd == KeyMap::Left || cmd == KeyMap::Right ||
				    cmd == KeyMap::Jump || cmd == KeyMap::ToggleCam)
					ExecuteCmd(cmd, true, 0);
		return;
	case InputEvent::Type::RightDown:
		m_look_x = event.x;
		m_look_y = event.y;
		m_mouse_turn = true;
		return;
	case InputEvent::Type::MouseMove:
		if (m_mouse_turn && m_charselect_view) {
			int moved_x = event.x - m_look_x, moved_y = event.y - m_look_y;
			if (moved_x == 0 && moved_y == 0)
				return; // the host putting the cursor back
			m_mouse_grabbed = true;
			m_charselect_view->Turn(-0.6f * static_cast<float>(moved_x));
			m_charselect_view->Pitch(-0.25f * static_cast<float>(moved_y));
		}
		return;
	case InputEvent::Type::Wheel:
		if (m_charselect_view)
			m_charselect_view->Zoom(event.wheel);
		return;
	case InputEvent::Type::MouseUp:
		if (m_charselect_view) {
			int slot = m_charselect_view->PickCharacter(event.x, event.y);
			if (slot >= 0)
				SelectSlot(slot);
		}
		return;
	default:
		return;
	}
}

void FrontEnd::CharacterSelectEvent(const InputEvent &event)
{
	// The confirmation dialog takes clicks on it; the rest go on as usual.
	if (ConfirmationEvent(event))
		return;
	if (m_exploring) {
		ExploreEvent(event);
		return;
	}
	if (event.type == InputEvent::Type::Key) {
		// ProcessNewUICharacterSelectKeys: Escape is Quit (ActivateBackButton), Enter enters the
		// world, Up and Down step through the characters round the ends (SelectCharacter 1001
		// and 1000).
		const auto &slots = m_world.Characters();
		auto filled = [&](int i) { return i >= 0 && i < CharacterButtons && i < static_cast<int>(slots.size()) && !slots[i].Empty(); };
		switch (event.key) {
		case InputEvent::Key::Up:
		case InputEvent::Key::Down: {
			// 1001: the one before; from the first, the last filled slot (else 0). 1000: the one
			// after, or the first past the last or an empty one. Then none if the first is empty.
			int slot;
			if (event.key == InputEvent::Key::Up) {
				slot = m_selected_slot - 1;
				if (slot < 0) {
					slot = CharacterButtons - 1;
					while (slot > 0 && !filled(slot))
						--slot;
				}
			}
			else {
				slot = m_selected_slot + 1;
				if (slot > CharacterButtons - 1 || !filled(slot))
					slot = 0;
			}
			if (slot == 0 && !filled(0))
				slot = -1;
			if (!m_buttons_enabled || slot == m_selected_slot)
				break;
			m_selected_slot = filled(slot) ? slot : -1;
			if (m_selected_slot >= 0)
				m_config.last_character = m_selected_slot;
			RefreshCharacterButtons();
			break;
		}
		case InputEvent::Key::Enter:
			EnterWorldSelected();
			break;
		case InputEvent::Key::Escape:
			LeaveWorld();
			break;
		default:
			break;
		}
		return;
	}

	if (m_anim_window && event.IsMouse() && m_charselect_view && m_charselect_view->Loaded()) {
		switch (m_anim_window->OnMouse(event)) {
		case CharSelectAnimWindow::Action::ModeChanged:
			m_config.char_select_emotes = m_anim_window->AllAnimations(); // kept on exit (CharSelectEmotes)
			if (m_charselect_view)
				m_charselect_view->SetEmotes(m_config.char_select_emotes);
			return;
		case CharSelectAnimWindow::Action::Play:
			if (m_charselect_view)
				m_charselect_view->PlayAnimation(m_anim_window->Selected());
			return;
		case CharSelectAnimWindow::Action::None:
			break;
		}
	}

	std::string clicked = m_charselect.OnMouse(event);
	if (clicked.empty())
		return;

	for (int i = 0; i < CharacterButtons; ++i) {
		if (clicked == SlotButton(i)) {
			SelectSlot(i);
			if (event.type == InputEvent::Type::DoubleClick)
				EnterWorldSelected();
			return;
		}
	}
	if (clicked == "Enter_World_Button")
		EnterWorldSelected();
	else if (clicked == "Delete_Button") {
		// WndNotification: eqstr 10019 in the confirmation dialog (0x1b, 10 s), the buttons off.
		const auto &slots = m_world.Characters();
		if (m_selected_slot < 0 || m_selected_slot >= static_cast<int>(slots.size()))
			return;
		std::string name = slots[m_selected_slot].name;
		std::string text = m_ui.eqstr ? m_ui.eqstr->Format(10019, {name}) : "Are you absolutely sure you want to delete " + name + "?";
		ShowConfirmation(0x1b, 10000, text, [this](bool yes) {
			if (yes)
				DeleteSelected();
		});
		SetButtonsEnabledState(false);
	}
	else if (clicked == "Quit_Button")
		LeaveWorld();
	else if (clicked == "Explore_Button")
		EnterExplorationMode();
	else if (clicked == "Rotate_Button" && m_charselect_view) {
		if (auto *rotate = m_charselect.Find("Rotate_Button"))
			m_charselect_view->SetRotate(rotate->checked);
	}
}

void FrontEnd::RenderCharacterSelect(Canvas &canvas, uint64_t now_ms)
{
	// The zone and the selected character in 3D behind the window (CharacterSelectView).
	const auto &slots = m_world.Characters();
	if (m_exploring && m_charselect_view) {
		// ProcessControls, before the frame.
		MoveControls controls;
		controls.forward = m_keys.forward;
		controls.back = m_keys.back;
		controls.left = m_keys.left;
		controls.right = m_keys.right;
		controls.strafe = m_mouse_turn;
		controls.jump = m_jump_requested;
		controls.run = m_run;
		m_jump_requested = false;
		m_charselect_view->Explore(controls, now_ms);
	}
	RenderScene(canvas, now_ms);
	bool scene = m_charselect_view && m_charselect_view->Loaded() && m_charselect_view->Error().empty();
	if (scene) {
		// TurnCharacterNameSpritesOn: the name sprites over the heads, ours and (exploring) the
		// others'. CDisplay_SetNameSpriteTint makes them yellow, and renderNameTag lights them as in
		// a zone: the room's ambient + 0.4.
		const render::Vec3 &light = m_charselect_view->Ambient();
		uint32_t color = NameSpriteLit(0xffff00, light.x, light.y, light.z);
		for (const auto &tag : m_charselect_view->NameTags())
			DrawNameSprite(canvas, tag.x, tag.y, tag.depth, tag.focal, CharacterNameTag(tag.slot < 0 ? m_selected_slot : tag.slot),
				color);
	}
	else {
		if (m_selected_slot >= 0 && m_selected_slot < static_cast<int>(slots.size())) {
			const CharacterSlot &c = slots[m_selected_slot];
			int center = (m_charselect.Bounds().Right() + canvas.Width()) / 2;
			auto centered = [&](int y, const std::string &line, int height, uint32_t color) {
				TextSize size = m_text.Measure(line, height);
				m_text.Draw(canvas, center - size.w / 2, y, line, height, color);
			};
			int top = canvas.Height() * 3 / 8;
			centered(top, c.name, 20, 0xffffff);
			centered(top + 30, "Level " + std::to_string(c.level) + " " + RaceName(c.race) + " " + ClassName(c.class_id), 14,
				0xd0d0d0);
			if (m_charselect_view && !m_charselect_view->Loaded())
				centered(canvas.Height() - 40, "Loading...", 12, 0x808080);
		}
	}
	if (m_entering_world)
		m_text.Draw(canvas, m_charselect.Bounds().Right() + 20, 20, "Entering world...", 14, 0xffffff);

	if (m_exploring)
		m_explore_window.Draw(canvas, m_text, *m_ui.textures, now_ms);
	else
		m_charselect.Draw(canvas, m_text, *m_ui.textures, now_ms);
	if (m_anim_window && scene && !m_exploring) {
		// The list follows the model shown: another character, or the same one rebuilt.
		if (m_anim_serial != m_charselect_view->ModelSerial() + 1) {
			m_anim_window->SetAnimations(m_charselect_view->Animations());
			m_anim_serial = m_charselect_view->ModelSerial() + 1;
		}
		m_anim_window->Place(canvas.Width(), canvas.Height());
		m_anim_window->Draw(canvas, m_text, *m_ui.textures, now_ms);
	}
	if (m_confirm.open)
		m_confirm.window.Draw(canvas, m_text, *m_ui.textures, now_ms);
}

// ---------------------------------------------------------------- character creation (eqgame)

void FrontEnd::EnterCharacterCreate()
{
	if (!m_creation) {
		auto creation = std::make_unique<CharacterCreation>(m_rules, m_ui.eqstr);
		std::string error;
		if (!creation->Build(*m_ui.definitions, error)) {
			ShowMessage(error);
			return;
		}
		m_creation = std::move(creation);
	}
	// DoNewCharacterCreation: CCharacterCreation::Activate over the character select scene.
	m_rules.SetServerType(m_world.ServerType());
	m_rules.SetStrictNames(m_world.StrictNames());
	m_creation->SetModels(m_charselect_view ? m_charselect_view->Models() : nullptr);
	m_creation->Activate(m_game_width, m_game_height, m_world.Expansions(),
		m_charselect_view ? m_charselect_view->Shown() : nullptr);
	m_creating = false;
	m_screen = Screen::CharacterCreate;
	ShowCreatedCharacter();
}

void FrontEnd::ReturnToCharacterSelect()
{
	m_screen = Screen::CharacterSelect;
	if (m_charselect_view) {
		m_charselect_view->SetCloseUp(false);
		const auto *rotate = m_charselect.Find("Rotate_Button");
		m_charselect_view->SetRotate(!rotate || rotate->checked);
	}
	RefreshCharacterButtons();
}

void FrontEnd::ShowCreatedCharacter()
{
	if (!m_charselect_view || !m_creation)
		return;
	render::CharacterAppearance appearance = m_creation->Appearance();
	appearance.class_id = static_cast<uint8_t>(m_creation->ClassId());
	m_charselect_view->SetCharacter(&appearance, m_creation->ClassId());
	m_charselect_view->SetRotate(m_creation->Rotate());
	m_charselect_view->SetCloseUp(m_creation->PickingFace());
}

void FrontEnd::CharacterCreateEvent(const InputEvent &event)
{
	// Its messages are the confirmation dialog's (OK, category 0x1d), which takes clicks on it.
	if (ConfirmationEvent(event))
		return;
	if (m_creating)
		return;
	switch (m_creation->OnEvent(event)) {
	case CharacterCreation::Action::Back:
		ReturnToCharacterSelect();
		return;
	case CharacterCreation::Action::Message:
		ShowConfirmation(0x1d, m_creation->MessageMs(), m_creation->Message());
		break;
	case CharacterCreation::Action::Submit: {
		// SubmitNameToWorld: OP_ApproveName (eqstr 0x62c up for 45 s), then (approved) OP_CharacterCreate.
		NewCharacter character = m_creation->Character();
		if (m_world.CreateCharacter(character)) {
			m_creating = true;
			ShowConfirmation(0x1d, 45000, m_creation->SubmittingText());
		}
		else
			ShowConfirmation(0x1d, 45000, m_creation->RejectedText());
		break;
	}
	case CharacterCreation::Action::None:
		break;
	}
	ShowCreatedCharacter();
}

bool FrontEnd::ControlCenter(const std::string &screen_id, int &x, int &y) const
{
	const SidlWindow *window = m_screen == Screen::CharacterSelect && m_exploring  ? &m_explore_window
	                           : m_screen == Screen::CharacterSelect               ? &m_charselect
	                           : m_screen == Screen::CharacterCreate && m_creation ? &m_creation->Window()
	                                                                               : nullptr;
	if (m_confirm.open && m_confirm.window.Find(screen_id))
		window = &m_confirm.window;
	const SidlWindow::Control *control = window ? window->Find(screen_id) : nullptr;
	if (!control || !window->Shown(*control))
		return false;
	x = control->rect.x + control->rect.w / 2;
	y = control->rect.y + control->rect.h / 2;
	return true;
}

void FrontEnd::RenderScene(Canvas &canvas, uint64_t now_ms)
{
	bool scene = m_charselect_view && m_charselect_view->Loaded() && m_charselect_view->Error().empty();
	if (m_charselect_view && m_charselect_view->Loaded() && !m_charselect_view->Error().empty() && !m_scene_error_logged) {
		Log("character select scene: " + m_charselect_view->Error());
		m_scene_error_logged = true;
	}
	if (scene)
		m_charselect_view->Render(canvas, now_ms);
	else
		canvas.Fill({0, 0, canvas.Width(), canvas.Height()}, 0x000000);
}

void FrontEnd::RenderCharacterCreate(Canvas &canvas, uint64_t now_ms)
{
	RenderScene(canvas, now_ms);
	m_creation->Draw(canvas, m_text, *m_ui.textures, now_ms);
	if (m_confirm.open)
		m_confirm.window.Draw(canvas, m_text, *m_ui.textures, now_ms);
}

// ---------------------------------------------------------------- zone (eqgame's world display)

namespace
{
	constexpr uint32_t ZoneWhite = 0xffffff;
	constexpr uint32_t ZoneGrey = 0xa0a0a0;
	constexpr uint32_t ZoneYellow = 0xf0e070;
}

void FrontEnd::BuildNameSprites(uint64_t now_ms)
{
	// The target's pulses a step a frame in eqgame, a cycle about every 300 ms; by the clock here,
	// whatever our frame rate.
	int pulse = static_cast<int>(now_ms % NameSprite::PulseMs * NameSprite::PulseSteps / NameSprite::PulseMs);
	uint16_t target = m_game_values.target;
	ZoneSpawn self = m_zone.Self();
	self.name = m_zone.Profile().name;
	const auto &spawns = m_zone.Spawns();
	std::vector<uint16_t> ids = m_zone_view->DrawnSpawns();
	ids.push_back(0); // us
	std::vector<ZoneView::NameSprite> sprites;
	for (uint16_t id : ids) {
		const ZoneSpawn *spawn = &self;
		bool targeted = target != 0 && (id == 0 ? target == m_zone.MySpawnId() : id == target);
		if (id != 0) {
			auto it = spawns.find(id);
			if (it == spawns.end())
				continue;
			spawn = &it->second;
		}
		// SetNameSpriteState's state: players' (us too) with Show PC Names, the rest with Show NPC Names.
		if (spawn->npc == 0 ? !m_pc_names : !m_npc_names)
			continue;
		std::string text = NameSpriteText(*spawn, m_config.show_names, m_zone.Guilds(), m_ui.eqstr, targeted);
		if (text.empty())
			continue;
		ZoneView::NameSprite sprite;
		sprite.id = id;
		size_t newline = text.find('\n');
		sprite.lines[0] = NameLineTexture(text.substr(0, newline), now_ms);
		if (newline != std::string::npos)
			sprite.lines[1] = NameLineTexture(text.substr(newline + 1), now_ms);
		sprite.color = NameSpriteColor(*spawn, m_world.ServerType());
		if (targeted)
			sprite.color = NameSpritePulse(sprite.color, pulse);
		sprites.push_back(sprite);
	}
	// Extended nameplate distance: as far as the zone is drawn (its actors are culled there).
	float yon = m_config.extended_nameplates ? std::numeric_limits<float>::max() : NameSprite::YonClip;
	m_zone_view->SetNameSprites(std::move(sprites), yon);
	// Lines not shown for a while go (their GPU copies with them).
	if (m_name_textures.size() > 256)
		for (auto it = m_name_textures.begin(); it != m_name_textures.end();)
			it = now_ms - it->second.used_ms > 10000 ? m_name_textures.erase(it) : std::next(it);
}

void FrontEnd::WindowMenuPicks()
{
	for (const WindowManager::MenuPick &pick : m_windows.TakeMenuPicks()) {
		const WindowManager::Settings *settings = m_windows.SettingsOf(pick.window);
		if (!settings)
			continue;
		std::string window = pick.window;
		if (pick.id == WindowManager::MenuTint) {
			m_color_picker.Activate(settings->tint, [this, window](uint32_t color) { m_windows.SetTint(window, color); });
			continue;
		}
		bool faded = pick.id == WindowManager::MenuFadedAlpha;
		int alpha = faded ? settings->fade_to : settings->alpha;
		AskQuantity(m_windows, m_window_state, 100, -1, -1, false, false,
			[this, window, faded](int value) {
				int alpha = std::min((value * 255 + 0x80) / 100, 255);
				if (faded)
					m_windows.SetFadeTo(window, alpha);
				else
					m_windows.SetAlpha(window, alpha);
			},
			alpha * 100 / 255);
	}
}

ZoneView::NameLine FrontEnd::NameLineTexture(const std::string &text, uint64_t now_ms)
{
	if (text.empty())
		return {};
	auto it = m_name_textures.find(text);
	if (it == m_name_textures.end()) {
		// s3dCreateStringSprite with font 5: the 20 pixel bold Arial's coverage, white, its alpha
		// the coverage, in a power-of-two texture from its top left.
		NameTexture made;
		std::vector<uint8_t> alpha;
		int w = 0, h = 0, left = 0;
		if (m_text.Coverage(text, NameSprite::FontHeight, NameSprite::FontWeight, alpha, w, h, left)) {
			Image image;
			image.width = 1;
			while (image.width < w)
				image.width *= 2;
			image.height = 1;
			while (image.height < h)
				image.height *= 2;
			image.pixels.assign(static_cast<size_t>(image.width) * image.height, 0x00ffffff);
			for (int y = 0; y < h; ++y)
				for (int x = 0; x < w; ++x)
					image.pixels[static_cast<size_t>(y) * image.width + x] =
						static_cast<uint32_t>(alpha[static_cast<size_t>(y) * w + x]) << 24 | 0xffffff;
			image.has_alpha = true;
			made.texture = render::MakeTexture(image);
			made.line.texture = made.texture.get();
			made.line.width = w;
			made.line.height = h;
			made.line.left = left;
			made.line.text_width = m_text.Measure(text, NameSprite::FontHeight, NameSprite::FontWeight).w;
		}
		it = m_name_textures.emplace(text, std::move(made)).first;
	}
	it->second.used_ms = now_ms;
	return it->second.line;
}

void FrontEnd::DrawNameSprite(Canvas &canvas, int x, int y, float depth, float focal, const std::string &text, uint32_t color)
{
	// A line is 20 texels of 0.05 units; the second (the guild's) 0.85 of that, under it, the lot
	// sitting on the head.
	// The 20 pixel bold font stretched to that, as eqgfx stretches its glyphs.
	float scale = NameSprite::UnitsPerTexel * focal / std::max(depth, NameSprite::NearestDepth);
	size_t newline = text.find('\n');
	std::string lines[2] = {text.substr(0, newline), newline == std::string::npos ? "" : text.substr(newline + 1)};
	float scales[2] = {scale, scale * NameSprite::LineScale};
	int widths[2] = {}, heights[2] = {};
	for (int i = 0; i < 2; ++i) {
		if (lines[i].empty())
			continue;
		TextSize size = m_text.Measure(lines[i], NameSprite::FontHeight, NameSprite::FontWeight);
		widths[i] = static_cast<int>(size.w * scales[i] + 0.5f);
		heights[i] = static_cast<int>(size.h * scales[i] + 0.5f);
	}
	y -= heights[0] + heights[1];
	for (int i = 0; i < 2; ++i) {
		if (lines[i].empty())
			continue;
		m_text.DrawScaled(canvas, x - widths[i] / 2, y, lines[i], NameSprite::FontHeight, scales[i], color,
			NameSprite::FontWeight);
		y += heights[i];
	}
}

void FrontEnd::EnterZoneScreen(bool zoning)
{
	m_screen = Screen::Zone;
	if (!zoning) {
		m_chat.Attach(nullptr);
		m_chat.Clear();
		m_game_values.target = 0;
	}
	m_time_of_day.clear();
	m_spell_book.Deactivate(); // a new zone starts standing, the book closed
	m_spell_cast.Reset();
	m_zone_view.reset();
	m_weather.Kill(m_sound.get()); // StopWorldDisplay
	m_zone_error_logged = false;
	m_debug.Reset();
	m_mouse_turn = m_mouse_pan = m_mouse_grabbed = false;
	m_keys = HeldKeys{};
	m_ui_hidden = false; // ProcessGameLoop: __ScreenMode 2 in the world
	m_hunger_ms = 0;     // a new ActorInfo: its UnknownTimer4 at 0
	m_autorun = false;
	m_jump_requested = m_crouch_sent = false;
	m_main_loop_started = false;
	m_tgb_sent = false;
	if (!zoning) {
		AddZoneLog("Entering the zone...", ZoneGrey);
		m_windows.Reset(nullptr, nullptr, m_game_width, m_game_height);
		m_windows_built = false;
		m_window_state.ClearCursor();
	}
	if (m_sound) // DoCharacterSelection, entering the world
		m_sound->SetMusicSelection(audio::EqSound::CharacterSelect, false);
}

bool FrontEnd::StartReplay(const std::string &path, std::string &error)
{
	if (!m_zone.BeginReplay(path, error))
		return false;
	EnterZoneScreen();
	AddZoneLog("Replaying " + path + " (no server).", ZoneGrey);
	return true;
}

namespace
{
	// The windows eqgame has up on a first login, and the rest of what we build so far.
	struct ZoneWindow
	{
		const char *screen;
		bool shown;
	};
	constexpr ZoneWindow ZoneWindows[] = {
		{"PlayerWindow", true}, {"TargetWindow", true}, {"GroupWindow", true}, {"BuffWindow", true},
		{"CastSpellWnd", true}, {"HotButtonWnd", true}, {"SelectorWindow", true}, {"ActionsWindow", false},
		{"InventoryWindow", false}, {"OptionsWindow", false}, {"FriendsWindow", false}, {"PetInfoWindow", false},
		{"HelpWindow", false}, {"CompassWindow", false}, {"SocialEditWnd", false}, {"ChatWindow", true},
		{"SkillsWindow", false}, {"ItemDisplayWindow", false}, {"QuantityWnd", false}, {"SpellBookWnd", false},
		{ColorPicker::Screen, false},
		{"ConfirmationDialogBox", false}, {"CastingWindow", false}, {"BankWnd", false},
		{"MerchantWnd", false}, {"LootWnd", false}, {"TradeWnd", false}, {"GiveWnd", false},
		{"SkillsSelectWindow", false}, {"NoteWindow", false}, {"BookWindow", false}, {"TrainWindow", false},
		{RaidWindow::Screen, false}, {RaidWindow::OptionsScreen, false},
	};

	// The selector's buttons (EQUI_SelectorWnd.xml) and the windows they open and close.
	struct SelectorToggle
	{
		const char *button, *screen;
	};
	constexpr SelectorToggle SelectorToggles[] = {
		{"SELW_ActionsToggleButton", "ActionsWindow"}, {"SELW_InventoryToggleButton", "InventoryWindow"},
		{"SELW_OptionsToggleButton", "OptionsWindow"}, {"SELW_FriendsToggleButton", "FriendsWindow"},
		{"SELW_HotboxToggleButton", "HotButtonWnd"}, {"SELW_CastSpellToggleButton", "CastSpellWnd"},
		{"SELW_PetInfoToggleButton", "PetInfoWindow"}, {"SELW_BuffToggleButton", "BuffWindow"},
		{"SELW_HelpToggleButton", "HelpWindow"},
	};
}

void FrontEnd::BuildZoneWindows()
{
	m_windows_built = true;
	if (!m_ui.definitions || !m_ui.textures)
		return;
	m_windows.SetStrings(m_ui.eqstr);
	m_windows.Reset(m_ui.definitions, m_ui.textures, m_game_width, m_game_height);
	m_windows.SetValues(&m_game_values);
	m_game_values.eqstr = m_ui.eqstr;
	m_game_values.spells = m_ui.spells;
	m_game_values.fv_rules = m_world.FvRules();
	m_game_values.auto_identify = m_world.AutoIdentify();
	for (const auto &window : ZoneWindows) {
		std::string error;
		if (!m_windows.Add(window.screen, window.shown, &error))
			Log("UI: " + error);
	}
	Containers::AddWindows(m_windows);
	// The sides their constructors let size them (CXWnd's 0x78).
	m_windows.SetResizeEdges("MerchantWnd", WindowManager::ResizeTop | WindowManager::ResizeBottom);
	m_windows.SetResizeEdges("LootWnd", WindowManager::ResizeTop | WindowManager::ResizeBottom);
	m_windows.SetResizeEdges("OptionsWindow", WindowManager::ResizeTop | WindowManager::ResizeBottom);
	m_windows.SetResizeEdges("SocialEditWnd", 0x44); // the sides (0x04 sizes nothing)
	// eqgame names the file after the character and the first 7 characters of the login
	// server's address (UI_Name_loginse.ini for loginserver.takproject.net).
	m_ui_layout_path.clear();
	m_hot_buttons_path.clear();
	m_window_state = GameWindowState{};
	DefaultSocials(m_window_state, m_ui.eqstr);
	const std::string &name = m_zone.Profile().name;
	if (!m_config.ui_dir.empty() && !name.empty()) {
		std::string suffix = name + "_" + m_config.login_host.substr(0, 7) + ".ini";
		m_ui_layout_path = m_config.ui_dir + "/UI_" + suffix;
		IniFile ini;
		if (ini.Load(m_ui_layout_path))
			m_windows.Load(ini);
		m_windows.Show("QuantityWnd", false); // only ever up for a stack
		m_windows.Show("SpellBookWnd", false); // open only while it's activated
		m_windows.Show("ConfirmationDialogBox", false);
		m_windows.Show("CastingWindow", false); // up only while we cast
		m_windows.Show(ColorPicker::Screen, false); // only while it's asked
		Log("UI: " + std::to_string(m_windows.Count()) + " windows, layout " + m_ui_layout_path);
		// CEverQuest::CreateIniFilenames: the character's own ini is the layout's name without UI_.
		m_hot_buttons_path = m_config.ui_dir + "/" + suffix;
	}
	Abilities::Host abilities_host;
	abilities_host.chat = [this](const std::string &text, int type) { ChatOut(text, type); };
	abilities_host.target = [this] { return m_game_values.target; };
	abilities_host.attacking = [this] { return m_attack; };
	abilities_host.casting = [this] { return m_spell_cast.Casting() && m_zone.Profile().class_id != 8; };
	abilities_host.hp = [this] { return m_game_values.CurHp(); };
	abilities_host.haste = [this] { return m_buffs.AttackSpeed(); };
	abilities_host.can_hit = [this](uint16_t id) { return CanIHitTarget(id); };
	abilities_host.melee_range = [this](uint16_t id) { return MeleeRange(id); };
	abilities_host.distance = [this](uint16_t id) { return GroundDistance(id); };
	abilities_host.heading = [this] { return m_zone.Profile().heading; };
	abilities_host.sense_heading = [this] { m_compass.SenseHeading(); };
	abilities_host.remove_affect = [this](int spa) { m_buffs.RemoveAffect(spa); };
	abilities_host.total = [this](int spa) { return m_buffs.Total(spa); };
	abilities_host.change_stance = [this](uint8_t stance) { ChangeStance(stance); };
	abilities_host.swimming = [this] { return m_zone_view && m_zone_view->Swimming(); };
	abilities_host.velocity = [this](float &x, float &y, float &z) {
		ZoneMovement m = m_zone_view ? m_zone_view->Movement() : ZoneMovement{};
		x = m.dx;
		y = m.dy;
		z = m.dz;
	};
	abilities_host.speed = [this] { return m_zone_view ? m_zone_view->Movement().speed : 0.0f; };
	abilities_host.animate = [this](int id, float speed) {
		if (m_zone_view)
			m_zone_view->PlayAnimation(id, speed);
	};
	abilities_host.hook_point = [this](float distance, float &x, float &y, float &z) {
		render::Vec3 at;
		if (!m_zone_view || !m_zone_view->FishingHook(distance, at))
			return false;
		x = at.x;
		y = at.y;
		z = at.z;
		return true;
	};
	abilities_host.sound_at = [this](float x, float y, float z, int slot) {
		if (m_zone_view)
			m_zone_view->PlaySoundAt(slot, {x, y, z});
	};
	m_abilities.Attach(m_windows, m_window_state, m_zone, m_ui.eqstr, std::move(abilities_host));
	m_abilities.Reset();
	if (!m_hot_buttons_path.empty()) {
		IniFile custom;
		if (custom.Load(m_hot_buttons_path)) {
			LoadHotButtons(custom, m_window_state);
			LoadSocials(custom, m_window_state);
			m_abilities.Load(custom);
		}
	}
	// CDisplay_ActivateMainUI activates the compass if it was up; Toggle Compass (Alt+D) opens and
	// closes it.
	m_compass.Activate(m_zone.Profile().skills[40]);
	m_color_picker.Attach(m_windows);
	// CRaidWnd and CRaidOptionsWnd made; CDisplay_ActivateMainUI activates the raid window if it was
	// up, for the raid's own (the options window only ever opens from it).
	RaidWindow::Host raid_window_host;
	raid_window_host.pick_color = [this](uint32_t color, std::function<void(uint32_t)> done) {
		m_color_picker.Activate(color, std::move(done));
	};
	// The class colors in the UI ini's [RaidWindow] (LoadStringFromUIIniFile, SaveToUIIniFile).
	raid_window_host.load = [this](const std::string &key) {
		IniFile ini;
		if (!m_ui_layout_path.empty())
			ini.Load(m_ui_layout_path);
		return ini.Get("RaidWindow", key);
	};
	raid_window_host.save = [this](const std::string &key, const std::string &value) {
		if (m_ui_layout_path.empty())
			return;
		IniFile ini;
		ini.Load(m_ui_layout_path);
		ini.Set("RaidWindow", key, value);
		if (!ini.Save(m_ui_layout_path))
			Log("UI: couldn't write " + m_ui_layout_path);
	};
	m_raid_window.Attach(m_windows, m_raid, m_game_values, std::move(raid_window_host));
	m_windows.Show(RaidWindow::OptionsScreen, false);
	if (m_windows.Shown(RaidWindow::Screen) && RaidAllowed())
		m_raid_window.Activate();
	else
		m_windows.Show(RaidWindow::Screen, false);
	m_windows.TakeChanged();
	SpellBook::Host host;
	host.change_stance = [this](uint8_t stance) { ChangeStance(stance); };
	host.moving_forward = [this] { return m_zone_view && m_zone_view->Movement().speed > 0; };
	host.chat = [this](const std::string &text, int type) { ChatOut(text, type); };
	SpellCast::Host cast_host;
	cast_host.target = [this] { return m_game_values.target; };
	cast_host.set_target = [this](uint16_t id) { m_game_values.target = id; };
	cast_host.can_see = [this](uint16_t id) { return CanSee(id); };
	cast_host.chat = [this](const std::string &text, int type) { ChatOut(text, type); };
	m_spell_cast.Attach(m_windows, m_zone, m_ui.spells, m_ui.eqstr, std::move(cast_host));
	m_buffs.Attach(m_zone, m_ui.spells, m_ui.eqstr, [this](const std::string &text, int type) { ChatOut(text, type); });
	m_book.Attach(m_windows, m_text);
	m_game_values.buffs = &m_buffs;
	m_spell_book.Attach(m_windows, m_window_state, m_zone, m_game_values, m_spell_cast, m_ui.spells, m_ui.eqstr,
		std::move(host));
	m_spell_book.Reset();
	m_game_values.spell_book = &m_spell_book;
	m_game_values.spell_cast = &m_spell_cast;
	// CChatManager::CChatManager: the main chat window is titled "Main Chat" (eqstr 6623), with
	// no close or minimize box.
	if (SidlWindow *chat = m_windows.Find("ChatWindow")) {
		chat->Root().text = EqString(6623, "Main Chat");
		chat->RemoveTitleBoxes();
	}
	m_chat.Attach(m_windows.Find("ChatWindow"));
	m_chat.SetLinkColor(m_chat_colors.Get(ItemTags::ColorType));
	auto bag_sound = [this](int wave) {
		if (m_sound)
			m_sound->PlayWave(wave);
	};
	Containers::Host bag_host;
	bag_host.sound = bag_sound;
	bag_host.chat = [this](const std::string &text, int type) { ChatOut(text, type); };
	m_containers.Attach(m_windows, m_zone, m_game_values, std::move(bag_host)); // the bag windows hidden
	BankWindow::Host bank_host;
	bank_host.chat = [this](const std::string &text, int type) { ChatOut(text, type); };
	bank_host.save = [this] { SavePC(); };
	bank_host.close_bags = [this] { m_containers.CloseAll(); };
	bank_host.music = [this](bool on) {
		if (m_sound)
			m_sound->SetMusicSelection(audio::EqSound::Bank, on);
	};
	bank_host.make_visible = [this] { m_abilities.MakeMeVisible(); };
	m_bank.Attach(m_windows, m_zone, m_window_state, m_game_values, m_ui.eqstr, std::move(bank_host)); // hidden too
	MerchantWindow::Host merchant_host;
	merchant_host.chat = [this](const std::string &text, int type) { ChatOut(text, type); };
	merchant_host.save = [this] { SavePC(); };
	merchant_host.sound = bag_sound;
	merchant_host.close_bag = [this](int16_t slot) { m_containers.Close(slot); };
	merchant_host.clear_target = [this] { m_game_values.target = 0; }; // pinstTarget, not the zone's
	merchant_host.music = [this](bool on) {
		if (m_sound)
			m_sound->SetMusicSelection(audio::EqSound::Merchant, on);
	};
	m_merchant.Attach(m_windows, m_zone, m_window_state, m_game_values, std::move(merchant_host));
	TrainWindow::Host train_host;
	train_host.chat = [this](const std::string &text, int type) { ChatOut(text, type); };
	train_host.music = [this](bool on) {
		if (m_sound)
			m_sound->SetMusicSelection(audio::EqSound::Trainer, on);
	};
	train_host.clear_target = [this] { m_game_values.target = 0; }; // pinstTarget
	m_train.Attach(m_windows, m_zone, m_ui.eqstr, std::move(train_host));
	LootWindow::Host loot_host;
	loot_host.chat = [this](const std::string &text, int type) { ChatOut(text, type); };
	loot_host.save = [this] { SavePC(); };
	loot_host.sound = bag_sound;
	loot_host.change_stance = [this](uint8_t stance) { ChangeStance(stance); };
	loot_host.clear_target = [this] { m_game_values.target = 0; };
	loot_host.height = [this](uint16_t id) { return m_zone_view ? m_zone_view->CenterHeight(id) : 0.0f; };
	loot_host.dead = [this] { return m_dead; };
	loot_host.make_visible = [this] { m_abilities.MakeMeVisible(); };
	m_loot.Attach(m_windows, m_zone, m_window_state, m_game_values, std::move(loot_host));
	m_loot.loot_no_drop = m_config.loot_no_drop;
	TradeWindow::Host trade_host;
	trade_host.chat = [this](const std::string &text, int type) { ChatOut(text, type); };
	trade_host.save = [this] { SavePC(); };
	trade_host.set_target = [this](uint16_t id) { m_game_values.target = id; };
	trade_host.height = [this](uint16_t id) { return m_zone_view ? m_zone_view->CenterHeight(id) : 0.0f; };
	// msg_req_trade: busy too while auto attack's on.
	trade_host.ok_to_transact = [this] { return IsOkToTransact() && !m_attack; };
	trade_host.refusal = [this](const std::string &name) -> uint8_t {
		// msg_req_trade: 'b' with no one; 'c' with party members only, unless they're in ours.
		if (m_trade_with == 0)
			return 'b';
		if (m_trade_with == 1 && !(m_group.InGroup() && m_group.IsAGroupMember(name)))
			return 'c';
		return 0;
	};
	m_trade.Attach(m_windows, m_zone, m_window_state, m_game_values, std::move(trade_host));
	GroundItems::Host ground_host;
	ground_host.chat = [this](const std::string &text, int type) { ChatOut(text, type); };
	ground_host.open_inventory = [this] { m_windows.Show("InventoryWindow", true); };
	ground_host.world_container_open = [this] { return m_containers.WorldOpen(); };
	ground_host.open_world_container = [this](const ZoneWorldContainer &world) { m_containers.OpenWorld(world, m_now_ms); };
	m_ground.Attach(m_windows, m_zone, m_window_state, m_game_values, std::move(ground_host));
	m_ground.fast_drop = m_config.fast_drop;
	OptionsWindow::Host options_host;
	options_host.eqstr = [this](uint32_t id, const std::string &fallback) { return EqString(id, fallback); };
	options_host.chat = [this](const std::string &text, int type) { ChatOut(text, type); };
	options_host.saved = [this](int id, bool alternate, uint32_t key) { SaveKeyBinding(id, alternate, key); };
	options_host.reset = [this] { ResetKeyBindings(); };
	options_host.user_color = [this](int index) { return m_chat_colors.User(index); };
	options_host.save_color = [this](int index, uint32_t rgb) { SaveUserColor(index, rgb); };
	options_host.pick_color = [this](uint32_t color, std::function<void(uint32_t)> done) {
		m_color_picker.Activate(color, std::move(done));
	};
	options_host.general = [this](OptionsWindow::Host::General setting) { return GeneralOption(setting); };
	options_host.set_general = [this](OptionsWindow::Host::General setting, int value) {
		SetGeneralOption(setting, value);
	};
	options_host.display = [this](OptionsWindow::Host::Display setting) { return DisplayOption(setting); };
	options_host.set_display = [this](OptionsWindow::Host::Display setting, float value) {
		SetDisplayOption(setting, value);
	};
	options_host.filters = &m_filters;
	options_host.filters_changed = [this] { FiltersChanged(); };
	m_options.Attach(m_windows, m_key_bindings, std::move(options_host));
	m_windows.TakeChanged();
}

void FrontEnd::SaveUiLayout()
{
	if (m_ui_layout_path.empty())
		return;
	IniFile ini;
	ini.Load(m_ui_layout_path); // keep what we don't manage
	m_windows.Store(ini);
	if (!ini.Save(m_ui_layout_path))
		Log("UI: couldn't write " + m_ui_layout_path);
}

void FrontEnd::SaveCustom()
{
	m_window_state.hot_buttons_changed = m_window_state.socials_changed = false;
	if (m_hot_buttons_path.empty())
		return;
	IniFile ini;
	ini.Load(m_hot_buttons_path); // keep the rest of the character's ini
	StoreHotButtons(ini, m_window_state);
	StoreSocials(ini, m_window_state);
	m_abilities.Store(ini);
	if (!ini.Save(m_hot_buttons_path))
		Log("UI: couldn't write " + m_hot_buttons_path);
}

void FrontEnd::SaveDebugSettings()
{
	if (m_config.settings_ini.empty())
		return;
	IniFile ini;
	ini.Load(m_config.settings_ini); // keep the host's settings
	m_debug.Store(ini);
	if (!ini.Save(m_config.settings_ini))
		Log("debug menu: couldn't write " + m_config.settings_ini);
}

bool FrontEnd::BuffSpellInfo(const std::string &control, const std::string &window)
{
	// CBuffWindow::HandleSpellInfoDisplay: a buff of a spell we know toggles the item display with
	// the spell's name and icon only (SetSpell's second argument 0).
	if (window != "BuffWindow" || control.rfind("Buff", 0) != 0)
		return false;
	int slot = std::atoi(control.c_str() + 4);
	if (m_buffs.SlotSpell(slot))
		ToggleSpellDisplay(m_windows, m_game_values, m_window_state, m_buffs.Slot(slot).spell, false);
	return true;
}

void FrontEnd::DestroyHeld()
{
	// CInventoryWnd_DestroyHeld (@00421637): with FastItemDestroy off, the confirmation dialog
	// (category 5, 10 s) asks about the item on the cursor (eqstr 2043) or the coins (2061).
	const ZoneProfile &me = m_zone.Profile();
	auto held = m_zone.Items().find(Inventory::Cursor);
	if (m_fast_item_destroy) {
		DestroyHeldItemOrMoney();
		return;
	}
	std::string question;
	if (held != m_zone.Items().end())
		question = FormatEqString(m_ui.eqstr, 2043, "Are you absolutely sure you want to destroy your %1?", {held->second.name});
	else if (Inventory::CursorMoneyType(me))
		question = EqString(2061, "Are you absolutely sure you want to destroy the money you are holding?");
	else
		return;
	SidlWindow *dialog = m_windows.Find("ConfirmationDialogBox");
	if (!dialog || m_windows.Shown("ConfirmationDialogBox"))
		return;
	for (const char *id : {"Yes_Button", "No_Button"})
		if (SidlWindow::Control *button = dialog->Find(id))
			button->visible = true;
	if (SidlWindow::Control *ok = dialog->Find("OK_Button"))
		ok->visible = false;
	if (SidlWindow::Control *text = dialog->Find("TextOutput")) {
		text->text = question;
		text->scroll_pos = 0;
	}
	const Rect &r = dialog->Bounds();
	m_windows.MoveTo("ConfirmationDialogBox", (m_windows.ScreenWidth() - r.w) / 2, (m_windows.ScreenHeight() - r.h) / 2);
	m_windows.Show("ConfirmationDialogBox", true);
	m_windows.Raise("ConfirmationDialogBox");
	m_destroy_until_ms = m_now_ms + 10000;
}

bool FrontEnd::AskInGame(const std::string &text, uint32_t timeout_ms, std::function<void(bool)> answer)
{
	SidlWindow *dialog = m_windows.Find("ConfirmationDialogBox");
	if (!dialog || m_windows.Shown("ConfirmationDialogBox"))
		return false;
	for (const char *id : {"Yes_Button", "No_Button"})
		if (SidlWindow::Control *button = dialog->Find(id))
			button->visible = true;
	if (SidlWindow::Control *ok = dialog->Find("OK_Button"))
		ok->visible = false;
	if (SidlWindow::Control *output = dialog->Find("TextOutput")) {
		output->text = text;
		output->scroll_pos = 0;
	}
	const Rect &r = dialog->Bounds();
	m_windows.MoveTo("ConfirmationDialogBox", (m_windows.ScreenWidth() - r.w) / 2, (m_windows.ScreenHeight() - r.h) / 2);
	m_windows.Show("ConfirmationDialogBox", true);
	m_windows.Raise("ConfirmationDialogBox");
	m_question.open = true;
	m_question.until_ms = m_now_ms + timeout_ms;
	m_question.answer = std::move(answer);
	return true;
}

void FrontEnd::AnswerInGame(bool yes)
{
	if (!m_question.open)
		return;
	m_question.open = false;
	m_windows.Show("ConfirmationDialogBox", false);
	auto answer = std::move(m_question.answer);
	m_question.answer = nullptr;
	if (answer)
		answer(yes);
}

void FrontEnd::DestroyHeldItemOrMoney()
{
	// EQ_PC::DestroyHeldItemOrMoney (@004d0d88): the item on the cursor goes (OP_MoveItem from the
	// cursor to -1, no count), or the biggest coin's pile (OP_MoveCoin from the cursor to -1) and
	// every coin on the cursor with it.
	std::map<int16_t, ZoneItem> &items = m_zone.EditItems();
	if (auto held = items.find(Inventory::Cursor); held != items.end()) {
		items.erase(held);
		m_zone.MoveItem(Inventory::Cursor, -1, 0);
		return;
	}
	ZoneProfile &me = m_zone.EditProfile();
	int32_t coins[4] = {me.cursor_copper, me.cursor_silver, me.cursor_gold, me.cursor_platinum};
	for (int coin = 3; coin >= 0; --coin)
		if (coins[coin] > 0) {
			m_zone.MoveCoin(0, -1, coin, coin, coins[coin]);
			me.cursor_platinum = me.cursor_gold = me.cursor_silver = me.cursor_copper = 0;
			return;
		}
}

void FrontEnd::DrawBuffTooltips(Canvas &canvas, int mouse_x, int mouse_y)
{
	// CBuffWindow::PostDraw (@004095fe): with the mouse over the buff window, each button's tooltip
	// by its top right, 5 in, each column's a little further left than the last (its number times
	// the button's width over the columns) and down 15 a column, back up every third.
	if (m_windows.ScreenAt(mouse_x, mouse_y) != "BuffWindow")
		return;
	const SidlWindow *wnd = m_windows.Find("BuffWindow");
	const SidlWindow::Control *first = wnd ? wnd->Find("Buff0") : nullptr;
	if (!first || wnd->Minimized())
		return;
	const Rect &c = wnd->Root().client;
	int w = first->def->rect.w + 1, h = first->def->rect.h + 1;
	int columns = c.w / w, rows = c.h / h;
	if (columns <= 0)
		return;
	int step = w / columns, n = 0;
	for (int column = 0; column < columns; ++column)
		for (int row = 0; row < rows && n < Buffs::Slots; ++row, ++n) {
			const SidlWindow::Control *button = wnd->Find("Buff" + std::to_string(n));
			if (button && !button->tooltip.empty())
				m_windows.DrawTooltip(canvas, m_text, button->tooltip, button->def->font,
					button->rect.Right() - column * step - 5, (column % 3) * 15 + button->rect.y, false);
		}
}

void FrontEnd::HandleWindowClicks()
{
	// Nothing goes on the cursor over an item or coins (CCursorAttachment::IsOkToActivate).
	for (const auto &hold : m_windows.TakeHolds())
		if (!m_zone.Items().count(Inventory::Cursor) && !Inventory::CursorMoneyType(m_zone.Profile()))
			GameWindowHold(m_windows, m_window_state, hold.window, hold.control);
	for (const auto &click : m_windows.TakeRightClicks()) {
		if (m_spell_book.RightClick(click.window, click.control))
			continue;
		if (m_abilities.RightClick(click.window, click.control)) // the skill select window
			continue;
		// A corpse's slot: into the inventory (RequestLootSlot's auto loot).
		if (m_loot.RightClick(click.window, click.control, m_now_ms))
			continue;
		// CInvSlot::HandleRButtonUp: a bag's window opens or closes, with no Shift, Ctrl or Alt
		// down, nothing waiting on the zone and no spell being cast (but a bard's song).
		// Under 1000, a common item in the cursor, worn or general slots is eaten, drunk or clicked
		// (CInvSlot_DoDrinkEatPoison).
		if (int slot = InvSlotAt(m_windows, click.window, click.control); slot >= 0) {
			if (!click.shift && !click.ctrl && !click.alt && !m_window_state.busy &&
			    (!m_spell_cast.Casting() || m_zone.Profile().class_id == 8))
				if (const ZoneItem *item = SlotItem(m_windows, m_game_values, click.window, click.control)) {
					if (item->item_class == 1)
						m_containers.Toggle(static_cast<int16_t>(slot));
					else if (item->item_class == 2 && slot < 1000)
						ReadNote(*item, static_cast<int16_t>(slot));
					else if (item->item_class == 0 && slot < 31) {
						ZoneItem copy = *item; // what it was: eating may take the last of it
						DoDrinkEatPoison(copy, static_cast<int16_t>(slot));
					}
				}
			continue;
		}
		GameWindowRightClick(m_windows, m_window_state, click.window, click.control);
	}
	// The right button held: a slot's item, a book slot's, a gem's or a buff's spell in the item
	// display; let go, it closes (WndNotification's 0x17 and 0x19).
	for (const auto &hold : m_windows.TakeRightHolds()) {
		if (m_spell_book.RightHold(hold.window, hold.control) || BuffSpellInfo(hold.control, hold.window))
			continue;
		ShowItemDisplay(m_windows, m_game_values, m_window_state, hold.window, hold.control);
	}
	for (const auto &release : m_windows.TakeRightReleases()) {
		if (m_spell_book.RightRelease(release.window, release.control) || BuffSpellInfo(release.control, release.window))
			continue;
		GameWindowRightRelease(m_windows, release.window, release.control);
	}
	for (WindowManager::Click click : m_windows.TakeClicks()) {
		if (click.window == "SkillsSelectWindow" && m_abilities.Click(click.window, click.control, m_now_ms))
			continue;
		if (m_color_picker.Click(click.window, click.control))
			continue;
		// The Options window's keyboard page, and the question about its defaults.
		if (m_options.Click(click.window, click.control, m_now_ms))
			continue;
		// Alt+click on a slot shows its item and does nothing else (CInvSlot::HandleLButtonUp).
		if (click.alt && ShowItemDisplay(m_windows, m_game_values, m_window_state, click.window, click.control))
			continue;
		// Shift+click on an item while typing in the chat links it there, with nothing on the
		// cursor (ours; eqgame links from the item display's icon only).
		if (click.shift && m_chat.Typing() && !m_zone.Items().count(Inventory::Cursor))
			if (const ZoneItem *item = SlotItem(m_windows, m_game_values, click.window, click.control)) {
				m_chat.AddItemTag(item->id, item->name);
				continue;
			}
		// The item display's icon links its item in the chat (CItemDisplayWnd::WndNotification).
		if (click.window == "ItemDisplayWindow" && click.control == "IconButton") {
			if (m_window_state.display_item.id && m_chat.Attached()) {
				m_windows.Show("ChatWindow", true);
				m_chat.AddItemTag(m_window_state.display_item.id, m_window_state.display_item.name);
			}
			continue;
		}
		// A corpse's slots and Done, and the confirmation dialog when it's asking about a NO DROP
		// item, or about dropping one on the ground.
		if (m_loot.Click(click.window, click.control, m_now_ms) || m_ground.Click(click.window, click.control))
			continue;
		// The inventory's Destroy, and the confirmation dialog asking about it.
		if (click.window == "InventoryWindow" && click.control == "IW_Destroy") {
			DestroyHeld();
			continue;
		}
		if (click.window == "ConfirmationDialogBox" && m_question.open) {
			if (click.control == "Yes_Button" || click.control == "No_Button" || click.control == "OK_Button")
				AnswerInGame(click.control == "Yes_Button");
			continue;
		}
		if (click.window == "ConfirmationDialogBox" && m_destroy_until_ms) {
			if (click.control == "Yes_Button") // ProcessYes's 5
				DestroyHeldItemOrMoney();
			if (click.control == "Yes_Button" || click.control == "No_Button" || click.control == "OK_Button") {
				m_destroy_until_ms = 0;
				m_windows.Show("ConfirmationDialogBox", false);
			}
			continue;
		}
		// The spellbook, the gems and the confirmation dialog.
		if (m_spell_book.Click(click.window, click.control, click.alt, m_now_ms))
			continue;
		// A bag window's Done and Combine, the bank's Done, the merchant's Buy, Sell and Done.
		if (m_containers.Click(click.window, click.control) || m_bank.Click(click.window, click.control) ||
		    m_merchant.Click(click.window, click.control, click.shift, click.ctrl) || m_train.Click(click.window, click.control) ||
		    m_raid_window.Click(click.window, click.control))
			continue;
		// While a merchant's up, a slot anywhere is picked to buy or sell (CInvSlot::HandleLButtonUp).
		if (m_merchant.SlotClick(click.window, click.control))
			continue;
		// The trade and give windows' buttons.
		if (m_trade.Click(click.window, click.control))
			continue;
		// The inventory's slots and character view, the money buttons, and the quantity window's Accept.
		InventoryClickResult moved;
		if (MoneyClick(m_windows, m_window_state, m_zone, click.window, click.control, click.shift, click.ctrl, moved) ||
		    InventoryClick(m_windows, m_window_state, m_zone, click.window, click.control, click.shift, click.ctrl, moved)) {
			if (moved.sound && m_sound)
				m_sound->PlayWave(0x92);
			if (moved.coin_sound && m_sound)
				m_sound->PlayWave(0x8a);
			if (moved.message)
				ChatOut(EqString(moved.message, Inventory::MessageText(moved.message)), moved.message_color);
			if (moved.trade_changed)
				m_trade.ItemChanged();
			if (moved.close_bag >= 0)
				m_containers.Close(moved.close_bag);
			// The quantity window's edit box takes the keys from the others.
			if (m_windows.Shown("QuantityWnd")) {
				m_chat.Close();
				if (SidlWindow *social_editor = m_windows.Find("SocialEditWnd"))
					social_editor->Focus("");
			}
			continue;
		}
		// CNoteWnd's Done.
		if (m_book.Click(click.window, click.control))
			continue;
		if (click.window == "NoteWindow" && click.control == "DoneButton") {
			m_windows.Show("NoteWindow", false);
			continue;
		}
		// CGroupWnd::WndNotification: Follow, Invite, and Disband or Decline.
		if (click.window == "GroupWindow") {
			if (click.control == "FollowButton")
				m_group.Accept();
			else if (click.control == "InviteButton")
				m_group.Invite();
			else if (click.control == "DisbandButton" || click.control == "DeclineButton")
				m_group.Disband();
		}
		// A gauge clicked: setTargetBasedOnIndex by its EQType (us, a member, a pet).
		if (SidlWindow *wnd = m_windows.Find(click.window))
			if (const SidlWindow::Control *gauge = wnd->Find(click.control); gauge && gauge->def->type == "Gauge") {
				m_group.TargetByGauge(gauge->def->eq_type);
				continue;
			}
		// CBuffWindow::WndNotification: a buff clicked comes off; with Alt its spell shows.
		if (click.window == "BuffWindow" && click.control.rfind("Buff", 0) == 0) {
			if (m_alt_down || click.alt)
				BuffSpellInfo(click.control, click.window);
			else
				m_buffs.ClickOff(std::atoi(click.control.c_str() + 4));
			continue;
		}
		// A hot button holding one of the Actions window's buttons does what that button does.
		std::string action = GameWindowClick(m_windows, m_window_state, click.window, click.control);
		if (!action.empty())
			click = {"ActionsWindow", action};
		if (click.window == "SelectorWindow")
			for (const auto &toggle : SelectorToggles)
				if (click.control == toggle.button)
					m_windows.Toggle(toggle.screen);
		if (click.window == "ActionsWindow")
			ActionsClick(click.control);
	}
	if (m_abilities.TakeChanged() | m_window_state.hot_buttons_changed | m_window_state.socials_changed)
		SaveCustom();
	// The selector's buttons stay down while their window is open.
	if (SidlWindow *selector = m_windows.Find("SelectorWindow"))
		for (const auto &toggle : SelectorToggles)
			if (SidlWindow::Control *button = selector->Find(toggle.button))
				button->checked = m_windows.Shown(toggle.screen);
}

void FrontEnd::ActionsClick(const std::string &control)
{
	// The abilities and combat pages' skill buttons (and their hot buttons).
	if (m_abilities.Click("ActionsWindow", control, m_now_ms))
		return;
	// The Actions window's main page: what the slash commands do.
	if (control == "AMP_CampButton")
		InterpretCommand("/camp");
	else if (control == "AMP_SitButton" || control == "AMP_StandButton")
		Sit();
	else if (control == "AMP_RunButton" || control == "AMP_WalkButton") {
		m_run = control == "AMP_RunButton";
		AddZoneLog(m_run ? "You are now running." : "You are now walking.", ZoneGrey);
	}
	// The combat page's Attack button (CombatPageWndNotification: ExecuteCmd 8).
	else if (control == "ACP_MeleeAttackButton")
		ToggleAutoAttack();
	else if (control == "ACP_RangeAttackButton")
		RangeAttackButton();
	// MainPageWndNotification: Invite only when not invited, Follow only when invited, Disband.
	else if (control == "AMP_InviteButton") {
		if (!m_group.Invited())
			m_group.Invite();
	}
	else if (control == "AMP_FollowButton") {
		if (m_group.Invited())
			m_group.Accept();
	}
	else if (control == "AMP_DisbandButton")
		m_group.Disband();
	else if (control == "AMP_WhoButton")
		Who(""); // MainPageWndNotification (not while DAT_00798560, a flag not known)

}

void FrontEnd::AddZoneLog(const std::string &text, uint32_t color)
{
	m_chat.Add(text, color);
}

void FrontEnd::ChatOut(const std::string &text, int type)
{
	// print_chat_dsp_chat: DoPercentConvert first.
	m_chat.Add(PercentConvert(text), m_chat_colors.Get(type));
}

void FrontEnd::SetTarget(uint16_t id, bool command)
{
	if (!command)
		m_game_values.target = id;
	m_debug.SelectTarget(id);
	m_zone.SetTarget(id, command);
}

bool FrontEnd::InvisibleToMe(const ZoneSpawn &spawn) const
{
	// A player or an NPC alive: an untargetable body type is, and one invisible (VisibilityState
	// 1) is unless we see invisible (EQ_Character::CanISeeInvis, which we don't have: we don't),
	// and a GM's is to all but GMs.
	if (spawn.npc >= 2 || spawn.hp <= 0)
		return false;
	if (spawn.body_type > 0x40)
		return true;
	return spawn.invis == 1;
}

uint16_t FrontEnd::NearestInView(float range, bool npc) const
{
	if (!m_zone_view)
		return 0;
	uint16_t best = 0;
	float best_distance = 0;
	uint16_t me = m_zone.MySpawnId();
	for (const ZoneView::ViewedSpawn &seen : m_zone_view->SpawnsInView(range)) {
		auto it = m_zone.Spawns().find(seen.id);
		if (seen.id == me || it == m_zone.Spawns().end())
			continue;
		const ZoneSpawn &spawn = it->second;
		if (spawn.npc != (npc ? 1 : 0))
			continue;
		if (npc && spawn.pet_owner) {
			// Not a player's pet (ours included).
			auto owner = m_zone.Spawns().find(spawn.pet_owner);
			if (spawn.pet_owner == me || (owner != m_zone.Spawns().end() && owner->second.npc == 0))
				continue;
		}
		// Heard from in the last 12 seconds, moving, or an NPC standing still; not invisible to us.
		// (An NPC with a rider, a mount, is passed over too; we don't know riders.)
		bool current = m_now_ms - spawn.updated_ms < 12001 || spawn.speed != 0 || npc;
		if (!current || InvisibleToMe(spawn))
			continue;
		if (seen.from_camera <= range && (!best || seen.from_camera < best_distance) && seen.camera_clear) {
			best = seen.id;
			best_distance = seen.from_camera;
		}
	}
	return best;
}

void FrontEnd::TargetNearest(bool npc)
{
	uint16_t id = NearestInView(500, npc);
	if (!id)
		return;
	const ZoneSpawn &spawn = m_zone.Spawns().at(id);
	// ExecuteCmd 29: an NPC of an untargetable body type (11, 60, 65, 67) below race 2253 leaves
	// the target as it was. A horse (race 216) would give its rider; we don't know riders.
	if (npc && spawn.race < 0x8cd &&
		(spawn.body_type == 11 || spawn.body_type == 60 || spawn.body_type == 65 || spawn.body_type == 67))
		return;
	SetTarget(id);
}

void FrontEnd::CycleNearestTarget()
{
	// Zeal's CycleTarget::get_next_ent(250, NPC): three seconds since the last press starts again
	// at the nearest.
	if (!m_zone_view)
		return;
	if (m_now_ms - m_cycle_ms > 3000)
		m_cycle_index = -1;
	m_cycle_ms = m_now_ms;
	uint16_t me = m_zone.MySpawnId();
	struct Near
	{
		uint16_t id;
		float distance;
	};
	std::vector<Near> near;
	for (const ZoneView::ViewedSpawn &seen : m_zone_view->SpawnsInView(250)) {
		auto it = m_zone.Spawns().find(seen.id);
		if (seen.id == me || it == m_zone.Spawns().end())
			continue;
		const ZoneSpawn &spawn = it->second;
		// get_world_visible_actor_list(250, true): within 250 of us, level, not invisible to us,
		// and a line to it from our head or feet.
		if (spawn.npc != 1 || spawn.level == 0 || seen.from_me_2d > 250 || !seen.me_clear || InvisibleToMe(spawn))
			continue;
		// A pet only of an NPC (or of no one we know); a mount (a player its rider) we can't tell.
		if (spawn.pet_owner) {
			auto owner = m_zone.Spawns().find(spawn.pet_owner);
			bool npc_owner = owner != m_zone.Spawns().end() && (owner->second.npc == 1 || owner->second.npc == 3);
			if (spawn.pet_owner == me || (owner != m_zone.Spawns().end() && !npc_owner))
				continue;
		}
		near.push_back({seen.id, seen.from_me});
	}
	if (near.empty())
		return;
	std::stable_sort(near.begin(), near.end(), [](const Near &a, const Near &b) { return a.distance < b.distance; });
	auto advance = [&] {
		if (++m_cycle_index >= static_cast<int>(near.size()))
			m_cycle_index = 0;
	};
	advance();
	if (near[m_cycle_index].id == m_game_values.target)
		advance();
	SetTarget(near[m_cycle_index].id);
}

void FrontEnd::ToggleTarget()
{
	if (m_chat.Typing()) // UI_ChatInputCheck() == 2
		return;
	uint16_t me = m_zone.MySpawnId();
	if (m_game_values.target == me) {
		if (m_toggle_target && m_zone.Spawns().count(m_toggle_target))
			SetTarget(m_toggle_target);
		return;
	}
	if (m_game_values.target)
		m_toggle_target = m_game_values.target;
	SetTarget(me);
}

void FrontEnd::AnswerTellAfk(const std::string &sender, const std::string &text)
{
	// msg_new_text's tells (channels 7 and 14) while we're AFK: a tell back (OP_ChannelMessage 7)
	// with "AFK Message: " (eqstr 1418) and /afk's message, or eqstr 1419 without one; not to a
	// tell that's someone's answer (starting with 1417 or 1418), so two AFK players don't loop.
	if (!m_zone.Self().afk)
		return;
	std::string sorry = EqString(1417, "Sorry, I am A.F.K."), label = EqString(1418, "AFK Message: ");
	if (text.compare(0, sorry.size(), sorry) == 0 || text.compare(0, label.size(), label) == 0)
		return;
	m_zone.SendChannel(7, sender,
		m_afk_message.empty() ? EqString(1419, "Sorry, I am A.F.K. (Away From Keyboard) right now.") : label + m_afk_message);
}

void FrontEnd::AddNameToTellList(const std::string &name)
{
	auto same = [&](const std::string &other) { return Lower(other) == Lower(name); };
	m_tell_list.erase(std::remove_if(m_tell_list.begin(), m_tell_list.end(), same), m_tell_list.end());
	m_tell_list.insert(m_tell_list.begin(), name);
	if (m_tell_list.size() > 50)
		m_tell_list.resize(50);
	m_tell_index = 0;
}

void FrontEnd::CycleReply(bool back)
{
	if (!m_chat.Typing())
		return;
	// The line's start picks the command: eqstr 13974 "/tell", 14202 "/rtarget", or ";tell".
	std::string line = Lower(m_chat.InputText());
	std::string tell = Lower(EqString(13974, "/tell")), rtarget = Lower(EqString(14202, "/rtarget"));
	std::string command;
	if (line.compare(0, tell.size(), tell) == 0)
		command = EqString(13974, "/tell");
	else if (line.compare(0, rtarget.size(), rtarget) == 0)
		command = EqString(14202, "/rtarget");
	else if (line.compare(0, 5, ";tell") != 0)
		return;
	if (m_tell_list.empty()) {
		// CycleNamesInTellList, then ExecuteCmd: eqstr 12364 (12365 for /rtarget).
		uint32_t id = command == EqString(14202, "/rtarget") ? 12365 : 12364;
		ChatOut(EqString(id, id == 12364 ? "You have not received any tells, so you cannot reply."
		                                 : "You have not received any tells, so you cannot use reply target."),
			ChatType::Default);
		return;
	}
	int count = static_cast<int>(m_tell_list.size());
	m_tell_index = back ? (m_tell_index > 0 ? m_tell_index - 1 : count - 1) : (m_tell_index + 1 < count ? m_tell_index + 1 : 0);
	const std::string &name = m_tell_list[m_tell_index];
	// "/rtarget <name> "; otherwise "/tell <name> ", or the chat server's ";tell <name> " for a
	// name with a dot (another server's).
	if (command == EqString(14202, "/rtarget"))
		m_chat.Open(command + " " + name + " ");
	else if (name.find('.') != std::string::npos)
		m_chat.Open(";tell " + name + " ");
	else
		m_chat.Open(EqString(13974, "/tell") + " " + name + " ");
}

void FrontEnd::OpenChatLine(const std::string &text)
{
	// The active chat window's input box set to it and given the focus.
	if (!m_chat.Attached())
		return;
	m_windows.Show("ChatWindow", true);
	m_chat.Open(text);
	m_chat_opened_by_key = true;
}

void FrontEnd::DoDrinkEatPoison(const ZoneItem &item, int16_t slot)
{
	// CInvSlot_DoDrinkEatPoison (@004223ca): by the item's type, food (14), drink (15), alcohol (38),
	// a poison (42: not ours yet), else its click effect.
	auto text = [this](uint32_t id, const char *fallback, const std::vector<std::string> &args = {}) {
		return FormatEqString(m_ui.eqstr, id, fallback, args);
	};
	ZoneProfile &me = const_cast<ZoneProfile &>(m_zone.Profile());
	switch (item.item_type) {
	case 42: // CEverQues_ApplyPoison
		return;
	case 14:
	case 15: {
		bool food = item.item_type == 14;
		int32_t &level = food ? me.hunger : me.thirst;
		if (level > 5999) {
			ChatOut(food ? text(12074, "You could not possibly eat any more, you would explode!")
			             : text(12077, "You could not possibly drink any more, you would explode!"),
				ChatType::Default);
			return;
		}
		ChatOut(food ? text(12075, "You take a bite out of a %1.", {item.name}) : text(12078, "You take a drink from an %1.", {item.name}),
			ChatType::Default);
		level = std::min(level + item.cast_time * 100 / 2, 6000);
		if (!food && m_sound)
			m_sound->PlayWave(0x95);
		ChatOut(food ? text(12076, "Ahhh. That was tasty.") : text(12079, "Ahhh. That was refreshing."), ChatType::Default);
		m_zone.Consume(slot, food ? 1 : 2);
		return;
	}
	case 38: {
		if (me.drunk > 199) {
			ChatOut(text(12080, "You could not possibly consume more alcohol or become more intoxicated!"), ChatType::Default);
			return;
		}
		ChatOut(text(12081, "Glug, glug, glug...  You take a swig of %1.", {item.name}), ChatType::Default);
		// Alcohol Tolerance (66) takes the edge off: a roll under the skill (and under 96) of 40.
		int more = 40;
		if (Skills::HasSkill(me, 66) != 0) {
			int roll = std::rand() % 100 + 1;
			int skill = me.skills[66];
			if (roll <= skill && roll < 96 && skill > 0)
				more = roll * 40 / skill;
		}
		me.drunk = static_cast<int16_t>(std::min(me.drunk + more, 200));
		m_zone.DeleteCharge(slot);
		ChatOut(text(12079, "Ahhh. That was refreshing."), ChatType::Default);
		return;
	}
	default:
		break;
	}
	// A click effect: an item that has one (EffectType2), not while singing for a bard's own kinds
	// (12082), not a worn one, not while casting.
	if (item.stackable < 2 || item.effect_type2 == 0)
		return;
	int8_t kind = item.effect_type2;
	if (me.class_id == 8 && (kind == 2 || kind == 4 || kind == 1) && m_spell_cast.Casting()) {
		ChatOut(text(12082, "You must stop singing to cast this spell!"), ChatType::Spells);
		return;
	}
	if (kind == 2 || m_spell_cast.Casting())
		return;
	if (m_spell_cast.CastItem(item, slot, m_now_ms) && item.item_type == 21 && m_sound)
		m_sound->PlayWave(0x95); // a potion
}

bool FrontEnd::AutoEat(bool food)
{
	// EQ_Character::AutoEat (@004c3988): the first drink (15) or food (14) in the general slots,
	// the slot itself and then its bag's, goes to the zone (OP_Consume {slot, -1, -1, 1 food or 2
	// drink}) and one comes off it here.
	const auto &items = m_zone.Items();
	int type = food ? 14 : 15;
	auto is = [&](int16_t slot) {
		auto it = items.find(slot);
		return it != items.end() && it->second.item_class == 0 && it->second.item_type == type;
	};
	for (int16_t s = Inventory::FirstGeneral; s <= Inventory::LastGeneral; ++s) {
		auto it = items.find(s);
		if (it == items.end())
			continue;
		int16_t slot = -1;
		if (is(s))
			slot = s;
		else if (it->second.item_class == 1)
			for (int n = 0; n < it->second.bag_slots && slot < 0; ++n)
				if (is(Inventory::BagSlot(s, n)))
					slot = Inventory::BagSlot(s, n);
		if (slot >= 0) {
			m_zone.Consume(slot, food ? 1 : 2, false);
			return true;
		}
	}
	return false;
}

void FrontEnd::YelledForHelp(uint16_t id)
{
	auto it = m_zone.Spawns().find(id);
	if (it == m_zone.Spawns().end())
		return;
	const ZoneSpawn &them = it->second;
	const ZoneProfile &me = m_zone.Profile();
	// get_bearing to them (as auto follow's), and CDisplay_HeadingDiff against our heading: d the
	// way round that's shorter (0..256), `diff` its sign before the wrap.
	float degrees = std::atan2(them.x - me.x, them.y - me.y) * 180.0f / 3.14159265f;
	if (degrees < 0)
		degrees += 360;
	float bearing = degrees * 511.5f / 360.0f;
	float heading = me.heading;
	float low = std::min(heading, bearing), high = std::max(heading, bearing);
	float d = high - low, diff = d;
	if (d > 256) {
		diff = d - 512;
		d = 512 - d;
	}
	bool right = (heading < bearing && diff < 0) || (bearing < heading && diff > 0);
	uint32_t text;
	if (d >= 0 && d <= 32)
		text = 12907; // straight ahead
	else if (d > 224 && d <= 256)
		text = 12908; // behind
	else if (d <= 96)
		text = right ? 12909 : 12912;
	else if (d <= 160)
		text = right ? 12910 : 12913;
	else if (d <= 224)
		text = right ? 12911 : 12914;
	else
		return;
	static const char *const fallbacks[] = {"%1 yells for help from straight ahead of you", "%1 yells for help from behind you",
		"%1 yells for help from ahead and to the right of you", "%1 yells for help from off to the right of you",
		"%1 yells for help from behind you and to the right", "%1 yells for help from ahead and to the left of you",
		"%1 yells for help from off to the left of you", "%1 yells for help from behind you and to the left"};
	ChatOut(FormatEqString(m_ui.eqstr, text, fallbacks[text - 12907], {them.DisplayName()}), 0x11a);
}

bool FrontEnd::ZoneAnswerWaiting() const
{
	return m_window_state.busy || m_spell_cast.Waiting() || m_merchant.Waiting() || m_train.Waiting() ||
	       m_loot.Waiting() || m_ground.Waiting();
}

void FrontEnd::SavePC()
{
	// CEverQuest::SavePC: not dead nor out of the world; while the zone's answer to something is
	// waited on, the next six-second tick tries again (DAT_007cd847 = 0xb5).
	if (m_dead || m_zone.GetState() != ZoneSession::State::InZone || m_game_values.CurHp() < 1)
		return;
	if (ZoneAnswerWaiting()) {
		m_save_ticks = 181;
		return;
	}
	m_zone.SavePC();
	m_save_ticks = 0;
}

int FrontEnd::TakeFallDamage(float fall)
{
	// EQ_Character::TakeFallDamage (@004bfe16): the fall, plus a hundredth of what we carry
	// (cur_encumbrance), less 1.5; 10 damage a unit of it, 20000 past 9. Safe fall takes its skill
	// off (Acrobatics would add 10..30% of it: no AAs yet) and tells the zone either way. None on a
	// mount under 200 (no mounts yet), nor in the Bazaar (zone 151).
	const ZoneProfile &me = m_zone.Profile();
	float over = static_cast<int16_t>(m_game_values.Weight()) * 0.01f + fall - 1.5f;
	int damage = 0;
	if (over >= 9)
		damage = 20000;
	else if (over > 0) {
		damage = static_cast<int16_t>(over * 10);
		if (damage > 0 && Skills::HasSkill(me, 39) != 0) {
			damage -= std::max<int16_t>(me.skills[39], 0);
			m_zone.SafeFallSuccess();
		}
		damage = std::max(damage, 0);
	}
	if (me.zone_id == 151)
		damage = 0;
	if (damage <= 0 || m_dead)
		return 0;
	// ApplyDamage (@004c0150): OP_EnvDamage and OP_Damage (type 0xfc), and ReportSuccessfulHit's
	// line and sound here, as an incoming hit's. The HP drop is the zone's to send back.
	m_zone.ReportEnvDamage(0xfc, damage);
	ZoneCast hit;
	hit.target = m_zone.MySpawnId();
	hit.type = 0xfc;
	hit.spell = -1;
	hit.damage = damage;
	ApplyCombat(m_combat.Damaged(hit, m_zone, m_game_values.target, m_game_values.CurHp()));
	return damage;
}

void FrontEnd::HungerAndThirst(uint64_t now_ms)
{
	// EQ_Character::ProcessHungerandThirst (@004c0f62), from DoPassageOfTime. The zone digests
	// (OP_Stamina); this eats and drinks for us when we get low and says how we're doing.
	ZoneProfile &me = const_cast<ZoneProfile &>(m_zone.Profile());
	// DoPassageOfTime runs once we are in the world: not before the zone has us.
	if (m_dead || m_zone.GetState() != ZoneSession::State::InZone)
		return;
	if (m_zone.Self().gm || me.deity == 20000) {
		me.hunger = me.thirst = 32000;
		return;
	}
	bool sustained = m_buffs.Total(115) != 0; // SPA 115, Song of Sustenance's
	if (sustained) {
		me.hunger = std::max(me.hunger, 3500);
		me.thirst = std::max(me.thirst, 3500);
	}
	// 46 s (a monk's 92 s) since the last time; it starts at zero, so the first is as we come in.
	// Innate Metabolism (GetAbility 0xf) would stretch it: we don't have AAs yet.
	uint64_t every = me.class_id == 7 ? 92000 : 46000;
	if (now_ms - m_hunger_ms <= every)
		return;
	auto text = [this](uint32_t id, const char *fallback) { return FormatEqString(m_ui.eqstr, id, fallback, {}); };
	// Not while the zone's answer to something is waiting (DAT_007d0254, DAT_007d0258). The
	// mount's versions of these (12484, 12486) wait for mounts.
	bool waiting = ZoneAnswerWaiting();
	if (me.thirst < 3000 && !waiting && !AutoEat(false))
		ChatOut(text(12485, "You are thirsty."), ChatType::Default);
	if (me.hunger < 3000 && !waiting && !AutoEat(true))
		ChatOut(text(12487, "You are hungry."), ChatType::Default);
	m_hunger_ms = now_ms;
	if (sustained)
		return;
	// CalculateHungerThirstStatus (@004c3bfa): the charges of food and drink in the general slots
	// and their bags; none 1 (4 for drink), one 2 (8).
	int food = 0, drink = 0;
	const auto &items = m_zone.Items();
	auto count = [&](int16_t slot) {
		auto it = items.find(slot);
		if (it == items.end() || it->second.item_class != 0)
			return;
		if (it->second.item_type == 14)
			food += static_cast<uint8_t>(it->second.charges);
		if (it->second.item_type == 15)
			drink += static_cast<uint8_t>(it->second.charges);
	};
	for (int16_t s = Inventory::FirstGeneral; s <= Inventory::LastGeneral; ++s) {
		auto it = items.find(s);
		if (it == items.end())
			continue;
		count(s);
		if (it->second.item_class == 1)
			for (int n = 0; n < it->second.bag_slots; ++n)
				count(Inventory::BagSlot(s, n));
	}
	int status = food == 0 ? 1 : food < 2 ? 2 : 0;
	status += drink == 0 ? 4 : drink < 2 ? 8 : 0;
	static const std::pair<uint32_t, const char *> Says[] = {
		{0, nullptr},
		{12488, "You are out of food."},
		{12489, "You are low on food."},
		{0, nullptr},
		{12490, "You are out of drink."},
		{12491, "You are out of food and drink."},
		{12492, "You are out of drink and low on food."},
		{0, nullptr},
		{12493, "You are low on drink."},
		{12494, "You are out of food and low on drink."},
		{12495, "You are low on food and drink."},
	};
	if (Says[status].first)
		ChatOut(text(Says[status].first, Says[status].second), ChatType::Default);
}

void FrontEnd::ReadNote(const ZoneItem &item, int16_t slot)
{
	// A note (type 0): CNoteWnd's Deactivate, then CheckNote; a book's: CBookWnd's, then CheckBook.
	std::string which = std::to_string(slot) + ":" + item.name;
	if (item.book_type != 0) {
		if (!m_book.Check(which, item.name))
			return;
	}
	else {
		m_windows.Show("NoteWindow", false);
		if (m_note_shown == which) {
			m_note_shown.clear();
			return;
		}
		m_note_shown = which;
		m_note_title = item.name;
	}
	m_zone.ReadBook(item.book_type, item.book_file);
}

void FrontEnd::BookText(int type, const std::string &text)
{
	if (type == 1)
		m_book.SetBook(text);
	if (type != 0)
		return;
	SidlWindow *note = m_windows.Find("NoteWindow");
	if (!note)
		return;
	// CNoteWnd::SetNote: the window named for the note, the text black (<c "#000000">...</c>).
	note->Root().text = m_note_title;
	if (SidlWindow::Control *display = note->Find("Note")) {
		display->text = "<c \"#000000\">" + text + "</c>";
		display->scroll_pos = 0;
	}
	m_windows.Show("NoteWindow", true);
	m_windows.Raise("NoteWindow");
}

void FrontEnd::Follow()
{
	auto text = [this](uint32_t id, const char *fallback, const std::vector<std::string> &args = {}) {
		return FormatEqString(m_ui.eqstr, id, fallback, args);
	};
	const ZoneSpawn *target = m_game_values.Target();
	uint32_t refused = 0;
	const char *why = "";
	if (!target || target->npc != 0)
		refused = 13225, why = "You must first target a group member to auto-follow.";
	else if (target->id == m_zone.MySpawnId())
		refused = 13226, why = "You can't auto-follow yourself.";
	else if (!m_group.IsAGroupMember(target->name))
		refused = 13225, why = "You must first target a group member to auto-follow.";
	else if (m_buffs.SpeedModifier(m_zone.Self().run_speed) < -1000) // MovementSpeedModifier: rooted
		refused = 13227, why = "You cannot move right now, how could you follow?";
	else if (m_zone.Self().stance == 102 || m_zone.Self().linkdead) {
		ChatOut(text(13228, "You are not yourself right now, you cannot follow."), ChatType::Default);
		return;
	}
	else if (m_zone.Self().stance != 100)
		refused = 13229, why = "Stand up first!";
	if (refused != 0) {
		ChatOut(text(refused, why), ChatType::Default);
		return;
	}
	ChatOut(text(13230, "You will now auto-follow %1.", {target->DisplayName()}), ChatType::Default);
	ChatOut(text(13231, "*WARNING*: Do NOT use around lava, water, cliffs, or other dangerous areas because you WILL fall into them. You have been warned."),
		ChatType::Red);
	ChatOut(text(13232, "Also, auto-follow works best in wide open areas with low lag.  Twisty areas, lag, and other factors may cause auto-follow to fail."),
		ChatType::Red);
	m_following = target->id;
}

void FrontEnd::TurnOffAutoFollow(bool say)
{
	if (m_following == 0)
		return;
	m_autorun = false;
	m_run = true;
	auto it = m_zone.Spawns().find(m_following);
	if (say && it != m_zone.Spawns().end())
		ChatOut(FormatEqString(m_ui.eqstr, 12694, "You are no longer auto-following %1.", {it->second.DisplayName()}),
			ChatType::Default);
	m_following = 0;
}

void FrontEnd::FollowPlayerAI()
{
	if (m_following == 0 || !m_zone_view)
		return;
	auto it = m_zone.Spawns().find(m_following);
	if (it == m_zone.Spawns().end()) {
		// ~EQPlayer: whoever followed them stops, without a word.
		TurnOffAutoFollow(false);
		return;
	}
	const ZoneSpawn &them = it->second;
	if (them.stance == 120) {
		ChatOut(FormatEqString(m_ui.eqstr, 12692, "%1 is dead, cancelling auto-follow.", {them.name}), ChatType::Default);
		TurnOffAutoFollow();
		return;
	}
	ZoneMovement me = m_zone_view->Movement();
	float dx = them.x - me.x, dy = them.y - me.y, dz = them.z - me.z;
	float distance = std::sqrt(dx * dx + dy * dy + dz * dz); // CDisplay_TrueDistance
	if (distance > 200) {
		ChatOut(FormatEqString(m_ui.eqstr, 12693, "%1 is too far away, cancelling auto-follow.", {them.DisplayName()}),
			ChatType::Default);
		TurnOffAutoFollow();
		return;
	}
	float lift = m_zone_view->CenterHeight(them.id) + m_zone_view->CenterHeight(m_zone.MySpawnId());
	// get_bearing: atan2 of the x and y differences, in degrees, as 511.5ths of a turn.
	float degrees = std::atan2(dx, dy) * 180.0f / 3.14159265f;
	if (degrees < 0)
		degrees += 360;
	float bearing = degrees * 511.5f / 360.0f;
	if (CanSee(them.id)) { // CastRay from them to us
		float heading = me.heading, turned = bearing;
		if (distance >= 20) {
			// CDisplay_HeadingDiff: the way round that's shorter, at most 4 a frame (9 past a quarter turn).
			float low = std::min(heading, bearing), high = std::max(heading, bearing);
			float diff = high - low, signed_diff = diff;
			if (diff > 256) {
				signed_diff = diff - 512;
				diff = 512 - diff;
			}
			if (diff >= 1) {
				float step = diff < 4 ? diff : diff <= 128 ? 4.0f : 9.0f;
				if ((heading < bearing && signed_diff < 0) || (bearing < heading && signed_diff > 0))
					turned = heading - step;
				else
					turned = heading + step;
				while (turned >= 512)
					turned -= 512;
				while (turned < 0)
					turned += 512;
			}
		}
		m_zone_view->Turn(turned - heading);
	}
	if (distance < lift + 15) {
		m_autorun = false;
		return;
	}
	const ZoneMovement now = m_zone_view->Movement();
	if (distance < lift + 30 && (them.speed < now.speed || now.speed == 0))
		m_run = false;
	else
		m_run = true;
	m_autorun = true;
}

void FrontEnd::UpdateDeath(uint64_t now_ms)
{
	if (!m_dead || m_zoning || m_death_ms == UINT64_MAX)
		return;
	if (m_death_ms == 0) {
		// CDisplay::MovePlayer: a corpse whose DeathTimer isn't going yet starts it.
		m_death_ms = now_ms > 1 ? now_ms : 2;
		return;
	}
	if (m_death_ms == 1) {
		// PrepareLocalPCForRepop (the options saved, EQ_PC::PrepareForRepop: what the next
		// zone's profile gives back), then MoveToZone(ZoneBoundId, "repop_to_home_at_death", 1, 10).
		m_raid.Died();
		if (m_zone.RequestZoneChange(m_zone.Profile().bind_zone, 10))
			m_death_ms = UINT64_MAX;
		return;
	}
	if (now_ms - m_death_ms > 2000) {
		ChatOut(EqString(539, "Returning to home point, please wait..."), ChatType::Red);
		m_death_ms = 1;
	}
}

bool FrontEnd::CanSee(uint16_t spawn) const
{
	auto it = m_zone.Spawns().find(spawn);
	if (!m_zone_view || it == m_zone.Spawns().end())
		return true; // ourselves, or nothing to look through yet
	const ZoneProfile &me = m_zone.Profile();
	return m_zone_view->CanSee({me.y, me.x, me.z}, {it->second.y, it->second.x, it->second.z});
}

void FrontEnd::ChangeStance(uint8_t stance)
{
	uint8_t was = m_zone.Self().stance;
	if (stance == was || m_zone.GetState() != ZoneSession::State::InZone)
		return;
	m_zone.SetStance(stance);
	if (m_zone_view)
		m_zone_view->SetStance(stance);
	if (was == 110)
		m_spell_book.StoodUp();
}

void FrontEnd::WorldClick(int x, int y)
{
	if (uint16_t id = m_zone_view->PickSpawn(x, y, m_zone.MySpawnId())) {
		SetTarget(id);
		m_group.Clicked(id); // EQ_3DView_MouseUp: a member clicked is the one selected
		if (m_zone.Items().count(Inventory::Cursor) || Inventory::CursorMoneyType(m_zone.Profile()))
			m_trade.DropOnPlayer(id);
		return;
	}
	if (!m_trade.Active()) {
		bool held = m_zone.Items().count(Inventory::Cursor) != 0;
		if (held && m_ground.DropRequest(m_now_ms))
			return;
		if (!held && Inventory::CursorMoneyType(m_zone.Profile()))
			return;
		uint32_t drop_id = 0;
		if (!held && !m_containers.WorldOpen() && m_zone_view->PickGroundItem(x, y, m_zone.MySpawnId(), drop_id) && m_ground.PickUp(drop_id))
			return;
	}
	uint8_t door = 0;
	int16_t skill = 0;
	int16_t disarm = m_zone.Profile().skills[17]; // disarm traps
	switch (m_zone_view->ClickDoor(x, y, disarm, door, skill)) {
	case ZoneDoors::Use::Ask:
		m_zone.ClickDoor(door, 0xffff, skill);
		break;
	case ZoneDoors::Use::TooFar:
		AddZoneLog(EqString(1462, "You can't reach that, get closer."), ZoneYellow);
		break;
	case ZoneDoors::Use::Nothing:
		break;
	}
}

bool FrontEnd::IsOkToTransact() const
{
	// Not camping, no combine, buy or sell waiting (DAT_007d0254), no merchant asked
	// (DAT_007d0258, a guildmaster's too), no world container (pWorldItems), spellbook, bank,
	// merchant, trainer, corpse or trade (the bazaar window when we have it).
	return m_zone.CampRemainingMs() == 0 && !m_containers.Combining() && !m_spell_book.Activated() && !m_bank.Active() &&
	       !m_merchant.Active() && !m_merchant.Waiting() && !m_merchant.Busy() && !m_train.Active() &&
	       !m_train.Waiting() && !m_loot.Active() &&
	       !m_loot.Waiting() && !m_loot.Busy() && !m_trade.Active() && !m_ground.Busy() && !m_ground.Waiting() &&
	       !m_containers.WorldOpen();
}

void FrontEnd::RightClickedOnPlayer(uint16_t spawn)
{
	if (!IsOkToTransact() || spawn == m_zone.MySpawnId())
		return;
	auto it = m_zone.Spawns().find(spawn);
	if (it == m_zone.Spawns().end())
		return;
	const ZoneSpawn &who = it->second;
	// A corpse: CEverQuest_LootCorpse; out of reach, it's considered as anyone is.
	if (who.npc >= 2 && m_loot.LootCorpse(spawn))
		return;
	constexpr int Banker = 16, Merchant = 32;
	if (who.npc < 2 && who.class_id == Merchant && m_zone.SpawnDistance(spawn) <= 20) {
		m_abilities.MakeMeVisible();
		AutoAttackOff(1466); // "Auto attack off."
		m_merchant.Request(spawn);
		return;
	}
	if (who.npc < 2 && who.class_id == Banker && m_zone.SpawnDistance(spawn) <= 20) {
		m_bank.Activate(spawn);
		if (m_bank.Active()) // CBankWnd::Activate, after the banker's welcome
			AutoAttackOff(1466);
		return;
	}
	// A guildmaster (17..31): our class's (ours + 16) is asked to train us, with none training us
	// already; another's turns us away. Either way, /consider follows.
	if (who.npc < 2 && who.class_id >= 17 && who.class_id <= 31 && m_zone.SpawnDistance(spawn) <= 20) {
		const ZoneProfile &me = m_zone.Profile();
		if (me.class_id + 16 == who.class_id) {
			if (m_train.Guildmaster() == 0) {
				m_abilities.MakeMeVisible();
				AutoAttackOff(1466); // "Auto attack off."
				m_train.Request(spawn);
			}
		}
		else
			ChatOut(FormatEqString(m_ui.eqstr, 1467, "You are not a member of the %1 class guild.  Begone.",
			            {GameValues::ClassDesc(m_ui.eqstr, who.class_id - 16)}),
				10);
	}
	// Anything else: _consider with nothing typed, which considers our target (not who was clicked).
	InterpretCommand("/consider");
}

void FrontEnd::Sit()
{
	// Standing (or crouching) sits; anything else stands.
	uint8_t stance = m_zone.Self().stance;
	if (stance == 100 || stance == 111)
		ChangeStance(110);
	else
		ChangeStance(100);
}

void FrontEnd::SetAutoAttack(bool on)
{
	m_attack = on;
	if (m_sound)
		m_sound->SetMusicSelection(audio::EqSound::Combat, on && m_config.combat_music);
}

void FrontEnd::ToggleAutoAttack()
{
	if (m_screen != Screen::Zone)
		return;
	if (m_spell_book.Activated() && !m_attack) {
		ChatOut(EqString(9027, "You can't attack with your spell book open."), ChatType::Red);
		return;
	}
	if (m_merchant.Active() || m_bank.Active() || m_train.Guildmaster() != 0)
		return; // pinstActiveMerchant, pinstActiveBanker, pinstActiveGMaster
	uint8_t stance = m_zone.Self().stance;
	if (stance == 100 || m_attack) {
		SetAutoAttack(!m_attack);
		ChatOut(m_attack ? EqString(12370, "Auto attack is on.") : EqString(12371, "Auto attack is off."), ChatType::Red);
	}
	else if (stance == 102)
		ChatOut(EqString(12368, "You do not have control of yourself right now."), ChatType::Red);
	else
		ChatOut(EqString(12369, "You must be standing to attack!"), ChatType::Red);
}

void FrontEnd::AutoAttackOff(uint32_t said)
{
	if (!m_attack)
		return;
	SetAutoAttack(false);
	ChatOut(EqString(said, said == 1466 ? "Auto attack off." : "Auto attack is off."), ChatType::Red);
}

void FrontEnd::ApplyCombat(const Combat::Report &report)
{
	for (const Combat::Line &line : report.lines)
		ChatOut(line.text, line.type);
	if (report.target != 0 && m_game_values.target == 0)
		SetTarget(report.target);
	if (report.my_damage != 0)
		m_zone.ModifyHp(-report.my_damage); // EQ_Character::ModifyCurHP
	if (report.hit_me) {
		// EQPlayer::IWasHit: no more invisible (or hidden, or sneaking), and pushed.
		m_abilities.Hit();
		for (int spa : {12, 28, 29})
			m_buffs.RemoveAffect(spa);
		if (m_zone_view && report.force != 0)
			m_zone_view->Push(report.force, report.heading, report.pitch);
	}
	if (report.close_book && m_spell_book.Activated())
		m_spell_book.Deactivate();
	if (report.hurt_me) {
		// Hit: the book closes, and sitting or kneeling we stand up.
		if (m_spell_book.Activated())
			m_spell_book.Deactivate();
		uint8_t stance = m_zone.Self().stance;
		if (stance == 110 || stance == 105)
			ChangeStance(100);
	}
	if (report.animation >= 0 && report.victim != 0) {
		if (report.victim != m_zone.MySpawnId())
			m_zone.PlayAnimation(report.victim, report.animation);
		else if (m_zone_view)
			m_zone_view->PlayAnimation(report.animation);
	}
	if (m_zone_view)
		for (const Combat::Sound &sound : report.sounds)
			m_zone_view->PlayActorSound(sound.spawn, sound.wave);
}

int FrontEnd::GeneralOption(OptionsWindow::Host::General setting) const
{
	using General = OptionsWindow::Host::General;
	switch (setting) {
	case General::FastItemDestroy: return m_fast_item_destroy ? 1 : 0;
	case General::GuildInvites: return m_guild_invites ? 1 : 0;
	case General::Autosplit: return m_zone.Profile().autosplit ? 1 : 0;
	case General::Anonymous:
	case General::Roleplaying: return m_zone.Self().anon;
	case General::PlayerTrade: return m_trade_with;
	case General::ItemDropping: return m_ground.fast_drop;
	case General::SoundRealism: return m_sound_realism;
	case General::MusicVolume: return m_music_volume;
	case General::SoundVolume: return m_sound_volume;
	case General::LoadSkin: break;
	}
	return 0;
}

void FrontEnd::SetGeneralOption(OptionsWindow::Host::General setting, int value)
{
	// COptionsWnd::GeneralPageWndNotification (@0042b532), then saveOptions.
	using General = OptionsWindow::Host::General;
	switch (setting) {
	case General::FastItemDestroy:
		m_fast_item_destroy = value != 0;
		ChatOut(m_fast_item_destroy ? EqString(12032, "Fast Item Destroy is ON.") : EqString(12031, "Fast Item Destroy is OFF."),
			ChatType::Yellow);
		SaveSetting("Defaults", "FastItemDestroy", value != 0);
		break;
	case General::GuildInvites:
		m_guild_invites = value != 0;
		SaveSetting("Defaults", "GuildInvites", value != 0);
		break;
	case General::Autosplit:
		// IsAutoSplitEnabled, said (12036, 12037), and CEverQuest_ToggleAutoSplit.
		ChatOut(value ? EqString(12036, "You will now automatically split money with your group.")
		              : EqString(12037, "You will no longer split money with your group."),
			ChatType::Default);
		m_zone.SetAutoSplit(value != 0);
		break;
	case General::Anonymous:
	case General::Roleplaying: {
		// The state said (12034, 12035; 13233, 13234), OP_SpawnAppearance 21 with it, our name
		// sprite's tint after it (it follows Self()).
		bool anonymous = setting == General::Anonymous;
		bool on = anonymous ? value == 1 || value == 3 : value == 2 || value == 3;
		std::string text = anonymous ? (on ? EqString(12034, "You are now anonymous.") : EqString(12035, "You are no longer anonymous."))
		                             : (on ? EqString(13233, "You are now roleplaying.")
		                                   : EqString(13234, "You are no longer roleplaying."));
		ChatOut(text, ChatType::Default);
		m_zone.SetAnonymous(static_cast<uint8_t>(std::clamp(value, 0, 3)));
		SavePC();
		break;
	}
	case General::PlayerTrade:
		m_trade_with = std::clamp(value, 0, 2);
		SaveSetting("Defaults", "PlayerTrade", m_trade_with);
		break;
	case General::ItemDropping:
		// _fastdrop with "off", "on" or "never".
		InterpretCommand("/fastdrop " + (value == 0 ? EqString(12946, "off") : value == 1 ? EqString(12947, "on") : EqString(12989, "never")));
		break;
	case General::SoundRealism:
		// EqSoundManager::SetEffectsLevel with the value * 100 / 20 hundredths: the reverb's share
		// of the positional sounds started from now on.
		m_sound_realism = value;
		m_ui.sound.effects_level = (value * 100 / 20) * 0.01f;
		if (m_sound)
			m_sound->SetVolumes(m_ui.sound);
		SaveSetting("Sound", "SoundRealism", value);
		break;
	case General::MusicVolume:
	case General::SoundVolume:
		// SetWaveVolume and SetMusicVolume with the value * 100 / 20 hundredths.
		(setting == General::MusicVolume ? m_music_volume : m_sound_volume) = value;
		(setting == General::MusicVolume ? m_ui.sound.music_volume : m_ui.sound.sound_volume) = (value * 100 / 20) * 0.01f;
		if (m_sound)
			m_sound->SetVolumes(m_ui.sound);
		SaveSetting("Sound", setting == General::MusicVolume ? "MusicVolume" : "SoundVolume", value);
		break;
	case General::LoadSkin:
		// CLoadskinWnd::Activate: not ours yet (TODO).
		break;
	}
}

float FrontEnd::DisplayOption(OptionsWindow::Host::Display setting) const
{
	using Display = OptionsWindow::Host::Display;
	const WindowManager::Globals &windows = m_windows.Global();
	switch (setting) {
	case Display::PCNames: return m_pc_names ? 1.0f : 0.0f;
	case Display::NPCNames: return m_npc_names ? 1.0f : 0.0f;
	case Display::LevelOfDetail: return m_level_of_detail ? 1.0f : 0.0f;
	case Display::Sky: return static_cast<float>(m_sky);
	case Display::SpellNearClip: return m_spell_near_clip;
	case Display::SpellDensity: return m_spell_density;
	case Display::SpellOpacity: return m_spell_opacity;
	case Display::ParticleDensity: return static_cast<float>(m_particle_density);
	case Display::Gamma: return m_gamma;
	case Display::ClipPlane: return static_cast<float>(m_clip_slider);
	case Display::FadeDelay: return static_cast<float>(windows.delay);
	case Display::FadeDuration: return static_cast<float>(windows.duration);
	case Display::WindowAlpha: return static_cast<float>(windows.alpha);
	case Display::FadeToAlpha: return static_cast<float>(windows.fade_to);
	case Display::VideoModes: break;
	}
	return 0;
}

void FrontEnd::SetDisplayOption(OptionsWindow::Host::Display setting, float value)
{
	// COptionsWnd::DisplayPageWndNotification (@0042bba3), then saveOptions.
	using Display = OptionsWindow::Host::Display;
	auto real = [](float f) {
		char text[32];
		snprintf(text, sizeof(text), "%f", f);
		return std::string(text);
	};
	switch (setting) {
	case Display::PCNames: // TurnCharacterNameSpritesOn / Off
		m_pc_names = value != 0;
		SaveSetting("Defaults", "PCNames", m_pc_names ? 1 : 0);
		break;
	case Display::NPCNames: // TAKP's TurnNPCNameSpritesOff (which turns them on) / On
		m_npc_names = value != 0;
		SaveSetting("Defaults", "NPCNames", m_npc_names ? 1 : 0);
		break;
	case Display::LevelOfDetail:
		// setLOD: said (6650, 6651), then s3dSetDynamicLOD (not ours: models are drawn whole).
		m_level_of_detail = value != 0;
		ChatOut(m_level_of_detail ? EqString(6650, "Level of Detail *ON*") : EqString(6651, "Level of Detail *OFF*"),
			ChatType::Default);
		SaveSetting("Defaults", "LevelOfDetail", m_level_of_detail ? 1 : 0);
		break;
	case Display::Sky: // the sky type (0 none), t3dSetSkyDualLayer
		m_sky = std::clamp(static_cast<int>(value), 0, 2);
		ApplyDisplayOptions();
		SaveSetting("Defaults", "Sky", m_sky);
		break;
	case Display::ParticleDensity: // CDisplay::SetPCloudDensity
		m_particle_density = std::clamp(static_cast<int>(value), 0, 3);
		ApplyDisplayOptions();
		SaveSetting("Defaults", "ParticleDensity", m_particle_density);
		break;
	case Display::SpellDensity:
		m_spell_density = value;
		ApplyDisplayOptions();
		SaveSetting("Defaults", "SpellParticleDensity", real(value));
		break;
	case Display::SpellNearClip:
		m_spell_near_clip = value;
		ApplyDisplayOptions();
		SaveSetting("Defaults", "SpellParticleNearClipPlane", real(value));
		break;
	case Display::SpellOpacity:
		m_spell_opacity = value;
		ApplyDisplayOptions();
		SaveSetting("Defaults", "SpellParticleOpacity", real(value));
		break;
	case Display::Gamma:
		// CDisplay_SetGammaCorrection: t3dSetGammaLevel changes the ramp only full screen, which
		// the TAKP client never is; kept, and nothing more.
		m_gamma = value;
		SaveSetting("Defaults", "Gamma", real(value));
		break;
	case Display::ClipPlane: // DAT_00798b28, _DAT_00799660 (saveOptions2)
		m_clip_slider = std::clamp(static_cast<int>(value), 0, 20);
		m_clip_plane = std::max(m_clip_slider * 0.2f, 0.1f);
		ApplyDisplayOptions();
		SaveSetting("Defaults", "ClipPlane", m_clip_slider);
		break;
	case Display::FadeDelay: m_windows.SetGlobalFadeDelay(static_cast<int>(value)); break;
	case Display::FadeDuration: m_windows.SetGlobalFadeDuration(static_cast<int>(value)); break;
	case Display::WindowAlpha: m_windows.SetGlobalAlpha(static_cast<int>(value)); break;
	case Display::FadeToAlpha: m_windows.SetGlobalFadeTo(static_cast<int>(value)); break;
	case Display::VideoModes:
		// CVideoModesWnd::Activate: not ours yet (TODO).
		break;
	}
}

void FrontEnd::ApplyDisplayOptions()
{
	static const float densities[] = {0, 0.33f, 0.66f, 1}; // SetPCloudDensity's
	render::SetPCloudDensity(densities[std::clamp(m_particle_density, 0, 3)]);
	if (!m_zone_view)
		return;
	m_zone_view->SetSkyMode(m_sky);
	m_zone_view->SetClipPlane(m_clip_plane);
	m_zone_view->SetSpellParticles(m_spell_density, m_spell_opacity, m_spell_near_clip);
}

void FrontEnd::SaveSetting(const char *section, const char *key, int value)
{
	SaveSetting(section, key, std::to_string(value));
}

void FrontEnd::SaveSetting(const char *section, const char *key, const std::string &value)
{
	if (m_config.settings_ini.empty())
		return;
	IniFile ini;
	ini.Load(m_config.settings_ini); // keep the rest
	ini.Set(section, key, value);
	if (!ini.Save(m_config.settings_ini))
		Log(std::string("settings: couldn't write ") + section + " " + key + " to " + m_config.settings_ini);
}

void FrontEnd::FiltersChanged()
{
	m_zone.SetServerFilters(m_filters.Packet());
	if (m_config.settings_ini.empty())
		return;
	IniFile ini;
	ini.Load(m_config.settings_ini); // keep the rest
	m_filters.Store(ini);
	if (!ini.Save(m_config.settings_ini))
		Log("filters: couldn't write " + m_config.settings_ini);
}

void FrontEnd::SpellDamaged(const ZoneCast &cast)
{
	const SpellInfo *spell = m_ui.spells ? m_ui.spells->Find(cast.spell) : nullptr;
	if (!spell)
		return;
	uint16_t me = m_zone.MySpawnId();
	Combat::Report report;
	if (cast.target == me && cast.caster != me) {
		auto it = m_zone.Spawns().find(cast.caster);
		if (it != m_zone.Spawns().end() && m_game_values.target == 0 && it->second.body_type < 65 &&
			spell->good_effect == 0)
			report.target = cast.caster;
		if (cast.damage > 0)
			report.close_book = true;
		else if (cast.damage < 0)
			report.my_damage = cast.damage; // ModifyCurHP of the negative: it gives
	}
	if (spell->good_effect == 0)
		m_combat.SpellHurt(cast.target, cast.damage, m_zone, report);
	ApplyCombat(report);
}

bool FrontEnd::AllowedToAttack(const ZoneSpawn &target) const
{
	// Never at (or as) these races; an NPC, or a pet of one; a player (or a player's pet) only in PvP:
	// both PvP, within 4 levels, both level 6 and up. (Duels, the server's PvP types 2 and 4, and
	// being in different water aren't ours yet.)
	const ZoneSpawn &me = m_zone.Self();
	auto never = [](uint16_t race) { return race == 72 || race == 141 || race == 73; };
	if (never(target.race) || never(me.race) || me.race == 114)
		return false;
	const ZoneSpawn *other = &target;
	if (target.npc == 1 && target.pet_owner != 0) {
		auto owner = m_zone.Spawns().find(target.pet_owner);
		if (owner != m_zone.Spawns().end() && owner->second.npc == 0)
			other = &owner->second;
		else if (target.pet_owner == m_zone.MySpawnId())
			other = &me;
	}
	if (other->npc == 1)
		return true;
	int my_level = m_zone.Profile().level, level = other->level;
	return other->pvp != 0 && me.pvp != 0 && std::abs(my_level - level) <= 4 && my_level >= 6 && level >= 6;
}

float FrontEnd::MeleeRange(uint16_t id) const
{
	// The two models' bounding radii (t3dGetScaledActorBoundingRadius) times 0.75, at least 14, plus
	// how far apart their centers sit over their feet, plus 2; at most 75. 14 without the models.
	if (!m_zone_view)
		return 14;
	uint16_t me = m_zone.MySpawnId();
	float mine = m_zone_view->BoundingRadius(me), theirs = m_zone_view->BoundingRadius(id);
	if (mine < 0 || theirs < 0)
		return 14;
	float range = std::max(14.0f, (mine + theirs) * 0.75f);
	range += std::fabs(m_zone_view->CenterHeight(me) - m_zone_view->CenterHeight(id)) + 2;
	return std::min(range, 75.0f);
}

float FrontEnd::GroundDistance(uint16_t id) const
{
	auto it = m_zone.Spawns().find(id);
	if (it == m_zone.Spawns().end())
		return 0;
	const ZoneProfile &me = m_zone.Profile();
	return std::hypot(it->second.x - me.x, it->second.y - me.y);
}

bool FrontEnd::CanIHitTarget(uint16_t id, float extra)
{
	if (m_buffs.Total(40) >= 1) { // divine aura
		ChatOut(EqString(12695, "You can't attack while invulnerable!"), ChatType::Default);
		return false;
	}
	if (id != 0 && id == m_zone.MySpawnId()) {
		ChatOut(EqString(12696, "Try attacking someone other than yourself, it's more productive."), ChatType::Red);
		return false;
	}
	auto it = m_zone.Spawns().find(id);
	if (id == 0 || it == m_zone.Spawns().end()) {
		ChatOut(EqString(12697, "You must first click on the being you wish to attack!"), ChatType::Red);
		return false;
	}
	const ZoneSpawn &target = it->second;
	if (!AllowedToAttack(target) || !m_zone_view)
		return false;
	if (!CanSee(id)) {
		ChatOut(EqString(123, "You can't hit them from here."), ChatType::Red);
		return false;
	}
	// get_range_squared: from foot to foot.
	const ZoneProfile &me = m_zone.Profile();
	uint16_t my_id = m_zone.MySpawnId();
	float dx = target.x - me.x, dy = target.y - me.y;
	float dz = (target.z - m_zone_view->CenterHeight(id)) - (me.z - m_zone_view->CenterHeight(my_id));
	float range = MeleeRange(id) + extra;
	if (dx * dx + dy * dy + dz * dz > range * range) {
		ChatOut(EqString(124, "Your target is too far away, get closer!"), 0x12f);
		return false;
	}
	// get_bearing to it against our heading: within 80 of 512 (256 between players), its wrap at
	// north as eqgame's.
	float bearing = std::atan2(dx, dy) * 512.0f / (2 * 3.14159265f);
	if (bearing < 0)
		bearing += 512;
	float heading = me.heading;
	if (bearing > 472 && heading < 40)
		bearing = heading;
	if (bearing < 40 && heading >= 472)
		bearing = heading;
	float limit = 80;
	if (target.npc == 0) {
		limit = 256;
		if (bearing > 350 && heading < 150)
			bearing = heading;
		if (bearing < 150 && heading >= 350)
			bearing = heading;
	}
	if (std::fabs(bearing - heading) > limit) {
		ChatOut(EqString(108, "You cannot see your target."), ChatType::Red);
		return false;
	}
	return true;
}

void FrontEnd::StartCamp()
{
	if (m_zone.GetState() != ZoneSession::State::InZone)
		return;
	if (m_zone.Camp())
		AddZoneLog("It will take you about 30 seconds to prepare your camp.", ZoneYellow);
}

void FrontEnd::HandleZoneEvents(const std::vector<ZoneSession::Event> &events)
{
	using Type = ZoneSession::Event::Type;
	for (const auto &event : events) {
		switch (event.type) {
		case Type::ZoneInfo: {
			AddZoneLog("You have entered " + m_zone.Info().long_name + ".", ZoneYellow);
			SetAutoAttack(false); // CEverQuest_EnterZone
			m_dead = false;
			m_death_ms = 0;
			m_cycle_index = -1;    // Zeal's CycleTarget::on_zone
			m_toggle_target = 0; // CEverQuest_EnterZone
			if (!m_windows_built)
				BuildZoneWindows();
			m_buffs.Reset(m_now_ms); // the profile's, which came first
			// CEverQuest_EnterZone -> CDisplay::StartWorldDisplay: load the zone, and put our
			// character (OP_ZoneEntry came first) in it.
			if (m_ui.eq_dir.empty())
				break;
			m_zone_view = std::make_unique<ZoneView>(m_ui.eq_dir, MakeRenderer());
			m_zone_view->SetVeliousTextures(m_config.velious_textures);
			m_zone_view->SetLuclinModels(m_config.luclin_models);
			m_zone_view->SetSpellFile(m_ui.spells);
			m_zone_view->SetFallDamage([this](float fall) { return TakeFallDamage(fall); });
			ApplyDisplayOptions();
			m_zone_view->StartLoading(m_zone.Info());
			m_zone_view->SetSound(m_sound.get());
			if (m_sound) // StartWorldDisplay
				m_sound->SetCurrentZone(m_zone.Info().short_name);
			const ZoneSpawn &me = m_zone.Self();
			m_zone_view->SetPlayer(PlayerAppearance(m_zone), me.walk_speed, me.run_speed);
			m_my_look_serial = me.look_serial;
			m_zone_view->SetPlayerPosition(me.x, me.y, me.z, me.heading);
			if (!m_zone.Doors().empty())
				m_zone_view->SetDoors(m_zone.Doors(), m_now_ms);
			for (const auto &[drop_id, item] : m_zone.GroundItems())
				m_zone_view->AddGroundItem(drop_id, item.model, item.x, item.y, item.z, item.heading);
			break;
		}
		case Type::DoorsSpawned:
			if (m_zone_view)
				m_zone_view->SetDoors(m_zone.Doors(), m_now_ms);
			break;
		case Type::SoundPlayed:
			if (m_zone_view)
				m_zone_view->PlaySpawnWave(event.id, event.value);
			break;
		case Type::Yelled:
			YelledForHelp(event.id);
			break;
		case Type::Consented: {
			// msg_consent_verify (@004e3314): to us, given or denied the grantee; else, to them, the
			// owner's corpse (an ignored owner's isn't said: no ignore list yet).
			bool ours = Lower(event.text2) == Lower(m_zone.Profile().name);
			uint32_t id = ours ? (event.value ? 1427 : 1428) : (event.value ? 2080 : 2103);
			const char *fallback = ours ? (event.value ? "You have given %1 permission to drag your corpse."
			                                           : "You have denied %1 permission to drag your corpse.")
			                            : (event.value ? "You have been given permission to drag %1's corpse."
			                                           : "You have been denied permission to drag %1's corpse.");
			ChatOut(FormatEqString(m_ui.eqstr, id, fallback, {ours ? event.text : event.text2}), ChatType::Default);
			break;
		}
		case Type::DoorMoved:
			if (m_zone_view)
				m_zone_view->MoveDoor(static_cast<uint8_t>(event.id), static_cast<uint8_t>(event.value), m_now_ms);
			break;
		case Type::InZone:
			m_zoning = false;
			m_guild.InZone();
			// HandleWorldMessage's OP_SendExpZonein: still looking for a group, say so here too.
			if (m_lfg)
				Lfg(false, "on");
			AddZoneLog(std::to_string(m_zone.Spawns().size()) + " spawns in the zone.", ZoneGrey);
			if (m_zone_view) {
				int16_t swimming = m_zone.Profile().skills[50];
				m_zone_view->SetSwimSkill(swimming >= 0 && swimming <= 252 ? swimming : 0);
				int16_t safe_fall = m_zone.Profile().skills[39];
				m_zone_view->SetSafeFallSkill(safe_fall >= 0 && safe_fall <= 252 ? safe_fall : 0);
			}
			break;
		case Type::ZoneApproved:
			// MoveToZone_2: "LOADING, PLEASE WAIT...", then the zone lets us go (Departed).
			m_zoning = true;
			m_zone_view.reset();
			m_weather.Kill(m_sound.get()); // StopWorldDisplay
			AddZoneLog(EqString(12387, "LOADING, PLEASE WAIT..."), ZoneGrey);
			break;
		case Type::ZoneDenied: {
			// MoveToZone_2: why, then back to the zone's safe point.
			uint32_t id = event.value == -7 ? 252 : event.value == -2 ? 5049 : event.value == -3 ? 5050 : event.value == -6 ? 5052
			            : event.value == -1                    ? 5051
			                                                   : 12386;
			AddZoneLog(EqString(id, "You can't enter that zone right now."), ZoneYellow);
			if (m_zone_view) {
				const ZoneInfo &info = m_zone.Info();
				m_zone_view->SetPlayerPosition(info.safe_x, info.safe_y, info.safe_z, m_zone_view->Movement().heading);
			}
			break;
		}
		case Type::Departed: {
			// Back to world, zoning: it sends the next zone's address (a ZoneServer event).
			std::string character = m_zone.Profile().name;
			m_zone.Close();
			const LoginResult &result = m_session.Result();
			if (!m_world.BeginZoning(result.world.address, m_config.world_port, result.session_name, result.world_key, character)) {
				m_zoning = false;
				ShowMessage(m_strings.Get("SISATEXT_CONNECT_FAILED"), [this](bool) { EnterStart(); });
			}
			break;
		}
		case Type::Chat: {
			std::string line;
			int type = ChatType::Default;
			if (ChannelLine(event.chat, m_zone.Profile().name, m_zone.Profile().languages, m_ui.eqstr, line, type))
				ChatOut(line, type);
			if (event.chat.channel == 7 && event.chat.sender != m_zone.Profile().name)
				AddNameToTellList(event.chat.sender);
			if (event.chat.channel == 7 || event.chat.channel == 14)
				AnswerTellAfk(event.chat.sender, event.chat.text);
			break;
		}
		case Type::Message:
			ChatOut(event.text, event.value);
			break;
		case Type::ItemLink:
			ShowItemDisplay(m_windows, m_game_values, m_window_state, m_zone.LinkedItem());
			break;
		case Type::CombineAnswered:
			m_containers.CombineAnswered();
			break;
		case Type::MerchantOpened:
		case Type::MerchantBought:
		case Type::MerchantSold:
		case Type::MerchantItemGone:
			m_merchant.ZoneEvent(event);
			break;
		case Type::TrainerAnswered:
			m_train.ZoneEvent(event);
			break;
		case Type::LootOpened:
		case Type::LootTaken:
		case Type::LootEnded:
			m_loot.ZoneEvent(event);
			break;
		case Type::GroundItemAdded:
			if (m_zone_view)
				if (auto it = m_zone.GroundItems().find(static_cast<uint32_t>(event.value)); it != m_zone.GroundItems().end())
					m_zone_view->AddGroundItem(it->first, it->second.model, it->second.x, it->second.y, it->second.z,
						it->second.heading);
			break;
		case Type::GroundItemRemoved:
			if (m_zone_view)
				m_zone_view->RemoveGroundItem(static_cast<uint32_t>(event.value));
			m_ground.ZoneEvent(event);
			break;
		case Type::WorldContainerOpened:
			m_ground.ZoneEvent(event);
			break;
		case Type::TradeRequested:
		case Type::TradeBegan:
		case Type::TradeRefused:
		case Type::TradeCancelled:
		case Type::TradeReady:
		case Type::TradeFinished:
		case Type::TradeCoins:
		case Type::TradeItem:
			m_trade.ZoneEvent(event);
			break;
		case Type::Emote:
			ChatOut(event.text, ChatType::Emote);
			break;
		case Type::Profile:
			m_group.ProfileArrived(); // CheckForGroupChanges
			break;
		case Type::BookText:
			BookText(event.value, event.text);
			break;
		case Type::WhoAnswered:
			WhoAnswer(event.who);
			break;
		case Type::GuildInvited:
		case Type::GuildMemberRank:
		case Type::GuildDisbanded:
		case Type::GuildWarAsked:
		case Type::GuildPeaceAsked:
		case Type::GuildWarRefused:
		case Type::GuildPeaceRefused:
		case Type::GuildChanged:
		case Type::GuildRankChanged:
			m_guild.ZoneEvent(event);
			break;
		case Type::GroupInvited:
		case Type::GroupInviteCancelled:
		case Type::GroupFollowed:
		case Type::GroupUpdated:
			m_group.ZoneEvent(event);
			break;
		case Type::RaidInvited:
		case Type::RaidUpdated:
			// HandleWorldMessage's 0x425f and 0x4260 (the new UI's only).
			m_raid.ZoneEvent(event);
			break;
		case Type::SpellMemorized:
		case Type::SpellForgotten:
			m_spell_cast.ZoneEvent(static_cast<int>(event.type), event.id, event.value, m_now_ms);
			[[fallthrough]];
		case Type::SpellScribed:
		case Type::SpellBarUnlocked:
		case Type::SpellsSwapped:
		case Type::SpellDeleted:
			m_spell_book.ZoneEvent(static_cast<int>(event.type), event.id, event.value);
			break;
		case Type::SpellRecast:
		case Type::ManaChanged:
			m_spell_cast.ZoneEvent(static_cast<int>(event.type), event.id, event.value, m_now_ms);
			break;
		case Type::CastBegan:
			m_spell_cast.CastBegan(event.cast, m_now_ms);
			[[fallthrough]];
		case Type::SpellAction:
		case Type::Damaged: {
			if (event.type == Type::Damaged && event.cast.type != 231) {
				// Melee and the environment (msg_successful_hit).
				ApplyCombat(m_combat.Damaged(event.cast, m_zone, m_game_values.target, m_game_values.CurHp()));
				break;
			}
			if (event.type == Type::Damaged)
				SpellDamaged(event.cast);
			m_buffs.ZoneEvent(event); // IWasHit: one landing on us
			std::vector<SpellCasting::Line> lines;
			std::vector<SpellCasting::Animation> animations;
			std::vector<SpellEffects::Request> effects;
			SpellCasting(m_ui.spells, m_ui.eqstr).Handle(event, m_zone, lines, animations, effects);
			for (const SpellCasting::Line &line : lines)
				ChatOut(line.text, line.type);
			for (const SpellCasting::Animation &animation : animations) {
				if (animation.spawn != m_zone.MySpawnId())
					m_zone.PlayAnimation(animation.spawn, animation.id);
				else if (m_zone_view)
					m_zone_view->PlayAnimation(animation.id);
			}
			if (m_zone_view)
				for (const SpellEffects::Request &effect : effects)
					m_zone_view->StartSpellEffect(effect);
			break;
		}
		case Type::Died: {
			// CEverQuest::ReportDeath: whoever it was with goes (the trade, the merchant, the bank),
			// our target with auto attack, then the killing blow and the death are told.
			uint16_t victim = event.cast.target;
			if (m_trade.Active() && m_trade.Target() == victim)
				m_trade.Deactivate();
			if (victim == m_merchant.Merchant())
				m_merchant.Deactivate();
			if (victim == m_bank.Banker())
				m_bank.Deactivate(false);
			if (victim != 0 && victim == m_train.Guildmaster()) // CEverQuest_LeaveGuildMaster
				m_train.Deactivate();
			if (victim == m_game_values.target) {
				SetTarget(0);
				SetAutoAttack(false);
			}
			if (victim == m_zone.MySpawnId()) {
				SetAutoAttack(false); // CEverQuest_ReportDeath_ClearOutRentables
				// EQPlayer::IDied: we fall and lie there, still, to the death music.
				m_dead = true;
				m_death_ms = 0; // EQPlayer::IDied: MovePlayer starts the DeathTimer
				m_autorun = false;
				if (m_zone_view)
					m_zone_view->SetDead(true);
				if (m_sound)
					m_sound->SetMusicSelection(audio::EqSound::Death, true);
			}
			Combat::Report report = m_combat.Died(event, m_zone, m_game_values.target, m_game_values.CurHp());
			if (event.cast.type == 231) {
				std::vector<SpellCasting::Line> lines;
				std::vector<SpellCasting::Animation> animations;
				SpellCasting(m_ui.spells, m_ui.eqstr).DeathBlow(event.cast, report.told, m_zone, lines, animations);
				for (const SpellCasting::Line &line : lines)
					ChatOut(line.text, line.type);
			}
			ApplyCombat(report);
			break;
		}
		case Type::OwnAnimation:
			// The zone's swing of ours (Mob::DoAnim reaches us too).
			if (m_zone_view)
				m_zone_view->PlayAnimation(event.value);
			break;
		case Type::MissileLaunched: {
			// EQMissile::EQMissile: the caster's attack animation as it lets go, then the missile.
			int animation = Missiles::CasterAnimation(event.missile);
			if (animation != 0xfe) {
				if (event.missile.source != m_zone.MySpawnId())
					m_zone.PlayAnimation(event.missile.source, animation);
				else if (m_zone_view)
					m_zone_view->PlayAnimation(animation);
			}
			if (m_zone_view)
				m_zone_view->LaunchMissile(event.missile);
			break;
		}
		case Type::BuffChanged:
			m_buffs.ZoneEvent(event);
			break;
		case Type::BindWoundAnswered:
		case Type::Begged:
		case Type::Stolen:
			m_abilities.ZoneEvent(event);
			break;
		case Type::SkillUp: // msg_skillimprove
			ChatOut(FormatEqString(m_ui.eqstr, 12091, "You have become better at %1! (%2)",
				{SkillName(m_ui.eqstr, event.id), std::to_string(event.value)}), ChatType::Skills);
			break;
		case Type::Considered:
			PrintConsider(event.consider);
			break;
		case Type::Assisted:
			// HandleWorldMessage's 0x4200: their target is ours (DoPassageOfTime tells the zone), a
			// spawn we know or us; with AttackOnAssist, auto attack goes on.
			if (event.id == m_zone.MySpawnId() || m_zone.Spawns().count(event.id)) {
				SetTarget(event.id);
				if (m_config.attack_on_assist)
					SetAutoAttack(true);
			}
			break;
		case Type::TargetChanged:
			// The zone's answer to /target (or its refusal of a click).
			m_game_values.target = event.id;
			m_debug.SelectTarget(event.id);
			break;
		case Type::SpawnRemoved:
			if (event.id == m_game_values.target) {
				SetAutoAttack(false);
				m_game_values.target = 0;
			}
			// HandleWorldMessage's despawn: the merchant, banker, guildmaster or corpse we're dealing
			// with goes.
			if (event.id == m_merchant.Merchant())
				m_merchant.Deactivate();
			if (event.id == m_bank.Banker())
				m_bank.Deactivate(false);
			if (event.id != 0 && event.id == m_train.Guildmaster())
				m_train.Deactivate();
			m_loot.ZoneEvent(event);
			m_trade.ZoneEvent(event);
			break;
		case Type::TimeOfDay:
			m_time_of_day = event.text;
			break;
		case Type::Weather:
			m_weather.Receive(event.id, event.value);
			break;
		case Type::LoggedOut:
			m_group.Clear(); // ProcessGameLoop's "Cleaning up groups"
			m_raid.Clear();  // and its CRaid::ResetRaid
			ShowMessage("You have camped.", [this](bool) { EnterStart(); });
			break;
		case Type::Error:
			ShowMessage(m_strings.Get("SISATEXT_LOSTCON") + "\n\n(" + event.text + ")", [this](bool) { EnterStart(); });
			break;
		default:
			break;
		}
	}
}

// RealRender_World's part of the weather: OP_Weather taken up, and the cloud's fade or the thunder
// in an outdoor zone (time type 1, 2 or 5); then what the view draws of it.
void FrontEnd::WeatherFrame(uint64_t now_ms)
{
	uint8_t time_type = m_zone.Info().time_type;
	Weather::Place place;
	place.outdoors = time_type == 1 || time_type == 2 || time_type == 5;
	place.zone_id = m_zone.Profile().zone_id;
	m_zone_view->ZoneFog(place.fog_start, place.fog_end);
	place.clip_plane = m_clip_plane;
	int message = m_weather.Frame(m_sound.get(), now_ms, place);
	m_zone_view->SetWeather(m_weather.GetEffects());
	if (message)
		AddZoneLog(EqString(static_cast<uint32_t>(message), message == 542 ? "It begins to snow."
		                                                     : message == 540 ? "It begins to rain blood."
		                                                                      : "It begins to rain."),
			ZoneGrey);
}

std::string FrontEnd::EqString(uint32_t id, const std::string &fallback) const
{
	return m_ui.eqstr && m_ui.eqstr->Has(id) ? m_ui.eqstr->Format(id, {}) : fallback;
}

void FrontEnd::HandleZoneLine(const std::string &type)
{
	// CDisplay_DoTeleport (@004b40aa): "DRNTP", the zone (5 digits), y, x and z (6 each) and the
	// heading (3), each only if the type is long enough; zone 255 makes the y an index into the
	// zone's teleport table (OP_SendZonepoints).
	if (!m_zone_view || m_zone.GetState() != ZoneSession::State::InZone)
		return;
	auto field = [&](size_t at, size_t length) { return type.size() > at + length - 1 ? type.substr(at, length) : std::string("0"); };
	uint32_t zone = static_cast<uint32_t>(atol(field(5, 5).c_str()));
	float y = static_cast<float>(atof(field(10, 6).c_str()));
	float x = static_cast<float>(atof(field(16, 6).c_str()));
	float z = static_cast<float>(atof(field(22, 6).c_str()));
	float heading = static_cast<float>(atof(field(28, 3).c_str()));
	if (zone == 255) {
		int index = static_cast<int>(y);
		const ZonePoint *point = nullptr;
		for (const auto &p : m_zone.ZonePoints())
			if (static_cast<int>(p.index) == index)
				point = &p;
		if (!point) {
			Log("zone line " + type + ": index " + std::to_string(index) + " is not in the teleport table");
			return;
		}
		zone = point->zone;
		y = point->y;
		x = point->x;
		z = point->z;
		heading = point->heading;
	}
	ZoneMovement here = m_zone_view->Movement();
	uint32_t current = m_zone.Profile().zone_id;
	// DoTeleport's own adjustments: Freeport East and the Ocean of Tears line up 1044 apart, and
	// two of the Qeynos lines nudge the arrival.
	if (current == 34 && zone == 10)
		x = here.x - 1044;
	else if (current == 10 && zone == 34)
		x = here.x + 1044;
	else if (current == 3 && zone == 4)
		z = here.z + 2;
	else if (current == 2 && zone == 4)
		y += 10;
	Log("zone line " + type + " -> zone " + std::to_string(zone));
	if (zone == current) {
		// DoTeleportB within the zone: move there; 999999 (a heading of 513) keeps what we have.
		m_zone_view->SetPlayerPosition(x < 999999 ? x : here.x, y < 999999 ? y : here.y, z < 999999 ? z : here.z,
			heading < 513 ? heading : here.heading);
		return;
	}
	m_zone.RequestZoneChange(zone);
}

uint32_t FrontEnd::KeyWithModifiers(const InputEvent &event) const
{
	// ProcessKeyboardInput: the right Shift, Ctrl and Alt count as the left ones.
	int scan = event.scan == 0x36 ? 0x2a : event.scan == 0x9d ? 0x1d : event.scan == 0xb8 ? 0x38 : event.scan;
	return static_cast<uint32_t>(scan) | (event.shift ? KeyMap::Shift : 0) | (event.ctrl ? KeyMap::Ctrl : 0) |
	       (event.alt || m_alt_down ? KeyMap::Alt : 0);
}

void FrontEnd::ZoneKey(const InputEvent &event)
{
	if (event.scan == 0)
		return;
	uint32_t key = KeyWithModifiers(event);
	if (event.type == InputEvent::Type::KeyUp) {
		for (int cmd : m_key_bindings.Up(key))
			ExecuteCmd(cmd, false, 0);
		return;
	}
	// ProcessKeyDown: while the chat has the keys, a key that types a character runs nothing.
	int typed = KeyMap::TypedChar(event.scan);
	if (m_chat.Typing() && typed)
		return;
	for (int cmd : m_key_bindings.Down(key)) {
		// Windows repeats a held key; DirectInput doesn't.
		if (event.repeat)
			continue;
		ExecuteCmd(cmd, true, typed);
	}
}

void FrontEnd::ExecuteCmd(int cmd, bool down, int typed)
{
	(void)typed;
	// While the chat has the keys, nothing runs whose own key types a character
	// (isCharacterAllowed of ConvertScanCodeToChar of the command's key).
	if (m_chat.Typing() && KeyMap::TypedChar(static_cast<int>(m_key_bindings.Get(cmd, false) & 0xff)))
		return;
	bool in_zone = m_screen == Screen::Zone;
	auto toggle = [&](const char *screen) {
		if (down && in_zone)
			m_windows.Toggle(screen);
	};
	switch (cmd) {
	case KeyMap::Forward:
		// Forward stands us up from sitting, closing the book (Zeal's movement binds; the other
		// movement keys do nothing sitting).
		if (down && in_zone && m_zone.Self().stance == 110)
			ChangeStance(100);
		m_keys.forward = down;
		if (down)
			m_autorun = false;
		break;
	case KeyMap::Back:
		m_keys.back = down;
		if (down)
			m_autorun = false;
		break;
	case KeyMap::Autorun:
		// On and off with the key down; turned on, it lets go of Move back.
		if (down && in_zone && ZoneCanMove()) {
			m_autorun = !m_autorun;
			if (m_autorun)
				m_keys.back = false;
		}
		break;
	case KeyMap::Right:
		m_keys.right = down;
		break;
	case KeyMap::Left:
		m_keys.left = down;
		break;
	case KeyMap::Jump:
		if (down)
			m_jump_requested = true;
		break;
	case KeyMap::ZoomIn:
		m_keys.zoom_in = down;
		break;
	case KeyMap::PitchUp:
		// Down: Look down let go, and its speed back to nothing; up: this one's speed.
		m_keys.pitch_up = down;
		if (down)
			m_keys.pitch_down = false, m_keys.pitch_down_speed = 0;
		else
			m_keys.pitch_up_speed = 0;
		break;
	case KeyMap::PitchDown:
		m_keys.pitch_down = down;
		if (down)
			m_keys.pitch_up = false, m_keys.pitch_up_speed = 0;
		else
			m_keys.pitch_down_speed = 0;
		break;
	case KeyMap::ZoomOut:
		m_keys.zoom_out = down;
		break;
	case KeyMap::FullScreen:
		if (down && in_zone)
			m_ui_hidden = !m_ui_hidden;
		break;
	case KeyMap::ToggleCam:
		// CDisplay_ToggleView: first person and the chase camera.
		if (down && in_zone && m_zone_view)
			m_zone_view->CycleCamera();
		else if (down && m_exploring && m_charselect_view)
			m_charselect_view->ToggleCamera();
		break;
	default:
		break;
	}
	if (!down || !in_zone)
		return;
	switch (cmd) {
	case KeyMap::AutoAttack:
		ToggleAutoAttack();
		break;
	case KeyMap::Consider:
		InterpretCommand("/consider");
		break;
	case KeyMap::Hail:
		InterpretCommand("/hail");
		break;
	case KeyMap::TargetMe: {
		// ExecuteCmd 22: ourselves, or with ourselves the target already, our pet: the first spawn
		// that's ours and not a temporary pet (EQPlayer+0x15c), if there is one. Not without control
		// of ourselves (stance 102) nor while a transaction waits (DAT_007d0254).
		if (m_window_state.busy || m_zone.Self().stance == 102)
			break;
		uint16_t me = m_zone.MySpawnId();
		if (m_game_values.target != me) {
			SetTarget(me);
			m_group.Unselect();
			break;
		}
		for (const auto &[id, spawn] : m_zone.Spawns())
			if (id != me && spawn.pet_owner == me && !spawn.temporary_pet) {
				SetTarget(id);
				break;
			}
		break;
	}
	case KeyMap::TargetPc:
	case KeyMap::TargetNpc:
		TargetNearest(cmd == KeyMap::TargetNpc);
		break;
	case KeyMap::Party1:
	case KeyMap::Party2:
	case KeyMap::Party3:
	case KeyMap::Party4:
	case KeyMap::Party5:
		// ExecuteCmd 0x17..0x1b: not without control of ourselves nor while a transaction waits.
		if (!m_window_state.busy && m_zone.Self().stance != 102)
			m_group.TargetMember(cmd - KeyMap::Party1);
		break;
	case KeyMap::Who:
		if (m_zone.Self().stance != 102)
			Who("");
		break;
	case KeyMap::InviteFollow:
		// ExecuteCmd 0x46: invite, or accept the invitation we have.
		if (m_zone.Self().stance != 102) {
			if (m_group.Invited())
				m_group.Accept();
			else
				m_group.Invite();
		}
		break;
	case KeyMap::Disband:
		if (m_zone.Self().stance != 102)
			m_group.Disband();
		break;
	case KeyMap::CycleNearestTarget:
		// Zeal's: not while the chat line is open (UI_ChatInputCheck).
		if (!m_chat.Typing())
			CycleNearestTarget();
		break;
	case KeyMap::ToggleTarget:
		ToggleTarget();
		break;
	case KeyMap::CycleReply:
	case KeyMap::CycleReplyBack:
		CycleReply(cmd == KeyMap::CycleReplyBack);
		break;
	case KeyMap::Tell:
	case KeyMap::Reply:
	case KeyMap::RTarget:
	case KeyMap::CmdModeSay:
	case KeyMap::CmdModeEmote: {
		// ExecuteCmd 13, 59, 100, 87 and 88 (the new UI's branches), only while the chat line
		// isn't open (__InChatMode): "/tell " (eqstr 13974 and a space), "'", ":", or the tell list's
		// name Cycle reply is on: "/tell <name> " (";tell <name> " for another server's, with a
		// dot), "/rtarget <name> " (eqstr 14202); with no tells, eqstr 12364 (12365 for
		// /rtarget). (A GM's Send tell to the petitioner being helped, CPetitionQWnd, isn't ours.)
		if (m_chat.Typing())
			break;
		if (cmd == KeyMap::Tell) {
			OpenChatLine(EqString(13974, "/tell") + " ");
			break;
		}
		if (cmd == KeyMap::CmdModeSay || cmd == KeyMap::CmdModeEmote) {
			OpenChatLine(cmd == KeyMap::CmdModeSay ? "'" : ":");
			break;
		}
		if (m_tell_index < 0 || m_tell_index >= static_cast<int>(m_tell_list.size())) {
			uint32_t id = cmd == KeyMap::RTarget ? 12365 : 12364;
			ChatOut(EqString(id, id == 12364 ? "You have not received any tells, so you cannot reply."
			                                 : "You have not received any tells, so you cannot use reply target."),
				ChatType::Default);
			break;
		}
		const std::string &name = m_tell_list[m_tell_index];
		if (cmd == KeyMap::RTarget)
			OpenChatLine(EqString(14202, "/rtarget") + " " + name + " ");
		else if (name.find('.') != std::string::npos)
			OpenChatLine(";tell " + name + " ");
		else
			OpenChatLine(EqString(13974, "/tell") + " " + name + " ");
		break;
	}
	case KeyMap::Camp:
		StartCamp();
		break;
	case KeyMap::SitStand:
		Sit();
		break;
	case KeyMap::RunWalk:
		m_run = !m_run;
		AddZoneLog(m_run ? "You are now running." : "You are now walking.", ZoneGrey);
		break;
	case KeyMap::HistoryUp:
	case KeyMap::HistoryDown:
		m_chat.History(cmd == KeyMap::HistoryUp);
		break;
	case KeyMap::SpellBook:
		// The book opened, or closed when it's open (DeactivateSpellBook through the window's 0x88).
		if (m_spell_book.Activated())
			m_spell_book.Deactivate();
		else
			m_spell_book.Activate(m_now_ms);
		break;
	case KeyMap::Inventory:
		// Not while a mouse button's down on the world or a transaction waits (DAT_007d0254).
		if (!m_mouse_turn && !m_mouse_pan && !m_window_state.busy)
			m_windows.Toggle("InventoryWindow");
		break;
	case KeyMap::Abilities:
	case KeyMap::Combat:
	case KeyMap::Socials:
	case KeyMap::Main:
		// CActionsWnd::Activate at its page.
		if (SidlWindow *actions = m_windows.Find("ActionsWindow")) {
			const char *page = cmd == KeyMap::Main ? "ActionsMainPage" : cmd == KeyMap::Abilities ? "ActionsAbilitiesPage"
			                   : cmd == KeyMap::Combat ? "ActionsCombatPage" : "ActionsSocialsPage";
			if (SidlWindow::Control *tabs = actions->Find("ACTW_ActionsSubwindows"))
				for (int i = 0; i < static_cast<int>(tabs->children.size()); ++i)
					if (actions->Item(tabs->children[i]).def->screen_id == page)
						actions->SelectTab(*tabs, i);
			m_windows.Show("ActionsWindow", true);
			m_windows.Raise("ActionsWindow");
		}
		break;
	case KeyMap::TogglePlayerWin: toggle("PlayerWindow"); break;
	case KeyMap::TogglePartyWin: toggle("GroupWindow"); break;
	case KeyMap::ToggleTargetWin: toggle("TargetWindow"); break;
	case KeyMap::ToggleSpellsWin: toggle("CastSpellWnd"); break;
	case KeyMap::ToggleBuffWin: toggle("BuffWindow"); break;
	case KeyMap::ToggleHotboxWin: toggle("HotButtonWnd"); break;
	case KeyMap::ToggleMainMenuWin: toggle("ActionsWindow"); break;
	case KeyMap::ToggleFriendsWin: toggle("FriendsWindow"); break;
	case KeyMap::TogglePetInfoWin: toggle("PetInfoWindow"); break;
	case KeyMap::ToggleOptionsWin: toggle("OptionsWindow"); break;
	case KeyMap::ToggleSelectorWin: toggle("SelectorWindow"); break;
	case KeyMap::ToggleCompassWin: toggle("CompassWindow"); break;
	case KeyMap::ToggleRaidWin:
		// ExecuteCmd 0x6d: pressed, with the raid's expansion; CRaidWnd::Activate or down.
		if (down && in_zone && RaidAllowed())
			m_raid_window.Toggle();
		break;
	default:
		if (cmd >= KeyMap::Hot1 && cmd <= KeyMap::Hot10) {
			// CHotButtonWnd::DoHotButton: the button of the page showing.
			std::string action = DoHotButton(m_windows, m_window_state, cmd - KeyMap::Hot1);
			if (!action.empty())
				ActionsClick(action);
		}
		else if (cmd >= KeyMap::HotPage1 && cmd <= KeyMap::HotPage10)
			m_window_state.hot_button_page = cmd - KeyMap::HotPage1; // Activate_HotkeyPage
		else if (cmd >= KeyMap::Cast1 && cmd <= KeyMap::Cast8)
			m_spell_book.GemLeftClick(cmd - KeyMap::Cast1, m_now_ms); // CCastSpellWnd::HandleSpellLeftClick
		break;
	}
}

void FrontEnd::SaveKeyBinding(int id, bool alternate, uint32_t key)
{
	if (m_config.settings_ini.empty())
		return;
	IniFile ini;
	ini.Load(m_config.settings_ini); // keep the rest
	KeyBindings::Store(ini, id, alternate, key);
	if (!ini.Save(m_config.settings_ini))
		Log("keymap: couldn't write " + m_config.settings_ini);
}

void FrontEnd::SaveUserColor(int index, uint32_t rgb)
{
	// COptionsWnd::SaveColors: CDisplay_SetUserDefinedColor, and [TextColors] User_<n+1>_Red,
	// _Green and _Blue (my-client.ini for eqclient.ini).
	m_chat_colors.Set(index, rgb);
	m_chat.SetLinkColor(m_chat_colors.Get(ItemTags::ColorType));
	WriteUserColor(index, rgb);
}

void FrontEnd::WriteUserColor(int index, uint32_t rgb)
{
	if (m_config.settings_ini.empty())
		return;
	IniFile ini;
	ini.Load(m_config.settings_ini); // keep the rest
	std::string key = "User_" + std::to_string(index + 1) + "_";
	ini.Set("TextColors", key + "Red", std::to_string(rgb >> 16 & 0xff));
	ini.Set("TextColors", key + "Green", std::to_string(rgb >> 8 & 0xff));
	ini.Set("TextColors", key + "Blue", std::to_string(rgb & 0xff));
	if (!ini.Save(m_config.settings_ini))
		Log("colors: couldn't write " + m_config.settings_ini);
}

void FrontEnd::ResetKeyBindings()
{
	if (m_config.settings_ini.empty())
		return;
	IniFile ini;
	ini.Load(m_config.settings_ini);
	for (const KeyMap::Command &command : KeyMap::List())
		KeyBindings::Remove(ini, command.id);
	if (!ini.Save(m_config.settings_ini))
		Log("keymap: couldn't write " + m_config.settings_ini);
}

bool FrontEnd::RaidAllowed() const
{
	return (m_zone.Profile().expansions & 8) != 0 || m_world.TestServer();
}

bool FrontEnd::ZoneCanMove() const
{
	return !m_dialog.open && m_zone.GetState() == ZoneSession::State::InZone;
}

void FrontEnd::CameraControls(uint64_t now_ms)
{
	// process_physics' frame units for us: 50 ms ticks since the last frame, at most 12.
	float units = m_controls_ms ? std::min((now_ms - m_controls_ms) * 0.02f, 12.0f) : 0;
	m_controls_ms = now_ms;
	// ProcessControls: Zoom in and out held move a zoomable camera in or out 2 units a frame unit.
	if (m_keys.zoom_in)
		m_zone_view->ZoomBy(-2 * units);
	if (m_keys.zoom_out)
		m_zone_view->ZoomBy(2 * units);
	// Look up and down held: a speed that goes up by one a frame to 12, times the frame units, in
	// EQ's 512ths of a turn (to 128 either way; our view's own limits are narrower).
	constexpr float Degrees = 360.0f / 512;
	if (m_keys.pitch_up) {
		m_keys.pitch_up_speed = std::min(m_keys.pitch_up_speed + 1, 12);
		m_zone_view->Pitch(m_keys.pitch_up_speed * units * Degrees);
	}
	if (m_keys.pitch_down) {
		m_keys.pitch_down_speed = std::min(m_keys.pitch_down_speed + 1, 12);
		m_zone_view->Pitch(-m_keys.pitch_down_speed * units * Degrees);
	}
}

MoveControls FrontEnd::ZoneControls()
{
	MoveControls controls;
	bool can_move = ZoneCanMove();
	// EQPlayer::ChangeStance: any change of our stance, ours or the server's, ends autorun.
	uint8_t stance = m_zone.Self().stance;
	if (stance != m_autorun_stance)
		m_autorun = false;
	m_autorun_stance = stance;
	// ProcessControls: with control of ourselves (standing or crouching), auto-follow steers; Move
	// back and the turn (or strafe) keys end it.
	if (m_following && (m_keys.back || m_keys.left || m_keys.right))
		TurnOffAutoFollow();
	if (can_move && (stance == 100 || stance == 111))
		FollowPlayerAI();
	if (can_move) {
		// ProcessControls: forward with the key, autorun, or both mouse buttons.
		controls.forward = m_keys.forward || m_autorun || (m_mouse_turn && m_mouse_pan);
		controls.back = m_keys.back;
		controls.left = m_keys.left;
		controls.right = m_keys.right;
		controls.strafe = m_mouse_turn;
		controls.jump = m_jump_requested;
	}
	controls.run = m_run;
	m_jump_requested = false;
	return controls;
}

void FrontEnd::ZoneEvent(const InputEvent &event)
{
	// The debug menu's stat card takes clicks on it, unless a drag is holding the mouse.
	if (event.IsMouse() && !m_mouse_turn && !m_mouse_pan && m_debug.OnMouse(event)) {
		if (m_debug.TakeChanged())
			SaveDebugSettings();
		return;
	}
	// Then the windows, unless the world has the mouse. The keys go to one edit box: a press in
	// the social editor's or the quantity window's takes them from the others, and one in the chat
	// window from the editors.
	SidlWindow *social_editor = m_windows.Find("SocialEditWnd");
	SidlWindow *quantity = m_windows.Find("QuantityWnd");
	SidlWindow *color_picker = m_windows.Find(ColorPicker::Screen);
	// The chat's links light under the mouse, and a click on one asks the zone about it.
	ItemTag link;
	if (event.IsMouse() && !m_mouse_turn && !m_mouse_pan &&
	    m_chat.OnMouse(event, m_windows.ScreenAt(event.x, event.y) == "ChatWindow", link))
		m_zone.ClickItemLink(link.id, link.name);
	if (event.IsMouse() && !m_mouse_turn && !m_mouse_pan && m_windows.OnMouse(event, m_now_ms)) {
		if (event.type == InputEvent::Type::MouseDown) {
			std::string pressed = m_windows.ScreenAt(event.x, event.y);
			SidlWindow *pressed_editor = pressed == "SocialEditWnd"     ? social_editor
			                             : pressed == "QuantityWnd"       ? quantity
			                             : pressed == ColorPicker::Screen ? color_picker
			                                                              : nullptr;
			if (pressed_editor && pressed_editor->Typing()) {
				m_chat.Close();
				for (SidlWindow *other : {social_editor, quantity, color_picker})
					if (other && other != pressed_editor)
						other->Focus("");
			}
			else if (pressed == "ChatWindow" && m_chat.Typing())
				for (SidlWindow *other : {social_editor, quantity, color_picker})
					if (other)
						other->Focus("");
		}
		return;
	}
	SidlWindow *editor = quantity && quantity->Typing()           ? quantity
	                     : color_picker && color_picker->Typing() ? color_picker
	                                                              : social_editor;
	bool editing = editor && editor->Typing();
	switch (event.type) {
	case InputEvent::Type::Wheel:
		// Out of first person into the chase camera and back, as well as closer and farther.
		if (m_zone_view)
			m_zone_view->Zoom(event.wheel);
		return;
	case InputEvent::Type::RightUp:
		// EQ_3DView_MouseUp's right button, let go within 500 ms of going down on the world: the
		// spawn under the cursor (CEverQuest::RMouseUp, RightClickedOnPlayer).
		if (m_right_world_ms && m_now_ms - m_right_world_ms <= 500 && m_zone_view && !m_ui_hidden)
			if (uint16_t id = m_zone_view->PickSpawn(event.x, event.y, m_zone.MySpawnId()))
				RightClickedOnPlayer(id);
		m_right_world_ms = 0;
		return;
	case InputEvent::Type::RightDown:
		m_right_world_ms = m_now_ms ? m_now_ms : 1;
		// Mouse look: the character turns to where the camera looks, then turns with the mouse.
		if (!m_mouse_pan) {
			m_look_x = event.x;
			m_look_y = event.y;
		}
		m_mouse_turn = true;
		if (m_zone_view && m_zone.Self().stance != 110) // sitting, the mouse doesn't turn us
			m_zone_view->FaceCamera();
		return;
	case InputEvent::Type::MouseDown:
	case InputEvent::Type::DoubleClick:
		// Panning the chase camera (or running, with the right button too).
		if (!m_mouse_turn) {
			m_look_x = event.x;
			m_look_y = event.y;
		}
		m_mouse_pan = true;
		m_world_click = true;
		m_click_travel = 0;
		break;
	case InputEvent::Type::MouseUp:
		// CEverQuest_LMouseUp, for a click that didn't drag (and not while a transaction waits:
		// DAT_007d0254): what's on the cursor as a button goes; then the spawn under the cursor
		// (LeftClickedOnPlayer: our target, and an item or coins on the cursor go to them, a
		// trade); else, not trading, an item on the cursor goes on the ground and with nothing on
		// it the ground item under it comes up (GroundItems); else the door or switch under it
		// (CEverQuest_ClickedSwitch). Nobody: the target stays.
		if (m_world_click && m_click_travel <= 3 && !m_window_state.busy && (!m_ui_hidden || m_mouse_turn)) {
			m_window_state.ClearCursor(); // RemoveAttachment, and the click goes on
			if (m_zone_view)
				WorldClick(event.x, event.y);
		}
		m_world_click = false;
		break;
	case InputEvent::Type::MouseMove:
		if (m_mouse_turn || m_mouse_pan) {
			// Right turns right, up looks up. The right button turns the character; the left
			// circles the camera, and only in the chase view (in first person it does nothing,
			// and the cursor stays free).
			bool turn = m_mouse_turn;
			bool pan = !turn && m_zone_view && m_zone_view->View() == ZoneView::CameraView::Chase;
			m_click_travel += std::abs(event.x - m_look_x) + std::abs(event.y - m_look_y);
			if (!turn && !pan) {
				m_mouse_grabbed = false;
				m_look_x = event.x;
				m_look_y = event.y;
				break;
			}
			int moved_x = event.x - m_look_x, moved_y = event.y - m_look_y;
			if (moved_x == 0 && moved_y == 0)
				return; // the host putting the cursor back
			m_mouse_grabbed = true;
			float dx = -0.6f * static_cast<float>(moved_x);
			float dy = -0.25f * static_cast<float>(moved_y);
			if (m_zone_view) {
				if (!turn)
					m_zone_view->Orbit(dx);
				else if (m_zone.Self().stance != 110) // sitting, the mouse doesn't turn us
					m_zone_view->Turn(dx);
				m_zone_view->Pitch(dy);
			}
			return;
		}
		break;
	default:
		break;
	}
	// The chat window's input line: Enter (or a /) opens it, Enter sends it, Escape closes it.
	// While it's open, letters go into it and Up and Down go back through what was sent; the other
	// arrows still move.
	if (event.type == InputEvent::Type::Char) {
		if (editing)
			editor->OnKey(event);
		else if (m_chat.Typing())
			m_chat.OnChar(event.ch);
		else if (event.ch == '/' && m_chat.Attached()) {
			m_windows.Show("ChatWindow", true);
			m_chat.Open("/");
		}
		return;
	}
	if (event.type != InputEvent::Type::Key)
		return;
	// The options' keyboard page waiting for a key takes it ahead of everything (ProcessKeyboardInput).
	if (m_options.Assigning() && m_options.Key(event.scan, event.shift, event.ctrl, event.alt))
		return;
	// Typing into the social editor: Enter does nothing, Escape closes it (below). Into the
	// quantity window's box, Enter is its Accept.
	if (editing && event.key == InputEvent::Key::Enter && editor == quantity) {
		InventoryClickResult moved;
		QuantityEnter(m_windows, m_window_state, m_zone, moved);
		if (moved.coin_sound && m_sound)
			m_sound->PlayWave(0x8a);
		return;
	}
	if (editing && (editor->OnKey(event) || event.key == InputEvent::Key::Enter))
		return;
	// ProcessKeyboardInput: with an edit box other than the chat's taking the keys, only Escape,
	// Delete and the arrows (without Shift) go on to ProcessKeyDown.
	if (editing && !(!event.shift && (event.scan == 0x01 || event.scan == 0xd3 || event.scan == 0xc8 ||
	                                  event.scan == 0xd0 || event.scan == 0xcb || event.scan == 0xcd)))
		return;
	if (m_chat.Typing()) {
		std::string line;
		if (m_chat.OnKey(event, line)) {
			if (event.key == InputEvent::Key::Enter && !line.empty())
				InterpretCommand(line);
			return;
		}
	}
	// A book with the keyboard turns its pages (CBookWnd::HandleKeyboardMsg).
	if (m_book.Key(event.key, event.shift, event.ctrl, event.alt))
		return;
	// The open spellbook's page keys (HandleKeys, ahead of the movement keys) and Delete.
	if (m_spell_book.Key(event.key, m_now_ms))
		return;
	// F11 is the debug menu's, ahead of the keymap (ours: the keymap's Toggle netstat has it too).
	if (event.key == InputEvent::Key::F11) {
		m_debug.open = !m_debug.open;
		SaveDebugSettings();
		return;
	}
	switch (event.key) {
	case InputEvent::Key::Enter:
		// ProcessKeyDown's Enter: ExecuteCmd 0xce, the chat's input line.
		if (m_chat.Attached()) {
			m_windows.Show("ChatWindow", true);
			m_chat.Open();
		}
		break;
	case InputEvent::Key::Escape: {
		// A button on the cursor goes, and that's all (CDisplay_NewUIProcessEscape's RemoveAttachment).
		if (m_window_state.Attached()) {
			m_window_state.ClearCursor();
			break;
		}
		// ExecuteCmd's escape: no target, then CDisplay_NewUIProcessEscape closes the first of its
		// windows that's open (of the ones we have: the item display, quantity, the bank, a merchant,
		// loot, a trade, a book, a note, help, skills, then every bag window at once, options, the
		// spellbook, the social editor, inventory).
		SetTarget(0);
		ChatOut(EqString(12375, "You no longer have a target."), ChatType::Default);
		for (const char *screen :
			{"ItemDisplayWindow", "QuantityWnd", "BankWnd", "MerchantWnd", "LootWnd", "TradeWnd", "BookWindow",
			 "NoteWindow", "HelpWindow", "SkillsWindow", "", "OptionsWindow", "SpellBookWnd", "TrainWindow", "SocialEditWnd",
			 "InventoryWindow"}) {
			if (!*screen) {
				if (m_containers.CloseAll())
					break;
				continue;
			}
			if (std::string(screen) == "BankWnd") {
				if (m_bank.Active()) {
					m_bank.Deactivate(true);
					break;
				}
				continue;
			}
			if (std::string(screen) == "TrainWindow") {
				if (m_train.Active()) {
					m_train.Deactivate();
					break;
				}
				continue;
			}
			if (std::string(screen) == "MerchantWnd") {
				if (m_merchant.Active()) {
					m_merchant.Deactivate();
					break;
				}
				continue;
			}
			if (std::string(screen) == "LootWnd") {
				if (m_loot.Active()) {
					m_loot.Deactivate();
					break;
				}
				continue;
			}
			if (std::string(screen) == "TradeWnd") { // or the give window
				if (m_trade.Active()) {
					m_trade.Deactivate();
					break;
				}
				continue;
			}
			if (m_windows.Shown(screen)) {
				if (std::string(screen) == "SpellBookWnd")
					m_spell_book.Deactivate();
				else
					m_windows.Show(screen, false);
				break;
			}
		}
		break;
	}
	default:
		break;
	}
	// Then whatever the key's bound to (ProcessMovementKeys).
	ZoneKey(event);
}

void FrontEnd::RenderZone(Canvas &canvas, uint64_t now_ms)
{
	int width = canvas.Width(), height = canvas.Height();

	// The world, or the loading screen until the zone is in.
	bool world = m_zone_view && m_zone_view->Loaded() && m_zone_view->Error().empty();
	if (m_zone_view && m_zone_view->Loaded() && !m_zone_view->Error().empty() && !m_zone_error_logged) {
		Log("zone view: " + m_zone_view->Error());
		AddZoneLog("Couldn't load the zone: " + m_zone_view->Error(), ZoneGrey);
		m_zone_error_logged = true;
	}
	using Clock = std::chrono::steady_clock;
	auto since = [](Clock::time_point start) { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); };
	DebugMenu::FrameTimes times;
	times.present = m_present_ms;
	times.upload = m_upload_ms;
	times.update = m_update_ms;
	if (world) {
		Clock::time_point tags_start = Clock::now();
		BuildNameSprites(now_ms);
		times.tags = since(tags_start);
		m_zone_view->Render(canvas, now_ms);
		const ZoneView::RenderTimes &render_times = m_zone_view->Times();
		times.zone = render_times.zone;
		times.models = render_times.models;
		times.submit = render_times.submit;
	}
	else {
		canvas.Fill({0, 0, width, height}, 0x000000);
		std::string loading = "LOADING, PLEASE WAIT...";
		TextSize size = m_text.Measure(loading, 20);
		m_text.Draw(canvas, (width - size.w) / 2, height / 2 - size.h / 2, loading, 20, ZoneWhite);
	}

	Clock::time_point ui_start = Clock::now();
	const TextStats text_before = m_text.stats;
	// The 2D drawing over the world, a piece at a time (RetainedUi), in order.
	enum : uint64_t { HelpPiece = 2, AltTooltipsPiece, StatCardPiece, CursorPiece };
	std::vector<RetainedUi::Piece> pieces;
	if (world)
		pieces.push_back({HelpPiece, [this, width, height](Canvas &c) {
			std::string help = std::string("Arrows/WASD move   Space jump   Ctrl+R ") + (m_run ? "walk" : "run") +
			                   "   Right drag look   Wheel zoom   F9 camera   F11 debug   Enter chat";
			TextSize size = m_text.Measure(help, FontSize::Small);
			m_text.Draw(c, width - size.w - 10, height - size.h - 8, help, FontSize::Small, ZoneGrey);
		}});
	m_chat.Layout(m_text);
	if (!m_ui_hidden) // DrawWindows
		m_windows.Pieces(m_text, now_ms, pieces);
	pieces.push_back({AltTooltipsPiece, [this](Canvas &c) {
		if (m_ui_hidden) // the windows' PostDraws, from DrawWindows
			return;
		// CCastSpellWnd::DrawSpellButtonTooltips: Alt held shows each gem's tooltip, by its top right.
		if (m_alt_down && m_windows.Shown("CastSpellWnd"))
			if (const SidlWindow *gems = m_windows.Find("CastSpellWnd"); gems && !gems->Minimized())
				for (int gem = 0; gem < ZoneProfile::Gems; ++gem)
					if (const SidlWindow::Control *g = gems->Find("CSPW_Spell" + std::to_string(gem)); g && gems->Shown(*g))
						m_windows.DrawTooltip(c, m_text, g->tooltip, g->def->font, g->rect.Right() + 2, g->rect.y + 2, false);
		// CContainerWnd::PostDraw and CBankWnd's: Alt held over a bag or the bank shows each slot's tooltip.
		if (m_alt_down) {
			Rect area = ScreenArea();
			m_containers.DrawTooltips(c, m_text, m_mouse_x - area.x, m_mouse_y - area.y);
			m_bank.DrawTooltips(c, m_text, m_mouse_x - area.x, m_mouse_y - area.y);
			m_merchant.DrawTooltips(c, m_text, m_mouse_x - area.x, m_mouse_y - area.y);
			m_loot.DrawTooltips(c, m_text, m_mouse_x - area.x, m_mouse_y - area.y);
			m_trade.DrawTooltips(c, m_text, m_mouse_x - area.x, m_mouse_y - area.y);
			DrawBuffTooltips(c, m_mouse_x - area.x, m_mouse_y - area.y);
		}
	}});
	// Top left and right: the debug menu's stat card and radar, above the windows.
	double card_ms = 0;
	pieces.push_back({StatCardPiece, [this, height, &card_ms, since](Canvas &c) {
		Clock::time_point start = Clock::now();
		m_debug.Draw(c, m_text, m_zone, m_zone_view.get(), height - 10);
		if (!c.Recording())
			card_ms += since(start);
	}});
	// The item or button on the cursor, and the cursor, over it all: here when the world fills the
	// window and no dialog's over it (else Render draws them, after the dialog).
	m_cursor_drawn = m_screen_fills_window && !m_dialog.open;
	if (m_cursor_drawn)
		pieces.push_back({CursorPiece, [this, now_ms](Canvas &c) {
			if (!m_cursor_visible || m_mouse_grabbed || m_ui_hidden)
				return;
			DrawCursorAttachment(c, m_text, m_windows, m_window_state, m_game_values, m_mouse_x, m_mouse_y, now_ms);
			if (!m_windows.DrawCursor(c, m_mouse_x, m_mouse_y, now_ms))
				c.Blit(m_art.Get("cursor.bmp"), m_mouse_x, m_mouse_y, true);
		}});

	// Kept from frame to frame on a GPU host's canvas when it's all ours over the view: the world
	// fills the window and Finish cleared all of it, no dialog darkens it, and no debug lines are
	// drawn straight into it.
	CanvasDirty *dirty = canvas.Tracker();
	bool retained = world && dirty && m_screen_fills_window && !m_dialog.open && !m_zone_view->DrawsDebugLines() &&
	                dirty->BlockThisFrame() && dirty->Block().x == 0 && dirty->Block().y == 0 &&
	                dirty->Block().w == width && dirty->Block().h == height;
	if (retained)
		m_retained_ui.Frame(canvas, pieces);
	else {
		for (const RetainedUi::Piece &piece : pieces) {
			canvas.SetAlpha(255);
			canvas.ResetClip();
			piece.draw(canvas);
		}
		if (dirty)
			m_retained_ui.Forget(canvas);
	}
	times.ui_debug = card_ms;
	times.ui = since(ui_start);
	times.ui_windows = times.ui - card_ms;
	times.slowest = m_windows.SlowestWindow();
	times.slowest_ms = m_windows.SlowestWindowMs();
	times.ui_text = m_text.stats.ms - text_before.ms;
	times.ui_rasterized = m_text.stats.rasterized - text_before.rasterized;
	times.ui_redrawn = retained ? 100.0 * static_cast<double>(m_retained_ui.RedrawnPixels()) / std::max(width * height, 1) : 100;
	m_debug.Frame(times);
}
