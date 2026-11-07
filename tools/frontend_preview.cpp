// frontend_preview (Linux): drives the front end headlessly and writes PNG
// snapshots, to check layout and flow without a Windows box. Text is drawn as
// outline boxes of an estimated Arial size, since GDI isn't available.
//
//   frontend_preview EQDIR OUTDIR [HOST:PORT USER PASSWORD [charselect|create|shop|ground|commands|group]]
//   frontend_preview EQDIR OUTDIR --replay FILE [SECONDS]
//
// Without a login server it snapshots the start screen, a dialog and the login
// screen. With one it also logs in and snapshots server select and the play result.
// "shop" enters the world, stands by the nearest merchant, opens its shop with the right button,
// sells the first thing it can and buys it back. "ground" lists what's on the ground, drops an item
// (yes to the dialog), finds it on the screen and picks it back up. "commands" types chat commands
// (/random, /lootnodrop, /fastdrop, socials at nobody and at the nearest spawn) and prints the chat,
// then shows the compass as we turn (snapshots, and the strips' places) and the XP gauge. "charselect" stops at character select (two snapshots of the 3D scene, as the
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
#include "ini_file.h"
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

	// Where a spawn is drawn on the screen (window pixels, not under a window): the last frame's
	// PickSpawn, rendered first.
	bool FindOnScreen(FrontEnd &frontend, Harness &h, uint16_t id, int &x, int &y)
	{
		ZoneView *view = frontend.ZoneScene();
		if (!view)
			return false;
		h.Snapshot("find-spawn");
		Rect area = frontend.ScreenArea();
		for (int sy = 8; sy < frontend.ScreenHeight(); sy += 8)
			for (int sx = 8; sx < frontend.ScreenWidth(); sx += 8)
				if (view->PickSpawn(sx, sy, frontend.Zone().MySpawnId()) == id && !frontend.Windows().Over(sx, sy)) {
					x = sx + area.x, y = sy + area.y;
					return true;
				}
		return false;
	}

	// Live: chat commands typed in and what they print; the compass as we turn; the XP gauge.
	int CommandsTest(FrontEnd &frontend, Harness &h)
	{
		WindowManager &windows = frontend.Windows();
		const ZoneSession &zone = frontend.Zone();
		h.Key(InputEvent::Key::F11); // the debug menu out of the way
		size_t chat_seen = frontend.Chat().Lines().size();
		auto chat = [&] {
			for (; chat_seen < frontend.Chat().Lines().size(); ++chat_seen)
				printf("[preview] chat: %s\n", frontend.Chat().Lines()[chat_seen].text.c_str());
		};
		auto type = [&](const std::string &line) {
			printf("[preview] > %s\n", line.c_str());
			h.Key(InputEvent::Key::Enter);
			h.Type(line);
			h.Key(InputEvent::Key::Enter);
			h.RunUntil([] { return false; }, 700);
			chat();
		};
		chat();
		for (const char *line : {"/random", "/random 50 10", "/random -1 5", "/random 1234567", "/random 40000",
			"/lootnodrop", "/lootnodrop sometimes", "/lootnodrop", "/lootnodrop always", "/fastdrop", "/fastdrop never",
			"/fastdrop off", "/smile", "/wave", "/hug", "/boggle", "/tap", "/fidget", "/smile nobodyhere", "/flipoff",
			"/kiss blows you a kiss"})
			type(line);
		// The nearest spawn, by name.
		const ZoneProfile &me = zone.Profile();
		const ZoneSpawn *near = nullptr;
		float best = 0;
		for (const auto &[id, spawn] : zone.Spawns()) {
			float d = std::hypot(spawn.x - me.x, spawn.y - me.y);
			if (spawn.npc < 2 && (!near || d < best)) {
				near = &spawn;
				best = d;
			}
		}
		if (near) {
			printf("[preview] nearest: %s at %.0f\n", near->name.c_str(), best);
			std::string first = near->name.substr(0, near->name.find('_'));
			for (const char *social : {"/bow ", "/tap ", "/yawn ", "/dance "})
				type(social + first);
			// DoPercentConvert: the target's name, pronouns and race, and our pet (none: "Pet").
			type("/target " + first);
			type("%t is %s, %o, %p, a %r; %m. x%t 50% '%t'");
			type("/smile say waves at %t");
		}
		// The keys that open the chat line with something in it (their own character not typed
		// after), and Target myself twice (no pet: still us).
		auto press = [&](int scan, bool shift, char ch, const char *what) {
			InputEvent down;
			down.type = InputEvent::Type::Key;
			down.scan = scan;
			down.shift = shift;
			frontend.OnEvent(down);
			InputEvent typed;
			typed.type = InputEvent::Type::Char;
			typed.ch = static_cast<unsigned char>(ch);
			frontend.OnEvent(typed);
			InputEvent up = down;
			up.type = InputEvent::Type::KeyUp;
			frontend.OnEvent(up);
			h.RunUntil([] { return false; }, 200);
			printf("[preview] %s: typing %d, line \"%s\"\n", what, frontend.Chat().Typing() ? 1 : 0,
				frontend.Chat().InputText().c_str());
			chat();
			if (frontend.Chat().Typing())
				h.Key(InputEvent::Key::Escape);
		};
		press(0x14, false, 't', "Send tell (T)");
		press(0x28, false, '\'', "Command mode say (')");
		press(0x27, true, ':', "Command mode emote (:)");
		press(0x13, false, 'r', "Reply (R)");
		press(0x3b, false, 0, "Target myself (F1)");
		printf("[preview] target %u (us %u)\n", frontend.Target(), zone.MySpawnId());
		press(0x3b, false, 0, "Target myself again (F1)");
		printf("[preview] target %u (us %u)\n", frontend.Target(), zone.MySpawnId());
		printf("[preview] exp %u (gauge %d)\n", me.exp, static_cast<int>(std::min(me.exp * 3.03031, 1000.0)));

		// The right button on food and drink in the inventory window (CInvSlot_DoDrinkEatPoison).
		windows.Show("InventoryWindow", true);
		h.RunUntil([] { return false; }, 300);
		for (int16_t slot = 1; slot < 31; ++slot) {
			auto it = zone.Items().find(slot);
			if (it == zone.Items().end())
				continue;
			const ZoneItem &item = it->second;
			printf("[preview] slot %d: %s (class %u, type %u, charges %d, stackable %u, click %d spell %d)\n", slot, item.name.c_str(),
				item.item_class, item.item_type, item.charges, item.stackable, item.effect_type2, item.effect2);
		}
		// The right button on a note reads it (OP_ReadBook, CNoteWnd).
		for (const auto &[slot, item] : zone.Items())
			if (slot >= 22 && slot < 31 && item.item_class == 2) {
				const SidlWindow *inv = windows.Find("InventoryWindow");
				const SidlWindow::Control *c = inv ? inv->Find("InvSlot" + std::to_string(slot)) : nullptr;
				if (!c)
					break;
				Rect area = frontend.ScreenArea();
				int x = area.x + c->rect.x + c->rect.w / 2, y = area.y + c->rect.y + c->rect.h / 2;
				printf("[preview] right click note '%s' (file '%s', type %u)\n", item.name.c_str(), item.book_file.c_str(), item.book_type);
				frontend.OnEvent({InputEvent::Type::MouseMove, x, y});
				frontend.OnEvent({InputEvent::Type::RightDown, x, y});
				frontend.OnEvent({InputEvent::Type::RightUp, x, y});
				h.RunUntil([&] { return windows.Shown("NoteWindow"); }, 3000);
				const SidlWindow *note = windows.Find("NoteWindow");
				const SidlWindow::Control *text = note ? note->Find("Note") : nullptr;
				printf("[preview] note window %s: '%s' %s\n", windows.Shown("NoteWindow") ? "up" : "down",
					note ? note->Root().text.c_str() : "", text ? text->text.c_str() : "");
				h.Snapshot("n1-note");
				windows.Show("NoteWindow", false);
				break;
			}
		for (int type : {14, 15, 14}) {
			int16_t found = -1;
			for (const auto &[slot, item] : zone.Items())
				if (slot >= 22 && slot < 31 && item.item_class == 0 && item.item_type == type)
					found = slot;
			if (found < 0)
				continue;
			const SidlWindow *inv = windows.Find("InventoryWindow");
			const SidlWindow::Control *c = inv ? inv->Find("InvSlot" + std::to_string(found)) : nullptr;
			if (!c)
				continue;
			Rect area = frontend.ScreenArea();
			int x = area.x + c->rect.x + c->rect.w / 2, y = area.y + c->rect.y + c->rect.h / 2;
			printf("[preview] right click slot %d (%s): hunger %d thirst %d\n", found, zone.Items().at(found).name.c_str(),
				zone.Profile().hunger, zone.Profile().thirst);
			frontend.OnEvent({InputEvent::Type::MouseMove, x, y});
			frontend.OnEvent({InputEvent::Type::RightDown, x, y});
			frontend.OnEvent({InputEvent::Type::RightUp, x, y});
			h.RunUntil([] { return false; }, 800);
			chat();
			auto after = zone.Items().find(found);
			printf("[preview]   after: hunger %d thirst %d, slot %d %s (%d)\n", zone.Profile().hunger, zone.Profile().thirst, found,
				after == zone.Items().end() ? "empty" : after->second.name.c_str(), after == zone.Items().end() ? 0 : after->second.charges);
		}

		windows.Show("CompassWindow", true);
		windows.MoveTo("CompassWindow", 300, 40);
		for (int turn = 0; turn < 4; ++turn) {
			h.RunUntil([] { return false; }, 400);
			if (const SidlWindow *compass = windows.Find("CompassWindow")) {
				const SidlWindow::Control *a = compass->Find("CompassStrip1"), *b = compass->Find("CompassStrip2");
				printf("[preview] compass: heading %.1f, strips at %d and %d (client %d..%d)\n",
					frontend.ZoneScene() ? frontend.ZoneScene()->Movement().heading : -1.0f, a ? a->rect.x : -1, b ? b->rect.x : -1,
					compass->Root().client.x, compass->Root().client.x + compass->Root().client.w);
			}
			h.Snapshot("c" + std::to_string(turn) + "-compass");
			frontend.OnEvent({InputEvent::Type::RightDown, 600, 300});
			frontend.OnEvent({InputEvent::Type::MouseMove, 480, 300});
			frontend.OnEvent({InputEvent::Type::RightUp, 480, 300});
		}
		type("/camp");
		h.RunUntil([&] { return zone.GetState() != ZoneSession::State::InZone && zone.GetState() != ZoneSession::State::Camping; },
			40000);
		printf("[preview] camped\n");
		return 0;
	}

	// Live, as the account's first character (Replaycaster), with a second player (Claudebot through
	// eqclient --group-invite Replaycaster --group-accept):
	// their invitation followed through the group window, the member's gauge clicked, /disband to
	// leave; then our /invite of them, Target party member 1 and /disband to remove them.
	int GroupTest(FrontEnd &frontend, Harness &h)
	{
		WindowManager &windows = frontend.Windows();
		const ZoneSession &zone = frontend.Zone();
		const Group &group = frontend.GroupState();
		h.Key(InputEvent::Key::F11);
		size_t chat_seen = frontend.Chat().Lines().size();
		auto chat = [&] {
			for (; chat_seen < frontend.Chat().Lines().size(); ++chat_seen)
				printf("[preview] chat: %s\n", frontend.Chat().Lines()[chat_seen].text.c_str());
		};
		auto type = [&](const std::string &line) {
			printf("[preview] > %s\n", line.c_str());
			h.Key(InputEvent::Key::Enter);
			h.Type(line);
			h.Key(InputEvent::Key::Enter);
			h.RunUntil([] { return false; }, 700);
			chat();
		};
		auto click = [&](const char *window, const std::string &id) {
			const SidlWindow *wnd = windows.Find(window);
			const SidlWindow::Control *c = wnd ? wnd->Find(id) : nullptr;
			if (!c) {
				printf("[preview] no %s in %s\n", id.c_str(), window);
				return;
			}
			printf("[preview] click %s.%s\n", window, id.c_str());
			Rect area = frontend.ScreenArea();
			int x = area.x + c->rect.x + c->rect.w / 2, y = area.y + c->rect.y + c->rect.h / 2;
			frontend.OnEvent({InputEvent::Type::MouseMove, x, y});
			frontend.OnEvent({InputEvent::Type::MouseDown, x, y});
			frontend.OnEvent({InputEvent::Type::MouseUp, x, y});
			frontend.Update(NowMs());
		};
		auto state = [&](const char *when) {
			printf("[preview] %s: leader '%s', invited %d, profile list:", when, group.Leader().c_str(), group.Invited() ? 1 : 0);
			for (const auto &name : zone.Profile().group_members)
				printf(" '%s'", name.c_str());
			printf("\n");
			for (int i = 0; i < Group::Members; ++i)
				if (!group.Member(i).empty()) {
					const ZoneSpawn *spawn = group.MemberSpawn(i);
					printf("[preview]   member %d: %s (%s)\n", i + 1, group.Member(i).c_str(),
						spawn ? ("in the zone, hp " + std::to_string(spawn->hp)).c_str() : "not here");
				}
			if (const SidlWindow *wnd = windows.Find("GroupWindow"))
				for (const char *id : {"InviteButton", "DisbandButton", "FollowButton", "DeclineButton"})
					if (const SidlWindow::Control *c = wnd->Find(id))
						printf("[preview]   %s %s%s\n", id, wnd->Shown(*c) ? "shown" : "hidden", c->enabled ? "" : " (disabled)");
		};
		windows.Show("GroupWindow", true);
		windows.MoveTo("GroupWindow", 300, 300);
		h.RunUntil([] { return false; }, 300);
		state("start");
		printf("[preview] waiting for Claudebot\n");
		auto bot = [&]() -> const ZoneSpawn * {
			for (const auto &[id, spawn] : zone.Spawns())
				if (spawn.name == "Claudebot")
					return &spawn;
			return nullptr;
		};
		h.RunUntil([&] { return bot() != nullptr; }, 60000);
		if (const ZoneSpawn *b = bot()) {
			// Next to them, facing them (north).
			frontend.ZoneScene()->SetPlayerPosition(b->x, b->y - 15, b->z, 0);
			h.RunUntil([] { return false; }, 1500);
		}
		printf("[preview] waiting for an invitation\n");
		h.RunUntil([&] { return group.Invited(); }, 60000);
		h.RunUntil([] { return false; }, 300);
		chat();
		state("invited");
		click("GroupWindow", "FollowButton");
		h.RunUntil([] { return false; }, 3000);
		chat();
		state("followed");
		h.Snapshot("g1-grouped");
		click("GroupWindow", "Gauge1");
		printf("[preview] target after the gauge: %u\n", frontend.Target());
		// /who here and in all of EverQuest, and filtered.
		for (const char *line : {"/who", "/who all", "/who all claude", "/who 1 10 human", "/who corpse", "/whotarget"})
			type(line);
		h.RunUntil([] { return false; }, 1500);
		chat();
		// /follow them: from 70 away we go after them and stop by them; Move back ends it.
		type("/follow");
		if (const ZoneSpawn *b = bot()) {
			frontend.ZoneScene()->SetPlayerPosition(b->x, b->y - 70, b->z, 256);
			auto gap = [&] {
				ZoneMovement m = frontend.ZoneScene()->Movement();
				return std::hypot(b->x - m.x, b->y - m.y);
			};
			printf("[preview] following from %.0f away\n", gap());
			for (int i = 0; i < 6; ++i) {
				h.RunUntil([] { return false; }, 1000);
				ZoneMovement m = frontend.ZoneScene()->Movement();
				printf("[preview]   %.0f away, heading %.0f, speed %.2f\n", gap(), m.heading, m.speed);
			}
		}
		InputEvent back;
		back.type = InputEvent::Type::Key;
		back.key = InputEvent::Key::Down;
		frontend.OnEvent(back);
		h.RunUntil([] { return false; }, 200);
		back.type = InputEvent::Type::KeyUp;
		frontend.OnEvent(back);
		h.RunUntil([] { return false; }, 300);
		chat();
		type("/disband");
		h.RunUntil([] { return false; }, 2500);
		chat();
		state("left");

		// A click on them in the world targets them (and would select them, were they a member).
		int bx = 0, by = 0;
		if (const ZoneSpawn *b = bot(); b && FindOnScreen(frontend, h, b->id, bx, by)) {
			frontend.OnEvent({InputEvent::Type::MouseMove, bx, by});
			frontend.OnEvent({InputEvent::Type::MouseDown, bx, by});
			frontend.OnEvent({InputEvent::Type::MouseUp, bx, by});
		}
		h.RunUntil([] { return false; }, 300);
		printf("[preview] target %u\n", frontend.Target());
		h.RunUntil([] { return false; }, 300);
		state("targeted");
		// /lfg on shows in /who lfg; forming the group turns it off.
		type("/lfg on");
		type("/who lfg");
		type("/invite");
		h.RunUntil([&] { return !group.Member(0).empty(); }, 10000);
		h.RunUntil([] { return false; }, 1500);
		chat();
		state("formed");
		h.Snapshot("g2-formed");
		// Target party member 1 (F2) selects them; /disband as the leader removes them.
		InputEvent f2;
		f2.type = InputEvent::Type::Key;
		f2.scan = 0x3c;
		frontend.OnEvent(f2);
		f2.type = InputEvent::Type::KeyUp;
		frontend.OnEvent(f2);
		printf("[preview] target after F2: %u\n", frontend.Target());
		// /makeleader of someone not in it, then of them; then /disband leaves (two: it's gone).
		type("/makeleader nobody");
		type("/makeleader");
		h.RunUntil([] { return false; }, 1500);
		chat();
		state("led by them");
		type("/disband");
		h.RunUntil([] { return false; }, 2500);
		chat();
		state("removed");
		type("/camp");
		h.RunUntil([&] { return zone.GetState() != ZoneSession::State::InZone && zone.GetState() != ZoneSession::State::Camping; },
			40000);
		chat();
		printf("[preview] camped\n");
		return 0;
	}

	// Live: what's on the ground; an item from the inventory dropped in front of us (the dialog's
	// Yes), found on the screen and picked back up, then put away; and a camp.
	int GroundTest(FrontEnd &frontend, Harness &h)
	{
		WindowManager &windows = frontend.Windows();
		const ZoneSession &zone = frontend.Zone();
		ZoneView *view = frontend.ZoneScene();
		h.Key(InputEvent::Key::F11); // the debug menu out of the way
		Rect area = frontend.ScreenArea();
		auto press = [&](int x, int y) {
			frontend.OnEvent({InputEvent::Type::MouseMove, area.x + x, area.y + y});
			frontend.OnEvent({InputEvent::Type::MouseDown, area.x + x, area.y + y});
			frontend.OnEvent({InputEvent::Type::MouseUp, area.x + x, area.y + y});
			frontend.Update(NowMs());
		};
		auto control = [&](const char *window, const std::string &id) {
			const SidlWindow *wnd = windows.Find(window);
			if (const SidlWindow::Control *c = wnd ? wnd->Find(id) : nullptr)
				press(c->rect.x + c->rect.w / 2, c->rect.y + c->rect.h / 2);
			else
				printf("[preview] no %s in %s\n", id.c_str(), window);
		};
		size_t chat_seen = frontend.Chat().Lines().size();
		auto chat = [&] {
			for (; chat_seen < frontend.Chat().Lines().size(); ++chat_seen)
				printf("[preview] chat: %s\n", frontend.Chat().Lines()[chat_seen].text.c_str());
		};
		h.RunUntil([] { return false; }, 1500);
		h.Snapshot("o0-zone");
		printf("[preview] %zu on the ground, %zu drawn\n", zone.GroundItems().size(), view ? view->GroundItemsShown() : 0);
		int listed = 0;
		for (const auto &[id, item] : zone.GroundItems())
			if (listed++ < 6)
				printf("[preview]   %u: %s, item %d, type %u at %.0f,%.0f,%.0f\n", id, item.model.c_str(), item.item_id,
					item.object_type, item.x, item.y, item.z);
		// An item onto the cursor.
		int lone = -1;
		for (const auto &[slot, item] : zone.Items())
			if (slot >= Inventory::FirstGeneral && slot <= Inventory::LastGeneral && item.item_class == 0 &&
			    !Inventory::Stacks(item) && item.no_drop != 0 && lone < 0)
				lone = slot;
		if (lone < 0) {
			printf("[preview] nothing droppable\n");
			for (const auto &[slot, item] : zone.Items())
				printf("[preview]   slot %d: '%s' class %d no drop %d\n", slot, item.name.c_str(), item.item_class, item.no_drop);
		}
		else {
			windows.Show("InventoryWindow", true);
			windows.Raise("InventoryWindow");
			control("InventoryWindow", "InvSlot" + std::to_string(lone));
			windows.Show("InventoryWindow", false);
			std::string name = zone.Items().count(Inventory::Cursor) ? zone.Items().at(Inventory::Cursor).name : "";
			printf("[preview] on the cursor: '%s'\n", name.c_str());
			// The ground in front of us, clear of the windows.
			int gx = -1, gy = -1;
			uint32_t none = 0;
			for (int y = frontend.ScreenHeight() * 3 / 4; y < frontend.ScreenHeight() - 40 && gx < 0; y += 8)
				for (int x = frontend.ScreenWidth() / 3; x < frontend.ScreenWidth() * 2 / 3 && gx < 0; x += 8)
					if (!view->PickSpawn(x, y, zone.MySpawnId()) && !view->PickGroundItem(x, y, zone.MySpawnId(), none) && !windows.Over(x, y))
						gx = x, gy = y;
			printf("[preview] the ground at %d,%d\n", gx, gy);
			size_t before = zone.GroundItems().size();
			press(gx, gy);
			printf("[preview] dialog %s\n", windows.Shown("ConfirmationDialogBox") ? "up" : "down");
			h.Snapshot("o1-confirm");
			control("ConfirmationDialogBox", "Yes_Button");
			h.RunUntil([&] { return zone.GroundItems().size() > before; }, 5000);
			h.RunUntil([] { return false; }, 800);
			chat();
			printf("[preview] dropped: cursor %s, %zu on the ground (%zu drawn)\n",
				zone.Items().count(Inventory::Cursor) ? "busy" : "free", zone.GroundItems().size(), view->GroundItemsShown());
		}
		h.Snapshot("o2-dropped");
		// Everything dropped within 10 of us (this one, and any an earlier run left) back up, one
		// at a time: found on the screen, clicked, put away.
		for (int round = 0; round < 4; ++round) {
			const ZoneProfile &me = zone.Profile();
			uint32_t mine = 0;
			for (const auto &[id, item] : zone.GroundItems())
				if (std::hypot(item.x - me.x, item.y - me.y) < 10 && mine == 0)
					mine = id;
			if (mine == 0)
				break;
			// Where it is at our feet, past our model: the pixels that pick it, and ours that pick us.
			const ZoneGroundItem &there = zone.GroundItems().at(mine);
			windows.Show("ChatWindow", false);
			windows.Show("InventoryWindow", false);
			auto where = [&](const char *when) {
				int on_item = 0, on_us = 0, x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1;
				uint32_t under = 0;
				for (int y = 0; y < frontend.ScreenHeight(); y += 2)
					for (int x = 0; x < frontend.ScreenWidth(); x += 2) {
						if (view->PickGroundItem(x, y, zone.MySpawnId(), under) && under == mine) {
							++on_item;
							x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
						} else if (view->PickSpawn(x, y, zone.MySpawnId()) == zone.MySpawnId()) {
							++on_us;
						}
					}
				printf("[preview] %s: %d pixels pick it (%d,%d to %d,%d), %d pick us\n", when, on_item * 4, x0, y0, x1, y1, on_us * 4);
			};
			if (round == 0)
				where("at our feet");
			// Then step back and aside so it's in front of us and not behind our model, out from
			// under the chat window.
			view->SetPlayerPosition(there.x + 6, there.y - 12, there.z, 0);
			h.RunUntil([] { return false; }, 1200);
			h.Snapshot("o3-before-pickup"); // drawn: picked from the last frame
			where("stepped back");
			int px = -1, py = -1;
			uint32_t picked = 0;
			for (int y = 4; y < frontend.ScreenHeight() && px < 0; y += 4)
				for (int x = 4; x < frontend.ScreenWidth() && px < 0; x += 4)
					if (view->PickGroundItem(x, y, zone.MySpawnId(), picked) && picked == mine && !windows.Over(x, y) &&
					    !view->PickSpawn(x, y, zone.MySpawnId()))
						px = x, py = y;
			printf("[preview] drop %u (%s) on the screen at %d,%d\n", mine, zone.GroundItems().at(mine).model.c_str(), px, py);
			if (px < 0)
				break;
			press(px, py);
			h.RunUntil([&] { return zone.Items().count(Inventory::Cursor) != 0; }, 5000);
			h.RunUntil([] { return false; }, 500);
			chat();
			printf("[preview] picked up: cursor '%s', %zu on the ground, inventory %s\n",
				zone.Items().count(Inventory::Cursor) ? zone.Items().at(Inventory::Cursor).name.c_str() : "",
				zone.GroundItems().size(), windows.Shown("InventoryWindow") ? "up" : "down");
			if (zone.Items().count(Inventory::Cursor))
				control("InventoryWindow", "IW_CharacterView");
			h.RunUntil([] { return false; }, 500);
			printf("[preview] put away: cursor %s\n", zone.Items().count(Inventory::Cursor) ? "busy" : "free");
			windows.Show("InventoryWindow", false);
		}
		// A forge (a tradeskill container, type 17): clicked, it opens as BagWorld1; Done closes it.
		for (const auto &[id, item] : zone.GroundItems()) {
			if (item.object_type != 17)
				continue;
			uint32_t forge = id;
			windows.Show("ChatWindow", false);
			view->SetPlayerPosition(item.x, item.y - 12, item.z + 20, 0); // over the floor, to fall onto it
			h.RunUntil([] { return false; }, 2500);
			h.Snapshot("o4-forge");
			int fx = -1, fy = -1;
			uint32_t picked = 0;
			for (int y = 4; y < frontend.ScreenHeight() && fx < 0; y += 4)
				for (int x = 4; x < frontend.ScreenWidth() && fx < 0; x += 4)
					if (view->PickGroundItem(x, y, zone.MySpawnId(), picked) && picked == forge && !windows.Over(x, y) &&
					    !view->PickSpawn(x, y, zone.MySpawnId()))
						fx = x, fy = y;
			printf("[preview] forge %u (%s) on the screen at %d,%d, %.1f away\n", forge, item.model.c_str(), fx, fy,
				std::hypot(item.x - zone.Profile().x, item.y - zone.Profile().y));
			if (fx < 0)
				break;
			press(fx, fy);
			h.RunUntil([&] { return windows.Shown("BagWorld1"); }, 5000);
			chat();
			if (const SidlWindow *bag = windows.Find("BagWorld1"); bag && windows.Shown("BagWorld1")) {
				const SidlWindow::Control *label = bag->Find("Container_Label");
				int slots = 0;
				for (int n = 1; n <= 10; ++n)
					if (const SidlWindow::Control *c = bag->Find("ContainerSlot" + std::to_string(n)); c && c->visible)
						++slots;
				const SidlWindow::Control *combine = bag->Find("Container_Combine");
				printf("[preview] BagWorld1 up: '%s', %d slots, Combine %s\n", label ? label->text.c_str() : "", slots,
					combine && combine->visible ? "shown" : "hidden");
				h.RunUntil([] { return false; }, 300);
				h.Snapshot("o5-forge-open");
				control("BagWorld1", "DoneButton");
				h.RunUntil([] { return false; }, 300);
				printf("[preview] after Done: BagWorld1 %s\n", windows.Shown("BagWorld1") ? "up" : "down");
			}
			else
				printf("[preview] the forge didn't open\n");
			break;
		}
		h.Key(InputEvent::Key::Enter);
		h.Type("/camp");
		h.Key(InputEvent::Key::Enter);
		h.RunUntil([&] { return zone.GetState() != ZoneSession::State::InZone && zone.GetState() != ZoneSession::State::Camping; },
			40000);
		printf("[preview] camped\n");
		return 0;
	}

	// Live: the nearest merchant's shop opened with the right button, the first thing in a general
	// slot sold to it and bought back (the zone answering both), then Done and a camp.
	int ShopTest(FrontEnd &frontend, Harness &h)
	{
		WindowManager &windows = frontend.Windows();
		const ZoneSession &zone = frontend.Zone();
		auto click = [&](const char *window, const std::string &id, bool ctrl = false) {
			const SidlWindow *wnd = windows.Find(window);
			const SidlWindow::Control *c = wnd ? wnd->Find(id) : nullptr;
			if (!c) {
				printf("[preview] no %s in %s\n", id.c_str(), window);
				return;
			}
			Rect area = frontend.ScreenArea();
			int x = area.x + c->rect.x + c->rect.w / 2, y = area.y + c->rect.y + c->rect.h / 2;
			InputEvent e{InputEvent::Type::MouseMove, x, y};
			frontend.OnEvent(e);
			e.ctrl = ctrl;
			e.type = InputEvent::Type::MouseDown;
			frontend.OnEvent(e);
			e.type = InputEvent::Type::MouseUp;
			frontend.OnEvent(e);
			frontend.Update(NowMs());
		};
		size_t chat_seen = frontend.Chat().Lines().size();
		auto chat = [&] {
			for (; chat_seen < frontend.Chat().Lines().size(); ++chat_seen)
				printf("[preview] chat: %s\n", frontend.Chat().Lines()[chat_seen].text.c_str());
		};
		auto coins = [&](const char *when) {
			const ZoneProfile &me = zone.Profile();
			printf("[preview] purse %s: %dp %dg %ds %dc\n", when, me.platinum, me.gold, me.silver, me.copper);
		};
		const ZoneProfile &me = zone.Profile();
		const ZoneSpawn *merchant = nullptr;
		float best = 0;
		for (const auto &[id, spawn] : zone.Spawns()) {
			float d = std::hypot(spawn.x - me.x, spawn.y - me.y);
			if (spawn.npc == 1 && spawn.class_id == 32 && (!merchant || d < best))
				merchant = &spawn, best = d;
		}
		if (!merchant) {
			printf("[preview] no merchant in the zone\n");
			return 1;
		}
		uint16_t merchant_id = merchant->id;
		printf("[preview] merchant %s (%d), %.0f away\n", merchant->name.c_str(), merchant_id, best);
		h.Key(InputEvent::Key::F11); // the debug menu (open by default) out of the way
		for (const char *screen : {"InventoryWindow", "ChatWindow"})
			windows.Show(screen, false);
		windows.MoveTo("ChatWindow", frontend.ScreenWidth() - 400, 10); // clear of the shop's Done
		frontend.ZoneScene()->SetPlayerPosition(merchant->x, merchant->y - 10, merchant->z, 0);
		h.RunUntil([] { return false; }, 2000); // our position to the zone
		int x = 0, y = 0;
		if (!FindOnScreen(frontend, h, merchant_id, x, y)) {
			printf("[preview] the merchant isn't on the screen\n");
			return 1;
		}
		frontend.OnEvent({InputEvent::Type::MouseMove, x, y});
		frontend.OnEvent({InputEvent::Type::RightDown, x, y});
		frontend.OnEvent({InputEvent::Type::RightUp, x, y});
		if (!h.RunUntil([&] { return windows.Shown("MerchantWnd"); }, 5000)) {
			chat();
			printf("[preview] no shop\n");
			return 1;
		}
		h.RunUntil([] { return false; }, 1000); // its items
		windows.Show("ChatWindow", true);
		size_t count = 0;
		for (const auto &[slot, item] : zone.Items())
			if (slot >= Inventory::FirstMerchant && slot <= Inventory::LastMerchant) {
				if (count++ < 8)
					printf("[preview]   merchant slot %d: %s, %d copper\n", slot - Inventory::FirstMerchant, item.name.c_str(),
						item.price);
			}
		printf("[preview] the shop is up with %zu items; inventory %s\n", count,
			windows.Shown("InventoryWindow") ? "up" : "down");
		chat();
		h.Snapshot("m1-shop");
		// Sell the first common item in a general slot that doesn't stack.
		for (const auto &[slot, item] : zone.Items())
			if (slot >= Inventory::FirstGeneral && slot <= Inventory::LastGeneral)
				printf("[preview]   our slot %d: %s, class %d, price %d, no_drop %d, no_rent %d\n", slot, item.name.c_str(),
					item.item_class, item.price, item.no_drop, item.no_rent);
		int16_t sell = -1;
		ZoneItem sold;
		for (const auto &[slot, item] : zone.Items())
			if (slot >= Inventory::FirstGeneral && slot <= Inventory::LastGeneral && item.item_class == 0 &&
			    !Inventory::Stacks(item) && item.no_drop != 0 && item.price > 0 && sell < 0)
				sell = slot, sold = item;
		if (sell >= 0) {
			coins("before");
			click("InventoryWindow", "InvSlot" + std::to_string(sell));
			chat();
			h.Snapshot("m2-selected");
			click("MerchantWnd", "MW_Sell_Button");
			h.RunUntil([&] { return !zone.Items().count(sell); }, 5000);
			h.RunUntil([] { return false; }, 1000);
			chat();
			coins("after selling");
			int16_t back = -1;
			for (const auto &[slot, item] : zone.Items())
				if (slot >= Inventory::FirstMerchant && slot <= Inventory::LastMerchant && item.id == sold.id)
					back = slot;
			if (back >= 0) {
				printf("[preview] it's in the merchant's slot %d for %d copper\n", back - Inventory::FirstMerchant,
					zone.Items().at(back).price);
				click("MerchantWnd", "MW_MerchantSlot" + std::to_string(back - Inventory::FirstMerchant));
				chat();
				click("MerchantWnd", "MW_Buy_Button", true);
				h.RunUntil([&] {
					for (const auto &[slot, item] : zone.Items())
						if (slot < Inventory::FirstMerchant && item.id == sold.id)
							return true;
					return false;
				}, 5000);
				h.RunUntil([] { return false; }, 500);
				chat();
				coins("after buying it back");
				for (const auto &[slot, item] : zone.Items())
					if (slot < Inventory::FirstMerchant && item.id == sold.id)
						printf("[preview] %s is back in slot %d\n", item.name.c_str(), slot);
			}
		}
		if (sell < 0) {
			// Nothing to sell: Buy on the merchant's first item, more than we have.
			click("MerchantWnd", "MW_MerchantSlot0");
			const SidlWindow *shop = windows.Find("MerchantWnd");
			const SidlWindow::Control *buy = shop ? shop->Find("MW_Buy_Button") : nullptr;
			printf("[preview] Buy %s, %s\n", buy && buy->visible ? "shown" : "hidden", buy && buy->enabled ? "enabled" : "disabled");
			click("MerchantWnd", "MW_Buy_Button", true);
			h.RunUntil([] { return false; }, 300);
			chat();
		}
		h.Snapshot("m3-after");
		click("MerchantWnd", "DoneButton");
		h.RunUntil([] { return false; }, 300);
		printf("[preview] after Done: shop %s, inventory %s, merchant items %zu\n", windows.Shown("MerchantWnd") ? "up" : "down",
			windows.Shown("InventoryWindow") ? "up" : "down",
			static_cast<size_t>(std::count_if(zone.Items().begin(), zone.Items().end(),
				[](const auto &p) { return p.first >= Inventory::FirstMerchant; })));
		chat();
		h.Key(InputEvent::Key::Enter);
		h.Type("/camp");
		h.Key(InputEvent::Key::Enter);
		h.RunUntil([&] { return zone.GetState() != ZoneSession::State::InZone && zone.GetState() != ZoneSession::State::Camping; },
			40000);
		printf("[preview] camped\n");
		return 0;
	}
}

namespace
{
	// Replay "options": the Options window's keyboard page, and keys through the keymap.
	int OptionsTest(FrontEnd &frontend, Harness &h, const std::string &settings_ini)
	{
		WindowManager &windows = frontend.Windows();
		auto key = [&](int scan, bool shift = false, bool ctrl = false, bool alt = false, bool up = false) {
			InputEvent event;
			event.type = up ? InputEvent::Type::KeyUp : InputEvent::Type::Key;
			event.scan = scan;
			event.shift = shift;
			event.ctrl = ctrl;
			event.alt = alt;
			frontend.OnEvent(event);
			frontend.Update(NowMs());
		};
		auto press = [&](int scan, bool shift = false, bool ctrl = false, bool alt = false) {
			key(scan, shift, ctrl, alt);
			key(scan, shift, ctrl, alt, true);
		};
		size_t chat_seen = frontend.Chat().Lines().size();
		auto chat = [&] {
			for (; chat_seen < frontend.Chat().Lines().size(); ++chat_seen)
				printf("[preview] chat: %s\n", frontend.Chat().Lines()[chat_seen].text.c_str());
		};
		auto click = [&](int x, int y) {
			frontend.OnEvent({InputEvent::Type::MouseMove, x, y});
			frontend.OnEvent({InputEvent::Type::MouseDown, x, y});
			frontend.OnEvent({InputEvent::Type::MouseUp, x, y});
			frontend.Update(NowMs());
		};
		// Alt+O: Toggle options window.
		printf("[preview] options before Alt+O: %s\n", windows.Shown("OptionsWindow") ? "shown" : "hidden");
		press(0x18, false, false, true);
		printf("[preview] options after Alt+O: %s\n", windows.Shown("OptionsWindow") ? "shown" : "hidden");
		SidlWindow *options = windows.Find("OptionsWindow");
		if (!options)
			return 1;
		printf("[preview] OptionsWindow %dx%d\n", options->Bounds().w, options->Bounds().h);
		SidlWindow::Control *tabs = options->Find("OPTW_OptionsSubwindows");
		for (int page : tabs->children) {
			int shown = 0;
			for (int piece : options->Item(page).children)
				shown += options->Item(piece).visible;
			printf("[preview] page %s: %d pieces shown\n", options->Item(page).def->screen_id.c_str(), shown);
		}
		options->SelectTab(*tabs, 3);
		SidlWindow::Control *list = options->Find("OKP_KeyboardAssignmentList");
		SidlWindow::Control *combo = options->Find("OKP_KeyboardFilterCombobox");
		auto dump = [&](const char *what) {
			printf("[preview] %s: combo '%s', %zu rows\n", what,
				combo->choice >= 0 ? combo->choices[combo->choice].c_str() : "", list->rows.size());
			for (const auto &row : list->rows)
				printf("[preview]   %-28s | %-12s | %-12s | %s\n", row[0].c_str(), row[1].c_str(), row[2].c_str(),
					row[3].c_str());
		};
		dump("keyboard page");
		h.Snapshot("o1-keyboard");
		// The combo box: its list down, then "All".
		click(combo->rect.x + combo->rect.w / 2, combo->rect.y + combo->rect.h / 2);
		printf("[preview] combo list open: %d\n", combo->list_open);
		h.Snapshot("o2-combo-open");
		int line = 15; // font 3's height
		const Rect &r = combo->rect;
		click(r.x + 20, r.Bottom() + 4 + line * 8 + line / 2 - 2); // the ninth choice, All (inside the border)
		printf("[preview] combo after pick: open %d, choice %d (%s), %zu rows\n", combo->list_open, combo->choice,
			combo->choice >= 0 ? combo->choices[combo->choice].c_str() : "", list->rows.size());
		// Back to Movement, then bind Move forward's alternate to Shift+F.
		combo->choice = 0;
		frontend.Windows().Find("OptionsWindow");
		click(combo->rect.x + 20, combo->rect.y + 5);
		click(r.x + 20, r.Bottom() + 4 + line / 2); // Movement
		Rect content = options->ContentRect(*list);
		int header = 15 + 4, row_h = 15 + 2;
		auto cell = [&](int row, int column) {
			int x = content.x;
			for (int c = 0; c < column; ++c)
				x += list->columns[c].width;
			click(x + 10, content.y + header + row * row_h + row_h / 2);
		};
		cell(2, 2); // Move forward, Alternate
		printf("[preview] clicked forward alt: '%s'\n", list->rows[2][2].c_str());
		key(0x2a, true);       // Shift alone: still waiting
		printf("[preview] after Shift alone: '%s'\n", list->rows[2][2].c_str());
		press(0x21, true);     // Shift+F
		printf("[preview] after Shift+F: '%s'\n", list->rows[2][2].c_str());
		cell(0, 1); // Autorun/walk, Keypress: Escape unbinds it
		press(0x01);
		printf("[preview] Autorun after Escape: '%s'\n", list->rows[0][1].c_str());
		cell(1, 1); // Jump: Enter can't be bound
		press(0x1c);
		chat();
		printf("[preview] Jump after Enter: '%s'\n", list->rows[1][1].c_str());
		IniFile ini;
		ini.Load(settings_ini);
		printf("[preview] ini: FORWARD_2=%s AUTORUN=%s JUMP=%s\n", ini.Get("KeyMaps", "KEYMAP_FORWARD_2").c_str(),
			ini.Get("KeyMaps", "KEYMAP_AUTORUN").c_str(), ini.Get("KeyMaps", "KEYMAP_JUMP").c_str());
		h.Snapshot("o3-assigned");
		// Reload defaults: the question, then Yes.
		if (const SidlWindow::Control *button = options->Find("OKP_DefaultKeysButton"))
			click(button->rect.x + 5, button->rect.y + 5);
		printf("[preview] confirmation: %s\n", windows.Shown("ConfirmationDialogBox") ? "shown" : "hidden");
		if (SidlWindow *dialog = windows.Find("ConfirmationDialogBox"))
			if (const SidlWindow::Control *yes = dialog->Find("Yes_Button"))
				click(yes->rect.x + 5, yes->rect.y + 5);
		ini = IniFile{};
		ini.Load(settings_ini);
		printf("[preview] after defaults: forward alt '%s', autorun '%s', ini FORWARD_2 '%s'\n", list->rows[2][2].c_str(),
			list->rows[0][1].c_str(), ini.Get("KeyMaps", "KEYMAP_FORWARD_2", "(none)").c_str());
		// The keymap in the zone.
		press(0x18, false, false, true); // Alt+O closes it
		printf("[preview] options after Alt+O: %s\n", windows.Shown("OptionsWindow") ? "shown" : "hidden");
		{
			// As Windows sends Alt+O: Alt down a system key, O with Alt, then Alt let go as a plain
			// key up (no Key::Alt, just its scan code). W after it isn't Alt+W (the selector).
			InputEvent alt;
			alt.type = InputEvent::Type::Key;
			alt.key = InputEvent::Key::Alt;
			frontend.OnEvent(alt);
			press(0x18, false, false, true);
			InputEvent alt_up;
			alt_up.type = InputEvent::Type::KeyUp;
			alt_up.scan = 0x38;
			frontend.OnEvent(alt_up);
			bool selector = windows.Shown("SelectorWindow");
			press(0x11);
			printf("[preview] Alt+O, Alt up, W: options %s, selector %s -> %s\n",
				windows.Shown("OptionsWindow") ? "shown" : "hidden", selector ? "shown" : "hidden",
				windows.Shown("SelectorWindow") ? "shown" : "hidden");
			key(0x11, false, false, false, true);
			press(0x18, false, false, true);
		}
		press(0x13, false, true); // Ctrl+R
		chat();
		bool compass = windows.Shown("CompassWindow");
		press(0x20, false, false, true); // Alt+D
		printf("[preview] compass %s -> %s\n", compass ? "shown" : "hidden", windows.Shown("CompassWindow") ? "shown" : "hidden");
		press(0x17); // I: inventory
		printf("[preview] inventory after I: %s\n", windows.Shown("InventoryWindow") ? "shown" : "hidden");
		press(0x30, false, true); // Ctrl+B: spellbook
		printf("[preview] spellbook after Ctrl+B: %s\n", windows.Shown("SpellBookWnd") ? "shown" : "hidden");
		press(0x30, false, true);
		printf("[preview] spellbook after Ctrl+B again: %s\n", windows.Shown("SpellBookWnd") ? "shown" : "hidden");
		press(0x18, false, true); // Ctrl+O: actions social page
		if (SidlWindow *actions = windows.Find("ActionsWindow"))
			printf("[preview] actions %s, page %d\n", windows.Shown("ActionsWindow") ? "shown" : "hidden",
				actions->Find("ACTW_ActionsSubwindows")->tab);
		press(0x04, true); // Shift+3: hot button page 3
		if (SidlWindow *hot = windows.Find("HotButtonWnd"))
			frontend.Update(NowMs()), printf("[preview] hot page label '%s'\n", hot->Find("HB_CurrentPageLabel")->text.c_str());
		press(0x3b); // F1
		printf("[preview] target after F1: %u (me %u)\n", frontend.Target(), frontend.Zone().MySpawnId());
		// W held: moving; let go: stopped. Typing: W goes into the chat line, and Shift+Up brings back what was sent.
		ZoneView *view = frontend.ZoneScene();
		key(0x11);
		h.RunUntil([] { return false; }, 400);
		printf("[preview] W held: speed %.2f\n", view ? view->Movement().speed : -1.0f);
		key(0x11, false, false, false, true);
		h.RunUntil([] { return false; }, 400);
		printf("[preview] W let go: speed %.2f\n", view ? view->Movement().speed : -1.0f);
		// Num Lock: autorun on and off; on again, Move back (S) ends it.
		auto autorun = [&](const char *what) {
			h.RunUntil([] { return false; }, 400);
			printf("[preview] %s: speed %.2f\n", what, view ? view->Movement().speed : -1.0f);
		};
		key(0x45), key(0x45, false, false, false, true);
		autorun("Num Lock");
		key(0x45), key(0x45, false, false, false, true);
		autorun("Num Lock again");
		key(0x45), key(0x45, false, false, false, true);
		autorun("Num Lock a third time");
		key(0x1f), key(0x1f, false, false, false, true);
		autorun("then S tapped");
		h.Key(InputEvent::Key::Enter);
		h.Type("/loc");
		h.Key(InputEvent::Key::Enter);
		frontend.Update(NowMs());
		chat();
		h.Key(InputEvent::Key::Enter);
		key(0x11);
		h.Type("w");
		key(0x11, false, false, false, true);
		press(0xc8, true); // Shift+Up
		frontend.Update(NowMs());
		if (SidlWindow *chat_window = windows.Find("ChatWindow"))
			printf("[preview] chat input after W and Shift+Up: '%s'\n", chat_window->Find("CWChatInput")->text.c_str());
		h.Snapshot("o4-done");
		return 0;
	}
}

int main(int argc, char **argv)
{
	bool replay = argc >= 5 && std::string(argv[3]) == "--replay";
	if (!replay && argc != 3 && argc != 6 && argc != 7) {
		fprintf(stderr,
			"usage: %s EQDIR OUTDIR [HOST:PORT USER PASSWORD [charselect|create]]\n"
			"       %s EQDIR OUTDIR --replay FILE [SECONDS [options]]\n",
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
	if (replay && argc >= 7 && std::string(argv[6]) == "options")
		config.settings_ini = std::string(argv[2]) + "/my-client.ini"; // the keymap's saves
	bool stop_at_charselect = argc == 7 && std::string(argv[6]) == "charselect";
	bool explore = argc == 7 && std::string(argv[6]) == "explore";
	// createluclin: the same with every Luclin model on, a dwarf warrior whose face picker cycles
	// each feature (printing what the model has) before it's created.
	bool create_luclin = argc == 7 && std::string(argv[6]) == "createluclin";
	bool create = argc == 7 && (std::string(argv[6]) == "create" || create_luclin);
	if (create_luclin)
		for (auto &race : config.luclin_models.use)
			race[0] = race[1] = true;
	bool shop = argc == 7 && std::string(argv[6]) == "shop";
	bool ground = argc == 7 && std::string(argv[6]) == "ground";
	bool commands = argc == 7 && std::string(argv[6]) == "commands";
	bool group_test = argc == 7 && std::string(argv[6]) == "group";
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
		// A book's window (CBookWnd) with the zone's text for the Quintessence book: its pages as
		// ParseAndFormatBookContent lays them out, Right and Left, Escape.
		{
			BookWindow &book = frontend.Book();
			auto pages = [&](const char *when) {
				const SidlWindow *w = windows.Find("BookWindow");
				const SidlWindow::Control *right = w ? w->Find("RightButton") : nullptr;
				const SidlWindow::Control *left = w ? w->Find("LeftButton") : nullptr;
				printf("[preview] book %s: %s '%s', page %d, left %d right %d\n", when,
					windows.Shown("BookWindow") ? "shown" : "hidden", w ? w->Root().text.c_str() : "", book.Page(),
					left ? left->enabled : -1, right ? right->enabled : -1);
			};
			book.Check("preview", "Quintessence of the Elements");
			book.SetBook("Soon after the creation of the`elder gods by the Nameless,`while the gods were placing`their "
			             "creations onto Norrath,`the elemental gods gathered to`make their own creation.`Unlike what "
			             "the other gods`created on Norrath, Fennin Ro,`Tarew Marr, The Rathe Council,`and Xegony gathered "
			             "for their`own creation. They combined`the very essence of their`beings and created an object`of "
			             "infinite power, the`Quintessence of the Elements.`Once created the gods realized`the danger any "
			             "wielder of it`would possess, so they locked`it away in the Rathe's vault. A line with no breaks in "
			             "it runs on and on until it wraps.");
			frontend.Update(NowMs());
			pages("opened");
			for (int p = 0; p < 4 && !book.PageText(p).empty(); ++p) {
				printf("[preview] page %d:\n", p + 1);
				const std::string &text = book.PageText(p);
				for (size_t start = 0; start < text.size();) {
					size_t nl = text.find('\n', start);
					printf("[preview]   |%s|\n", text.substr(start, nl - start).c_str());
					start = nl + 1;
				}
			}
			h.Snapshot("k1-book");
			h.Key(InputEvent::Key::Right);
			pages("after Right");
			h.Key(InputEvent::Key::Right);
			pages("after Right again");
			h.Key(InputEvent::Key::Left);
			pages("after Left");
			h.Key(InputEvent::Key::Escape);
			pages("after Escape");
			while (book.Page() > 0) // as it was for the rest
				book.Click("BookWindow", "LeftButton");
		}
		// The buff window: Alt held over it shows every buff's tooltip (CBuffWindow::PostDraw), and
		// alt+click on a buff shows its spell's name and icon in the item display.
		if (const SidlWindow *buffs = windows.Find("BuffWindow"); buffs && buffs->Find("Buff0"))
			h.RunUntil([&] { return !buffs->Find("Buff0")->tooltip.empty(); }, 2000); // a buff landing soon
		if (const SidlWindow *buffs = windows.Find("BuffWindow"))
			printf("[preview] buff window %s, Buff0 '%s'\n", windows.Shown("BuffWindow") ? "shown" : "hidden",
				buffs->Find("Buff0") ? buffs->Find("Buff0")->tooltip.c_str() : "(none)");
		if (const SidlWindow *buffs = windows.Find("BuffWindow"); buffs && windows.Shown("BuffWindow"))
			if (const SidlWindow::Control *b = buffs->Find("Buff0"); b && !b->tooltip.empty()) {
				int bx = b->rect.x + b->rect.w / 2, by = b->rect.y + b->rect.h / 2;
				Rect area = frontend.ScreenArea();
				frontend.OnEvent({InputEvent::Type::MouseMove, area.x + bx, area.y + by});
				h.Key(InputEvent::Key::Alt);
				h.RunUntil([] { return false; }, 200);
				printf("[preview] buff 0 '%s' at %d,%d with Alt held\n", b->tooltip.c_str(), b->rect.x, b->rect.y);
				h.Snapshot("q1-buff-tooltips");
				InputEvent event{InputEvent::Type::MouseDown, area.x + bx, area.y + by};
				event.alt = true;
				frontend.OnEvent(event);
				event.type = InputEvent::Type::MouseUp;
				frontend.OnEvent(event);
				frontend.Update(NowMs());
				h.KeyUp(InputEvent::Key::Alt);
				const SidlWindow *display = windows.Find("ItemDisplayWindow");
				const SidlWindow::Control *text = display ? display->Find("ItemDescription") : nullptr;
				printf("[preview] alt+click on buff 0: item display %s '%s', text '%s'\n",
					windows.Shown("ItemDisplayWindow") ? "shown" : "hidden", display ? display->Root().text.c_str() : "",
					text ? text->text.c_str() : "");
				windows.Show("ItemDisplayWindow", false);
			}
		// Destroy: the first general slot's item onto the cursor, Destroy and No, then Destroy and Yes.
		if (SidlWindow *inv = windows.Find("InventoryWindow")) {
			windows.Show("InventoryWindow", true);
			windows.Raise("InventoryWindow");
			frontend.Update(NowMs());
			int16_t slot = -1;
			for (int16_t s = Inventory::FirstGeneral; s <= Inventory::LastGeneral && slot < 0; ++s)
				if (frontend.Zone().Items().count(s) && frontend.Zone().Items().at(s).item_class == 0)
					slot = s;
			auto press = [&](const char *id) {
				SidlWindow *w = windows.Find(id[0] == 'I' ? "InventoryWindow" : "ConfirmationDialogBox");
				const SidlWindow::Control *c = w ? w->Find(id) : nullptr;
				if (!c)
					return;
				int x = c->rect.x + c->rect.w / 2, y = c->rect.y + c->rect.h / 2;
				frontend.OnEvent({InputEvent::Type::MouseMove, x, y});
				frontend.OnEvent({InputEvent::Type::MouseDown, x, y});
				frontend.OnEvent({InputEvent::Type::MouseUp, x, y});
				frontend.Update(NowMs());
			};
			auto report = [&](const char *when) {
				const SidlWindow *d = windows.Find("ConfirmationDialogBox");
				const SidlWindow::Control *t = d ? d->Find("TextOutput") : nullptr;
				auto held = frontend.Zone().Items().find(Inventory::Cursor);
				printf("[preview] destroy %s: dialog %s '%s', cursor '%s'\n", when,
					windows.Shown("ConfirmationDialogBox") ? "up" : "down", t ? t->text.c_str() : "",
					held != frontend.Zone().Items().end() ? held->second.name.c_str() : "");
			};
			if (slot >= 0) {
				press(("InvSlot" + std::to_string(slot)).c_str());
				report("picked up");
				press("IW_Destroy");
				report("asked");
				h.Snapshot("x1-destroy");
				press("No_Button");
				report("no");
				press("IW_Destroy");
				press("Yes_Button");
				report("yes");
			}
			windows.Show("InventoryWindow", false);
		}
		if (argc >= 7 && std::string(argv[6]) == "options")
			return OptionsTest(frontend, h, config.settings_ini);
		// The window menu: a right press on the player window off its controls, Background's
		// submenu, Tint Color, a swatch and Accept; then Texture off, and Lock.
		if (const SidlWindow *player = where("PlayerWindow")) {
			const Rect &c = player->Root().client;
			int px = -1, py = -1;
			for (int y = c.y + 2; y < c.Bottom() && px < 0; y += 2)
				for (int x = c.x + 2; x < c.Right() && px < 0; x += 2)
					if (!player->InteractiveAt(x, y)) {
						px = x;
						py = y;
					}
			auto at = [&](const Rect &r) { return std::make_pair(r.x + r.w / 2, r.y + r.h / 2); };
			auto move = [&](const Rect &r) {
				auto [x, y] = at(r);
				frontend.OnEvent({InputEvent::Type::MouseMove, x, y});
				frontend.Update(NowMs());
			};
			auto pick = [&](const Rect &r) {
				auto [x, y] = at(r);
				frontend.OnEvent({InputEvent::Type::MouseMove, x, y});
				frontend.OnEvent({InputEvent::Type::MouseDown, x, y});
				frontend.OnEvent({InputEvent::Type::MouseUp, x, y});
				frontend.Update(NowMs());
			};
			auto menu = [&] {
				frontend.OnEvent({InputEvent::Type::MouseMove, px, py});
				frontend.OnEvent({InputEvent::Type::RightDown, px, py});
				frontend.OnEvent({InputEvent::Type::RightUp, px, py});
				frontend.Update(NowMs());
				h.RunUntil([] { return false; }, 100); // drawn: laid out
			};
			menu();
			h.RunUntil([] { return false; }, 100);
			printf("[preview] window menu at %d,%d: open %d, row 0 %d,%d %dx%d\n", px, py, windows.MenuOpen(),
				windows.MenuRowRect(0, 0).x, windows.MenuRowRect(0, 0).y, windows.MenuRowRect(0, 0).w, windows.MenuRowRect(0, 0).h);
			h.Snapshot("m1-window-menu");
			move(windows.MenuRowRect(0, 0)); // Background
			h.RunUntil([] { return false; }, 100);
			printf("[preview] Background's submenu: %d\n", windows.MenuRowRect(1, 1).w > 0);
			h.Snapshot("m2-background-submenu");
			move(windows.MenuRowRect(1, 0));
			pick(windows.MenuRowRect(1, 1)); // Tint Color
			h.RunUntil([] { return false; }, 100);
			printf("[preview] color picker %s, menu open %d\n", windows.Shown("ColorPickerWnd") ? "shown" : "hidden",
				windows.MenuOpen());
			h.Snapshot("m3-color-picker");
			if (const SidlWindow *picker = windows.Find("ColorPickerWnd")) {
				if (const SidlWindow::Control *swatch = picker->Find("CPW_TC0Button"))
					click(swatch->rect.x + swatch->rect.w / 2, swatch->rect.y + swatch->rect.h / 2);
				if (const SidlWindow::Control *accept = picker->Find("CPW_Accept_Button"))
					click(accept->rect.x + accept->rect.w / 2, accept->rect.y + accept->rect.h / 2);
			}
			h.RunUntil([] { return false; }, 100);
			const WindowManager::Settings *s = windows.SettingsOf("PlayerWindow");
			printf("[preview] after Accept: picker %s, PlayerWindow tint %06x\n",
				windows.Shown("ColorPickerWnd") ? "shown" : "hidden", s ? s->tint : 0);
			h.Snapshot("m4-tinted");
			menu();
			move(windows.MenuRowRect(0, 0));
			pick(windows.MenuRowRect(1, 0)); // Texture
			h.RunUntil([] { return false; }, 100);
			printf("[preview] after Texture: texture %d\n", s ? s->texture : -1);
			h.Snapshot("m5-no-texture");
			menu();
			pick(windows.MenuRowRect(0, 3)); // Lock
			printf("[preview] after Lock: locked %d\n", s ? s->locked : -1);
			menu();
			pick(windows.MenuRowRect(0, 3));
			printf("[preview] after Lock again: locked %d\n", s ? s->locked : -1);
			menu();
			move(windows.MenuRowRect(0, 1)); // Alpha
			pick(windows.MenuRowRect(1, 0)); // Normal Level
			if (const SidlWindow *quantity = windows.Find("QuantityWnd")) {
				const SidlWindow::Control *edit = quantity->Find("QTYW_SliderInput");
				printf("[preview] Normal Level: quantity %s at '%s'\n", windows.Shown("QuantityWnd") ? "shown" : "hidden",
					edit ? edit->text.c_str() : "?");
			}
			windows.Show("QuantityWnd", false);
			windows.SetTint("PlayerWindow", 0xffffff);
			if (s && !s->texture) {
				menu();
				move(windows.MenuRowRect(0, 0));
				pick(windows.MenuRowRect(1, 0));
			}
		}
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
			// No room: every general slot and bag slot filled (stand-ins the replay's zone never
			// hears of), an item on the cursor put away on the character view drops to the ground
			// (OP_GroundSpawn, eqstr 12937); then the inventory as it was.
			{
				std::map<int16_t, ZoneItem> &edit = const_cast<ZoneSession &>(frontend.Zone()).EditItems();
				std::map<int16_t, ZoneItem> kept = edit;
				ZoneItem filler;
				filler.id = 13005;
				filler.name = "Filler";
				filler.icon = 600;
				filler.no_drop = 1;
				for (int16_t slot = Inventory::FirstGeneral; slot <= Inventory::LastGeneral; ++slot) {
					if (!edit.count(slot))
						edit[slot] = filler, edit[slot].slot = slot;
					if (edit[slot].item_class == 1)
						for (int n = 0; n < edit[slot].bag_slots; ++n)
							if (int16_t inside = Inventory::BagSlot(slot, n); !edit.count(inside))
								edit[inside] = filler, edit[inside].slot = inside;
				}
				ZoneItem held = filler;
				held.name = "Dropped Filler";
				held.slot = Inventory::Cursor;
				held.slots = 0; // nowhere to wear it
				edit[Inventory::Cursor] = held;
				size_t chat_before = frontend.Chat().Lines().size();
				h.RunUntil([] { return false; }, 100); // the character view takes clicks with an item held
				if (const SidlWindow::Control *view = inventory->Find("IW_CharacterView"))
					click(view->rect.x + view->rect.w / 2, view->rect.y + view->rect.h / 2);
				printf("[preview] no room: cursor %s\n", frontend.Zone().Items().count(Inventory::Cursor) ? "busy" : "free");
				for (size_t i = chat_before; i < frontend.Chat().Lines().size(); ++i)
					printf("[preview] no room chat: %s\n", frontend.Chat().Lines()[i].text.c_str());
				edit = kept;
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

		// The give window: an item picked up and the left button on an NPC (no merchant or banker)
		// with it, the replay's zone saying yes as the zone does for an NPC; the item's in the give
		// window; Give, and the zone's OP_FinishTrade closes it. Then again with Cancel.
		{
			const ZoneSpawn *npc = nullptr;
			const ZoneSpawn &me_spawn = frontend.Zone().Self();
			(void)me_spawn;
			for (const auto &[id, spawn] : frontend.Zone().Spawns())
				if (spawn.npc == 1 && spawn.class_id != 16 && spawn.class_id != 32 && spawn.race >= 1 && spawn.race <= 12 &&
				    !npc)
					npc = &spawn;
			ZoneView *view = frontend.ZoneScene();
			if (npc && view) {
				uint16_t npc_id = npc->id;
				size_t chat_before = frontend.Chat().Lines().size();
				auto give_round = [&](const char *finish) {
					windows.Show("InventoryWindow", true);
					SidlWindow *inventory = windows.Find("InventoryWindow");
					int lone = -1;
					for (const auto &[slot, item] : frontend.Zone().Items())
						if (slot >= Inventory::FirstGeneral && slot <= Inventory::LastGeneral && item.item_class == 0 &&
						    !Inventory::Stacks(item) && lone < 0)
							lone = slot;
					if (lone < 0 || !inventory)
						return;
					windows.Raise("InventoryWindow");
					if (const SidlWindow::Control *c = inventory->Find("InvSlot" + std::to_string(lone))) {
						printf("[preview] give: picking up slot %d at %d,%d, under it %s\n", lone, c->rect.x + c->rect.w / 2,
							c->rect.y + c->rect.h / 2, windows.ScreenAt(c->rect.x + c->rect.w / 2, c->rect.y + c->rect.h / 2).c_str());
						click(c->rect.x + c->rect.w / 2, c->rect.y + c->rect.h / 2);
					}
					printf("[preview] give: quantity %d (up %d), busy %d, coins on the cursor %d\n",
						windows.Shown("QuantityWnd"), frontend.WindowState().QuantityUp(), frontend.WindowState().busy,
						Inventory::CursorMoneyType(frontend.Zone().Profile()));
					printf("[preview] give: %s on the cursor\n",
						frontend.Zone().Items().count(Inventory::Cursor) ? frontend.Zone().Items().at(Inventory::Cursor).name.c_str()
						                                                 : "nothing");
					windows.Show("InventoryWindow", false);
					view->SetPlayerPosition(npc->x, npc->y - 8, npc->z, 0);
					h.RunUntil([] { return false; }, 1000);
					h.Snapshot("g0-at-npc");
					int nx = -1, ny = -1;
					for (int y = 8; y < frontend.ScreenHeight() && nx < 0; y += 8)
						for (int x = 8; x < frontend.ScreenWidth() && nx < 0; x += 8)
							if (view->PickSpawn(x, y, frontend.Zone().MySpawnId()) == npc_id && !windows.Over(x, y))
								nx = x, ny = y;
					printf("[preview] give: %s on the screen at %d,%d, %.1f away\n", npc->name.c_str(), nx, ny,
						frontend.Zone().SpawnDistance(npc_id));
					if (nx < 0)
						return;
					click(nx, ny);
					h.RunUntil([&] { return windows.Shown("GiveWnd"); }, 2000);
					if (const SidlWindow *give = where("GiveWnd")) {
						const SidlWindow::Control *slot0 = give->Find("GVW_MyItemSlot0");
						printf("[preview] give: slot 0 shows '%s', target %d\n", slot0 ? slot0->tooltip.c_str() : "(none)",
							frontend.Target());
						h.RunUntil([] { return false; }, 200);
						h.Snapshot(std::string("g1-give-") + finish);
						if (const SidlWindow::Control *b = give->Find(finish))
							click(b->rect.x + b->rect.w / 2, b->rect.y + b->rect.h / 2);
						h.RunUntil([&] { return !windows.Shown("GiveWnd"); }, 2000);
						where("GiveWnd");
						printf("[preview] give: after %s, trade slots %zu, cursor %s\n", finish,
							static_cast<size_t>(std::count_if(frontend.Zone().Items().begin(), frontend.Zone().Items().end(),
								[](const auto &p) { return p.first >= Inventory::FirstTrade && p.first < 4000; })),
							frontend.Zone().Items().count(Inventory::Cursor) ? "busy" : "free");
					}
				};
				give_round("GVW_Give_Button");
				give_round("GVW_Cancel_Button");
				for (size_t i = chat_before; i < frontend.Chat().Lines().size(); ++i)
					printf("[preview] give chat: %s\n", frontend.Chat().Lines()[i].text.c_str());
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
			// The spell display: alt+click on the first book slot and on the first gem with a spell
			// (printed, then closed by a second), and the right button held on the slot and let go.
			{
				auto alt_click = [&](int ax, int ay) {
					InputEvent event{InputEvent::Type::MouseMove, ax, ay};
					frontend.OnEvent(event);
					event.alt = true;
					event.type = InputEvent::Type::MouseDown;
					frontend.OnEvent(event);
					event.type = InputEvent::Type::MouseUp;
					frontend.OnEvent(event);
					frontend.Update(NowMs());
				};
				auto show = [&](const char *what) {
					const SidlWindow *display = windows.Find("ItemDisplayWindow");
					if (!display)
						return;
					printf("[preview] %s: item display %s '%s'\n", what, windows.Shown("ItemDisplayWindow") ? "shown" : "hidden",
						display->Root().text.c_str());
					if (!windows.Shown("ItemDisplayWindow"))
						return;
					const SidlWindow::Control *description = display->Find("ItemDescription");
					std::string text = description ? description->text : "";
					for (size_t start = 0; start < text.size();) {
						size_t br = text.find("<BR>", start);
						printf("[preview]   %s\n", text.substr(start, br == std::string::npos ? std::string::npos : br - start).c_str());
						if (br == std::string::npos)
							break;
						start = br + 4;
					}
				};
				int sx, sy;
				windows.Show("ItemDisplayWindow", false); // the icon test's item, else the first click closes it
				if (me.spell_book[0] != -1 && at("SpellBookWnd", "SBW_Spell0", sx, sy)) {
					alt_click(sx, sy);
					show("alt+click on book slot 0");
					h.Snapshot("s1-spell-display");
					alt_click(sx, sy);
					show("alt+click on it again");
					frontend.OnEvent({InputEvent::Type::RightDown, sx, sy});
					h.RunUntil([] { return false; }, 900);
					show("right button held on it");
					frontend.OnEvent({InputEvent::Type::RightUp, sx, sy});
					frontend.Update(NowMs());
					show("let go");
				}
				for (int g = 0; g < ZoneProfile::Gems; ++g)
					if (spells.Find(me.mem_spells[g]) && at("CastSpellWnd", "CSPW_Spell" + std::to_string(g), sx, sy)) {
						alt_click(sx, sy);
						show(("alt+click on gem " + std::to_string(g)).c_str());
						frontend.OnEvent({InputEvent::Type::RightDown, sx, sy});
						h.RunUntil([] { return false; }, 900);
						show("right button held on it");
						frontend.OnEvent({InputEvent::Type::RightUp, sx, sy});
						frontend.Update(NowMs());
						show("let go");
						break;
					}
			}
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
		// Auto attack: Q (Autoattack) turns it on and off standing, said; /attack on and off say
		// nothing; sitting turns it off ("Auto attack is off."); while it's on the player window's red
		// lasso is drawn every other frame.
		{
			auto attack_key = [&] {
				InputEvent e;
				e.type = InputEvent::Type::Key;
				e.scan = 0x10; // Q
				frontend.OnEvent(e);
				e.type = InputEvent::Type::KeyUp;
				frontend.OnEvent(e);
				frontend.Update(NowMs());
			};
			auto attack_report = [&](const char *when) {
				SidlWindow *player = frontend.Windows().Find("PlayerWindow");
				int lit = 0;
				for (int i = 0; i < 6; ++i) {
					frontend.Update(NowMs());
					lit += player && player->Root().lasso != 0;
				}
				const auto &lines = frontend.Chat().Lines();
				printf("[preview] auto attack %s: %d, stance %d, lasso %d of 6 frames, last chat '%s'\n", when,
					frontend.AutoAttack(), frontend.Zone().Self().stance, lit, lines.empty() ? "" : lines.back().text.c_str());
			};
			if (frontend.Chat().Typing())
				h.Key(InputEvent::Key::Escape); // the chat line's left open: Q would type
			attack_report("at first");
			attack_key();
			attack_report("after Q");
			if (SidlWindow *player = frontend.Windows().Find("PlayerWindow")) {
				for (int i = 0; i < 2 && player->Root().lasso == 0; ++i)
					frontend.Update(NowMs());
				h.Snapshot("q1-auto-attack-lasso");
			}
			attack_key();
			attack_report("after Q again");
			h.Key(InputEvent::Key::Enter);
			h.Type("/attack on");
			h.Key(InputEvent::Key::Enter);
			attack_report("after /attack on");
			h.Key(InputEvent::Key::Enter);
			h.Type("/sit");
			h.Key(InputEvent::Key::Enter);
			attack_report("after /sit");
			h.Key(InputEvent::Key::Enter);
			h.Type("/stand");
			h.Key(InputEvent::Key::Enter);
			h.RunUntil([] { return false; }, 300);
			h.Key(InputEvent::Key::Enter);
			h.Type("/attack");
			h.Key(InputEvent::Key::Enter);
			attack_report("after /attack");
			h.Key(InputEvent::Key::Enter);
			h.Type("/attack off");
			h.Key(InputEvent::Key::Enter);
			attack_report("after /attack off");
		}
		// The combat page's skill buttons: the right button on the first lists what it can take
		// (SkillsSelectWindow), Kick (if we have it) goes on it, and a click uses it: the combat
		// timer, CanIHitTarget's say, OP_CombatAbility. /doability 7 is the same button.
		if (SidlWindow *actions = frontend.Windows().Find("ActionsWindow")) {
			WindowManager &windows = frontend.Windows();
			windows.Show("ActionsWindow", true);
			windows.MoveTo("ActionsWindow", 20, 20);
			windows.Raise("ActionsWindow");
			if (SidlWindow::Control *tabs = actions->Find("ACTW_ActionsSubwindows"))
				for (int i = 0; i < static_cast<int>(tabs->children.size()); ++i)
					if (actions->Item(tabs->children[i]).def->screen_id == "ActionsCombatPage")
						actions->SelectTab(*tabs, i);
			frontend.Update(NowMs());
			auto press = [&](const Rect &r, bool right) {
				Rect area = frontend.ScreenArea();
				int x = area.x + r.x + r.w / 2, y = area.y + r.y + r.h / 2;
				frontend.OnEvent({InputEvent::Type::MouseMove, x, y});
				frontend.OnEvent({right ? InputEvent::Type::RightDown : InputEvent::Type::MouseDown, x, y});
				frontend.OnEvent({right ? InputEvent::Type::RightUp : InputEvent::Type::MouseUp, x, y});
				frontend.Update(NowMs());
			};
			size_t seen = frontend.Chat().Lines().size();
			auto new_chat = [&](const char *when) {
				for (; seen < frontend.Chat().Lines().size(); ++seen)
					printf("[preview] skills %s chat: %s\n", when, frontend.Chat().Lines()[seen].text.c_str());
			};
			// Whatever drag an earlier step left going (it keeps the mouse from the windows) ends.
			frontend.OnEvent({InputEvent::Type::RightUp, 1, 1});
			frontend.OnEvent({InputEvent::Type::MouseUp, 1, 1});
			frontend.Update(NowMs());
			if (const SidlWindow::Control *button = actions->Find("ACP_FirstAbilityButton")) {
				printf("[preview] combat button at %d,%d %dx%d, shown %d, window shown %d\n", button->rect.x, button->rect.y,
					button->rect.w, button->rect.h, actions->Shown(*button), windows.Shown("ActionsWindow"));
				printf("[preview] combat button enabled %d\n", button->enabled);
				press(button->rect, true);
				SidlWindow *select = windows.Find("SkillsSelectWindow");
				SidlWindow::Control *list = select ? select->Find("SKSW_SkillSelectorList") : nullptr;
				printf("[preview] skill select shown %d:", windows.Shown("SkillsSelectWindow"));
				int kick = -1;
				if (list)
					for (size_t r = 0; r < list->rows.size(); ++r) {
						printf(" '%s'", list->rows[r][0].c_str());
						if (list->rows[r][0] == "Kick")
							kick = static_cast<int>(r);
					}
				printf("\n");
				if (list && kick >= 0) {
					press(select->ListRowRect(*list, kick), false);
					printf("[preview] first combat button now '%s', select shown %d\n", button->text.c_str(),
						windows.Shown("SkillsSelectWindow"));
					h.Snapshot("s1-combat-skill-button");
					// The nearest NPC (not an invisible camp marker) as the target, and us stood 6 in
					// front of it facing it.
					const ZoneSession &zone = frontend.Zone();
					const ZoneSpawn *npc = nullptr;
					float best = 0;
					for (const auto &[id, spawn] : zone.Spawns()) {
						float d = std::hypot(spawn.x - zone.Profile().x, spawn.y - zone.Profile().y);
						if (spawn.npc == 1 && spawn.race != 127 && spawn.race != 240 && (!npc || d < best))
							npc = &spawn, best = d;
					}
					ZoneView *view = frontend.ZoneScene();
					if (npc && view && frontend.Zone().Profile().cur_hp > 0) {
						h.Key(InputEvent::Key::Enter);
						h.Type("/target " + npc->name);
						h.Key(InputEvent::Key::Enter);
						h.RunUntil([] { return false; }, 300);
						printf("[preview] target %u (%s)\n", frontend.Target(), npc->DisplayName().c_str());
						view->SetPlayerPosition(npc->x, npc->y - 6, npc->z, 0);
						h.RunUntil([] { return false; }, 1000);
					}
					press(button->rect, false);
					printf("[preview] kick used: button down %d\n", button->checked);
					new_chat("first use");
					press(button->rect, false); // still on its timer: nothing
					new_chat("second use");
					h.Key(InputEvent::Key::Enter);
					h.Type("/doability 7");
					h.Key(InputEvent::Key::Enter);
					new_chat("/doability 7");
					h.Key(InputEvent::Key::Enter);
					h.Type("/doability");
					h.Key(InputEvent::Key::Enter);
					new_chat("/doability");
					// /filter: a toggle, a three-way, the bard songs' four, the usage; /serverfilter.
					for (const char *line : {"/filter othersmisses", "/filter othersmisses", "/filter pcspells",
						     "/filter bardsongs", "/filter dmgshield", "/filter nonsense", "/serverfilter on",
						     "/serverfilter off", "/filter dmgshield", "/filter pcspells", "/filter pcspells",
						     "/filter bardsongs", "/filter bardsongs", "/filter bardsongs"}) {
						h.Key(InputEvent::Key::Enter);
						h.Type(line);
						h.Key(InputEvent::Key::Enter);
						new_chat(line);
					}
				}
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
				printf("[preview] character: %s level %u %s %s zone %u, holding %u and %u\n", c.name.c_str(), c.level, RaceName(c.race),
					ClassName(c.class_id), c.zone, c.equip[7], c.equip[8]);

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
			if (create_luclin) {
				// Each race and gender in turn: what the model wears before any face is picked.
				for (int race = 0; race < 14; ++race)
					for (const char *gender : {"CC0_Male_Button", "CC0_Female_Button"}) {
						h.ClickId("CC0_Race" + std::to_string(race) + "_Button");
						h.ClickId(gender);
						h.RunUntil([] { return false; }, 50);
						render::CharacterAppearance a = frontend.Creation()->Appearance();
						printf("[preview] race %u gender %u: face %u hair %u/%u beard %u/%u eyes %u/%u\n", a.race, a.gender, a.face,
							a.hair_style, a.hair_color, a.beard, a.beard_color, a.eye_color1, a.eye_color2);
						if (race == 0 && gender[8] == 'M')
							h.Snapshot("c0-human-male");
					}
				h.ClickId("CC0_Race2_Button");   // Dwarf
				h.ClickId("CC0_Class13_Button"); // Warrior
				h.ClickId("CC0_Male_Button");
			}
			else {
				h.ClickId("CC0_Race3_Button");  // Erudite
				h.ClickId("CC0_Class14_Button"); // Wizard
				h.ClickId("CC0_Female_Button");
			}
			h.ClickId("CC0_Face_Button");
			h.RunUntil([] { return false; }, 200);
			h.Snapshot("c0-face");
			auto features = [&](const char *what) {
				render::CharacterAppearance a = frontend.Creation()->Appearance();
				printf("[preview] %-22s face %u hair %u/%u beard %u/%u eyes %u/%u\n", what, a.face, a.hair_style, a.hair_color,
					a.beard, a.beard_color, a.eye_color1, a.eye_color2);
			};
			{
				std::string rows;
				for (const char *row : {"Face", "Hair", "Hair_Color", "Eyes", "Beard", "Beard_Color", "Wode"}) {
					const auto *control = frontend.Creation()->Window().Find(std::string(row) + "_Up_Button");
					if (control && control->visible)
						rows += std::string(rows.empty() ? "" : ", ") + row;
				}
				printf("[preview] face picker rows: %s\n", rows.c_str());
			}
			features("picker opened");
			for (const char *button : {"Face_Up_Button", "Hair_Up_Button", "Hair_Up_Button", "Hair_Up_Button", "Hair_Up_Button",
					 "Hair_Color_Up_Button", "Hair_Color_Down_Button", "Eyes_Up_Button", "Eyes_Down_Button", "Beard_Up_Button",
					 "Beard_Up_Button", "Beard_Color_Up_Button", "Random_Button"})
				if (h.ClickId(button))
					features(button);
			h.ClickId("Face_Up_Button");
			features("Face_Up_Button");
			h.RunUntil([] { return false; }, 200);
			h.Snapshot("c0-face-picked");
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
				if (frontend.DialogOpen() || frontend.ConfirmationOpen()) {
					printf("[preview] dialog: %s\n",
						(frontend.DialogOpen() ? frontend.DialogText() : frontend.ConfirmationText()).c_str());
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
				NewCharacter sent = frontend.Creation()->Character();
				printf("[preview] sending face %u hair %u/%u beard %u/%u eyes %u/%u\n", sent.face, sent.hair_style, sent.hair_color,
					sent.beard, sent.beard_color, sent.eye_color1, sent.eye_color2);
				h.ClickId("CC_Next_Button");
				h.RunUntil([&] { return frontend.CurrentScreen() == Screen::CharacterSelect; }, 10000);
				if (frontend.CurrentScreen() == Screen::CharacterSelect)
					created = name;
				else {
					printf("[preview] dialog: %s\n",
						(frontend.DialogOpen() ? frontend.DialogText() : frontend.ConfirmationText()).c_str());
					if (!h.ClickId("OK_Button"))
						h.Key(InputEvent::Key::Enter);
				}
			}
			if (created.empty())
				return 1;
			h.RunUntil([] { return false; }, 300);
			h.Snapshot("c4-created");
			for (const auto &c : frontend.Characters())
				if (!c.Empty())
					printf("[preview] character: %s level %u %s %s zone %u face %u hair %u/%u beard %u/%u eyes %u/%u\n", c.name.c_str(),
						c.level, RaceName(c.race), ClassName(c.class_id), c.zone, c.face, c.hair_style, c.hair_color, c.beard,
						c.beard_color, c.eye_color1, c.eye_color2);
			// Clean up: delete it again.
			if (frontend.SelectedCharacter() >= 0 && frontend.Characters()[frontend.SelectedCharacter()].name == created) {
				h.ClickId("Delete_Button");
				printf("[preview] delete asks: %s (buttons %s)\n", frontend.ConfirmationText().c_str(),
					frontend.ButtonsEnabled() ? "on" : "off");
				h.ClickId("Yes_Button");
				h.RunUntil([&] {
					for (const auto &c : frontend.Characters())
						if (c.name == created)
							return false;
					return true;
				}, 5000);
				printf("[preview] deleted %s\n", created.c_str());
				h.RunUntil([] { return false; }, 100);
				printf("[preview] buttons after the dialog: %s\n", frontend.ButtonsEnabled() ? "on" : "off");
			}
			h.Key(InputEvent::Key::Escape);
			return 0;
		}

		if (explore) {
			// Explore mode: in, the chase camera, a walk forward, a click on another character,
			// then out with Escape.
			h.RunUntil([&] { return frontend.CharacterScene() && frontend.CharacterScene()->Loaded(); }, 20000);
			h.Snapshot("x0-select");
			const CharacterSelectView *view = frontend.CharacterScene();
			printf("[preview] doors: %zu\n", view->DoorCount());
			h.ClickId("Explore_Button");
			printf("[preview] exploring: %d\n", frontend.Exploring());
			h.Snapshot("x1-explore");
			auto frames = [&](int ms) {
				uint64_t end = NowMs() + static_cast<uint64_t>(ms);
				int w = frontend.ScreenWidth(), hh = frontend.ScreenHeight();
				std::vector<uint32_t> pixels(static_cast<size_t>(w) * hh);
				Canvas canvas(pixels.data(), w, hh, w);
				while (NowMs() < end) {
					frontend.Update(NowMs());
					frontend.Render(canvas, NowMs());
				}
			};
			h.Key(InputEvent::Key::F9);
			frames(300);
			h.Snapshot("x2-chase");
			for (const auto &tag : view->NameTags())
				printf("[preview] name tag: slot %d at %d, %d\n", tag.slot, tag.x, tag.y);
			int selected = frontend.SelectedCharacter();
			for (const auto &tag : view->NameTags())
				if (tag.slot >= 0) {
					// On the body, under the name.
					for (int dy = 20; dy <= 120 && frontend.SelectedCharacter() == selected; dy += 10)
						h.Click(tag.x, tag.y + dy);
					break;
				}
			printf("[preview] selected slot %d -> %d, exploring %d\n", selected, frontend.SelectedCharacter(), frontend.Exploring());
			frames(300);
			render::Vec3 before = view->Position();
			h.Key(InputEvent::Key::Up);
			frames(1500);
			h.KeyUp(InputEvent::Key::Up);
			frames(300);
			render::Vec3 after = view->Position();
			printf("[preview] walked from %.1f, %.1f, %.1f to %.1f, %.1f, %.1f\n", before.x, before.y, before.z, after.x,
				after.y, after.z);
			h.Snapshot("x3-walked");
			h.Snapshot("x4-clicked");
			// The wheel: 4 notches out, then 30 in (back to first person).
			frontend.OnEvent({InputEvent::Type::Wheel, 400, 300, -4});
			frames(100);
			h.Snapshot("x4-zoomed-out");
			printf("[preview] wheel out 4: first person %d\n", view->FirstPerson());
			frontend.OnEvent({InputEvent::Type::Wheel, 400, 300, 30});
			frames(100);
			printf("[preview] wheel in 30: first person %d\n", view->FirstPerson());
			h.Key(InputEvent::Key::Escape);
			printf("[preview] after Escape exploring: %d\n", frontend.Exploring());
			frames(300);
			h.Snapshot("x5-left");
			h.Key(InputEvent::Key::Escape);
			return 0;
		}

		if (stop_at_charselect) {
			h.RunUntil([] { return false; }, 2000);
			h.Snapshot("7-character-select-turned");
			// Down to the next character, with what it holds (its emitters: a torch's flame).
			h.Key(InputEvent::Key::Down);
			h.RunUntil([] { return false; }, 2000);
			h.Snapshot("7-character-select-next");
			if (const CharacterSelectView *scene = frontend.CharacterScene(); scene && scene->Model())
				printf("[preview] next character: %s, %zu emitters, holding %u and %u\n", scene->Model()->Code().c_str(),
					scene->Model()->Emitters().size(), scene->Shown() ? scene->Shown()->equip[7] : 0, scene->Shown() ? scene->Shown()->equip[8] : 0);
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
			if (shop)
				return ShopTest(frontend, h);
			if (ground)
				return GroundTest(frontend, h);
			if (commands)
				return CommandsTest(frontend, h);
			if (group_test)
				return GroupTest(frontend, h);
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
