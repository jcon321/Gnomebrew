# my-client

A from-scratch EQMac (TAKP-era) client. Two programs share one core:

- **`eqclient_gui`** (Windows): the original front end, meaning the start screen, the login screen and
  server select, drawn from your EverQuest install's own art. After PLAY it authenticates to
  world and shows character select, built from the install's SIDL UI files
  (`uifiles/default/EQUI_CharacterSelect.xml`), over the 3D character select scene: the
  `load2` zone with the selected character standing in their class's room while the camera
  circles them. Empty slots open character creation. Enter World connects to the zone and
  draws it in 3D: you run, walk, turn and jump through it (arrows or WASD, Space, Ctrl+R), with
  everyone else around you and the time of day lighting it, under a chase camera (F9: first
  person). Over the view are eqgame's own windows (player, target, chat and the rest, below) and
  the debug menu (F11): a stat card in sections that fold (your details, frame rate, movement and
  the floor under you, where the zone last heard you are, camera, network, the latest packets, the
  zone header and light, the sound, render toggles for wireframe / no fog / collision walls / a floor
  marker, a target clicked in the world or the list, and the nearest spawns) and a 2D radar. Music plays
  from character select on (eqtheme.mp3, then each zone's own), and the zones' ambient sounds (waterfalls,
  torches, wildlife) around you, as the real client plays them.
- **`eqclient`** (console, Linux and Windows): logs in, authenticates to world, lists
  characters, can create a test character (`--create`), and with `--char` and `--zone` enters
  the zone, lists nearby spawns, can `--say` something, then camps out. Used for testing
  against a server.

## Layout

```
core/       portable: RDP session, login/world protocol, packet logs (record/replay), DES, inflate, PFS (.pak/.s3d), BMP, DDS, TGA, XML, WLD, eqtext.txt,
            character creation rules
console/    eqclient
audio/      portable sound: the mix (WAV, MP3, positional voices), XMI music and its synths, the client's sound manager
render/     portable 3D: renderer interface, software rasterizer, textures, zone scenes, sky, skeletons, race models,
            particle clouds and the spell particle system
frontend/   the front end: canvas, widgets, screens, SIDL UI engine, character select scene, character creation (portable)
            + win32_main.cpp / win32_text.cpp / win32_d3d11.cpp / win32_audio.cpp (Windows host, Direct3D 11
            renderer, waveOut and MIDI out)
tools/      frontend_preview, scene_preview and sound_preview (Linux), cross-check-windows.sh
third_party/rdplib/  vendored rdplib (MIT, by solar), unmodified
third_party/minimp3/  vendored minimp3.h (CC0, lieff/minimp3 @ea99364), unmodified: the MP3 music
third_party/tinysoundfont/  vendored tsf.h (MIT, schellingb/TinySoundFont @853a0a1), unmodified: the SoundFont synth
cmake/      llvm-mingw.cmake (Linux -> Windows cross toolchain)
```

The core and the front-end logic are portable, so both compile and run on Linux too. Only the
window, input, GDI text, Direct3D and sound output glue is Windows-specific.

## Building

### Windows (Visual Studio 2022)

Open the folder in Visual Studio (File > Open > Folder) and build `eqclient_gui.exe`, or:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

No dependencies beyond the Windows SDK.

### Linux (console client and preview tool)

```bash
sudo apt install -y build-essential cmake ninja-build
cmake -S . -B build -G Ninja
cmake --build build
```

### Checking the Windows build from Linux

`tools/cross-check-windows.sh` compiles and links the Windows targets with
[llvm-mingw](https://github.com/mstorsjo/llvm-mingw) into `build-win/`. It finds the toolchain
in `../toolchains/llvm-mingw-*-ucrt-*`, or you can set `LLVM_MINGW_ROOT`. The script is there
to catch errors before a Visual Studio build. Its `.exe` files only depend on system DLLs, so
they also run as-is on Windows 10+.

## Running the GUI

All settings live in `my-client.ini` next to `eqclient_gui.exe`. The EQ folder is only read for
assets; its `eqclient.ini` is never used. The first run writes the file with a comment on every
setting (`frontend/settings_file.cpp` has them all, with their defaults). Later runs add any
missing keys the same way, and keep the rest of the file, your own `;` comments too. Every
setting is listed except the optional `[TextColors]`. Without the comments, it looks like this:

```ini
[Paths]
EQDir=C:\TAKP

[Login]
Server=192.168.0.18:6000
User=

[Display]
Width=1920
Height=1080
Renderer=d3d11
VSync=1

[Sound]
Sound=1
MusicVolume=10
SoundVolume=10
Music=midi
SoundFont=synthus2.sf2

[Defaults]
LastCharSel=0
ShowNamesLevel=4
EnableExtendedNameplateDistance=1
VeliousTextures=1
CharSelectAnimWindow=1
CharSelectEmotes=0
AttackOnAssist=0
LootNoDrop=0
FastDrop=0

[Debug]
RecordPackets=0
MenuOpen=1
Folded=ServerSync,Camera,Packets,Zone
Wireframe=0
NoFog=0
CollisionWalls=0
FloorMarker=0
NoOcclusionCulling=0
NoClipPlane=0
NoWeatherEffects=0
Vision=-1
AllSound=1
Music=1
ZoneAmbience=1
GameSounds=1
WeatherSounds=1
```

- `EQDir` is the EverQuest folder. It needs `eqfeart*.pak` (the front-end art), `eqtext.txt`
  (the front-end strings), `uifiles/default` (the SIDL UI for character select),
  `eqstr_en.txt` (the server message strings), and for the 3D character select scene
  `load2.s3d`, `load2_obj.s3d`, `sky.s3d` and `global_chr.s3d` / `global4_chr.s3d` /
  `global7_chr.s3d`, and in the zone `<zone>.s3d` and `<zone>_obj.s3d`. Only assets are read from it; the client never loads or reads `eqgame.exe`.
  Nothing from the install is copied into this repo. If `EQDir` isn't
  set, the GUI tries the current folder, then the exe's folder.
- `Server` is the login server. It isn't read from `eqhost.txt`, so you can't hit the live TAKP
  server by accident.
- `Width` and `Height` are the game resolution, which is the window's size (default 1920x1080).
- `Renderer` is `d3d11` (the GPU draws the 3D views and presents the window) or `software` (the
  CPU rasterizer, with GDI presenting, capped near 60 fps). If Direct3D 11 or `d3dcompiler_47.dll`
  isn't available, the GUI uses the software renderer and says why in `my-client.log`.
  `--renderer software` overrides it for one run.
- `VSync=1` waits for the monitor's refresh with Direct3D; `0` runs as fast as it can (to measure).
  Character select is drawn at this size, like eqgame: the SIDL windows at their real pixel size, the
  3D scene filling the rest. A window that doesn't fit the screen's work area is shrunk to fit.
- eqmain's screens (start, login, server select) are 640x480 art. They're
  drawn at their native size in the middle of the window, black around them, not scaled.
- `Sound=0` turns all sound off (the real client's `Sound=FALSE`). `MusicVolume` and `SoundVolume` are
  the options' music and sound levels, 0 to 20 (10 by default, half volume, as the client starts).
  `Music` is where the XMI music (the zones before Luclin) goes: `midi`, Windows' MIDI synth (the
  Microsoft GS Wavetable Synth), which is where the real client's went (Miles' XMIDI driver on the MIDI
  mapper); or `sf2`, our own mix through `SoundFont`, one of the General MIDI banks EverQuest ships
  (`synthus2.sf2`, `synthusr.sf2`; in the EQ folder unless it's a full path). Without a MIDI out, `midi`
  falls back to the SoundFont. The MP3 music and the sounds always play through our mix (waveOut).
- `LastCharSel` is the character select slot to start on. Like the real client's `[Defaults]
  LastCharSel`, it's saved as you select characters (written when the GUI exits).
- `ShowNamesLevel` is `/shownames`: 0 (off: no names over players or corpses), 1 first names,
  2 with surnames, 3 with guilds, 4 everything (AA titles too), and Zeal's 5 (title and first
  name), 6 (title, first and last) and 7 (first name and guild). `/shownames` changes it, and it's
  kept when the GUI exits.
- `EnableExtendedNameplateDistance=1` shows names as far out as the zone is drawn, as TAKP's
  eqgame.dll and Zeal do; `0` keeps eqgame's own 70 units.
- `VeliousTextures=1` dresses the Velious armor (materials 17..23) in its own textures from
  `global17_amr.s3d`..`global23_amr.s3d`, one switch for eqclient.ini's `LoadArmor17`..`LoadArmor23`;
  `0` shows it as the leather, chain or plate it stands in for, as eqgame does without them.
- `CharSelectAnimWindow=1` shows our own Animations window at character select (not eqgame's):
  every animation the model has, by name, played by double clicking one. `CharSelectEmotes` picks
  what the character plays now and then: `0` eqgame's own picks for its class (combat moves,
  casting, the fidget), `1` half of them a social's instead (`S01`..`S29`, those its model has).
- `AttackOnAssist` is `/assist on|off`, kept as it changes.
- `LootNoDrop` is `/lootnodrop`: whether looting a NO DROP item asks first, `0` always, `1` only
  for what you can't use, `2` never (eqgame's `DAT_00799674`, kept in its `eqOptions2.opt`).
  `FastDrop` is `/fastdrop`: `1` drops an item clicked onto the ground at once, `0` asks first,
  `-1` never drops it (`DAT_0079966c`). Both are kept as they change.

- `RecordPackets=1` writes each zone session's packets to `replays\<char>-<time>.eqr` next to the exe.
  `--replay FILE` plays one back: the GUI goes straight into that zone with no server, the recorded
  packets arriving when they did (spawns, chat, doors, weather...). You can run around; what you send
  goes nowhere. When the log runs out you stay in the zone. Camping logs out when its timer runs, and
  zone lines are turned down. The console client and `frontend_preview` play the same files.
- The rest of `[Debug]` is the debug menu's (F11): whether it's open, the stat card's folded
  sections, its RENDER toggles and vision, and its SOUND switches. They're saved as you change
  them in the menu.
- `[TextColors]` is optional and isn't written for you: the chat's text colors, keyed as eqgame's
  (`User_<n>_Red`, `User_<n>_Green`, `User_<n>_Blue`, n 1..72; all three, 0..255, or the built-in
  color). For example `User_1_Red=255` with Green and Blue makes says red.

In the zone, the game's own SIDL windows come up as eqgame's do on a first login (player, target,
group, buffs, spell gems, hotbuttons, chat and the selector, whose buttons open and close the
others). You drag them (by the title bar, or anywhere on one that isn't sizable), resize sizable
ones (`Style_Sizable`, such as chat and target) from their border's pieces as eqgame does, the
corners both ways, with eqgame's resize cursors over them, and close and minimize them from their title bars.
Which sides and corners size a window is `CXWnd`'s 0x78, set in code rather than the SIDL: all
(0xff) by default; `CMerchantWnd`, `CLootWnd` and `COptionsWnd` set 0x11, the top and bottom only,
and `CSocialEditWnd` 0x44, the sides (bits: 1 top, 2 top right, 8 bottom right, 0x10 bottom, 0x20
bottom left, 0x40 left and right, 0x80 top left; `CXWndManager::HandleLeftMouseDown`). A border
piece that doesn't size the window moves it. They fade when the
mouse has been off them a while. Their layout is kept in eqgame's own format, in
`UI_<character>_<server>.ini` next to the exe (the server part is the login server's address cut
to 7 characters, as eqgame names it).

Hot buttons work as eqgame's do: hold the left button on any button of the Actions window, on any
of its pages, for 3/4 of a second and a copy of it follows the cursor; click a hot button to put it
there (whatever was there goes onto the cursor instead). Hold a hot button to take it off onto the
cursor. Clicking the world or pressing Escape drops what's on the cursor. Clicking a hot button
does what its button does (so far, the main page's Camp and Run/Walk). They're kept in eqgame's
format in `<character>_<server>.ini` next to the exe, under `[HotButtons]`
(`Page<p>Button<b>=<type letter><number>`).

Items move as they do in eqgame (`core/inventory.*`, mirroring its `CInvSlot`, `SwapCombine` and
`AutoInventory`): click an item in the inventory window to pick it up, and click a slot to put it
there, taking whatever was in it onto the cursor (or topping up the same stack). Worn slots only
take what can go there and what your class, race and deity can use. A stack shows its count; a
click on one opens the quantity window to say how many to take (Shift takes it all, Ctrl one).
Clicking the character picture with an item on the cursor puts it away: onto the same stack,
into an empty worn slot it fits, then a general slot, then a bag. Each move goes to the zone as
eqgame's `OP_MoveItem`.

- The player window's gauges show HP (with your name), mana (not for classes without any) and
  stamina; the experience and AA gauges (EQTypes 4 and 5) are the zone's `OP_ExpUpdate` and
  `OP_AAExpUpdate` values, each 0..330 of the way through the level or AA point, times 3.03031
  (eqgame reads them from `PCData+0x94` and `+0x1528`; no experience table is needed, and the
  profile's total experience is replaced by `OP_ExpUpdate`'s as we enter the zone); the pet gauge along the bottom of the HP bar, only while you have a pet. The target
  window shows the target's name, HP bar and percent. What the windows do for themselves each frame
  in eqgame is in `frontend/game_windows.cpp`: the group window's Invite and Disband buttons, the
  buff window's buttons in columns from the top right (empty slots draw nothing), and the hot
  buttons. Click someone to target them, or `/target name`; Escape clears the target (and, as in
  eqgame, closes the help, options or inventory window if one's open). Gauges are drawn the way
  `CGaugeWnd::Draw` does it, bars sliding to a new value at a full bar a second.
- The chat window keeps the last 400 lines in eqgame's colors (from `[TextColors]` in
  `my-client.ini`, or the built-in ones), with a scrollbar and the wheel. Enter or `/` opens its
  input line, Up and Down bring back the last 40 lines sent. Commands are looked up in eqgame's own
  table, in its order, so abbreviations work as they do there (`/g` is /gsay, `/s` is /split).
  So far: /say (or no slash, or `'`), /tell (/msg, /send), /reply, /gsay, /guildsay, /shout,
  /auction, /ooc, /emote (`:`, /me), /hail, /target, /assist, /consider, /location, /time,
  /played, /motd, /afk, /anonymous, /shownames, /sit, /stand, /book, /cast, /camp, /clearchat,
  /random, /lootnodrop, /fastdrop and the socials (/smile, /wave and the other 86).
  The rest say they aren't supported yet; a GM's commands act as if they weren't there for
  anyone else.
- `/consider` sends `OP_Consider` (`OP_ConsiderCorpse` on a corpse) and prints the answer as
  `PrintConsiderAssessment` does: the faction, the assessment by our level in the con color, and
  the PvP and deity-team rules. `/time` is `CEverQuest_WhatTimeIsIt`: Norrath's date carried on
  from `OP_TimeOfDay` (nothing in the Plane of Time or on Luclin), then Earth's. `/afk` and
  `/anonymous` are SpawnAppearance 24 and 21. `/assist` sends `OP_Assist`, and the zone's answer
  becomes the target.
- `/random` (@004f62aa) takes `high` or `low high` either way round, each under 7 characters, not
  below 0 (eqstr 13061) nor over 32000 (13062), and sends `OP_RandomReq {u32 low, u32 high}`. The
  zone rolls (0 to 100 for nothing typed) and sends everyone near `OP_RandomReply {low, high,
  result, name[64]}`, printed as eqstr 1086 and 1087 in color `0x11f` (eqgame leaves out the rolls
  of anyone it ignores).
- `/lootnodrop` (@004f5c8b) matches `never`, `sometimes`, `always` by their first four letters (or
  2, 1, 0) and says "New Setting:"; with nothing typed it prints its usage and the current setting.
  `/fastdrop` (@004f9222) takes `on`, `off` or `never` as whole words, case aside.
- **Socials** (`CEverQuest_send_social` @00532ddd): 88 of them, numbered in its switch from /smile
  (0) to /flex (87); /gesture, /flipoff, /bird and /finger are /rude's handler. `/smile [name]
  [text]`: the first word picks who it's at (`EQPlayer_get_player_partial_name`; nothing, or eqstr
  12295, the target), not ourselves, and not past 60 ("I don't see anyone by that name around
  here...", 12296 or 12321). Each social has an eqstr pair for what's sent (12500 on: with no target,
  then with one) and another 200 on for our own echo, with its own arguments: our name (sent as
  "", since the zone puts our name in front), the target's name, and our or the target's
  he/him/his (12209..12211, 12297..12301 by gender; "You", "you", "your" in the echo). Anything
  typed after the name replaces the text: sent as `" text."`, echoed as "You emote 'text'". A
  full stop goes after a final letter or digit, the text goes out as `OP_Emote`, and the echo is
  printed in the emote color (`0x107`). A social with an animation (`PlayAnimationTrack` id, e.g.
  77 for /smile) plays it on us and sends `OP_Animation {u16 us, pad, i32 id, float 1}`, but only
  standing still. A social with only a lone text (/fidget) does nothing with a target; /nudge has
  only a targeted one. The table (`Socials` in `frontend/chat_commands.cpp`) was read out of the
  exe's two switches: the sent one Ghidra recovers; the echo's jump table (0x535d63) it doesn't.
  `DoPercentConvert` (%t and the rest) isn't applied.
- The Actions window's tabs (Main, Abilities, Combat, Socials) switch pages, drawn as `CTabWnd`
  draws them, and its Camp, Run and Walk buttons work; Run or Walk, Sit or Stand and Invite or
  Follow show as eqgame picks them. The socials page and the hot button window page through ten
  sets with their arrows, round from the last to the first.
- Socials work as eqgame's do: the first page starts with its twelve (Afk, Anon, ... Wave), a
  right click on a social (on the Actions window or a hot button) opens the Edit Social window
  (a name, its color and five lines), and clicking one does its lines a step apart, with
  `/pause N[, command]` waiting about N tenths of a second. They're kept in `[Socials]` in
  `<char>_<server>.ini`, eqgame's format.
- Names over heads are eqgame's name sprites (`CDisplay::SetNameSpriteState`): a player's first
  name and surname, LFG, AFK and LD, and `<guild>` on a second line (the world's guild list, asked
  for as you enter the world); an NPC's or corpse's name alone. Their colors are eqgame's: blue
  players, teal NPCs and corpses, green GMs, red PvP, purple roleplayers. They're a world unit tall
  (smaller further off, the size they'd be at 20 units when closer; a 20 pixel bold Arial stretched, as
  eqgfx draws them), shown as far out as the zone is drawn (eqgame's own cutoff is 70 units; see
  `EnableExtendedNameplateDistance`), and
  hidden behind walls. Your target's name flashes from white back to its color, as eqgame shows
  the target (no ring: the client of this era had none). Name sprites are lit as `renderNameTag`
  lights them: the world's ambient plus 0.4, each channel at most full. F1, or clicking our own
  model in the chase view, targets ourselves.
- **Tooltips** (`WindowManager::DrawTooltip`, eqgame's `DrawWindows` and `DrawTooltipAtPoint`):
  once the mouse has rested 500 ms, the control under it shows its tooltip, white on a
  `0xc8000080` box 16 pixels right of the mouse (left of it at the screen's edge). `DrawWindows`
  uses the system font (2), but the TAKP client shows an item's or a gem's in font 3. Controls
  start with their `TooltipReference`; an inventory slot shows its item's name or the empty worn
  slot's (eqstr 558-576), a gem `1) Spell`. Alt held shows every gem's at once
  (`DrawSpellButtonTooltips`).
- **In-game text** goes through eqgfx's glyph cache, not whole GDI strings: each glyph is
  `ExtTextOutW`'d alone white on black, cut to its `GetTextExtentPoint32W` width
  (`createGlyphCacheEntry`), its alpha the blue channel's top four bits (`updateGlyphTexture`),
  and a string is those glyphs side by side. So there are no ClearType fringes and no overhang
  into the next letter. SIDL fonts 0..4 ask weight 100 and 5..6 weight 700. eqmain's front-end
  screens stay whole-string GDI.
- **The compass** (`frontend/compass.*`, eqgame's `CCompassWnd`): two `A_CompassStrip`s (180
  pixels for the 512 heading units) slide under `A_CompassOverlay`
  (`UpdateCompassAndBuffWindows` @0041564d): CompassStrip1 at
  `ftol(((heading + needle) % 512) * 0.3515625 - 145) + 47`, less a multiple of 180 to bring it
  into (-180, 0] when it's past 0, and CompassStrip2 180 to its right, both cut to the window. With
  Sense Heading (skill 40) under 200 the needle wanders: it swings toward a variation
  (`CalculateCompassNeedleVariation`: `rand() % span - span / 2`, span `(200 - skill) * 512 / 200`,
  at least `51 - skill / 5` either way) at 80 heading units a second, 160 when it's more than 50
  off, and on reaching it from below picks another; it's kept within -256..256. Using the skill
  (`SenseHeading`) points it true for 5 s. eqgame opens it only with `TOGGLE_COMPASSWIN`, which has
  no default key; until there's a keymap we put it up on every zone-in.
- The inventory, skills, item display, spellbook, casting, buff, quantity and compass windows are
  filled in (below, under Spells and Items and stats). Others are still empty frames.

Command-line flags override the ini: `--eqdir`, `--login`, `--size WxH`, `--replay FILE`. The GUI saves the last
username as `[Login] User=` and the selected character as `[Defaults] LastCharSel=`, and writes a log to `my-client.log` next to the exe.

Like the real client on an EQMacEmu server with `auto_create_accounts`, the first login of a new
name creates the account but is still rejected. Click CONNECT again.

## Custom changes

Everything else in this README follows the original client. These are deliberate changes and
additions of ours, or ones taken from TAKP's eqgame.dll or Zeal rather than stock eqgame:

- **Settings in `my-client.ini`:** nothing is read from or written to `eqclient.ini`, and the login
  server is `[Login] Server`, not `eqhost.txt`, so the live TAKP server can't be reached by accident.
- **Renderer:** Direct3D 11 by default, or our software rasterizer (`Renderer=d3d11|software`, with a
  fallback when D3D11 isn't there), `VSync` on or off, and any `Width`x`Height`. The real client
  draws through eqgfx_dx8 (Direct3D 8).
- **Debug menu** (F11): the stat card with its folding sections, the 2D radar, the RENDER toggles
  (wireframe, no fog, collision walls, floor marker, no occlusion culling, no clip plane, no
  weather effects, Luclin spell effects, VSync, vision) and the SOUND switches, kept in `[Debug]`.
- **Packet record and replay:** `RecordPackets=1` writes each zone session to an `.eqr`, and
  `--replay` plays one back with no server (GUI, `eqclient` and `frontend_preview`).
- **Mouse wheel camera zoom:** the wheel zooms from first person out to 200 units and back.
- **Emotes in character select's random animations** (`CharSelectEmotes=1`): eqgame's
  `DoClassRandomAnimation` picks only from its class's combat moves, casting and the fidget. With
  this on, half its picks are a social instead (`S01`..`S29`, of those the model has).
- **Character select Animations window** (`CharSelectAnimWindow`): ours, not eqgame's. It plays any
  animation the model has, by name, and switches between Combat only and All + emotes.
- **Character select field of view:** widened by the aspect ratio (88 degrees at 16:9) as Zeal's
  `SetCameraLens` hook does, rather than eqgfx's 63.3.
- **Extended nameplate distance** (`EnableExtendedNameplateDistance=1`): names show as far as the zone
  is drawn, as TAKP's eqgame.dll and Zeal do, not eqgame's 70 units.
- **`/shownames` 5, 6 and 7:** Zeal's levels (title and first name; title, first and last; first
  name and guild) on top of eqgame's 0..4.
- **Shift+click linking:** Shift+click on an inventory item while typing puts its link in the chat
  line. eqgame links only from the item display's icon.
- **SoundFont music** (`Music=sf2`): the XMI music through our own synth and a SoundFont the game
  ships, instead of Windows' MIDI synth where the real client's went.
- **One Velious armor switch** (`VeliousTextures`): stands in for all of eqclient.ini's
  `LoadArmor17`..`LoadArmor23` together.

## Console client

```
build/eqclient -u USER -p PASS [options]
  --login HOST[:PORT]   login server (default 127.0.0.1:6000)
  --world NAME|INDEX    world from the server list (default: first)
  --world-host HOST     override the world address from the server list
  --world-port PORT     world port (default 9000)
  --char NAME           enter world with this character and print the zone address
  --create NAME         create a Human Warrior with this name first (for testing)
  --zone SECONDS        with --char: enter the zone, stay this long, then /camp (30 s) and log out
  --say TEXT            with --zone: /say this once in the zone
  --click-door ID       with --zone: use door ID once in, and print the doors that move (OP_MoveDoor)
  --zone-to ID          with --zone: once in, zone to zone ID (OP_ZoneChange; the zone checks it against
                        its zone points), through world to the next zone, then stay and camp there
  --create-class N      with --create: a Human of class N instead (2 cleric, 11 necromancer...)
  --click-link ID       with --zone: click a chat link to item (or say link) ID and print the item sent back
  --pickup DROP|far     with --zone: list the ground items, then OP_ClickObject drop DROP ("far": the
                        farthest that isn't a tradeskill container) with no 20-unit range check, and
                        print the zone's answer and the cursor. Tests the zone's own range check
  --move FROM:TO[:N]    with --zone: move what's in slot FROM to TO as the inventory window does (0 the
                        cursor; N takes N of a stack; "auto" puts the cursor's item away). Repeatable
  --cast GEM[:SPELL]    with --zone: cast gem GEM (1..8; SPELL memorized there first) on ourselves and
                        print the zone's answers and our buffs. Repeatable, 3 s after each cast ends
  --idle SECONDS        without --char, stay at character select this long (default 0)
  --record DIR          with --zone: write each zone session's packets to DIR/<char>-<time>.eqr
  --replay FILE         play a recorded zone session back (no server): stay --zone seconds (default
                        10), print what's around, then camp out
  --no-retry            don't retry once after "Invalid username or password"
  -v                    hex dump every packet
```

## Preview tool (Linux)

```
build/frontend_preview EQDIR OUTDIR [HOST:PORT USER PASSWORD [charselect|create|shop|ground|commands]]
build/frontend_preview EQDIR OUTDIR --replay FILE [SECONDS]
```

This drives the real front-end code with scripted input and writes PNG snapshots. Text is
drawn as outline boxes, since there's no GDI. With a login server it logs in, snapshots server
select and character select, and presses Enter World (add `charselect` after the password to stop
at character select instead, or `shop` to stand by the nearest merchant, open its shop with the
right button, sell the first thing worth anything and buy it back, then camp; or `ground` to list
what's on the ground, drop an item and pick it back up, and open a forge and close it; or
`commands` to type /random, /lootnodrop, /fastdrop and socials (alone and at the nearest spawn),
print the chat, and snapshot the compass as it turns). `create` instead makes an Erudite wizard from the first empty slot,
snapshots every creation screen and the face picker, submits it, and deletes it again. It's for
checking layout and flow without Windows. `--replay` plays a packet log back instead (no server)
and snapshots the zone once it's in, then every 3 seconds for SECONDS (default 10), then drags,
opens, minimizes, resizes and closes windows (the layout goes to OUTDIR), types into the chat window
(a say, commands, /target) and prints what the chat window holds, then stands near the target to
snapshot the names over heads (from two distances and in first person) and prints what a player's
name says at each `/shownames` level. Along the way it opens a bag (a made-up backpack when the
character has none) and moves an item in and out, stands by a banker, right clicks it and
moves an item and coins into the bank, and gives an NPC coins and then an item through the give
window (the replay answers as the zone does for an NPC), Give once and Cancel once.

```
build/scene_preview EQDIR OUT.png --char RACE GENDER CLASS [--face F] [--equip M,M,...] [--velious 0|1] [--camera H] [--frames N] [--size WxH]
build/scene_preview EQDIR OUT.png [--zone NAME] [--at X Y Z] [--heading H] [--pitch P] [--frames N] [--size WxH]
build/scene_preview EQDIR OUT.png --player RACE GENDER --zone NAME --at X Y Z [--heading H] [--face F] [--equip M,M,...]
                    [--orbit H] [--pitch P] [--first-person] [--zoom N] [--walk MS [--turn -1|1]] [--npc ...]
                    [--time HOUR MINUTE] [--time-type T] [--fog START END RRGGBB] [--sky N] [--frames N] [--size WxH]
                    [--debug-draw wireframe,nofog,walls,floor,noculling,noclip] [--door NAME TYPE X Y Z HEADING INCLINE SIZE PARAM]
                    [--door-open MS] [--click X Y] [--jump MS] [--sound OUT.wav] [--weather rain|snow MS]
                    [--vision none|infra|ultra] [--spell-effect STAGE EFFECT CASTER TARGET MS [CAST_MS ANIM]]
                    [--missile SOURCE TARGET KIND OBJECT MODEL MS]
```

`scene_preview` renders the 3D view to a PNG: the character select scene for any race, gender,
class, face and armor, a free camera anywhere in a zone (server coordinates, heading 0..512), or
the in-zone view with a character standing at a server position (`--fog` and `--sky` stand in for
the zone's `OP_NewZone`). In that mode `--walk MS` runs forward first (and `--turn`), `--npc RACE
GENDER SIZE X Y Z HEADING` stands spawns around (each followed by any of `--npc-look TEXTURE HELM`,
`--npc-equip M,M,...` (the nine equipment slots as the server sends them), `--npc-move SPEED TURN`, `--npc-stance S` and `--npc-fly F`), `--spawn-ms MS` runs them that long first
and prints what they do, `--time HOUR MINUTE` sets the time of day, and `--debug-draw` turns on the
debug menu's render toggles (`noculling` too), `--vision` the race's vision, and `--spell-effect`
plays a `SpellsNew.eff` stage on the caster and target spawns. `--missile` lets an `OP_Projectile` go from
one `--npc` at another as the server fills it for a bolt (`28 0 IT68` is its own; `9 SPELL MODEL` a
spell's) and prints where it is every 96 ms. `--door` puts a door there as `OP_SpawnDoor` would (copy a row of the
server's `doors` table), `--door-open MS` opens them all and runs that long before any `--walk`, and
`--click X Y` reports what a left click there would do. `--jump MS` jumps that far into the walk, and
`--sound OUT.wav` mixes what the doors, we and the spawns sound like as it runs (no zone music or
ambience), printing each wave played. `--weather rain|snow MS` starts it raining or snowing and runs
that long first, printing the weather's fog and particles each second (`--debug-draw noweather` draws
none of it). `--frames N` times N renders.

```
build/sound_preview EQDIR OUT.wav [--zone NAME] [--at X Y Z] [--heading H] [--walk X Y Z] [--seconds S]
                    [--music opening|charselect|combat|death|ID]... [--night] [--environment 1|2|3]
                    [--soundfont FILE] [--music-volume V] [--sound-volume V]
                    [--rain N | --snow N] [--weather-off S] [--covered]
build/sound_preview EQDIR --emitters ZONE
build/sound_preview EQDIR --xmi FILE INDEX OUT.mid
```

`sound_preview` runs the client's sound without a window or a sound card and writes the mix to a WAV:
standing at a server position in a zone (or walking from there to `--walk`'s over the time), with the
music selections given turned on, the XMI music through a SoundFont (default `synthus2.sf2`). Each second
it prints the music track and the voices playing, and at the end each second's loudness. `--night` and
`--environment` (1 outside, 2 a high ceiling, 3 a low one) set what the emitters see. `--rain N` or
`--snow N` is an `OP_Weather` at the start (an outdoor zone), `--weather-off S` stops it at S seconds, and
`--covered` puts a roof over us (no rain or wind loop); each second then prints the weather's state and
what it sounds. `--emitters` prints
a zone's `.emt`, and `--xmi` writes one of an `.xmi`'s sequences out as a standard MIDI file.

## How the front end was recovered

Layouts, art names, fonts and colors come from the `eqmain.dll` decompile (TAKP-Client-Underthehood):

| Screen | eqmain function | Art |
|--------|-----------------|-----|
| Start | `displayLuclinOptionsScreen` @10021a50 | `eqlstart.bmp`, `eqlstart_overlay.bmp`, five `button*.bmp` at x=400 |
| Login | `displayLoginScreen` @100226b0 | `eqklogin.bmp`, fields at (295,260) and (295,365), buttons at x=10 |
| Server select | `displayServerSelectionScreen` @100275d0 | `selserver.bmp`, lists at (130,100) and (410,100), `eqbutton*.bmp` PLAY at (421,389) |
| Dialogs | `showConfirmationDialog` @10002680 | `dialog.bmp` / `dialogmedium.bmp`, screen darkened by half |

- **State machine:** `new_dll_main` @10025300. States are 7 = start, 0 = login, 0x15 = server
  select.
- **Art:** `eqfeart1.pak` to `eqfeart17.pak`, searched in load order. Each is a PFS archive of
  24-bit BMPs, with magenta (`#FF00FF`) as the transparent color key.
- **Buttons:** the `createStandardButton` styles at 0x100550e8 are `sbutton`, `mbutton`,
  `button`, `eqbutton` and `lsb`. Each has three images: normal, hover, pressed.
- **Strings:** `eqtext.txt`, loaded by `loadLocalizedTextStrings` @10032ff0. Every quoted
  string goes into a table in file order (at 0x1007ff60 in eqmain), and the UI reads it by
  index. This code looks strings up by their `KEY` names instead.
- **Fonts:** GDI Arial, from `CreateFontA` in `initializeGraphicsAndFonts`. Heights are 25
  (buttons and fields), 14 (labels and lists) and 12. Button labels in a group shrink together
  until all fit (`drawTextWithVerticalAdjustment`).

## Character select (SIDL)

eqgame.exe builds its UI from `uifiles/<skin>/EQUI.xml` and the files it includes. `frontend/sidl.cpp`
loads the same files:

- `Ui2DAnimation`: frames cut from TGA textures, drawn stretched into a control's rectangle.
- `ButtonDrawTemplate`: Normal, Pressed, Flyby, Disabled and PressedFlyby animations.
- `WindowDrawTemplate`: a background texture tiled over the client area, plus 12 border pieces
  with corners at natural size and edges stretched between them.
- `Screen` (nested too), `Button`, `Label`, `StaticAnimation`, `Editbox` and `STMLbox` pieces. A
  child's `Location` is relative to its parent's client area (`RelativePosition` false: to the
  screen). `AutoStretch` pieces sit at anchor offsets from the parent's edges, so they follow it
  when it's moved or resized. Static animations draw under the other pieces; otherwise pieces
  stack in the order they're listed.
- Title bars (`Style_Titlebar`) use the template's `Titlebar` pieces, between the side borders.
- STML is drawn as plain text: `<BR>` breaks the line and other tags are dropped.
- SIDL `Font` numbers index the seven fonts `CDisplay::InitNewUI` makes, 0 to 6: Arial 10, 12, 14,
  15, 16, 20 and 24 (the heights Zeal's font hook sees for each), with 3 (15px) as the schema
  default.
- Every piece is clipped to its parent's client area, as `CXWnd::GetClientClipRect` does, and can't
  be clicked outside it: the frame and title bar to the piece's own rect, everything else to its
  client area.

Character select uses `CharacterSelectWindow`. Its buttons do what
`CEverQuest::DoCharacterSelection` (@0053b9cf) does: Enter World sends `0x8001` with the name,
Delete sends `0x405a` with the name, and Quit sends `0x4023`.

## 3D character select

`CEverQuest::DoCharacterSelection` renders a real zone behind the window. `frontend/charselect_view.cpp`
does the same, from these decompiled functions:

- **Zone:** 185 (`load2`) if `load2.s3d` exists, else 184 (`load`)
  (`CCharacterSelect::AlternateBackgroundAvailable` @0040f068).
- **Placement:** in `load2` each class has its own room (`CCharacterSelect_SetLocationByClass`
  @0040fa3a: warrior at /loc 600, 1475, -180, and so on). The character is dropped onto the floor
  below (`EQPlayer::PositionOnFloor`), facing north (heading 0).
- **Camera:** view 5 (`CEverQuest_SetupCharacterSelectCamera` @0053c7cd, `CDisplay_SetCCreateCamera`
  @0049fa4e, `CDisplay::DoCamAi`): 6 units out (7 for trolls, ogres and Iksar), at two thirds of
  eye height, looking level at the character, with a 90° view angle and the far plane at 5000. With
  Rotate checked (the default) the heading starts at 456 and turns 3 units every 50 ms.
- **Model:** the classic race models (`global_chr.s3d`, plus `global4_chr` for Iksar and `global7_chr`
  for Vah Shir) play their `P01` standing idle; races without one borrow another model's (the human
  female plays the wood elf female's). Face and armor materials from the character list swap the
  body textures (`HUMCH0201.BMP` is the chest in material 2, chain), and tints multiply them. A robe
  (chest 10 to 16, `CDisplay_SwapMaterial`) swaps in the robed body mesh (`HUM01_DMSPRITEDEF`) and its
  cloth (`CLK04` to `CLK10`). An NPC of a player race wears its body texture from the chest to the feet
  (`EQPlayer::EQPlayer`), which is how a priest in texture 11 gets his robe. The primary and secondary
  are IT model numbers (`gequip*.s3d`): the primary hangs from the right hand's `R_POINT` bone, and the
  secondary from `L_POINT`, or from `SHIELD_POINT` on the forearm when it's a shield
  (`CDisplay_UpdateItemSlot2`, `IsShieldModel`). Models are scaled to their race's default height / 5 (`EQPlayer::SetHeights`), standing 3.125 x that over the floor.
- **Fog:** `DoCharacterSelection` sets the load zone's fog to 150..250 in grey 200, pushed out by
  `SetLocationByClass`'s clip plane (1.2; a druid's room 2.2), and darker in the rooms set at 23:00.
  The yon is the fog's end, so the other class rooms don't show through the sky.
- **Animations:** every 5 to 15 seconds the character plays one of `DoClassRandomAnimation`'s picks
  for its class, once (mixing in emotes is ours, see Custom changes). The old models' social animations (`S05`..`S28`) are in `global3_chr.s3d`,
  not `global_chr`, so character select loads it too.
- **Name:** `CDisplay::SetNameSpriteState` with the character select window up is
  `"%s [%d %s]\n%s"` (name, level, `GetClassDesc`, `GetFullZoneName`), tinted yellow.
  `GetFullZoneName` is the eqstr string, else `EQWorldData`'s long name (its `AddZone` calls, the
  first of an id kept; `core/zone_names.cpp`), else "Unknown Zone".
- **Field of view:** eqgfx reads `SetViewAngle(90)` in 512ths of a turn, so 63.3 degrees across.
  As a custom change, Zeal's `SetCameraLens` hook widens it in character select by the aspect ratio (88 degrees at
  16:9); character creation keeps that.

The renderer (`render/`) is a portable software rasterizer drawing into the same surface as the
front end, standing in for `eqgfx_dx8.dll`: perspective-correct textures with mipmaps and bilinear
filtering where they're magnified, masked and blended materials, vertex lighting, and strips
rasterized on worker threads. The sky is a background layer: it's drawn after the solid geometry,
only on pixels the zone left empty, and before translucent surfaces, so a hidden sky costs nothing.
At 1920x1080 a character select frame takes about 24-31 ms on a 4-core VM.

On Windows the views draw through Direct3D 11 instead (`frontend/win32_d3d11.cpp`, behind the same
`render::Renderer` interface). It keeps the software rasterizer's rules: the same lit vertex colors
and fog, masked texels under half alpha dropped, the sky only where nothing was drawn, and
translucent triangles sorted back to front. Depth is reversed (1 near, 0 far) for precision across a
zone. Textures are uploaded the first time they're drawn and freed with them, and so are a zone's
vertices (all its meshes in one buffer); each frame only sends the visible meshes' indices, grouped
by material into a draw call each. The vertex light is applied in the shader, so models and the sky
send their vertices once per frame rather than once per material, with tints as each batch's light. The 2D front end is
still drawn on the CPU into the canvas, which is blended over the 3D view each frame. The canvas's
top byte is how much of the view shows through: a GPU view sets its block to `0xff000000`, drawing
writes 0, and alpha blends and darkening scale it, so the chat backdrop and the SIDL windows
look the same as before. GDI doesn't know about that byte, so text that lands on the view is drawn
white on black into a scratch DIB and blended in by its coverage. Name tags behind walls read the
depth buffer back a few frames late (the newest copy the GPU has finished, without waiting).

Things learned about the files:

- WLD x/y are the server's y/x (z is up; +x is north), checked against the EQEmu map `load2.map`.
- Bone tracks are 8 int16s a frame, as EQGfx's `processTransformationNode` reads them: a quaternion
  `{w, x, y, z}` / 16384, a shift / 256, and the bone's scale / 256 (unsigned), which multiplies down to
  its children's shifts, scales and meshes. It isn't the shift's denominator: the halfling, dwarf and
  gnome males' pelvis is 1.6 (410), without which they're two thirds the size of their females.
- BMP textures are used bottom-up (v = 0 is the bottom row); the zones' DDS textures (DXT1/3/5) are
  stored that way already.
- Zone lighting is the `lights.wld` point lights (linear falloff) over a flat ambient, plus the meshes'
  small additive vertex colors. It's an approximation of the real client's lighting.
- TAKP's repacked `sky.s3d` names its filename table with CRC `0xFFFFFFFF` instead of `0x61580AC9`.

## Character creation

Clicking an empty slot ("Create New Character") opens `CharacterCreation` from
`EQUI_CharacterCreate.xml` over the character select scene, as `CEverQuest::DoNewCharacterCreation`
(@0053ae99) does. `frontend/charcreate.cpp` ports `CCharacterCreation`:

- Four screens behind Back and Next: race, class, abilities, name, face and gender; deity; starting
  city; and the welcome text. `ActivateScreen` lays them out for the screen size. The first screen
  puts race and class at the left edge, the name panel at the top center and the abilities at the
  right edge; the others are 640x440 and centered.
- Opening the screen picks a random race, gender, face and class, like `Activate`. A race picks a new
  random face and class; a class resets the stats, deity and city to the first allowed ones (humans
  and half elves get a random city among the allowed ones, as the real client does).
- Abilities start at the race's base plus the class's bonus (green), which comes out of 50 points;
  each can go 25 over that, 150 at most. The stat's name button puts everything left into it. Next
  insists the points are spent and the name is 4 to 15 letters.
- Get Name is the client's own `CreateName`: syllables by race plus a feminine ending for women. The
  name is sent as a capital followed by lower case, the only form world approves.
- Set Face opens `FacePickWindow` with the camera close on the face. Face cycles 0..7; the old models
  have no separate hair, eyes or beard, so those rows don't change anything.
- Next on the last screen sends `OP_ApproveName` and, once it's approved, `OP_CharacterCreate` with
  what `EQ_PC::InitPlayerStart` fills in: the start zone, the spawn y/x/z and heading, and all five
  bind points (`CreationRules::Start`). World copies them as sent, so a client that sends only the
  zone starts new characters at 0, 0, 0, bound there. Character select then opens on the new
  character.

The rules are built into `core/creation_rules.cpp`: which classes a race can be, race base stats,
class bonus points, allowed deities and cities, and `CreateName`'s syllables. The tables were taken
once from TAKP's eqgame.exe and checked to give the same answer as the exe's for every race, class,
deity, city and server type. As in the exe, Erudite shadow knights, necromancers and Cazic Thule
need `paineel.s3d` in the EQ folder. `GetCharCreateText`, which picks the welcome text's strings by
race, class, deity and city through about a thousand lines of branches, is a table of string ids
instead. It was generated by compiling the decompiled function and running it for every allowed
combination. Every race, class, deity and start zone combination the client allows matches world's
`char_create_combinations` table (all 317; the server's extra rows are Berserkers, which this client
doesn't know).

Not yet: the real client's messages use `CConfirmationDialog`; these use the front end's dialog.

## In the zone

`frontend/zone_view.cpp` is the world display `CDisplay::StartWorldDisplay` (@004a849e) sets up once
`OP_NewZone` is in, loaded on a background thread while the zone handshake carries on:

- **Zone:** `<zone>.s3d`, the objects `objects.wld` places from `<zone>_obj.s3d`, and the `lights.wld`
  point lights, the same loader as character select.
- **Fog and far plane:** `CEverQuest::HandleWorldMessage` copies `OP_NewZone` into `instEQZoneInfo`. A
  zone without fog gets grey fog from 500 to 600, and the clip range is at least 50.
  `CEverQuest::SetTimedFog` and `CDisplay::SetFog` use `fog_minclip[0]` to `fog_maxclip[0]` in the zone's
  fog color, and the yon (far plane) is the fog's end. Past it you see the sky, or the fog color.
- **Sky:** only when the zone's `sky` isn't 0 and the yon is at least 101, as `RealRender_World` checks
  (`sky.s3d` `LAYER<sky>1` / `LAYER<sky>3`). It dims with the time of day (below).
- **Our character:** dressed from `OP_ZoneEntry` (`ServerZoneEntry_Struct`, 356 bytes, not encrypted):
  race, gender, face, the nine equipment materials and tints, and the size the model is scaled to. The
  server's z is the model's center, `size / 5 * 3.125` over the feet (`Mob::CalcZOffset`). The feet go there
  and then settle onto the floor right below. Headings in the profile and `OP_ZoneEntry` are 0..512;
  spawn packets send them as a byte (0..256).
- **Camera:** a chase camera behind and above the character, looking at their head. It comes in when a
  wall or the floor is in the way (tested over a box around the camera, so surfaces it backs into at a
  shallow angle don't cut into the view), and hides the model once it's inside them. The wheel (ours,
  see Custom changes) zooms from first person out to 200 units, `DoCamAi`'s limit: out of first person into the chase camera, and
  back in past 3 units. Holding the right button turns the character with the mouse and tilts the view
  (the character first turns to where the camera looks); holding the left button in the chase view
  circles the camera around them, and running brings it back behind. Both buttons run forward. While a
  drag holds the mouse the cursor is hidden and kept where the drag started. F9 switches between first
  person and the chase camera; Page Up/Down tilt it.
- **Moving** (`frontend/player_movement.cpp`), ported from `CEverQuest::ProcessControls` (@0053f337) and
  `process_physics` (@0054d964): Up/W runs (or walks: Ctrl+R, `/walk`, `/run`) at the zone entry's
  `runspeed` (0.7) or `walkspeed` (0.46), Down/S backs up at the walk speed, Left/A and Right/D turn,
  speeding up by one a frame to 12 heading units a tick. Holding the right button, left and right strafe
  instead: half the run (or walk) speed to the side on top of the forward push, so running and strafing
  is about 12% faster, or the full speed standing still. Time runs in 50 ms ticks: on the ground the
  velocity is `v * 0.625 + push * 0.8` (`MovementFriction1/2`), in the air there's no push and it keeps
  0.99, and gravity is `OP_NewZone`'s. The real client damps once a frame, so it stops quicker the
  faster it draws; we play like it does at 60 frames a second. That's about 30 units a second running,
  and the walk and run animations play at `1 + velocity / 2` (`ProcessAnimation`). Space jumps
  (`1.65 * JumpHeightModifier`, 1.35, plus a push of 1 along the heading) and sends `OP_Jump`. A
  press on the ground arms a jump 1.5 s after the last one, for half a second: moving forward jumps
  at once (`L03`); standing still crouches first (`ChangeStance` 111, `L08`, sent as
  `OP_SpawnAppearance`), then jumps straight up (`L04`); backing up calls it off. A press must
  never be read on a frame where no time passed. The Windows loop timed frames with
  `GetTickCount64`, whose 16 ms steps did that at vsync rates, so it uses `steady_clock`.
  Sitting, `ProcessControls` takes no movement keys or Space and the right button doesn't turn us.
  Forward stands us up, closing the spellbook, and we move off at once while getting up (`P02`
  backwards) plays out. In first person the view follows the head bone down and up. The
  collision is ours: the feet follow the floor (stepping up to 40% of the character's height) and moves
  slide along walls, tested at knee and chest height.
- **Water** (`render/zone_regions.cpp`): `<zone>.wld`'s BSP tree (0x21) finds the region (0x22) a point
  is in (`t3dGetRegionNumberFromWorldAndXYZ`: back when `n . p < -d`), and the region groups (0x29) say
  what regions are. A group's type is its name, or its user data when the name is under 20 characters
  (classic zones: `Z0002_ZONE` with `WTN__01258...`). `CDisplay::GetEnvironment` reads the first known
  prefix: `DR` dry, `WT` water, `SL` slime, `LA` lava, `VW`, `W2`, `W3` (types 0, 5..10). `MovePlayer`
  samples the feet (+0.25), the body (`ModelHeightOffset`, the server's z) and the eyes when their
  region changes. The body's is `SwimmingWaterType`, which `process_physics` swims in: gravity still
  pulls, but across `v * 0.95 + push * 0.025` and up and down `v * 0.2 + speed * sin(pitch) * 0.8`
  (lava, slime and `VW`: 0.94, 0.02, 0.15), so you swim where you look at about half the push, and
  sink slowly when you stop. `ProcessControls` adds a hundredth of the swimming skill (the profile's
  skills at 2988) to the forward speed and gives none backwards; swimming forward looking up does
  `DoSwimJump` (at most every 1.5 s, where it isn't water ahead: `MovementSpeedZ += 3 x 2.5 x 1.35` and
  a push of 0.5) to climb out. The look pitch is the view's tilt from where each camera starts. In water
  we play `P06` swimming (at `1 + velocity / 2`) or `L09` treading water, and so do other spawns whose
  body is in water. Not yet: breath and drowning, the underwater sound, levitation.
- **Zone lines:** a feet region with `TP` at 3 (`DRNTP...`) is one; `MovePlayer` calls
  `CDisplay_DoTeleport` as the feet step into it. The type holds the zone (5 digits), then y, x, z (6
  each) and the heading (3); zone 255 makes the y an index into `OP_SendZonepoints` (Rivervale's lines
  are 98 and 99). A line into the same zone moves us (999999 and a heading of 513 keep ours), with the
  client's adjustments between Freeport East and the Ocean of Tears and two Qeynos lines. Otherwise
  `CEverQuest::MoveToZone_2`: `OP_ZoneChange` (name, zone, reason 0) and wait for its answer
  (`IsZoneAvailable`); on success "LOADING, PLEASE WAIT...", `OP_DeleteSpawn` with our id, and the zone
  answers `OP_LogoutReply` and drops us. Then world again with the zoning flag at 192 in
  `OP_SendLoginInfo`: it answers `OP_EnterWorld` with the character in the game, we enter as it, and it
  sends the next zone's address. A refusal prints the client's `eqstr` message for it (not experienced
  enough, not ready, expansion...) and moves us to the zone's safe point. The chat carries over.
- **Doors and switches** (`frontend/zone_doors.cpp`, the client's `EQSwitch`): levers, lifts, traps and
  the like are all "doors". `OP_SpawnDoor` is a u16 count and the 44-byte `Door_Struct`s deflated; each
  is the actor `<name>_ACTORDEF` from `<zone>_obj.s3d`, scaled to its size / 100, turned to its heading and
  tilted by its incline (eqgfx takes these as a placement's heading and pitch, `createActorFromFileContext`).
  Types 50 (pressure plates), 54, and 57 with an incline of 1 (teleporters) are invisible, and 53 has no
  model. `EQSwitch_UpdateDoorStates` moves them each frame in 50 ms ticks by type: 0-8 swing a quarter
  turn at 8 a tick (5-7 the other way), 10-27 slide sideways, 30/35/40/130 tip over, 59-81 and the traps
  rise or sink (lifts go their parameter's distance at 0.45 a tick), 100-107 spin, and one left open
  closes by itself after `GetSwitchStateChangeTime` (10 s for types 0 and 5, 20 s for most). A left
  click on one (the first door the ray from the camera hits before the zone) is `EQSwitch::UseSwitch`:
  nothing while it moves, "You can't reach that, get closer." (1462) beyond a quarter of its radius +
  our height + 20, else `OP_ClickDoor` (8 bytes: id, skill, item 0xffff, our spawn id). The zone
  answers everyone with `OP_MoveDoor {id, state}` (2 opening, 3 closing), which is `ChangeState`. A
  door is solid where it stands, so closed doors block, lifts carry, and the camera stops at them.
  Not done: sounds, keys and lockpicks, pressure plates and touch teleporters, the particle-effect switches.
- **Position updates:** `OP_ClientUpdate` as `pack_physics` packs it (the speed * 10, the heading in
  256ths, its change per tick, y/x/z*10, and the velocities in 16ths per tick). `ProcessControls` flags an
  update when the heading moves 2 or the speed 0.2, `DoPassageOfTime` every second, and `DoMainLoop` sends
  at most every 150 ms and stops repeating an unchanged update after 6.
- **Other spawns:** each race's model is `EQPlayer::GetActorTag`'s (`render/actor_tags.cpp`, the client's
  race table): `<tag>_ACTORDEF` from `<zone>_chr.s3d`, `<zone>_chr2.s3d` or `global*_chr.s3d`, else race 75's
  (`CreatePlayerActor`). Every model, player races included, is sized by `EQPlayer::SetHeights`: size / 5,
  with the model's origin `ModelHeightOffset` (3.125 x size / 5) over the feet. The models are built for
  that, ~6.5 units tall with their feet ~3.125 under the origin, once their bones' scales are applied:
  a halfling (3.5) is 4.7 tall, a human (6) 8. Their names float over them within 120 units,
  hidden by anything in front.
- **How they look:** the spawn's `bodytexture` (byte 101) is the client's `EQPlayer::Texture`. 0xFF (players)
  dresses each slot from the equipment list; anything else is an NPC's texture for every part
  (`CDisplay::SwapNPCMaterials`: every `<tag>PPTTVV_MDF` material becomes `<tag>PP<texture>VV_MDF` where the
  model has one, untinted), so a Qeynos guard is race 71 (`QCM`) with texture 1. The head is a mesh of its
  own: `SwapHead` puts `<tag>HE<nn>_DMSPRITEDEF` in place of `HE00`, chosen by the NPC's `helm` (byte 102)
  or the helm material (chain is 2), and the helm's tint goes only on its `HELM*` / `CHAIN*` materials, not
  the face. `OP_WearChange` changes a slot's material and tint.
- **Animations:** the client plays animations by number; the table at 004d7a21 (TAKP calls it
  `initializeHotkeyLabels`) names their tracks: 1..11 `C01`..`C11` combat, 16 `D05` death, 17..25
  `L01`..`L09` walking to treading water, 26 `O01` fidget, 32..38 `P01`..`P07` idle to sitting, 39..47
  `T01`..`T09` casting. Each bone plays its own track, else the one of the model
  `mapCharacterPrefixToRaceCode` names (`HUM` and `QCM` play `ELM`'s, `GNM` `DWM`'s, a minotaur the gnoll's), from
  any loaded archive (the elves' sitting idle is in `global3_chr.s3d`), and a missing animation falls back
  as `getFallbackAnimationId` says, then to the idle.
- **How they move:** `unpack_physics` puts a spawn where its update says, with its speed (the update's
  animation byte / 10), turn and velocity. The server sends a walking NPC with no velocity at all, so
  `process_physics` moves it: each frame the speed pushes along the heading against friction (0.625, halved
  while an NPC moves), which settles at 1.16 x speed a tick, the 23.3 x speed units a second the server
  moves it. Gravity pulls unless the spawn flies or levitates (`flymode`, byte 100: 3, the NPC default, floats
  only in water), and the feet stay on the floor (`MovePlayer`, `CheckForUnderFloor`). We stop a spawn 5 s
  after its last update, since the server only updates spawns in range.
- **What they play** (`CDisplay::ProcessAnimation`): idle, with a fidget one time in four each second (pets
  one in a hundred); shuffling (`P03`) while turning on the spot; walking, or running once faster than the
  walk speed, at `1 + velocity / 2`; swimming or treading water; jumping and falling. Stances play
  `EQPlayer::ChangeStance`'s transitions (sitting down is `P02`, standing up the same backwards, `L08`
  ducks, `D05` lies down), corpses lie dead, and `OP_Animation` plays the server's attacks, casting and
  emotes once (its ids are the same numbers).
- **Time of day:** `OP_TimeOfDay`'s hour (1..24, 1 is midnight) carried on at 20 game seconds a second
  (`EQWorldData::AdvanceTime`). Zones of time type 1, 2 and 5 take their ambient light from the client's
  hourly red, green and blue tables (`SetAmbientLight`); other zones get 0.1. No channel goes under 0.18,
  `CDisplay`'s minimum ambient (set by its constructor, which TAKP's function list calls `CEverQuest::loadOptions`). As eqgfx's
  `calculateMeshLighting` does, a zone vertex is its baked color plus its alpha times the ambient light,
  so nights outdoors go dark blue but lamp-lit corners stay lit. Placed objects are lit the same way by
  their own colors: each `objects.wld` placement (0x15, flag 0x100) points through a 0x33 to a 0x32 with
  one color per vertex of its actor's meshes in turn, baked with the torches near it. Materials of render
  method 0x0B (fire) and 0x17 are additive and unlit, drawn full bright. A light reaches what's drawn when
  its flags share a bit with EQGfx's mask (`processPhysicsEffects`): 0x30000000 for characters, 0x20000000
  for meshes with baked colors. The zone's own lights (`lights.wld`, flags 0x100) get 0x10000000 from
  `createPointLightFromFileContext`: they light characters (a lamp-lit room lights the people in it) but not
  the zone, whose colors they're baked into. The light a spawn carries (`LightType`, Spawn_Struct byte 94
  and SpawnAppearance type 5: a torch, lantern or lightstone) gets both from `CDisplay::CreateLight`, 2
  units in front of it and 4 over its center, its color and radius from `InitCommonLights` (a torch
  flickers yellow through 10 colors at 200 ms, radius 100). A character's 3 brightest lights (0.05
  radius^2 / (distance^2 + 1), among those whose sphere reaches its BSP region) add color x 0.05 radius^2
  x cos / distance^2 to each vertex; carried lights do the same to the zone's and objects' vertices (up
  to 8, nearest the camera, each on the meshes it reaches). `SetSunLight` adds the sun: a directional
  light colored like the ambient (outdoors only), its angle 320 at 3:00, 0 (overhead) at noon, turning 128
  every 6 hours, shining along (0, sin(512 - angle), -cos(512 - angle)); on the zone it's times the vertex's
  exposure, like the ambient. A character's ambient and sun are times the exposure of the floor under it
  (`s3dGetActorScaledAmbient`: its triangle's vertex alphas, straight down 400), so indoors it's the room's
  lamps that light it. The world's constant ambient (fragment 0x35, 0 in most zones) is added to what has no
  baked colors. Everything stops at
  full light (the DLL clamps each
  channel to 255). Infravision and ultravision (`EQ_Character` Vision1/Vision2: the dark elf has
  ultravision, the human, barbarian and erudite neither, everyone else infravision) raise the ambient to
  their color, (82, 55, 164) or (75, 38, 38), and add it times (1 - our floor's exposure, followed 0.01 a
  frame) to everything as the world's special ambient; infravision also gives every warm-blooded spawn
  (body type not 3, 5, 8 or 12) a constant red of 100 while the sky's light is under 0.53 (until it's over
  0.7: `SetPlayerConstantAmbient2`, the color `CDisplay` keeps at 0x2e10). The debug menu's RENDER section
  picks the vision to try (ours stands in for spells and items to come). The sky dims along `SetSkyBackground`'s
  60 day periods (night until 3:00, dawn to 9:00, day to 15:00, dusk to 20:48), and the fog darkens with
  the light. The sun and moon aren't drawn yet.

- **Occlusion culling** (`render/occlusion.cpp`): with `UseUmbra` on (eqclient.ini's default) eqgfx
  culls with dPVS, not the regions' stored PVS. `initializeWorldGraphics` makes one cell for the
  whole zone with no portals; `createDPVSObjectForRendering` makes an object for each region's mesh
  and each actor, whose occluders are its solid (not 0x10), opaque, unmasked triangles; and
  `resolveVisibility` goes nearest first, drawing each object unless what was written before hides
  it, then writing its own. We do the same with a 256-wide depth buffer of the view, spawns
  included. Against unculled renders from 80 views per zone under 0.002% of pixels differ; Qeynos
  hides 777 of the 974 meshes in view.
- **Our look** (`ZoneSession::UpdateAppearance`): after our items move, `CEverQuest_UpdatePlayerAppearance`
  and `CDisplay::HandleMaterial` per armor slot (the worn item's material and tint, a custom helm's IT
  number), `UpdateEquipmentDisplay`'s weapons (the ranged item in the left hand when both hands are
  empty) and `UpdateLight` (the brightest worn light, a lightstone in a general slot over it). The
  slots that changed go to the zone as `OP_WearChange` and the light as `OP_SpawnAppearance`.
  Velious materials 17..23 come from `global17_amr.s3d`..`global23_amr.s3d`, or stand in as leather,
  chain or plate without them (`CDisplay_SwapMaterial`; 4 and 23 are leather on all but a monk).
  Helms 240 and 241 are each race's Velious helm by gender (IT530..IT656), anything from 240 its IT
  model, and they hang on `<tag>HE_DAG` (`ActorInfo`'s DagHead), not `HEAD_POINT_DAG`. A held
  item's particle clouds (a torch's flame and smoke) are kept by item and bone, so a model rebuilt
  for a look change carries them on (`CDisplay::ChangeDag` makes them for a new item alone).

The zone screen fills the window: the 3D view under the SIDL windows; with the debug menu open, our
details and the nearest spawns at the top left and the radar at the top right.

## Spells

- **Spell data:** `spells_en.txt` (`core/spell_file.*`): names, the casting texts, range, cast time,
  recast, the first SPA, target type, `goodEffect`, `AEDuration`, `CastingAnim` / `TargetAnim`,
  both effect indexes, `new_icon` and `deleteable`.
- **Spellbook** (`frontend/spell_book.*`, `CSpellBookWnd` and `CCastSpellWnd`): the gems' book
  button or `/book` opens it for casters and sits us down; standing up closes it. Left and Right
  turn two pages. Slots show the spell's `A_SpellIcons` cell on `Red`/`BlueIconBackground`.
  Scribing is a scroll on the cursor clicked onto an empty slot: `StartSpellScribe`'s checks, 92
  ticks, then `OP_MemorizeSpell` 0. Memorizing is a book spell onto the cursor (`CCursorAttachment`
  type 1) clicked onto an empty gem, `OP_MemorizeSpell` 1; a gem's right click forgets it (2). Two
  slots right clicked swap (`OP_SwapSpell`); Delete asks, then sends `OP_DeleteSpell`.
- **Gems:** the spell's `A_SpellGems` icon on the background ring in its target type's color,
  dimmed until it can be cast (`RefreshSpellGemButtons`, `setGaugeColor`).
- **Casting** (`frontend/spell_cast.*`, `CastSpell`): a gem click or `/cast 1-8` runs eqgame's checks
  and messages (standing, level, line of sight, mana, target type, range, body type, pet spells),
  sends `OP_CastSpell` and shows the casting window's gauge. The zone answers with
  `OP_MemorizeSpell` 3 for the recast and recovery, `OP_ManaChange` to end the cast, and
  `OP_TokenText` for why one failed. A gem click stops a bard's song.
- **Anyone's casts** (`frontend/spell_casting.*`): `OP_BeginCast`, `OP_Action` and `OP_Damage`.
  `CEverQuest_StartCasting` prints "%1 begins to cast a spell." (ours is 12205; a bard's is
  `other_casts`; rogue poisons are silent). `TriggerSpellEffect` plays the caster's `CastingAnim`,
  except for a bard without an instrument. `ReportSuccessfulHit` prints `cast_on_you`,
  "You have taken %2 points of damage." or `cast_on_other` (within 75 units of the target), and
  plays the target's `TargetAnim` (`D02` for detrimental damage).
- **Buffs** (`frontend/buffs.*`): the profile's, a spell landing on us (`HitBySpell`, with
  `FindAffectSlot`'s stacking, blocking and overwriting), `OP_Buff` both ways, the 6 s tick and
  expiry, and a click taking a beneficial one off. The buff window shows the icons on red or blue
  and blinks their last ticks. Buffs count in the stats, resists, AC, max mana and speed (SoW,
  snares, roots), and max HP is `Max_HP`'s, not the zone's HP updates.
- **Luclin effects** (`frontend/spell_effects.*`, `render/spell_particles.*`): a spell's
  `SpellsNew.eff` record plays stage 0 while casting and stage 2 on landing, each emitter chosen by
  the caster's level (NPCs count as 1). The emitters sit on the caster, the target or a summoning's
  spot and follow their bones (`CH_DAG`, `HEAD_POINT_DAG`, `L`/`R_POINT_DAG`, `BO_L`/`R_DAG`).
  They're eqgfx's `CParticleSystem` (`createParticleEmitter`, `renderParticleSystem`): `spellsnew.edd`
  v105 emitter definitions and `SpellEffects\` textures, a burst and then emissions at the density,
  with a distance level of detail. There are ten spawn shapes, velocities and accelerations in the
  bone's frame, fades, a color lerp, 2x2/4x4 cell sheets, streaks and spin. Texel alpha blends,
  alpha tested at 8.
- **Old effects:** a spell without a `SpellsNew.eff` record draws its `spells.eff` record instead
  (all 256 are read, as `loadSpellEffectsFromFile` insists). These are the `PCLOUD` particle clouds
  on bones and floors, and `EQEffect` sprite flashes and rings that light like lanterns. The debug
  menu's Luclin spell effects switch forces them for every spell. Each stage's sound plays from the
  caster or the target.
- **Missiles** (`frontend/missiles.*`, `EQMissile` @00502540): `OP_Projectile` (`0x4045`,
  `Arrow_Struct`, the missile's own first 0x70 bytes) is an arrow, a thrown weapon or a bolt in
  flight, only to look at: the zone says what it hit. The caster plays `GetAnimationIdForAttack`'s
  animation (44 for a spell). `ProcessMissile` makes it the first frame: `CreateActor` of the packet's
  model (else `IT63`, else none and it's gone) with its light, out 2 minutes; a spell's (kind 9) is out
  its `AEDuration`, or 30 s for a bolt (travel type 3, which also keeps it level), and gets the
  effect's stage 1 on the missile (on whom 2). Then once 32 ms have gone by each frame: `MissileAI`
  turns it at its target's center plus half its `ModelHeightOffset`, 16 a step at most, asking where
  that is every 125 ms to a second by flat distance (every step under 60, hit under 2, pointed
  straight at it under 32; not at all once it's dead); `process_physics` pushes it along its heading
  at its speed x 0.8 onto 0.625 of the last push (0.85 when not aimed, as the server's bolts are), in
  50 ms steps; `MoveMissile` ends it on a spawn in its way (not the caster) or when its time's up, and
  holds it at the zone's walls. Two things differ from eqgame so it glides onto its target: it's
  drawn on along its push between the 32 ms steps (eqgame's sits still between them), and a climb
  off by under 16 is stepped too (eqgame leaves it until the last 32 units, then snaps).
  - **This server's bolts** (`Mob::ProjectileAnimation`): kind 28 rather than 9, object 0 rather than
    the spell, model `IT68` (a small orb) with light 8, speed 1, not aimed, level. So no spell
    effect rides on them, and the caster swings (animation 5, skill 0's) rather than casting.
    Speed 1 not aimed settles at 5.33 units a 50 ms step, 107 a second, against the zone's own bolt
    (`Beacon::BoltSpell`) at 100. `SpellsNew.eff` has no missile stage for any bolt (only the
    fireworks); `spells.eff`'s bolts name `GENC10`/`GENU00` sprite actors, and the server's comment
    says the model should be `GENC00` or `GENW00`, gequip's blit-sprite actors.

## Items and stats

- **Moving items** (`core/inventory.*`, `CInvSlot`, `SwapCombine`, `AutoInventory`): described under
  Running the GUI. Each move is `OP_MoveItem`.
- **Slots** (`CInvSlotMgr_MoveItem`, `CInvSlot::HandleLButtonUp`): 0 the cursor, 1..21 worn,
  22..29 general, 250 + (general - 22) * 10 + n in their bags and 330 + n in a bag on the cursor;
  the bank's 2000..2007 and 2030 + bank * 10 + n; the trade window's 3000..3007 (3008..3015 the
  other side's) and 3030 + trade * 10 + n; a world container's 4000 + n; a corpse's 5000..5029; a
  merchant's 6000..6079 (the inspect window's 8000..8030 take no clicks). EQMacEmu's slots are the
  same (`ServerToMacSlot` passes them through). Nothing comes back out of the trade window, a
  corpse's or a world container's stack takes no quantity window, and an item goes into a trade
  slot only while trading, an empty one, and no-drop only to an NPC.
- **Labels:** `NoWrap` defaults to false (SIDL.xml), so a label's text breaks at its spaces into
  lines as wide as the label, each line aligned on its own (`AlignCenter`, `AlignRight`): the
  merchant's "Item Being Considered" is "Item Being" over a centered "Considered".
- **Item buttons:** the bag window's `Container_Icon` and the merchant's `MW_SelectedItem` show
  their item as the button's decal (`A_DragItem`'s cell: `SetContainer`, `SelectBuySellSlot`) over
  its Normal, `A_RecessedBox`. The item display's `IconButton` is different: `SetItem` makes
  `A_DragItem` its Normal.
- **Bag windows** (`frontend/containers.*`, `CContainerMgr`, `CContainerWnd`): the right button on
  a bag (`CInvSlot::HandleRButtonUp`, no Shift, Ctrl or Alt) opens its window or closes it; at
  most 17 are up. `SetContainer` keeps it in the layout as `BagInv1..8`, `BagBank1..8`,
  `BagTrade1..16` or `BagWorld1`, first placed at the screen's spot moved 5 right and 2 down per
  general slot after the first (a bank slot 5 left and 2 down, a trade slot 5 left and 2 up). The
  bag's name goes on `Container_Label`; the title stays the screen's "Container". It
  shows `bag_slots` of its ten slots, then `Container_Combine` (bag types over 8 but not 51) and
  Done, each 4 under what's above and centered, and sizes the window 14 wider and 36 taller than
  they take. Opening and closing play wave 0x91, or 0x86 and 0x85 for bag types 6, 7, 10, 15,
  18..20 and 22. Picking up or swapping out a bag closes its window, and Escape closes them all
  after the skills window. Alt over one shows every slot's tooltip at its right less 5, its top +
  2 (+22 in the second column) (`PostDraw`). `HandleCombine` refuses the class and racial
  containers (types 24..27 a class's tome, eqstr 12010; 31..45 a race's forge or kit,
  12011..12023), a cursor with anything on it (12024) and an empty bag (12025), then sends
  `OP_TradeSkillCombine` (0x4105, 32 bytes: type, slot, the ten item ids, the bag's id @30) and
  locks the slots (`DAT_007d0254`) until the zone's empty answer.
- **Coins** (`Inventory::MoveMoney`, `CEverQuest_MoveMoney` @00530fd3): coins live in four places,
  0 the cursor, 1 the purse, 2 the bank, 3 the trade window, each platinum (0) to copper (3). A
  money button (`CInventoryWnd`/`CBankWnd::ClickedMoneyButton`) with nothing on the cursor opens the
  quantity window for that pile (`CQuantityWnd::Activate`: none does nothing; Shift all, Ctrl one)
  and its notification 0x1d moves that many onto the cursor (wave 0x8a); with coins on the cursor
  it takes them all onto its own pile. The inventory's character view takes them as their own
  button would. Another coin only while the bank's up: to a bigger coin `amount - amount % 10^n`
  goes and `amount / 10^n` comes, to a smaller one `amount * 10^n`. `OP_MoveCoin` (0x412d, 20
  bytes: from, to, from coin, to coin, amount) numbers the coins the other way, 0 copper .. 3
  platinum, and carries the amount asked even when none moved. On the cursor (`CCursorAttachment`
  type 3) they show as `A_DragItem` cell 143 + `CursorMoneyType` (1 platinum .. 4 copper, the
  biggest coin there), the count centered in font 1 over a dark box across the bottom, 4 up. The
  profile has the bank's coins at 2940 and the cursor's at 2956.
- **Bank** (`frontend/bank_window.*`, `CBankWnd`): `CEverQuest::RightClickedOnPlayer` (the right
  button let go within 500 ms of going down on the world, `CEverQuest::RMouseUp`) opens it on a
  banker (class 16) within 20 (`CDisplay::PlayerDistance`, 3D) when `CEverQuest_IsOkToTransact`
  (no camp, combine, world container, spellbook, trainer, bazaar, trade, bank, merchant or loot
  window). Anything else it doesn't handle is `_consider` with nothing typed, which considers our
  target, not who was clicked. `Activate` says eqstr 1465 ("%1 tells you, 'Welcome to my bank!'"),
  puts the banker's name on `BW_BankerName` and opens the inventory too if it was closed.
  `OnProcessFrame` leaves the bank when the banker is over 20 away, checked every 500 ms.
  `Deactivate` (Done, Escape right after the item display and quantity windows, walking away)
  says 12143 ("Come back soon!"), closes the inventory if the bank opened it and closes every bag
  window.
- **Merchants** (`frontend/merchant_window.*`, `CMerchantWnd`): the right button on a merchant
  (class 32) within 20 sends `OP_ShopRequest` (0x400b, 12 bytes: merchant, us) and waits
  (`DAT_007d0258`, part of `IsOkToTransact`). The answer (`msg_req_merchantitems`: command @4, 1
  open; the rate @8, a float, kept as `MerchantGreed`) opens the window and the inventory with it.
  The items come deflated in `OP_ShopInventoryPacket` (0x400c: a count, a byte, then {u16 item
  class, `Item_Struct`} records), each in its `equipSlot` 0..79 (slots 6000..6079); `OP_ShopDelItem` (0x4038) takes one out. Their
  `Price` is the item's times its SellRate, and `SellRate` comes as 1. While the window's up, a
  left click on any slot only selects it (`CInvSlotMgr_SelectSlot`: a yellow 0xffff00 outline,
  `CInvSlotWnd`'s 0x114) and says its price in chat color 0x113 (`User_20`, yellow by default; what else it says is 0x111): `ValueBuy` is `round(count *
  price * rate)` (times SellRate when it's over 1), `ValueSell` is `round(price / rate) *
  count`, to the nearest 100 from 10,000,000; their text (`ValueSBuy`, `ValueSSell`) is eqstr
  2409..2412 per coin (" 1 gold 5 silver"), or 2413 / 2408 for nothing. A stack's message says
  "per" (4020 buying, 12026 selling), anything else "for the" (4019, 12027), and item type 39 its
  monthly rent (4021, a twelfth of the price). Buy is disabled when the purse can't pay for one;
  Sell for NO RENT, NO DROP (`CanDrop`) or worthless items. Buying asks the quantity window for a
  stack (20 at most), checks lore items (1573), room in the general slots (1571) and the purse
  (12060, 12061), then sends `OP_ShopPlayerBuy` (0x4035, 16 bytes: merchant, us, its slot @4,
  the count @8) and locks the slots (`DAT_007d0254`); the answer (slot @4, count @8, price @12;
  slot -1 refused) takes the price out of the purse (`HandleMoney`, breaking coins) and says
  "You give%1 to %2." (1437). Selling checks one at a time (12063), NO DROP (12921, 12062) and
  an empty bag (12064), sends `OP_ShopPlayerSell` (0x4027, the same layout), and the answer
  adds `ValueSell` to the purse (the fewest coins) and says 1361 in color 0x114; the client
  takes the item away itself, as the zone deletes it without a packet. Done, the close box,
  Escape and walking more than 20 away across the ground (every 500 ms) send `OP_ShopEnd`
  (0x4037: merchant, us) and clear our target. The slots scroll in `MW_MerchantSlotsWnd`, a
  `Style_VScroll` screen; our step is 10 pixels.
- **`OP_MerchantItemPacket`** (0x4031) isn't a merchant's: the zone's `ItemPacketTrade` (what
  `PutItemInInventory` sends: something bought, traded for or handed back by an NPC) goes out
  under it, and eqgame's `HandleWorldMessage` gives it to `msg_snd_ieq` as it does
  `OP_ItemPacket`: the item into our slot `equipSlot`.
- **Trading** (`frontend/trade_window.*`, `CTradeWnd`, `CGiveWnd`): a left click on a player or NPC
  with an item or coins on the cursor (`LeftClickedOnPlayer`, `DropItemOrMoneyOnPlayer`) asks to
  trade when `IsOkToTransact` and they're within their `ModelHeightOffset` + 20 (3D; further:
  12294), a NO DROP item only to an NPC (12921, 12922): `OP_TradeRequest` (0x40d1,
  `BeginTrade_Struct` {target, source}). The zone answers an NPC's at once with
  `OP_TradeRequestAck` (0x40e6) and passes a player's on; their `msg_req_trade` opens the window
  and acks, or (busy) sends `OP_TradeRefused` (0x40d6, {target, source, reason}: 'b' nobody, 'c'
  group only, 1 busy) and says 1447. The ack (`msg_begin_trade`) opens ours with the cursor's item
  or coins in it, the give window for an NPC. Items go onto the same stack in our slots, else into
  the first empty one (8 for a player, 4 for an NPC), as `OP_MoveItem` to 3000 + n; full is 12319
  in color 10. Coins on the cursor onto our money buttons go in (`OP_MoveCoin` to 3). Theirs come
  as `OP_TradeItemPacket` (0x40df: {u16 from, i16 slot (0..7, or 30 + bag * 10 + n), u8 type (0
  common, 1 bag, 2 book, 0xff gone), the item, short for bags and books}) and `OP_TradeCoins`
  (0x40e4, `TradeMoney_Struct`: 0 copper .. 3 platinum, how many more). The names are red, green
  once that side accepted; any change takes both back (`UpdateTradeDisplay(0)`). Trade sends
  `OP_TradeAcceptClick` (0x40da: {the other side, u8 1}), theirs comes the same way, and
  `OP_FinishTrade` (0x40dc) closes it; what we get comes as `OP_MerchantItemPacket` items and
  `OP_TradeMoneyUpdate` coins. Cancel sends `OP_CancelTrade` (0x40db, {target, source}) and the
  zone's answer (target: who cancelled) closes it with 1450, 1448 or 1449. Escape, the close box
  and walking off (every 500 ms) cancel. Closing forgets our items and coins in it: the zone hands
  them back. eqgame's `Deactivate` would send a second `OP_CancelTrade` and say 1450 again after
  the zone's cancel; ours doesn't.
- **The ground** (`frontend/ground_items.*`, `ZoneView`'s ground items): `OP_GroundSpawn` (0x402c)
  is an `Object_Struct` (224 bytes; 222 arrive): item id @8, drop id @12, charges @24, heading @140,
  z @144, x @148, y @152, the actor `"<IDFile>_ACTORDEF"` @156, a dropped bag's items' ids @186,
  the object type @212 (a tradeskill container's bag type). HandleWorldMessage keeps it in the
  EQItemList and `CreateActor`s it at `GetFloorHeight` from 1 over it, plus 0.5; ours is the item's
  model (`CharacterModels::BuildItem`: `IT<n>_HS_DEF` or `IT<n>_DMSPRITEDEF` from gequip*.s3d on a
  one-bone model), with its particle clouds (a torch's flame and smoke, as a held one's).
  `CDisplay::CreateActor` (@004b0120) looks `<name>_ACTORDEF` up in everything the world has loaded
  (gequip*.s3d, the zone's `_obj.s3d`, the `_chr` files); for a ground spawn a missing one is
  `IT63_ACTORDEF` (a bag), then `RED_BLOB_ACTORDEF` (a sprite in gequip.s3d). It sizes the
  tradeskill containers: in Rivervale, Ak'Anon and Kaladim (zones 19, 55, 60, 67) IT70 and IT74
  at 0.8; elsewhere IT66, IT69 and IT73 at 1.5 and IT74 at 1.3; `BBBOARD*` at 1.5. The server's
  `object` table is all `IT<n>` but one `CHEST1` in oasis (which oasis_obj.s3d doesn't have, so
  it's a bag there too); `ground_spawns` has a few bad names (`IT`, `ORD`, `63`) that show as bags.
  Ours doesn't search the zone's `_obj.s3d`. **Clicks** (`CDisplay::GetClickedActor` @004b008a,
  eqgfx `s3dPickObjectOnScreen`): a ray from the camera through the pixel, out to the far clip,
  and the actor whose triangles it hits first (`collideRayWithEntity`), spawns, our own model
  outside first person, and ground items alike, invisible ones left out (`t3dIsActorInvisible`).
  `CEverQuest_ClickedPlayer` and `EQItemList_is_item_actor` each take that one actor, so a model in
  front of an item takes the click (`ZoneView::Pick`, `CharacterModel::RayHit`).
  `CEverQuest_LMouseUp` (new UI) goes, for a click that didn't drag
  and with no transaction waiting (`DAT_007d0254`): the spawn under the cursor
  (`LeftClickedOnPlayer`); else, with no trade, an item on the cursor is dropped (not a common
  item of type 12, 33 or 39, which click the doors), coins do nothing, and with nothing held a
  ground item within 20 (3D) is asked for, `OP_ClickObject` (0x402b: drop id, us), holding
  `DAT_007d0254` and `DAT_007d0258` and opening the inventory; else the doors (`ClickedSwitch`). The
  zone answers everyone with `OP_ClickObject` (who picked it up, or 0 when it decays: either one
  answering ours lets the holds go; drop id 0xffffffff, a lore item refused), the item coming on
  the cursor as its own packet. Dropping: NO DROP says 12921 or 12922; `/fastdrop`
  (`DAT_0079966c`, 0 from eqOptions2.opt to start) 1 drops at once, -1 says 12320, 0 asks with
  the confirmation dialog (category 0x15, 20 s, 10011); `DropHeldItemOnGround` says 10012 (color
  15) and sends our own `OP_GroundSpawn` with the item where we stand (the zone puts it down there
  and tells everyone, us too). A tradeskill container answers a click with `OP_ClickObjectAction`
  (0x40d7: who @0, drop id @4, open @8, type @10, slots @12, icon @16; `msg_chest_lock`): not us,
  nothing; open 0 is 1406; one already open 1443; else `OpenWorldContainer` names it by its type
  ("Forge"...) and opens it as `BagWorld1` at 4000. Its items come as `OP_ObjectItemPacket` (0x40fb,
  slot 0..9), `OP_ClearObject` (0x4205) empties it, Combine sends slot 1000, and
  `CContainerMgr::Process` closes it past 20 (every 500 ms); `CheckCloseable` keeps it open (Done
  disabled, Escape passes it by) while a NO DROP item's in it. Closing sends `OP_ClickObjectAction`
  {us, drop id, 0} (20 bytes). `AutoGenInventory` with no room for the cursor's item drops it at
  once, with no confirmation and whatever `/fastdrop` says (12937, color 15), or keeps a NO DROP
  one on the cursor (`CanDrop`'s message). The zone's merchant overflow and anything else
  `CreateGroundObject` makes come the same way. `IsOkToTransact` waits on all of it.
- **Looting** (`frontend/loot_window.*`, `CLootWnd`): the right button on a corpse is
  `CEverQuest_LootCorpse`: within `ModelHeightOffset * 1.6 + 14` (`PlayerDistance` with 1.2, up and
  down counting 1/1.2) and with nothing else waiting it sends `OP_LootRequest` (0x404e, the corpse's
  id); further it says 12390 and considers our target. `OP_MoneyOnCorpse` (0x4050:
  response @0, platinum, gold, silver, copper @4..) is `DoLoot_handleLootCorpse`: 2 is 12070 ("not
  at this time"), 0 is 12073 ("someone is already looting"), 1 or 3 adds the coins (12072,
  `BuildMoneyText`'s "1 platinum, 2 gold and 3 copper", color 0x11d, wave 0x8a), kneels
  (stance 105) and opens the window with the corpse's name. TAKP's eqgame.dll hands each
  `OP_LootItemPacket` (0x4052, one `Item_Struct`) to `msg_send_corpse_eq_equip`, which puts it in
  the first free loot slot keeping its `equipSlot`, the corpse slot, for `OP_LootItem` (0x40a0, 12
  bytes: corpse, us, that slot @4, auto loot @8). A left click asks for it onto the cursor, the
  right button into the inventory; a NO DROP item from another's corpse asks first (2072, the
  confirmation dialog for 20 s; /lootnodrop 1 only for what we can't use, 2 never). The zone's
  `OP_LootItem` answer has the slot taken (-1: 1370). Done, the close box, Escape and walking off
  past `ModelHeightOffset * 1.5 + 20` across the ground send `OP_EndLootRequest` (0x404f) once,
  and the zone's `OP_EndLootRequest` or `OP_LootComplete` (0x4145), or the corpse despawning,
  closes it, clears our target and stands us up.
- **String expansion** (`orderedStringExpansion` @004f7ea2, `EqStr::Format`): `%1`..`%9` are the
  arguments, `%Tn` the string whose id is argument n expanded with the same arguments (the
  zone's 554 "%1 says '%T2'" for a merchant's welcome), `%#id` that string inline, `%%` a
  percent sign; ten deep at most. `%R` (grammar rules) isn't ours yet.
- **Item display** (`frontend/item_display.*`, `CItemDisplayWnd`): alt+click (`CInvSlot::HandleLButtonUp`)
  or the right button held 750 ms (`HandleRButtonHeld`) shows it; letting go over a slot after the
  hold closes it. `SetItem`'s text shows the following:
  - the status flags (MAGIC, LORE, ARTIFACT, SUMMONED, NO DROP, NO RENT), slots and charges
  - a scroll's level, skill and mana, and a weapon's skill and delay
  - AC and DMG with the damage bonus, in red at full value when `AntiTwinkAdj` brings them down
  - elemental and bane damage, skill mod, faction, stats and saves
  - recommended and required levels, effect and focus spells, food and drink sizes
  - weight, range and size, and the classes, races and deities that can use it
  `LORE:` shows only when `OP_LogServer` says the world auto-identifies.
- **Links** (`frontend/item_tags.*`): an item tag is `0x12`, `'0'`, a six-digit id, the text, `0x12`
  (`CEditWnd::AddItemTag`, `ConvertItemTags`). EQMacEmu sends say links the same way. The chat shows
  a tag in `User_71`'s color (`convertColorIndex` 0x146), blue under the mouse and navy while pressed.
  A click sends `OP_ItemLinkResponse` (0x4264) with the id and the first word. The zone answers an
  item's with its `Item_Struct`, or says a say link's phrase for us. A line holds at most 10, and
  Backspace takes one whole.
- **Inventory window:** stats are base plus worn items through `AntiTwinkAdj`, 1..255, plus the
  first food and drink in the general slots and bags (`GetAttributeModifierFromItems`). They're
  colored as `getComparisonColor` does: grey at the base, green above, red below. AGI is times
  `encum_factor`. The class is its Kunark title from 51 (`KunarkClass`), and the AA gauge shows
  from 51.
  - **AC** (`EQ_Main_CalcDefense`): `(old_ac without the soft caps + compute_defense) * 1000 / 847`.
    Worn AC is four thirds for all but the casters. `GetClassACBonus` adds a monk's by weight, a
    rogue's and beastlord's by AGI, and an Iksar's by level.
  - **ATK** (`CalcATKValue`): `(offense + compute_tohit) * 1000 / 744` with the primary's skill, hand
    to hand without a weapon.
  - **Resists:** `basesave_*` by race (and a monk's disease and poison by level from 51), the worn
    items', and the class bonuses (a warrior's half level to magic; monk, ranger, beastlord,
    paladin, shadow knight and rogue more each level from 50), to 500.
  - **Weight** (`cur_encumbrance`): the cursor, worn and general slots and their bags (less weight
    reduction), and the coins (less the coin bags 17201..17230). It's red over our STR.
- **Skills window** (`CSkillsWnd`): the skills the server says we have (0..73, not 254 or 255),
  then the languages, with eqstr names and ranks from Awful to Master. `OP_SkillUpdate` prints
  "You have become better at %1! (%2)"; `OP_SkillUpdate2` updates it silently.

## Sound

`audio/eq_sound.cpp` is the client's `EqSoundManager` (@004d482a) and what's under it (Miles' sound
objects, `MusicManager`, the emitters), on our own mix (`audio/mixer.cpp`) instead of Miles.

- **Files:** a sound is a loose file in the EQ folder, else in `snd1.pfs` to `snd14.pfs` (the fourteen
  `EqSoundManager` mounts; `snd15`-`17` and the `snd21`-`61` copies aren't). The 2000-odd waves are all
  mono 8- or 16-bit PCM. Music is `.mp3`, or `.xmi`: Miles' Extended MIDI, several sequences to a file,
  each MIDI without running status whose delays are runs of bytes under 0x80 at a fixed 120 ticks a
  second (the files' tempo events are left over; their notes fall on the beats at 120 a second), and whose
  note-ons carry their durations. Controllers 116 and 117 are Miles' FOR and NEXT loops and 110-120 its
  own (not passed on), and the sequence volume scales every channel's volume controller, as
  `AIL_set_sequence_volume` does.
- **Music** is `MusicManager`'s list of tracks (`MusicManager::Set`: an asset and sequence, a loop count,
  a priority and three fades). Turning a track on or off leaves `MusicManager::GiveTime` to pick the one
  that plays: the active track with the lowest priority number, the playing one keeping a tie. The one it
  replaces fades out over its switch fade, or its stop fade when nothing follows. A track whose asset is
  still sounding fades back up instead of starting over (`SoundObject_PlayOrAdjustSoundVolume`), so
  walking between two areas of a zone's `.xmi` keeps the tune going until it has faded right out (Ak'Anon's
  fade out over 50 s). `LoadGlobalMusic`'s tracks: 1, the opening, and 4, character select, are both
  `eqtheme.mp3` at priority 10 (`CDisplay::InitDDraw` turns 1 on, the main loop off; character select
  turns 4 on and entering the world off); 2 combat (priority 3, `combattheme1` or `2.mp3` at random); 8
  death (priority 2, played once); and `gl.xmi`'s sequences for the merchant, bank and guild windows.
  Volumes are the options' levels / 20 (`CEverQuest_loadOptions`. TAKP's function list has
  `SetMusicVolume` and `SetWaveVolume` swapped: `SoundVolume` goes to the one it calls `SetMusicVolume`,
  which sets the emitters' volume).
- **Zone emitters:** `<zone>.emt` (`EmitterLoad`; `<zone>_sounds.eff` is the fallback, but every zone with
  sounds has an `.emt`), a line each: a leading number, the file, then the columns its header names, in
  world coordinates (the WLD's: the server's y, x, z). An `.xmi` or `.mp3` one is a music track of
  priority 10000 + its activation range, on while we're in that range. A `.wav` one (`SoundEmitter::GiveTime`)
  wants to play while we're in range and its repeat delay (min + rand() % (max - min) ms, rolled again each
  time) has passed: looping (loop type 0, or no delays) it holds its voice until we leave the range and it
  fades out, else it plays once each time the delay comes round. A randomized one plays somewhere within
  that distance around us, level with us. The day and night columns follow `SetupEmitterEnvironment`
  (night is an hour under 5 or over 18 on the 1..24 clock, so before 4:00 and from 18:00; every half second); the environment column `SetGenericEnvironment`:
  1 outside, 2 a ceiling under 500 over a covered floor (exposure under 0.48), 3 a ceiling within 20.
  An emitter for another is off.
- **Voices** come from the client's pools (`SoundManager::InitializeSoundSystem` and the constructor): 6
  plain and 6 positional on channel 0, 1 plain (MP3 music) and 2 positional on channel 1, 4 positional on
  channel 2 (the emitters), and the one sequence. A sound whose pool is empty doesn't play, so at most four
  emitters sound at once, a held loop keeps its voice until it's let go (even out of earshot: Shar Vahl's
  emitters have a 10000 activation range), and a new MP3 or XMI waits for the last to fade out.
- **Positional voices** are "Miles Fast 2D Positional Audio", the provider the client asks for: full
  volume within the emitter's full-volume radius, min / distance past it, silent past its maximum (and not
  started there), panned by where it is relative to the way the camera faces. The listener is our
  character (`SetListenerLocation` in `RealRender_World`). Volume changes are straight-line fades, as
  `updateSampleVolume` steps them, and a voice faded to 0 ends.

- **Game sounds** are `EqSoundManager`'s 4000 numbered waves (`WaveLoad`, `WavePlay`): `LoadGlobalWaves`'
  lists (the jumps and the rest of each sex and size's, from 32; the footsteps, water, spells, weapons and so
  on from 92), the doors' and the creatures'.
- **Doors** (`EQSwitch::LoadDoorSoundEffects`, `ChangeState`): each type loads its waves as it spawns
  (metal, stone, sliding doors, the drawbridge, portcullises, levers, traps and spears), and changing state
  plays its type's for opening, closing, or (portcullises and the drawbridge) shutting, where the door is
  (`CDisplay_PlaySoundAtLocation`: full volume within 5, silent past 80). Wood doors use the global
  `DoorWd_O`/`DoorWd_C`. A door closing by itself (`ToggleDoorState`) goes round `ChangeState`, so it's quiet.
- **Creatures and us** (`frontend/actor_sound.cpp`): `EQPlayer::SetSounds` gives each race (by sex, body
  texture, and for one race its height) a set of sounds: idle and fidget loops, jump, get hit, gasps,
  drowning, death, attacks, casting, and its walk and run (a loop, or footsteps like ours). It's a 6.7 KB
  function of nested switches, so its table (`audio/actor_sound_table.cpp`) was taken by running it in
  Ghidra's emulator for every race, sex and texture. `CDisplay_PlayAnimationSound` runs with every
  `PlayAnimationTrack2`, so each frame with what the actor's doing: our footsteps (one of four, every 500
  ms), a spawn's walk or run loop on its `EqMobileEmitter` (which follows it, heard within 24 times half its
  height, and goes back to its idle loop at each change of animation), fidget loops, jumps, ladders, sitting
  and kneeling, drowning, dying, and attacks (the race's own one time in three, or by what's in hand,
  `CDisplay_GetItemType`: a weapon's whoosh, bare-handed kicks, stabs and punches, the whip). They play from
  the actor (`EQPlayer::PlaySoundA`: full volume within half its height, out to 24 times that); ours on a
  channel of our own at 0.8, and not over themselves. Landing (`CDisplay::CheckForUnderFloor`) at 1.25 a
  tick or faster out of water thumps; ours past 2.5 is `fall_hit` when `TakeFallDamage` would hurt (by the
  fall and our safe fall skill; only the sound). Our jump sound plays as the jump starts (our track holds
  the jump; the client's goes back to walking once the rise slows, which is when our footsteps carry on).

- **Water** (`EQPlayer::feetEnvironmentChange`, `headEnvironmentChange`): anyone's feet coming out of water
  or lava (`WaterOut`), or going into water at 1 a tick up or down or faster (`WaterIn`); our head going
  under plays `WatUndLp` (`Torch_Lp` in lava) once where we are, and coming up stops it. Our own sounds
  have the client's two voices, so a splash out right after a long splash in can find none.
- **Weather** (`CDisplay`'s `StartWeather`, `StopWeather`, `ProcessCloud`, `LightningStrike`): `OP_Weather` (type 1 snow, else rain; intensity 0 stops it) is taken up the next frame in
  the world. In an outdoor zone (time type 1, 2 or 5) the cloud comes in a step a frame (2/256; it goes at
  3/256), and once in, rain turns on the global `rainloop.wav` emitter ("It begins to rain.") and snow
  `wind_lp1.wav` ("It begins to snow."): half volume everywhere, 2 s in and 3 s out, silent while the floor
  over us is covered (`setSoundEmitterUnderwater`'s exposure under 0.48). While it rains, thunder
  (`thunder1`/`thunder2.wav`) comes as it arrives and every 10 to 20 s, three times in ten answered by the
  other within a second. Leaving the zone kills it at once.

  What it draws: from the start until it's gone there's no sky, and the fog is the weather's
  (`SetTimedFog`): each step coming in pulls the fog's start in by its start / 128 and its end by what's
  past 550 / 128, both times the Clip Plane (so at our 4, in a quarter of the way), to a start of 30 and an
  end of 550; going, it opens out by as much a step until the zone's own fog comes back. Once it's in,
  eqgfx's particle clouds (`render/particles.h`, from `gequip.s3d`'s sprites): rain is up to 1000 streaks
  (`RAIN_CLOUD`, one every 3 ms over a 450 square 250 above the camera, falling 200 a second, each a
  one-pixel additive line a fortieth of a second long in the rain's grey, red in the Plane of Fear),
  each tried against the world as it's made: one that meets a solid surface (or lava, slime) leaves a
  dot there for a fifth of a second (`RAINS_CLOUD`), one that meets water a ring (`RAINRING.BMP`)
  spreading to 3 over half a second (`RAINW_CLOUD`). Snow is up to 1500 flakes (`SNOWFLAKE.BMP`, half a
  unit across, wobbling) falling 50 a second over a 600 square, leaving dots where they land. When it
  stops, those falling finish. A thunder clap flashes for its frame (the ambient light at 0.99, the fog no
  darker than our floor's exposure). The debug menu's RENDER section has **No weather effects**, which
  draws none of it (the sound stays on its own switch), and a row with the weather's fog and its
  particles. Not done: the particle density option (at its highest), and the distance level of detail
  eqgfx gives a cloud made a second time.
- **The debug menu's SOUND section** shows the track playing and its number (1 the opening, 4 character
  select, 17 on the zones' music), the voices, the ambient ones by name and distance (of four), the
  creatures' loops, the emitters switched on and in range, the time of day and ceiling the emitters see,
  and the files the zone wanted that aren't there; and switches for all sound, the music (MusicManager's
  own, `/music`: what's playing fades out), the zone's ambience, the game's sounds (doors, steps, jumps,
  splashes, creatures) and the weather (rain, wind, thunder). Its weather row shows what's coming, here or
  going (with the cloud's fade and when thunder comes next), the last `OP_Weather`, and the weather's sounds
  playing by name. The switches last until the client exits.

Not yet: everything triggered by game events the client doesn't have yet (combat and death music,
item and hit sounds, the merchant/bank music), and EAX reverb (the
per-zone room types `SetCurrentZone` sets, and underwater's).

## Protocol

**Transport.** RDP, the reliable UDP protocol the 2002 client uses. The client links the
vendored `third_party/rdplib` and sets it up the way `net_connect` (eqgame.exe `0x0054cfb3`) does:
- endpoint flags `0x40000002` (CRC + IPv4)
- application data on stream 1
- data rate `0x1400`, send buffer `0x40000`
- each message is a little-endian `uint16` opcode followed by the body

**Login** (port 6000):

| Step | Client sends | Server replies |
|------|--------------|----------------|
| 1 | `0x0059` SessionReady | `0x0059` with a build date string |
| 2 | `0x0001` LoginPC: 40 bytes, `{user[20], pass[20]}` DES-CBC with Verant's key as key and IV | `0x0004` LoginAccepted with `"LS#<id>"`, or `0x0002` ClientError with text |
| 3 | `0x0052` LoginBanner | `0x0052`: 4 bytes, then the banner text |
| 4 | `0x0046` ServerListRequest | `0x0046`: `u16 count, pad[2], u8 showcount`, then per world `name\0 ip\0` and 13 bytes of flags |
| 5 | `0x0047` PlayEverquestRequest with the world IP string | `0x0047`: 17 bytes, the 10-character key at offset 1 |

The DES is built in (`core/des.cpp`). It was checked against OpenSSL and a real login.

**World** (port 9000):

| Step | Client sends | Server replies |
|------|--------------|----------------|
| 1 | `0x1858` SendLoginInfo: 200 bytes, `"LS#id\0key\0"`, zoning at 188, `0xc3` at 196 | `0x41c3` LogServer, `0x1007` ApproveWorld, `0x8001` EnterWorld, `0x41d8` ExpansionInfo, `0x4047` SendCharInfo (`CharacterSelect_Struct`, 1620 bytes) |
| 2 | `0x8001` EnterWorld with `name\0` | `0x41dd` MOTD, `0x8009` SetChatServer, then `0x8004` ZoneServerInfo (`ip[128], u16 port`) or `0x8005` ZoneUnavail |

| 3 | `0x408b` ApproveName: 76 bytes, `name[64]`, race, class and deity as u32 (`SubmitNameToWorld` @0040c84f) | `0x408b`: 1 byte, 1 = approved |
| 4 | `0x4049` CharacterCreate: `CharCreate_Struct`, 8452 bytes (`FinalizeNewPCAndSendToWorld` @0040c966) | `0x4047` with the new list, or `0x408b` 0 |

A rejected world login gets no error packet. The server just drops the connection.
`ZoneServerInfo.port` is in network byte order.

**Zone** (the port from `ZoneServerInfo`), following `ZoneAuthenticate` @0054c80d, `GetZoneInfoFromNetwork`
@0053d026, `CEverQuest_EnterZone` @0053d2c4 and `CEverQuest::DoMainLoop` @0053df6e:

| Step | Client sends | Server replies |
|------|--------------|----------------|
| 1 | `0x41e8` DataRate (float, 5.0), then `0x4028` ZoneEntry: `crc32(name, 32)`, `name[64]` | `0x4036` PlayerProfile, `0x4028` ZoneEntry (our spawn), `0x415f` ZoneSpawns, inventory, time, weather |
| 2 | `0x405d` ReqNewZone with the name | `0x405b` NewZone (572 bytes) |
| 3 | `0x400a` ReqClientSpawn | doors, ground spawns, zone points, `0x40d8` SendExpZonein |
| 4 | `0x40d8` SendExpZonein | `0x40f5` SpawnAppearance type 0x10 (our spawn id), `0x40d8` again |
| 5 | `0x40f3` ClientUpdate (15-byte position) | the server completes the connect |

- **Profile and spawns:** PlayerProfile (8460 bytes) and ZoneSpawns (224 bytes per `Spawn_Struct`,
  also used for single new spawns) are deflated, then scrambled with 64-bit add/rotate passes
  (`EncryptProfilePacket` / `EncryptZoneSpawnPacket` in EQMacEmu). `core/zone_session.cpp` undoes
  them, matching TAKP-YewV4's decryptors.
- **Positions:** x and y are whole units and z is in tenths. `0x409f` MobUpdate batches up to 33
  15-byte updates after a u32 count.
- **Chat:** `0x4107` ChannelMessage is 136 bytes plus the text, and the client sends `strlen + 141`.
- **Server messages:** `0x4236` FormattedMessage carries an `eqstr_en.txt` id and NUL-separated
  arguments.
- **Camping:** `0x4207` Camp, then `0x4150` Logout 30 seconds later (the server's camp timer), and
  `0x4159` LogoutReply.


  ## Thanks
  Thank you to the following projects & their contributors
  - [EQMacEmu](https://github.com/EQMacEmu)
  - [Zeal](https://github.com/CoastalRedwood/Zeal)
  - [TakpUnderthehood](https://github.com/adonislive/TAKP-Client-Underthehood)
  - [eqgame_dll_takp](https://github.com/EQMacEmu/eqgame_dll_takp) (the client fixes TAKP's eqgame.dll makes)
  - [eqw_takp](https://github.com/CoastalRedwood/eqw_takp) (eqgame's input globals and focus handling)
