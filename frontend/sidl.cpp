#include "sidl.h"
#include "tga.h"
#include "xml.h"

#include <algorithm>
#include <cctype>
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
	static const int heights[] = {10, 10, 12, 14, 15, 16, 20};
	return font >= 0 && font < 7 ? heights[font] : 14;
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
		m_window_templates[item] = std::move(t);
		return;
	}

	static const char *const PieceTypes[] = {"Screen", "Button", "Label", "StaticAnimation", "StaticText", "StaticFrame",
		"Editbox", "Listbox", "Gauge", "Slider", "Combobox", "Page", "TabBox", "LayoutBox", "StaticHeader", "STMLbox"};
	if (std::find(std::begin(PieceTypes), std::end(PieceTypes), node.name) == std::end(PieceTypes))
		return;

	SidlPiece p;
	p.type = node.name;
	p.item = item;
	p.screen_id = node.ChildText("ScreenID", item);
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
	p.align_center = node.ChildBool("AlignCenter", false);
	p.align_right = node.ChildBool("AlignRight", false);
	p.checkbox = node.ChildBool("Style_Checkbox", false);
	p.radio_group = node.ChildText("RadioGroup");
	if (const XmlNode *t = node.Child("ButtonDrawTemplate"))
		p.button = ReadButtonTemplate(*t);
	p.animation = node.ChildText("Animation");
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

bool SidlWindow::Shown(const Control &control) const
{
	for (const Control *c = &control; c; c = c->parent >= 0 ? &m_controls[c->parent] : nullptr)
		if (!c->visible)
			return false;
	return true;
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

	auto frame_of = [&](int piece) -> const SidlFrame * {
		const SidlAnimation *a = m_defs->Animation(frame->border[piece]);
		return a ? a->FrameAt(now_ms) : nullptr;
	};
	auto w = [&](int piece) { const SidlFrame *f = frame_of(piece); return f ? f->source.w : 0; };
	auto h = [&](int piece) { const SidlFrame *f = frame_of(piece); return f ? f->source.h : 0; };
	const Rect &r = control.rect;
	auto draw = [&](int piece, Rect dest) {
		if (dest.w > 0 && dest.h > 0)
			DrawAnimation(canvas, textures, frame->border[piece], dest, now_ms);
	};
	// Corners at natural size; edges stretched between them.
	draw(TopLeft, {r.x, r.y, w(TopLeft), h(TopLeft)});
	draw(TopRight, {r.Right() - w(TopRight), r.y, w(TopRight), h(TopRight)});
	draw(Top, {r.x + w(TopLeft), r.y, r.w - w(TopLeft) - w(TopRight), h(Top)});
	draw(BottomLeft, {r.x, r.Bottom() - h(BottomLeft), w(BottomLeft), h(BottomLeft)});
	draw(BottomRight, {r.Right() - w(BottomRight), r.Bottom() - h(BottomRight), w(BottomRight), h(BottomRight)});
	draw(Bottom, {r.x + w(BottomLeft), r.Bottom() - h(Bottom), r.w - w(BottomLeft) - w(BottomRight), h(Bottom)});
	int left_top = r.y + h(TopLeft), left_bottom = r.Bottom() - h(BottomLeft);
	draw(LeftTop, {r.x, left_top, w(LeftTop), h(LeftTop)});
	draw(LeftBottom, {r.x, left_bottom - h(LeftBottom), w(LeftBottom), h(LeftBottom)});
	draw(Left, {r.x, left_top + h(LeftTop), w(Left), left_bottom - h(LeftBottom) - left_top - h(LeftTop)});
	int right_top = r.y + h(TopRight), right_bottom = r.Bottom() - h(BottomRight);
	draw(RightTop, {r.Right() - w(RightTop), right_top, w(RightTop), h(RightTop)});
	draw(RightBottom, {r.Right() - w(RightBottom), right_bottom - h(RightBottom), w(RightBottom), h(RightBottom)});
	draw(Right, {r.Right() - w(Right), right_top + h(RightTop), w(Right), right_bottom - h(RightBottom) - right_top - h(RightTop)});
}

void SidlWindow::Draw(Canvas &canvas, TextRenderer &text, SidlTextures &textures, uint64_t now_ms) const
{
	if (!m_controls.empty())
		DrawControl(0, canvas, text, textures, now_ms);
}

void SidlWindow::DrawControl(int index, Canvas &canvas, TextRenderer &text, SidlTextures &textures, uint64_t now_ms) const
{
	const Control &control = m_controls[index];
	if (!control.visible)
		return;
	const SidlPiece &def = *control.def;
	int height = SidlFontHeight(def.font);
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
	}

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
		DrawAnimation(canvas, textures, name, control.rect, now_ms);
		if (!control.text.empty()) {
			TextSize size = text.Measure(control.text, height);
			uint32_t color = control.enabled ? control.text_color : 0x808080;
			text.Draw(canvas, control.rect.x + (control.rect.w - size.w) / 2, control.rect.y + (control.rect.h - size.h) / 2,
				control.text, height, color);
		}
	}
	else if (def.type == "Label" || def.type == "StaticText") {
		if (!control.text.empty()) {
			TextSize size = text.Measure(control.text, height);
			int x = control.rect.x;
			if (def.align_center)
				x += (control.rect.w - size.w) / 2;
			else if (def.align_right)
				x += control.rect.w - size.w;
			text.Draw(canvas, x, control.rect.y, control.text, height, control.text_color);
		}
	}
	else if (def.type == "Editbox") {
		const Rect &c = control.client;
		TextSize size = text.Measure(control.text.empty() ? "W" : control.text, height);
		int y = c.y + (c.h - size.h) / 2;
		if (!control.text.empty())
			text.Draw(canvas, c.x + 2, y, control.text, height, control.text_color);
		if (index == m_focus && (now_ms / 500) % 2 == 0) {
			int x = c.x + 2 + (control.text.empty() ? 0 : size.w);
			canvas.Fill({x, y, 1, size.h}, control.text_color);
		}
	}
	else if (def.type == "STMLbox") {
		if (!control.text.empty()) {
			const Rect &c = control.client;
			text.DrawWrapped(canvas, {c.x + 2, c.y + 2, c.w - 4, c.h - 4}, StmlToText(control.text), height,
				def.has_text_color ? control.text_color : 0xffffff);
		}
	}

	// Static animations go under the other pieces (the creation screen lists its city buttons
	// before the maps they sit on; the spellbook its page art before the spells); otherwise
	// pieces stack in the order they're listed.
	for (int pass = 0; pass < 2; ++pass)
		for (int child : control.children)
			if ((m_controls[child].def->type == "StaticAnimation") == (pass == 0))
				DrawControl(child, canvas, text, textures, now_ms);
}

std::string SidlWindow::OnMouse(const InputEvent &event)
{
	std::string clicked;
	for (int i = 0; i < static_cast<int>(m_controls.size()); ++i) {
		Control &control = m_controls[i];
		const std::string &type = control.def->type;
		if ((type != "Button" && type != "Editbox") || !Shown(control))
			continue;
		bool inside = control.rect.Contains(event.x, event.y);
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
