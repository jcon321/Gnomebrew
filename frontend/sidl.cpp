#include "sidl.h"
#include "tga.h"
#include "xml.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace
{
	const char *const BorderNames[BorderPieceCount] = {"TopLeft", "Top", "TopRight", "RightTop", "Right", "RightBottom",
		"BottomRight", "Bottom", "BottomLeft", "LeftTop", "Left", "LeftBottom"};

	bool ReadFile(const std::string &path, std::string &out)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
			return false;
		out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
		return true;
	}

	Rect ReadRect(const XmlNode &node)
	{
		Rect r;
		if (const XmlNode *location = node.Child("Location")) {
			r.x = location->ChildInt("X", 0);
			r.y = location->ChildInt("Y", 0);
		}
		if (const XmlNode *size = node.Child("Size")) {
			r.w = size->ChildInt("CX", 0);
			r.h = size->ChildInt("CY", 0);
		}
		return r;
	}

	uint32_t ReadColor(const XmlNode &node)
	{
		return (static_cast<uint32_t>(node.ChildInt("R", 0) & 0xff) << 16) | (static_cast<uint32_t>(node.ChildInt("G", 0) & 0xff) << 8) |
		       static_cast<uint32_t>(node.ChildInt("B", 0) & 0xff);
	}

	SidlButtonTemplate ReadButtonTemplate(const XmlNode &node)
	{
		SidlButtonTemplate t;
		t.normal = node.ChildText("Normal");
		t.pressed = node.ChildText("Pressed");
		t.flyby = node.ChildText("Flyby");
		t.disabled = node.ChildText("Disabled");
		t.pressed_flyby = node.ChildText("PressedFlyby");
		return t;
	}

	Rect Intersect(const Rect &a, const Rect &b)
	{
		int x0 = std::max(a.x, b.x), y0 = std::max(a.y, b.y);
		int x1 = std::min(a.Right(), b.Right()), y1 = std::min(a.Bottom(), b.Bottom());
		return {x0, y0, std::max(x1 - x0, 0), std::max(y1 - y0, 0)};
	}

	std::string ToLowerCopy(std::string s)
	{
		for (auto &c : s)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return s;
	}
}

// ---------------------------------------------------------------- animations

const SidlFrame *SidlAnimation::FrameAt(uint64_t now_ms) const
{
	if (frames.empty())
		return nullptr;
	if (frames.size() == 1)
		return &frames[0];
	uint64_t total = 0;
	for (const auto &f : frames)
		total += static_cast<uint64_t>(std::max(f.duration, 1));
	uint64_t t = cycle ? now_ms % total : std::min<uint64_t>(now_ms, total - 1);
	for (const auto &f : frames) {
		uint64_t d = static_cast<uint64_t>(std::max(f.duration, 1));
		if (t < d)
			return &f;
		t -= d;
	}
	return &frames.back();
}

int SidlFontHeight(int font)
{
	static const int heights[] = {10, 12, 14, 15, 16, 20, 24};
	return font >= 0 && font < 7 ? heights[font] : 15;
}

// ---------------------------------------------------------------- textures

const Image *SidlTextures::Get(const std::string &name)
{
	std::string key = ToLowerCopy(name);
	auto cached = m_cache.find(key);
	if (cached != m_cache.end())
		return cached->second.get();

	// The skin files are mixed case and Linux is case sensitive, so index the folder once.
	if (!m_scanned) {
		m_scanned = true;
		std::error_code ec;
		for (const auto &entry : std::filesystem::directory_iterator(m_dir, ec))
			m_files[ToLowerCopy(entry.path().filename().string())] = entry.path().string();
	}

	std::unique_ptr<Image> image;
	auto file = m_files.find(key);
	std::string data;
	if (file != m_files.end() && ReadFile(file->second, data)) {
		std::vector<uint8_t> bytes(data.begin(), data.end());
		auto decoded = std::make_unique<Image>();
		bool ok = key.size() > 4 && key.compare(key.size() - 4, 4, ".bmp") == 0 ? DecodeBmp(bytes, *decoded) : DecodeTga(bytes, *decoded);
		if (ok)
			image = std::move(decoded);
	}
	if (!image)
		m_missing.push_back(name);
	const Image *result = image.get();
	m_cache[key] = std::move(image);
	return result;
}

// ---------------------------------------------------------------- definitions

bool SidlDefinitions::Load(const std::string &ui_dir, std::string &error)
{
	m_dir = ui_dir;
	m_animations.clear();
	m_window_templates.clear();
	m_pieces.clear();
	m_warnings.clear();

	std::string text;
	std::string equi = ui_dir + "/EQUI.xml";
	if (!ReadFile(equi, text)) {
		error = "Can't read " + equi;
		return false;
	}
	auto root = ParseXml(text, error);
	if (!root) {
		error = "EQUI.xml: " + error;
		return false;
	}
	// EQUI.xml is a list of <Composite><Include>file</Include>...</Composite>, in load order.
	std::vector<std::string> includes;
	std::function<void(const XmlNode &)> collect = [&](const XmlNode &node) {
		for (const auto &child : node.children) {
			if (child->name == "Include")
				includes.push_back(child->text);
			else
				collect(*child);
		}
	};
	collect(*root);

	for (const auto &file : includes)
		if (ToLowerCopy(file) != "sidl.xml")
			LoadFile(file);
	return true;
}

void SidlDefinitions::LoadFile(const std::string &file)
{
	std::string text;
	std::string path = m_dir + "/" + file;
	if (!ReadFile(path, text)) {
		m_warnings.push_back("missing " + file);
		return;
	}
	std::string error;
	auto root = ParseXml(text, error);
	if (!root) {
		m_warnings.push_back(file + ": " + error);
		return;
	}
	for (const auto &child : root->children)
		ReadElement(*child);
}

void SidlDefinitions::ReadElement(const XmlNode &node)
{
	std::string item = node.Attribute("item");
	if (item.empty())
		return;

	if (node.name == "Ui2DAnimation") {
		SidlAnimation animation;
		animation.cycle = node.ChildBool("Cycle", true);
		for (const XmlNode *f : node.Children("Frames")) {
			SidlFrame frame;
			frame.texture = f->ChildText("Texture");
			frame.source = ReadRect(*f);
			if (const XmlNode *hotspot = f->Child("Hotspot")) {
				frame.hotspot_x = hotspot->ChildInt("X", 0);
				frame.hotspot_y = hotspot->ChildInt("Y", 0);
			}
			frame.duration = f->ChildInt("Duration", 1000);
			animation.frames.push_back(frame);
		}
		m_animations[item] = std::move(animation);
		return;
	}

	if (node.name == "WindowDrawTemplate") {
		SidlWindowTemplate t;
		t.background = node.ChildText("Background");
		if (const XmlNode *border = node.Child("Border"))
			for (int i = 0; i < BorderPieceCount; ++i)
				t.border[i] = border->ChildText(BorderNames[i]);
		if (const XmlNode *title = node.Child("Titlebar")) {
			t.title_left = title->ChildText("Left");
			t.title_middle = title->ChildText("Middle");
			t.title_right = title->ChildText("Right");
		}
		if (const XmlNode *vsb = node.Child("VSBTemplate")) {
			if (const XmlNode *up = vsb->Child("UpButton"))
				t.vscroll.up = ReadButtonTemplate(*up);
			if (const XmlNode *down = vsb->Child("DownButton"))
				t.vscroll.down = ReadButtonTemplate(*down);
			if (const XmlNode *thumb = vsb->Child("Thumb")) {
				t.vscroll.thumb_top = thumb->ChildText("Top");
				t.vscroll.thumb_middle = thumb->ChildText("Middle");
				t.vscroll.thumb_bottom = thumb->ChildText("Bottom");
			}
			t.vscroll.gutter = vsb->ChildText("MiddleTextureInfo");
			if (const XmlNode *tint = vsb->Child("MiddleTint"))
				t.vscroll.gutter_tint = ReadColor(*tint);
		}
		if (const XmlNode *box = node.Child("CloseBox"))
			t.close_box = ReadButtonTemplate(*box);
		if (const XmlNode *box = node.Child("MinimizeBox"))
			t.minimize_box = ReadButtonTemplate(*box);
		m_window_templates[item] = std::move(t);
		return;
	}

	static const char *const PieceTypes[] = {"Screen", "Button", "Label", "StaticAnimation", "StaticText", "StaticFrame",
		"Editbox", "Listbox", "Gauge", "Slider", "Combobox", "Page", "TabBox", "LayoutBox", "StaticHeader", "STMLbox",
		"SpellGem", "InvSlot"};
	if (std::find(std::begin(PieceTypes), std::end(PieceTypes), node.name) == std::end(PieceTypes))
		return;

	SidlPiece p;
	p.type = node.name;
	p.item = item;
	p.screen_id = node.ChildText("ScreenID", item);
	p.eq_type = node.ChildInt("EQType", 0);
	p.font = node.ChildInt("Font", 3);
	p.relative = node.ChildBool("RelativePosition", true);
	p.rect = ReadRect(node);
	p.has_size = node.Child("Size") != nullptr;
	p.text = node.ChildText("Text");
	if (const XmlNode *color = node.Child("TextColor")) {
		p.text_color = ReadColor(*color);
		p.has_text_color = true;
	}
	p.auto_stretch = node.ChildBool("AutoStretch", false);
	p.left_to_left = node.ChildBool("LeftAnchorToLeft", true);
	p.top_to_top = node.ChildBool("TopAnchorToTop", true);
	p.right_to_left = node.ChildBool("RightAnchorToLeft", true);
	p.bottom_to_top = node.ChildBool("BottomAnchorToTop", true);
	p.left_offset = node.ChildInt("LeftAnchorOffset", 0);
	p.top_offset = node.ChildInt("TopAnchorOffset", 0);
	p.right_offset = node.ChildInt("RightAnchorOffset", 0);
	p.bottom_offset = node.ChildInt("BottomAnchorOffset", 0);
	p.transparent = node.ChildBool("Style_Transparent", false);
	p.titlebar = node.ChildBool("Style_Titlebar", false);
	p.closebox = node.ChildBool("Style_Closebox", false);
	p.minimizebox = node.ChildBool("Style_Minimizebox", false);
	p.sizable = node.ChildBool("Style_Sizable", false);
	p.align_center = node.ChildBool("AlignCenter", false);
	p.align_right = node.ChildBool("AlignRight", false);
	p.checkbox = node.ChildBool("Style_Checkbox", false);
	p.radio_group = node.ChildText("RadioGroup");
	if (const XmlNode *t = node.Child("ButtonDrawTemplate"))
		p.button = ReadButtonTemplate(*t);
	p.animation = node.ChildText("Animation");
	if (p.type == "InvSlot")
		p.slot_background = node.ChildText("Background");
	if (const XmlNode *t = node.Child("SpellGemDrawTemplate")) {
		p.gem_holder = t->ChildText("Holder");
		p.gem_background = t->ChildText("Background");
		p.gem_highlight = t->ChildText("Highlight");
	}
	p.text_offset_x = node.ChildInt("TextOffsetX", 0);
	p.text_offset_y = node.ChildInt("TextOffsetY", 0);
	p.gauge_offset_x = node.ChildInt("GaugeOffsetX", 0);
	p.gauge_offset_y = node.ChildInt("GaugeOffsetY", 16);
	if (const XmlNode *tint = node.Child("FillTint"))
		p.fill_tint = ReadColor(*tint);
	if (const XmlNode *tint = node.Child("LinesFillTint"))
		p.lines_fill_tint = ReadColor(*tint);
	p.draw_lines_fill = node.ChildBool("DrawLinesFill", false);
	if (const XmlNode *t = node.Child("GaugeDrawTemplate")) {
		p.gauge.background = t->ChildText("Background");
		p.gauge.fill = t->ChildText("Fill");
		p.gauge.lines = t->ChildText("Lines");
		p.gauge.lines_fill = t->ChildText("LinesFill");
		p.gauge.cap_left = t->ChildText("EndCapLeft");
		p.gauge.cap_right = t->ChildText("EndCapRight");
	}
	p.vscroll = node.ChildBool("Style_VScroll", false);
	p.draw_template = node.ChildText("DrawTemplate");
	p.style_border = node.ChildBool("Style_Border", false);
	for (const XmlNode *piece : node.Children("Pieces"))
		p.pieces.push_back(piece->text);
	m_pieces[item] = std::move(p);
}

const SidlAnimation *SidlDefinitions::Animation(const std::string &name) const
{
	auto it = m_animations.find(name);
	return it != m_animations.end() ? &it->second : nullptr;
}

const SidlWindowTemplate *SidlDefinitions::WindowTemplate(const std::string &name) const
{
	auto it = m_window_templates.find(name);
	return it != m_window_templates.end() ? &it->second : nullptr;
}

const SidlPiece *SidlDefinitions::Piece(const std::string &item) const
{
	auto it = m_pieces.find(item);
	return it != m_pieces.end() ? &it->second : nullptr;
}

// ---------------------------------------------------------------- windows

namespace
{
	// STML as plain text: <BR> breaks the line, other tags are dropped.
	std::string StmlToText(const std::string &stml)
	{
		std::string out;
		for (size_t i = 0; i < stml.size(); ++i) {
			if (stml[i] == '<') {
				size_t end = stml.find('>', i);
				if (end == std::string::npos)
					break;
				std::string tag = ToLowerCopy(stml.substr(i + 1, end - i - 1));
				if (tag == "br" || tag == "br/" || tag == "p")
					out += '\n';
				i = end;
			}
			else if (stml.compare(i, 4, "&lt;") == 0) {
				out += '<';
				i += 3;
			}
			else if (stml.compare(i, 4, "&gt;") == 0) {
				out += '>';
				i += 3;
			}
			else if (stml.compare(i, 5, "&amp;") == 0) {
				out += '&';
				i += 4;
			}
			else if (stml[i] != '\r')
				out += stml[i];
		}
		return out;
	}
}

bool SidlWindow::Build(const SidlDefinitions &defs, const std::string &screen_item, std::string &error)
{
	m_defs = &defs;
	m_controls.clear();
	m_focus = -1;
	m_scroll_control = m_scroll_hover = -1;
	m_scroll_part = m_scroll_hover_part = ScrollPart::None;
	m_minimized = false;
	m_box_hover = m_box_pressed = TitleBox::None;
	const SidlPiece *screen = defs.Piece(screen_item);
	if (!screen || screen->type != "Screen") {
		error = "SIDL screen '" + screen_item + "' not found";
		return false;
	}
	Add(*screen, -1);
	Place(0, screen->rect);
	return true;
}

int SidlWindow::Add(const SidlPiece &piece, int parent)
{
	int index = static_cast<int>(m_controls.size());
	Control control;
	control.def = &piece;
	control.parent = parent;
	control.text = piece.text;
	control.text_color = piece.text_color;
	if (!piece.draw_template.empty())
		control.frame = m_defs->WindowTemplate(piece.draw_template);
	m_controls.push_back(control);
	if (parent >= 0)
		m_controls[parent].children.push_back(index);
	if (piece.type == "Screen") {
		for (const auto &item : piece.pieces)
			if (const SidlPiece *child = m_defs->Piece(item))
				if (child != &piece)
					Add(*child, index);
	}
	return index;
}

void SidlWindow::Place(int index, const Rect &rect)
{
	Control &control = m_controls[index];
	control.rect = rect;

	// The client area sits inside the border pieces.
	int left = 0, top = 0, right = 0, bottom = 0;
	if (control.frame && control.def->style_border) {
		auto size_of = [&](int piece, bool width) {
			const SidlAnimation *a = m_defs->Animation(control.frame->border[piece]);
			const SidlFrame *f = a ? a->FrameAt(0) : nullptr;
			return f ? (width ? f->source.w : f->source.h) : 0;
		};
		left = size_of(Left, true);
		right = size_of(Right, true);
		top = size_of(Top, false);
		bottom = size_of(Bottom, false);
	}
	control.title = {};
	if (control.frame && control.def->titlebar) {
		const SidlAnimation *a = m_defs->Animation(control.frame->title_middle);
		const SidlFrame *f = a ? a->FrameAt(0) : nullptr;
		int height = f ? f->source.h : 0;
		control.title = {rect.x + left, rect.y + top, rect.w - left - right, height};
		top += height;
	}
	control.client = {rect.x + left, rect.y + top, rect.w - left - right, rect.h - top - bottom};

	const Rect parent = control.client;
	for (int child : control.children) {
		const SidlPiece &piece = *m_controls[child].def;
		Rect r = piece.rect;
		if (piece.auto_stretch) {
			int l = piece.left_to_left ? parent.x + piece.left_offset : parent.Right() - piece.left_offset;
			int t = piece.top_to_top ? parent.y + piece.top_offset : parent.Bottom() - piece.top_offset;
			int rr = piece.right_to_left ? parent.x + piece.right_offset : parent.Right() - piece.right_offset;
			int b = piece.bottom_to_top ? parent.y + piece.bottom_offset : parent.Bottom() - piece.bottom_offset;
			r = {l, t, rr - l, b - t};
		}
		else {
			if (!piece.has_size) {
				if (const SidlAnimation *a = m_defs->Animation(piece.animation))
					if (const SidlFrame *f = a->FrameAt(0)) {
						r.w = f->source.w;
						r.h = f->source.h;
					}
			}
			// RelativePosition false places it in screen coordinates.
			if (piece.relative) {
				r.x += parent.x;
				r.y += parent.y;
			}
		}
		Place(child, r);
	}
}

SidlWindow::Control *SidlWindow::Find(const std::string &screen_id)
{
	for (auto &control : m_controls)
		if (control.def->screen_id == screen_id)
			return &control;
	return nullptr;
}

const SidlWindow::Control *SidlWindow::Find(const std::string &screen_id) const
{
	for (const auto &control : m_controls)
		if (control.def->screen_id == screen_id)
			return &control;
	return nullptr;
}

void SidlWindow::Move(const std::string &screen_id, const Rect &rect)
{
	if (Control *control = Find(screen_id))
		Place(static_cast<int>(control - m_controls.data()), rect);
}

void SidlWindow::Focus(const std::string &screen_id)
{
	Control *control = Find(screen_id);
	m_focus = control ? static_cast<int>(control - m_controls.data()) : -1;
}

bool SidlWindow::Focused(const std::string &screen_id) const
{
	const Control *control = Find(screen_id);
	return control && m_focus == static_cast<int>(control - m_controls.data());
}

Rect SidlWindow::ContentRect(const Control &control) const
{
	Rect r = control.client;
	if (HasScrollbar(control))
		r.w -= LayoutScrollbar(control).bar.w;
	return r;
}

bool SidlWindow::Shown(const Control &control) const
{
	for (const Control *c = &control; c; c = c->parent >= 0 ? &m_controls[c->parent] : nullptr)
		if (!c->visible)
			return false;
	return true;
}

Rect SidlWindow::AnimationSize(const std::string &name) const
{
	const SidlAnimation *a = m_defs ? m_defs->Animation(name) : nullptr;
	const SidlFrame *f = a ? a->FrameAt(0) : nullptr;
	return f ? Rect{0, 0, f->source.w, f->source.h} : Rect{};
}

void SidlWindow::BorderInsets(int &left, int &top, int &right, int &bottom) const
{
	left = top = right = bottom = 0;
	if (m_controls.empty())
		return;
	const Control &root = m_controls.front();
	if (!root.frame || !root.def->style_border)
		return;
	left = AnimationSize(root.frame->border[Left]).w;
	right = AnimationSize(root.frame->border[Right]).w;
	top = AnimationSize(root.frame->border[Top]).h;
	bottom = AnimationSize(root.frame->border[Bottom]).h;
}

Rect SidlWindow::TitleBoxRect(TitleBox box) const
{
	if (m_controls.empty() || box == TitleBox::None)
		return {};
	const Control &root = m_controls.front();
	if (!root.frame || root.title.h <= 0)
		return {};
	// Right-aligned in the title bar, the close box last; each its Normal frame's size.
	const Rect &t = root.title;
	int right = t.Right() - 2;
	Rect close, minimize;
	if (root.def->closebox) {
		Rect size = AnimationSize(root.frame->close_box.normal);
		close = {right - size.w, t.y + (t.h - size.h) / 2, size.w, size.h};
		right = close.x - 1;
	}
	if (root.def->minimizebox) {
		Rect size = AnimationSize(root.frame->minimize_box.normal);
		minimize = {right - size.w, t.y + (t.h - size.h) / 2, size.w, size.h};
	}
	return box == TitleBox::Close ? close : minimize;
}

SidlWindow::TitleBox SidlWindow::TitleBoxAt(int x, int y) const
{
	for (TitleBox box : {TitleBox::Close, TitleBox::Minimize}) {
		Rect r = TitleBoxRect(box);
		if (r.w > 0 && r.Contains(x, y))
			return box;
	}
	return TitleBox::None;
}

bool SidlWindow::HitTest(const Control &control, int x, int y) const
{
	if (!control.rect.Contains(x, y) || !Shown(control))
		return false;
	for (int p = control.parent; p >= 0; p = m_controls[p].parent)
		if (!m_controls[p].client.Contains(x, y))
			return false;
	return true;
}

bool SidlWindow::InteractiveAt(int x, int y) const
{
	if (m_minimized)
		return false;
	for (const auto &control : m_controls) {
		const std::string &type = control.def->type;
		if ((type == "Button" || type == "Editbox") && control.enabled && HitTest(control, x, y))
			return true;
		if (HasScrollbar(control) && LayoutScrollbar(control).bar.Contains(x, y) && HitTest(control, x, y))
			return true;
	}
	return false;
}

void SidlWindow::Check(const std::string &screen_id)
{
	Control *target = Find(screen_id);
	if (!target)
		return;
	if (!target->def->radio_group.empty())
		for (auto &control : m_controls)
			if (control.def->radio_group == target->def->radio_group)
				control.checked = false;
	target->checked = true;
}

void SidlWindow::DrawAnimation(Canvas &canvas, SidlTextures &textures, const std::string &name, const Rect &dest,
	uint64_t now_ms) const
{
	const SidlAnimation *animation = m_defs->Animation(name);
	const SidlFrame *frame = animation ? animation->FrameAt(now_ms) : nullptr;
	const Image *image = frame ? textures.Get(frame->texture) : nullptr;
	if (image)
		canvas.Stretch(*image, frame->source, dest);
}

void SidlWindow::LayoutBorder(const Control &control, uint64_t now_ms, Rect (&pieces)[BorderPieceCount]) const
{
	auto frame_of = [&](int piece) -> const SidlFrame * {
		const SidlAnimation *a = m_defs->Animation(control.frame->border[piece]);
		return a ? a->FrameAt(now_ms) : nullptr;
	};
	auto w = [&](int piece) { const SidlFrame *f = frame_of(piece); return f ? f->source.w : 0; };
	auto h = [&](int piece) { const SidlFrame *f = frame_of(piece); return f ? f->source.h : 0; };
	// Corners at natural size; edges stretched between them.
	const Rect &r = control.rect;
	pieces[TopLeft] = {r.x, r.y, w(TopLeft), h(TopLeft)};
	pieces[TopRight] = {r.Right() - w(TopRight), r.y, w(TopRight), h(TopRight)};
	pieces[Top] = {r.x + w(TopLeft), r.y, r.w - w(TopLeft) - w(TopRight), h(Top)};
	pieces[BottomLeft] = {r.x, r.Bottom() - h(BottomLeft), w(BottomLeft), h(BottomLeft)};
	pieces[BottomRight] = {r.Right() - w(BottomRight), r.Bottom() - h(BottomRight), w(BottomRight), h(BottomRight)};
	pieces[Bottom] = {r.x + w(BottomLeft), r.Bottom() - h(Bottom), r.w - w(BottomLeft) - w(BottomRight), h(Bottom)};
	int left_top = r.y + h(TopLeft), left_bottom = r.Bottom() - h(BottomLeft);
	pieces[LeftTop] = {r.x, left_top, w(LeftTop), h(LeftTop)};
	pieces[LeftBottom] = {r.x, left_bottom - h(LeftBottom), w(LeftBottom), h(LeftBottom)};
	pieces[Left] = {r.x, left_top + h(LeftTop), w(Left), left_bottom - h(LeftBottom) - left_top - h(LeftTop)};
	int right_top = r.y + h(TopRight), right_bottom = r.Bottom() - h(BottomRight);
	pieces[RightTop] = {r.Right() - w(RightTop), right_top, w(RightTop), h(RightTop)};
	pieces[RightBottom] = {r.Right() - w(RightBottom), right_bottom - h(RightBottom), w(RightBottom), h(RightBottom)};
	pieces[Right] = {r.Right() - w(Right), right_top + h(RightTop), w(Right), right_bottom - h(RightBottom) - right_top - h(RightTop)};
}

int SidlWindow::BorderPieceAt(int x, int y) const
{
	if (m_controls.empty())
		return -1;
	const Control &root = m_controls.front();
	if (!root.frame || !root.def->style_border)
		return -1;
	Rect pieces[BorderPieceCount];
	LayoutBorder(root, 0, pieces);
	for (int i = 0; i < BorderPieceCount; ++i)
		if (pieces[i].w > 0 && pieces[i].h > 0 && pieces[i].Contains(x, y))
			return i;
	return -1;
}

void SidlWindow::DrawFrame(const Control &control, Canvas &canvas, SidlTextures &textures, uint64_t now_ms) const
{
	const SidlWindowTemplate *frame = control.frame;
	if (!frame)
		return;
	// Client background: the template texture tiled at its natural size.
	if (!control.def->transparent)
		if (const Image *background = textures.Get(frame->background))
			canvas.TileRegion(*background, {0, 0, background->width, background->height}, control.client);
	if (!control.def->style_border)
		return;
	Rect pieces[BorderPieceCount];
	LayoutBorder(control, now_ms, pieces);
	for (int i = 0; i < BorderPieceCount; ++i)
		if (pieces[i].w > 0 && pieces[i].h > 0)
			DrawAnimation(canvas, textures, frame->border[i], pieces[i], now_ms);
}

void SidlWindow::DrawTitleBox(Canvas &canvas, SidlTextures &textures, TitleBox box, const SidlButtonTemplate &look,
	uint64_t now_ms) const
{
	Rect r = TitleBoxRect(box);
	if (r.w <= 0)
		return;
	// CXWnd::DrawCloseButton: PressedFlyby (or Pressed) while held over it, Flyby under the mouse.
	bool hover = m_box_hover == box, pressed = m_box_pressed == box;
	std::string name = pressed && hover && !look.pressed_flyby.empty() ? look.pressed_flyby
	                   : pressed                                       ? look.pressed
	                   : hover                                         ? look.flyby
	                                                                   : look.normal;
	DrawAnimation(canvas, textures, name.empty() ? look.normal : name, r, now_ms);
}

void SidlWindow::Draw(Canvas &canvas, TextRenderer &text, SidlTextures &textures, uint64_t now_ms) const
{
	if (m_controls.empty())
		return;
	Rect clip = canvas.Clip();
	DrawControl(0, canvas, text, textures, now_ms, clip);
	canvas.SetClip(clip);
}

void SidlWindow::DrawControl(int index, Canvas &canvas, TextRenderer &text, SidlTextures &textures, uint64_t now_ms,
	const Rect &parent_clip) const
{
	const Control &control = m_controls[index];
	if (!control.visible)
		return;
	const SidlPiece &def = *control.def;
	int height = SidlFontHeight(def.font);
	bool minimized = index == 0 && m_minimized;
	// CXWnd::DoAllDrawing: the frame and title bar inside the control's rect, the rest inside its
	// client area, both cut to the parent's client area (GetScreenClipRect, GetClientClipRect).
	// Its children are cut to the same client area, so nothing spills out of its parent.
	Rect client_clip = Intersect(control.client, parent_clip);
	canvas.SetClip(Intersect(control.rect, parent_clip));
	DrawFrame(control, canvas, textures, now_ms);
	if (control.title.h > 0) {
		const Rect &t = control.title;
		auto width = [&](const std::string &name) {
			const SidlAnimation *a = m_defs->Animation(name);
			const SidlFrame *f = a ? a->FrameAt(now_ms) : nullptr;
			return f ? f->source.w : 0;
		};
		int lw = width(control.frame->title_left), rw = width(control.frame->title_right);
		DrawAnimation(canvas, textures, control.frame->title_middle, {t.x + lw, t.y, t.w - lw - rw, t.h}, now_ms);
		DrawAnimation(canvas, textures, control.frame->title_left, {t.x, t.y, lw, t.h}, now_ms);
		DrawAnimation(canvas, textures, control.frame->title_right, {t.Right() - rw, t.y, rw, t.h}, now_ms);
		if (!control.text.empty()) {
			TextSize size = text.Measure(control.text, 12);
			text.Draw(canvas, t.x + (t.w - size.w) / 2, t.y + (t.h - size.h) / 2, control.text, 12, control.text_color);
		}
		if (index == 0) {
			DrawTitleBox(canvas, textures, TitleBox::Minimize, control.frame->minimize_box, now_ms);
			DrawTitleBox(canvas, textures, TitleBox::Close, control.frame->close_box, now_ms);
		}
	}
	if (minimized)
		return;

	canvas.SetClip(client_clip);
	if (def.type == "StaticAnimation") {
		DrawAnimation(canvas, textures, def.animation, control.rect, now_ms);
	}
	else if (def.type == "Button") {
		const SidlButtonTemplate &t = def.button;
		bool down = control.checked || (control.pressed && control.hover);
		std::string name = !control.enabled ? t.disabled
		                   : down            ? (control.hover && !t.pressed_flyby.empty() ? t.pressed_flyby : t.pressed)
		                   : control.hover   ? t.flyby
		                                     : t.normal;
		if (name.empty())
			name = t.normal;
		if (!control.blank)
			DrawAnimation(canvas, textures, name, control.rect, now_ms);
		if (!control.text.empty()) {
			// CButtonWnd::Draw: grey when disabled, yellow when checked or under the mouse.
			TextSize size = text.Measure(control.text, height);
			uint32_t color = !control.enabled                  ? 0x808080
			                 : control.checked || control.hover ? 0xffff00
			                                                    : control.text_color;
			text.Draw(canvas, control.rect.x + (control.rect.w - size.w) / 2, control.rect.y + (control.rect.h - size.h) / 2,
				control.text, height, color);
		}
	}
	else if (def.type == "InvSlot") {
		// CInvSlotWnd with nothing in it: the slot's own picture.
		DrawAnimation(canvas, textures, def.slot_background, control.rect, now_ms);
	}
	else if (def.type == "SpellGem") {
		// CSpellGemWnd::Draw (@005a6050): a gem with a spell is the Holder tinted the spell's color,
		// the icon, then the Background ring over them; an empty one is only the Highlight piece
		// (0x0 in the default skin, so the window shows through).
		DrawAnimation(canvas, textures, def.gem_highlight, control.rect, now_ms);
	}
	else if (def.type == "Label" || def.type == "StaticText") {
		std::string shown = control.text;
		if (def.eq_type != 0 && m_values)
			m_values->Label(def.eq_type, shown);
		if (!shown.empty()) {
			TextSize size = text.Measure(shown, height);
			int x = control.rect.x;
			if (def.align_center)
				x += (control.rect.w - size.w) / 2;
			else if (def.align_right)
				x += control.rect.w - size.w;
			text.Draw(canvas, x, control.rect.y, shown, height, control.text_color);
		}
	}
	else if (def.type == "Gauge") {
		DrawGauge(control, canvas, text, textures, now_ms);
	}
	else if (def.type == "Editbox") {
		// The end of the text, as much as fits, with the caret after it.
		const Rect &c = control.client;
		std::string shown = control.text;
		TextSize size = text.Measure(shown.empty() ? "W" : shown, height);
		while (!shown.empty() && size.w > c.w - 6) {
			shown.erase(0, std::max<size_t>(1, shown.size() / 8));
			size = text.Measure(shown.empty() ? "W" : shown, height);
		}
		int y = c.y + (c.h - size.h) / 2;
		if (!shown.empty())
			text.Draw(canvas, c.x + 2, y, shown, height, control.text_color);
		if (index == m_focus && (now_ms / 500) % 2 == 0) {
			int x = c.x + 2 + (shown.empty() ? 0 : size.w);
			canvas.Fill({x, y, 1, size.h}, control.text_color);
		}
	}
	else if (def.type == "STMLbox" && control.draw_content) {
		control.draw_content(canvas, text, ContentRect(control));
	}
	else if (def.type == "STMLbox") {
		if (!control.text.empty()) {
			const Rect &c = control.client;
			text.DrawWrapped(canvas, {c.x + 2, c.y + 2, c.w - 4, c.h - 4}, StmlToText(control.text), height,
				def.has_text_color ? control.text_color : 0xffffff);
		}
	}

	if (HasScrollbar(control))
		DrawScrollbar(control, canvas, textures, now_ms);

	// Static animations go under the other pieces (the creation screen lists its city buttons
	// before the maps they sit on; the spellbook its page art before the spells); otherwise
	// pieces stack in the order they're listed.
	for (int pass = 0; pass < 2; ++pass)
		for (int child : control.children)
			if ((m_controls[child].def->type == "StaticAnimation") == (pass == 0))
				DrawControl(child, canvas, text, textures, now_ms, client_clip);
}

std::string SidlWindow::OnMouse(const InputEvent &event)
{
	// Scrollbars: the arrows step a row, the gutter a page, the thumb drags.
	m_scroll_hover = -1;
	m_scroll_hover_part = ScrollPart::None;
	for (int i = 0; i < static_cast<int>(m_controls.size()); ++i) {
		Control &control = m_controls[i];
		if (!HasScrollbar(control) || !Shown(control))
			continue;
		ScrollPart part = ScrollPartAt(control, event.x, event.y);
		if (part != ScrollPart::None) {
			m_scroll_hover = i;
			m_scroll_hover_part = part;
		}
		if ((event.type == InputEvent::Type::MouseDown || event.type == InputEvent::Type::DoubleClick) && part != ScrollPart::None) {
			m_scroll_control = i;
			m_scroll_part = part;
			int page = std::max(control.scroll_page, 1);
			switch (part) {
			case ScrollPart::Up: ScrollBy(control, -1); break;
			case ScrollPart::Down: ScrollBy(control, 1); break;
			case ScrollPart::PageUp: ScrollBy(control, -page); break;
			case ScrollPart::PageDown: ScrollBy(control, page); break;
			default:
				m_scroll_grab_y = event.y;
				m_scroll_grab_pos = control.scroll_pos;
				break;
			}
			return "";
		}
	}
	if (m_scroll_control >= 0) {
		Control &control = m_controls[m_scroll_control];
		if (event.type == InputEvent::Type::MouseMove && m_scroll_part == ScrollPart::Thumb) {
			ScrollLayout layout = LayoutScrollbar(control);
			int travel = layout.track.h - layout.thumb.h;
			if (travel > 0)
				control.scroll_pos = std::clamp(m_scroll_grab_pos + (event.y - m_scroll_grab_y) * control.scroll_max / travel, 0,
					control.scroll_max);
		}
		if (event.type == InputEvent::Type::MouseUp) {
			m_scroll_control = -1;
			m_scroll_part = ScrollPart::None;
		}
		return "";
	}

	std::string clicked;
	for (int i = 0; i < static_cast<int>(m_controls.size()); ++i) {
		Control &control = m_controls[i];
		const std::string &type = control.def->type;
		if ((type != "Button" && type != "Editbox") || !Shown(control))
			continue;
		bool inside = HitTest(control, event.x, event.y);
		if (type == "Editbox") {
			if (inside && event.type == InputEvent::Type::MouseDown && control.enabled)
				m_focus = i;
			continue;
		}
		switch (event.type) {
		case InputEvent::Type::MouseMove:
			control.hover = inside;
			break;
		case InputEvent::Type::MouseDown:
		case InputEvent::Type::DoubleClick:
			control.hover = inside;
			control.pressed = inside && control.enabled;
			break;
		case InputEvent::Type::MouseUp:
			if (control.pressed && inside && control.enabled) {
				clicked = control.def->screen_id;
				if (control.def->checkbox) {
					if (control.def->radio_group.empty())
						control.checked = !control.checked;
					else
						Check(control.def->screen_id);
				}
			}
			control.pressed = false;
			break;
		default:
			break;
		}
	}
	return clicked;
}

bool SidlWindow::OnKey(const InputEvent &event)
{
	if (m_focus < 0 || !Shown(m_controls[m_focus]) || !m_controls[m_focus].enabled)
		return false;
	Control &control = m_controls[m_focus];
	if (event.type == InputEvent::Type::Char) {
		if (event.ch >= 0x20 && event.ch < 0x7f && control.text.size() < control.max_chars)
			control.text += static_cast<char>(event.ch);
		return true;
	}
	if (event.type == InputEvent::Type::Key && event.key == InputEvent::Key::Backspace) {
		if (!control.text.empty())
			control.text.pop_back();
		return true;
	}
	return false;
}

bool SidlWindow::OnWheel(int x, int y, int notches)
{
	for (int i = static_cast<int>(m_controls.size()) - 1; i >= 0; --i) {
		Control &control = m_controls[i];
		if (control.def->vscroll && control.rect.Contains(x, y) && Shown(control)) {
			ScrollBy(control, -3 * notches);
			return true;
		}
	}
	return false;
}

void SidlWindow::ScrollBy(Control &control, int rows)
{
	control.scroll_pos = std::clamp(control.scroll_pos + rows, 0, std::max(control.scroll_max, 0));
}

// ---------------------------------------------------------------- gauges

void SidlWindow::DrawGauge(const Control &control, Canvas &canvas, TextRenderer &text, SidlTextures &textures,
	uint64_t now_ms) const
{
	const SidlPiece &def = *control.def;
	std::string label;
	int value = m_values ? m_values->Gauge(def.eq_type, label) : 0;

	// CGaugeWnd::Draw: while the text stays the same, the bar moves toward a new value at one
	// thousandth a millisecond; the casting, breath, memorizing and scribing bars (7..10) jump.
	float shown = static_cast<float>(value);
	bool slides = def.eq_type < 7 || def.eq_type > 10;
	if (slides && control.gauge_ms != 0 && label == control.gauge_text) {
		float elapsed = static_cast<float>(now_ms - control.gauge_ms);
		float diff = shown - control.gauge_shown;
		if (elapsed < std::fabs(diff))
			shown = control.gauge_shown + (diff > 0 ? elapsed : -elapsed);
	}
	control.gauge_shown = shown;
	control.gauge_text = label;
	control.gauge_ms = now_ms;
	if (value == -1)
		return;

	// The left cap, the background at its size with the right cap after it, all from GaugeOffset.
	const SidlGaugeTemplate &t = def.gauge;
	const Rect &c = control.client;
	int x = c.x + def.gauge_offset_x, y = c.y + def.gauge_offset_y;
	Rect cap_left = AnimationSize(t.cap_left);
	if (cap_left.w > 0) {
		DrawAnimation(canvas, textures, t.cap_left, {x, y, cap_left.w, cap_left.h}, now_ms);
		x += cap_left.w;
	}
	Rect bar = AnimationSize(t.background);
	bar.x = x;
	bar.y = y;
	DrawAnimation(canvas, textures, t.background, bar, now_ms);
	Rect cap_right = AnimationSize(t.cap_right);
	if (cap_right.w > 0)
		DrawAnimation(canvas, textures, t.cap_right, {bar.Right(), y, cap_right.w, cap_right.h}, now_ms);

	// The fill, cut off at the value (not squeezed), then the lines over the whole bar.
	auto fill = [&](const std::string &name, float amount, uint32_t tint) {
		const SidlAnimation *a = m_defs->Animation(name);
		const SidlFrame *f = a ? a->FrameAt(now_ms) : nullptr;
		const Image *image = f ? textures.Get(f->texture) : nullptr;
		int width = static_cast<int>(static_cast<float>(bar.w) * std::min(amount, 1000.0f) / 1000.0f);
		if (image && width > 0)
			canvas.Stretch(*image, f->source, bar, tint, {bar.x, bar.y, width, bar.h});
	};
	if (value == -2)
		fill(t.fill, 1000, 0x800000);
	else if (value < -9)
		fill(t.fill, -shown, 0x800080);
	else if (value > 0)
		fill(t.fill, shown, def.fill_tint);
	if (value == -2 || value < -9 || value > 0)
		DrawAnimation(canvas, textures, t.lines, bar, now_ms);

	// The text from TextOffset (the client clip rect hides what's outside the gauge).
	if (label.empty())
		label = control.text;
	if (!label.empty())
		text.Draw(canvas, c.x + def.text_offset_x, c.y + def.text_offset_y, label, SidlFontHeight(def.font),
			def.has_text_color ? control.text_color : 0xf0f0f0);
}

// ---------------------------------------------------------------- scrollbars

bool SidlWindow::HasScrollbar(const Control &control) const
{
	return control.def->vscroll && control.frame && !control.frame->vscroll.up.normal.empty();
}

SidlWindow::ScrollLayout SidlWindow::LayoutScrollbar(const Control &control) const
{
	// Down the right of the client area: the arrows at its ends, the thumb in the track between,
	// as long as a page is of the whole (no shorter than its end pieces), filling it when there's
	// nothing to scroll.
	const SidlScrollbarTemplate &t = control.frame->vscroll;
	Rect up = AnimationSize(t.up.normal), down = AnimationSize(t.down.normal);
	const Rect &c = control.client;
	ScrollLayout l;
	l.bar = {c.Right() - up.w, c.y, up.w, c.h};
	l.up = {l.bar.x, c.y, up.w, up.h};
	l.down = {l.bar.x, c.Bottom() - down.h, down.w, down.h};
	l.track = {l.bar.x, l.up.Bottom(), l.bar.w, std::max(l.down.y - l.up.Bottom(), 0)};
	l.thumb = l.track;
	if (control.scroll_max > 0) {
		int ends = AnimationSize(t.thumb_top).h + AnimationSize(t.thumb_bottom).h;
		int page = std::max(control.scroll_page, 1);
		int h = std::max(static_cast<int>(static_cast<int64_t>(l.track.h) * page / (control.scroll_max + page)), ends + 2);
		h = std::min(h, l.track.h);
		l.thumb.h = h;
		l.thumb.y = l.track.y + static_cast<int>(static_cast<int64_t>(l.track.h - h) * control.scroll_pos / control.scroll_max);
	}
	return l;
}

SidlWindow::ScrollPart SidlWindow::ScrollPartAt(const Control &control, int x, int y) const
{
	ScrollLayout l = LayoutScrollbar(control);
	if (!l.bar.Contains(x, y))
		return ScrollPart::None;
	if (l.up.Contains(x, y))
		return ScrollPart::Up;
	if (l.down.Contains(x, y))
		return ScrollPart::Down;
	if (control.scroll_max <= 0)
		return ScrollPart::Thumb;
	if (y < l.thumb.y)
		return ScrollPart::PageUp;
	if (y >= l.thumb.Bottom())
		return ScrollPart::PageDown;
	return ScrollPart::Thumb;
}

void SidlWindow::DrawScrollbar(const Control &control, Canvas &canvas, SidlTextures &textures, uint64_t now_ms) const
{
	const SidlScrollbarTemplate &t = control.frame->vscroll;
	ScrollLayout l = LayoutScrollbar(control);
	int index = static_cast<int>(&control - m_controls.data());

	// The gutter: its texture repeated down the track, tinted.
	if (const Image *gutter = textures.Get(t.gutter)) {
		Rect source{0, 0, gutter->width, gutter->height};
		for (int y = l.track.y; y < l.track.Bottom(); y += source.h) {
			Rect part{l.track.x, y, l.track.w, std::min(source.h, l.track.Bottom() - y)};
			canvas.Stretch(*gutter, {0, 0, source.w, part.h}, part, t.gutter_tint, part);
		}
	}
	auto button = [&](const SidlButtonTemplate &look, ScrollPart part, const Rect &r) {
		bool hover = m_scroll_hover == index && m_scroll_hover_part == part;
		bool pressed = m_scroll_control == index && m_scroll_part == part;
		std::string name = control.scroll_max <= 0 ? look.disabled
		                   : pressed && hover      ? (look.pressed_flyby.empty() ? look.pressed : look.pressed_flyby)
		                   : pressed               ? look.pressed
		                   : hover                 ? look.flyby
		                                           : look.normal;
		DrawAnimation(canvas, textures, name.empty() ? look.normal : name, r, now_ms);
	};
	button(t.up, ScrollPart::Up, l.up);
	button(t.down, ScrollPart::Down, l.down);
	if (control.scroll_max <= 0 || l.thumb.h <= 0)
		return;
	Rect top = AnimationSize(t.thumb_top), bottom = AnimationSize(t.thumb_bottom);
	DrawAnimation(canvas, textures, t.thumb_top, {l.thumb.x, l.thumb.y, l.thumb.w, top.h}, now_ms);
	DrawAnimation(canvas, textures, t.thumb_middle, {l.thumb.x, l.thumb.y + top.h, l.thumb.w, l.thumb.h - top.h - bottom.h}, now_ms);
	DrawAnimation(canvas, textures, t.thumb_bottom, {l.thumb.x, l.thumb.Bottom() - bottom.h, l.thumb.w, bottom.h}, now_ms);
}
