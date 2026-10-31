#pragma once

#include <cstdint>

class WindowManager;

// The compass window (eqgame's CCompassWnd, EQUI_CompassWnd.xml): two copies of A_CompassStrip
// side by side, slid under A_CompassOverlay by our heading.
//   - UpdateCompassAndBuffWindows (@0041564d), each frame while it's up: the strip's offset is
//     ftol(((heading + needle) % 512) * 0.3515625 - 145) + 47 (180 pixels for 512 heading units),
//     brought into (-180, 0] when it's past 0; CompassStrip1 goes there, CompassStrip2 180 to its
//     right, in the window's client area.
//   - The needle wanders with a Sense Heading under 200: it swings toward a variation
//     (CalculateCompassNeedleVariation @004157ca: rand() % span - span / 2, span
//     (200 - skill) * 512 / 200, but at least 51 - skill / 5 either way) at 80 heading units a
//     second, 160 when it's more than 50 off (UpdateCompassRotationSpeed @0041584e); arriving
//     from below, it settles there and picks another. It's kept within -256..256.
//   - Using Sense Heading (SenseHeading @004158a8, from EQ_Character::UseSkill) points it true for
//     5 s.
class CompassWindow
{
public:
	// CCompassWnd::Activate: shown, with a variation picked to start.
	void Activate(int sense_heading);
	// UpdateCompassAndBuffWindows: the strips' place for this frame. `sense_heading` is our skill,
	// `heading` 0..512.
	void Frame(WindowManager &windows, float heading, int sense_heading, uint64_t now_ms);
	void SenseHeading(); // EQ_Character::UseSkill's: true for 5 s

private:
	void PickVariation(int sense_heading); // CalculateCompassNeedleVariation
	void RotationSpeed();                  // UpdateCompassRotationSpeed

	int m_variation = 0;       // 0x140: where the needle is swinging to
	int m_needle = 0;          // 0x144: how far off it is now, heading units
	uint64_t m_last_ms = 0;    // 0x148
	double m_speed = 0;        // 0x150: heading units a second, toward m_variation
	int64_t m_sensed_ms = 0;   // 0x158: time left pointing true
	uint32_t m_seed = 1;
};
