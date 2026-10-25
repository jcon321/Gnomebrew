#include "frontend.h"

#include "eqstr.h"
#include "game_windows.h"
#include "ini_file.h"
#include "inventory.h"

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
	render::CharacterAppearance PlayerAppearance(const ZoneSession &zone)
	{
		const ZoneSpawn &me = zone.Self();
		render::CharacterAppearance appearance;
		appearance.race = me.race ? me.race : zone.Profile().race;
		appearance.gender = me.gender;
		appearance.face = me.face;
		appearance.height = me.size;
		appearance.class_id = me.class_id ? me.class_id : static_cast<uint8_t>(zone.Profile().class_id);
		for (int k = 0; k < render::EquipSlots; ++k) {
			appearance.equip[k] = me.equipment[k];
			appearance.tint[k] = me.colors[k];
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
	m_zone.SetStringTable(m_ui.eqstr);
	m_zone.SetRecordDir(config.record_dir);
	m_debug.SetVSync(m_ui.vsync, m_ui.set_vsync);
	m_user.text = config.username;
	if (!m_ui.eq_dir.empty() && m_ui.definitions) {
		// Character select's zone takes a moment to load; start while the player logs in.
		m_charselect_view = std::make_unique<CharacterSelectView>(m_ui.eq_dir, MakeRenderer());
		m_charselect_view->SetVeliousTextures(config.velious_textures);
		m_charselect_view->StartLoading();
	}
	m_rules.CheckInstall(m_ui.eq_dir);
	if (m_ui.mixer && !m_ui.eq_dir.empty()) {
		m_sound = std::make_unique<audio::EqSound>(*m_ui.mixer, m_ui.eq_dir, m_ui.sound);
		m_debug.SetSound(m_sound.get());
	}
	m_debug.SetWeather(&m_weather);
	if (!m_config.settings_ini.empty()) {
		m_chat_colors.Load(m_config.settings_ini);
		IniFile ini;
		if (ini.Load(m_config.settings_ini))
			m_debug.Load(ini);
	}
	m_game_values.zone = &m_zone;
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

void FrontEnd::OnEvent(const InputEvent &event)
{
	if (event.IsMouse()) {
		m_mouse_x = event.x;
		m_mouse_y = event.y;
	}

	// Held movement keys are let go even while a dialog has the input.
	if (event.type == InputEvent::Type::KeyUp) {
		SetZoneKey(event.key, false, false);
		return;
	}
	if (event.type == InputEvent::Type::FocusLost) {
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
		if (me.look_serial != m_my_look_serial) {
			m_my_look_serial = me.look_serial;
			m_zone_view->SetPlayer(PlayerAppearance(m_zone), me.walk_speed, me.run_speed);
		}
		m_zone_view->SetMyLight(me.light);
		m_zone_view->SetDebugDraw(m_debug.Drawing());
		MoveControls controls = ZoneControls();
		// Sitting nothing moves us, a jump included (ProcessControls takes the keys only standing or
		// crouching). Forward stands us up (ZoneEvent), and we go at once.
		if (m_zone.Self().stance == 110) {
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
	}
	if (m_screen == Screen::Zone) {
		Rect area = ScreenArea();
		m_windows.Update(now_ms, m_mouse_x - area.x, m_mouse_y - area.y);
		HandleWindowClicks();
		m_spell_book.Frame(now_ms);
		// DoPassageOfTime: the social going's next line.
		std::string social_line = SocialStep(m_window_state, now_ms);
		if (!social_line.empty())
			InterpretCommand(social_line);
		m_game_values.running = m_run;
		if (m_zone_view)
			m_game_values.heading = m_zone_view->Movement().heading;
		UpdateGameWindows(m_windows, m_game_values, m_window_state);
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
	Canvas screen(canvas.Row(area.y) + area.x, area.w, area.h, canvas.Stride());
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
		Canvas dialog(canvas.Row(area.y) + area.x, area.w, area.h, canvas.Stride());
		RenderDialog(dialog);
	}

	// The item or button on the cursor, over everything but the cursor.
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
			m_zone.SetGuilds(m_world.Guilds());
			if (!m_zone.Begin(m_world.ZoneAddress(), m_world.ZonePort(), m_world.EnteringCharacter())) {
				ShowMessage(m_strings.Get("SISATEXT_CONNECT_FAILED"), [this](bool) { EnterStart(); });
				break;
			}
			EnterZoneScreen(m_zoning);
			break;
		case Type::CharacterCreated: {
			// The new list is already in; character select starts on the new character.
			m_creating = false;
			if (m_dialog.open)
				CloseDialog(true);
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
				if (m_dialog.open)
					CloseDialog(true);
				m_creation->NameRejected();
				ShowMessage(m_creation->RejectedText());
			}
			else
				ShowMessage("Character creation failed: " + event.text);
			break;
		case Type::ZoneUnavailable:
			m_entering_world = false;
			RefreshCharacterButtons();
			ShowMessage("That zone is unavailable right now.\n\n" + event.text);
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
	// CDisplay::InitDDraw starts the opening music when eqgame starts, and initializeNetworkGame
	// character select's whenever it's entered (the same eqtheme.mp3).
	if (m_sound) {
		if (!m_opening_played)
			m_sound->SetMusicSelection(audio::EqSound::Opening, true);
		m_opening_played = true;
		m_sound->SetMusicSelection(audio::EqSound::CharacterSelect, true);
	}

	// CCharacterSelect::AlternateBackgroundAvailable: Rotate starts checked.
	if (auto *rotate = m_charselect.Find("Rotate_Button"))
		rotate->checked = true;
	if (m_charselect_view) {
		m_charselect_view->SetRotate(true);
		m_charselect_view->SetCloseUp(false);
	}

	// Start on the remembered slot (LastCharSel), else the first character.
	m_selected_slot = -1;
	const auto &slots = m_world.Characters();
	int last = m_config.last_character;
	if (last >= 0 && last < CharacterButtons && last < static_cast<int>(slots.size()) && !slots[last].Empty())
		m_selected_slot = last;
	for (int i = 0; m_selected_slot < 0 && i < CharacterButtons && i < static_cast<int>(slots.size()); ++i) {
		if (!slots[i].Empty())
			m_selected_slot = i;
	}
	if (m_selected_slot >= 0)
		m_config.last_character = m_selected_slot;
	RefreshCharacterButtons();
}

void FrontEnd::RefreshCharacterButtons()
{
	const auto &slots = m_world.Characters();
	if (m_selected_slot >= 0 && (m_selected_slot >= static_cast<int>(slots.size()) || slots[m_selected_slot].Empty()))
		m_selected_slot = -1;

	for (int i = 0; i < CharacterButtons; ++i) {
		SidlWindow::Control *button = m_charselect.Find(SlotButton(i));
		if (!button)
			continue;
		bool filled = i < static_cast<int>(slots.size()) && !slots[i].Empty();
		button->text = filled ? slots[i].name : button->def->text; // empty slots keep "Create New Character"
		button->checked = i == m_selected_slot;
		button->enabled = !m_entering_world;
	}

	bool have_character = m_selected_slot >= 0 && !m_entering_world;
	if (auto *enter = m_charselect.Find("Enter_World_Button"))
		enter->enabled = have_character;
	if (auto *del = m_charselect.Find("Delete_Button"))
		del->enabled = have_character;
	if (auto *quit = m_charselect.Find("Quit_Button"))
		quit->enabled = !m_entering_world;

	if (m_charselect_view) {
		if (m_selected_slot >= 0) {
			const CharacterSlot &c = slots[m_selected_slot];
			render::CharacterAppearance appearance;
			appearance.race = c.race;
			appearance.gender = c.gender;
			appearance.face = c.face;
			appearance.class_id = static_cast<uint8_t>(c.class_id);
			for (int k = 0; k < render::EquipSlots; ++k) {
				appearance.equip[k] = c.equip[k];
				appearance.tint[k] = c.tint[k];
			}
			m_charselect_view->SetCharacter(&appearance, c.class_id);
		}
		else
			m_charselect_view->SetCharacter(nullptr, 0);
	}
}

void FrontEnd::SelectSlot(int slot)
{
	const auto &slots = m_world.Characters();
	if (slot < 0 || slot >= CharacterButtons)
		return;
	if (slot >= static_cast<int>(slots.size()) || slots[slot].Empty()) {
		// An empty slot is "Create New Character".
		RefreshCharacterButtons();
		EnterCharacterCreate();
		return;
	}
	m_selected_slot = slot;
	m_config.last_character = slot; // DoCharacterSelection writes LastCharSel when the selection changes
	RefreshCharacterButtons();
}

void FrontEnd::EnterWorldSelected()
{
	const auto &slots = m_world.Characters();
	if (m_entering_world || m_selected_slot < 0 || m_selected_slot >= static_cast<int>(slots.size()))
		return;
	if (m_world.EnterWorld(slots[m_selected_slot].name)) {
		m_entering_world = true;
		RefreshCharacterButtons();
	}
}

void FrontEnd::LeaveWorld()
{
	m_world.Logout();
	EnterStart();
}

void FrontEnd::CharacterSelectEvent(const InputEvent &event)
{
	if (event.type == InputEvent::Type::Key) {
		switch (event.key) {
		case InputEvent::Key::Up:
		case InputEvent::Key::Down: {
			// Step to the previous/next filled slot.
			const auto &slots = m_world.Characters();
			int step = event.key == InputEvent::Key::Up ? -1 : 1;
			for (int i = m_selected_slot + step; i >= 0 && i < CharacterButtons && i < static_cast<int>(slots.size()); i += step) {
				if (!slots[i].Empty()) {
					SelectSlot(i);
					break;
				}
			}
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
		const auto &slots = m_world.Characters();
		if (m_selected_slot < 0 || m_selected_slot >= static_cast<int>(slots.size()))
			return;
		std::string name = slots[m_selected_slot].name;
		ShowQuestion("Delete " + name + "?\n\nThis can't be undone.", [this, name](bool yes) {
			if (yes)
				m_world.DeleteCharacter(name);
		});
	}
	else if (clicked == "Quit_Button")
		LeaveWorld();
	else if (clicked == "Explore_Button")
		ShowMessage(m_strings.Get("SISATEXT_NOT_IMPLEMENTED"));
	else if (clicked == "Rotate_Button" && m_charselect_view) {
		if (auto *rotate = m_charselect.Find("Rotate_Button"))
			m_charselect_view->SetRotate(rotate->checked);
	}
}

void FrontEnd::RenderCharacterSelect(Canvas &canvas, uint64_t now_ms)
{
	// The zone and the selected character in 3D behind the window (CharacterSelectView).
	const auto &slots = m_world.Characters();
	RenderScene(canvas, now_ms);
	bool scene = m_charselect_view && m_charselect_view->Loaded() && m_charselect_view->Error().empty();
	if (scene) {
		// TurnCharacterNameSpritesOn: the name floats over the character's head.
		int x, y;
		if (m_selected_slot >= 0 && m_selected_slot < static_cast<int>(slots.size()) && m_charselect_view->NamePosition(x, y)) {
			const std::string &name = slots[m_selected_slot].name;
			TextSize size = m_text.Measure(name, 14);
			m_text.Draw(canvas, x - size.w / 2 + 1, y - size.h + 1, name, 14, 0x000000);
			m_text.Draw(canvas, x - size.w / 2, y - size.h, name, 14, 0xffffff);
		}
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

	m_charselect.Draw(canvas, m_text, *m_ui.textures, now_ms);
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
	m_creation->Activate(m_game_width, m_game_height, m_world.Expansions());
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
	if (m_creating)
		return;
	switch (m_creation->OnEvent(event)) {
	case CharacterCreation::Action::Back:
		ReturnToCharacterSelect();
		return;
	case CharacterCreation::Action::Message:
		ShowMessage(m_creation->Message());
		break;
	case CharacterCreation::Action::Submit: {
		// SubmitNameToWorld: OP_ApproveName, then (approved) OP_CharacterCreate.
		NewCharacter character = m_creation->Character();
		if (m_world.CreateCharacter(character)) {
			m_creating = true;
			ShowMessage(m_creation->SubmittingText());
		}
		else
			ShowMessage(m_creation->RejectedText());
		break;
	}
	case CharacterCreation::Action::None:
		break;
	}
	ShowCreatedCharacter();
}

bool FrontEnd::ControlCenter(const std::string &screen_id, int &x, int &y) const
{
	const SidlWindow *window = m_screen == Screen::CharacterSelect                 ? &m_charselect
	                           : m_screen == Screen::CharacterCreate && m_creation ? &m_creation->Window()
	                                                                               : nullptr;
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
}

// ---------------------------------------------------------------- zone (eqgame's world display)

namespace
{
	constexpr uint32_t ZoneWhite = 0xffffff;
	constexpr uint32_t ZoneGrey = 0xa0a0a0;
	constexpr uint32_t ZoneYellow = 0xf0e070;
}

void FrontEnd::DrawNameSprites(Canvas &canvas, uint64_t now_ms)
{
	// TurnCharacterNameSpritesOn / TurnNPCNameSpritesOn: names over heads, ours in the chase view.
	// The target's pulses a step a frame in eqgame, a cycle about every 300 ms; by the clock here,
	// whatever our frame rate.
	int pulse = static_cast<int>(now_ms % NameSprite::PulseMs * NameSprite::PulseSteps / NameSprite::PulseMs);
	uint16_t target = m_game_values.target;
	ZoneSpawn self = m_zone.Self();
	self.name = m_zone.Profile().name;
	const auto &spawns = m_zone.Spawns();
	// Extended nameplate distance: as far as the zone is drawn (its actors are culled there).
	float yon = m_config.extended_nameplates ? std::numeric_limits<float>::max() : NameSprite::YonClip;
	for (const ZoneView::NameTag &tag : m_zone_view->NameTags(yon)) {
		const ZoneSpawn *spawn = &self;
		bool targeted = target != 0 && (tag.id == 0 ? target == m_zone.MySpawnId() : tag.id == target);
		if (tag.id != 0) {
			auto it = spawns.find(tag.id);
			if (it == spawns.end())
				continue;
			spawn = &it->second;
		}
		std::string text = NameSpriteText(*spawn, m_config.show_names, m_zone.Guilds(), m_ui.eqstr, targeted);
		if (text.empty())
			continue;
		uint32_t color = NameSpriteColor(*spawn, m_world.ServerType());
		if (targeted)
			color = NameSpritePulse(color, pulse);
		// A line is 20 texels of 0.05 units; the guild's 0.85 of that, under it, the lot sitting
		// on the head.
		// The 20 pixel bold font stretched to that, as eqgfx stretches its glyphs.
		float scale = NameSprite::UnitsPerTexel * tag.focal / std::max(tag.depth, NameSprite::NearestDepth);
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
		int y = tag.y - heights[0] - heights[1];
		for (int i = 0; i < 2; ++i) {
			if (lines[i].empty())
				continue;
			m_text.DrawScaled(canvas, tag.x - widths[i] / 2, y, lines[i], NameSprite::FontHeight, scales[i], color,
				NameSprite::FontWeight);
			y += heights[i];
		}
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
	m_zone_view.reset();
	m_weather.Kill(m_sound.get()); // StopWorldDisplay
	m_zone_error_logged = false;
	m_debug.Reset();
	m_mouse_turn = m_mouse_pan = m_mouse_grabbed = false;
	m_keys = HeldKeys{};
	m_jump_requested = m_crouch_sent = false;
	m_main_loop_started = false;
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
		{"ConfirmationDialogBox", false},
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
		Log("UI: " + std::to_string(m_windows.Count()) + " windows, layout " + m_ui_layout_path);
		// CEverQuest::CreateIniFilenames: the character's own ini is the layout's name without UI_.
		m_hot_buttons_path = m_config.ui_dir + "/" + suffix;
		IniFile custom;
		if (custom.Load(m_hot_buttons_path)) {
			LoadHotButtons(custom, m_window_state);
			LoadSocials(custom, m_window_state);
		}
	}
	m_windows.TakeChanged();
	SpellBook::Host host;
	host.change_stance = [this](uint8_t stance) { ChangeStance(stance); };
	host.moving_forward = [this] { return m_zone_view && m_zone_view->Movement().speed > 0; };
	host.chat = [this](const std::string &text, int type) { ChatOut(text, type); };
	m_spell_book.Attach(m_windows, m_window_state, m_zone, m_game_values, m_ui.spells, m_ui.eqstr, std::move(host));
	m_spell_book.Reset();
	m_game_values.spell_book = &m_spell_book;
	m_chat.Attach(m_windows.Find("ChatWindow"));
	m_chat.SetLinkColor(m_chat_colors.Get(ItemTags::ColorType));
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

void FrontEnd::HandleWindowClicks()
{
	// Nothing goes on the cursor over an item (CCursorAttachment::IsOkToActivate).
	for (const auto &hold : m_windows.TakeHolds())
		if (!m_zone.Items().count(Inventory::Cursor))
			GameWindowHold(m_windows, m_window_state, hold.window, hold.control);
	for (const auto &click : m_windows.TakeRightClicks())
		if (!m_spell_book.RightClick(click.window, click.control))
			GameWindowRightClick(m_windows, m_window_state, click.window, click.control);
	for (const auto &hold : m_windows.TakeRightHolds())
		ShowItemDisplay(m_windows, m_game_values, m_window_state, hold.window, hold.control);
	for (const auto &release : m_windows.TakeRightReleases())
		GameWindowRightRelease(m_windows, release.window, release.control);
	for (WindowManager::Click click : m_windows.TakeClicks()) {
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
		// The spellbook, the gems and the confirmation dialog.
		if (m_spell_book.Click(click.window, click.control, click.alt, m_now_ms))
			continue;
		// The inventory's slots and character view, and the quantity window's Accept.
		InventoryClickResult moved;
		if (InventoryClick(m_windows, m_window_state, m_zone, click.window, click.control, click.shift, click.ctrl, moved)) {
			if (moved.sound && m_sound)
				m_sound->PlayWave(0x92);
			if (moved.message)
				ChatOut(EqString(moved.message, Inventory::MessageText(moved.message)), ChatType::Red);
			// The quantity window's edit box takes the keys from the others.
			if (m_windows.Shown("QuantityWnd")) {
				m_chat.Close();
				if (SidlWindow *social_editor = m_windows.Find("SocialEditWnd"))
					social_editor->Focus("");
			}
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
		// The Actions window's main page: what the slash commands do.
		if (click.window == "ActionsWindow") {
			if (click.control == "AMP_CampButton")
				InterpretCommand("/camp");
			else if (click.control == "AMP_SitButton" || click.control == "AMP_StandButton")
				Sit();
			else if (click.control == "AMP_RunButton" || click.control == "AMP_WalkButton") {
				m_run = click.control == "AMP_RunButton";
				AddZoneLog(m_run ? "You are now running." : "You are now walking.", ZoneGrey);
			}
		}
	}
	if (m_window_state.hot_buttons_changed || m_window_state.socials_changed)
		SaveCustom();
	// The selector's buttons stay down while their window is open.
	if (SidlWindow *selector = m_windows.Find("SelectorWindow"))
		for (const auto &toggle : SelectorToggles)
			if (SidlWindow::Control *button = selector->Find(toggle.button))
				button->checked = m_windows.Shown(toggle.screen);
}

void FrontEnd::AddZoneLog(const std::string &text, uint32_t color)
{
	m_chat.Add(text, color);
}

void FrontEnd::ChatOut(const std::string &text, int type)
{
	m_chat.Add(text, m_chat_colors.Get(type));
}

void FrontEnd::SetTarget(uint16_t id, bool command)
{
	if (!command)
		m_game_values.target = id;
	m_debug.SelectTarget(id);
	m_zone.SetTarget(id, command);
}

void FrontEnd::ChangeStance(uint8_t stance)
{
	uint8_t was = m_zone.Self().stance;
	if (stance == was || m_zone.GetState() != ZoneSession::State::InZone)
		return;
	m_zone.SetStance(stance);
	if (m_zone_view)
		m_zone_view->SetSitting(stance == 110);
	if (was == 110)
		m_spell_book.StoodUp();
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
			if (!m_windows_built)
				BuildZoneWindows();
			// CEverQuest_EnterZone -> CDisplay::StartWorldDisplay: load the zone, and put our
			// character (OP_ZoneEntry came first) in it.
			if (m_ui.eq_dir.empty())
				break;
			m_zone_view = std::make_unique<ZoneView>(m_ui.eq_dir, MakeRenderer());
			m_zone_view->SetVeliousTextures(m_config.velious_textures);
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
			break;
		}
		case Type::DoorsSpawned:
			if (m_zone_view)
				m_zone_view->SetDoors(m_zone.Doors(), m_now_ms);
			break;
		case Type::DoorMoved:
			if (m_zone_view)
				m_zone_view->MoveDoor(static_cast<uint8_t>(event.id), static_cast<uint8_t>(event.value), m_now_ms);
			break;
		case Type::InZone:
			m_zoning = false;
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
				m_last_tell = event.chat.sender; // AddNameToTellList
			break;
		}
		case Type::Message:
			ChatOut(event.text, event.value);
			break;
		case Type::ItemLink:
			ShowItemDisplay(m_windows, m_game_values, m_window_state, m_zone.LinkedItem());
			break;
		case Type::Emote:
			ChatOut(event.text, ChatType::Emote);
			break;
		case Type::SpellScribed:
		case Type::SpellMemorized:
		case Type::SpellForgotten:
		case Type::SpellBarUnlocked:
		case Type::SpellsSwapped:
		case Type::SpellDeleted:
			m_spell_book.ZoneEvent(static_cast<int>(event.type), event.id, event.value, m_now_ms);
			break;
		case Type::SkillUp: // msg_skillimprove
			ChatOut(FormatEqString(m_ui.eqstr, 12091, "You have become better at %1! (%2)",
				{SkillName(m_ui.eqstr, event.id), std::to_string(event.value)}), ChatType::Skills);
			break;
		case Type::TargetChanged:
			// The zone's answer to /target (or its refusal of a click).
			m_game_values.target = event.id;
			m_debug.SelectTarget(event.id);
			break;
		case Type::SpawnRemoved:
			if (event.id == m_game_values.target)
				m_game_values.target = 0;
			break;
		case Type::TimeOfDay:
			m_time_of_day = event.text;
			break;
		case Type::Weather:
			m_weather.Receive(event.id, event.value);
			break;
		case Type::LoggedOut:
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
	place.clip_plane = ZoneView::ClipPlane;
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

bool FrontEnd::SetZoneKey(InputEvent::Key key, bool down, bool typing)
{
	// Movement keys: the arrows always, W A S D when the chat line is closed (and always let go).
	bool *held = nullptr;
	switch (key) {
	case InputEvent::Key::Up: held = &m_keys.up; break;
	case InputEvent::Key::Down: held = &m_keys.down; break;
	case InputEvent::Key::Left: held = &m_keys.left; break;
	case InputEvent::Key::Right: held = &m_keys.right; break;
	case InputEvent::Key::W: held = &m_keys.w; break;
	case InputEvent::Key::A: held = &m_keys.a; break;
	case InputEvent::Key::S: held = &m_keys.s; break;
	case InputEvent::Key::D: held = &m_keys.d; break;
	default: return false;
	}
	bool letter = key == InputEvent::Key::W || key == InputEvent::Key::A || key == InputEvent::Key::S || key == InputEvent::Key::D;
	if (down && letter && typing)
		return false;
	*held = down;
	return true;
}

MoveControls FrontEnd::ZoneControls()
{
	MoveControls controls;
	bool can_move = !m_dialog.open && m_zone.GetState() == ZoneSession::State::InZone;
	if (can_move) {
		controls.forward = m_keys.up || m_keys.w || (m_mouse_turn && m_mouse_pan); // both buttons run
		controls.back = m_keys.down || m_keys.s;
		controls.left = m_keys.left || m_keys.a;
		controls.right = m_keys.right || m_keys.d;
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
	// The chat's links light under the mouse, and a click on one asks the zone about it.
	ItemTag link;
	if (event.IsMouse() && !m_mouse_turn && !m_mouse_pan &&
	    m_chat.OnMouse(event, m_windows.ScreenAt(event.x, event.y) == "ChatWindow", link))
		m_zone.ClickItemLink(link.id, link.name);
	if (event.IsMouse() && !m_mouse_turn && !m_mouse_pan && m_windows.OnMouse(event, m_now_ms)) {
		if (event.type == InputEvent::Type::MouseDown) {
			std::string pressed = m_windows.ScreenAt(event.x, event.y);
			SidlWindow *pressed_editor = pressed == "SocialEditWnd" ? social_editor
			                             : pressed == "QuantityWnd" ? quantity
			                                                        : nullptr;
			if (pressed_editor && pressed_editor->Typing()) {
				m_chat.Close();
				for (SidlWindow *other : {social_editor, quantity})
					if (other && other != pressed_editor)
						other->Focus("");
			}
			else if (pressed == "ChatWindow" && m_chat.Typing())
				for (SidlWindow *other : {social_editor, quantity})
					if (other)
						other->Focus("");
		}
		return;
	}
	SidlWindow *editor = quantity && quantity->Typing() ? quantity : social_editor;
	bool editing = editor && editor->Typing();
	switch (event.type) {
	case InputEvent::Type::Wheel:
		// Out of first person into the chase camera and back, as well as closer and farther.
		if (m_zone_view)
			m_zone_view->Zoom(event.wheel);
		return;
	case InputEvent::Type::RightDown:
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
		// A click that didn't drag uses the door or switch under the cursor (CEverQuest_ClickedSwitch),
		// or else targets the spawn under the cursor (nobody: the target stays).
		if (m_world_click && m_click_travel <= 3)
			m_window_state.ClearCursor(); // and the click goes on
		if (m_world_click && m_click_travel <= 3 && m_zone_view) {
			uint8_t door = 0;
			int16_t skill = 0;
			int16_t disarm = m_zone.Profile().skills[17]; // disarm traps
			switch (m_zone_view->ClickDoor(event.x, event.y, disarm, door, skill)) {
			case ZoneDoors::Use::Ask:
				m_zone.ClickDoor(door, 0xffff, skill);
				break;
			case ZoneDoors::Use::TooFar:
				AddZoneLog(EqString(1462, "You can't reach that, get closer."), ZoneYellow);
				break;
			case ZoneDoors::Use::Nothing:
				if (uint16_t id = m_zone_view->PickSpawn(event.x, event.y))
					SetTarget(id);
				break;
			}
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
	// Typing into the social editor: Enter does nothing, Escape closes it (below). Into the
	// quantity window's box, Enter is its Accept.
	if (editing && event.key == InputEvent::Key::Enter && editor == quantity) {
		QuantityEnter(m_windows, m_window_state, m_zone);
		return;
	}
	if (editing && (editor->OnKey(event) || event.key == InputEvent::Key::Enter))
		return;
	bool typing = m_chat.Typing() || editing;
	if (m_chat.Typing()) {
		std::string line;
		if (m_chat.OnKey(event, line)) {
			if (event.key == InputEvent::Key::Enter && !line.empty())
				InterpretCommand(line);
			return;
		}
	}
	// The open spellbook's page keys (HandleKeys, ahead of the movement keys) and Delete.
	if (m_spell_book.Key(event.key, m_now_ms))
		return;
	// Forward stands us up from sitting, closing the book (Zeal's movement binds; the other
	// movement keys do nothing sitting).
	if (m_zone.Self().stance == 110 &&
	    (event.key == InputEvent::Key::Up || (event.key == InputEvent::Key::W && !typing)))
		ChangeStance(100);
	if (SetZoneKey(event.key, true, typing))
		return;
	switch (event.key) {
	case InputEvent::Key::Enter:
		if (m_chat.Attached()) {
			m_windows.Show("ChatWindow", true);
			m_chat.Open();
		}
		break;
	case InputEvent::Key::Space:
		if (!typing)
			m_jump_requested = true;
		break;
	case InputEvent::Key::R:
		// Ctrl+R toggles running and walking.
		if (event.ctrl) {
			m_run = !m_run;
			AddZoneLog(m_run ? "You are now running." : "You are now walking.", ZoneGrey);
		}
		break;
	// The camera: F9 switches first person and the chase camera (CDisplay's camera views),
	// Page Up / Page Down tilt it.
	case InputEvent::Key::F9:
		if (m_zone_view)
			m_zone_view->CycleCamera();
		break;
	case InputEvent::Key::F11:
		m_debug.open = !m_debug.open;
		SaveDebugSettings();
		break;
	case InputEvent::Key::PageUp:
	case InputEvent::Key::PageDown:
		if (m_zone_view)
			m_zone_view->Pitch(event.key == InputEvent::Key::PageUp ? 5.0f : -5.0f);
		break;
	case InputEvent::Key::Escape: {
		// A button on the cursor goes, and that's all (CDisplay_NewUIProcessEscape's RemoveAttachment).
		if (m_window_state.Attached()) {
			m_window_state.ClearCursor();
			break;
		}
		// ExecuteCmd's escape: no target, then CDisplay_NewUIProcessEscape closes the first of its
		// windows that's open (of the ones we have: the item display, quantity, help, skills, options,
		// the social editor, inventory).
		SetTarget(0);
		ChatOut(EqString(12375, "You no longer have a target."), ChatType::Default);
		for (const char *screen :
			{"ItemDisplayWindow", "QuantityWnd", "HelpWindow", "SkillsWindow", "OptionsWindow", "SpellBookWnd",
			 "SocialEditWnd", "InventoryWindow"})
			if (m_windows.Shown(screen)) {
				if (std::string(screen) == "SpellBookWnd")
					m_spell_book.Deactivate();
				else
					m_windows.Show(screen, false);
				break;
			}
		break;
	}
	default:
		break;
	}
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
	if (world) {
		m_zone_view->Render(canvas, now_ms);
		const ZoneView::RenderTimes &render_times = m_zone_view->Times();
		times.zone = render_times.zone;
		times.models = render_times.models;
		times.submit = render_times.submit;
		Clock::time_point tags_start = Clock::now();
		DrawNameSprites(canvas, now_ms);
		times.tags = since(tags_start);
	}
	else {
		canvas.Fill({0, 0, width, height}, 0x000000);
		std::string loading = "LOADING, PLEASE WAIT...";
		TextSize size = m_text.Measure(loading, 20);
		m_text.Draw(canvas, (width - size.w) / 2, height / 2 - size.h / 2, loading, 20, ZoneWhite);
	}

	Clock::time_point ui_start = Clock::now();
	if (world) {
		std::string help = std::string("Arrows/WASD move   Space jump   Ctrl+R ") + (m_run ? "walk" : "run") +
		                   "   Right drag look   Wheel zoom   F9 camera   F11 debug   Enter chat";
		TextSize size = m_text.Measure(help, FontSize::Small);
		m_text.Draw(canvas, width - size.w - 10, height - size.h - 8, help, FontSize::Small, ZoneGrey);
	}

	m_chat.Layout(m_text);
	m_windows.Draw(canvas, m_text, now_ms);
	// Top left and right: the debug menu's stat card and radar, above the windows.
	m_debug.Draw(canvas, m_text, m_zone, m_zone_view.get(), height - 10);
	times.ui = since(ui_start);
	m_debug.Frame(times);
}
