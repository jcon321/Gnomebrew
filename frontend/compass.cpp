#include "compass.h"

#include "window_manager.h"

#include <cstdlib>

namespace
{
	// __ftol: toward 0.
	int Ftol(double v) { return static_cast<int>(v); }
}

void CompassWindow::Activate(int sense_heading)
{
	PickVariation(sense_heading);
}

void CompassWindow::PickVariation(int sense_heading)
{
	if (sense_heading > 199) {
		m_variation = 0;
		return;
	}
	int span = (200 - sense_heading) * 0x200 / 200;
	// MSVC's rand(): 15 bits of a linear congruential generator.
	m_seed = m_seed * 214013u + 2531011u;
	int r = static_cast<int>((m_seed >> 16) & 0x7fff);
	int x = span > 0 ? r % span - span / 2 : 0;
	int least = 0x33 - sense_heading / 5;
	m_variation = std::abs(x) < least ? (x < 0 ? -least : least) : x;
}

void CompassWindow::RotationSpeed()
{
	m_speed = 80.0;
	if (std::abs(m_variation - m_needle) > 50 || m_sensed_ms > 0)
		m_speed = 160.0;
	if (m_variation < m_needle)
		m_speed = -m_speed;
}

void CompassWindow::SenseHeading()
{
	m_needle = 0;
	m_sensed_ms = 5000;
}

void CompassWindow::Frame(WindowManager &windows, float heading, int sense_heading, uint64_t now_ms)
{
	if (windows.Shown("CompassWindow")) {
		if (m_sensed_ms < 1) {
			if (sense_heading < 200) {
				int step = Ftol(static_cast<double>(static_cast<int>(now_ms - m_last_ms)) * m_speed * 0.001);
				if (std::abs(m_variation - m_needle) < step) {
					m_needle = m_variation;
					PickVariation(sense_heading);
				}
				else {
					m_needle += step;
					if (m_needle > 0x100)
						m_needle -= 0x200;
					else if (m_needle < -0x100)
						m_needle += 0x200;
				}
				RotationSpeed();
			}
			else
				m_needle = 0;
		}
		int at = (Ftol(heading) + m_needle) % 0x200;
		int x = Ftol(at * 0.3515625 - 145.0) + 0x2f;
		if (x > 0)
			x -= static_cast<int>((static_cast<unsigned>(x) + 0xb3) / 0xb4) * 0xb4;
		if (SidlWindow *wnd = windows.Find("CompassWindow")) {
			wnd->Reposition("CompassStrip1", x, 0);
			wnd->Reposition("CompassStrip2", x + 0xb4, 0);
		}
	}
	m_sensed_ms -= static_cast<int64_t>(now_ms - m_last_ms);
	m_last_ms = now_ms;
}
