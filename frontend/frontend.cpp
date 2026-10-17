#include "frontend.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cctype>

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
}

FrontEnd::FrontEnd(FrontEndArt &art, const EqText &strings, TextRenderer &text, const FrontEndConfig &config, GameData ui)
	: m_art(art), m_strings(strings), m_text(text), m_config(config), m_ui(std::move(ui))
{
	m_session.SetLogger([this](const std::string &line) { Log(line); });
	m_world.SetLogger([this](const std::string &line) { Log(line); });
	m_zone.SetLogger([this](const std::string &line) { Log(line); });
	m_zone.SetStringTable(m_ui.eqstr);
	m_user.text = config.username;
	if (!m_ui.eq_dir.empty() && m_ui.definitions) {
		// Character select's zone takes a moment to load; start while the player logs in.
		m_charselect_view = std::make_unique<CharacterSelectView>(m_ui.eq_dir);
		m_charselect_view->StartLoading();
	}
	m_rules.CheckInstall(m_ui.eq_dir);
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
	m_entering_world = false;
	m_logging_in = false;

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

	// createInputField(x=0x127, y, w=0x127, h=0x1a, maxlen incl. terminator 0x14 / 0x11)
	m_user.rect = {0x127, 0x104, 0x127, 0x1a};
	m_user.max_chars = 0x13;
	m_password.rect = {0x127, 0x16d, 0x127, 0x1a};
	m_password.max_chars = 0x10;
	m_password.password = true;
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
	if (event.type != InputEvent::Type::Char && event.type != InputEvent::Type::Key) {
		m_mouse_x = event.x;
		m_mouse_y = event.y;
	}

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
	if (m_zone_view) {
		const ZoneSpawn &me = m_zone.Self();
		m_zone_view->SetPlayerPosition(me.x, me.y, me.z, me.heading);
	}

	if (m_screen == Screen::ServerSelect && !m_submitting && now_ms - m_last_list_request_ms > ServerListRefreshMs) {
		m_last_list_request_ms = now_ms;
		m_session.RequestServerList();
	}
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

	if (m_cursor_visible)
		canvas.Blit(m_art.Get("cursor.bmp"), m_mouse_x, m_mouse_y, true);
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
			if (!m_zone.Begin(m_world.ZoneAddress(), m_world.ZonePort(), m_world.EnteringCharacter())) {
				ShowMessage(m_strings.Get("SISATEXT_CONNECT_FAILED"), [this](bool) { EnterStart(); });
				break;
			}
			EnterZoneScreen();
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
	constexpr uint32_t ZoneDim = 0x606060;
	constexpr uint32_t ZoneYellow = 0xf0e070;
	constexpr int RadarSize = 220;
	constexpr float RadarRange = 300.0f; // world units from the center to the edge
	constexpr int PanelWidth = 215, PanelHeight = 340; // our details and the nearest spawns, top left
	constexpr int ChatWidth = 620;
	constexpr size_t MaxLogLines = 200;

	struct Nearby
	{
		float distance;
		const ZoneSpawn *spawn;
	};

	std::vector<Nearby> SortedByDistance(const ZoneSession &zone)
	{
		const ZoneProfile &me = zone.Profile();
		std::vector<Nearby> out;
		for (const auto &entry : zone.Spawns()) {
			if (entry.first == zone.MySpawnId())
				continue;
			float dx = entry.second.x - me.x, dy = entry.second.y - me.y;
			out.push_back({std::sqrt(dx * dx + dy * dy), &entry.second});
		}
		std::sort(out.begin(), out.end(), [](const Nearby &a, const Nearby &b) { return a.distance < b.distance; });
		return out;
	}

	uint32_t SpawnColor(const ZoneSpawn &spawn)
	{
		if (spawn.npc == 0)
			return 0x40c0ff; // players
		if (spawn.npc == 1)
			return spawn.pet_owner ? 0xc080ff : 0xff8040;
		return 0x808080; // corpses
	}

	std::string ChatLine(const ZoneChat &chat, const std::string &me, uint32_t &color)
	{
		bool mine = chat.sender == me;
		std::string who = mine ? "You" : chat.sender;
		switch (chat.channel) {
		case 0: color = 0x60ff60; return who + (mine ? " say" : " tells") + " the guild, '" + chat.text + "'";
		case 2: color = 0x60e0ff; return who + (mine ? " tell" : " tells") + " the group, '" + chat.text + "'";
		case 3: color = 0xff6060; return who + (mine ? " shout" : " shouts") + ", '" + chat.text + "'";
		case 4: color = 0x60ff60; return who + (mine ? " auction" : " auctions") + ", '" + chat.text + "'";
		case 5: color = 0x60ff60; return who + (mine ? " say" : " says") + " out of character, '" + chat.text + "'";
		case 7: color = 0xff80ff; return mine ? "You told " + chat.target + ", '" + chat.text + "'" : who + " tells you, '" + chat.text + "'";
		case 8: color = ZoneWhite; return who + (mine ? " say" : " says") + ", '" + chat.text + "'";
		default: color = ZoneGrey; return who + ": " + chat.text;
		}
	}
}

void FrontEnd::EnterZoneScreen()
{
	m_screen = Screen::Zone;
	m_zone_log.clear();
	m_time_of_day.clear();
	m_zone_view.reset();
	m_zone_error_logged = false;
	m_mouse_look = false;
	AddZoneLog("Entering the zone...", ZoneGrey);

	// The chat sits along the bottom left of the window, over the 3D view.
	int bottom = m_game_height - Height;
	m_chat_input = InputField{};
	m_chat_input.rect = {50, bottom + 448, 460, 20};
	m_chat_input.max_chars = 200;
	m_chat_input.text_height = FontSize::Normal;
	m_chat_input.color = ZoneWhite;
	m_chat_input.focused = true;

	m_camp.Place(522, bottom + 440, Art(1));
	m_camp.label = "CAMP";
	m_camp.text_height = FitTextHeight(m_text, {m_camp.label}, 100, 0x22);
}

void FrontEnd::AddZoneLog(const std::string &text, uint32_t color)
{
	// Split on newlines so each line wraps on its own.
	size_t start = 0;
	while (start <= text.size()) {
		size_t end = text.find('\n', start);
		m_zone_log.push_back({text.substr(start, end == std::string::npos ? std::string::npos : end - start), color});
		if (end == std::string::npos)
			break;
		start = end + 1;
	}
	if (m_zone_log.size() > MaxLogLines)
		m_zone_log.erase(m_zone_log.begin(), m_zone_log.begin() + static_cast<long>(m_zone_log.size() - MaxLogLines));
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
			// CEverQuest_EnterZone -> CDisplay::StartWorldDisplay: load the zone, and put our
			// character (OP_ZoneEntry came first) in it.
			if (m_ui.eq_dir.empty())
				break;
			m_zone_view = std::make_unique<ZoneView>(m_ui.eq_dir);
			m_zone_view->StartLoading(m_zone.Info());
			const ZoneSpawn &me = m_zone.Self();
			render::CharacterAppearance appearance;
			appearance.race = me.race ? me.race : m_zone.Profile().race;
			appearance.gender = me.gender;
			appearance.face = me.face;
			appearance.height = me.size;
			for (int k = 0; k < render::EquipSlots; ++k) {
				appearance.equip[k] = me.equipment[k];
				appearance.tint[k] = me.colors[k];
			}
			m_zone_view->SetPlayer(appearance);
			m_zone_view->SetPlayerPosition(me.x, me.y, me.z, me.heading);
			break;
		}
		case Type::InZone:
			AddZoneLog(std::to_string(m_zone.Spawns().size()) + " spawns in the zone.", ZoneGrey);
			break;
		case Type::Chat: {
			uint32_t color = ZoneWhite;
			std::string line = ChatLine(event.chat, m_zone.Profile().name, color);
			AddZoneLog(line, color);
			break;
		}
		case Type::Message:
			AddZoneLog(event.text, ZoneYellow);
			break;
		case Type::TimeOfDay:
			m_time_of_day = event.text;
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

void FrontEnd::ZoneEvent(const InputEvent &event)
{
	switch (event.type) {
	case InputEvent::Type::Wheel:
		// Out of first person into the chase camera and back, as well as closer and farther.
		if (m_zone_view)
			m_zone_view->Zoom(event.wheel);
		return;
	case InputEvent::Type::RightDown:
		m_mouse_look = true;
		m_look_x = event.x;
		m_look_y = event.y;
		return;
	case InputEvent::Type::RightUp:
		m_mouse_look = false;
		return;
	case InputEvent::Type::MouseMove:
		if (m_mouse_look) {
			// Mouse look: right turns right, up looks up.
			if (m_zone_view) {
				m_zone_view->Orbit(-0.6f * static_cast<float>(event.x - m_look_x));
				m_zone_view->Pitch(-0.25f * static_cast<float>(event.y - m_look_y));
			}
			m_look_x = event.x;
			m_look_y = event.y;
			return;
		}
		break;
	default:
		break;
	}
	if (event.type == InputEvent::Type::Char) {
		m_chat_input.OnChar(event.ch);
		return;
	}
	if (event.type == InputEvent::Type::Key) {
		switch (event.key) {
		case InputEvent::Key::Backspace:
			m_chat_input.OnBackspace();
			break;
		case InputEvent::Key::Enter: {
			std::string text = m_chat_input.text;
			m_chat_input.text.clear();
			if (text == "/camp")
				StartCamp();
			else if (!text.empty() && text[0] != '/')
				m_zone.Say(text); // the server echoes it back
			else if (!text.empty())
				AddZoneLog("That command isn't supported yet.", ZoneGrey);
			break;
		}
		// The camera, until the character can move: F9 switches first person and the chase
		// camera (CDisplay's camera views), the arrows turn it, Page Up / Page Down tilt it.
		case InputEvent::Key::F9:
			if (m_zone_view)
				m_zone_view->CycleCamera();
			break;
		case InputEvent::Key::Left:
		case InputEvent::Key::Right:
			if (m_zone_view)
				m_zone_view->Orbit(event.key == InputEvent::Key::Left ? 12.0f : -12.0f);
			break;
		case InputEvent::Key::PageUp:
		case InputEvent::Key::PageDown:
			if (m_zone_view)
				m_zone_view->Pitch(event.key == InputEvent::Key::PageUp ? 5.0f : -5.0f);
			break;
		case InputEvent::Key::Escape:
			if (m_zone.GetState() == ZoneSession::State::InZone)
				ShowQuestion("Camp out?\n\nIt takes about 30 seconds.", [this](bool yes) {
					if (yes)
						StartCamp();
				});
			break;
		default:
			break;
		}
		return;
	}
	if (m_camp.OnMouse(event))
		StartCamp();
}

void FrontEnd::RenderZone(Canvas &canvas, uint64_t now_ms)
{
	const ZoneProfile &me = m_zone.Profile();
	const ZoneInfo &info = m_zone.Info();
	int width = canvas.Width(), height = canvas.Height();

	// The world, or the loading screen until the zone is in.
	bool world = m_zone_view && m_zone_view->Loaded() && m_zone_view->Error().empty();
	if (m_zone_view && m_zone_view->Loaded() && !m_zone_view->Error().empty() && !m_zone_error_logged) {
		Log("zone view: " + m_zone_view->Error());
		AddZoneLog("Couldn't load the zone: " + m_zone_view->Error(), ZoneGrey);
		m_zone_error_logged = true;
	}
	if (world) {
		m_zone_view->Render(canvas, now_ms);
		// TurnCharacterNameSpritesOn: our name over our head in the chase view.
		int x, y;
		if (!me.name.empty() && m_zone_view->NamePosition(x, y)) {
			TextSize size = m_text.Measure(me.name, 14);
			m_text.Draw(canvas, x - size.w / 2 + 1, y - size.h + 1, me.name, 14, 0x000000);
			m_text.Draw(canvas, x - size.w / 2, y - size.h, me.name, 14, ZoneWhite);
		}
	}
	else {
		canvas.Fill({0, 0, width, height}, 0x000000);
		std::string loading = "LOADING, PLEASE WAIT...";
		TextSize size = m_text.Measure(loading, 20);
		m_text.Draw(canvas, (width - size.w) / 2, height / 2 - size.h / 2, loading, 20, ZoneWhite);
	}

	// Top left: zone name, time of day, camp countdown, our details.
	canvas.Darken({0, 0, PanelWidth, PanelHeight});
	m_text.Draw(canvas, 10, 8, info.long_name.empty() ? "Entering the zone..." : info.long_name, 20, ZoneWhite);
	std::string status;
	if (m_zone.GetState() == ZoneSession::State::Camping)
		status = "Camping... " + std::to_string((m_zone.CampRemainingMs() + 999) / 1000) + "s";
	else if (!m_time_of_day.empty())
		status = "Time " + m_time_of_day;
	if (!status.empty())
		m_text.Draw(canvas, 10, 30, status, FontSize::Small, ZoneGrey);

	int y = 48;
	auto line = [&](const std::string &text, int text_height, uint32_t color) {
		m_text.Draw(canvas, 10, y, text, text_height, color);
		y += text_height + 3;
	};
	if (!me.name.empty()) {
		line(me.name + (me.last_name.empty() ? "" : " " + me.last_name), 16, ZoneWhite);
		line("Level " + std::to_string(me.level) + " " + RaceName(me.race) + " " + ClassName(static_cast<uint8_t>(me.class_id)),
			FontSize::Small, ZoneGrey);
		line("HP " + std::to_string(me.cur_hp) + "   Mana " + std::to_string(me.mana), FontSize::Small, ZoneGrey);
		char loc[64];
		snprintf(loc, sizeof(loc), "Loc %.0f, %.0f, %.0f", me.y, me.x, me.z); // /loc order: y, x, z
		line(loc, FontSize::Small, ZoneGrey);
		line(std::to_string(me.platinum) + "p " + std::to_string(me.gold) + "g " + std::to_string(me.silver) + "s " +
				std::to_string(me.copper) + "c",
			FontSize::Small, ZoneGrey);
	}

	// The nearest spawns.
	auto nearby = SortedByDistance(m_zone);
	y = 150;
	m_text.Draw(canvas, 10, y, "Nearby", FontSize::Normal, ZoneWhite);
	y += 18;
	for (size_t i = 0; i < nearby.size() && y < PanelHeight - 14; ++i, y += 14) {
		const ZoneSpawn &spawn = *nearby[i].spawn;
		char row[96];
		snprintf(row, sizeof(row), "%.22s (%u)", spawn.DisplayName().c_str(), spawn.level);
		m_text.Draw(canvas, 10, y, row, FontSize::Small, SpawnColor(spawn));
		std::string distance = std::to_string(static_cast<int>(nearby[i].distance));
		TextSize size = m_text.Measure(distance, FontSize::Small);
		m_text.Draw(canvas, 205 - size.w, y, distance, FontSize::Small, ZoneDim);
	}

	// Top right: a radar, north up, east right. EQ's +y is north and +x is west.
	const int radar_x = width - RadarSize - 10, radar_y = 10;
	Rect radar{radar_x, radar_y, RadarSize, RadarSize};
	canvas.Darken(radar);
	canvas.Frame(radar, 0x404850);
	canvas.Fill({radar_x + RadarSize / 2, radar_y + 4, 1, RadarSize - 8}, 0x303840);
	canvas.Fill({radar_x + 4, radar_y + RadarSize / 2, RadarSize - 8, 1}, 0x303840);
	m_text.Draw(canvas, radar_x + RadarSize / 2 - 3, radar_y + 3, "N", FontSize::Small, ZoneGrey);
	float scale = (RadarSize / 2 - 6) / RadarRange;
	int labels = 0;
	for (auto it = nearby.rbegin(); it != nearby.rend(); ++it) { // farthest first so near dots draw on top
		if (it->distance > RadarRange)
			continue;
		const ZoneSpawn &spawn = *it->spawn;
		int px = radar_x + RadarSize / 2 + static_cast<int>(-(spawn.x - me.x) * scale);
		int py = radar_y + RadarSize / 2 + static_cast<int>(-(spawn.y - me.y) * scale);
		canvas.Fill({px - 1, py - 1, 3, 3}, SpawnColor(spawn));
	}
	for (size_t i = 0; i < nearby.size() && labels < 6; ++i) {
		if (nearby[i].distance > RadarRange)
			break;
		const ZoneSpawn &spawn = *nearby[i].spawn;
		int px = radar_x + RadarSize / 2 + static_cast<int>(-(spawn.x - me.x) * scale);
		int py = radar_y + RadarSize / 2 + static_cast<int>(-(spawn.y - me.y) * scale);
		m_text.Draw(canvas, px + 4, py - 6, spawn.DisplayName(), 10, ZoneGrey);
		++labels;
	}
	canvas.Fill({radar_x + RadarSize / 2 - 2, radar_y + RadarSize / 2 - 2, 5, 5}, ZoneWhite); // us
	m_text.Draw(canvas, radar_x + 4, radar_y + RadarSize - 14, std::to_string(static_cast<int>(RadarRange)) + " units", 10,
		ZoneGrey);

	// Bottom left: the chat log, the last lines that fit, newest at the bottom.
	int bottom = height - Height;
	Rect log{10, bottom + 350, ChatWidth, 92};
	canvas.Darken({0, log.y - 4, ChatWidth + 20, height - log.y + 4});
	int row_height = 13;
	int rows = log.h / row_height;
	int first = std::max(0, static_cast<int>(m_zone_log.size()) - rows);
	for (int i = first, ry = log.y + 1; i < static_cast<int>(m_zone_log.size()); ++i, ry += row_height)
		m_text.Draw(canvas, log.x + 4, ry, m_zone_log[i].text, FontSize::Small, m_zone_log[i].color);

	m_text.Draw(canvas, 10, bottom + 450, "Say:", FontSize::Normal, ZoneGrey);
	canvas.Frame({m_chat_input.rect.x - 2, m_chat_input.rect.y - 1, m_chat_input.rect.w + 4, m_chat_input.rect.h + 2}, 0x404850);
	m_chat_input.Draw(canvas, m_text, now_ms);
	m_camp.Draw(canvas, m_text);

	if (world) {
		std::string help = "Right drag look   Wheel zoom   F9 camera   Left/Right turn   PgUp/PgDn tilt";
		TextSize size = m_text.Measure(help, FontSize::Small);
		m_text.Draw(canvas, width - size.w - 10, height - size.h - 8, help, FontSize::Small, ZoneGrey);
	}
}
