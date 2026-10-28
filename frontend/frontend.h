#pragma once

// The front end (what eqmain.dll does before eqgame.exe takes over): the start
// screen, the login screen and server select, laid out from eqmain's
// displayLuclinOptionsScreen (@10021a50), displayLoginScreen (@100226b0) and
// displayServerSelectionScreen (@100275d0). After PLAY it continues as eqgame.exe
// does: world authentication and character select (EQUI_CharacterSelect.xml,
// CEverQuest::DoCharacterSelection @0053b9cf). Portable: the platform layer feeds
// input events in window pixels, calls Update/Render with a canvas the size of the
// window (the game resolution), and presents it.

#include "art.h"
#include "charcreate.h"
#include "charselect_anim_window.h"
#include "charselect_view.h"
#include "chat_text.h"
#include "chat_window.h"
#include "creation_rules.h"
#include "debug_menu.h"
#include "eq_sound.h"
#include "eqtext.h"
#include "game_values.h"
#include "game_windows.h"
#include "name_sprites.h"
#include "login_client.h"
#include "sidl.h"
#include "buffs.h"
#include "spell_book.h"
#include "spell_cast.h"
#include "weather.h"
#include "window_manager.h"
#include "world_client.h"
#include "zone_session.h"
#include "zone_view.h"
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
	int last_character = 0; // character select slot to start on (LastCharSel)
	uint16_t world_port = 9000;
	std::string record_dir; // each zone session's packets go to a packet log (.eqr) here; empty: none
	std::string ui_dir;     // where the UI_<character>_<server>.ini window layouts go; empty: not kept
	std::string settings_ini; // my-client.ini: its [TextColors] (the chat's colors; empty: the built-in ones) and
	                          // [Debug] (the debug menu's settings; empty: not kept)
	int show_names = 4;       // /shownames: 0 (off) .. 7 (ShowNamesLevel)
	bool extended_nameplates = true; // names as far out as the zone is drawn, not eqgame's 70 units
	bool velious_textures = true;    // the Velious armor (materials 17..23), else the older armor they stand in for
	bool char_select_anim_window = true; // our Animations window at character select (CharSelectAnimWindow)
	bool char_select_emotes = false;     // character select's random animations include socials' (CharSelectEmotes)
};

class EqStr;
class SpellFile;

// eqgame's data: the SIDL UI (uifiles/default) that character select is built
// from, and eqstr_en.txt for server messages. Optional: without the UI the
// front end stops at server select with an error.
struct GameData
{
	SidlDefinitions *definitions = nullptr;
	SidlTextures *textures = nullptr;
	std::string error; // why the UI couldn't be loaded
	const EqStr *eqstr = nullptr;
	const SpellFile *spells = nullptr; // spells_en.txt, for the item display
	std::string eq_dir; // for the 3D character select scene; empty skips it
	// The 3D views' renderer (the host's GPU one); unset, they use the software Rasterizer.
	std::function<std::unique_ptr<render::Renderer>()> make_renderer;
	// The host's vsync, for the debug menu: whether it's on, and how to change it; unset without one.
	bool vsync = false;
	std::function<void(bool)> set_vsync;
	// The host's sound: the mix our sound plays into (unset: none), and its volumes.
	audio::Mixer *mixer = nullptr;
	audio::EqSound::Settings sound;
};

class FrontEnd
{
public:
	static constexpr int Width = 640;
	static constexpr int Height = 480;

	enum class Screen { Start, Login, ServerSelect, CharacterSelect, CharacterCreate, Zone };

	FrontEnd(FrontEndArt &art, const EqText &strings, TextRenderer &text, const FrontEndConfig &config, GameData ui = {});

	void OnEvent(const InputEvent &event);
	void Update(uint64_t now_ms);
	void Render(Canvas &canvas, uint64_t now_ms);

	// The window's size. eqmain's screens are 640x480 art, drawn at their native size in the
	// middle of the window; character select and the zone fill it, like eqgame, with the SIDL
	// windows at their real pixel size.
	void SetGameResolution(int width, int height);
	int ScreenWidth() const { return m_game_width; }
	int ScreenHeight() const { return m_game_height; }
	Rect ScreenArea() const; // where the current screen is laid out, in window pixels

	// Straight into the zone from a packet log (ZoneSession::BeginReplay), with no server.
	bool StartReplay(const std::string &path, std::string &error);

	void SetCursorVisible(bool visible) { m_cursor_visible = visible; }
	// How long the host took to show the last frame, ms (the debug menu's Present).
	void SetPresentMs(double ms) { m_present_ms = ms; }
	// While a mouse drag turns the character or pans the camera, the cursor is hidden and held
	// where the drag started: the host puts it back at (x, y), in window pixels, after every move
	// and once more when the drag ends.
	bool MouseGrab(int &x, int &y) const;
	bool QuitRequested() const { return m_quit; }
	std::string Username() const { return m_user.text; }
	Screen CurrentScreen() const { return m_screen; }
	bool DialogOpen() const { return m_dialog.open; }
	const std::string &DialogText() const { return m_dialog.text; }
	const std::vector<CharacterSlot> &Characters() const { return m_world.Characters(); }
	int SelectedCharacter() const { return m_selected_slot; }
	int LastCharacter() const { return m_config.last_character; } // the slot to remember (LastCharSel)
	int ShowNames() const { return m_config.show_names; }         // /shownames, to remember (ShowNamesLevel)
	bool CharSelectEmotes() const { return m_config.char_select_emotes; } // the Animations window's choice
	const ZoneSession &Zone() const { return m_zone; }
	const ChatWindow &Chat() const { return m_chat; }
	uint16_t Target() const { return m_game_values.target; } // spawn id, 0 none
	const CharacterSelectView *CharacterScene() const { return m_charselect_view.get(); }
	const CharSelectAnimWindow *AnimationsWindow() const { return m_anim_window.get(); }
	const audio::EqSound *Sound() const { return m_sound.get(); }
	const ZoneView *ZoneScene() const { return m_zone_view.get(); }
	ZoneView *ZoneScene() { return m_zone_view.get(); }
	WindowManager &Windows() { return m_windows; }
	const GameWindowState &WindowState() const { return m_window_state; }
	const SpellBook &SpellBookState() const { return m_spell_book; }
	const std::string &UiLayoutPath() const { return m_ui_layout_path; }
	CharacterCreation *Creation() { return m_creation.get(); }
	// For tools: the window-pixel center of a visible SIDL control on character select or creation.
	bool ControlCenter(const std::string &screen_id, int &x, int &y) const;

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
	void CharacterCreateEvent(const InputEvent &event);
	void ZoneEvent(const InputEvent &event);
	bool SetZoneKey(InputEvent::Key key, bool down, bool typing); // false: not a movement key
	MoveControls ZoneControls();

	void SubmitLogin();
	void PlaySelected();
	void RebuildServerLists();
	void HandleSessionEvents(const std::vector<LoginSession::Event> &events);
	void HandleWorldEvents(const std::vector<WorldSession::Event> &events);
	void EnterCharacterSelect();
	void EnterCharacterCreate();
	void ReturnToCharacterSelect();
	void ShowCreatedCharacter(); // the creation's choices in the 3D scene
	void RefreshCharacterButtons();
	void SelectSlot(int slot);
	void EnterWorldSelected();
	void LeaveWorld(); // close the world session and go back to the start screen
	// zoning: arriving through a zone line, which keeps the chat.
	void EnterZoneScreen(bool zoning = false);
	// CDisplay_DoTeleport for a zone line our feet stepped into (a "DRNTP..." region type).
	void HandleZoneLine(const std::string &type);
	std::string EqString(uint32_t id, const std::string &fallback) const; // eqstr_en.txt
	void HandleZoneEvents(const std::vector<ZoneSession::Event> &events);
	// The in-game SIDL windows, built once we're first in a zone (zoning keeps them), with the
	// character's layout from UI_<character>_<server>.ini and hot buttons and socials from
	// <character>_<server>.ini; each saved whenever it changes.
	void BuildZoneWindows();
	void SaveUiLayout();
	void SaveCustom(); // CEverQuest_saveCustom: the hot buttons and the socials changed
	void SaveDebugSettings(); // the debug menu's, to my-client.ini's [Debug]
	void HandleWindowClicks();
	void AddZoneLog(const std::string &text, uint32_t color); // a line of chat in this color
	void ChatOut(const std::string &text, int type);          // in a chat color type (ChatType)
	void DrawNameSprites(Canvas &canvas, uint64_t now_ms);
	// One name sprite (eqgfx's renderNameTag) sitting on the point x, y, `depth` in front of the
	// camera; a '\n' starts its second line.
	void DrawNameSprite(Canvas &canvas, int x, int y, float depth, float focal, const std::string &text, uint32_t color);
	void StartCamp();
	// CEverquest_InterpretCmd (chat_commands.cpp): a line typed into the chat window.
	void InterpretCommand(const std::string &line);
	// Our target, and the zone told (OP_TargetMouse, or OP_TargetCommand for /target).
	void SetTarget(uint16_t id, bool command = false);
	// EQPlayer::ChangeStance for us (100 standing, 110 sitting): the zone told, our model, and the
	// spellbook closing as we get up (DeacSpellScreen).
	void ChangeStance(uint8_t stance);
	bool CanSee(uint16_t spawn) const; // CastRay from us to a spawn, through the zone
	void Sit(); // CEverQuest_Sit: /sit and the Actions window's Sit and Stand

	// EqSoundManager::GiveTime's callers (character select and the world display): the listener and
	// the emitters' surroundings from the zone, then the sound's frame.
	void SoundFrame(uint64_t now_ms);
	void WeatherFrame(uint64_t now_ms);

	void RenderStart(Canvas &canvas);
	void RenderLogin(Canvas &canvas, uint64_t now_ms);
	void RenderServerSelect(Canvas &canvas);
	void RenderCharacterSelect(Canvas &canvas, uint64_t now_ms);
	void RenderCharacterCreate(Canvas &canvas, uint64_t now_ms);
	void RenderScene(Canvas &canvas, uint64_t now_ms); // the 3D character select scene, or black
	void RenderZone(Canvas &canvas, uint64_t now_ms);
	void RenderDialog(Canvas &canvas);
	Rect DialogArea() const; // the 640x480 area the dialog is laid out in, centered on the screen

	ButtonArt Art(int style);
	void Log(const std::string &line);
	std::unique_ptr<render::Renderer> MakeRenderer() const { return m_ui.make_renderer ? m_ui.make_renderer() : nullptr; }

	FrontEndArt &m_art;
	const EqText &m_strings;
	TextRenderer &m_text;
	double m_present_ms = 0;
	FrontEndConfig m_config;

	Screen m_screen = Screen::Start;
	Dialog m_dialog;
	bool m_quit = false;
	bool m_cursor_visible = true;
	int m_mouse_x = 0, m_mouse_y = 0; // window pixels
	int m_game_width = Width, m_game_height = Height;

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
	std::unique_ptr<CharSelectAnimWindow> m_anim_window; // CharSelectAnimWindow; null when it's off
	uint32_t m_anim_serial = 0;                          // the model its list was filled for, + 1

	// character creation
	CreationRules m_rules;
	std::unique_ptr<CharacterCreation> m_creation;
	bool m_creating = false; // waiting for the world server's answer

	// zone: the 3D view with the SIDL windows and the debug menu over it
	ChatWindow m_chat;
	ChatColors m_chat_colors;
	GameValues m_game_values;
	std::string m_last_tell; // who to /reply to (AddNameToTellList)
	std::string m_time_of_day;
	std::unique_ptr<ZoneView> m_zone_view;
	uint32_t m_my_look_serial = 0; // ZoneSpawn::look_serial our model was built for
	bool m_zone_error_logged = false;
	bool m_zoning = false; // through a zone line: from the zone's approval until the next zone is in
	DebugMenu m_debug;
	WindowManager m_windows;
	bool m_windows_built = false;
	std::string m_ui_layout_path;
	std::string m_hot_buttons_path;
	struct HeldKeys
	{
		bool up = false, down = false, left = false, right = false, w = false, a = false, s = false, d = false;
	};
	HeldKeys m_keys;
	bool m_alt_down = false; // the Alt key (GetKeyboardFlags' 4), for the gems' tooltips
	bool m_run = true;
	GameWindowState m_window_state; // the socials, the hot buttons, the button on the cursor
	SpellBook m_spell_book;         // and the spell gems
	SpellCast m_spell_cast;         // our casting from them
	Buffs m_buffs;                  // the spells on us
	bool m_jump_requested = false;
	bool m_crouch_sent = false; // the stance last sent for a standing jump's wind-up
	// Mouse drags in the zone: the right button turns the character and tilts the view, the left
	// button (in the chase view) circles the camera around them, and both run forward. Drags
	// measure from where they started (m_look_x, m_look_y), where the host holds the cursor.
	bool m_mouse_turn = false, m_mouse_pan = false;
	bool m_mouse_grabbed = false;
	int m_look_x = 0, m_look_y = 0;
	bool m_world_click = false; // the left button went down on the world
	int m_click_travel = 0;     // pixels the mouse moved since, to tell a click from a drag

	// sound
	std::unique_ptr<audio::EqSound> m_sound;
	bool m_opening_played = false;   // CDisplay::InitDDraw's opening music, once a run of eqgame
	bool m_main_loop_started = false; // the zone's first frame (DoMainLoop ends the opening music)
	uint64_t m_sound_surroundings_ms = 0;
	Weather m_weather; // CDisplay's (its sound, so far)

	LoginSession m_session;
	WorldSession m_world;
	ZoneSession m_zone;
	uint64_t m_now_ms = 0;
};
