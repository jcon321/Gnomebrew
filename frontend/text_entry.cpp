#include "text_entry.h"

#include "edit_box.h"

namespace
{
	const char *const EntryId = "Entry";
	const char *const PromptId = "Prompt";
	const char *const OkId = "OK";
	const char *const CancelId = "Cancel";
}

void TextEntry::Attach(WindowManager &windows)
{
	m_windows = &windows;
	m_active = false;
	m_results.clear();
	windows.Show(Screen, false);
}

void TextEntry::Activate(const std::string &owner, int id, const std::string &prompt, const std::string &text,
	int min_chars, int max_chars)
{
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	if (!wnd)
		return;
	m_owner = owner;
	m_id = id;
	m_min = min_chars;
	if (SidlWindow::Control *label = wnd->Find(PromptId))
		label->text = prompt;
	if (SidlWindow::Control *entry = wnd->Find(EntryId)) {
		entry->text = text;
		entry->max_chars = static_cast<size_t>(max_chars);
		EditBox::SetSel(*entry, 0, -1);
		wnd->Focus(EntryId);
	}
	Update();
	// CXWnd::Center: the middle of the screen.
	Rect r = m_windows->WindowRect(Screen);
	m_windows->MoveTo(Screen, (m_windows->ScreenWidth() - r.w) / 2, (m_windows->ScreenHeight() - r.h) / 2);
	m_windows->Show(Screen, true);
	m_windows->Raise(Screen);
	m_active = true;
}

void TextEntry::Update()
{
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	if (!wnd)
		return;
	if (m_active && !m_windows->Shown(Screen)) {
		Finish(false);
		return;
	}
	const SidlWindow::Control *entry = wnd->Find(EntryId);
	if (SidlWindow::Control *ok = wnd->Find(OkId))
		ok->enabled = entry && static_cast<int>(entry->text.size()) >= m_min;
}

bool TextEntry::Click(const std::string &window, const std::string &control)
{
	if (window != Screen)
		return false;
	if (control == OkId || control == CancelId) {
		if (m_active)
			Finish(control == OkId);
		return true;
	}
	return false;
}

bool TextEntry::Enter()
{
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	if (!m_active || !wnd || !wnd->Typing())
		return false;
	Update();
	const SidlWindow::Control *ok = wnd->Find(OkId);
	if (ok && ok->enabled)
		Finish(true);
	return true;
}

void TextEntry::Finish(bool ok)
{
	SidlWindow *wnd = m_windows ? m_windows->Find(Screen) : nullptr;
	Result result;
	result.owner = m_owner;
	result.id = m_id;
	result.ok = ok;
	if (ok && wnd)
		if (const SidlWindow::Control *entry = wnd->Find(EntryId))
			result.text = entry->text;
	m_results.push_back(std::move(result));
	m_active = false;
	if (wnd)
		wnd->Focus("");
	if (m_windows)
		m_windows->Show(Screen, false);
}

std::vector<TextEntry::Result> TextEntry::TakeResults()
{
	std::vector<Result> results;
	results.swap(m_results);
	return results;
}
