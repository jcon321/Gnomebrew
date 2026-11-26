#include "video_modes_window.h"

#include "eqstr.h"
#include "window_manager.h"

#include <algorithm>

namespace
{
	const char *const ResolutionList = "VIDW_VideoResolutionsList";
	const char *const RefreshCombo = "VIDW_RefreshRateCombobox";
	const char *const BitDepthCombo = "VIDW_BitDepthCombobox";
	const char *const AcceptButton = "VIDW_AcceptButton";

	SidlWindow::Control *Child(WindowManager *windows, const char *id)
	{
		SidlWindow *wnd = windows ? windows->Find(VideoModesWindow::Screen) : nullptr;
		return wnd ? wnd->Find(id) : nullptr;
	}

	// SetColors(white, yellow, gray): the text, the row under the mouse and the selection.
	void SetColors(SidlWindow::Control &control)
	{
		control.row_color = 0xffffff;
		control.hover_color = 0xffff00;
		control.select_color = 0x808080;
	}
}

void VideoModesWindow::Attach(WindowManager &windows, const EqStr *eqstr, Host host)
{
	m_windows = &windows;
	m_eqstr = eqstr;
	m_host = std::move(host);
	m_active = false;
	m_resolutions.clear();
	if (!windows.Find(Screen)) // no SidlPiece: the constructor makes no children
		return;
	windows.Show(Screen, false);
	if (SidlWindow::Control *list = Child(m_windows, ResolutionList)) {
		SetColors(*list);
		list->columns = {{"Resolution", 200, {}}}; // AddColumn("Resolution", 200, 0, 3)
		list->justification = {0};
	}
	for (const char *id : {RefreshCombo, BitDepthCombo})
		if (SidlWindow::Control *combo = Child(m_windows, id))
			SetColors(*combo);
	Init();
}

std::string VideoModesWindow::RateText(int rate) const
{
	if (rate == 0)
		return m_eqstr && m_eqstr->Has(6629) ? m_eqstr->Format(6629, {}) : "Default";
	return m_eqstr && m_eqstr->Has(6628) ? m_eqstr->Format(6628, {std::to_string(rate)}) : std::to_string(rate) + "Hz";
}

std::string VideoModesWindow::FormatText(int format) const
{
	int id = format == 0x18 ? 6625 : format == 0x17 ? 6626 : 6627;
	const char *fallback = format == 0x18 ? "16bit(555)" : format == 0x17 ? "16bit(565)" : "32bit";
	return m_eqstr && m_eqstr->Has(id) ? m_eqstr->Format(id, {}) : fallback;
}

void VideoModesWindow::Init()
{
	SidlWindow::Control *list = Child(m_windows, ResolutionList);
	if (!list || !m_host.modes)
		return;
	// AddVideoModeToAvailableResolutions for each mode the adapter has, of a size and format it takes.
	m_resolutions.clear();
	for (const VideoMode &mode : m_host.modes()) {
		if (mode.width < 640 || mode.height < 480 || mode.format < 0x15 || mode.format > 0x18)
			continue;
		auto it = std::find_if(m_resolutions.begin(), m_resolutions.end(),
			[&](const Resolution &r) { return r.width == mode.width && r.height == mode.height; });
		if (it == m_resolutions.end()) {
			if (m_resolutions.size() > 0xff)
				continue;
			m_resolutions.push_back({mode.width, mode.height, {mode.refresh}, {mode.format}});
			continue;
		}
		if (it->rates.size() < 32 && std::find(it->rates.begin(), it->rates.end(), mode.refresh) == it->rates.end())
			it->rates.push_back(mode.refresh);
		if (it->formats.size() < 4 && std::find(it->formats.begin(), it->formats.end(), mode.format) == it->formats.end())
			it->formats.push_back(mode.format);
	}
	list->rows.clear(); // vtable 0x114, DeleteAll
	list->row_colors.clear();
	list->scroll_pos = 0;
	for (const char *id : {RefreshCombo, BitDepthCombo})
		if (SidlWindow::Control *combo = Child(m_windows, id)) {
			combo->choices.clear();
			combo->choice = -1;
		}
	VideoMode current = m_host.current ? m_host.current() : VideoMode{};
	int current_row = 0;
	for (size_t i = 0; i < m_resolutions.size(); ++i) {
		const Resolution &r = m_resolutions[i];
		list->rows.push_back({std::to_string(r.width) + "x" + std::to_string(r.height)});
		if (r.width != current.width || r.height != current.height)
			continue;
		current_row = static_cast<int>(i);
		FillCombos(r);
		// The rate and format we're in (+0x7a30, +0x7a20 and +0x7a24's 565).
		if (SidlWindow::Control *combo = Child(m_windows, RefreshCombo)) {
			auto at = std::find(r.rates.begin(), r.rates.end(), current.refresh);
			combo->choice = at == r.rates.end() ? 0 : static_cast<int>(at - r.rates.begin());
		}
		if (SidlWindow::Control *combo = Child(m_windows, BitDepthCombo)) {
			int choice = 0;
			for (size_t f = 0; f < r.formats.size(); ++f) {
				int format = r.formats[f];
				bool same = current.BitsPerPixel() == 32 ? (format == 0x15 || format == 0x16) : format == current.format;
				if (same)
					choice = static_cast<int>(f);
			}
			combo->choice = choice;
		}
	}
	list->selected_row = m_resolutions.empty() ? -1 : current_row; // SetCurSel
	list->selected_column = list->selected_row >= 0 ? 0 : -1;
}

void VideoModesWindow::FillCombos(const Resolution &r)
{
	if (SidlWindow::Control *combo = Child(m_windows, RefreshCombo))
		for (int rate : r.rates)
			combo->choices.push_back(RateText(rate)); // InsertChoice
	if (SidlWindow::Control *combo = Child(m_windows, BitDepthCombo))
		for (int format : r.formats)
			combo->choices.push_back(FormatText(format));
}

void VideoModesWindow::UpdateSelection(int resolution)
{
	if (resolution < 0 || resolution >= static_cast<int>(m_resolutions.size()))
		return;
	// The choices they had (GetCurChoice), DeleteAll, refilled, and SetChoice back.
	int rate_choice = 0, format_choice = 0;
	SidlWindow::Control *rate = Child(m_windows, RefreshCombo);
	SidlWindow::Control *format = Child(m_windows, BitDepthCombo);
	if (rate) {
		rate_choice = rate->choice;
		rate->choices.clear();
	}
	if (format) {
		format_choice = format->choice;
		format->choices.clear();
	}
	FillCombos(m_resolutions[resolution]);
	auto keep = [](SidlWindow::Control *combo, int choice) {
		if (combo)
			combo->choice = combo->choices.empty() ? -1 : std::clamp(choice, 0, static_cast<int>(combo->choices.size()) - 1);
	};
	keep(rate, rate_choice);
	keep(format, format_choice);
}

void VideoModesWindow::Activate()
{
	if (!m_windows || !m_windows->Find(Screen))
		return;
	m_windows->Show(Screen, true);
	m_windows->Raise(Screen);
	m_active = true;
}

void VideoModesWindow::Deactivate()
{
	if (m_windows && m_windows->Find(Screen))
		m_windows->Show(Screen, false);
	m_active = false;
}

bool VideoModesWindow::Click(const std::string &window, const std::string &control)
{
	if (window != Screen)
		return false;
	SidlWindow::Control *list = Child(m_windows, ResolutionList);
	int row = list ? list->selected_row : -1;
	if (control == ResolutionList) {
		UpdateSelection(row);
		return true;
	}
	if (control == AcceptButton) {
		if (!list || row < 0 || row >= static_cast<int>(m_resolutions.size()))
			return true;
		const Resolution &r = m_resolutions[row];
		const SidlWindow::Control *rate = Child(m_windows, RefreshCombo);
		const SidlWindow::Control *format = Child(m_windows, BitDepthCombo);
		int rate_choice = rate ? rate->choice : 0, format_choice = format ? format->choice : 0;
		VideoMode mode;
		mode.width = r.width;
		mode.height = r.height;
		mode.refresh = rate_choice >= 0 && rate_choice < static_cast<int>(r.rates.size()) ? r.rates[rate_choice] : 0;
		int f = format_choice >= 0 && format_choice < static_cast<int>(r.formats.size()) ? r.formats[format_choice] : 0x16;
		mode.format = f == 0x15 || f == 0x16 ? 0x16 : f; // 0x20 bits for the 32-bit formats, else 0x10
		if (m_host.accept)
			m_host.accept(mode);
		return true;
	}
	return control == RefreshCombo || control == BitDepthCombo;
}
