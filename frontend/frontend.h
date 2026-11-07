#pragma once

// The front end (what eqmain.dll does before eqgame.exe takes over): the start
// screen, the login screen and server select, laid out from eqmain's
// displayLuclinOptionsScreen (@10021a50), displayLoginScreen (@100226b0) and
// displayServerSelectionScreen (@100275d0). After PLAY it continues as eqgame.exe
// does: world authentication and character select (EQUI_CharacterSelect.xml,
// CEverQuest::DoCharacterSelection @0053b9cf). Portable: the platform layer feeds
// input events in window pixels, calls Update/Render with a canvas the size of the
// window (the game resolution), and presents it.

#include "abilities.h"
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
#include "key_map.h"
#include "name_sprites.h"
#include "login_client.h"
#include "sidl.h"
#include "bank_window.h"
#include "buffs.h"
#include "combat.h"
#include "color_picker.h"
#include "compass.h"
#include "containers.h"
#include "ground_items.h"
#include "loot_window.h"
#include "merchant_window.h"
#include "options_window.h"
#include "group.h"
#include "trade_window.h"
#include "spell_book.h"
#include "spell_cast.h"
#include "weather.h"
#include "window_manager.h"
#include "world_client.h"
#include "zone_session.h"
#include "zone_view.h"
#include "widgets.h"

#include <ctime>
#include <functional>
#include <map>
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
	render::LuclinModels luclin_models; // the player races drawn with their Luclin models (UseLuclin*), none by default
	bool char_select_anim_window = true; // our Animations window at character select (CharSelectAnimWindow)
	bool char_select_emotes = false;     // character select's random animations include socials' (CharSelectEmotes)
	bool attack_on_assist = false;       // /assist on|off (AttackOnAssist): the assist's answer turns auto attack on
	bool combat_music = true;            // /combatmusic on|off (CombatMusic): auto attack plays the combat music
	int loot_no_drop = 0;                // /lootnodrop (LootNoDrop): 0 always ask, 1 for what we can't use, 2 never
	int fast_drop = 0;                   // /fastdrop (FastDrop): 1 drop at once, 0 ask, -1 never
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
	// The confirmation dialog over character select and creation (CConfirmationDialog).
	bool ConfirmationOpen() const { return m_confirm.open; }
	bool ButtonsEnabled() const { return m_buttons_enabled; } // SetButtonsEnabledState's
	std::string ConfirmationText() const
	{
		const SidlWindow::Control *output = m_confirm.window.Find("TextOutput");
		return output ? output->text : std::string();
	}
	bool Exploring() const { return m_exploring; }
	const std::vector<CharacterSlot> &Characters() const { return m_world.Characters(); }
	int SelectedCharacter() const { return m_selected_slot; }
	int LastCharacter() const { return m_config.last_character; } // the slot to remember (LastCharSel)
	int ShowNames() const { return m_config.show_names; }         // /shownames, to remember (ShowNamesLevel)
	bool CharSelectEmotes() const { return m_config.char_select_emotes; } // the Animations window's choice
	bool AttackOnAssist() const { return m_config.attack_on_assist; }      // /assist on|off, to remember
	bool CombatMusic() const { return m_config.combat_music; }             // /combatmusic on|off, to remember
	bool AutoAttack() const { return m_attack; }                           // EverQuestinfo's __Attack
	int LootNoDrop() const { return m_config.loot_no_drop; }               // /lootnodrop, to remember
	int FastDrop() const { return m_config.fast_drop; }                    // /fastdrop, to remember
	const ZoneSession &Zone() const { return m_zone; }
	const Group &GroupState() const { return m_group; }
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

	// CConfirmationDialog::Activate over character select and creation (ConfirmationDialogBox):
	// category 0x1d is a message with OK, the rest ask Yes or No. It closes as No after timeout_ms
	// (0: never). A second one closes the first as No.
	void ShowConfirmation(int category, uint32_t timeout_ms, const std::string &text,
		std::function<void(bool)> on_close = nullptr);
	void CloseConfirmation(bool yes);
	bool ConfirmationEvent(const InputEvent &event); // a click on it; true if it took the event
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
	// ProcessKeyDown and ProcessKeyUp in the zone: a key's commands from the keymap, run or let go.
	void ZoneKey(const InputEvent &event);
	// CEverquest_ExecuteCmd: a command of the keymap's, its key down or let go; `typed` is the
	// character the key types (ConvertScanCodeToChar), which runs nothing while the chat has the keys.
	void ExecuteCmd(int cmd, bool down, int typed);
	uint32_t KeyWithModifiers(const InputEvent &event) const; // the scan code and Shift, Ctrl, Alt bits
	void SaveKeyBinding(int id, bool alternate, uint32_t key); // _writeKeyMapToIni
	void ResetKeyBindings();                                   // DeleteKeyBindingFromConfig for all
	MoveControls ZoneControls();
	bool ZoneCanMove() const; // in the zone, with no dialog up

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
	// CCharacterSelect::SetButtonsEnabledState: the character, Explore, Enter World and Delete buttons.
	void SetButtonsEnabledState(bool enabled);
	void DeleteSelected(); // CCharacterSelect::DeleteCharacter (and CEverQuest_RemoveCharacterOptionFile)
	void EnterExplorationMode();
	void LeaveExplorationMode();
	void ExploreEvent(const InputEvent &event); // explore mode's keys and mouse
	std::string CharacterNameTag(int slot) const; // SetNameSpriteState's "%s [%d %s]\n%s"
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
	bool BuffSpellInfo(const std::string &control, const std::string &window); // a buff's spell shown; false: not a buff
	void ActionsClick(const std::string &control); // an Actions window button, clicked or on a hot button
	// CEverQuest::RightClickedOnPlayer: a corpse's loot, a banker's bank, a merchant's shop, or else
	// /consider (of our target).
	void RightClickedOnPlayer(uint16_t spawn);
	bool IsOkToTransact() const;
	void WorldClick(int x, int y); // CEverQuest_LMouseUp on the world (window pixels) // CEverQuest_IsOkToTransact: no window that deals with someone up
	void AddZoneLog(const std::string &text, uint32_t color); // a line of chat in this color
	void ChatOut(const std::string &text, int type);          // in a chat color type (ChatType)
	// TurnCharacterNameSpritesOn / TurnNPCNameSpritesOn: the names over the heads of the spawns
	// drawn last frame (and ours), for the zone view's next Render to draw.
	void BuildNameSprites(uint64_t now_ms);
	// HandleContextMenuAction's picks of the window menu that open a window: Tint Color the color
	// picker with the window's BGTint, Normal and Faded Level the quantity window from 0 to 100 at
	// the alpha now (x 100 / 255), its answer back as x 255 / 100 (HandleContextMenuEvent's 0x1e and
	// 0x1d).
	void WindowMenuPicks();
	// A line of a name sprite: its text's coverage as a texture, made once and kept while in use.
	ZoneView::NameLine NameLineTexture(const std::string &text, uint64_t now_ms);
	struct NameTexture
	{
		std::unique_ptr<render::Texture> texture;
		ZoneView::NameLine line;
		uint64_t used_ms = 0;
	};
	std::map<std::string, NameTexture> m_name_textures;
	// One name sprite (eqgfx's renderNameTag) sitting on the point x, y, `depth` in front of the
	// camera; a '\n' starts its second line.
	void DrawNameSprite(Canvas &canvas, int x, int y, float depth, float focal, const std::string &text, uint32_t color);
	void StartCamp();
	// CEverquest_InterpretCmd (chat_commands.cpp): a line typed into the chat window.
	void InterpretCommand(const std::string &line);
	// CEverQuest_Consider and PrintConsiderAssessment (@0052d0e6): the zone's answer to /consider.
	void PrintConsider(const ZoneConsider &consider);
	// Our target, and the zone told (OP_TargetMouse, or OP_TargetCommand for /target).
	void SetTarget(uint16_t id, bool command = false);
	// CDisplay::GetNearestPlayerInView: the nearest NPC (or player) in the view no more than
	// `range` from the camera, with nothing of the zone's between; 0 for none.
	uint16_t NearestInView(float range, bool npc) const;
	// ExecuteCmd 28 and 29: Target nearest PC, Target nearest NPC.
	void TargetNearest(bool npc);
	// Ours, as Zeal's CycleTarget::get_next_ent: the NPCs in view within 250 of us we can see,
	// nearest first, the next of them each press (the nearest again after 3 seconds).
	void CycleNearestTarget();
	// ExecuteCmd 63, Toggle last two targets: from ourselves back to the target we left, or from
	// any other target (or none) to ourselves, while the chat line isn't open.
	void ToggleTarget();
	// AddNameToTellList (@004f847d): a name to the front, once.
	void AddNameToTellList(const std::string &name);
	// ExecuteCmd 60 and 61, Cycle reply forward and back (the new UI's branch): while the chat
	// line is open and starts with /tell or /rtarget, the next (or the last) name of the tell list
	// after it (CycleNamesInTellList @004f8531).
	void CycleReply(bool back);
	// ExecuteCmd's keys that open the chat line with something in it (Send tell, Reply, Send tell
	// to target, Command mode say and emote), while it isn't open (__InChatMode).
	void OpenChatLine(const std::string &text);
	// DoPercentConvert (@00538110): %t, %m, %s, %o, %p and %r in a line of chat, at its start or
	// after a space or an apostrophe, become the target's name, our pet's, the target's he, him,
	// his and its race. print_chat_dsp_chat runs it on every line printed, SendNewText,
	// CEverQuest::Emote and send_social on what they send.
	std::string PercentConvert(const std::string &text) const;
	// EQPlayer::NotOnSameDeity (@005097d2) of us and a player, on the deity team servers: we're on a
	// team (EQPlayer_WhatDeityAlliance) and they're not a GM on it.
	bool NotOnSameDeity(const ZoneSpawn &spawn) const;
	// CEverQuest::Who (@0052f614, chat_commands.cpp): /who's filters; the zone's players listed
	// here, or "all" asks the world (OP_WhoAllRequest), whose answer msg_who_response prints.
	void Who(const std::string &args);
	// /lfg (@004f9518): `ours` from the command (and on joining a group), else the zone's re-send.
	void Lfg(bool ours, const std::string &word);
	// /follow (@004fc259): auto-follow our target, a member of our group; FollowPlayerAI
	// (@00507b08), each frame we have control, turns us toward them and runs, walks or stops us by
	// the distance; TurnOffAutoFollow (@00507e0a) ends it.
	void Follow();
	// Archery and throwing (ranged_attack.cpp): EQPlayer::ModifyAttackSpeed (with a bow, a quiver's
	// haste), a slot's weapon delay as DoPassageOfTime takes it, DoPassageOfTime's readying of the
	// Range button, the button (CombatPageWndNotification), DoAttack(11), HandleAmmo and DoMissileHit.
	uint32_t ModifyAttackSpeed(uint32_t delay, bool bow) const;
	uint32_t WeaponDelay(int16_t slot) const;
	void RangedPassageOfTime(uint64_t now_ms);
	void RangeAttackButton();
	void RangedAttack(uint16_t target);
	int HandleAmmo();
	void DoMissileHit(uint16_t target, const ZoneItem &item);
	// CInvSlot_DoDrinkEatPoison (@004223ca): the right button on a common item (frontend.cpp).
	void DoDrinkEatPoison(const ZoneItem &item, int16_t slot);
	// EQ_Note::SendTextRequestMsg (@004ce45d): the right button on a book or note closes its window,
	// and asks for its text unless it was this one's (CNoteWnd::CheckNote, CBookWnd::CheckBook); the
	// answer (OP_ReadBook) fills it (CNoteWnd::SetNote, Activate).
	void ReadNote(const ZoneItem &item, int16_t slot);
	void BookText(int type, const std::string &text);
	std::string m_note_shown;   // CNoteWnd+0x544: the item whose note is up (ours: its slot and name)
	std::string m_note_title;   // its name (+0x144)
	void FollowPlayerAI();
	void TurnOffAutoFollow(bool say = true);
	void WhoAnswer(const ZoneWho &who);
	// CDisplay_IsInvisible to us (we don't know whether we see invisible: we don't).
	bool InvisibleToMe(const ZoneSpawn &spawn) const;
	// CDisplay::RealRender_World's DeathTimer: lying dead, 2 seconds on "Returning to home point,
	// please wait...", and the frame after it, back to our bind point (MoveToZone).
	void UpdateDeath(uint64_t now_ms);
	// EQPlayer::ChangeStance for us (100 standing, 110 sitting): the zone told, our model, and the
	// spellbook closing as we get up (DeacSpellScreen).
	void ChangeStance(uint8_t stance);
	bool CanSee(uint16_t spawn) const; // CastRay from us to a spawn, through the zone
	void Sit(); // CEverQuest_Sit: /sit and the Actions window's Sit and Stand
	// _EverQuestinfo_SetAutoAttack: on or off, the combat music with it (when /combatmusic is on).
	// DoPassageOfTime tells the zone once it's changed.
	void SetAutoAttack(bool on);
	// ExecuteCmd 8 (the Autoattack key, the Actions window's Attack button, /attack with nothing
	// after it): on or off standing, and said; not with the book open or a merchant or bank up.
	void ToggleAutoAttack();
	void AutoAttackOff(uint32_t said); // off with eqstr `said` (1466, 12371), if it was on
	// The hit, miss or death a Combat report tells: its lines, sounds, the victim's animation, our
	// new target, and what it does to us.
	void ApplyCombat(const Combat::Report &report);
	// The rest of msg_successful_hit and ReportSuccessfulHit for a spell's OP_Damage (SpellCasting
	// tells it): someone's detrimental spell on us makes them our target, its damage the victim's
	// get-hit sound and our standing up, and our hit points.
	void SpellDamaged(const ZoneCast &cast);
	// A /filter switch changed: the zone's told (CEverQuest_send_update_filters) and they're kept
	// (saveOptions).
	void FiltersChanged();
	// EQPlayer::CanIHitTarget (@00509e09) from us: not invulnerable, a target that isn't us,
	// AllowedToAttack, in sight (CastRay), within get_melee_range plus `extra` and in front of us;
	// says why not.
	bool CanIHitTarget(uint16_t id, float extra = 0);
	bool AllowedToAttack(const ZoneSpawn &target) const; // EQPlayer::AllowedToAttack (@0050996a), melee
	float MeleeRange(uint16_t id) const;  // get_melee_range (@004f3898) between us and a spawn
	float GroundDistance(uint16_t id) const; // CDisplay::PlayerDistance(us, spawn, 0): across the ground

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
	SidlWindow m_explore_window; // ExploreModeWindow (CExploreModeWnd)
	bool m_exploring = false;
	bool m_buttons_enabled = true; // SetButtonsEnabledState's
	struct Confirmation
	{
		SidlWindow window;
		bool built = false, open = false;
		int category = 0;
		uint64_t until_ms = 0; // 0: no timeout
		std::function<void(bool)> on_close;
	};
	Confirmation m_confirm;
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
	// __LastTell: who sent us tells, the newest first, 50 at most (AddNameToTellList); /reply goes
	// to the first, Cycle reply steps through them (CycleNamesInTellList's DAT_007cf0dc).
	std::vector<std::string> m_tell_list;
	int m_tell_index = 0;
	// A key that opened the chat line (OpenChatLine): its own character, which Windows sends after
	// the key, isn't typed into it (eqgame's DirectInput key reaches the edit box before the
	// command opens it).
	bool m_chat_opened_by_key = false;
	uint16_t m_toggle_target = 0; // Toggle last two targets' (DAT_00799788): the target we left for ourselves
	std::string m_time_of_day;
	std::string m_afk_message;                    // /afk's text (DAT_007cf0e0); nothing answers tells with it yet
	std::time_t m_started_at = std::time(nullptr); // WinMain's, for /played's session time
	std::unique_ptr<ZoneView> m_zone_view;
	uint32_t m_my_look_serial = 0; // ZoneSpawn::look_serial our model was built for
	bool m_zone_error_logged = false;
	bool m_zoning = false; // through a zone line: from the zone's approval until the next zone is in
	DebugMenu m_debug;
	WindowManager m_windows;
	bool m_windows_built = false;
	std::string m_ui_layout_path;
	std::string m_hot_buttons_path;
	// The movement commands held down (ExecuteCmd 3..6's Zeal_KeyInput, DAT_007ce05c and the
	// turns'), until their key comes up.
	struct HeldKeys
	{
		bool forward = false, back = false, left = false, right = false;
	};
	HeldKeys m_keys;
	// Autorun (ExecuteCmd 1's DAT_00798600): on and off with its key, off with Move forward or
	// back, any change of our stance (EQPlayer::ChangeStance) and a new zone (EnterZone).
	bool m_autorun = false;
	uint8_t m_autorun_stance = 0; // our stance when it was last looked at, to see it change
	uint64_t m_attack_timer_ms = 0; // ActorInfo's AttackTimer
	bool m_range_ready = false;     // __RangeAttackReady
	int m_last_attack = 0;          // ActorInfo's LastAttack: 11 shooting, 13 the primary's swing
	bool m_ranged_look = false;     // the held items changed for shooting or back: our model again
	bool m_lfg = false;           // DAT_007cf1fc: we're looking for a group
	uint16_t m_following = 0;     // ActorInfo's Following: whom /follow has us after (0 nobody)
	KeyBindings m_key_bindings; // the keymap, from my-client.ini's [KeyMaps]
	OptionsWindow m_options;
	bool m_alt_down = false; // the Alt key (GetKeyboardFlags' 4), for the gems' tooltips
	bool m_run = true;
	GameWindowState m_window_state; // the socials, the hot buttons, the button on the cursor
	SpellBook m_spell_book;         // and the spell gems
	SpellCast m_spell_cast;         // our casting from them
	Combat m_combat;                // what hits, misses and deaths say and do
	ChatFilters m_filters;          // /filter's switches, in my-client.ini's [Filters]
	Abilities m_abilities;          // the ability and combat skill buttons, and using skills
	bool m_attack = false;          // auto attack (__Attack)
	bool m_dead = false;            // we died in this zone (EQPlayer::IDied's stance 120): nothing moves us
	uint64_t m_death_ms = 0;        // ActorInfo's DeathTimer: when we lay down; 1 once told we're going home
	int m_cycle_index = -1;         // CycleNearestTarget's place in the list (Zeal's last_index)
	uint64_t m_cycle_ms = 0;        // its last press
	bool m_attack_sent = false;     // what OP_AutoAttack last said (DAT_0063db29)
	Buffs m_buffs;                  // the spells on us
	CompassWindow m_compass;
	ColorPicker m_color_picker;     // CColorPickerWnd, for the window menu's Tint Color
	Containers m_containers;        // the bag windows
	BankWindow m_bank;
	MerchantWindow m_merchant;
	LootWindow m_loot;
	TradeWindow m_trade;            // and the give window
	Group m_group;
	GroundItems m_ground;           // dropping and picking up
	uint64_t m_right_world_ms = 0;  // when the right button went down on the world (0: it didn't)
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
