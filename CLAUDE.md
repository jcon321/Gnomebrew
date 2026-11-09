# CLAUDE.md

Gnomebrew (this repo, in the `my-client` folder) is a from-scratch EQMac (TAKP-era) EverQuest client in C++20/CMake. It is ported from
the decompiled original (`eqgame.exe`, `eqmain.dll`, `eqgfx_dx8.dll` in
`../TAKP-Client-Underthehood/decomp`) and reads only assets from an EQ install
(`../TAKP-Client/TAKPv2-1`). It never loads or reads `eqgame.exe`: logic and tables the real client
keeps in its binary are ported into the source. Settings go only in `my-client.ini`, never
`eqclient.ini`.

- `eqclient_gui` (Windows only): the real game, with the front end, character select and the
  3D zone.
- `eqclient` (Linux and Windows): a console client for testing against a server.
- `frontend_preview`, `scene_preview`, `sound_preview` (Linux): headless tools that drive the
  real code and write PNGs or WAVs.

`core/` (protocol, files, rules), `audio/` and `render/` are portable. `frontend/` is portable
except for `win32_*.cpp`.

## Build

Linux (console client and the preview tools), in `build/`:

```bash
cmake -S . -B build -G Ninja        # once
ninja -C build
```

Windows, cross-compiled on Linux with llvm-mingw (`../toolchains/llvm-mingw-*`), in `build-win/`:

```bash
tools/cross-check-windows.sh        # once: configures build-win/ with cmake/llvm-mingw.cmake
ninja -C build-win                  # eqclient_gui.exe, eqclient.exe
```

After any source change, build both. The user tests by copying `build-win/eqclient_gui.exe` to
Windows, so tell them it's ready. Visual Studio 2022 also builds the folder directly on Windows.

## Headless testing

There's no GUI on Linux, so check changes with the tools (full flags in `README.md`):

- **Live server:** `build/eqclient -u replaytest -p replaytest --char Replaytest --zone 30` logs in,
  enters the zone and camps out. In the zone, `--say`, `--cast`, `--move`, `--click-door`,
  `--click-link` and `--zone-to` exercise features. Reuse the `replaytest` account rather than
  making new ones. A killed run leaves the character linkdead for about 2 minutes (error 1018).
- **Record / replay:** `--record DIR` writes the zone session to a `.eqr` packet log.
  `build/frontend_preview ../TAKP-Client/TAKPv2-1 OUTDIR --replay FILE.eqr [SECONDS]` plays it back
  without a server through the real front end. It writes PNG snapshots and prints what the windows
  hold, which is how in-zone UI work is checked. Text is drawn as boxes because there's no GDI.
- **Front end against a server:** `build/frontend_preview EQDIR OUTDIR HOST:PORT USER PASS
  [charselect|create]`.
- **3D:** `build/scene_preview EQDIR OUT.png ...` renders character select, a free camera, or a
  character in a zone (spawns, doors, weather, spell effects, jumps, debug draws). Look at the PNG.
- **Sound:** `build/sound_preview EQDIR OUT.wav --zone NAME --at X Y Z ...` mixes and prints what
  plays.

Pipe long runs through `stdbuf -oL` so output isn't lost on a timeout.

## Docs

- `README.md` is the reference on how the original client works, as recovered from the decompile:
  game logic, formulas, file formats, packets, eqgame function names and addresses. Read the
  relevant section before porting something. Keep it current with the `update-readme` skill.
- `TODO.md` is what the client doesn't do yet. Keep it current with the `update-todo` skill.
