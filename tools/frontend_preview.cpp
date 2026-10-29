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
// into the chat window, targets a spawn and prints the chat; then links an item into the chat line
// with Shift+click, sends it, clicks the link in the chat (OP_ItemLinkResponse) and links the item
// display's item from its icon.

#include "animations.h"
#include "eqstr.h"
#include "spell_file.h"
#include "frontend.h"
#include "inventory.h"
#include "opcodes.h"
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
			{
				// The stats the inventory window's labels show (EQTypes 5..16 and 22..25).
				GameValues values;
				values.zone = &frontend.Zone();
				std::string shown[11];
				uint32_t color = 0;
				const int types[11] = {5, 6, 7, 8, 9, 10, 11, 22, 23, 24, 25};
				for (int i = 0; i < 11; ++i)
					values.Label(types[i], shown[i], color);
				printf("[preview] STR %s STA %s DEX %s AGI %s WIS %s INT %s CHA %s  AC %s ATK %s  weight %s / %s\n",
					shown[0].c_str(), shown[1].c_str(), shown[2].c_str(), shown[3].c_str(), shown[4].c_str(),
					shown[5].c_str(), shown[6].c_str(), shown[7].c_str(), shown[8].c_str(), shown[9].c_str(),
					shown[10].c_str());
				std::string resist[5];
				for (int i = 0; i < 5; ++i)
					values.Label(12 + i, resist[i], color);
				printf("[preview] PR %s DR %s FR %s CR %s MR %s\n", resist[0].c_str(), resist[1].c_str(),
					resist[2].c_str(), resist[3].c_str(), resist[4].c_str());
			}
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
			// A bag: the right button on it opens its window (its slots, Done, its size), an item
			// goes from the inventory into it and back out, then Done closes it; Escape closes them all.
			int bag = -1;
			for (const auto &[slot, item] : frontend.Zone().Items())
				if (slot >= Inventory::FirstGeneral && slot <= Inventory::LastGeneral && item.item_class == 1 && bag < 0)
					bag = slot;
			// Without one, a backpack (8 slots) in the first empty general slot: the replay's zone
			// hears nothing of it.
			for (int16_t slot = Inventory::FirstGeneral; slot <= Inventory::LastGeneral && bag < 0; ++slot)
				if (!frontend.Zone().Items().count(slot)) {
					ZoneItem backpack;
					backpack.slot = slot;
					backpack.id = 17005;
					backpack.item_class = 1;
					backpack.name = "Backpack";
					backpack.icon = 565;
					backpack.size = 2;
					backpack.bag_slots = 8;
					backpack.bag_size = 2;
					backpack.bag_type = 1;
					const_cast<ZoneSession &>(frontend.Zone()).EditItems()[slot] = backpack;
					bag = slot;
				}
			if (bag >= 0) {
				const SidlWindow::Control *c = inventory->Find("InvSlot" + std::to_string(bag));
				int x = c->rect.x + c->rect.w / 2, y = c->rect.y + c->rect.h / 2;
				frontend.OnEvent({InputEvent::Type::MouseMove, x, y});
				frontend.OnEvent({InputEvent::Type::RightDown, x, y});
				frontend.OnEvent({InputEvent::Type::RightUp, x, y});
				frontend.Update(NowMs());
				std::string name = Containers::WindowName(static_cast<int16_t>(bag));
				if (const SidlWindow *bag_wnd = where(name.c_str())) {
					const Rect &r = bag_wnd->Bounds();
					printf("[preview] %s '%s': %dx%d at %d,%d\n", name.c_str(), bag_wnd->Root().text.c_str(), r.w, r.h, r.x, r.y);
					for (int n = 1; n <= 10; ++n)
						if (const SidlWindow::Control *s = bag_wnd->Find("ContainerSlot" + std::to_string(n)); s && s->visible)
							printf("[preview]   ContainerSlot%d: item slot %d, at %d,%d, '%s'\n", n, s->item_slot, s->rect.x - r.x,
								s->rect.y - r.y, s->tooltip.c_str());
					for (const char *id : {"Container_Combine", "DoneButton"})
						if (const SidlWindow::Control *b = bag_wnd->Find(id))
							printf("[preview]   %s: %s, at %d,%d\n", id, b->visible ? "shown" : "hidden", b->rect.x - r.x,
								b->rect.y - r.y);
					h.RunUntil([] { return false; }, 200);
					h.Snapshot("w1-bag");
					auto bag_click = [&](const SidlWindow *wnd, const std::string &id) {
						if (const SidlWindow::Control *s = wnd->Find(id))
							click(s->rect.x + s->rect.w / 2, s->rect.y + s->rect.h / 2);
					};
					int lone_now = -1;
					for (const auto &[slot, item] : frontend.Zone().Items())
						if (slot >= Inventory::FirstGeneral && slot <= Inventory::LastGeneral && item.item_class == 0 &&
						    !Inventory::Stacks(item) && lone_now < 0)
							lone_now = slot;
					if (lone_now >= 0) {
						slot_click(lone_now, false);
						bag_click(bag_wnd, "ContainerSlot1");
						items("into the bag");
						bag_click(bag_wnd, "ContainerSlot1");
						slot_click(lone_now, false);
						items("back out");
					}
					bag_click(bag_wnd, "DoneButton");
					where(name.c_str());
					frontend.OnEvent({InputEvent::Type::RightDown, x, y});
					frontend.OnEvent({InputEvent::Type::RightUp, x, y});
					frontend.Update(NowMs());
					where(name.c_str());
					h.Key(InputEvent::Key::Escape); // the bags close before the inventory does
					where(name.c_str());
					where("InventoryWindow");
				}
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

		// The bank: stand by a banker, find it on the screen and right click it; its window, its
		// coins and the inventory come up; an item goes into a bank slot and back; coins (some put
		// in our purse, as the replay's zone hears nothing) go into the bank as gold, changed at
		// the bank; then walking away closes it.
		{
			const ZoneSpawn *banker = nullptr;
			for (const auto &[id, spawn] : frontend.Zone().Spawns())
				if (spawn.npc == 1 && spawn.class_id == 16 && !banker)
					banker = &spawn;
			ZoneView *view = frontend.ZoneScene();
			if (banker && view) {
				uint16_t banker_id = banker->id;
				windows.Show("InventoryWindow", false);
				view->SetPlayerPosition(banker->x, banker->y - 12, banker->z, 0);
				h.RunUntil([] { return false; }, 1500);
				h.Snapshot("k0-at-banker"); // rendered: PickSpawn looks at the last frame
				int bx = -1, by = -1;
				for (int y = 8; y < frontend.ScreenHeight() && bx < 0; y += 8)
					for (int x = 8; x < frontend.ScreenWidth() && bx < 0; x += 8)
						if (view->PickSpawn(x, y, frontend.Zone().MySpawnId()) == banker_id && !windows.Over(x, y))
							bx = x, by = y;
				printf("[preview] banker %s (%d) on the screen at %d,%d, %.1f away\n", banker->name.c_str(), banker_id, bx, by,
					frontend.Zone().SpawnDistance(banker_id));
				size_t chat_before = frontend.Chat().Lines().size();
				if (bx >= 0) {
					frontend.OnEvent({InputEvent::Type::MouseMove, bx, by});
					frontend.OnEvent({InputEvent::Type::RightDown, bx, by});
					frontend.OnEvent({InputEvent::Type::RightUp, bx, by});
					frontend.Update(NowMs());
					if (const SidlWindow *bank = where("BankWnd")) {
						where("InventoryWindow");
						auto text_of = [&](const char *id) {
							const SidlWindow::Control *c = bank->Find(id);
							return c ? c->text : std::string("(none)");
						};
						h.RunUntil([] { return false; }, 200);
						printf("[preview] bank: '%s', coins %s %s %s %s\n", text_of("BW_BankerName").c_str(),
							text_of("BW_Money0").c_str(), text_of("BW_Money1").c_str(), text_of("BW_Money2").c_str(),
							text_of("BW_Money3").c_str());
						h.Snapshot("k1-bank");
						auto control_click = [&](const char *window, const std::string &id) {
							const SidlWindow *wnd = windows.Find(window);
							if (const SidlWindow::Control *c = wnd ? wnd->Find(id) : nullptr)
								click(c->rect.x + c->rect.w / 2, c->rect.y + c->rect.h / 2);
						};
						int lone = -1;
						for (const auto &[slot, item] : frontend.Zone().Items())
							if (slot >= Inventory::FirstGeneral && slot <= Inventory::LastGeneral && item.item_class == 0 &&
							    !Inventory::Stacks(item) && lone < 0)
								lone = slot;
						auto bank_items = [&](const char *when) {
							printf("[preview] bank %s:", when);
							for (const auto &[slot, item] : frontend.Zone().Items())
								if (slot >= Inventory::FirstBank)
									printf(" %d=%s", slot, item.name.c_str());
							printf("\n");
						};
						if (lone >= 0) {
							control_click("InventoryWindow", "InvSlot" + std::to_string(lone));
							control_click("BankWnd", "BW_BankSlot0");
							bank_items("an item in");
							control_click("BankWnd", "BW_BankSlot0");
							control_click("InventoryWindow", "InvSlot" + std::to_string(lone));
							bank_items("and out");
						}
						ZoneProfile &me = const_cast<ZoneSession &>(frontend.Zone()).EditProfile();
						me.copper += 250;
						auto coins = [&](const char *when) {
							printf("[preview] coins %s: purse %d %d %d %d, bank %d %d %d %d, cursor %d %d %d %d\n", when, me.platinum,
								me.gold, me.silver, me.copper, me.bank_platinum, me.bank_gold, me.bank_silver, me.bank_copper,
								me.cursor_platinum, me.cursor_gold, me.cursor_silver, me.cursor_copper);
						};
						coins("before");
						control_click("InventoryWindow", "IW_Money3");
						if (where("QuantityWnd")) {
							h.Snapshot("k2-coin-quantity");
							h.Key(InputEvent::Key::Enter);
						}
						coins("all the copper on the cursor");
						frontend.OnEvent({InputEvent::Type::MouseMove, 600, 400});
						h.RunUntil([] { return false; }, 200);
						h.Snapshot("k3-coins-on-cursor");
						control_click("BankWnd", "BW_Money1");
						coins("into the bank as gold");
						frontend.Update(NowMs());
						printf("[preview] bank coins %s %s %s %s\n", text_of("BW_Money0").c_str(), text_of("BW_Money1").c_str(),
							text_of("BW_Money2").c_str(), text_of("BW_Money3").c_str());
						view->SetPlayerPosition(banker->x, banker->y - 60, banker->z, 0);
						h.RunUntil([] { return false; }, 800);
						where("BankWnd");
						where("InventoryWindow");
					}
				}
				for (size_t i = chat_before; i < frontend.Chat().Lines().size(); ++i)
					printf("[preview] bank chat: %s\n", frontend.Chat().Lines()[i].text.c_str());
			}
		}

		// Item links: Shift+click an item into the chat line while typing (printed, the tags'
		// markers as '|'), send it, then light the link in the chat under the mouse and click it
		// (OP_ItemLinkResponse goes out); and the item display's icon links its item.
		auto input_text = [&] {
			const SidlWindow *chat = windows.Find("ChatWindow");
			const SidlWindow::Control *input = chat ? chat->Find("CWChatInput") : nullptr;
			std::string text = input ? input->text : "";
			std::replace(text.begin(), text.end(), ItemTags::Marker, '|');
			return text;
		};
		windows.Show("InventoryWindow", true);
		windows.Raise("InventoryWindow");
		const SidlWindow::Control *linked_slot = nullptr;
		if (SidlWindow *inventory = windows.Find("InventoryWindow")) {
			h.Key(InputEvent::Key::Enter);
			h.Type("look at ");
			for (int slot = 1; slot <= 29 && !linked_slot; ++slot) {
				const SidlWindow::Control *c = inventory->Find("InvSlot" + std::to_string(slot));
				if (!c || c->cell < 0)
					continue;
				InputEvent event{InputEvent::Type::MouseMove, c->rect.x + c->rect.w / 2, c->rect.y + c->rect.h / 2};
				frontend.OnEvent(event);
				event.shift = true;
				event.type = InputEvent::Type::MouseDown;
				frontend.OnEvent(event);
				event.type = InputEvent::Type::MouseUp;
				frontend.OnEvent(event);
				frontend.Update(NowMs());
				linked_slot = c;
			}
			h.Type(" ok");
			printf("[preview] chat input with a link: '%s'\n", input_text().c_str());
			h.RunUntil([] { return false; }, 300);
			h.Snapshot("l1-link-input");
			h.Key(InputEvent::Key::Enter);
			frontend.Update(NowMs());
		}
		windows.Show("InventoryWindow", false);
		auto link_clicks = [&] {
			int count = 0;
			for (const auto &packet : frontend.Zone().RecentPackets())
				if (packet.outgoing && packet.opcode == ZoneOp::ItemLinkResponse)
					count += static_cast<int>(packet.count);
			return count;
		};
		if (const SidlWindow *chat = windows.Find("ChatWindow"); chat && linked_slot) {
			Rect b = chat->Bounds(); // taller first, from its top border: the sizing above left it a title bar
			drag(b.x + b.w / 2, b.y + 1, b.x + b.w / 2, b.y - 160);
			where("ChatWindow");
			const SidlWindow::Control *output = chat->Find("CWChatOutput");
			Rect area = output ? chat->ContentRect(*output) : Rect{};
			InputEvent wheel; // back down to the newest line
			wheel.type = InputEvent::Type::Wheel;
			wheel.x = area.x + area.w / 2;
			wheel.y = area.y + area.h / 2;
			wheel.wheel = -10;
			frontend.OnEvent(wheel);
			h.Snapshot("l2-link-chat"); // drawn: the chat's rows laid out for its size
			int before = link_clicks();
			bool found = false;
			for (int y = area.Bottom() - 4; y > area.y && !found; y -= 3)
				for (int x = area.x + 2; x < area.Right() && !found; x += 3) {
					click(x, y);
					if (link_clicks() > before) {
						found = true;
						frontend.OnEvent({InputEvent::Type::MouseMove, x, y});
						h.RunUntil([] { return false; }, 300);
						h.Snapshot("l3-link-hover");
						printf("[preview] clicked the chat's link at %d,%d: OP_ItemLinkResponse sent\n", x, y);
					}
				}
			if (!found)
				printf("[preview] no link found to click in the chat\n");
			frontend.OnEvent({InputEvent::Type::MouseMove, 2, 2});
		}
		// The item display's icon: the alt+clicked item's tag in the chat line.
		windows.Show("InventoryWindow", true);
		windows.Raise("InventoryWindow");
		if (linked_slot) {
			InputEvent event{InputEvent::Type::MouseMove, linked_slot->rect.x + 4, linked_slot->rect.y + 4};
			frontend.OnEvent(event);
			event.alt = true;
			event.type = InputEvent::Type::MouseDown;
			frontend.OnEvent(event);
			event.type = InputEvent::Type::MouseUp;
			frontend.OnEvent(event);
			frontend.Update(NowMs());
			if (const SidlWindow *display = windows.Find("ItemDisplayWindow"))
				if (const SidlWindow::Control *icon = display->Find("IconButton")) {
					click(icon->rect.x + icon->rect.w / 2, icon->rect.y + icon->rect.h / 2);
					printf("[preview] chat input after the item display's icon: '%s'\n", input_text().c_str());
				}
			h.Key(InputEvent::Key::Escape);
		}
		for (const auto &line : frontend.Chat().Lines())
			if (!line.links.empty())
				printf("[preview] chat line with %zu link(s): %s\n", line.links.size(), line.text.c_str());

		// The spellbook, for a caster (the replay answers what we ask as the zone would): the gems'
		// book button opens it, sitting; two right clicks swap the first two slots; a click puts the
		// first spell on the cursor and a click on an empty gem memorizes it; a right click on the
		// gem forgets it; a scroll from the inventory scribes into the first empty slot; a right
		// click and Delete ask to delete it, and Yes does; Done closes the book and stands us up.
		const ZoneProfile &me = frontend.Zone().Profile();
		auto spell_name = [&](int id) {
			const SpellInfo *spell = spells.Find(id);
			return spell ? spell->name : std::string(id == -1 ? "-" : "?");
		};
		auto book = [&](const char *when) {
			printf("[preview] book %s: stance %d, open %d, cursor spell %d;", when, frontend.Zone().Self().stance,
				frontend.SpellBookState().Activated(), frontend.WindowState().cursor_spell);
			for (int i = 0; i < 10; ++i)
				printf(" %d=%s", i, spell_name(me.spell_book[i]).c_str());
			printf("; gems:");
			for (int i = 0; i < ZoneProfile::Gems; ++i)
				printf(" %s", spell_name(me.mem_spells[i]).c_str());
			printf("\n");
		};
		auto run_until = [&](auto done, int ms) { return h.RunUntil(done, ms); };
		int x, y;
		book("at first");
		if (at("CastSpellWnd", "CSPW_SpellBook", x, y)) {
			click(x, y);
			h.RunUntil([] { return false; }, 300);
			book("opened");
			h.Snapshot("b1-book");
			int bx, by, cx, cy;
			if (frontend.SpellBookState().Activated() && at("SpellBookWnd", "SBW_Spell0", bx, by) &&
			    at("SpellBookWnd", "SBW_Spell1", cx, cy)) {
				right_click(bx, by);
				h.RunUntil([] { return false; }, 100);
				h.Snapshot("b2-selected");
				right_click(cx, cy);
				h.RunUntil([] { return false; }, 100);
				book("swapped 0 and 1");
				right_click(bx, by);
				right_click(cx, cy);
				h.RunUntil([] { return false; }, 100);
				book("swapped back");
				// Memorizing: into the first empty gem.
				int gem = -1;
				for (int i = 0; i < ZoneProfile::Gems && gem < 0; ++i)
					if (me.mem_spells[i] == -1)
						gem = i;
				click(bx, by);
				frontend.OnEvent({InputEvent::Type::MouseMove, 600, 400});
				h.RunUntil([] { return false; }, 100);
				book("spell picked up");
				h.Snapshot("b3-spell-on-cursor");
				int gx, gy;
				if (gem >= 0 && me.spell_book[0] != -1 && at("CastSpellWnd", "CSPW_Spell" + std::to_string(gem), gx, gy)) {
					click(gx, gy);
					h.RunUntil([] { return false; }, 3000);
					h.Snapshot("b4-memorizing");
					bool done = run_until([&] { return me.mem_spells[gem] != -1; }, 70000);
					book(done ? "memorized" : "memorize timed out");
					// Dim until it can be cast (a second and its recast time after), then bright again.
					auto fade = [&] {
						const SidlWindow::Control *c = windows.Find("CastSpellWnd")->Find("CSPW_Spell" + std::to_string(gem));
						return c ? c->gem_fade : -1;
					};
					h.RunUntil([] { return false; }, 400);
					printf("[preview] gem %d fade just memorized: %d\n", gem, fade());
					h.Snapshot("b5-memorized");
					h.RunUntil([] { return false; }, 4000);
					printf("[preview] gem %d fade 4 s later: %d\n", gem, fade());
					h.Snapshot("b5-memorized-ready");
					// Tooltips: the gem's after the mouse rests on it 500 ms, and every gem's with Alt held.
					auto tip = [&](const char *screen, int tx, int ty) {
						const SidlWindow *w = windows.Find(screen);
						const SidlWindow::Control *c = w ? w->ControlAt(tx, ty) : nullptr;
						return c ? c->tooltip : std::string("(no control)");
					};
					frontend.OnEvent({InputEvent::Type::MouseMove, 700, 500});
					h.RunUntil([] { return false; }, 100);
					frontend.OnEvent({InputEvent::Type::MouseMove, gx, gy});
					h.RunUntil([] { return false; }, 300);
					h.Snapshot("t1-gem-300ms");
					h.RunUntil([] { return false; }, 400);
					printf("[preview] tooltip on gem %d: '%s'\n", gem, tip("CastSpellWnd", gx, gy).c_str());
					h.Snapshot("t2-gem-700ms");
					h.Key(InputEvent::Key::Alt);
					frontend.OnEvent({InputEvent::Type::MouseMove, 700, 500});
					h.RunUntil([] { return false; }, 100);
					h.Snapshot("t3-gems-alt");
					h.KeyUp(InputEvent::Key::Alt);
					right_click(gx, gy);
					h.RunUntil([] { return false; }, 200);
					book("gem right clicked");
				}
				// Scribing: the first scroll in the general slots onto the cursor, then an empty slot.
				int scroll = -1;
				for (const auto &[slot, item] : frontend.Zone().Items())
					if (slot >= Inventory::FirstGeneral && slot <= Inventory::LastGeneral && item.item_type == 20 && scroll < 0)
						scroll = slot;
				int empty = -1;
				for (int i = 0; i < 16 && empty < 0; ++i)
					if (me.spell_book[i] == -1)
						empty = i;
				SidlWindow *inventory = windows.Find("InventoryWindow");
				if (scroll >= 0 && empty >= 0 && inventory) {
					windows.Show("InventoryWindow", true);
					windows.Raise("InventoryWindow");
					frontend.Update(NowMs());
					if (const SidlWindow::Control *c = inventory->Find("InvSlot" + std::to_string(scroll)))
						click(c->rect.x + c->rect.w / 2, c->rect.y + c->rect.h / 2);
					windows.Show("InventoryWindow", false);
					windows.Raise("SpellBookWnd");
					frontend.Update(NowMs());
					printf("[preview] scroll from slot %d on the cursor: %d\n", scroll,
						static_cast<int>(frontend.Zone().Items().count(Inventory::Cursor)));
					int ex, ey;
					if (at("SpellBookWnd", "SBW_Spell" + std::to_string(empty), ex, ey)) {
						click(ex, ey);
						h.RunUntil([] { return false; }, 3000);
						h.Snapshot("b6-scribing");
						bool done = run_until([&] { return me.spell_book[empty] != -1; }, 70000);
						h.RunUntil([] { return false; }, 200);
						book(done ? "scribed" : "scribe timed out");
						printf("[preview] the cursor after scribing: %d\n",
							static_cast<int>(frontend.Zone().Items().count(Inventory::Cursor)));
						h.Snapshot("b7-scribed");
						// Deleting it: selected, Delete, Yes.
						right_click(ex, ey);
						h.Key(InputEvent::Key::Delete);
						h.RunUntil([] { return false; }, 200);
						where("ConfirmationDialogBox");
						h.Snapshot("b8-confirm-delete");
						int yx, yy;
						if (at("ConfirmationDialogBox", "Yes_Button", yx, yy))
							click(yx, yy);
						h.RunUntil([] { return false; }, 200);
						where("ConfirmationDialogBox");
						book("deleted");
					}
				}
				else
					printf("[preview] no scroll (%d) or empty slot (%d) to scribe with\n", scroll, empty);
				// Inventory tooltips: an item's name, an empty worn slot's name.
				if (inventory) {
					windows.Show("InventoryWindow", true);
					windows.Raise("InventoryWindow");
					frontend.Update(NowMs());
					for (int slot : {1, 13, 22, 23, 29}) {
						const SidlWindow::Control *c = inventory->Find("InvSlot" + std::to_string(slot));
						if (!c)
							continue;
						int ix = c->rect.x + c->rect.w / 2, iy = c->rect.y + c->rect.h / 2;
						const SidlWindow::Control *under = inventory->ControlAt(ix, iy);
						printf("[preview] tooltip on InvSlot%d: '%s'\n", slot, under ? under->tooltip.c_str() : "(none)");
						if (slot == 22) {
							frontend.OnEvent({InputEvent::Type::MouseMove, ix, iy});
							h.RunUntil([] { return false; }, 700);
							h.Snapshot("t4-inventory-item");
						}
					}
					windows.Show("InventoryWindow", false);
				}
				int dx, dy;
				if (at("SpellBookWnd", "DoneButton", dx, dy))
					click(dx, dy);
				h.RunUntil([] { return false; }, 1500);
				book("done");
				h.Snapshot("b9-closed");
			}
		}
		// Casting from the gems (a caster with spells memorized; the replay answers as the zone
		// would): with the nearest NPC targeted the gem casts, the casting window up and emptying;
		// once it's gone off the window goes and the gems dim for the recovery, and a click then
		// hasn't recovered; /cast 9 shows the format. On the Qeynos dock, so there's a floor to
		// stand on and see from (a new character falls through South Qeynos at 0, 0, 0).
		{
			if (ZoneView *view = frontend.ZoneScene(); view && frontend.Zone().Info().short_name == "qeynos") {
				view->SetPlayerPosition(0, 10, 5, 0);
				h.RunUntil([] { return false; }, 1500);
			}
			const ZoneProfile &self = frontend.Zone().Profile();
			int gem = -1;
			for (int i = 0; i < ZoneProfile::Gems && gem < 0; ++i)
				if (spells.Find(self.mem_spells[i]))
					gem = i;
			int gx, gy;
			if (gem >= 0 && at("CastSpellWnd", "CSPW_Spell" + std::to_string(gem), gx, gy)) {
				auto last_line = [&] {
					const auto &lines = frontend.Chat().Lines();
					return lines.empty() ? std::string() : lines.back().text;
				};
				auto cast_report = [&](const char *when) {
					const SidlWindow *bar = windows.Find("CastingWindow");
					const SidlWindow *gems = windows.Find("CastSpellWnd");
					printf("[preview] cast %s: casting window %d, mana %d, gem fades", when,
						bar && windows.Shown("CastingWindow"), self.mana);
					for (int i = 0; gems && i < ZoneProfile::Gems; ++i)
						if (const SidlWindow::Control *c = gems->Find("CSPW_Spell" + std::to_string(i)))
							printf(" %d", c->gem_fade);
					printf("; chat: '%s'\n", last_line().c_str());
				};
				const SpellInfo *spell = spells.Find(self.mem_spells[gem]);
				printf("[preview] casting gem %d: %s (%d ms, %d mana, recovery %d ms)\n", gem + 1, spell->name.c_str(),
					spell->cast_ms, spell->mana, spell->recovery_ms);
				// The nearest NPC (no invisible markers), and us standing 8 away from it where we can see it.
				const ZoneSpawn *nearest = nullptr;
				float best = 0;
				for (const auto &[id, spawn] : frontend.Zone().Spawns()) {
					float d = std::hypot(spawn.x - self.x, spawn.y - self.y);
					if (spawn.npc == 1 && spawn.race != 127 && (!nearest || d < best)) {
						nearest = &spawn;
						best = d;
					}
				}
				if (nearest && frontend.ZoneScene()) {
					frontend.ZoneScene()->SetPlayerPosition(nearest->x + 8, nearest->y, nearest->z + 2, 0);
					h.RunUntil([] { return false; }, 1500);
					best = std::hypot(nearest->x - self.x, nearest->y - self.y);
					printf("[preview] standing by it, can see it: %d\n",
						frontend.ZoneScene()->CanSee({self.y, self.x, self.z}, {nearest->y, nearest->x, nearest->z}));
				}
				if (nearest) {
					printf("[preview] targeting %s, %.0f away\n", nearest->name.c_str(), best);
					h.Key(InputEvent::Key::Enter);
					h.Type("/target " + nearest->name);
					h.Key(InputEvent::Key::Enter);
					h.RunUntil([] { return false; }, 300);
					click(gx, gy);
					h.RunUntil([] { return false; }, spell->cast_ms / 3);
					cast_report("a third in");
					h.Snapshot("c1-casting");
					h.RunUntil([&] { return !windows.Shown("CastingWindow"); }, spell->cast_ms + 2000);
					h.RunUntil([] { return false; }, 300);
					cast_report("gone off");
					h.Snapshot("c2-gone-off");
					click(gx, gy);
					h.RunUntil([] { return false; }, 100);
					cast_report("clicked again");
					h.RunUntil([] { return false; }, spell->recovery_ms + 1000);
					cast_report("recovered");
					h.Snapshot("c3-recovered");
				}
				h.Key(InputEvent::Key::Enter);
				h.Type("/cast 9");
				h.Key(InputEvent::Key::Enter);
				h.RunUntil([] { return false; }, 100);
				cast_report("/cast 9");
			}
		}
		// Sitting: /sit lowers the first person view with our head; Space, the back key and turning
		// with the right button do nothing; W stands us up and we run at once, getting up playing out.
		if (ZoneView *view = frontend.ZoneScene()) {
			auto sit_report = [&](const char *when) {
				ZoneMovement m = view->Movement();
				printf("[preview] sitting %s: stance %d, standing up %d, eye drop %.2f, at %.2f,%.2f,%.2f\n", when,
					frontend.Zone().Self().stance, view->StandingUp(), view->EyeDrop(), m.x, m.y, m.z);
			};
			// On the Qeynos dock: a new character starts at 0, 0, 0 in South Qeynos, over nothing.
			if (frontend.Zone().Info().short_name == "qeynos") {
				view->SetPlayerPosition(0, 10, 5, 0);
				h.RunUntil([] { return false; }, 1500);
			}
			sit_report("standing");
			while (view->View() != ZoneView::CameraView::FirstPerson)
				view->CycleCamera();
			h.Key(InputEvent::Key::Enter);
			h.Type("/sit");
			h.Key(InputEvent::Key::Enter);
			h.RunUntil([] { return false; }, 300);
			sit_report("down 0.3 s");
			h.Snapshot("p1-sitting-first-person");
			h.RunUntil([] { return false; }, 1500);
			sit_report("down 1.8 s");
			h.Snapshot("p2-sat-first-person");
			h.Key(InputEvent::Key::Space);
			h.Key(InputEvent::Key::S);
			h.RunUntil([] { return false; }, 500);
			h.KeyUp(InputEvent::Key::S);
			float heading = view->Movement().heading;
			frontend.OnEvent({InputEvent::Type::MouseMove, 640, 360});
			frontend.OnEvent({InputEvent::Type::RightDown, 640, 360});
			for (int i = 1; i <= 4; ++i)
				frontend.OnEvent({InputEvent::Type::MouseMove, 640 + 40 * i, 360});
			frontend.OnEvent({InputEvent::Type::RightUp, 800, 360});
			frontend.Update(NowMs());
			printf("[preview] sitting right drag: heading %.1f -> %.1f\n", heading, view->Movement().heading);
			sit_report("after Space and S");
			h.Key(InputEvent::Key::W);
			h.RunUntil([] { return false; }, 200);
			sit_report("W pressed 0.2 s");
			h.RunUntil([&] { return !view->StandingUp(); }, 5000);
			sit_report("up");
			h.RunUntil([] { return false; }, 500);
			sit_report("W held 0.5 s more");
			h.KeyUp(InputEvent::Key::W);
			view->CycleCamera();
		}
		for (const auto &line : frontend.Chat().Lines())
			if (line.text.find("pell") != std::string::npos || line.text.find("scrib") != std::string::npos ||
			    line.text.find("memoriz") != std::string::npos || line.text.find("forget") != std::string::npos)
				printf("[preview] chat: %s\n", line.text.c_str());
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
				const StartPoint start = frontend.Creation()->Character().start;
				printf("[preview] creating %s, start zone %u at %.0f, %.0f, %.0f\n", name.c_str(), start.zone, start.y, start.x,
					start.z);
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
			if (const CharSelectAnimWindow *anims = frontend.AnimationsWindow()) {
				// The Animations window: the second row double clicked plays it; then Play on the
				// third, and All + emotes.
				const SidlWindow &window = anims->Window();
				const SidlWindow::Control *list = window.Find("AnimList");
				const SidlWindow::Control *play = window.Find("PlayButton");
				const SidlWindow::Control *all = window.Find("AllButton");
				if (list && play && all) {
					printf("[preview] animations window: %zu animations\n", list->rows.size());
					Rect content = window.ContentRect(*list);
					int font = SidlFontHeight(list->def->font);
					int y = content.y + font + 4 + (font + 2) + (font + 2) / 2;
					Rect area = frontend.ScreenArea();
					frontend.OnEvent({InputEvent::Type::MouseMove, area.x + content.x + 20, area.y + y});
					frontend.OnEvent({InputEvent::Type::MouseDown, area.x + content.x + 20, area.y + y});
					frontend.OnEvent({InputEvent::Type::MouseUp, area.x + content.x + 20, area.y + y});
					frontend.OnEvent({InputEvent::Type::DoubleClick, area.x + content.x + 20, area.y + y});
					frontend.OnEvent({InputEvent::Type::MouseUp, area.x + content.x + 20, area.y + y});
					// The scene takes the request as it next draws.
					h.RunUntil([] { return false; }, 300);
					h.Snapshot("7-character-select-animation");
					printf("[preview] double clicked %s: playing %s\n", render::AnimationCode(anims->Selected()),
						render::AnimationCode(frontend.CharacterScene()->Animation()));
					// Play on the next row down.
					y += font + 2;
					h.Click(content.x + 20, y);
					h.Click(play->rect.x + play->rect.w / 2, play->rect.y + play->rect.h / 2);
					h.Snapshot("7-character-select-play");
					printf("[preview] Play %s: playing %s\n", render::AnimationCode(anims->Selected()),
						render::AnimationCode(frontend.CharacterScene()->Animation()));
					h.Click(all->rect.x + all->rect.w / 2, all->rect.y + all->rect.h / 2);
					printf("[preview] All + emotes: %d (CharSelectEmotes %d)\n", anims->AllAnimations(), frontend.CharSelectEmotes());
				}
			}
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

			// /consider the guard (the zone answers), /time, /played, /motd, /afk and /anonymous
			// on and off, and /assist the guard (who has no target: nothing changes).
			for (const char *line : {"/consider", "/time", "/played", "/motd", "/afk", "/afk", "/anonymous", "/anonymous",
				     "/assist"}) {
				h.Key(InputEvent::Key::Enter);
				h.Type(line);
				h.Key(InputEvent::Key::Enter);
				h.RunUntil([] { return false; }, 400);
			}
			h.RunUntil([] { return false; }, 1000);
			printf("[preview] target after /assist: %u\n", frontend.Target());
			h.Snapshot("9c-zone-commands");

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
