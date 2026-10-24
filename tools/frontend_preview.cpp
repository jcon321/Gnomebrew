// frontend_preview (Linux): drives the front end headlessly and writes PNG
// snapshots, to check layout and flow without a Windows box. Text is drawn as
// outline boxes of an estimated Arial size, since GDI isn't available.
//
//   frontend_preview EQDIR OUTDIR [HOST:PORT USER PASSWORD [charselect]]
//   frontend_preview EQDIR OUTDIR --replay FILE [SECONDS]
//
// Without a login server it snapshots the start screen, a dialog and the login
// screen. With one it also logs in and snapshots server select and the play result.
// "charselect" stops at character select (two snapshots of the 3D scene, as the
// camera turns) instead of entering the world. --replay plays a packet log back (no server)
// and snapshots the zone once it's in, then every few seconds for SECONDS (default 10), then
// tries the windows: drags the selector by its title bar, opens the inventory from it, shows its
// items in the item display with alt+click (printing them) and the first by holding the right button,
// opens the skills window from it (printing its list), picks items up and puts them down (a stack
// through the quantity window, with Ctrl, and put away on the character view), closes both with
// Escape, minimizes and restores
// the actions window, and sizes the target and chat windows from their borders (the layout goes
// to OUTDIR/UI_*.ini); puts Actions window buttons on the hot
// buttons by holding them and clicking a hot button, swaps two, uses one, takes one off again by
// holding it and drops it with Escape (the hot buttons go to OUTDIR/<character>_*.ini); then types
// into the chat window, targets a spawn and prints the chat.

#include "eqstr.h"
#include "spell_file.h"
#include "frontend.h"
#include "inventory.h"
#include "png_writer.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <thread>
#include <vector>

namespace
{
	class BoxTextRenderer : public TextRenderer
	{
	public:
		TextSize Measure(const std::string &utf8, int height, int) override
		{
			// Arial averages roughly half its cell height per character.
			return {static_cast<int>(utf8.size()) * height / 2, height};
		}

		void Draw(Canvas &canvas, int x, int y, const std::string &utf8, int height, uint32_t rgb, int weight) override
		{
			if (utf8.empty())
				return;
			TextSize size = Measure(utf8, height, weight);
			canvas.Frame({x, y + height / 5, size.w, height * 3 / 5}, rgb);
		}

		void DrawWrapped(Canvas &canvas, const Rect &area, const std::string &utf8, int height, uint32_t rgb, int weight) override
		{
			// Greedy wrap on the estimated widths, one box per line.
			int per_line = std::max(1, area.w / std::max(1, height / 2));
			int y = area.y;
			size_t start = 0;
			while (start < utf8.size() && y + height <= area.Bottom()) {
				size_t newline = utf8.find('\n', start);
				size_t end = std::min(newline == std::string::npos ? utf8.size() : newline, start + per_line);
				Draw(canvas, area.x + 2, y, utf8.substr(start, end - start), height, rgb, weight);
				start = end + (end == newline ? 1 : 0);
				y += height;
			}
		}
	};

	uint64_t NowMs()
	{
		return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
	}

	struct Harness
	{
		FrontEnd &frontend;
		std::string out_dir;
		std::vector<uint32_t> pixels;

		void Snapshot(const std::string &name)
		{
			int w = frontend.ScreenWidth(), h = frontend.ScreenHeight();
			pixels.assign(static_cast<size_t>(w) * h, 0);
			Canvas canvas(pixels.data(), w, h, w);
			frontend.Render(canvas, NowMs());
			std::string path = out_dir + "/" + name + ".png";
			printf("[preview] %s %s\n", WritePng(path, pixels, w, h) ? "wrote" : "FAILED", path.c_str());
		}

		void Click(int x, int y) // in the current screen's layout
		{
			Rect area = frontend.ScreenArea();
			x += area.x;
			y += area.y;
			frontend.OnEvent({InputEvent::Type::MouseMove, x, y});
			frontend.OnEvent({InputEvent::Type::MouseDown, x, y});
			frontend.OnEvent({InputEvent::Type::MouseUp, x, y});
		}

		// A SIDL control on character select or creation, by ScreenID.
		bool ClickId(const std::string &id)
		{
			int x, y;
			if (!frontend.ControlCenter(id, x, y)) {
				printf("[preview] no control %s\n", id.c_str());
				return false;
			}
			frontend.OnEvent({InputEvent::Type::MouseMove, x, y});
			frontend.OnEvent({InputEvent::Type::MouseDown, x, y});
			frontend.OnEvent({InputEvent::Type::MouseUp, x, y});
			return true;
		}

		void Key(InputEvent::Key key)
		{
			InputEvent event;
			event.type = InputEvent::Type::Key;
			event.key = key;
			frontend.OnEvent(event);
		}

		void KeyUp(InputEvent::Key key)
		{
			InputEvent event;
			event.type = InputEvent::Type::KeyUp;
			event.key = key;
			frontend.OnEvent(event);
		}

		void Type(const std::string &text)
		{
			for (char c : text) {
				InputEvent event;
				event.type = InputEvent::Type::Char;
				event.ch = static_cast<unsigned char>(c);
				frontend.OnEvent(event);
			}
		}

		// Pumps the front end until done() or timeout.
		template <typename Done>
		bool RunUntil(Done done, int timeout_ms = 15000)
		{
			uint64_t end = NowMs() + timeout_ms;
			while (NowMs() < end) {
				frontend.Update(NowMs());
				if (done())
					return true;
				std::this_thread::sleep_for(std::chrono::milliseconds(15));
			}
			return false;
		}
	};
}

int main(int argc, char **argv)
{
	bool replay = argc >= 5 && std::string(argv[3]) == "--replay";
	if (!replay && argc != 3 && argc != 6 && argc != 7) {
		fprintf(stderr,
			"usage: %s EQDIR OUTDIR [HOST:PORT USER PASSWORD [charselect|create]]\n"
			"       %s EQDIR OUTDIR --replay FILE [SECONDS]\n",
			argv[0], argv[0]);
		return 2;
	}
	std::string eq_dir = argv[1];

	FrontEndArt art;
	std::string error;
	if (!art.Open(eq_dir, error)) {
		fprintf(stderr, "%s\n", error.c_str());
		return 1;
	}
	EqText strings;
	if (!strings.Load(eq_dir + "/eqtext.txt")) {
		fprintf(stderr, "can't read %s/eqtext.txt\n", eq_dir.c_str());
		return 1;
	}

	FrontEndConfig config;
	config.ui_dir = argv[2];
	bool stop_at_charselect = argc == 7 && std::string(argv[6]) == "charselect";
	bool create = argc == 7 && std::string(argv[6]) == "create";
	if (argc >= 6 && !replay) {
		std::string login = argv[3];
		size_t colon = login.rfind(':');
		config.login_host = login.substr(0, colon);
		if (colon != std::string::npos)
			config.login_port = static_cast<uint16_t>(atoi(login.c_str() + colon + 1));
	}

	std::string ui_dir = eq_dir + "/uifiles/default";
	SidlDefinitions sidl;
	SidlTextures textures(ui_dir);
	GameData ui;
	ui.eq_dir = eq_dir;
	if (sidl.Load(ui_dir, error)) {
		ui.definitions = &sidl;
		ui.textures = &textures;
	}
	else
		ui.error = error;
	for (const auto &warning : sidl.Warnings())
		printf("[preview] SIDL: %s\n", warning.c_str());
	EqStr eqstr;
	if (eqstr.Load(eq_dir + "/eqstr_en.txt"))
		ui.eqstr = &eqstr;
	SpellFile spells;
	if (spells.Load(eq_dir + "/spells_en.txt"))
		ui.spells = &spells;

	BoxTextRenderer text;
	FrontEnd frontend(art, strings, text, config, ui);
	frontend.log = [](const std::string &line) { printf("%s\n", line.c_str()); };
	frontend.SetGameResolution(1280, 720); // character select is drawn at this size
	Harness h{frontend, argv[2], {}};
	using Screen = FrontEnd::Screen;

	if (replay) {
		int seconds = argc >= 6 ? atoi(argv[5]) : 10;
		std::string replay_error;
		if (!frontend.StartReplay(argv[4], replay_error)) {
			fprintf(stderr, "[preview] %s\n", replay_error.c_str());
			return 1;
		}
		if (!h.RunUntil([&] { return frontend.Zone().GetState() == ZoneSession::State::InZone; })) {
			fprintf(stderr, "[preview] the replay never got in the zone\n");
			return 1;
		}
		h.RunUntil([&] { return frontend.ZoneScene() && frontend.ZoneScene()->Loaded(); }, 30000);
		h.RunUntil([] { return false; }, 500);
		h.Snapshot("r0-zone");
		for (int t = 3; t <= seconds; t += 3) {
			h.RunUntil([] { return false; }, 3000);
			h.Snapshot("r" + std::to_string(t) + "s");
		}

		WindowManager &windows = frontend.Windows();
		auto drag = [&](int x0, int y0, int x1, int y1) {
			frontend.OnEvent({InputEvent::Type::MouseMove, x0, y0});
			frontend.OnEvent({InputEvent::Type::MouseDown, x0, y0});
			for (int i = 1; i <= 4; ++i)
				frontend.OnEvent({InputEvent::Type::MouseMove, x0 + (x1 - x0) * i / 4, y0 + (y1 - y0) * i / 4});
			frontend.OnEvent({InputEvent::Type::MouseUp, x1, y1});
			frontend.Update(NowMs());
		};
		auto click = [&](int x, int y) { drag(x, y, x, y); };
		auto where = [&](const char *screen) {
			const SidlWindow *w = windows.Find(screen);
			if (w)
				printf("[preview] %s %s at %d,%d %dx%d%s\n", screen, windows.Shown(screen) ? "shown" : "hidden",
					w->Bounds().x, w->Bounds().y, w->Bounds().w, w->Bounds().h, w->Minimized() ? " (minimized)" : "");
			return w;
		};
		printf("[preview] %d windows, layout %s\n", windows.Count(), frontend.UiLayoutPath().c_str());
		h.Key(InputEvent::Key::F11); // the debug menu out of the way
		if (const SidlWindow *selector = where("SelectorWindow")) {
			const Rect &title = selector->Root().title;
			drag(title.x + 20, title.y + title.h / 2, title.x + 220, title.y + title.h / 2 + 120);
			where("SelectorWindow");
			int x, y;
			if (const SidlWindow::Control *button = selector->Find("SELW_InventoryToggleButton")) {
				x = button->rect.x + button->rect.w / 2;
				y = button->rect.y + button->rect.h / 2;
				click(x, y);
			}
		}
		h.RunUntil([] { return false; }, 300);
		h.Snapshot("w1-inventory");
		if (where("InventoryWindow")) {
			SidlWindow *inventory = windows.Find("InventoryWindow");
			for (int i = 0; i < 4; ++i)
				if (const SidlWindow::Control *money = inventory->Find("IW_Money" + std::to_string(i)))
					printf("[preview] IW_Money%d: %s\n", i, money->text.c_str());
			// Alt+click on a slot with an item shows it in the item display (each one printed;
			// Escape closes it first); the right button held on the first shows it too, and letting
			// go closes it.
			bool first = true;
			for (int slot = 1; slot <= 29; ++slot) {
				const SidlWindow::Control *c = inventory->Find("InvSlot" + std::to_string(slot));
				if (!c || c->cell < 0)
					continue;
				int x = c->rect.x + c->rect.w / 2, y = c->rect.y + c->rect.h / 2;
				InputEvent event{InputEvent::Type::MouseMove, x, y};
				frontend.OnEvent(event);
				event.alt = true;
				event.type = InputEvent::Type::MouseDown;
				frontend.OnEvent(event);
				event.type = InputEvent::Type::MouseUp;
				frontend.OnEvent(event);
				frontend.Update(NowMs());
				if (const SidlWindow *display = where("ItemDisplayWindow")) {
					const SidlWindow::Control *description = display->Find("ItemDescription");
					printf("[preview] InvSlot%d item display '%s':\n", slot, display->Root().text.c_str());
					std::string text = description ? description->text : "";
					for (size_t start = 0; start <= text.size();) {
						size_t br = text.find("<BR>", start);
						printf("[preview]   %s\n", text.substr(start, br == std::string::npos ? std::string::npos : br - start).c_str());
						if (br == std::string::npos)
							break;
						start = br + 4;
					}
					if (first)
						h.Snapshot("w1-item-display");
				}
				if (!first)
					continue;
				first = false;
				h.Key(InputEvent::Key::Escape);
				where("ItemDisplayWindow");
				frontend.OnEvent({InputEvent::Type::RightDown, x, y});
				h.RunUntil([] { return false; }, 900);
				where("ItemDisplayWindow");
				frontend.OnEvent({InputEvent::Type::RightUp, x, y});
				frontend.Update(NowMs());
				where("ItemDisplayWindow");
			}
			h.Key(InputEvent::Key::Escape); // the last item's display
			where("ItemDisplayWindow");
			// The Skills button opens the skills window; escape closes it before the inventory.
			if (const SidlWindow::Control *button = inventory->Find("IW_Skills"))
				click(button->rect.x + button->rect.w / 2, button->rect.y + button->rect.h / 2);
			h.RunUntil([] { return false; }, 300);
			if (where("SkillsWindow")) {
				const SidlWindow *skills = windows.Find("SkillsWindow");
				h.Snapshot("w1-skills");
				if (const SidlWindow::Control *list = skills->Find("SkillList")) {
					printf("[preview] skills list: %zu rows, scroll 0..%d, a page of %d\n", list->rows.size(), list->scroll_max,
						list->scroll_page);
					for (const auto &row : list->rows)
						printf("[preview]   %-24s %-12s %s\n", row[0].c_str(), row[1].c_str(), row[2].c_str());
				}
				h.Key(InputEvent::Key::Escape);
				where("SkillsWindow");
			}
			// Items onto the cursor and back (each step printing what's where): a click picks the
			// first lone item up and puts it in the first empty general slot; a click on the first
			// stack opens the quantity window, the slider goes halfway and Enter takes that many,
			// which the character view puts away again; Ctrl takes one, which goes back on its stack.
			auto items = [&](const char *when) {
				printf("[preview] items %s:", when);
				for (const auto &[slot, item] : frontend.Zone().Items())
					printf(" %d=%s%s", slot, item.name.c_str(),
						Inventory::Stacks(item) ? ("x" + std::to_string(Inventory::Count(item))).c_str() : "");
				printf("\n");
			};
			auto slot_click = [&](int slot, bool ctrl) {
				const SidlWindow::Control *c = inventory->Find("InvSlot" + std::to_string(slot));
				if (!c)
					return;
				int x = c->rect.x + c->rect.w / 2, y = c->rect.y + c->rect.h / 2;
				InputEvent event{InputEvent::Type::MouseMove, x, y};
				frontend.OnEvent(event);
				event.ctrl = ctrl;
				event.type = InputEvent::Type::MouseDown;
				frontend.OnEvent(event);
				event.type = InputEvent::Type::MouseUp;
				frontend.OnEvent(event);
				frontend.Update(NowMs());
			};
			int lone = -1, stack = -1, empty = -1;
			for (const auto &[slot, item] : frontend.Zone().Items()) {
				if (slot < Inventory::FirstGeneral || slot > Inventory::LastGeneral)
					continue;
				if (Inventory::Count(item) > 1 && stack < 0)
					stack = slot;
				else if (!Inventory::Stacks(item) && item.item_class == 0 && lone < 0)
					lone = slot;
			}
			for (int slot = Inventory::FirstGeneral; slot <= Inventory::LastGeneral && empty < 0; ++slot)
				if (!frontend.Zone().Items().count(static_cast<int16_t>(slot)))
					empty = slot;
			items("before");
			if (lone >= 0 && empty >= 0) {
				slot_click(lone, false);
				items("picked up");
				frontend.OnEvent({InputEvent::Type::MouseMove, 600, 400});
				h.RunUntil([] { return false; }, 200);
				h.Snapshot("w1-cursor-item");
				// Escape can't take an item off the cursor, so it closes the inventory (as eqgame's
				// CDisplay_NewUIProcessEscape goes on when RemoveAttachment won't); the selector opens
				// it again.
				h.Key(InputEvent::Key::Escape);
				items("after Escape");
				where("InventoryWindow");
				if (const SidlWindow::Control *button = windows.Find("SelectorWindow")->Find("SELW_InventoryToggleButton"))
					click(button->rect.x + button->rect.w / 2, button->rect.y + button->rect.h / 2);
				where("InventoryWindow");
				slot_click(empty, false);
				items("put down");
			}
			if (stack >= 0) {
				slot_click(stack, false);
				if (const SidlWindow *quantity = where("QuantityWnd")) {
					h.RunUntil([] { return false; }, 200);
					h.Snapshot("w1-quantity");
					const SidlWindow::Control *slider = quantity->Find("QTYW_Slider");
					const SidlWindow::Control *edit = quantity->Find("QTYW_SliderInput");
					printf("[preview] quantity: slider %d of %d, box '%s'\n", slider->slider_value, slider->slider_ticks - 1,
						edit->text.c_str());
					drag(slider->rect.x + 5, slider->rect.y + slider->rect.h / 2, slider->rect.x + slider->rect.w / 2,
						slider->rect.y + slider->rect.h / 2);
					printf("[preview] quantity: slider %d, box '%s'\n", slider->slider_value, edit->text.c_str());
					h.Snapshot("w1-quantity-half");
					h.Key(InputEvent::Key::Enter);
					where("QuantityWnd");
					items("took some");
					frontend.OnEvent({InputEvent::Type::MouseMove, 600, 400});
					h.RunUntil([] { return false; }, 200);
					h.Snapshot("w1-cursor-stack");
					if (const SidlWindow::Control *view = inventory->Find("IW_CharacterView"))
						click(view->rect.x + view->rect.w / 2, view->rect.y + view->rect.h / 2);
					items("put away");
				}
				slot_click(stack, true);
				items("ctrl took one");
				slot_click(stack, false);
				items("put back");
			}
			h.Key(InputEvent::Key::Escape); // eqgame's escape closes the inventory
			where("InventoryWindow");
		}
		// The actions window has a minimize box: down to its title bar and back.
		if (const SidlWindow::Control *button = windows.Find("SelectorWindow")->Find("SELW_ActionsToggleButton"))
			click(button->rect.x + button->rect.w / 2, button->rect.y + button->rect.h / 2);
		if (const SidlWindow *actions = where("ActionsWindow")) {
			Rect box = actions->TitleBoxRect(SidlWindow::TitleBox::Minimize);
			click(box.x + box.w / 2, box.y + box.h / 2);
			where("ActionsWindow");
			h.RunUntil([] { return false; }, 300);
			h.Snapshot("w2-minimized");
			click(box.x + box.w / 2, box.y + box.h / 2);
			where("ActionsWindow");
		}
		// Hot buttons. Holding is the left button down 750 ms; letting go after that isn't a click.
		auto hold = [&](int x, int y) {
			frontend.OnEvent({InputEvent::Type::MouseMove, x, y});
			frontend.OnEvent({InputEvent::Type::MouseDown, x, y});
			h.RunUntil([] { return false; }, 900);
			frontend.OnEvent({InputEvent::Type::MouseUp, x, y});
			frontend.Update(NowMs());
		};
		auto at = [&](const char *screen, const std::string &id, int &x, int &y) {
			SidlWindow *w = windows.Find(screen);
			const SidlWindow::Control *c = w ? w->Find(id) : nullptr;
			if (!c)
				return false;
			x = c->rect.x + c->rect.w / 2;
			y = c->rect.y + c->rect.h / 2;
			return true;
		};
		auto hot_text = [&](int n) {
			const SidlWindow::Control *c = windows.Find("HotButtonWnd")->Find("HB_Button" + std::to_string(n));
			return c ? c->text : std::string("?");
		};
		auto report = [&](const char *step) {
			const GameWindowState &s = frontend.WindowState();
			printf("[preview] hot buttons %s: cursor %d/%d \"%s\", HB1 \"%s\", HB2 \"%s\"\n", step, s.cursor.type,
				s.cursor.value, s.cursor_text.c_str(), hot_text(1).c_str(), hot_text(2).c_str());
		};
		int camp_x, camp_y, walk_x, walk_y, hb1_x, hb1_y, hb2_x, hb2_y;
		if (where("HotButtonWnd") && at("ActionsWindow", "AMP_CampButton", camp_x, camp_y) &&
		    at("ActionsWindow", "AMP_WalkButton", walk_x, walk_y) && at("HotButtonWnd", "HB_Button1", hb1_x, hb1_y) &&
		    at("HotButtonWnd", "HB_Button2", hb2_x, hb2_y)) {
			hold(camp_x, camp_y);
			report("Camp held");
			click(hb1_x, hb1_y);
			report("dropped on 1");
			hold(walk_x, walk_y);
			frontend.OnEvent({InputEvent::Type::MouseMove, hb1_x, hb1_y});
			h.Snapshot("h1-walk-attached");
			click(hb1_x, hb1_y);
			report("Walk dropped on 1 (Camp back on the cursor)");
			h.Snapshot("h2-swapped");
			click(hb2_x, hb2_y);
			report("dropped on 2");
			click(hb1_x, hb1_y);
			frontend.Update(NowMs());
			report("1 used (walking)");
			hold(hb2_x, hb2_y);
			report("2 held");
			h.Key(InputEvent::Key::Escape);
			report("Escape");
			click(hb1_x, hb1_y);
			frontend.Update(NowMs());
			report("1 used again (running)");
			h.Snapshot("h3-hot-buttons");
		}
		// Socials: the first page's defaults, one edited (right click, Clear, a name, two lines, a
		// color, Accept), done from the Actions window, then put on hot button 3 and done from there.
		auto right_click = [&](int x, int y) {
			frontend.OnEvent({InputEvent::Type::MouseMove, x, y});
			frontend.OnEvent({InputEvent::Type::RightDown, x, y});
			frontend.OnEvent({InputEvent::Type::RightUp, x, y});
			frontend.Update(NowMs());
		};
		auto social_report = [&](const char *step) {
			const GameWindowState &s = frontend.WindowState();
			const SidlWindow::Control *b1 = windows.Find("ActionsWindow")->Find("ASP_SocialButton1");
			const SidlWindow::Control *b12 = windows.Find("ActionsWindow")->Find("ASP_SocialButton12");
			printf("[preview] socials %s: 1 \"%s\" %06x, 12 \"%s\" %06x, going %d line %d wait %d, HB3 \"%s\", editor %s\n",
				step, b1 ? b1->text.c_str() : "?", b1 ? b1->text_color : 0, b12 ? b12->text.c_str() : "?",
				b12 ? b12->text_color : 0, s.current_social, s.social_line, s.social_wait, hot_text(3).c_str(),
				windows.Shown("SocialEditWnd") ? "shown" : "hidden");
		};
		if (SidlWindow *actions = windows.Find("ActionsWindow"))
			if (SidlWindow::Control *tabs = actions->Find("ACTW_ActionsSubwindows")) {
				actions->SelectTab(*tabs, 3);
				frontend.Update(NowMs());
				social_report("defaults");
				h.Snapshot("s1-socials-page");
				int s1_x, s1_y, x, y;
				if (at("ActionsWindow", "ASP_SocialButton1", s1_x, s1_y)) {
					right_click(s1_x, s1_y);
					where("SocialEditWnd");
					h.RunUntil([] { return false; }, 300);
					h.Snapshot("s2-editor-afk");
					if (at("SocialEditWnd", "SEW_Clear_Button", x, y))
						click(x, y);
					if (at("SocialEditWnd", "SEW_NameInput", x, y)) {
						click(x, y);
						h.Type("Testing123456789"); // 15 at most
					}
					if (at("SocialEditWnd", "SEW_Line0Input", x, y)) {
						click(x, y);
						h.Type("/pause 3, /loc");
					}
					if (at("SocialEditWnd", "SEW_Line2Input", x, y)) {
						click(x, y);
						h.Type("wasd from a social");
					}
					if (at("SocialEditWnd", "SEW_TC3Button", x, y))
						click(x, y);
					h.RunUntil([] { return false; }, 300);
					h.Snapshot("s3-editor-typed");
					if (at("SocialEditWnd", "SEW_Accept_Button", x, y))
						click(x, y);
					social_report("accepted");
					size_t chat_before = frontend.Chat().Lines().size();
					click(s1_x, s1_y);
					for (int i = 0; i < 12; ++i) {
						h.RunUntil([] { return false; }, 105);
						social_report("running");
					}
					for (size_t i = chat_before; i < frontend.Chat().Lines().size(); ++i)
						printf("[preview] social chat: %s\n", frontend.Chat().Lines()[i].text.c_str());
					int hb3_x, hb3_y;
					if (at("HotButtonWnd", "HB_Button3", hb3_x, hb3_y)) {
						hold(s1_x, s1_y);
						click(hb3_x, hb3_y);
						social_report("on hot button 3");
						h.RunUntil([] { return false; }, 300);
						h.Snapshot("s4-hot-button");
						click(hb3_x, hb3_y);
						social_report("hot button 3 used");
						click(hb3_x, hb3_y);
						social_report("hot button 3 again (still going: nothing)");
						right_click(hb3_x, hb3_y);
						social_report("hot button 3 right clicked");
						h.Key(InputEvent::Key::Escape);
						social_report("Escape");
					}
				}
			}
		// The target window is sizable: its bottom right corner, 60 right and 40 down.
		if (const SidlWindow *target = where("TargetWindow")) {
			Rect b = target->Bounds();
			drag(b.Right() - 1, b.Bottom() - 1, b.Right() + 59, b.Bottom() + 39);
			where("TargetWindow");
		}
		// The chat window sized from its border: the right side alone, then the bottom right corner
		// both ways, with the resize cursor over the corner.
		if (const SidlWindow *chat = where("ChatWindow")) {
			Rect b = chat->Bounds();
			drag(b.Right() - 2, b.y + b.h / 2, b.Right() + 38, b.y + b.h / 2);
			where("ChatWindow");
			b = chat->Bounds();
			frontend.OnEvent({InputEvent::Type::MouseMove, b.Right() - 2, b.Bottom() - 2});
			h.Snapshot("w4-resize-cursor");
			drag(b.Right() - 2, b.Bottom() - 2, b.Right() - 42, b.Bottom() - 32);
			where("ChatWindow");
		}
		h.RunUntil([] { return false; }, 300);
		h.Snapshot("w3-closed");

		// Chat: a say, a command that isn't one, an abbreviation, /loc, /target, and a line too
		// long for the input box.
		auto send = [&](const std::string &line) {
			h.Key(InputEvent::Key::Enter);
			h.Type(line);
			h.Key(InputEvent::Key::Enter);
			frontend.Update(NowMs());
		};
		send("hello there");
		send("/nosuchcommand");
		send("/s");
		send("/loc");
		send("/g hi group");
		send("/target a");
		h.RunUntil([] { return false; }, 300);
		printf("[preview] target after /target: %u\n", frontend.Target());
		// The names over heads: stand 25 units south of the target, facing it, then from 60 out
		// (the names smaller), and in the chase view (ours too).
		auto target = frontend.Zone().Spawns().find(frontend.Target());
		if (target != frontend.Zone().Spawns().end() && frontend.ZoneScene()) {
			const ZoneSpawn &t = target->second;
			const char *names[] = {"n1-names-25", "n2-names-60"};
			float backs[] = {25, 60};
			for (int i = 0; i < 2; ++i) {
				frontend.ZoneScene()->SetPlayerPosition(t.x, t.y - backs[i], t.z, 0);
				h.RunUntil([] { return false; }, 400);
				h.Snapshot(names[i]);
			}
			frontend.ZoneScene()->SetPlayerPosition(t.x, t.y - 25, t.z, 0);
			frontend.ZoneScene()->CycleCamera();
			h.RunUntil([] { return false; }, 400);
			h.Snapshot("n3-names-chase");
			frontend.ZoneScene()->CycleCamera();
			for (const auto &tag : frontend.ZoneScene()->NameTags(std::numeric_limits<float>::max())) {
				auto it = frontend.Zone().Spawns().find(tag.id);
				const ZoneSpawn &s = tag.id == 0 || it == frontend.Zone().Spawns().end() ? frontend.Zone().Self() : it->second;
				printf("[preview] name tag %u at %d,%d depth %.1f: '%s' %06x\n", tag.id, tag.x, tag.y, tag.depth,
					NameSpriteText(s, 4, frontend.Zone().Guilds(), &eqstr).c_str(), NameSpriteColor(s, 0));
			}
		}
		// What a player's name says with everything on.
		ZoneSpawn player;
		player.name = "Soandso";
		player.surname = "Smith";
		player.class_id = 3;
		player.aa_title = 2;
		player.gender = 1;
		player.guild_id = 7;
		player.lfg = 1;
		player.afk = 1;
		player.linkdead = 1;
		GuildNames guilds{{7, "Knights of Thunder"}};
		for (int level = 0; level <= NameSprite::ShowNamesMax; ++level)
			printf("[preview] shownames %d: '%s'\n", level, NameSpriteText(player, level, guilds, &eqstr).c_str());
		player.anon = 1;
		printf("[preview] anonymous: '%s' %06x\n", NameSpriteText(player, 4, guilds, &eqstr).c_str(), NameSpriteColor(player, 0));
		player.anon = 2;
		printf("[preview] roleplaying: '%s' %06x\n", NameSpriteText(player, 4, guilds, &eqstr).c_str(), NameSpriteColor(player, 0));
		player.anon = 0;
		player.pvp = 1;
		printf("[preview] pvp: '%s' %06x\n", NameSpriteText(player, 4, {}, &eqstr).c_str(), NameSpriteColor(player, 0));
		for (int step = 0; step < NameSprite::PulseSteps; step += 5)
			printf("[preview] target pulse %d: %06x\n", step, NameSpritePulse(0x17b8ad, step));
		frontend.OnEvent({InputEvent::Type::Char, 0, 0, 0, 0});
		// A spawn clicked: the first name tag in view, a little under the name.
		if (const ZoneView *view = frontend.ZoneScene()) {
			for (const auto &tag : view->NameTags(120))
				if (tag.id != 0) {
					click(tag.x, tag.y + 12);
					printf("[preview] clicked spawn %u at %d,%d: target %u\n", tag.id, tag.x, tag.y + 12, frontend.Target());
					break;
				}
		}
		for (int i = 0; i < 30; ++i)
			frontend.OnEvent({InputEvent::Type::Char, 0, 0, 0, 0}); // nothing: not typing
		h.Key(InputEvent::Key::Enter);
		h.Type(std::string(120, 'x'));
		h.RunUntil([] { return false; }, 300);
		h.Snapshot("c1-chat");
		h.Key(InputEvent::Key::Escape); // closes the chat line, not a window
		if (const SidlWindow *chat = where("ChatWindow")) {
			Rect b = chat->Bounds();
			InputEvent wheel;
			wheel.type = InputEvent::Type::Wheel;
			wheel.x = b.x + b.w / 2;
			wheel.y = b.y + b.h / 2;
			wheel.wheel = 1;
			frontend.OnEvent(wheel);
			if (const SidlWindow::Control *output = chat->Find("CWChatOutput"))
				printf("[preview] chat output scroll %d of %d, page %d\n", output->scroll_pos, output->scroll_max, output->scroll_page);
		}
		for (const auto &line : frontend.Chat().Lines())
			printf("[preview] chat %06x %s\n", line.rgb, line.text.c_str());
		h.Snapshot("c2-target");
		return 0;
	}

	h.Snapshot("1-start");
	h.Click(400 + 114, 0xff + 17); // ACCOUNT -> "not implemented" dialog
	h.Snapshot("2-dialog");
	h.Key(InputEvent::Key::Enter);
	h.Click(400 + 114, 0xd2 + 17); // CONNECT
	h.Snapshot("3-login");

	if (argc >= 6) {
		h.Type(argv[4]);
		h.Key(InputEvent::Key::Enter); // to the password field
		h.Type(argv[5]);
		h.Snapshot("4-login-filled");

		for (int attempt = 0; attempt < 2; ++attempt) {
			h.Key(InputEvent::Key::Enter);
			h.RunUntil([&] { return frontend.CurrentScreen() == Screen::ServerSelect || frontend.DialogOpen(); });
			if (!frontend.DialogOpen())
				break;
			printf("[preview] dialog: %s\n", frontend.DialogText().c_str());
			h.Snapshot("5-login-error");
			h.Key(InputEvent::Key::Enter); // close; a new account needs a second try
		}

		if (frontend.CurrentScreen() != Screen::ServerSelect) {
			fprintf(stderr, "[preview] never reached server select\n");
			return 1;
		}
		h.Snapshot("6-server-select");

		h.Key(InputEvent::Key::Enter); // PLAY the selected server
		h.RunUntil([&] { return frontend.DialogOpen() || frontend.CurrentScreen() == Screen::CharacterSelect; });
		if (frontend.DialogOpen()) {
			printf("[preview] dialog: %s\n", frontend.DialogText().c_str());
			h.Snapshot("7-play-result");
			return 1;
		}
		h.frontend.OnEvent({InputEvent::Type::MouseMove, 400, 300});
		if (const CharacterSelectView *scene = frontend.CharacterScene())
			h.RunUntil([&] { return scene->Loaded(); }, 10000);
		h.RunUntil([] { return false; }, 300);
		h.Snapshot("7-character-select");
		for (const auto &c : frontend.Characters())
			if (!c.Empty())
				printf("[preview] character: %s level %u %s %s zone %u\n", c.name.c_str(), c.level, RaceName(c.race),
					ClassName(c.class_id), c.zone);

		if (create) {
			// Character creation from the first empty slot: an Erudite wizard with a random name,
			// a snapshot of each screen, then submitted. The new character is deleted afterwards.
			const auto &slots = frontend.Characters();
			int empty = 0;
			while (empty < static_cast<int>(slots.size()) && !slots[empty].Empty())
				++empty;
			h.ClickId("Char" + std::to_string(empty + 1) + "_Button");
			if (frontend.CurrentScreen() != Screen::CharacterCreate) {
				printf("[preview] creation didn't open%s\n", frontend.DialogOpen() ? (": " + frontend.DialogText()).c_str() : "");
				return 1;
			}
			h.RunUntil([] { return false; }, 300);
			h.Snapshot("c0-random");
			h.ClickId("CC0_Race3_Button");  // Erudite
			h.ClickId("CC0_Class14_Button"); // Wizard
			h.ClickId("CC0_Female_Button");
			h.ClickId("CC0_Face_Button");
			h.RunUntil([] { return false; }, 200);
			h.Snapshot("c0-face");
			h.ClickId("Face_Up_Button");
			h.ClickId("Accept_Button");
			h.ClickId("CC0_Stat0_Up_Button");
			h.ClickId("CC0_Stat5_Button"); // the rest into intelligence, up to its maximum
			h.ClickId("CC0_Stat1_Button"); // and whatever's left into stamina
			std::string created;
			for (int attempt = 0; attempt < 3 && created.empty(); ++attempt) {
				h.ClickId("CC0_Get_Name_Button");
				h.RunUntil([] { return false; }, 200);
				h.Snapshot("c0-erudite-wizard");
				h.ClickId("CC_Next_Button");
				if (frontend.DialogOpen()) {
					printf("[preview] dialog: %s\n", frontend.DialogText().c_str());
					return 1;
				}
				h.Snapshot("c1-deity");
				h.ClickId("CC1_Deity10_Button"); // Quellious
				h.ClickId("CC_Next_Button");
				h.Snapshot("c2-city");
				h.ClickId("CC_Next_Button");
				h.Snapshot("c3-welcome");
				std::string name = frontend.Creation()->Character().name;
				printf("[preview] creating %s, start zone %u\n", name.c_str(), frontend.Creation()->Character().start_zone);
				h.ClickId("CC_Next_Button");
				h.RunUntil([&] { return frontend.CurrentScreen() == Screen::CharacterSelect; }, 10000);
				if (frontend.CurrentScreen() == Screen::CharacterSelect)
					created = name;
				else {
					printf("[preview] dialog: %s\n", frontend.DialogText().c_str());
					h.Key(InputEvent::Key::Enter);
				}
			}
			if (created.empty())
				return 1;
			h.RunUntil([] { return false; }, 300);
			h.Snapshot("c4-created");
			for (const auto &c : frontend.Characters())
				if (!c.Empty())
					printf("[preview] character: %s level %u %s %s zone %u\n", c.name.c_str(), c.level, RaceName(c.race),
						ClassName(c.class_id), c.zone);
			// Clean up: delete it again.
			if (frontend.SelectedCharacter() >= 0 && frontend.Characters()[frontend.SelectedCharacter()].name == created) {
				h.ClickId("Delete_Button");
				h.Key(InputEvent::Key::Enter); // yes
				h.RunUntil([&] {
					for (const auto &c : frontend.Characters())
						if (c.name == created)
							return false;
					return true;
				}, 5000);
				printf("[preview] deleted %s\n", created.c_str());
			}
			h.Key(InputEvent::Key::Escape);
			return 0;
		}

		if (stop_at_charselect) {
			h.RunUntil([] { return false; }, 2000);
			h.Snapshot("7-character-select-turned");
			h.Key(InputEvent::Key::Escape); // Quit back to the start screen
			return 0;
		}
		if (frontend.SelectedCharacter() >= 0) {
			h.Key(InputEvent::Key::Enter); // Enter World
			h.RunUntil([&] {
				return frontend.DialogOpen() || frontend.Zone().GetState() == ZoneSession::State::InZone;
			});
			if (frontend.DialogOpen()) {
				printf("[preview] dialog: %s\n", frontend.DialogText().c_str());
				h.Snapshot("8-enter-world");
				return 1;
			}
			h.RunUntil([&] { return frontend.ZoneScene() && frontend.ZoneScene()->Loaded(); }, 15000);
			h.RunUntil([] { return false; }, 1500); // let spawn updates and messages arrive
			h.Snapshot("8-zone");
			if (const ZoneView *view = frontend.ZoneScene()) {
				printf("[preview] zone view: %s %s, %zu triangles drawn\n", view->ZoneName().c_str(),
					view->Error().empty() ? "loaded" : view->Error().c_str(), view->TrianglesDrawn());
				if (view->Model())
					printf("[preview] our model %s, feet at %.2f, %.2f, %.2f (WLD)\n", view->Model()->Code().c_str(),
						view->Feet().x, view->Feet().y, view->Feet().z);
			}
			h.Key(InputEvent::Key::F9);
			h.Snapshot("8-zone-first-person");
			h.Key(InputEvent::Key::F9);
			// Right-drag: turn around about a third of a turn.
			frontend.OnEvent({InputEvent::Type::RightDown, 600, 300});
			frontend.OnEvent({InputEvent::Type::MouseMove, 320, 300});
			frontend.OnEvent({InputEvent::Type::RightUp, 320, 300});
			h.Snapshot("8-zone-turned");

			// Run forward for 3 seconds, turn right for one, jump, then stop.
			auto loc = [&](const char *when) {
				const ZoneProfile &me = frontend.Zone().Profile();
				printf("[preview] %s: loc %.1f, %.1f, %.1f heading %.0f\n", when, me.y, me.x, me.z, me.heading);
			};
			loc("before moving");
			h.Key(InputEvent::Key::Up);
			h.RunUntil([] { return false; }, 3000);
			loc("after 3 s forward");
			h.Snapshot("8-zone-running");
			h.Key(InputEvent::Key::Right);
			h.RunUntil([] { return false; }, 1000);
			h.KeyUp(InputEvent::Key::Right);
			h.Key(InputEvent::Key::Space);
			h.RunUntil([] { return false; }, 150);
			h.Snapshot("8-zone-jump");
			h.RunUntil([] { return false; }, 1000);
			h.KeyUp(InputEvent::Key::Up);
			h.RunUntil([] { return false; }, 1500);
			h.RunUntil([] { return false; }, 1200); // the next periodic update reaches the server
			loc("stopped");
			h.Snapshot("8-zone-moved");

			h.Key(InputEvent::Key::Enter); // open the chat line
			h.Type("hello from the GUI");
			h.Key(InputEvent::Key::Enter);
			h.RunUntil([] { return false; }, 1500);
			h.Snapshot("9-zone-chat");

			// /target asks the zone, which answers with the target it set.
			h.Key(InputEvent::Key::Enter);
			h.Type("/target guard");
			h.Key(InputEvent::Key::Enter);
			h.RunUntil([&] { return frontend.Target() != 0; }, 3000);
			printf("[preview] target %u; our hp %d (+%d worn) of %d\n", frontend.Target(), frontend.Zone().BaseHp(),
				GameValues::WornHp(frontend.Zone().Items()), frontend.Zone().MaxHp());
			h.RunUntil([] { return false; }, 500);
			h.Snapshot("9b-zone-target");

			h.Key(InputEvent::Key::Enter); // camp
			h.Type("/camp");
			h.Key(InputEvent::Key::Enter);
			h.RunUntil([&] { return frontend.DialogOpen(); }, 45000);
			printf("[preview] dialog: %s\n", frontend.DialogText().c_str());
			for (const auto &line : frontend.Chat().Lines())
				printf("[preview] chat %06x %s\n", line.rgb, line.text.c_str());
			h.Snapshot("10-camped");
		}
		else {
			h.Click(2 + 4 + 40, 20 + 4 + 105 + 16); // Char1_Button: an empty slot
			printf("[preview] dialog: %s\n", frontend.DialogText().c_str());
			h.Snapshot("8-empty-slot");
			h.Key(InputEvent::Key::Enter);
			h.Key(InputEvent::Key::Escape); // Quit back to the start screen
			printf("[preview] after Esc: screen %d\n", static_cast<int>(frontend.CurrentScreen()));
		}
	}

	for (const auto &name : art.Missing())
		printf("[preview] missing art: %s\n", name.c_str());
	for (const auto &name : textures.Missing())
		printf("[preview] missing UI texture: %s\n", name.c_str());
	return 0;
}
