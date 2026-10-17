#pragma once

// The front end (what eqmain.dll does before eqgame.exe takes over): the start
// screen, the login screen and server select, laid out from eqmain's
// displayLuclinOptionsScreen (@10021a50), displayLoginScreen (@100226b0) and
// displayServerSelectionScreen (@100275d0). After PLAY it continues as eqgame.exe
// does: world authentication and character select (EQUI_CharacterSelect.xml,
// CEverQuest::DoCharacterSelection @0053b9cf). Portable: the platform layer feeds
// input events, calls Update/Render, and presents the 640x480 canvas.

#include "art.h"
#include "charselect_view.h"
#include "eqtext.h"
#include "login_client.h"
#include "sidl.h"
#include "world_client.h"
#include "zone_session.h"
#include "widgets.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

struct FrontEndConfig
{
	std::string login_host = "127.0.0.1";
	uint16_t login_port = 6000;
	std::string username; // pre-filled on the login screen
	uint16_t world_port = 9000;
};

class EqStr;

// eqgame's data: the SIDL UI (uifiles/default) that character select is built
// from, and eqstr_en.txt for server messages. Optional: without the UI the
// front end stops at server select with an error.
struct GameData
{
	SidlDefinitions *definitions = nullptr;
	SidlTextures *textures = nullptr;
	std::string error; // why the UI couldn't be loaded
	const EqStr *eqstr = nullptr;
	std::string eq_dir; // for the 3D character select scene; empty skips it
};

class FrontEnd
{
public:
	static constexpr int Width = 640;
	static constexpr int Height = 480;

	enum class Screen { Start, Login, ServerSelect, CharacterSelect, Zone };

	FrontEnd(FrontEndArt &art, const EqText &strings, TextRenderer &text, const FrontEndConfig &config, GameData ui = {});

	void OnEvent(const InputEvent &event);
	void Update(uint64_t now_ms);
	void Render(Canvas &canvas, uint64_t now_ms);

	void SetCursorVisible(bool visible) { m_cursor_visible = visible; }
	bool QuitRequested() const { return m_quit; }
	std::string Username() const { return m_user.text; }
	Screen CurrentScreen() const { return m_screen; }
	bool DialogOpen() const { return m_dialog.open; }
	const std::string &DialogText() const { return m_dialog.text; }
	const std::vector<CharacterSlot> &Characters() const { return m_world.Characters(); }
	int SelectedCharacter() const { return m_selected_slot; }
	const ZoneSession &Zone() const { return m_zone; }
	const CharacterSelectView *CharacterScene() const { return m_charselect_view.get(); }

	std::function<void(const std::string &)> log;

private:
	struct Dialog
	{
		bool open = false;
		bool medium = false;   // dialogmedium.bmp for long text
		bool question = false; // YES/NO instead of OK
		std::string text;
		Button ok, yes, no;
		int label_height = FontSize::Large;
		std::function<void(bool)> on_close;
	};

	void EnterStart();
	void EnterLogin();
	void EnterServerSelect();

	void ShowMessage(const std::string &text, std::function<void(bool)> on_close = nullptr);
	void ShowQuestion(const std::string &text, std::function<void(bool)> on_close);
	void CloseDialog(bool result);
	void DialogEvent(const InputEvent &event);

	void StartEvent(const InputEvent &event);
	void LoginEvent(const InputEvent &event);
	void ServerSelectEvent(const InputEvent &event);
	void CharacterSelectEvent(const InputEvent &event);
	void ZoneEvent(const InputEvent &event);

	void SubmitLogin();
	void PlaySelected();
	void RebuildServerLists();
	void HandleSessionEvents(const std::vector<LoginSession::Event> &events);
	void HandleWorldEvents(const std::vector<WorldSession::Event> &events);
	void EnterCharacterSelect();
	void RefreshCharacterButtons();
	void SelectSlot(int slot);
	void EnterWorldSelected();
	void LeaveWorld(); // close the world session and go back to the start screen
	void EnterZoneScreen();
	void HandleZoneEvents(const std::vector<ZoneSession::Event> &events);
	void AddZoneLog(const std::string &text, uint32_t color);
	void StartCamp();

	void RenderStart(Canvas &canvas);
	void RenderLogin(Canvas &canvas, uint64_t now_ms);
	void RenderServerSelect(Canvas &canvas);
	void RenderCharacterSelect(Canvas &canvas, uint64_t now_ms);
	void RenderZone(Canvas &canvas, uint64_t now_ms);
	void RenderDialog(Canvas &canvas);

	ButtonArt Art(int style);
	void Log(const std::string &line);

	FrontEndArt &m_art;
	const EqText &m_strings;
	TextRenderer &m_text;
	FrontEndConfig m_config;

	Screen m_screen = Screen::Start;
	Dialog m_dialog;
	bool m_quit = false;
	bool m_cursor_visible = true;
	int m_mouse_x = 0, m_mouse_y = 0;

	// start screen
	std::vector<Button> m_start_buttons; // CONNECT, ACCOUNT, CREDITS, OPTIONS, EXIT

	// login screen
	Button m_connect, m_cancel, m_help;
	InputField m_user, m_password;
	std::string m_login_status;
	bool m_logging_in = false;

	// server select
	Button m_exit, m_news, m_play;
	ScrollList m_names, m_statuses;
	Scrollbar m_scrollbar;
	CheckBox m_sort_name, m_sort_population;
	int m_sort_mode = 0;
	std::vector<WorldListing> m_worlds; // sorted as displayed
	std::string m_selected_name;
	std::string m_overlay_message;
	bool m_submitting = false;
	uint64_t m_last_list_request_ms = 0;

	// character select
	GameData m_ui;
	SidlWindow m_charselect;
	int m_selected_slot = -1;
	bool m_entering_world = false;
	std::unique_ptr<CharacterSelectView> m_charselect_view;
	bool m_scene_error_logged = false;

	// zone (stand-in for the 3D view)
	struct LogLine
	{
		std::string text;
		uint32_t color;
	};
	std::vector<LogLine> m_zone_log;
	InputField m_chat_input;
	Button m_camp;
	std::string m_time_of_day;

	LoginSession m_session;
	WorldSession m_world;
	ZoneSession m_zone;
	uint64_t m_now_ms = 0;
};
