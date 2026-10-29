---
name: update-readme
description: Record in my-client's README.md how the original EQ client works, as found while researching, decompiling or porting it. Use after porting or fixing anything from eqgame/eqgfx/eqmain (before reporting it done or committing), whenever a session learns a game rule, formula, constant, file-format detail, packet layout or decompile pitfall, whenever the user asks for a custom or unique change the original client doesn't have (it goes in the README's Custom changes section), and when a README statement turns out wrong or stale. Edits the existing sections; never a separate notes file.
---

# Update README.md

`README.md` (at the root of my-client) is the reference on how the original client behaves and how
my-client reproduces it. It's what the next session reads before porting something, so a finding
that stays only in chat, a commit message or a code comment is lost. `TODO.md` (see the
`update-todo` skill) says what's missing. The README says what's true about the original and
what we do.

## When

- A port or fix is done: update the README in the same commit as the code, alongside TODO.md.
- You learned something about the original client, whether or not it's ported yet:
  - a rule or formula (`v * 0.625 + push * 0.8`, AC's `* 1000 / 847`)
  - a constant or table (heights, radii, timings, colors, string ids)
  - a file format detail (WLD fragment fields, `.emt` columns, `spellsnew.edd`)
  - a packet's layout, order or byte order
  - which function does what, at what address
  - a mislabeled function in TAKP's symbol lists (`initializeHotkeyLabels` is the animation table)
  - a TAKP `eqgame.dll` or Zeal change to stock behavior
- The user asks for a custom or unique change, meaning something the original client doesn't do
  (an addition of ours, an option, or a TAKP eqgame.dll or Zeal behavior taken over stock eqgame).
  Add it to the **Custom changes** section, then see "Custom changes" below.
- A README statement is wrong or stale: something says "not yet" but now exists, a flag or ini
  key is missing, or a file or function moved. Correct it in place. Don't leave the old claim
  and add a contradicting one.

## How

1. Collect the findings. Read `git diff`, `git diff --cached` and `git log` since the session
   began, and what this conversation found in the decompile. Look at new code comments that
   explain the original (`as eqgame's X does`, an address, a magic number with a reason). Each
   one is a README candidate.
2. Find the section it belongs to: Running the GUI (windows, ini keys, controls), Console
   client / Preview tool (flags), How the front end was recovered, Character select, Character
   creation, In the zone, Sound, Protocol. A new system with several findings (spells, items and
   stats) gets its own `##` section placed near related ones. Add to an existing bullet before
   starting a new one.
3. Write each finding so it can be picked up cold:
   - Name the original function the way the decompile does: `CDisplay::SetFog`, or
     `CEverQuest::DoCharacterSelection` (@0053b9cf) when the address helps.
   - Give the real numbers and units: server or WLD coordinates, 0..512 headings, 50 ms ticks,
     units a second.
   - Name the file that ports it in backticks (`frontend/zone_doors.cpp`).
   - Say what's an approximation, or ours rather than the client's ("The collision is ours").
   - Note how it was checked when that matters: against the exe for every combination, against
     a live server, or in Ghidra's emulator.
4. Keep usage up to date as well. New ini keys go in the sample `my-client.ini` and its bullet
   list, in `frontend/settings_file.cpp`'s order. New `eqclient` flags go in the Console client
   block, copied from `console/main.cpp`'s usage text. New preview flags go in the Preview tool
   section.
5. Match the voice. Use plain declarative prose in the present tense ("the zone answers
   `OP_MoveDoor`"), eqgame's names, and `code` in backticks. Wrap lines near 100 columns. Use
   bold lead-ins on bullets (`- **Water** (...)`). Use "we" or "ours" only for my-client's side.
   Don't write dates, session notes or "now"/"no longer". The README describes the client, not
   its history.
6. Keep Custom changes separate from the original's behavior (see below).
7. In your summary to the user, give the README diff in a line or two: the sections touched and
   the findings added.

## Custom changes

The README's `## Custom changes` section (after Running the GUI) lists everything my-client does
on purpose that stock eqgame doesn't. Everywhere else, the README describes the original client.

- Add an entry when the user asks for a custom or unique change. Give it a bold name, what it
  does, and its ini key or flag. Say where it comes from when it isn't ours (TAKP's eqgame.dll,
  Zeal) and what eqgame does instead.
- Where a custom change also shows up in a system's section, mark it there too, e.g.
  "(ours, see Custom changes)", so it isn't read as eqgame's behavior.
- Only intentional changes go here. A stand-in that will be replaced by a port is not a custom
  change, for example our own collision or the 5 s spawn stop. Describe it in its section as ours
  and track it in TODO.md.
- When it's unclear whether something is custom, ask the user. Don't guess.
- If a custom change is removed or made faithful, take it out of the section.

## Don't

- Don't paste commit messages or decompiled code in. Write what it means.
- Don't record server rules as the client's. If the zone decides it, say the zone does.
- Don't move "still to do" lists here. A short "Not yet:" at the end of a section is fine when it
  bounds what the section describes. The full list belongs in TODO.md.
- Don't grow the intro. New features go in their sections. The intro only changes when what the
  client *is* changes.
