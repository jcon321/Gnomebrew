#include "color_picker.h"

#include "window_manager.h"

#include <algorithm>
#include <cstdlib>

namespace
{
	const char *const Sliders[3] = {"CPW_RedSlider", "CPW_GreenSlider", "CPW_BlueSlider"};
	const char *const Boxes[3] = {"CPW_RedSliderInput", "CPW_GreenSliderInput", "CPW_BlueSliderInput"};
	constexpr int Swatches = 15;        // CPW_TC0Button..CPW_TC14Button
	const char *const Preview = "CPW_TC15Button";
	const char *const Accept = "CPW_Accept_Button";

	std::string SwatchName(int i)
	{
		return "CPW_TC" + std::to_string(i) + "Button"; // "CPW_TC%dButton"
	}
}

void ColorPicker::Attach(WindowManager &windows)
{
	m_windows = &windows;
	m_active = false;
	m_done = nullptr;
	SidlWindow *wnd = windows.Find(Screen);
	if (!wnd)
		return;
	// CColorPickerWnd::CColorPickerWnd: each swatch's decal in its own TextColor; 256 ticks a
	// slider, three characters a box.
	for (int i = 0; i <= Swatches; ++i)
		if (SidlWindow::Control *swatch = wnd->Find(SwatchName(i)))
			swatch->decal_tint = swatch->text_color;
	for (int c = 0; c < 3; ++c) {
		if (SidlWindow::Control *slider = wnd->Find(Sliders[c]))
			slider->slider_ticks = 256;
		if (SidlWindow::Control *box = wnd->Find(Boxes[c]))
			box->max_chars = 3;
	}
}

void ColorPicker::SetColor(uint32_t color)
{
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	if (!wnd)
		return;
	m_rgb[0] = (color >> 16) & 0xff;
	m_rgb[1] = (color >> 8) & 0xff;
	m_rgb[2] = color & 0xff;
	for (int c = 0; c < 3; ++c) {
		SidlWindow::Control *slider = wnd->Find(Sliders[c]);
		SidlWindow::Control *box = wnd->Find(Boxes[c]);
		if (slider) {
			wnd->SetSlider(*slider, static_cast<int>(m_rgb[c]));
			m_slid[c] = slider->slider_value;
		}
		if (box) { // UpdateEditWndFromSlider
			box->text = std::to_string(m_rgb[c]);
			m_typed[c] = box->text;
		}
	}
	if (SidlWindow::Control *preview = wnd->Find(Preview)) // UpdateColorPreviewFromRGB
		preview->decal_tint = Color();
}

void ColorPicker::Activate(uint32_t color, std::function<void(uint32_t)> done)
{
	if (!m_windows || !m_windows->Find(Screen))
		return;
	m_done = std::move(done);
	SetColor(color);
	m_active = true;
	m_windows->Show(Screen, true);
	m_windows->Raise(Screen);
}

bool ColorPicker::Click(const std::string &window, const std::string &control)
{
	if (window != Screen)
		return false;
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	if (!wnd || !m_active)
		return true;
	if (control == Accept) {
		// UpdateSliderFromEditBox for each, then the color to whoever asked, and away.
		for (int c = 0; c < 3; ++c)
			if (SidlWindow::Control *box = wnd->Find(Boxes[c]))
				m_rgb[c] = static_cast<uint32_t>(std::clamp(std::atoi(box->text.c_str()), 0, 255));
		uint32_t color = Color();
		SetColor(color);
		std::function<void(uint32_t)> done = std::move(m_done);
		m_done = nullptr;
		m_active = false;
		m_windows->Show(Screen, false);
		if (done)
			done(color);
		return true;
	}
	for (int i = 0; i < Swatches; ++i)
		if (control == SwatchName(i)) {
			if (const SidlWindow::Control *swatch = wnd->Find(control))
				SetColor(swatch->decal_tint);
			break;
		}
	return true;
}

void ColorPicker::Update()
{
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	if (!wnd || !m_windows->Shown(Screen)) {
		m_active = false;
		return;
	}
	bool changed = false;
	for (int c = 0; c < 3; ++c) {
		SidlWindow::Control *slider = wnd->Find(Sliders[c]);
		SidlWindow::Control *box = wnd->Find(Boxes[c]);
		if (!slider || !box)
			continue;
		if (slider->slider_value != m_slid[c]) {
			// A slider moved: its value in its box (UpdateEditWndFromSlider).
			m_slid[c] = slider->slider_value;
			m_rgb[c] = static_cast<uint32_t>(m_slid[c]);
			box->text = std::to_string(m_rgb[c]);
			m_typed[c] = box->text;
			changed = true;
		}
		else if (box->text != m_typed[c]) {
			// Typed into: the number in the slider, more than 255 written back as 255.
			int value = std::atoi(box->text.c_str());
			if (value > 255) {
				value = 255;
				box->text = "255";
			}
			value = std::max(value, 0);
			wnd->SetSlider(*slider, value);
			m_slid[c] = slider->slider_value;
			m_rgb[c] = static_cast<uint32_t>(value);
			m_typed[c] = box->text;
			changed = true;
		}
	}
	if (changed)
		if (SidlWindow::Control *preview = wnd->Find(Preview))
			preview->decal_tint = Color();
}
