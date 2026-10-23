#include "game_windows.h"
#include "game_values.h"
#include "window_manager.h"
#include "zone_session.h"

#include <string>

namespace
{
	constexpr int BuffSlots = 15;    // CBuffWindow's buttons, Buff0..Buff14
	constexpr int GroupMembers = 5;  // the others in a group
	constexpr int HotButtons = 10;   // a page
	constexpr int Pages = 10;        // of socials, and of hot buttons

	void Show(SidlWindow &wnd, const std::string &screen_id, bool shown)
	{
		if (SidlWindow::Control *control = wnd.Find(screen_id))
			control->visible = shown;
	}

	void PlayerWindow(SidlWindow &wnd, const GameValues &values)
	{
		Show(wnd, "PetHP", values.Pet() != nullptr);
	}

	void GroupWindow(SidlWindow &wnd, const GameValues &values)
	{
		const bool invited = false, in_group = false;
		Show(wnd, "FollowButton", invited);
		Show(wnd, "DeclineButton", invited);
		Show(wnd, "InviteButton", !invited);
		Show(wnd, "DisbandButton", !invited);
		if (SidlWindow::Control *disband = wnd.Find("DisbandButton"))
			disband->enabled = in_group;
		if (SidlWindow::Control *invite = wnd.Find("InviteButton")) {
			const ZoneSpawn *target = values.Target();
			invite->enabled = target && target->npc == 0 && target->id != values.zone->MySpawnId();
		}
		for (int i = 1; i <= GroupMembers; ++i)
			Show(wnd, "PetGauge" + std::to_string(i), false);
	}

	void BuffWindow(SidlWindow &wnd)
	{
		const SidlWindow::Control *first = wnd.Find("Buff0");
		if (!first)
			return;
		// A button a pixel apart from the next, down each column, the columns leftward from the right.
		int w = first->def->rect.w, h = first->def->rect.h;
		const Rect &c = wnd.Root().client;
		int columns = c.w / (w + 1), rows = c.h / (h + 1);
		for (int i = 0; i < BuffSlots; ++i) {
			std::string id = "Buff" + std::to_string(i);
			SidlWindow::Control *button = wnd.Find(id);
			if (!button)
				continue;
			int column = rows > 0 ? i / rows : columns;
			button->visible = column < columns;
			button->blank = true;
			if (button->visible)
				wnd.Move(id, {c.Right() - (w + 1) * (column + 1), c.y + (h + 1) * (i % rows), w, h});
		}
	}

	void ActionsWindow(SidlWindow &wnd, const GameValues &values)
	{
		const bool invited = false, in_group = false;
		Show(wnd, "AMP_InviteButton", !invited);
		Show(wnd, "AMP_FollowButton", invited);
		auto me = values.zone->Spawns().find(values.zone->MySpawnId());
		bool sitting = me != values.zone->Spawns().end() && me->second.stance == 110;
		Show(wnd, "AMP_SitButton", !sitting);
		Show(wnd, "AMP_StandButton", sitting);
		Show(wnd, "AMP_RunButton", !values.running);
		Show(wnd, "AMP_WalkButton", values.running);
		if (SidlWindow::Control *disband = wnd.Find("AMP_DisbandButton"))
			disband->enabled = in_group || invited;
		if (SidlWindow::Control *invite = wnd.Find("AMP_InviteButton")) {
			const ZoneSpawn *target = values.Target();
			invite->enabled = target && target->npc == 0 && target->id != values.zone->MySpawnId();
		}
	}

	// CXStr's number constructor as the page labels use it: the page from 1, padded to two places.
	std::string PageLabel(int page)
	{
		std::string label = std::to_string(page + 1);
		return label.size() < 2 ? " " + label : label;
	}

	// The arrows' page, one on or back, round the ends.
	bool StepPage(int &page, const std::string &control, const char *left, const char *right)
	{
		if (control == right)
			page = (page + 1) % Pages;
		else if (control == left)
			page = (page + Pages - 1) % Pages;
		else
			return false;
		return true;
	}

	void SetText(SidlWindow &wnd, const std::string &screen_id, const std::string &text)
	{
		if (SidlWindow::Control *control = wnd.Find(screen_id))
			control->text = text;
	}

	void HotButtonWindow(SidlWindow &wnd, int page)
	{
		SetText(wnd, "HB_CurrentPageLabel", PageLabel(page));
		// CHotButtonWnd::OnProcessFrame (@0041fae9): an empty slot has no text and is checked,
		// so it shows its Pressed picture.
		for (int i = 1; i <= HotButtons; ++i) {
			std::string n = std::to_string(i);
			if (SidlWindow::Control *button = wnd.Find("HB_Button" + n)) {
				button->text.clear();
				button->checked = true;
			}
			Show(wnd, "HB_InvSlot" + n, false);
			Show(wnd, "HB_SpellGem" + n, false);
		}
	}
}

void GameWindowClick(WindowManager &windows, GameWindowPages &pages, const std::string &window, const std::string &control)
{
	// The socials' label keeps the XML's text until the page first changes.
	if (window == "ActionsWindow" && StepPage(pages.social, control, "ASP_SocialPageLeftButton", "ASP_SocialPageRightButton")) {
		if (SidlWindow *wnd = windows.Find(window))
			SetText(*wnd, "ASP_CurrentSocialPageLabel", PageLabel(pages.social));
	}
	if (window == "HotButtonWnd")
		StepPage(pages.hot_button, control, "HB_PageLeftButton", "HB_PageRightButton");
}

void UpdateGameWindows(WindowManager &windows, const GameValues &values, const GameWindowPages &pages)
{
	if (!values.zone)
		return;
	if (SidlWindow *wnd = windows.Find("PlayerWindow"))
		PlayerWindow(*wnd, values);
	if (SidlWindow *wnd = windows.Find("GroupWindow"))
		GroupWindow(*wnd, values);
	if (SidlWindow *wnd = windows.Find("BuffWindow"))
		BuffWindow(*wnd);
	if (SidlWindow *wnd = windows.Find("HotButtonWnd"))
		HotButtonWindow(*wnd, pages.hot_button);
	if (SidlWindow *wnd = windows.Find("ActionsWindow"))
		ActionsWindow(*wnd, values);
}
