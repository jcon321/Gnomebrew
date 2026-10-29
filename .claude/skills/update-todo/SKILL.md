---
name: update-todo
description: Keep my-client's TODO.md in step with the work. Use after finishing any my-client feature, fix or port (before reporting it done or committing), whenever a session leaves something "not yet" (a stub, an approximation, a skipped branch of an eqgame function, a missing window or packet), and when a TODO.md entry turns out to be stale. Updates the existing items and adds new ones; never a separate notes file.
---

# Update TODO.md

`TODO.md` (at the root of my-client) is the list of what the client still doesn't do. Keep it
true as you work: what this session finished moves out of "Still to do", and what it left
undone goes in.

## When

- A feature, port or fix is done: update TODO.md before you report it or commit, in the same
  commit as the code.
- You left something out on purpose or found a gap: an eqgame branch not ported, a packet field
  ignored, a check approximated, a dependency that doesn't exist yet (auto attack, the alarm
  window, the keymap...). Write it down now; don't leave it only in a code comment or in chat.
- An entry is stale (it lists as missing something that exists, or names a file or function that
  moved). Fix it when you see it.

## How

1. Collect what changed: `git diff` and `git diff --cached` (and `git log` since the session's
   first commit if you've committed), plus what this conversation decided. Look through added
   comments for "not yet", "isn't ours", "for now", "there's no ... yet", "approximat" and
   unported eqgame branches: each one is a TODO candidate.
2. Read `TODO.md` and find the item each change belongs to (by window, packet, or system).
3. For each touched item:
   - Fold what's now done into the item's done description: what it does, the eqgame functions it
     follows (`Name` or `Name (@address)`), and the files (`frontend/foo.*`).
   - Take the done pieces out of its "Still to do", and add what's still missing, specific enough
     to pick up cold: the eqgame function, the packet, the string id, why it's blocked.
   - Fully done: strike the title, `~~**Title**~~`, and keep only a "Still to do" for leftovers.
4. Work that fits no item: a new numbered item in the section it belongs to (UI; Rendering /
   presentation; Infrastructure / tooling), with the next free number. Never renumber: items are
   referred to by number, e.g. "Packet replay (16)".
5. Keep the "Order" list in step: strike what's done there too.
6. Match the file's voice: terse prose, eqgame's names, `code` in backticks, lines wrapped near
   100 columns, sub-lists with `-` under an item. No dates, no session notes, no "we".
7. Show the TODO.md diff in your summary to the user, in a line or two: what you marked done and
   what you added.

## Don't

- Don't record what's done and has nothing left. The item's done description says it once.
- Don't copy the git log or the commit messages in. TODO.md describes the client, not its
  history.
- Don't add a game rule the server owns. TODO.md is for the client showing and handling what the
  server sends.
