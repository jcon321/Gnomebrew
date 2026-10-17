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
		m_window_templates[item] = std::move(t);
		return;
	}

	static const char *const PieceTypes[] = {"Screen", "Button", "Label", "StaticAnimation", "StaticText", "StaticFrame",
		"Editbox", "Listbox", "Gauge", "Slider", "Combobox", "Page", "TabBox", "LayoutBox", "StaticHeader"};
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

bool SidlWindow::Build(const SidlDefinitions &defs, const std::string &screen_item, std::string &error)
{
	m_defs = &defs;
	m_screen = defs.Piece(screen_item);
	if (!m_screen || m_screen->type != "Screen") {
		error = "SIDL screen '" + screen_item + "' not found";
		return false;
	}
	m_template = m_screen->draw_template.empty() ? nullptr : defs.WindowTemplate(m_screen->draw_template);
	m_rect = m_screen->rect;

	// The client area sits inside the border pieces.
	int left = 0, top = 0, right = 0, bottom = 0;
	if (m_template && m_screen->style_border) {
		auto size_of = [&](int piece, bool width) {
			const SidlAnimation *a = defs.Animation(m_template->border[piece]);
			const SidlFrame *f = a ? a->FrameAt(0) : nullptr;
			return f ? (width ? f->source.w : f->source.h) : 0;
		};
		left = size_of(Left, true);
		right = size_of(Right, true);
		top = size_of(Top, false);
		bottom = size_of(Bottom, false);
	}
	m_client = {m_rect.x + left, m_rect.y + top, m_rect.w - left - right, m_rect.h - top - bottom};

	m_controls.clear();
	for (const auto &item : m_screen->pieces) {
		const SidlPiece *piece = defs.Piece(item);
		if (!piece)
			continue;
		Control control;
		control.def = piece;
		control.text = piece->text;
		Rect r = piece->rect;
		if (!piece->has_size) {
			if (const SidlAnimation *a = defs.Animation(piece->animation))
				if (const SidlFrame *f = a->FrameAt(0)) {
					r.w = f->source.w;
					r.h = f->source.h;
				}
		}
		if (piece->relative) {
			r.x += m_client.x;
			r.y += m_client.y;
		}
		control.rect = r;
		m_controls.push_back(control);
	}
	return true;
}

SidlWindow::Control *SidlWindow::Find(const std::string &screen_id)
{
	for (auto &control : m_controls)
		if (control.def->screen_id == screen_id)
			return &control;
	return nullptr;
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

void SidlWindow::Draw(Canvas &canvas, TextRenderer &text, SidlTextures &textures, uint64_t now_ms) const
{
	if (!m_screen)
		return;

	if (m_template) {
		// Client background: the template texture tiled at its natural size.
		if (const Image *background = textures.Get(m_template->background))
			canvas.TileRegion(*background, {0, 0, background->width, background->height}, m_client);

		if (m_screen->style_border) {
			auto frame_of = [&](int piece) -> const SidlFrame * {
				const SidlAnimation *a = m_defs->Animation(m_template->border[piece]);
				return a ? a->FrameAt(now_ms) : nullptr;
			};
			auto w = [&](int piece) { const SidlFrame *f = frame_of(piece); return f ? f->source.w : 0; };
			auto h = [&](int piece) { const SidlFrame *f = frame_of(piece); return f ? f->source.h : 0; };
			const Rect &r = m_rect;
			auto draw = [&](int piece, Rect dest) {
				if (dest.w > 0 && dest.h > 0)
					DrawAnimation(canvas, textures, m_template->border[piece], dest, now_ms);
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
			draw(Right, {r.Right() - w(Right), right_top + h(RightTop), w(Right),
				right_bottom - h(RightBottom) - right_top - h(RightTop)});
		}
	}

	for (const auto &control : m_controls) {
		if (!control.visible)
			continue;
		const SidlPiece &def = *control.def;
		int height = SidlFontHeight(def.font);

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
				uint32_t color = control.enabled ? def.text_color : 0x808080;
				text.Draw(canvas, control.rect.x + (control.rect.w - size.w) / 2, control.rect.y + (control.rect.h - size.h) / 2,
					control.text, height, color);
			}
		}
		else if (def.type == "Label" || def.type == "StaticText") {
			if (control.text.empty())
				continue;
			TextSize size = text.Measure(control.text, height);
			int x = control.rect.x;
			if (def.align_center)
				x += (control.rect.w - size.w) / 2;
			else if (def.align_right)
				x += control.rect.w - size.w;
			text.Draw(canvas, x, control.rect.y, control.text, height, def.text_color);
		}
	}
}

std::string SidlWindow::OnMouse(const InputEvent &event)
{
	std::string clicked;
	for (auto &control : m_controls) {
		if (!control.visible || control.def->type != "Button")
			continue;
		bool inside = control.rect.Contains(event.x, event.y);
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
