#pragma once

// eqgame's CTextEntryWnd (@004377db..@00437be4, EQUI_TextEntryWnd.xml's "TextEntryWnd"): a prompt
// over an edit box, with OK and Cancel. Activate (@00437958) puts the prompt in Prompt, the text in
// Entry all selected, limited to `max_chars`, and the box the focus; then centers it (CXWnd::Center)
// and shows it. Activate2 (@00437b05), as the text changes: OK enabled once it's `min_chars` long.
// Enter in the box (notification 6) is OK while OK's enabled; OK and Cancel, and closing it (9),
// call back (Callback @00437a7f) the window that asked with notification 0x1f, 1 for OK (with the
// text, GetEntryText) or 0, and it hides (HideTextEntryWindow: the focus back to that window).

#include "window_manager.h"

#include <string>
#include <vector>

class TextEntry
{
public:
	static constexpr const char *Screen = "TextEntryWnd";

	void Attach(WindowManager &windows); // the window, hidden
	void Activate(const std::string &owner, int id, const std::string &prompt, const std::string &text, int min_chars,
		int max_chars);
	bool Active() const { return m_active; }
	// OK or Cancel clicked; true when it was one of ours.
	bool Click(const std::string &window, const std::string &control);
	bool Enter(); // the box's Enter: OK, if it's enabled
	void Update(); // Activate2's OK, and a close (its box, or the window menu) as Cancel

	struct Result
	{
		std::string owner; // who asked
		int id = 0;        // what for (the asker's own number)
		bool ok = false;
		std::string text;
	};
	std::vector<Result> TakeResults();

private:
	void Finish(bool ok);

	WindowManager *m_windows = nullptr;
	bool m_active = false;
	std::string m_owner;
	int m_id = 0;
	int m_min = 1;
	std::vector<Result> m_results;
};
