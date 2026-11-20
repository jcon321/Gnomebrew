#pragma once

// eqgame's CEditWnd (@005a1ed0 on), for an edit box (the chat's input, the quantity window's, the
// friends window's...):
//   - the selection: SidlScreen and SidlPiece (+0x100, +0x104), indexes into the text as it shows
//     (an item tag counting as its name; ConvertIndexPrintableToTagged turns them into the tagged
//     text's), and +0x208, which end the caret is at;
//   - HandleKeyboardMsg (@005a3010), with the keyboard flags (Shift 1, Ctrl 2, Alt 4): a character
//     (and Space) replaces the selection (ReplaceSelection @005a3f00, through FilterInputStr
//     @005a3e30, no further than the box's limit); Backspace takes the selection, or the character
//     (or the tag) before the caret; Delete the selection or what's after the caret, and Shift+Delete
//     everything (SetText ""); Shift with Left, Right, Home and End moves the caret, a tag at a
//     time, dropping the selection (without Shift they're the game's keys, its turning);
//   - HandleLButtonDown (@005a3660): the caret where the mouse went down (SelectableCharFromPoint
//     @005a2660), with Shift the selection from the other end to there; HandleMouseMove
//     (@005a3770) while the button's down drags the selection out;
//   - RenderEditWindowText (@005a2c00) and CalculateTextOffset (@005a4300): from the first shown
//     character, which follows the caret; a selection on a box of the text's color, its text in the
//     inverse; DrawCaret (@005a2f40): a line in the text's color, shown 512 ms of every 1024 while
//     the box has the focus.
// A multi-line box (Style_Multiline: CEditWnd's style 0x10000) is laid out in lines by
// CTextureFont::WrapTextToWidth (@005a4b00: a line breaks after its last space, or before the
// character that overflowed, and at a '\n'), drawn from its top less its scroll position (VScrollPos);
// SelectableCharFromPoint takes the row from y, Shift+Home and Shift+End go to the caret's line's
// start and end, and GetCaretPt shows a caret at a wrapped line's start at the end of the line
// before when uk1[1] says so (a click right of the left edge, Right and End set it; Left and Home
// clear it). CalculateScrollRange (@005a4230) keeps the position within the lines' height past the
// box's, and EnsureCaretVisible (@005a44a0, after every key and click) scrolls the caret's line in.
// Up and Down go to the window (notification 0x15, the chat's history), and Enter too (6): the
// style that would type a '\n' (0x40000) isn't set on any box in this eqgame. A box that's not
// editable (SetEditable(false): the book's pages) takes no focus, caret or keys.
// Not here: GetMaskedText's password boxes (none in game), the IME, and a clipboard
// (processClipboardText @005583fe is the voice dictation's text: eqgame has no paste).

#include "sidl.h"

class Canvas;
class TextRenderer;

namespace EditBox
{
	// SetText's, when the text changed from outside since the box last saw it: the caret to its end.
	void Sync(SidlWindow::Control &box);
	int Caret(const SidlWindow::Control &box);
	// SetSel: the selection [start, end) of the shown text (end -1: to the end).
	void SetSel(SidlWindow::Control &box, int start, int end);
	// ReplaceSelection: `text` (already tagged, an item tag allowed) in the selection's place,
	// filtered when `filter`, if the box's limit lets it.
	void ReplaceSelection(SidlWindow::Control &box, const std::string &text, bool filter);
	// HandleKeyboardMsg for a key or a character: true when the box took it.
	bool Key(SidlWindow::Control &box, const InputEvent &event);
	// HandleLButtonDown and HandleMouseMove (the button down) at window pixels.
	void Press(SidlWindow::Control &box, int x, int y, bool shift);
	void Drag(SidlWindow::Control &box, int x, int y);
	// RenderEditWindowText and DrawCaret.
	void Draw(const SidlWindow::Control &box, Canvas &canvas, TextRenderer &text, int height, int weight, bool focused,
		uint64_t now_ms);
}
