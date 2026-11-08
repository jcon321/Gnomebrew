#pragma once

// eqgame's COptionsWnd on the "OptionsWindow" SIDL screen (EQUI_OptionsWindow.xml). So far its
// General, Display, Keyboard, Filters and Colors pages do something; the Mouse page shows empty.
//
// The Display page (SyncDisplayPage @0042a5e5, DisplayPageWndNotification @0042bba3): Show PC Names,
// Show NPC Names and Level of Detail checked by their settings; Sky (3D sky off, one layer, two),
// the spell particles' Near Clip Plane (2, 4, 6, 8 as Near .. Farthest) and Density (0, 0.25, 0.5,
// 1) and the other particles' Density (0..3) on their combo boxes; and the sliders, each with its
// value label, which changes only when the slider does: Gamma (8 ticks, the gamma 1..8, "%d %c" of
// value * 100 / 7), Far Clip Plane (21, value * 0.2 at least 0.1, value * 100 / 20 %), Fade Delay
// (21, value * 500 ms, "%.1f" seconds), Fade Duration (20, value * 500 + 500 ms), Window
// Transparency (230, alpha value + 26, as a percentage of 255), Fade-to Transparency (256, the
// alpha) and the spell particles' Opacity (101, hundredths). DisplayPageOnProcessFrame sets the
// page from the settings every frame it's up, so it follows what changes them elsewhere (the
// window menu's levels, /shownames). Video Modes is CVideoModesWnd (not ours).
//
// The General page (SyncGeneralPage @0042a4b2, GeneralPageWndNotification @0042b532): Fast Item
// Destroy, Receive Guild Invites, Autosplit, Anonymous and Roleplaying checked by their settings
// (the last two from one state: 1 anonymous, 2 roleplaying, 3 both); "Allow trading with" on
// everyone, party members only or no one (the setting's 2, 1, 0); Fast Drop on with confirmation,
// without or never (/fastdrop's 0, 1, -1); the three sliders 0..20 (SetNumTicks 0x15), each with
// its value label ("%d %c", the value * 100 / 20), which changes only when the slider does. The page
// shows them as the window's made and as the page is picked (ProcessGeneralOptionsTab); a click, a
// pick or a slider moved sets the setting through the host, which does what eqgame does with it.
//
// The Filters page ("OptionsChatPage"; SyncChatPage @0042a897, ChatPageWndNotification @0042c747):
// Chat0..Chat9 checked for the channels and messages shown (guild, socials, group, shouts,
// auctions, OOC, my misses, others' misses, others' hits, misses of me), DamageShields and
// NPCSpells checked when they're shown (their switches are 1 when hidden), and the PC spells, bard
// songs, critical spells and critical melee combo boxes on their switch. They show the /filter
// switches as the window's made and as the page is picked (ProcessChatOptionsTab), not as /filter
// changes them. A click sets the switch, and the zone's told and they're kept
// (CEverQuest_send_update_filters, saveOptions, saveOptions2).
//
// The Colors page (ColorPageWndNotification @0042c85b): UserColor0..71 each show their label in the
// player's text color they stand for (COptionsWnd_GetUserDefinedColor @0042a953, black as white),
// set as the page is picked (ProcessUserDefinedColorTab) and when the window's made. A click opens
// the color picker with it; Accept saves the color (SaveColors @0042c92b: CDisplay_SetUserDefinedColor
// and [TextColors] User_<n+1>_Red, _Green, _Blue, in my-client.ini rather than eqclient.ini). The
// buttons keep their old color until the page is picked again, as eqgame's do. Reset Default Colors
// asks (CConfirmationDialog 0x21, eqstr 6602, 20 s), and yes saves every built-in color
// (RestoreDefaultColors @0042ca2a, GetDefaultUserDefinedColor).
//
// The Keyboard page (COptionsWnd::UpdateKeyboardCategoryList @0042b07b,
// KeyboardPageWndNotification @0042c41c, KeyboardPageHandleKeyboardMsg @0042c4fb): the category
// combo box picks which commands the list shows, each with its key and its alternate. A click on
// one of those two cells shows "Press a key" there; the next key (with whatever of Shift, Ctrl and
// Alt is held) is bound to it and saved, Escape unbinds it, a key eqgame keeps for itself (Enter,
// /, the Windows keys) leaves it as it was and says so. "Reload defaults" asks first, then puts
// every key back (ResetKeysToDefault @0042b399). Ours, for now: a command my-client doesn't do
// yet is listed in red.

#include "filters.h"
#include "key_map.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class WindowManager;

class OptionsWindow
{
public:
	struct Host
	{
		std::function<std::string(uint32_t id, const std::string &fallback)> eqstr;
		std::function<void(const std::string &text, int type)> chat;
		// _writeKeyMapToIni: a binding changed; ResetKeysToDefault: all back to the defaults.
		std::function<void(int id, bool alternate, uint32_t key)> saved;
		std::function<void()> reset;
		// The Colors page: the player's color n (0xRRGGBB), SaveColors, and the color picker up
		// with a color, `done` getting the one accepted.
		std::function<uint32_t(int index)> user_color;
		std::function<void(int index, uint32_t rgb)> save_color;
		std::function<void(uint32_t color, std::function<void(uint32_t)> done)> pick_color;
		// The General page: a setting as eqgame keeps it (the check boxes 0 or 1; the anonymous
		// state 0..3 for Anonymous and Roleplaying; DAT_00798ad8 for the trade; /fastdrop's -1..1;
		// the sliders 0..20), and one set from the page, the same way. LoadSkin is the button.
		enum class General
		{
			FastItemDestroy, GuildInvites, Autosplit, Anonymous, Roleplaying, PlayerTrade, ItemDropping,
			SoundRealism, MusicVolume, SoundVolume, LoadSkin
		};
		std::function<int(General)> general;
		std::function<void(General, int)> set_general;
		// The Display page: a setting as eqgame keeps it (__PCNames, DAT_00798af8 and ae8 0 or 1;
		// the sky 0..2; the spell particles' near clip plane, density and opacity as floats;
		// the other particles' density 0..3; the gamma 1..8; the clip plane slider's value
		// DAT_00798b28; the windows' global fade delay and duration in ms, alpha and fade-to
		// alpha), and one set from the page. VideoModes is the button.
		enum class Display
		{
			PCNames, NPCNames, LevelOfDetail, Sky, SpellNearClip, SpellDensity, SpellOpacity, ParticleDensity,
			Gamma, ClipPlane, FadeDelay, FadeDuration, WindowAlpha, FadeToAlpha, VideoModes
		};
		std::function<float(Display)> display;
		std::function<void(Display, float)> set_display;
		// The Filters page: the /filter switches, and a click having changed one of them.
		ChatFilters *filters = nullptr;
		std::function<void()> filters_changed;
	};

	// The window's pages emptied but the general, keyboard, filters and colors pages', the keyboard
	// page filled (UpdateKeyboardAssignmentList: the first category), and the general and filters
	// pages set (SyncGeneralPage, SyncChatPage).
	void Attach(WindowManager &windows, KeyBindings &keys, Host host);

	// A click on the keyboard page, or on the confirmation dialog while it asks about the
	// defaults; true when it was one.
	bool Click(const std::string &window, const std::string &control, uint64_t now_ms);
	// Waiting for a key to bind (KeyAssignmentRow and KeyAssignmentColumn set). The keys go to it
	// before anything else does with them (ProcessKeyboardInput).
	bool Assigning() const { return m_row >= 0; }
	// A key down while assigning: `scan` the DirectInput code. True when it took it.
	bool Key(int scan, bool shift, bool ctrl, bool alt);
	// The assignment ends when the window or the page goes (COptionsWnd's hide and
	// ProcessTabPageSelection), and the question about the defaults after 20 seconds.
	void Frame(uint64_t now_ms);

private:
	void Fill();   // UpdateKeyboardCategoryList
	void Cancel(); // KeyboardPageCancelKeypressAssignment
	void SetCell(int row, int column, const std::string &text);
	bool KeyboardPageShowing() const;
	void ShowConfirmation(uint64_t now_ms); // CConfirmationDialog::Activate with eqstr 6602
	void UpdateUserColors();                // COptionsWnd_GetUserDefinedColor
	void SyncGeneralPage();
	bool GeneralPageClick(const std::string &control); // GeneralPageWndNotification's clicks and picks
	void GeneralPageSliders();                          // and its sliders (notification 0xd)
	void SyncDisplayPage();
	bool DisplayPageClick(const std::string &control); // DisplayPageWndNotification's clicks and picks
	void DisplayPageSliders();                          // and its sliders (notification 0xd)
	void SyncChatPage();
	bool ChatPageClick(const std::string &control); // ChatPageWndNotification

	WindowManager *m_windows = nullptr;
	KeyBindings *m_keys = nullptr;
	Host m_host;
	std::vector<int> m_row_ids; // each row's command (CListWnd's item data)
	int m_row = -1, m_column = -1; // the cell waiting for a key
	bool m_confirming = false;
	bool m_confirming_colors = false; // the dialog's 0x21: RestoreDefaultColors on yes
	bool m_color_page_shown = false;
	bool m_chat_page_shown = false;
	bool m_general_page_shown = false;
	int m_display_sliders[7] = {}; // the Display page's sliders as last seen
	int m_slider_values[3] = {}; // the General page's sliders as last seen (a change is CSliderWnd's 0xd)
	uint64_t m_confirm_until_ms = 0;
};
