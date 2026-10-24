#include "window_manager.h"
#include "ini_file.h"

#include <algorithm>

namespace
{
	// SIDL screens are laid out for 640x480 (CSidlScreenWnd::InitializeScreen's starting resolution).
	constexpr int BaseWidth = 640, BaseHeight = 480;
	// ProcessMouseEvent: how long the left button is down before it's a hold (0x2ee).
	constexpr uint64_t HoldMs = 750;

	// CSidlScreenWnd_ConvertToRes (@005702a0): a window's position along one axis at another
	// resolution. Windows on the far half keep their distance from that edge, ones across the
	// middle stay centered, and everything stays on screen.
	int ConvertToRes(int pos, int size, int from, int to)
	{
		if (from != to) {
			if (pos > 0) {
				int end = pos + size;
				if (end < from) {
					int half = from / 2;
					if (half < pos)
						pos = pos - from + to;
					else if (half < end)
						pos = to / 2 - half + pos;
				}
				else
					pos = to - size;
			}
			else
				pos = 0;
		}
		if (pos < 0)
			pos = 0;
		if (to < pos + size)
			pos = to - size;
		return pos;
	}

	std::string ResolutionKey(const char *name, int width, int height)
	{
		return std::string(name) + std::to_string(width) + "x" + std::to_string(height);
	}
}

void WindowManager::Reset(const SidlDefinitions *defs, SidlTextures *textures, int screen_width, int screen_height)
{
	m_defs = defs;
	m_textures = textures;
	m_width = screen_width;
	m_height = screen_height;
	m_windows.clear();
	m_clicks.clear();
	m_holds.clear();
	m_right_clicks.clear();
	m_right_holds.clear();
	m_right_releases.clear();
	m_right_press = {};
	m_right_down = m_right_held = false;
	m_held = false;
	m_changed = false;
	m_drag = Drag::None;
	m_drag_window = m_press = -1;
	m_box_down = SidlWindow::TitleBox::None;
}

SidlWindow *WindowManager::Add(const std::string &screen, bool shown, std::string *error)
{
	if (!m_defs)
		return nullptr;
	auto w = std::make_unique<Window>();
	std::string build_error;
	if (!w->wnd.Build(*m_defs, screen, build_error)) {
		if (error)
			*error = build_error;
		return nullptr;
	}
	w->screen = screen;
	w->shown = shown;
	w->wnd.SetValues(m_values);
	Rect r = w->wnd.Def().rect;
	r.w = std::min(r.w, m_width);
	r.h = std::min(r.h, m_height);
	r.x = ConvertToRes(r.x, r.w, BaseWidth, m_width);
	r.y = ConvertToRes(r.y, r.h, BaseHeight, m_height);
	Place(*w, r);
	m_windows.push_back(std::move(w));
	return &m_windows.back()->wnd;
}

void WindowManager::SetValues(SidlValues *values)
{
	m_values = values;
	for (auto &w : m_windows)
		w->wnd.SetValues(values);
}

SidlWindow *WindowManager::Find(const std::string &screen)
{
	for (auto &w : m_windows)
		if (w->screen == screen)
			return &w->wnd;
	return nullptr;
}

void WindowManager::Place(Window &w, const Rect &rect)
{
	w.rect = rect;
	w.wnd.Move(rect);
	if (w.wnd.Minimized()) {
		// Just the title bar inside the border.
		int left, top, right, bottom;
		w.wnd.BorderInsets(left, top, right, bottom);
		w.wnd.Move({rect.x, rect.y, rect.w, top + w.wnd.Root().title.h + bottom});
	}
}

void WindowManager::SetMinimized(Window &w, bool minimized)
{
	w.wnd.SetMinimized(minimized);
	Place(w, w.rect);
}

Rect WindowManager::Clamped(const Rect &rect) const
{
	Rect r = rect;
	r.x = std::max(0, std::min(r.x, m_width - r.w));
	r.y = std::max(0, std::min(r.y, m_height - r.h));
	return r;
}

void WindowManager::Load(const IniFile &ini)
{
	for (auto &wp : m_windows) {
		Window &w = *wp;
		const std::string &section = w.screen;
		Settings &s = w.settings;
		s.fades = ini.GetBool(section, "Fades", s.fades);
		s.delay = ini.GetInt(section, "Delay", s.delay);
		s.duration = ini.GetInt(section, "Duration", s.duration);
		s.alpha = std::clamp(ini.GetInt(section, "Alpha", s.alpha), 0, 255);
		s.fade_to = std::clamp(ini.GetInt(section, "FadeToAlpha", s.fade_to), 0, 255);
		s.locked = ini.GetBool(section, "Locked", s.locked);
		s.tint = static_cast<uint32_t>(std::clamp(ini.GetInt(section, "BGTint.red", 255), 0, 255)) << 16 |
		         static_cast<uint32_t>(std::clamp(ini.GetInt(section, "BGTint.green", 255), 0, 255)) << 8 |
		         static_cast<uint32_t>(std::clamp(ini.GetInt(section, "BGTint.blue", 255), 0, 255));
		w.alpha = w.fade_target = s.alpha;

		// LoadIniInfo: this resolution's position if there is one, kept at least 4 pixels on
		// screen; the size for sizable windows, no bigger than the screen.
		Rect r = w.rect;
		r.x = ini.GetInt(section, ResolutionKey("XPos", m_width, m_height), r.x);
		r.y = ini.GetInt(section, ResolutionKey("YPos", m_width, m_height), r.y);
		if (m_width - r.x < 4)
			r.x = m_width - 4;
		if (m_height - r.y < 4)
			r.y = m_height - 4;
		if (w.wnd.Def().sizable) {
			r.w = std::min(ini.GetInt(section, "Width", r.w), m_width);
			r.h = std::min(ini.GetInt(section, "Height", r.h), m_height);
		}
		Place(w, r);
		if (ini.Has(section, "Show"))
			w.shown = ini.GetInt(section, "Show", 0) != 0;
	}
}

void WindowManager::Store(IniFile &ini) const
{
	for (const auto &wp : m_windows) {
		const Window &w = *wp;
		const std::string &section = w.screen;
		const Settings &s = w.settings;
		ini.SetInt(section, ResolutionKey("XPos", m_width, m_height), w.rect.x);
		ini.SetInt(section, ResolutionKey("YPos", m_width, m_height), w.rect.y);
		if (w.wnd.Def().sizable) {
			ini.SetInt(section, "Width", w.rect.w);
			ini.SetInt(section, "Height", w.rect.h);
		}
		ini.SetInt(section, "BGTint.red", static_cast<int>((s.tint >> 16) & 0xff));
		ini.SetInt(section, "BGTint.green", static_cast<int>((s.tint >> 8) & 0xff));
		ini.SetInt(section, "BGTint.blue", static_cast<int>(s.tint & 0xff));
		ini.SetBool(section, "Fades", s.fades);
		ini.SetInt(section, "Delay", s.delay);
		ini.SetInt(section, "Duration", s.duration);
		ini.SetInt(section, "Alpha", s.alpha);
		ini.SetInt(section, "FadeToAlpha", s.fade_to);
		ini.SetBool(section, "Locked", s.locked);
		ini.SetInt(section, "Show", w.shown ? 1 : 0);
	}
}

bool WindowManager::TakeChanged()
{
	bool changed = m_changed;
	m_changed = false;
	return changed;
}

void WindowManager::Show(const std::string &screen, bool shown)
{
	for (int i = 0; i < static_cast<int>(m_windows.size()); ++i) {
		Window &w = *m_windows[i];
		if (w.screen != screen || w.shown == shown)
			continue;
		w.shown = shown;
		m_changed = true;
		if (shown) {
			// OnWindowShown: on top, unfaded.
			w.unfaded = true;
			w.hover_ms = 0;
			w.alpha = w.fade_target = w.settings.alpha;
			w.fade_ms = 0;
			BringToTop(i);
		}
		else if (m_drag_window == i || m_press == i) {
			m_drag = Drag::None;
			m_drag_window = m_press = -1;
			m_box_down = SidlWindow::TitleBox::None;
		}
		return;
	}
}

void WindowManager::MoveTo(const std::string &screen, int x, int y)
{
	for (auto &w : m_windows)
		if (w->screen == screen) {
			Place(*w, {x, y, w->rect.w, w->rect.h});
			m_changed = true;
			return;
		}
}

bool WindowManager::Shown(const std::string &screen) const
{
	for (const auto &w : m_windows)
		if (w->screen == screen)
			return w->shown;
	return false;
}

int WindowManager::TopAt(int x, int y) const
{
	for (int i = static_cast<int>(m_windows.size()) - 1; i >= 0; --i)
		if (m_windows[i]->shown && m_windows[i]->wnd.Bounds().Contains(x, y))
			return i;
	return -1;
}

bool WindowManager::Over(int x, int y) const
{
	return TopAt(x, y) >= 0;
}

std::string WindowManager::ScreenAt(int x, int y) const
{
	int top = TopAt(x, y);
	return top >= 0 ? m_windows[top]->screen : "";
}

void WindowManager::Raise(const std::string &screen)
{
	for (int i = 0; i < static_cast<int>(m_windows.size()); ++i)
		if (m_windows[i]->screen == screen && m_windows[i]->shown) {
			BringToTop(i);
			return;
		}
}

void WindowManager::BringToTop(int &index)
{
	if (index < 0 || index == static_cast<int>(m_windows.size()) - 1)
		return;
	int last = static_cast<int>(m_windows.size()) - 1;
	auto moved = [&](int i) { return i == index ? last : i > index ? i - 1 : i; };
	if (m_press >= 0)
		m_press = moved(m_press);
	if (m_drag_window >= 0)
		m_drag_window = moved(m_drag_window);
	auto window = std::move(m_windows[index]);
	m_windows.erase(m_windows.begin() + index);
	m_windows.push_back(std::move(window));
	index = last;
}

bool WindowManager::OnMouse(const InputEvent &event, uint64_t now_ms)
{
	using Type = InputEvent::Type;
	using Box = SidlWindow::TitleBox;
	InputEvent away = event; // for the windows the mouse isn't over: nothing's hovered
	away.x = away.y = -100000;

	switch (event.type) {
	case Type::MouseMove: {
		if (m_drag != Drag::None && m_drag_window >= 0) {
			Window &w = *m_windows[m_drag_window];
			int dx = event.x - m_grab_x, dy = event.y - m_grab_y;
			Rect r = m_grab_rect;
			if (m_drag == Drag::Move) {
				r.x += dx;
				r.y += dy;
				r = Clamped(r);
			}
			else {
				// GetMinSize: room for the border, the title bar and its boxes.
				int left, top, right, bottom;
				w.wnd.BorderInsets(left, top, right, bottom);
				int min_w = left + right + 40, min_h = top + bottom + w.wnd.Root().title.h + 16;
				int x0 = r.x, y0 = r.y, x1 = r.Right(), y1 = r.Bottom();
				if (m_edge_left)
					x0 = std::clamp(x0 + dx, 0, x1 - min_w);
				if (m_edge_right)
					x1 = std::clamp(x1 + dx, x0 + min_w, m_width);
				if (m_edge_top)
					y0 = std::clamp(y0 + dy, 0, y1 - min_h);
				if (m_edge_bottom)
					y1 = std::clamp(y1 + dy, y0 + min_h, m_height);
				r = {x0, y0, x1 - x0, y1 - y0};
			}
			Place(w, r);
			w.hover_ms = now_ms;
			return true;
		}
		int top = m_press >= 0 ? m_press : TopAt(event.x, event.y);
		for (int i = 0; i < static_cast<int>(m_windows.size()); ++i) {
			Window &w = *m_windows[i];
			if (!w.shown)
				continue;
			w.wnd.OnMouse(i == top ? event : away);
			Box hover = i == top ? w.wnd.TitleBoxAt(event.x, event.y) : Box::None;
			w.wnd.SetTitleBoxes(hover, i == m_press ? m_box_down : Box::None);
		}
		return top >= 0;
	}
	case Type::MouseDown:
	case Type::DoubleClick: {
		int top = TopAt(event.x, event.y);
		if (top < 0)
			return false;
		BringToTop(top);
		Window &w = *m_windows[top];
		Box box = w.wnd.TitleBoxAt(event.x, event.y);
		if (box != Box::None) {
			m_press = top;
			m_box_down = box;
			w.wnd.SetTitleBoxes(box, box);
			return true;
		}
		if (w.wnd.InteractiveAt(event.x, event.y)) {
			m_press = top;
			m_press_ms = now_ms;
			m_held = false;
			w.wnd.OnMouse(event);
			return true;
		}
		if (w.settings.locked)
			return true;
		bool sizable = w.wnd.Def().sizable && !w.wnd.Minimized();
		Drag drag = Drag::None;
		if (sizable && ResizeEdges(w, event.x, event.y, m_edge_left, m_edge_top, m_edge_right, m_edge_bottom))
			drag = Drag::Resize;
		else if (!sizable || w.wnd.Root().title.Contains(event.x, event.y))
			drag = Drag::Move;
		if (drag != Drag::None) {
			m_drag = drag;
			m_drag_window = top;
			m_grab_x = event.x;
			m_grab_y = event.y;
			m_grab_rect = w.rect;
		}
		return true;
	}
	case Type::MouseUp: {
		if (m_drag != Drag::None) {
			m_drag = Drag::None;
			m_drag_window = -1;
			m_changed = true;
			return true;
		}
		if (m_press >= 0) {
			Window &w = *m_windows[m_press];
			if (m_box_down != Box::None) {
				if (w.wnd.TitleBoxAt(event.x, event.y) == m_box_down) {
					if (m_box_down == Box::Close)
						Show(w.screen, false);
					else {
						SetMinimized(w, !w.wnd.Minimized());
						m_changed = true;
					}
				}
			}
			else {
				std::string clicked = w.wnd.OnMouse(event);
				if (!clicked.empty() && !m_held)
					m_clicks.push_back({w.screen, clicked, event.alt, event.shift, event.ctrl});
			}
			w.wnd.SetTitleBoxes(w.wnd.TitleBoxAt(event.x, event.y), Box::None);
			m_press = -1;
			m_box_down = Box::None;
			return true;
		}
		return false; // the press wasn't ours: whoever had it gets the release
	}
	case Type::Wheel: {
		int top = TopAt(event.x, event.y);
		if (top < 0)
			return false;
		m_windows[top]->wnd.OnWheel(event.x, event.y, event.wheel);
		return true;
	}
	case Type::RightDown:
	case Type::RightUp: {
		// No context menus yet, but the world doesn't get them either.
		int top = TopAt(event.x, event.y);
		Click at;
		if (top >= 0)
			at = {m_windows[top]->screen, m_windows[top]->wnd.ButtonAt(event.x, event.y)};
		if (event.type == Type::RightDown) {
			m_right_press = at;
			m_right_ms = now_ms;
			m_right_down = true;
			m_right_held = false;
		}
		else {
			bool same = !at.control.empty() && at.window == m_right_press.window && at.control == m_right_press.control;
			if (same)
				(m_right_held ? m_right_releases : m_right_clicks).push_back(at);
			m_right_press = {};
			m_right_down = m_right_held = false;
		}
		return top >= 0;
	}
	default:
		return false;
	}
}

bool WindowManager::ResizeEdges(const Window &w, int x, int y, bool &left, bool &top, bool &right, bool &bottom) const
{
	// CXWnd::HitTestUIElement: on a Style_Sizable window, each of the border's pieces, as drawn,
	// sizes the sides it's on; the corner pieces (and the side pieces next to them) two at once.
	left = top = right = bottom = false;
	switch (w.wnd.BorderPieceAt(x, y)) {
	case TopLeft: case LeftTop: left = top = true; break;
	case Top: top = true; break;
	case TopRight: case RightTop: right = top = true; break;
	case Right: right = true; break;
	case RightBottom: case BottomRight: right = bottom = true; break;
	case Bottom: bottom = true; break;
	case BottomLeft: case LeftBottom: left = bottom = true; break;
	case Left: left = true; break;
	default: return false;
	}
	return true;
}

const char *WindowManager::Cursor(int x, int y) const
{
	// CXWndManager's cursors: a window being moved, or being sized or ready to be (the mouse on
	// a sizable, unlocked window's border).
	bool left, top, right, bottom;
	if (m_drag == Drag::Move)
		return "A_CursorDrag";
	if (m_drag == Drag::Resize) {
		left = m_edge_left;
		top = m_edge_top;
		right = m_edge_right;
		bottom = m_edge_bottom;
	}
	else {
		int i = m_press >= 0 ? -1 : TopAt(x, y);
		if (i < 0)
			return nullptr;
		const Window &w = *m_windows[i];
		if (!w.wnd.Def().sizable || w.wnd.Minimized() || w.settings.locked || w.wnd.TitleBoxAt(x, y) != SidlWindow::TitleBox::None ||
		    !ResizeEdges(w, x, y, left, top, right, bottom))
			return nullptr;
	}
	if ((left && top) || (right && bottom))
		return "A_CursorResizeNWSE";
	if ((right && top) || (left && bottom))
		return "A_CursorResizeNESW";
	return left || right ? "A_CursorResizeEW" : "A_CursorResizeNS";
}

bool WindowManager::DrawCursor(Canvas &canvas, int x, int y, uint64_t now_ms) const
{
	const char *name = m_defs && m_textures ? Cursor(x, y) : nullptr;
	const SidlAnimation *animation = name ? m_defs->Animation(name) : nullptr;
	const SidlFrame *frame = animation ? animation->FrameAt(now_ms) : nullptr;
	const Image *image = frame ? m_textures->Get(frame->texture) : nullptr;
	if (!image)
		return false;
	canvas.Stretch(*image, frame->source, {x - frame->hotspot_x, y - frame->hotspot_y, frame->source.w, frame->source.h});
	return true;
}

bool WindowManager::DrawAnimation(Canvas &canvas, const std::string &name, const Rect &dest, uint64_t now_ms) const
{
	const SidlAnimation *animation = m_defs && m_textures ? m_defs->Animation(name) : nullptr;
	const SidlFrame *frame = animation ? animation->FrameAt(now_ms) : nullptr;
	const Image *image = frame ? m_textures->Get(frame->texture) : nullptr;
	if (!image)
		return false;
	canvas.Stretch(*image, frame->source, dest);
	return true;
}

bool WindowManager::DrawCell(Canvas &canvas, const std::string &name, int cell, const Rect &dest) const
{
	const SidlAnimation *animation = m_defs && m_textures ? m_defs->Animation(name) : nullptr;
	SidlFrame frame;
	const Image *image = animation && animation->Cell(cell, frame) ? m_textures->Get(frame.texture) : nullptr;
	if (!image)
		return false;
	canvas.Stretch(*image, frame.source, dest);
	return true;
}

std::vector<WindowManager::Click> WindowManager::TakeClicks()
{
	std::vector<Click> clicks;
	clicks.swap(m_clicks);
	return clicks;
}

std::vector<WindowManager::Click> WindowManager::TakeHolds()
{
	std::vector<Click> holds;
	holds.swap(m_holds);
	return holds;
}

std::vector<WindowManager::Click> WindowManager::TakeRightClicks()
{
	std::vector<Click> clicks;
	clicks.swap(m_right_clicks);
	return clicks;
}

std::vector<WindowManager::Click> WindowManager::TakeRightHolds()
{
	std::vector<Click> holds;
	holds.swap(m_right_holds);
	return holds;
}

std::vector<WindowManager::Click> WindowManager::TakeRightReleases()
{
	std::vector<Click> releases;
	releases.swap(m_right_releases);
	return releases;
}

void WindowManager::Update(uint64_t now_ms, int mouse_x, int mouse_y)
{
	// The left button held: the button under the mouse now (if it's still on one) is held, and
	// the one pressed comes back up without a click.
	if (m_press >= 0 && m_box_down == SidlWindow::TitleBox::None && !m_held && now_ms - m_press_ms > HoldMs) {
		m_held = true;
		m_windows[m_press]->wnd.CancelPress();
		int top = TopAt(mouse_x, mouse_y);
		std::string held = top >= 0 ? m_windows[top]->wnd.ButtonAt(mouse_x, mouse_y) : "";
		if (!held.empty())
			m_holds.push_back({m_windows[top]->screen, held});
	}
	// The right button held (ProcessMouseEvent's 750 ms): the button it went down on, if it's
	// still under the mouse.
	if (m_right_down && !m_right_held && now_ms - m_right_ms > HoldMs) {
		m_right_held = true;
		int top = TopAt(mouse_x, mouse_y);
		if (top >= 0 && !m_right_press.control.empty() && m_windows[top]->screen == m_right_press.window &&
			m_windows[top]->wnd.ButtonAt(mouse_x, mouse_y) == m_right_press.control)
			m_right_holds.push_back(m_right_press);
	}
	for (int i = 0; i < static_cast<int>(m_windows.size()); ++i) {
		Window &w = *m_windows[i];
		if (!w.shown || !w.settings.fades)
			continue;
		// CXWnd::OnProcessFrame: the mouse over it keeps it unfaded; Delay ms away fades it.
		bool held = (m_drag != Drag::None && m_drag_window == i) || m_press == i;
		if (held || w.hover_ms == 0 || w.wnd.Bounds().Contains(mouse_x, mouse_y)) // 0: just shown
			w.hover_ms = now_ms;
		bool unfaded = now_ms - w.hover_ms <= static_cast<uint64_t>(std::max(w.settings.delay, 0));
		if (unfaded != w.unfaded) {
			// StartFade, from wherever it is now.
			w.unfaded = unfaded;
			w.fade_from = w.alpha;
			w.fade_target = unfaded ? w.settings.alpha : w.settings.fade_to;
			w.fade_start_ms = now_ms;
			w.fade_ms = std::max(w.settings.duration, 0);
		}
		// ProcessTransition 4: linear over the duration.
		if (w.alpha != w.fade_target) {
			uint64_t t = now_ms - w.fade_start_ms;
			if (w.fade_ms <= 0 || t > static_cast<uint64_t>(w.fade_ms))
				w.alpha = w.fade_target;
			else
				w.alpha = w.fade_from + static_cast<int>((w.fade_target - w.fade_from) * static_cast<int64_t>(t) / w.fade_ms);
		}
	}
}

int WindowManager::EffectiveAlpha(const Window &w) const
{
	// DoAllDrawing: the window's Alpha, scaled by where its fade is.
	int alpha = w.settings.alpha;
	if (w.settings.fades)
		alpha = w.alpha * alpha / 255;
	return alpha;
}

void WindowManager::Draw(Canvas &canvas, TextRenderer &text, uint64_t now_ms)
{
	if (!m_textures)
		return;
	for (const auto &wp : m_windows) {
		const Window &w = *wp;
		if (!w.shown)
			continue;
		int alpha = EffectiveAlpha(w);
		if (alpha <= 0)
			continue;
		const Rect &bounds = w.wnd.Bounds();
		if (alpha < 255)
			canvas.Keep(bounds, m_kept);
		w.wnd.Draw(canvas, text, *m_textures, now_ms);
		if (alpha < 255)
			canvas.BlendKept(bounds, m_kept, alpha);
	}
}
