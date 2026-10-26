#include "sidl.h"
#include "item_tags.h"
#include "tga.h"
#include "xml.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <filesystem>
#include <initializer_list>
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
		t.normal_decal = node.ChildText("NormalDecal");
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

bool SidlAnimation::Cell(int cell, SidlFrame &out) const
{
	if (!grid || cell < 0 || frames.empty() || cell_width <= 0 || cell_height <= 0)
		return false;
	const SidlFrame &first = frames.front();
	int columns = first.source.w / cell_width, rows = first.source.h / cell_height;
	int per_frame = columns * rows;
	if (per_frame <= 0)
		return false;
	if (cell >= static_cast<int>(frames.size()) * per_frame)
		cell = 0;
	out = frames[cell / per_frame];
	int index = cell % per_frame;
	int column = vertical ? index / rows : index % columns;
	int row = vertical ? index % rows : index / columns;
	out.source = {out.source.x + column * cell_width, out.source.y + row * cell_height, cell_width, cell_height};
	return true;
}

int SidlFontHeight(int font)
{
	static const int heights[] = {10, 12, 14, 15, 16, 20, 24};
	return font >= 0 && font < 7 ? heights[font] : 15;
}

int SidlFontWeight(int font)
{
	return font == 5 || font == 6 ? 700 : 0;
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
	m_frame_templates.clear();
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
		animation.grid = node.ChildBool("Grid", false);
		animation.vertical = node.ChildBool("Vertical", false);
		animation.cell_width = node.ChildInt("CellWidth", 0);
		animation.cell_height = node.ChildInt("CellHeight", 0);
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

	if (node.name == "FrameTemplate") {
		SidlFrameTemplate t;
		for (int i = 0; i < BorderPieceCount; ++i)
			t.border[i] = node.ChildText(BorderNames[i]);
		t.overlap_left = node.ChildInt("OverlapLeft", 0);
		t.overlap_top = node.ChildInt("OverlapTop", 0);
		t.overlap_right = node.ChildInt("OverlapRight", 0);
		t.overlap_bottom = node.ChildInt("OverlapBottom", 0);
		m_frame_templates[item] = std::move(t);
		return;
	}

	if (node.name == "SliderDrawTemplate") {
		SidlSliderTemplate t;
		if (const XmlNode *thumb = node.Child("Thumb"))
			t.thumb = ReadButtonTemplate(*thumb);
		t.background = node.ChildText("Background");
		t.cap_left = node.ChildText("EndCapLeft");
		t.cap_right = node.ChildText("EndCapRight");
		m_slider_templates[item] = std::move(t);
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
	p.tooltip = node.ChildText("TooltipReference");
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
	if (const XmlNode *offset = node.Child("DecalOffset")) {
		p.decal.x = offset->ChildInt("X", 0);
		p.decal.y = offset->ChildInt("Y", 0);
	}
	if (const XmlNode *size = node.Child("DecalSize")) {
		p.decal.w = size->ChildInt("CX", 0);
		p.decal.h = size->ChildInt("CY", 0);
	}
	for (const XmlNode *column : node.Children("Columns"))
		p.columns.push_back({column->ChildText("Heading"), column->ChildInt("Width", 0)});
	p.animation = node.ChildText("Animation");
	p.slider_art = node.ChildText("SliderArt");
	if (p.type == "InvSlot")
		p.slot_background = node.ChildText("Background");
	if (const XmlNode *t = node.Child("SpellGemDrawTemplate")) {
		p.gem_holder = t->ChildText("Holder");
		p.gem_background = t->ChildText("Background");
		p.gem_highlight = t->ChildText("Highlight");
		p.gem_icon_x = node.ChildInt("SpellIconOffsetX", 0);
		p.gem_icon_y = node.ChildInt("SpellIconOffsetY", 0);
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
	p.tab_text = node.ChildText("TabText");
	if (const XmlNode *color = node.Child("TabTextColor"))
		p.tab_text_color = ReadColor(*color);
	if (const XmlNode *color = node.Child("TabTextActiveColor"))
		p.tab_text_active_color = ReadColor(*color);
	p.tab_icon = node.ChildText("TabIcon");
	p.tab_icon_active = node.ChildText("TabIconActive");
	p.tab_border = node.ChildText("TabBorderTemplate");
	p.page_border = node.ChildText("PageBorderTemplate");
	for (const XmlNode *page : node.Children("Pages"))
		p.pages.push_back(page->text);
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

const SidlFrameTemplate *SidlDefinitions::FrameTemplate(const std::string &name) const
{
	auto it = m_frame_templates.find(name);
	return it != m_frame_templates.end() ? &it->second : nullptr;
}

const SidlSliderTemplate *SidlDefinitions::SliderTemplate(const std::string &name) const
{
	auto it = m_slider_templates.find(name);
	return it != m_slider_templates.end() ? &it->second : nullptr;
}

const SidlPiece *SidlDefinitions::Piece(const std::string &item) const
{
	auto it = m_pieces.find(item);
	return it != m_pieces.end() ? &it->second : nullptr;
}

// ---------------------------------------------------------------- windows

namespace
{
	// STML's lines (<BR> and <P> break them), each as runs of text in one color: <c "#RRGGBB">
	// colors what follows until its </c>. Other tags are dropped. As CStmlWnd::ParseSTML
	// (@005826a0), a run of whitespace is one space, and none starts a line.
	std::vector<std::vector<SidlWindow::StmlRun>> ParseStml(const std::string &stml, uint32_t color)
	{
		std::vector<std::vector<SidlWindow::StmlRun>> lines(1);
		std::vector<uint32_t> colors{color};
		bool after_space = false;
		auto add = [&](const std::string &text) {
			auto &line = lines.back();
			if (!line.empty() && line.back().color == colors.back())
				line.back().text += text;
			else
				line.push_back({text, colors.back()});
			after_space = false;
		};
		for (size_t i = 0; i < stml.size(); ++i) {
			if (stml[i] == ' ' || stml[i] == '\t' || stml[i] == '\n' || stml[i] == '\r') {
				if (!after_space && !lines.back().empty())
					add(" ");
				after_space = true;
			}
			else if (stml[i] == '<') {
				size_t end = stml.find('>', i);
				if (end == std::string::npos)
					break;
				std::string tag = ToLowerCopy(stml.substr(i + 1, end - i - 1));
				if (tag == "br" || tag == "br/" || tag == "p")
					lines.emplace_back();
				else if (tag == "/c" && colors.size() > 1)
					colors.pop_back();
				else if (tag.size() > 2 && tag[0] == 'c' && tag[1] == ' ') {
					size_t hash = tag.find('#');
					colors.push_back(hash != std::string::npos ? strtoul(tag.c_str() + hash + 1, nullptr, 16) & 0xffffff
					                                           : colors.back());
				}
				i = end;
			}
			else if (stml.compare(i, 4, "&lt;") == 0) {
				add("<");
				i += 3;
			}
			else if (stml.compare(i, 4, "&gt;") == 0) {
				add(">");
				i += 3;
			}
			else if (stml.compare(i, 5, "&amp;") == 0) {
				add("&");
				i += 4;
			}
			else
				add(std::string(1, stml[i]));
		}
		return lines;
	}

	// A line wrapped into rows at spaces, as the chat window wraps; a word wider than a row has
	// one to itself.
	void WrapStml(TextRenderer &text, const std::vector<SidlWindow::StmlRun> &line, int width, int height, int weight,
		std::vector<std::vector<SidlWindow::StmlRun>> &rows)
	{
		std::vector<SidlWindow::StmlRun> row, word;
		int row_w = 0, word_w = 0;
		auto flush = [&]() {
			if (!row.empty() && row_w + word_w > width) {
				rows.push_back(std::move(row));
				row.clear();
				row_w = 0;
			}
			for (auto &piece : word) {
				if (!row.empty() && row.back().color == piece.color)
					row.back().text += piece.text;
				else
					row.push_back(std::move(piece));
			}
			row_w += word_w;
			word.clear();
			word_w = 0;
		};
		for (const auto &run : line)
			for (size_t pos = 0; pos < run.text.size();) {
				size_t space = run.text.find(' ', pos);
				size_t end = space == std::string::npos ? run.text.size() : space + 1;
				std::string piece = run.text.substr(pos, end - pos);
				word_w += text.Measure(piece, height, weight).w;
				word.push_back({piece, run.color});
				pos = end;
				if (space != std::string::npos)
					flush();
			}
		flush();
		rows.push_back(std::move(row));
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
	control.tooltip = piece.tooltip;
	control.text_color = piece.text_color;
	if (!piece.draw_template.empty())
		control.frame = m_defs->WindowTemplate(piece.draw_template);
	m_controls.push_back(control);
	if (parent >= 0)
		m_controls[parent].children.push_back(index);
	if (piece.type == "Screen" || piece.type == "Page") {
		for (const auto &item : piece.pieces)
			if (const SidlPiece *child = m_defs->Piece(item))
				if (child != &piece)
					Add(*child, index);
	}
	else if (piece.type == "TabBox") {
		// CTabWnd::CTabWnd: each of its Pages, the first one showing.
		for (const auto &item : piece.pages)
			if (const SidlPiece *page = m_defs->Piece(item))
				if (page->type == "Page")
					Add(*page, index);
		SelectTab(m_controls[index], 0);
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
	if (control.def->type == "TabBox") {
		// CTabWnd::UpdatePage: every page fills the inside of the page border, under the tabs.
		Rect inside = PageRect(control);
		if (const SidlFrameTemplate *border = m_defs->FrameTemplate(control.def->page_border))
			inside = FrameInside(*border, inside);
		for (int child : control.children)
			Place(child, inside);
		return;
	}
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

bool SidlWindow::Typing() const
{
	return m_focus >= 0 && Shown(m_controls[m_focus]) && m_controls[m_focus].enabled;
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
	// CXWnd's hit test rects (@00571540, pieces 3 and 5), from the rect inside the border: the
	// minimize box 8 in from its left and 2 down, the close box ending 7 short of its right and
	// 1 down; each its Normal frame's size.
	const Rect &t = root.title;
	if (box == TitleBox::Close) {
		if (!root.def->closebox)
			return {};
		Rect size = AnimationSize(root.frame->close_box.normal);
		return {t.Right() - 7 - size.w, t.y + 1, size.w, size.h};
	}
	if (!root.def->minimizebox)
		return {};
	Rect size = AnimationSize(root.frame->minimize_box.normal);
	return {t.x + 8, t.y + 2, size.w, size.h};
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
		if ((type == "Button" || type == "Editbox" || type == "InvSlot" || type == "SpellGem" || type == "Slider" ||
		     control.clickable) &&
		    control.enabled && HitTest(control, x, y))
			return true;
		if (HasScrollbar(control) && LayoutScrollbar(control).bar.Contains(x, y) && HitTest(control, x, y))
			return true;
		if (type == "TabBox" && HitTest(control, x, y) && TabAt(control, x, y) >= 0)
			return true;
	}
	return false;
}

void SidlDrawButtonText(TextRenderer &text, Canvas &canvas, const Rect &rect, const std::string &str, int height,
	uint32_t rgb, int weight)
{
	// As many words on each line as fit (at least one).
	std::vector<std::string> lines;
	TextSize whole = text.Measure(str, height, weight);
	if (whole.w <= rect.w)
		lines.push_back(str);
	else {
		std::string line;
		size_t start = 0;
		while (start < str.size()) {
			size_t end = str.find(' ', start);
			std::string word = str.substr(start, end == std::string::npos ? std::string::npos : end - start);
			start = end == std::string::npos ? str.size() : end + 1;
			if (word.empty())
				continue;
			std::string longer = line.empty() ? word : line + " " + word;
			if (!line.empty() && text.Measure(longer, height, weight).w > rect.w) {
				lines.push_back(line);
				line = word;
			}
			else
				line = longer;
		}
		if (!line.empty())
			lines.push_back(line);
	}
	int y = rect.y + (rect.h - whole.h * static_cast<int>(lines.size())) / 2;
	for (const auto &line : lines) {
		TextSize size = text.Measure(line, height, weight);
		text.Draw(canvas, rect.x + (rect.w - size.w) / 2, y, line, height, rgb, weight);
		y += whole.h;
	}
}

const SidlWindow::Control *SidlWindow::ControlAt(int x, int y) const
{
	// Children come after their parents, and later pieces are drawn over earlier ones, but static
	// animations go under the rest (DrawControl).
	if (m_minimized)
		return nullptr;
	for (int pass = 0; pass < 2; ++pass)
		for (size_t i = m_controls.size(); i-- > 1;) {
			const Control &control = m_controls[i];
			if ((control.def->type == "StaticAnimation") == (pass == 1) && Shown(control) && HitTest(control, x, y))
				return &control;
		}
	return nullptr;
}

std::string SidlWindow::ButtonAt(int x, int y) const
{
	if (m_minimized)
		return "";
	for (auto it = m_controls.rbegin(); it != m_controls.rend(); ++it)
		if ((it->def->type == "Button" || it->def->type == "InvSlot" || it->def->type == "SpellGem") && it->enabled &&
		    HitTest(*it, x, y))
			return it->def->screen_id;
	return "";
}

void SidlWindow::CancelPress()
{
	for (auto &control : m_controls)
		control.pressed = false;
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

void SidlWindow::SelectTab(Control &tab_box, int index)
{
	if (index < 0 || index >= static_cast<int>(tab_box.children.size()))
		return;
	tab_box.tab = index;
	for (int i = 0; i < static_cast<int>(tab_box.children.size()); ++i)
		m_controls[tab_box.children[i]].visible = i == index;
}

int SidlWindow::TabHeight(const Control &tab_box) const
{
	// CTabWnd::CTabWnd: its font's height and 8.
	return SidlFontHeight(tab_box.def->font) + 8;
}

Rect SidlWindow::PageRect(const Control &tab_box) const
{
	// CTabWnd::UpdatePage: the client area below the tabs, reaching up under their bottom
	// corners (the tab border's LeftBottom piece).
	const SidlFrameTemplate *border = m_defs->FrameTemplate(tab_box.def->tab_border);
	int top = TabHeight(tab_box) - (border ? AnimationSize(border->border[LeftBottom]).h : 0);
	const Rect &c = tab_box.client;
	return {c.x, c.y + top, c.w, c.h - top};
}

Rect SidlWindow::TabRect(const Control &tab_box, int index) const
{
	// CTabWnd::CalculateTabRectangle: the tabs share the page's width less the tab border's top
	// corners evenly, starting in from the page border's top left corner.
	int count = static_cast<int>(tab_box.children.size());
	if (index < 0 || index >= count)
		return {};
	const SidlFrameTemplate *tab_border = m_defs->FrameTemplate(tab_box.def->tab_border);
	const SidlFrameTemplate *page_border = m_defs->FrameTemplate(tab_box.def->page_border);
	Rect page = PageRect(tab_box);
	int width = page.w;
	if (tab_border)
		width -= AnimationSize(tab_border->border[TopLeft]).w + AnimationSize(tab_border->border[TopRight]).w;
	int left = page.x + (page_border ? AnimationSize(page_border->border[TopLeft]).w : 0);
	int x0 = left + width * index / count, x1 = left + width * (index + 1) / count;
	return {x0, tab_box.client.y, x1 - x0, TabHeight(tab_box)};
}

int SidlWindow::TabAt(const Control &tab_box, int x, int y) const
{
	for (int i = 0; i < static_cast<int>(tab_box.children.size()); ++i)
		if (TabRect(tab_box, i).Contains(x, y))
			return i;
	return -1;
}

void SidlWindow::DrawTabBox(const Control &tab_box, Canvas &canvas, TextRenderer &text, SidlTextures &textures,
	uint64_t now_ms, const Rect &clip) const
{
	// CTabWnd::Draw: the tabs behind, then the page, then the current tab over it.
	int count = static_cast<int>(tab_box.children.size());
	for (int i = 0; i < count; ++i)
		if (i != tab_box.tab)
			DrawTab(tab_box, i, canvas, text, textures, now_ms, clip);
	// CTabWnd::DrawCurrentPage: the page border, its top edge only under the tabs behind (the
	// current tab opens onto the page).
	canvas.SetClip(clip);
	if (const SidlFrameTemplate *border = m_defs->FrameTemplate(tab_box.def->page_border)) {
		Rect pieces[BorderPieceCount];
		LayoutFrame(border->border, PageRect(tab_box), now_ms, pieces);
		for (int i = 0; i < BorderPieceCount; ++i)
			if (i != Top && pieces[i].w > 0 && pieces[i].h > 0)
				DrawAnimation(canvas, textures, border->border[i], pieces[i], now_ms);
		for (int i = 0; i < count; ++i)
			if (i != tab_box.tab) {
				Rect tab = TabRect(tab_box, i);
				DrawAnimation(canvas, textures, border->border[Top], {tab.x, pieces[Top].y, tab.w, pieces[Top].h}, now_ms);
			}
	}
	DrawTab(tab_box, tab_box.tab, canvas, text, textures, now_ms, clip);
	canvas.SetClip(clip);
}

void SidlWindow::DrawTab(const Control &tab_box, int index, Canvas &canvas, TextRenderer &text, SidlTextures &textures,
	uint64_t now_ms, const Rect &clip) const
{
	// CTabWnd::DrawTab: the tab border, the page's TabIcon (TabIconActive on the current tab) in
	// its top left corner inside the border, and its TabText centered in the rest.
	if (index < 0 || index >= static_cast<int>(tab_box.children.size()))
		return;
	const SidlPiece &page = *m_controls[tab_box.children[index]].def;
	bool current = index == tab_box.tab;
	Rect tab = TabRect(tab_box, index);
	Rect tab_clip = Intersect(tab, clip);
	if (!current) {
		// A tab behind sits 2 pixels lower, cut off the page border's bottom edge short of its own
		// bottom, so the page's top edge runs under it.
		tab.y += 2;
		if (const SidlFrameTemplate *page_border = m_defs->FrameTemplate(tab_box.def->page_border))
			tab_clip.h = std::max(tab_clip.h - AnimationSize(page_border->border[Bottom]).h, 0);
	}
	canvas.SetClip(tab_clip);
	Rect inside = tab;
	if (const SidlFrameTemplate *border = m_defs->FrameTemplate(tab_box.def->tab_border)) {
		Rect pieces[BorderPieceCount];
		LayoutFrame(border->border, tab, now_ms, pieces);
		for (int i = 0; i < BorderPieceCount; ++i)
			if (pieces[i].w > 0 && pieces[i].h > 0)
				DrawAnimation(canvas, textures, border->border[i], pieces[i], now_ms);
		// CTabWnd::CalculateTabContentRect: in by the Left, Top and Right pieces.
		int left = AnimationSize(border->border[Left]).w, top = AnimationSize(border->border[Top]).h;
		int right = AnimationSize(border->border[Right]).w;
		inside = {tab.x + left, tab.y + top, tab.w - left - right, tab.h - top};
	}
	const std::string &icon = current ? page.tab_icon_active : page.tab_icon;
	Rect icon_size = AnimationSize(icon);
	if (icon_size.w > 0) {
		DrawAnimation(canvas, textures, icon, {inside.x, inside.y, icon_size.w, icon_size.h}, now_ms);
		inside.x += icon_size.w;
		inside.w -= icon_size.w;
	}
	if (!page.tab_text.empty()) {
		int height = SidlFontHeight(tab_box.def->font), weight = SidlFontWeight(tab_box.def->font);
		TextSize size = text.Measure(page.tab_text, height, weight);
		text.Draw(canvas, inside.x + (inside.w - size.w) / 2, inside.y + (inside.h - size.h) / 2, page.tab_text, height,
			current ? page.tab_text_active_color : page.tab_text_color, weight);
	}
}

void SidlWindow::DrawCell(Canvas &canvas, SidlTextures &textures, const std::string &name, int cell, const Rect &dest) const
{
	const SidlAnimation *animation = m_defs->Animation(name);
	SidlFrame frame;
	if (!animation || !animation->Cell(cell, frame))
		return;
	if (const Image *image = textures.Get(frame.texture))
		canvas.Stretch(*image, frame.source, dest);
}

void SidlWindow::DrawAnimation(Canvas &canvas, SidlTextures &textures, const std::string &name, const Rect &dest,
	uint64_t now_ms, uint32_t tint) const
{
	const SidlAnimation *animation = m_defs->Animation(name);
	const SidlFrame *frame = animation ? animation->FrameAt(now_ms) : nullptr;
	const Image *image = frame ? textures.Get(frame->texture) : nullptr;
	if (image)
		canvas.Stretch(*image, frame->source, dest, tint, canvas.Clip());
}

void SidlWindow::DrawSpellGem(const Control &control, Canvas &canvas, SidlTextures &textures, uint64_t now_ms) const
{
	// CSpellGemWnd::Draw (DrawButtonTextures @005a6050): an empty gem is the Holder alone (the
	// dark, empty socket). One with a spell is the Background ring in the spell's color, a shade
	// darker for each step of its fade and lighter by 0x20 under the mouse; then the icon at
	// SpellIconOffset at its own size, as opaque as setGaugeColor's alphas say (0xff ready, then
	// 0xea down by 0x15 a step); then the Highlight over them.
	const SidlPiece &def = *control.def;
	if (control.cell < 0) {
		DrawAnimation(canvas, textures, def.gem_holder, control.rect, now_ms);
		return;
	}
	int fade = std::clamp(control.gem_fade, 0, 10);
	uint32_t color = 0;
	for (int shift = 0; shift <= 16; shift += 8) {
		int channel = (control.gem_color >> shift) & 0xff;
		channel -= fade * (channel / 11);
		if (control.hover)
			channel = std::min(channel + 0x20, 0xff);
		color |= static_cast<uint32_t>(channel) << shift;
	}
	DrawAnimation(canvas, textures, def.gem_background, control.rect, now_ms, color);
	if (const SidlAnimation *icons = m_defs->Animation("A_SpellGems")) {
		Rect icon{control.rect.x + def.gem_icon_x, control.rect.y + def.gem_icon_y, icons->cell_width, icons->cell_height};
		if (fade == 0)
			DrawCell(canvas, textures, "A_SpellGems", control.cell, icon);
		else {
			std::vector<uint32_t> kept;
			canvas.Keep(icon, kept);
			DrawCell(canvas, textures, "A_SpellGems", control.cell, icon);
			canvas.BlendKept(icon, kept, 0xea - 0x15 * (fade - 1));
		}
	}
	DrawAnimation(canvas, textures, def.gem_highlight, control.rect, now_ms);
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
	LayoutFrame(control.frame->border, control.rect, now_ms, pieces);
}

void SidlWindow::LayoutFrame(const std::string (&border)[BorderPieceCount], const Rect &r, uint64_t now_ms,
	Rect (&pieces)[BorderPieceCount]) const
{
	auto frame_of = [&](int piece) -> const SidlFrame * {
		const SidlAnimation *a = m_defs->Animation(border[piece]);
		return a ? a->FrameAt(now_ms) : nullptr;
	};
	auto w = [&](int piece) { const SidlFrame *f = frame_of(piece); return f ? f->source.w : 0; };
	auto h = [&](int piece) { const SidlFrame *f = frame_of(piece); return f ? f->source.h : 0; };
	// Corners at natural size; edges stretched between them.
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

Rect SidlWindow::FrameInside(const SidlFrameTemplate &frame, const Rect &r) const
{
	// CTAFrameDraw::GetInnerRect: in by the widest piece down each side, less the overlap.
	auto widest = [&](std::initializer_list<int> list, bool width) {
		int most = 0;
		for (int piece : list) {
			Rect size = AnimationSize(frame.border[piece]);
			most = std::max(most, width ? size.w : size.h);
		}
		return most;
	};
	int left = widest({LeftTop, Left, LeftBottom}, true) - frame.overlap_left;
	int top = widest({TopLeft, Top, TopRight}, false) - frame.overlap_top;
	int right = widest({RightTop, Right, RightBottom}, true) - frame.overlap_right;
	int bottom = widest({BottomLeft, Bottom, BottomRight}, false) - frame.overlap_bottom;
	return {r.x + left, r.y + top, r.w - left - right, r.h - top - bottom};
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
	int height = SidlFontHeight(def.font), weight = SidlFontWeight(def.font);
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
			// DrawWindowAndContents (@005729b0): the window's own font, centered, a pixel above middle.
			TextSize size = text.Measure(control.text, height, weight);
			text.Draw(canvas, t.x + (t.w - size.w) / 2, t.y + (t.h - size.h) / 2 - 1, control.text, height,
				control.text_color, weight);
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
		DrawAnimation(canvas, textures, control.animation.empty() ? def.animation : control.animation, control.rect, now_ms);
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
		// A cell_animation takes the place of the template and its decal, filling the button (the item
		// display's icon: CItemDisplayWnd::SetItem makes A_DragItem the Normal and clears the decal, so
		// the window's RedIconBackground doesn't show).
		bool cell_background = !control.cell_animation.empty();
		if (cell_background && control.cell >= 0)
			DrawCell(canvas, textures, control.cell_animation, control.cell, control.rect);
		else if (!control.blank && !cell_background && !control.no_background)
			DrawAnimation(canvas, textures, control.background.empty() ? name : control.background, control.rect, now_ms);
		// The decal at its offset (its own size without DecalSize); the text goes in what's right of it.
		// A grid decal (a buff's BuffIcons) shows the control's cell, and nothing without one
		// (CBuffWindow::SetBuffIcon's SetCurCell(-1)).
		Rect text_rect = control.rect;
		const std::string &decal_name = control.decal.empty() ? t.normal_decal : control.decal;
		const SidlAnimation *decal_animation = cell_background ? nullptr : m_defs->Animation(decal_name);
		bool grid_decal = decal_animation && decal_animation->grid;
		if (decal_animation && !control.blank && (!grid_decal || control.cell >= 0)) {
			Rect decal = def.decal.w > 0 && def.decal.h > 0 ? def.decal : AnimationSize(decal_name);
			decal.x += control.rect.x;
			decal.y += control.rect.y;
			if (grid_decal)
				DrawCell(canvas, textures, decal_name, control.cell, decal);
			else
				DrawAnimation(canvas, textures, decal_name, decal, now_ms);
			text_rect.w -= decal.Right() - text_rect.x;
			text_rect.x = decal.Right();
		}
		if (!control.text.empty()) {
			// CButtonWnd::Draw: grey when disabled, yellow when checked or under the mouse.
			uint32_t color = !control.enabled                  ? 0x808080
			                 : control.checked || control.hover ? 0xffff00
			                                                    : control.text_color;
			SidlDrawButtonText(text, canvas, text_rect, control.text, height, color, weight);
		}
	}
	else if (def.type == "InvSlot") {
		// CInvSlotWnd: the slot's own picture, and what's in it over that.
		DrawAnimation(canvas, textures, def.slot_background, control.rect, now_ms);
		if (control.cell >= 0)
			DrawCell(canvas, textures, control.cell_animation, control.cell, control.rect);
		// A stack's count in its bottom right corner: a dark box 12 by 10, a pixel in from the
		// edges, and the number padded to two places over it in font 1 (the CTextureFont the
		// slot makes for itself, @005a75e0), whatever the slot's own font.
		if (control.cell >= 0 && control.count > 0) {
			int r = control.rect.Right(), b = control.rect.Bottom();
			canvas.Fill({r - 13, b - 11, 12, 10}, 0x404040);
			std::string count = std::to_string(control.count);
			if (count.size() < 2)
				count = " " + count;
			text.Draw(canvas, r - 13, b - 11, count, SidlFontHeight(1), 0xffffff, SidlFontWeight(1));
		}
	}
	else if (def.type == "Slider") {
		DrawSlider(control, canvas, textures, now_ms);
	}
	else if (def.type == "SpellGem") {
		DrawSpellGem(control, canvas, textures, now_ms);
	}
	else if (def.type == "Label" || def.type == "StaticText") {
		std::string shown = control.text;
		uint32_t color = control.text_color;
		if (def.eq_type != 0 && m_values)
			m_values->Label(def.eq_type, shown, color);
		if (!shown.empty()) {
			TextSize size = text.Measure(shown, height, weight);
			int x = control.rect.x;
			if (def.align_center)
				x += (control.rect.w - size.w) / 2;
			else if (def.align_right)
				x += control.rect.w - size.w;
			text.Draw(canvas, x, control.rect.y, shown, height, color, weight);
		}
	}
	else if (def.type == "TabBox") {
		DrawTabBox(control, canvas, text, textures, now_ms, client_clip);
	}
	else if (def.type == "Gauge") {
		DrawGauge(control, canvas, text, textures, now_ms);
	}
	else if (def.type == "Listbox") {
		DrawListbox(control, canvas, text);
	}
	else if (def.type == "Editbox") {
		// The end of the text, as much as fits, with the caret after it. An item tag shows its name,
		// in the link color (CEditWnd::RenderEditWindowText).
		const Rect &c = control.client;
		std::vector<ItemTag> tags;
		std::string all = ShowItemTags(control.text, &tags);
		size_t from = 0;
		std::string shown = all;
		TextSize size = text.Measure(shown.empty() ? "W" : shown, height, weight);
		while (!shown.empty() && size.w > c.w - 6) {
			from += std::max<size_t>(1, shown.size() / 8);
			shown = all.substr(std::min(from, all.size()));
			size = text.Measure(shown.empty() ? "W" : shown, height, weight);
		}
		int y = c.y + (c.h - size.h) / 2;
		int x = c.x + 2;
		for (const ItemTagPiece &piece : SplitAtItemTags(tags, std::min(from, all.size()), all.size())) {
			std::string run = all.substr(piece.begin, piece.end - piece.begin);
			text.Draw(canvas, x, y, run, height, piece.tag >= 0 ? control.link_color : control.text_color, weight);
			x += text.Measure(run, height, weight).w;
		}
		if (index == m_focus && (now_ms / 500) % 2 == 0) {
			int x = c.x + 2 + (shown.empty() ? 0 : size.w);
			canvas.Fill({x, y, 1, size.h}, control.text_color);
		}
	}
	else if (def.type == "STMLbox" && control.draw_content) {
		control.draw_content(canvas, text, ContentRect(control));
	}
	else if (def.type == "STMLbox") {
		DrawStml(control, canvas, text);
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

	// CTabWnd's left button down: a press on a tab shows its page.
	if (event.type == InputEvent::Type::MouseDown || event.type == InputEvent::Type::DoubleClick)
		for (auto &control : m_controls)
			if (control.def->type == "TabBox" && HitTest(control, event.x, event.y)) {
				int tab = TabAt(control, event.x, event.y);
				if (tab >= 0) {
					SelectTab(control, tab);
					return "";
				}
			}

	// A slider moves with the mouse while the button's down on it.
	if (m_slider >= 0) {
		if (event.type == InputEvent::Type::MouseUp) {
			m_controls[m_slider].pressed = false;
			m_slider = -1;
		}
		else if (event.type == InputEvent::Type::MouseMove)
			SlideTo(m_controls[m_slider], event.x);
		return "";
	}
	if (event.type == InputEvent::Type::MouseDown || event.type == InputEvent::Type::DoubleClick)
		for (int i = static_cast<int>(m_controls.size()) - 1; i >= 0; --i) {
			Control &control = m_controls[i];
			if (control.def->type == "Slider" && control.enabled && HitTest(control, event.x, event.y)) {
				m_slider = i;
				control.pressed = true;
				SlideTo(control, event.x);
				return "";
			}
		}

	std::string clicked;
	for (int i = 0; i < static_cast<int>(m_controls.size()); ++i) {
		Control &control = m_controls[i];
		const std::string &type = control.def->type;
		if (type == "Slider" && Shown(control)) {
			control.hover = HitTest(control, event.x, event.y);
			continue;
		}
		if ((type != "Button" && type != "Editbox" && type != "InvSlot" && type != "SpellGem" && !control.clickable) ||
		    !Shown(control))
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

void SidlWindow::SetSlider(Control &slider, int value)
{
	slider.slider_value = std::clamp(value, 0, std::max(slider.slider_ticks - 1, 0));
}

// CSliderWnd's layout (GetSliderTextureRect, CalculateSliderRect, GetSliderBounds, GetSliderRect
// @005a6e20 on): the bar's background as high as its animation, centered down the control; the
// left end cap at the control's left, the right one at its right, the bar between them; the thumb
// from the bar's top left, its value's share of the bar's width along (UpdateThumb).
SidlWindow::SliderLayout SidlWindow::LayoutSlider(const Control &slider) const
{
	SliderLayout l;
	const SidlSliderTemplate *t = m_defs->SliderTemplate(slider.def->slider_art);
	if (!t)
		return l;
	const Rect &r = slider.rect;
	Rect background = AnimationSize(t->background), left = AnimationSize(t->cap_left), right = AnimationSize(t->cap_right);
	int y = r.y + (r.h - background.h) / 2;
	l.cap_left = {r.x, y, left.w, left.h};
	l.cap_right = {r.Right() - right.w, y, right.w, right.h};
	l.bar = {l.cap_left.Right(), y, l.cap_right.x - l.cap_left.Right(), background.h};
	Rect thumb = AnimationSize(t->thumb.normal);
	int along = slider.slider_ticks > 1 ? l.bar.w * slider.slider_value / (slider.slider_ticks - 1) : 0;
	l.thumb = {l.bar.x + along, l.bar.y, thumb.w, thumb.h};
	return l;
}

void SidlWindow::DrawSlider(const Control &slider, Canvas &canvas, SidlTextures &textures, uint64_t now_ms) const
{
	const SidlSliderTemplate *t = m_defs->SliderTemplate(slider.def->slider_art);
	if (!t)
		return;
	SliderLayout l = LayoutSlider(slider);
	DrawAnimation(canvas, textures, t->background, l.bar, now_ms);
	DrawAnimation(canvas, textures, t->cap_left, l.cap_left, now_ms);
	DrawAnimation(canvas, textures, t->cap_right, l.cap_right, now_ms);
	// DrawSliderBackground's thumb: Disabled, PressedFlyby or Pressed while held, Flyby under the mouse.
	const SidlButtonTemplate &thumb = t->thumb;
	bool held = slider.pressed;
	std::string name = !slider.enabled                                         ? thumb.disabled
	                   : held && slider.hover && !thumb.pressed_flyby.empty() ? thumb.pressed_flyby
	                   : held                                                 ? thumb.pressed
	                   : slider.hover                                         ? thumb.flyby
	                                                                          : thumb.normal;
	DrawAnimation(canvas, textures, name.empty() ? thumb.normal : name, l.thumb, now_ms);
}

void SidlWindow::SlideTo(Control &slider, int x)
{
	// SetScaledSliderValue: ticks times the distance along the bar over its width. Where on the bar
	// counts as 0 is our reading (its left end); the mouse handler that calls it isn't decompiled.
	SliderLayout l = LayoutSlider(slider);
	if (l.bar.w <= 0)
		return;
	SetSlider(slider, slider.slider_ticks * (x - l.bar.x) / l.bar.w);
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
			def.has_text_color ? control.text_color : 0xf0f0f0, SidlFontWeight(def.font));
}

// ---------------------------------------------------------------- list boxes

int SidlWindow::ListHeaderHeight(const Control &control) const
{
	// CListWnd::CListWnd: its font's height and 4, when it has columns.
	return control.def->columns.empty() ? 0 : SidlFontHeight(control.def->font) + 4;
}

int SidlWindow::ListRowHeight(const Control &control) const
{
	return control.row_height > 0 ? control.row_height : SidlFontHeight(control.def->font) + 2;
}

void SidlWindow::UpdateListScroll(Control &control) const
{
	Rect content = ContentRect(control);
	int rows = static_cast<int>(control.rows.size());
	control.scroll_page = std::max((content.h - ListHeaderHeight(control)) / ListRowHeight(control), 1);
	control.scroll_max = std::max(rows - control.scroll_page, 0);
	control.scroll_pos = std::clamp(control.scroll_pos, 0, control.scroll_max);
}

void SidlWindow::DrawStml(const Control &control, Canvas &canvas, TextRenderer &text) const
{
	// CStmlWnd: the text wrapped to the box, 2 pixels in, from the scroll position's row down; its
	// rows are worked out again only when the text or the width changes.
	const SidlPiece &def = *control.def;
	int height = SidlFontHeight(def.font), weight = SidlFontWeight(def.font);
	Rect content = ContentRect(control);
	Rect area{content.x + 2, content.y + 2, content.w - 4, content.h - 4};
	uint32_t color = def.has_text_color ? control.text_color : 0xffffff;
	if (control.stml_text != control.text || control.stml_width != area.w || control.stml_color != color) {
		control.stml_text = control.text;
		control.stml_width = area.w;
		control.stml_color = color;
		control.stml_rows.clear();
		if (!control.text.empty())
			for (const auto &line : ParseStml(control.text, color))
				WrapStml(text, line, area.w, height, weight, control.stml_rows);
	}
	int row_h = std::max(text.Measure("Wg", height, weight).h, 1);
	int rows = static_cast<int>(control.stml_rows.size());
	control.scroll_page = std::max(area.h / row_h, 1);
	control.scroll_max = std::max(rows - control.scroll_page, 0);
	int y = area.y;
	for (int i = std::clamp(control.scroll_pos, 0, control.scroll_max); i < rows && y < area.Bottom(); ++i, y += row_h) {
		int x = area.x;
		for (const auto &run : control.stml_rows[i]) {
			text.Draw(canvas, x, y, run.text, height, run.color, weight);
			x += text.Measure(run.text, height, weight).w;
		}
	}
}

void SidlWindow::DrawListbox(const Control &control, Canvas &canvas, TextRenderer &text) const
{
	// CListWnd::DrawHeader and DrawLine: the headings across the top, then the rows from the scroll
	// position down, each column's text 2 pixels in from its edges.
	const SidlPiece &def = *control.def;
	int height = SidlFontHeight(def.font), weight = SidlFontWeight(def.font);
	Rect content = ContentRect(control);
	Rect clip = canvas.Clip();
	auto draw = [&](const std::string &str, int column, int x, int width, int y, int line_height, uint32_t color) {
		if (str.empty() || width <= 4)
			return;
		Rect cell{x + 2, y, width - 4, line_height};
		TextSize size = text.Measure(str, height, weight);
		int justification = column < static_cast<int>(control.justification.size()) ? control.justification[column] : 0;
		int tx = justification == 1 ? cell.x + (cell.w - size.w) / 2 : justification == 2 ? cell.Right() - size.w : cell.x;
		canvas.SetClip(Intersect(cell, clip));
		text.Draw(canvas, tx, y + (line_height - size.h) / 2, str, height, color, weight);
	};
	int header = ListHeaderHeight(control);
	int x = content.x;
	for (size_t i = 0; i < def.columns.size(); ++i) {
		draw(def.columns[i].heading, static_cast<int>(i), x, def.columns[i].width, content.y, header, control.header_color);
		x += def.columns[i].width;
	}
	int row_height = ListRowHeight(control);
	int y = content.y + header;
	for (size_t r = static_cast<size_t>(std::max(control.scroll_pos, 0)); r < control.rows.size() && y < content.Bottom(); ++r) {
		const std::vector<std::string> &row = control.rows[r];
		x = content.x;
		for (size_t i = 0; i < row.size(); ++i) {
			int width = i < def.columns.size() ? def.columns[i].width : content.Right() - x;
			draw(row[i], static_cast<int>(i), x, width, y, row_height, control.row_color);
			x += width;
		}
		y += row_height;
	}
	canvas.SetClip(clip);
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
