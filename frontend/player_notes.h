#pragma once

// eqgame's player notes window (CPlayerNotesWnd @0042e779 on, EQUI_PlayerNotesWindow.xml's
// "PlayerNotesWindow") and /note (@004fbb72):
//   - made with the UI: PlayerNotesInput (a multi-line box, 10000 characters at most) takes up to
//     10000 bytes of notes.txt in the game's folder, as they are;
//   - /note alone opens it (Activate @0042e915; eqstr 13224 without the window); /note with text
//     is AppendText (@0042ea6c): "[" + ctime's date with its newline made a space + "]" and " " +
//     the text + "\n", the text's item links taken out (a 0x12 then '0': those 8 characters and the
//     next 0x12), after what's in the box; under 10000 the box takes it and SaveNotes, else 13224.
//     eqgame loops for ever on a 0x12 not followed by '0'; ours leaves it in.
//   - WndNotification (@0042e981): Enter in the box (6) puts a "\n" in the selection's place.
//   - Deactivate (@0042e95c, Done, the close box): SaveNotes (@0042e9cd), notes.txt written ("w")
//     with the box's text.
//   - The old UI's /note (DAT_008092d8 0) appends "[date] text\n" to notes.txt instead and says
//     13223; not ours.

#include <cstdint>
#include <functional>
#include <string>

class WindowManager;

class PlayerNotes
{
public:
	static constexpr const char *Screen = "PlayerNotesWindow";
	static constexpr const char *InputId = "PlayerNotesInput";
	static constexpr size_t MaxChars = 10000;

	struct Host
	{
		std::function<std::string(uint32_t, const std::string &)> text; // eqstr_en.txt's, or the fallback
		std::function<void(const std::string &)> chat;                  // print_chat_dsp_chat_2
		std::function<std::string()> now;                               // ctime of the time now
	};

	// The constructor's: notes.txt (in `eq_dir`) into the box, the window hidden.
	void Attach(WindowManager &windows, const std::string &eq_dir, Host host);
	void Command(const std::string &args); // /note
	void Activate();
	void Deactivate();
	void AppendText(const std::string &text);
	bool Active() const { return m_active; }
	void Frame(); // a close by its box is Deactivate
	// Enter typed in the box: a new line. False when it hasn't the focus.
	bool Enter();
	bool Save() const; // SaveNotes

private:
	WindowManager *m_windows = nullptr;
	std::string m_path;
	Host m_host;
	bool m_active = false; // +0x134
};
