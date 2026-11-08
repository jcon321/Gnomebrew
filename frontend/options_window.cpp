#include "options_window.h"

#include "chat_text.h"
#include "window_manager.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
	const char *const Screen = "OptionsWindow";
	const char *const Tabs = "OPTW_OptionsSubwindows";
	const char *const KeyboardPage = "OptionsKeyboardPage";
	const char *const FilterCombo = "OKP_KeyboardFilterCombobox";
	const char *const AssignmentList = "OKP_KeyboardAssignmentList";
	const char *const DefaultsButton = "OKP_DefaultKeysButton";
	const char *const ColorPage = "OptionsColorPage";
	const char *const ColorDefaultsButton = "UserColorDefault";
	const char *const GeneralPage = "OptionsGeneralPage";
	using General = OptionsWindow::Host::General;
	// The General page's check boxes (SyncGeneralPage: checked when the setting is 1).
	struct GeneralCheck
	{
		const char *id;
		General setting;
	};
	constexpr GeneralCheck GeneralChecks[] = {{"OGP_FastItemDestroyCheckbox", General::FastItemDestroy},
		{"OGP_GuildInvitesCheckbox", General::GuildInvites}, {"OGP_LootAutosplitCheckbox", General::Autosplit}};
	const char *const AnonymousCheck = "OGP_AnonymousCheckbox";
	const char *const RoleplayingCheck = "OGP_RoleplayingCheckbox";
	const char *const LoadSkinButton = "OGP_LoadSkinButton";
	const char *const PlayerTradeCombo = "OGP_PlayerTradeCombobox";
	const char *const ItemDroppingCombo = "OGP_ItemDroppingCombobox";
	// The sliders and their value labels, 21 ticks each (COptionsWnd::COptionsWnd's SetNumTicks).
	struct GeneralSlider
	{
		const char *id, *label;
		General setting;
	};
	constexpr GeneralSlider GeneralSliders[] = {
		{"OGP_SoundRealismSlider", "OGP_SoundRealismValueLabel", General::SoundRealism},
		{"OGP_MusicVolumeSlider", "OGP_MusicVolumeValueLabel", General::MusicVolume},
		{"OGP_SoundVolumeSlider", "OGP_SoundVolumeValueLabel", General::SoundVolume}};
	constexpr int SliderTicks = 0x15;
	const char *const DisplayPage = "OptionsDisplayPage";
	using Display = OptionsWindow::Host::Display;
	struct DisplayCheck
	{
		const char *id;
		Display setting;
	};
	constexpr DisplayCheck DisplayChecks[] = {{"ODP_PCNamesCheckbox", Display::PCNames},
		{"ODP_NPCNamesCheckbox", Display::NPCNames}, {"ODP_LevelOfDetailCheckbox", Display::LevelOfDetail}};
	const char *const VideoModesButton = "ODP_VideoModesButton";
	const char *const SkyCombo = "ODP_SkyCombobox";
	const char *const SpellNearClipCombo = "ODP_SpellParticlesNearClipCombobox";
	const char *const SpellDensityCombo = "ODP_SpellParticlesDensityCombobox";
	const char *const ParticleDensityCombo = "ODP_ParticleDensityCombobox";
	// The sliders (COptionsWnd::COptionsWnd's SetNumTicks) and their value labels.
	struct DisplaySlider
	{
		const char *id, *label;
		Display setting;
		int ticks;
	};
	constexpr DisplaySlider DisplaySliders[7] = {
		{"ODP_GammaSlider", "ODP_GammaValueLabel", Display::Gamma, 8},
		{"ODP_ClipPlaneSlider", "ODP_ClipPlaneValueLabel", Display::ClipPlane, 0x15},
		{"ODP_FadeDelaySlider", "ODP_FadeDelayValueLabel", Display::FadeDelay, 0x15},
		{"ODP_FadeDurationSlider", "ODP_FadeDurationValueLabel", Display::FadeDuration, 0x14},
		{"ODP_WindowAlphaSlider", "ODP_WindowAlphaValueLabel", Display::WindowAlpha, 0xe6},
		{"ODP_FadeToAlphaSlider", "ODP_FadeToAlphaValueLabel", Display::FadeToAlpha, 0x100},
		{"ODP_SpellParticlesOpacitySlider", "ODP_SpellParticlesOpacityValueLabel", Display::SpellOpacity, 0x65}};
	// CXStr_PrintString's "%d %c" with '%', and "%.1f".
	std::string Percent(int value) { return std::to_string(value) + " %"; }
	std::string Tenths(double value)
	{
		char text[32];
		snprintf(text, sizeof(text), "%.1f", value);
		return text;
	}
	const char *const ChatPage = "OptionsChatPage";
	// COptionsWnd's ChatArray ("Chat%d"): DAT_00798bf4..c18, 1 shown.
	constexpr int32_t ChatFilters::*const ChatSwitches[] = {&ChatFilters::guild, &ChatFilters::socials,
		&ChatFilters::group, &ChatFilters::shouts, &ChatFilters::auctions, &ChatFilters::ooc, &ChatFilters::my_misses,
		&ChatFilters::others_misses, &ChatFilters::others_hits, &ChatFilters::misses_me};
	// Checked when the switch is 0 (shown): DAT_00799670 and DAT_00799658.
	struct ShownAtZero
	{
		const char *id;
		int32_t ChatFilters::*value;
	};
	constexpr ShownAtZero ChatToggles[] = {
		{"DamageShields", &ChatFilters::damage_shields}, {"NPCSpells", &ChatFilters::npc_spells}};
	// The combo boxes: the choice is the switch, up to its count.
	struct ChatCombo
	{
		const char *id;
		int32_t ChatFilters::*value;
		int count;
	};
	constexpr ChatCombo ChatCombos[] = {{"PCSpellsComboBox", &ChatFilters::pc_spells, 3},
		{"BardSongsComboBox", &ChatFilters::bard_songs, 4}, {"CriticalSpellsComboBox", &ChatFilters::critical_spells, 3},
		{"CriticalMeleeComboBox", &ChatFilters::critical_melee, 3}};
	constexpr int UserColorButtons = 72; // ColorPageWndNotification's 0x48
	const char *const Dialog = "ConfirmationDialogBox";
	constexpr uint64_t ConfirmMs = 20000; // CConfirmationDialog::Activate(0x1c, 20000)

	// Ours, for now: a command my-client doesn't do yet is listed in red.
	constexpr uint32_t NotImplementedColor = 0xff0000;
}

void OptionsWindow::Attach(WindowManager &windows, KeyBindings &keys, Host host)
{
	m_windows = &windows;
	m_keys = &keys;
	m_host = std::move(host);
	m_row = m_column = -1;
	m_confirming = false;
	SidlWindow *wnd = windows.Find(Screen);
	if (!wnd)
		return;
	// Every page's pieces hidden but the keyboard, filters and color pages' (ours, until the others
	// do something).
	if (SidlWindow::Control *tabs = wnd->Find(Tabs))
		for (int page : tabs->children) {
			SidlWindow::Control &p = wnd->Item(page);
			const std::string &id = p.def->screen_id;
			if (id != KeyboardPage && id != ColorPage && id != ChatPage && id != GeneralPage && id != DisplayPage)
				for (int piece : p.children)
					wnd->Item(piece).visible = false;
		}
	if (SidlWindow::Control *list = wnd->Find(AssignmentList))
		list->header_color = 0x8aa3ff; // COptionsWnd::COptionsWnd: the list's +0x14c, 0xff8aa3ff
	// UpdateKeyboardAssignmentList: the first category.
	if (SidlWindow::Control *combo = wnd->Find(FilterCombo))
		combo->choice = 0;
	Fill();
	m_confirming_colors = false;
	m_color_page_shown = false;
	m_chat_page_shown = false;
	m_general_page_shown = false;
	for (int i = 0; i < 3; ++i)
		if (SidlWindow::Control *slider = wnd->Find(GeneralSliders[i].id)) {
			slider->slider_ticks = SliderTicks;
			wnd->SetSlider(*slider, slider->slider_value);
			m_slider_values[i] = slider->slider_value;
		}
	for (int i = 0; i < 7; ++i)
		if (SidlWindow::Control *slider = wnd->Find(DisplaySliders[i].id)) {
			slider->slider_ticks = DisplaySliders[i].ticks;
			wnd->SetSlider(*slider, slider->slider_value);
			m_display_sliders[i] = slider->slider_value;
		}
	UpdateUserColors(); // COptionsWnd::COptionsWnd
	SyncGeneralPage();
	SyncDisplayPage();
	SyncChatPage();
}

void OptionsWindow::SyncGeneralPage()
{
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	if (!wnd || !m_host.general)
		return;
	for (const GeneralCheck &check : GeneralChecks)
		if (SidlWindow::Control *button = wnd->Find(check.id))
			button->checked = m_host.general(check.setting) == 1;
	int anon = m_host.general(General::Anonymous);
	if (SidlWindow::Control *button = wnd->Find(AnonymousCheck))
		button->checked = anon == 1 || anon == 3;
	if (SidlWindow::Control *button = wnd->Find(RoleplayingCheck))
		button->checked = anon == 2 || anon == 3;
	if (SidlWindow::Control *combo = wnd->Find(PlayerTradeCombo)) {
		int trade = m_host.general(General::PlayerTrade);
		combo->choice = trade == 0 ? 2 : trade == 1 ? 1 : 0;
	}
	if (SidlWindow::Control *combo = wnd->Find(ItemDroppingCombo)) {
		int drop = m_host.general(General::ItemDropping);
		combo->choice = drop == -1 ? 2 : drop == 1 ? 1 : 0;
	}
	for (const GeneralSlider &slider : GeneralSliders)
		if (SidlWindow::Control *control = wnd->Find(slider.id))
			wnd->SetSlider(*control, m_host.general(slider.setting)); // CSliderWnd::SetValue
	GeneralPageSliders(); // SetValue's notification, for those it moved
}

void OptionsWindow::GeneralPageSliders()
{
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	if (!wnd)
		return;
	for (int i = 0; i < 3; ++i) {
		const GeneralSlider &slider = GeneralSliders[i];
		const SidlWindow::Control *control = wnd->Find(slider.id);
		if (!control || control->slider_value == m_slider_values[i])
			continue;
		m_slider_values[i] = control->slider_value;
		// The value label, "%d %c" with the value * 100 / 20 and '%'; the level, saved.
		if (SidlWindow::Control *label = wnd->Find(slider.label))
			label->text = Percent(control->slider_value * 100 / 20);
		if (m_host.set_general)
			m_host.set_general(slider.setting, control->slider_value);
	}
}

void OptionsWindow::SyncDisplayPage()
{
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	if (!wnd || !m_host.display)
		return;
	auto value = [this](Display setting) { return m_host.display(setting); };
	for (const DisplayCheck &check : DisplayChecks)
		if (SidlWindow::Control *button = wnd->Find(check.id))
			button->checked = value(check.setting) == 1;
	if (SidlWindow::Control *combo = wnd->Find(SkyCombo)) {
		int sky = static_cast<int>(value(Display::Sky));
		combo->choice = sky >= 0 && sky <= 2 ? sky : 0;
	}
	if (SidlWindow::Control *combo = wnd->Find(SpellNearClipCombo)) {
		float clip = value(Display::SpellNearClip);
		combo->choice = clip > 2 ? (clip > 4 ? (clip > 6 ? 3 : 2) : 1) : 0;
	}
	if (SidlWindow::Control *combo = wnd->Find(SpellDensityCombo)) {
		float density = value(Display::SpellDensity);
		combo->choice = density > 0 ? (density > 0.25f ? (density > 0.5f ? 3 : 2) : 1) : 0;
	}
	if (SidlWindow::Control *combo = wnd->Find(ParticleDensityCombo)) {
		int density = static_cast<int>(value(Display::ParticleDensity));
		combo->choice = density >= 0 && density <= 3 ? density : 0;
	}
	// The sliders' values (CSliderWnd::SetValue clamps them to their ticks).
	auto opacity = [](float x) { // COptionsWnd_SyncDisplayPage2: up from a half
		return static_cast<int>(std::fmod(x, 1.0f) < 0.5f ? std::floor(x) : std::ceil(x));
	};
	int values[7] = {
		static_cast<int>(value(Display::Gamma)) - 1,
		static_cast<int>(value(Display::ClipPlane)),
		static_cast<int>(static_cast<unsigned>(value(Display::FadeDelay)) / 500),
		static_cast<int>(std::min<unsigned>((static_cast<unsigned>(value(Display::FadeDuration)) - 500u) / 500u, 0x7fffffffu)),
		(static_cast<int>(value(Display::WindowAlpha)) & 0xff) - 0x1a,
		static_cast<int>(value(Display::FadeToAlpha)) & 0xff,
		opacity(value(Display::SpellOpacity) * 100),
	};
	for (int i = 0; i < 7; ++i)
		if (SidlWindow::Control *slider = wnd->Find(DisplaySliders[i].id))
			wnd->SetSlider(*slider, values[i]);
	DisplayPageSliders(); // SetValue's notification, for those it moved
}

void OptionsWindow::DisplayPageSliders()
{
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	if (!wnd)
		return;
	for (int i = 0; i < 7; ++i) {
		const DisplaySlider &slider = DisplaySliders[i];
		const SidlWindow::Control *control = wnd->Find(slider.id);
		if (!control || control->slider_value == m_display_sliders[i])
			continue;
		int v = m_display_sliders[i] = control->slider_value;
		std::string text;
		float setting = 0;
		switch (slider.setting) {
		case Display::Gamma: text = Percent(v * 100 / 7), setting = static_cast<float>(v + 1); break;
		case Display::ClipPlane: text = Percent(v * 100 / 20), setting = static_cast<float>(v); break;
		case Display::FadeDelay: text = Tenths(v * 0.5), setting = static_cast<float>(v * 500); break;
		case Display::FadeDuration: text = Tenths(v * 0.5 + 0.5), setting = static_cast<float>(v * 500 + 500); break;
		case Display::WindowAlpha: text = Percent((v + 0x1a) * 100 / 0xff), setting = static_cast<float>(v + 0x1a); break;
		case Display::FadeToAlpha: text = Percent(v * 100 / 0xff), setting = static_cast<float>(v); break;
		case Display::SpellOpacity: text = Percent(v), setting = v * 0.01f; break;
		default: break;
		}
		if (SidlWindow::Control *label = wnd->Find(slider.label))
			label->text = text;
		if (m_host.set_display)
			m_host.set_display(slider.setting, setting);
	}
}

bool OptionsWindow::DisplayPageClick(const std::string &control)
{
	SidlWindow *wnd = m_windows->Find(Screen);
	const SidlWindow::Control *clicked = wnd ? wnd->Find(control) : nullptr;
	if (!clicked || !m_host.set_display)
		return false;
	for (const DisplayCheck &check : DisplayChecks)
		if (control == check.id) {
			m_host.set_display(check.setting, clicked->checked ? 1.0f : 0.0f);
			return true;
		}
	if (control == VideoModesButton) {
		m_host.set_display(Display::VideoModes, 0);
		return true;
	}
	int choice = clicked->choice;
	if (control == SkyCombo) {
		if (choice >= 0 && choice <= 2)
			m_host.set_display(Display::Sky, static_cast<float>(choice));
		return true;
	}
	if (control == ParticleDensityCombo) {
		if (choice >= 0 && choice <= 3)
			m_host.set_display(Display::ParticleDensity, static_cast<float>(choice));
		return true;
	}
	if (control == SpellDensityCombo) {
		static const float densities[] = {0, 0.25f, 0.5f, 1};
		if (choice >= 0 && choice <= 3)
			m_host.set_display(Display::SpellDensity, densities[choice]);
		return true;
	}
	if (control == SpellNearClipCombo) {
		static const float clips[] = {2, 4, 6, 8};
		if (choice >= 0 && choice <= 3)
			m_host.set_display(Display::SpellNearClip, clips[choice]);
		return true;
	}
	return false;
}

bool OptionsWindow::GeneralPageClick(const std::string &control)
{
	SidlWindow *wnd = m_windows->Find(Screen);
	const SidlWindow::Control *clicked = wnd ? wnd->Find(control) : nullptr;
	if (!clicked || !m_host.general || !m_host.set_general)
		return false;
	for (const GeneralCheck &check : GeneralChecks)
		if (control == check.id) {
			m_host.set_general(check.setting, clicked->checked ? 1 : 0);
			return true;
		}
	// Anonymous adds or takes 1 from the state, Roleplaying 2.
	if (control == AnonymousCheck || control == RoleplayingCheck) {
		bool anonymous = control == AnonymousCheck;
		int step = anonymous ? 1 : 2;
		int anon = m_host.general(General::Anonymous) + (clicked->checked ? step : -step);
		m_host.set_general(anonymous ? General::Anonymous : General::Roleplaying, anon);
		return true;
	}
	if (control == LoadSkinButton) {
		m_host.set_general(General::LoadSkin, 0);
		return true;
	}
	if (control == PlayerTradeCombo) {
		if (clicked->choice >= 0 && clicked->choice <= 2)
			m_host.set_general(General::PlayerTrade, 2 - clicked->choice);
		return true;
	}
	if (control == ItemDroppingCombo) {
		// /fastdrop with eqstr 12946 "off", 12947 "on" or 12989 "never".
		m_host.set_general(General::ItemDropping, clicked->choice == 0 ? 0 : clicked->choice == 1 ? 1 : -1);
		return true;
	}
	return false;
}

void OptionsWindow::SyncChatPage()
{
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	if (!wnd || !m_host.filters)
		return;
	const ChatFilters &filters = *m_host.filters;
	for (int i = 0; i < static_cast<int>(std::size(ChatSwitches)); ++i)
		if (SidlWindow::Control *button = wnd->Find("Chat" + std::to_string(i)))
			button->checked = filters.*ChatSwitches[i] != 0;
	for (const ShownAtZero &toggle : ChatToggles)
		if (SidlWindow::Control *button = wnd->Find(toggle.id))
			button->checked = filters.*toggle.value == 0;
	for (const ChatCombo &combo : ChatCombos)
		if (SidlWindow::Control *box = wnd->Find(combo.id))
			box->choice = filters.*combo.value; // CComboWnd::SetChoice
}

bool OptionsWindow::ChatPageClick(const std::string &control)
{
	SidlWindow *wnd = m_windows->Find(Screen);
	const SidlWindow::Control *clicked = wnd ? wnd->Find(control) : nullptr;
	if (!clicked || !m_host.filters)
		return false;
	ChatFilters &filters = *m_host.filters;
	bool changed = false;
	for (int i = 0; i < static_cast<int>(std::size(ChatSwitches)); ++i)
		if (control == "Chat" + std::to_string(i)) {
			filters.*ChatSwitches[i] = clicked->checked ? 1 : 0;
			changed = true;
		}
	for (const ShownAtZero &toggle : ChatToggles)
		if (control == toggle.id) {
			filters.*toggle.value = clicked->checked ? 0 : 1;
			changed = true;
		}
	for (const ChatCombo &combo : ChatCombos)
		if (control == combo.id && clicked->choice >= 0 && clicked->choice < combo.count) {
			filters.*combo.value = clicked->choice;
			changed = true;
		}
	if (!changed)
		return false;
	// CEverQuest_send_update_filters, saveOptions, saveOptions2.
	if (m_host.filters_changed)
		m_host.filters_changed();
	return true;
}

void OptionsWindow::UpdateUserColors()
{
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	if (!wnd || !m_host.user_color)
		return;
	for (int i = 0; i < UserColorButtons; ++i)
		if (SidlWindow::Control *button = wnd->Find("UserColor" + std::to_string(i))) {
			uint32_t color = m_host.user_color(i) & 0xffffff;
			button->text_color = color == 0 ? 0xffffff : color;
		}
}

void OptionsWindow::Fill()
{
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	SidlWindow::Control *list = wnd ? wnd->Find(AssignmentList) : nullptr;
	if (!list)
		return;
	const SidlWindow::Control *combo = wnd->Find(FilterCombo);
	uint32_t mask = KeyMap::CategoryMask(combo ? combo->choice : 8);
	list->rows.clear();
	list->row_colors.clear();
	m_row_ids.clear();
	for (const KeyMap::Command &command : KeyMap::List()) {
		if (!(command.categories & mask))
			continue;
		list->rows.push_back({m_host.eqstr && command.eqstr ? m_host.eqstr(command.eqstr, command.name) : command.name,
			KeyMap::Describe(m_keys->Get(command.id, false)), KeyMap::Describe(m_keys->Get(command.id, true))});
		list->row_colors.push_back(command.implemented ? 0 : NotImplementedColor);
		m_row_ids.push_back(command.id);
	}
	list->selected_row = list->selected_column = -1;
	list->scroll_pos = 0;
	wnd->UpdateListScroll(*list);
}

void OptionsWindow::SetCell(int row, int column, const std::string &text)
{
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	SidlWindow::Control *list = wnd ? wnd->Find(AssignmentList) : nullptr;
	if (list && row >= 0 && row < static_cast<int>(list->rows.size()) && column < static_cast<int>(list->rows[row].size()))
		list->rows[row][column] = text;
}

void OptionsWindow::Cancel()
{
	if (m_row < 0)
		return;
	if (m_row < static_cast<int>(m_row_ids.size()))
		SetCell(m_row, m_column, KeyMap::Describe(m_keys->Get(m_row_ids[m_row], m_column == 2)));
	m_row = m_column = -1;
}

bool OptionsWindow::Click(const std::string &window, const std::string &control, uint64_t now_ms)
{
	if (!m_windows)
		return false;
	if (window == Dialog && m_confirming) {
		// ProcessYes's 0x1c: ResetKeysToDefault; 0x21: RestoreDefaultColors.
		if (control == "Yes_Button" && m_confirming_colors) {
			if (m_host.save_color)
				for (int i = 0; i < UserColorButtons; ++i)
					m_host.save_color(i, ChatColors::Default(i));
		}
		else if (control == "Yes_Button") {
			m_keys->Defaults();
			Fill();
			if (m_host.reset)
				m_host.reset();
		}
		if (control == "Yes_Button" || control == "No_Button" || control == "OK_Button") {
			m_confirming = m_confirming_colors = false;
			m_windows->Show(Dialog, false);
		}
		return true;
	}
	if (window != Screen)
		return false;
	SidlWindow *wnd = m_windows->Find(Screen);
	if (control == FilterCombo) {
		Cancel();
		Fill();
		return true;
	}
	if (control == AssignmentList) {
		const SidlWindow::Control *list = wnd->Find(AssignmentList);
		Cancel();
		// The key's or the alternate's cell: "Press a key" there until one comes (eqstr 6598). The
		// command's own does nothing.
		if (list && (list->selected_column == 1 || list->selected_column == 2) && list->selected_row >= 0 &&
		    list->selected_row < static_cast<int>(m_row_ids.size())) {
			m_row = list->selected_row;
			m_column = list->selected_column;
			SetCell(m_row, m_column, m_host.eqstr ? m_host.eqstr(6598, "Press a key") : "Press a key");
		}
		return true;
	}
	if (control == DefaultsButton) {
		// CConfirmationDialog::Activate(0x1c, 20000) with eqstr 6602.
		if (!m_windows->Shown(Dialog)) {
			m_confirming_colors = false;
			ShowConfirmation(now_ms);
		}
		return true;
	}
	// The Colors page: a color's button opens the picker with its color, and what's accepted is
	// saved; Reset Default Colors asks first (0x21, 20000, eqstr 6602).
	if (control == ColorDefaultsButton) {
		if (!m_windows->Shown(Dialog)) {
			m_confirming_colors = true;
			ShowConfirmation(now_ms);
		}
		return true;
	}
	if (GeneralPageClick(control) || DisplayPageClick(control) || ChatPageClick(control))
		return true;
	if (control.rfind("UserColor", 0) == 0) {
		int index = -1;
		const std::string digits = control.substr(9);
		if (!digits.empty() && std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; }))
			index = std::stoi(digits);
		const SidlWindow::Control *button = index >= 0 && index < UserColorButtons ? wnd->Find(control) : nullptr;
		if (!button || !m_host.pick_color)
			return true;
		m_host.pick_color(button->text_color, [this, index](uint32_t rgb) {
			if (m_host.save_color)
				m_host.save_color(index, rgb);
		});
		return true;
	}
	return false;
}

void OptionsWindow::ShowConfirmation(uint64_t now_ms)
{
	SidlWindow *dialog = m_windows->Find(Dialog);
	if (!dialog)
		return;
	for (const char *id : {"Yes_Button", "No_Button"})
		if (SidlWindow::Control *button = dialog->Find(id))
			button->visible = true;
	if (SidlWindow::Control *ok = dialog->Find("OK_Button"))
		ok->visible = false;
	if (SidlWindow::Control *text = dialog->Find("TextOutput")) {
		text->text = m_host.eqstr ? m_host.eqstr(6602, "Are you sure you want to lose your current settings?")
		                          : "Are you sure you want to lose your current settings?";
		text->scroll_pos = 0;
	}
	const Rect &r = dialog->Bounds();
	m_windows->MoveTo(Dialog, (m_windows->ScreenWidth() - r.w) / 2, (m_windows->ScreenHeight() - r.h) / 2);
	m_windows->Show(Dialog, true);
	m_windows->Raise(Dialog);
	m_confirming = true;
	m_confirm_until_ms = now_ms + ConfirmMs;
}

bool OptionsWindow::Key(int scan, bool shift, bool ctrl, bool alt)
{
	if (m_row < 0 || m_row >= static_cast<int>(m_row_ids.size()))
		return false;
	int id = m_row_ids[m_row];
	if (id > KeyMap::Assignable && id < KeyMap::OwnFirst) {
		Cancel();
		return true;
	}
	bool alternate = m_column == 2;
	uint32_t key;
	if (!KeyMap::IsValidKey(scan) && scan != 0x01) {
		// CEverQuest_IsValidKeyMap: eqstr 12092, and the key stays as it was.
		if (m_host.chat)
			m_host.chat(m_host.eqstr ? m_host.eqstr(12092, "That Key is used by EverQuest and cannot be assigned by the "
			                                               "user to a user-defined keymap.")
			                         : "That Key is used by EverQuest and cannot be assigned by the user to a "
			                           "user-defined keymap.",
				ChatType::Yellow);
		key = m_keys->Get(id, alternate);
	}
	else {
		// Shift, Ctrl and Alt alone wait for the key they go with; Escape unbinds.
		if (KeyMap::IsModifierKey(scan))
			return true;
		if (scan == 0x01)
			key = KeyMap::NoKey;
		else
			key = static_cast<uint32_t>(scan) | (ctrl ? KeyMap::Ctrl : 0) | (shift ? KeyMap::Shift : 0) |
			      (alt ? KeyMap::Alt : 0);
	}
	m_keys->Set(id, alternate, key);
	SetCell(m_row, m_column, KeyMap::Describe(key));
	if (m_host.saved)
		m_host.saved(id, alternate, key);
	m_row = m_column = -1;
	return true;
}

bool OptionsWindow::KeyboardPageShowing() const
{
	const SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	if (!wnd || !m_windows->Shown(Screen))
		return false;
	const SidlWindow::Control *tabs = wnd->Find(Tabs);
	const SidlWindow::Control *page = wnd->Find(KeyboardPage);
	return tabs && page && page->visible;
}

void OptionsWindow::Frame(uint64_t now_ms)
{
	if (!m_windows)
		return;
	if (m_row >= 0 && !KeyboardPageShowing())
		Cancel();
	// ProcessTabPageSelection's ProcessUserDefinedColorTab: the buttons' colors as the page is
	// picked (not as the window comes back up on it).
	const SidlWindow *options = m_windows->Find(Screen);
	const SidlWindow::Control *color_page = options ? options->Find(ColorPage) : nullptr;
	bool colors = color_page && color_page->visible;
	if (colors && !m_color_page_shown)
		UpdateUserColors();
	m_color_page_shown = colors;
	// ProcessGeneralOptionsTab's SyncGeneralPage, as the General page is picked; a slider moved.
	const SidlWindow::Control *general_page = options ? options->Find(GeneralPage) : nullptr;
	bool general = general_page && general_page->visible;
	if (general && !m_general_page_shown)
		SyncGeneralPage();
	m_general_page_shown = general;
	GeneralPageSliders();
	// DisplayPageOnProcessFrame: SyncDisplayPage every frame the page is up (a slider moved first,
	// as its notification would have come at once).
	const SidlWindow::Control *display_page = options ? options->Find(DisplayPage) : nullptr;
	if (display_page && display_page->visible && m_windows->Shown(Screen)) {
		DisplayPageSliders();
		SyncDisplayPage();
	}
	// ProcessChatOptionsTab's SyncChatPage, as the Filters page is picked.
	const SidlWindow::Control *chat_page = options ? options->Find(ChatPage) : nullptr;
	bool chat = chat_page && chat_page->visible;
	if (chat && !m_chat_page_shown)
		SyncChatPage();
	m_chat_page_shown = chat;
	if (SidlWindow *wnd = m_windows->Find(Screen); wnd && !m_windows->Shown(Screen))
		wnd->CloseCombos();
	if (m_confirming && (now_ms >= m_confirm_until_ms || !m_windows->Shown(Dialog))) {
		m_confirming = m_confirming_colors = false; // ProcessNo, as the time runs out
		m_windows->Show(Dialog, false);
	}
}
