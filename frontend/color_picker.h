#pragma once

// eqgame's CColorPickerWnd (@00414ceb on), the ColorPickerWnd screen: fifteen swatches
// (CPW_TC0Button..CPW_TC14Button, each its TextColor in its A_WhitePixel decal, as the constructor
// copies a button's 0x68 into its decal color 0x11c), the color as it stands in CPW_TC15Button,
// red, green and blue sliders of 256 ticks with three-character edit boxes beside them, and
// Accept.
//   - ColorPickerWnd_Activate / SetCurrentColor: up with a color, its channels in the sliders,
//     the boxes and the preview.
//   - HandleColorPickerNotification: a slider moved (0xd) puts its value in its box; a box typed
//     into (0xd) or finished (6) puts its number in its slider, more than 255 written back as 255
//     (UpdateSlidersFromEditBoxes); a swatch clicked (1) is the color; Accept reads the boxes into
//     the sliders and gives the color to whoever asked (its 0x140's WndNotification 0x1e:
//     HandleContextMenuEvent sets the window's BGTint), then the window goes.

#include <cstdint>
#include <functional>
#include <string>

class WindowManager;

class ColorPicker
{
public:
	static constexpr const char *Screen = "ColorPickerWnd";

	void Attach(WindowManager &windows); // after the window's added
	// ColorPickerWnd_Activate: up with `color` (0xRRGGBB); `done` gets the one accepted.
	void Activate(uint32_t color, std::function<void(uint32_t)> done);
	// A button let go of in a window; true when it was this window's.
	bool Click(const std::string &window, const std::string &control);
	// Each frame: the sliders and the boxes kept with each other and the preview.
	void Update();

private:
	void SetColor(uint32_t color); // ColorPickerWnd_SetCurrentcolor
	uint32_t Color() const { return m_rgb[0] << 16 | m_rgb[1] << 8 | m_rgb[2]; }

	WindowManager *m_windows = nullptr;
	std::function<void(uint32_t)> m_done; // 0x140: who asked
	bool m_active = false;                // 0x134
	uint32_t m_rgb[3] = {255, 255, 255};  // 0x184, 0x190, 0x19c
	std::string m_typed[3];               // the boxes as last seen
	int m_slid[3] = {255, 255, 255};      // the sliders as last seen
};
