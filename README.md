# Gnomebrew

A from-scratch client for EverQuest's Mac-era servers (TAKP, The Al'Kabor Project). It reads the
art, models, UI files and sounds from your own EverQuest install, never runs `eqgame.exe`, and
aims to look and behave like the original client.

Two programs share one core:

- **`eqclient_gui`** (Windows): the game client. It takes you from the login screen and server
  select through character select and creation into the 3D world, with eqgame's own windows,
  music and ambient sound.
- **`eqclient`** (console, Linux and Windows): a headless client for testing against a server. It
  logs in, lists or creates characters, and can enter a zone, say something and camp out.

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
setting is listed except the optional `[TextColors]` and `[KeyMaps]`. Without the comments, it looks like this:

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
UseLuclinHumanMale=0
UseLuclinHumanFemale=0
UseLuclinBarbarianMale=0
UseLuclinBarbarianFemale=0
UseLuclinEruditeMale=0
UseLuclinEruditeFemale=0
UseLuclinWoodElfMale=0
UseLuclinWoodElfFemale=0
UseLuclinHighElfMale=0
UseLuclinHighElfFemale=0
UseLuclinDarkElfMale=0
UseLuclinDarkElfFemale=0
UseLuclinHalfElfMale=0
UseLuclinHalfElfFemale=0
UseLuclinDwarfMale=0
UseLuclinDwarfFemale=0
UseLuclinTrollMale=0
UseLuclinTrollFemale=0
UseLuclinOgreMale=0
UseLuclinOgreFemale=0
UseLuclinHalflingMale=0
UseLuclinHalflingFemale=0
UseLuclinGnomeMale=0
UseLuclinGnomeFemale=0
UseLuclinIksarMale=0
UseLuclinIksarFemale=0
UseLuclinVahShirMale=0
UseLuclinVahShirFemale=0
UseLuclinElementals=0
AllLuclinPcModelsOff=0
LoadVeliousArmorsWithLuclin=1
LoadOldSkeletons=1
CharSelectAnimWindow=1
CharSelectEmotes=0
AttackOnAssist=0
CombatMusic=1
LootNoDrop=0
FastDrop=0

[Filters]
DamageShields=0
NpcSpells=0
PcSpells=0
BardSongs=0
ServerFilter=0
GuildSay=1
Socials=1
GroupSay=1
Shouts=1
Auctions=1
Ooc=1
MyMisses=1
OthersMisses=0
OthersHits=1
AttackerMissesMe=1
CriticalSpells=0
CriticalMelee=0
BadWords=0

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
  the options' music and sound levels, 0 to 20 (10 by default, half volume, as the client starts),
  set by the General page's sliders. `SoundRealism` is that page's third slider, the reverb's share
  of the positional sounds (see Reverb under Sound).
- `[Defaults]` `FastItemDestroy`, `GuildInvites` and `PlayerTrade` are the General page's Fast Item
  Destroy (1: Destroy doesn't ask), Receive Guild Invites and Allow trading with (2 everyone, 1 party
  members only, 0 no one), kept as they change (eqgame keeps them in eqOptions1.opt).
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
- `UseLuclin<Race>Male` / `UseLuclin<Race>Female` (`1` or `TRUE`) draw that race and sex with its
  Luclin model, `UseLuclinElementals` loads the Luclin elementals, `AllLuclinPcModelsOff` turns every
  Luclin player model but the Vah Shir's off, and `LoadVeliousArmorsWithLuclin=1` (the default) keeps
  the Velious armor files loaded with them: eqclient.ini's keys of the same names (see Luclin player
  models).
- `LoadOldSkeletons=1` (ours) draws skeletons (`SKE`, race 60 and those using it) with the old model
  from `global_chr.s3d`; `0` with the later one `global6_chr.s3d` defines first, as eqgame does.
- `CharSelectAnimWindow=1` shows our own Animations window at character select (not eqgame's):
  every animation the model has, by name, played by double clicking one. `CharSelectEmotes` picks
  what the character plays now and then: `0` eqgame's own picks for its class (combat moves,
  casting, the fidget), `1` half of them a social's instead (`S01`..`S29`, those its model has).
- `AttackOnAssist` is `/assist on|off`, kept as it changes. `CombatMusic` is `/combatmusic on|off`
  (eqclient.ini's `[Defaults] CombatMusic`, on unless set otherwise): whether auto attack plays the
  combat music. Kept as it changes.
- `LootNoDrop` is `/lootnodrop`: whether looting a NO DROP item asks first, `0` always, `1` only
  for what you can't use, `2` never (eqgame's `DAT_00799674`, kept in its `eqOptions2.opt`).
  `FastDrop` is `/fastdrop`: `1` drops an item clicked onto the ground at once, `0` asks first,
  `-1` never drops it (`DAT_0079966c`). Both are kept as they change.
- `[Filters]` are `/filter`'s switches and `/serverfilter`, kept as they change (eqgame keeps them in
  `eqOptions1.opt` and `eqOptions2.opt`): `DamageShields` and `NpcSpells` 0 shown, 1 hidden; `PcSpells`
  0 all, 1 off, 2 the group's; `BardSongs` 0 all, 1 ours, 2 the group's, 3 off; `GuildSay` through
  `AttackerMissesMe` 1 shown, 0 hidden; `CriticalSpells` and `CriticalMelee` 0 all, 1 ours, 2 off;
  `BadWords` 0 the bad word filter on.

- `RecordPackets=1` writes each zone session's packets to `replays\<char>-<time>.eqr` next to the exe.
  `--replay FILE` plays one back: the GUI goes straight into that zone with no server, the recorded
  packets arriving when they did (spawns, chat, doors, weather...). You can run around; what you send
  goes nowhere. When the log runs out you stay in the zone. Camping logs out when its timer runs, and
  zone lines are turned down. The console client and `frontend_preview` play the same files.
- The rest of `[Debug]` is the debug menu's (F11): whether it's open, the stat card's folded
  sections, its RENDER toggles and vision, and its SOUND switches. They're saved as you change
  them in the menu.
- `[TextColors]` is optional: the chat's text colors, keyed as eqgame's (`User_<n>_Red`,
  `User_<n>_Green`, `User_<n>_Blue`, n 1..72; all three, 0..255, or the built-in color). For
  example `User_1_Red=255` with Green and Blue makes says red. The Options window's Colors page
  writes them as you pick colors, and its Reset Default Colors writes every built-in one.
- `[KeyMaps]` isn't written for you either: the keys you bind on the Options window's Keyboard page
  go there as you bind them, in eqclient.ini's format (`KEYMAP_FORWARD=w`,
  `KEYMAP_FORWARD_2=SHIFT+CONTROL+UP`, `NOKEY` for none; see Keymap below), and Reload defaults
  takes them out again. A command with no key there has its default.

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
mouse has been off them a while. A window's Alpha and fade only reach its textures and its
color-only background: `CXWnd::DoAllDrawing` scales the UI alpha (`DAT_0062a864`) by them, and only
textured quads (`adjustColorAlphaAndRender`, `CTextureAnimation::DrawTexturedQuadWithAlpha`) and
DoAllDrawing's own background rect read it. Text, list selections and the caret draw as they are,
and `CXWndManager::DrawWindows` draws a window at alpha 0 too, so its text still shows
(`Canvas::SetAlpha`). A window with style 0x800 draws at its own UnfadedAlpha in place of its
parent's alpha; of the child controls only gauges set it (`CGaugeWnd`'s constructor @005c1d40,
mislabeled `KeyBindButton_InitializeKeyBindButton`; the decompile's `CGaugeWnd::CaugeWnd` @005a5be0
is the spell gem's), so a gauge, bar and empty background, never fades. The main chat window is
titled Main Chat (eqstr 6623) and has no close or minimize box (`CChatManager::CChatManager` clears
style bits 0x28), whatever `EQUI_ChatWindow.xml` says. Their layout is kept in eqgame's own format, in
`UI_<character>_<server>.ini` next to the exe (the server part is the login server's address cut
to 7 characters, as eqgame names it).

Right-click a window off its buttons, slots, gems and edit boxes for eqgame's window menu
(`WindowManager`'s menus, `CContextMenuManager`):

- **Its items** (`CreateDefaultMenu`): Background > Texture (eqstr 15006) and Tint Color (15007),
  Alpha > Normal Level (15004) and Faded Level (15005), Lock (6649, checked when locked), Minimize
  (15001) and Close (15000), with `A_DividerMiddle` separators. `WarnDefaultMenu` greys Background
  on a transparent window and Minimize and Close on one without those boxes (`EnableMenuItem`:
  `0xff808080`, else white). `CXWnd::HandleRButtonDown` opens it; `CSidlScreenWnd`'s init gives a
  top-level screen the default menu.
- **How a menu looks and works** (`CContextMenu`): a `CListWnd` in `WDT_RoundedNoTitle` and the
  system font (2), rows its font's height, columns Check ("*"), the text (its width + 4, 400 at
  most) and SubMenu (">"), from 3 left of the point and kept on the screen (`Activate`). The row
  under the mouse is yellow (`0x150`), and an item with a submenu opens it right of its row, or
  left of the menu at the screen's edge (`HandleContextMenuEvent`'s 0x14). A click on another item
  picks it and closes them all; a press anywhere else closes them.
- **What the items do** (`HandleContextMenuAction`): Texture switches the window's `BGType` between
  1, the template's background texture tiled and tinted by `BGTint` (`DrawClientBackground`), and
  2, the client area filled with the tint (`DoAllDrawing`'s `DrawColoredRect`; not kept in the ini).
  Tint Color opens the color picker with the window's tint. Normal and Faded Level open the
  quantity window from 0 to 100 at the alpha `x 100 / 255`, its answer `(n x 255 + 0x80) / 100` the
  window's `Alpha`, or its `FadeToAlpha` with fading turned on (`HandleContextMenuEvent`'s 0x1d).
  Lock, Minimize and Close do that. The tint multiplies the background texture, so white leaves it
  as it is (`wnd_bg_light_rock.tga` averages 29 of 255, `wnd_bg_dark_rock.tga` 6).
- **The color picker** (`CColorPickerWnd`, `frontend/color_picker.*`): fifteen swatches, each its
  button's `TextColor` in its `A_WhitePixel` decal (the constructor copies a button's 0x68 to its
  decal color 0x11c), the color so far in `CPW_TC15Button`, red, green and blue sliders of 256 ticks
  with three-character boxes (a slider moved writes its box; a box typed into moves its slider, over
  255 written back as 255), and Accept, which reads the boxes and gives the color to the window
  that asked (its BGTint) as the picker goes.

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
  in eqgame is in `frontend/game_windows.cpp`: the group window's Invite, Disband, Follow and Decline buttons, the
  buff window's buttons in columns from the top right (empty slots draw nothing), and the hot
  buttons. Click someone to target them, or `/target name`; Escape clears the target (and, as in
  eqgame, closes the help, options or inventory window if one's open). Gauges are drawn the way
  `CGaugeWnd::Draw` does it, bars sliding to a new value at a full bar a second.
- The chat window keeps the last 400 lines in eqgame's colors (from `[TextColors]` in
  `my-client.ini`, or the built-in ones), with a scrollbar and the wheel. Enter or `/` opens its
  input line, and the keymap's History up and down (Shift+Up and Shift+Down) bring back the last 40
  lines sent. Commands are looked up in eqgame's own
  table, in its order, so abbreviations work as they do there (`/g` is /gsay, `/s` is /split).
  So far: /say (or `'`), /channel, /tell (/msg, /send), /reply, /gsay, /guildsay, /shout,
  /auction, /ooc, /emote (`:`, /me), /hail, /target, /assist, /consider, /location, /time,
  /played, /motd, /afk, /anonymous, /shownames, /sit, /stand, /book, /cast, /camp, /clearchat,
  /random, /lootnodrop, /fastdrop, /autosplit, /roleplay, /targetgroupbuff (/tgb), /split, /yell,
  /consent, /attack, /combatmusic, /doability, the guild commands (see Guilds under In the zone),
  the raid commands (see Raids), /filter, /serverfilter and the socials (/smile, /wave and the
  other 86).
- **The default channel:** text typed with no command goes where the chat window's default channel
  says (`CEverquest_InterpretCmd`, after `CChatWindow::WndNotification` puts the window's +0x140 in
  `DAT_00798598`): 0 /guildsay, 2 /gsay, 4 /auction, 5 /ooc, 7 a /tell to the name at +0x148 (`"%s
  %s"`), 15 `CRaid_SendRaidChat` (no expansion check, unlike /rsay), 19 /chat to a chat channel,
  anything else /say; a line starting `/` that isn't a command is 12380 in red. It starts as
  `DAT_00798598` (`SetDefaultGameValues`: 8, or eqclient.ini's `DefaultChannel`; ours is always 8)
  and `/channel` (@004f799f) changes it: the word matched whole and without case against eqstr
  13070 GuildSay, 13071 gsay, 5111 rsay, 13072 tell (the next word the name), 13074 Auction, 13075
  OOC, 13076 Say, 13077 chat (the next word the channel), or the short forms gu, g, rs, t and auc;
  13079 "Your default channel for typing is now %1." with the matched string (13073 "Tell to %1",
  13078 "Chat to Channel %1"); nothing or anything else prints the usage, 13080..13083. The new UI
  keeps it per chat window (the active one's); the old UI wrote eqclient.ini instead. We have one
  chat window, and no chat server for 19 (it says /chat isn't supported).
  The rest say they aren't supported yet; a GM's commands act as if they weren't there for
  anyone else.
- `/consider` sends `OP_Consider` (`OP_ConsiderCorpse` on a corpse) and prints the answer as
  `PrintConsiderAssessment` does: the faction, the assessment by our level in the con color, and
  the PvP and deity-team rules. `/time` is `CEverQuest_WhatTimeIsIt`: Norrath's date carried on
  from `OP_TimeOfDay` (nothing in the Plane of Time or on Luclin), then Earth's. `/afk` and
  `/anonymous` are SpawnAppearance 24 and 21. While we're AFK, `msg_new_text` answers a tell
  (channel 7 or 14) with a tell back: "AFK Message: " (eqstr 1418) and /afk's message, or eqstr
  1419 without one, but not a tell starting with eqstr 1417 ("Sorry, I am A.F.K.") or 1418, so two
  AFK players don't answer each other for ever. It isn't echoed in our chat. `/assist` sends
  `OP_Assist`, and the zone's answer becomes the target.
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
  full stop goes after a final letter or digit, the text goes out as `OP_Emote` (its first u16
  0xffff, where `/emote` sends 0; the zone ignores it), and the echo is
  printed in the emote color (`0x107`). A social with an animation (`PlayAnimationTrack` id, e.g.
  77 for /smile) plays it on us and sends `OP_Animation {u16 us, pad, i32 id, float 1}`, but only
  standing still. A social with only a lone text (/fidget) does nothing with a target; /nudge has
  only a targeted one. The table (`Socials` in `frontend/chat_commands.cpp`) was read out of the
  exe's two switches: the sent one Ghidra recovers; the echo's jump table (0x535d63) it doesn't.
  `DoPercentConvert` runs on the built text after the full stop, before it's sent; the name
  typed after the social is looked up first, so `/smile %t` finds no one.
- **DoPercentConvert** (@00538110, `FrontEnd::PercentConvert`): a `%` at a line's first or second
  character, or after a space or an apostrophe, followed by t, m, s, o, p or r (either case) is
  replaced: `%t` the target's `stripName` (@00537e4b: up to an apostrophe, no digits or '#',
  underscores as spaces) or `Target`; `%m` the first spawn whose `PetOwnerSpawnId` is ours
  (temporary or not) or `Pet`; `%s`, `%o`, `%p` the target's he/she/it, him/her/it, his/her/its by
  gender (literals, not eqstr; no target: it, it, its); `%r` a player target's race
  (`CEverQuest_GetRaceDesc2`), `Corpse` for a player's corpse (Type 2), `NPC` for an NPC or its
  corpse, nothing with no target. After eleven, a qualifying `%` becomes `@`. The position test
  measures from where the last `%x` ended in the old text, not the new. `print_chat_dsp_chat` runs
  it on every line it prints (anyone's chat included), and `SendNewText` (say, tell, group, shout,
  auction, OOC, guild, raid), `CEverQuest::Emote` and `send_social` on what they send.
- The Actions window's tabs (Main, Abilities, Combat, Socials) switch pages, drawn as `CTabWnd`
  draws them (`DrawTab` @00593f80: the label centered across the tab inside its border, at the top
  of it, not centered down it: `DrawWrappedText`'s flags 0x15 lack eqgfx's 0x20, so a tall
  letter's tail is cut off by the tab's bottom), and its Camp, Run and Walk buttons work; Run or Walk, Sit or Stand and Invite or
  Follow show as eqgame picks them. The socials page and the hot button window page through ten
  sets with their arrows, round from the last to the first.
- Socials work as eqgame's do: the first page starts with its twelve (Afk, Anon, ... Wave), a
  right click on a social (on the Actions window or a hot button) opens the Edit Social window
  (a name, its color and five lines), and clicking one does its lines a step apart, with
  `/pause N[, command]` waiting about N tenths of a second. They're kept in `[Socials]` in
  `<char>_<server>.ini`, eqgame's format.
- Names over heads are eqgame's name sprites (`CDisplay::SetNameSpriteState`): a player's first
  name and surname, LFG, AFK and LD, and `<guild>` on a second line (the world's guild list, asked
  for as you enter the world, and the zone's changes to it); an NPC's or corpse's name alone. Their colors are eqgame's: blue
  players, teal NPCs and corpses, green GMs, red PvP, purple roleplayers. They're a world unit tall
  (smaller further off, the size they'd be at 20 units when closer; a 20 pixel bold Arial stretched, as
  eqgfx draws them), shown as far out as the zone is drawn (eqgame's own cutoff is 70 units; see
  `EnableExtendedNameplateDistance`). A name hangs on the actor's head point
  (`CDisplay_UpdateDAGPointers`' `DagHeadPoint`: `<tag>HEAD_POINT_DAG`, else `<tag>HE_DAG`; a Luclin
  model's `<tag>HEHEAD_DAG`; none, no name, as for horses), posed with the animation each frame:
  `renderSceneHierarchy` lifts it up the screen by 0.55 of the actor's scale (1.1 on a DAG whose sixth
  letter is H, 1.2 an ogre's) and `renderNameTag` by 1 more, and the first line is centered there
  (closer than 20 units, its bottom stays where the full-size line's would be). eqgfx queues the
  text (`t3dDeferCachedNameTagTextW`) and `t3dProcessRenderQueue` draws it with `D3DRS_ZENABLE` on
  and z writes off, at the head point's depth, so walls, trees and models in front cover it pixel by
  pixel. Ours (`ZoneView::DrawNameSprites`) draws each line as a camera-facing quad of its text's
  coverage (`TextRenderer::Coverage`) in the 3D pass the same way: translucent, depth tested, not
  written. Your target's name flashes from white back to its color, as eqgame shows
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
  (`SenseHeading`) points it true for 5 s. It's up when the layout last had it up, and Toggle
  Compass (`TOGGLE_COMPASSWIN`, Alt+D) opens and closes it.
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
- **Movement keys on W, A, S and D:** the keymap's defaults move with W, S, D and A (the arrows their
  alternates), Autoattack is Q and Duck F. eqgame's `default_key_bindings` moves with the numpad
  (8, 2, 6, 4; the arrows the alternates), Autoattack on A and Duck on D. Everything else is
  eqgame's. Because ExecuteCmd runs nothing whose own key types a character while the chat line is
  open (see Keymap), the arrows don't move you while you type with these defaults; with the arrows
  as the keys and W A S D as the alternates, they would.
- **Options window's not-implemented rows:** for now the Keyboard page lists a command Gnomebrew
  doesn't do yet in red. The window keeps its EQUI size. The Mouse page shows empty.
- **Far Clip Plane** starts at its maximum (`[Defaults] ClipPlane=20` in my-client.ini, 4.0, the view
  out to the zone's max clip) where eqgame starts at 5 (1.0, the zone's own fog).
- **Sky** starts at Double Layer (`[Defaults] Sky=2`, the cloud layer drawn) where eqgame starts at
  Single Layer.
- **Look up and Look down** tilt the view 5 degrees a press and keep going with Windows' key repeat;
  eqgame's `ProcessControls` speeds the tilt up while the key's held.
- **Debug menu** (F11, ahead of the keymap's Toggle netstat): the stat card with its folding sections, the 2D radar, the RENDER toggles
  (wireframe, no fog, collision walls and our sphere, floor marker, no occlusion culling, no clip plane, no
  weather effects, Luclin spell effects, VSync, vision) and the SOUND switches, kept in `[Debug]`.
- **Packet record and replay:** `RecordPackets=1` writes each zone session to an `.eqr`, and
  `--replay` plays one back with no server (GUI, `eqclient` and `frontend_preview`).
- **Mouse wheel camera zoom:** the wheel zooms from first person out to 200 units and back, in a zone
  and in character select's explore mode.
- **Emotes in character select's random animations** (`CharSelectEmotes=1`): eqgame's
  `DoClassRandomAnimation` picks only from its class's combat moves, casting and the fidget. With
  this on, half its picks are a social instead (`S01`..`S29`, of those the model has).
- **Character select Animations window** (`CharSelectAnimWindow`): ours, not eqgame's. It plays any
  animation the model has, by name, and switches between Combat only and All + emotes.
- **Character creation's random faces by the new race:** eqgame's `RandomizeFacialFeatures` rolls the
  hair, beard and eyes on the model before a race or gender button switches it, by the old race's
  rules, so a Luclin model coming from one without hair (Iksar, Vah Shir, Erudite, a troll or ogre
  man) is bald. Ours rolls them for the race and gender being switched to, so nobody starts bald.
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
- **Luclin models off by default** (`UseLuclin*`): a missing key is off, so every race starts with
  its old model. eqgame's `CDisplay::GetNewPCIniFlag` takes a missing key as TRUE (and writes it)
  for the Vah Shir, and for everyone when the Luclin or Planes of Power flag is set, as on TAKP.
- **Velious armor with Luclin models** (`LoadVeliousArmorsWithLuclin`): a missing key is on, so
  turning on a Luclin model doesn't take the Velious armor textures off everyone on the old models.
  eqgame takes it as off and skips the `Global<n>_amr` files then. The Luclin models themselves never
  wear them: those textures are cut for the old models (two chest pieces on the old UVs, where a
  Luclin chest has three), and eqgame with the key on puts them on half the chest over the bare
  shirt. A Luclin model wears the armor they stand in for (17..22 as leather, chain, plate), as eqgame
  does with the key off.
- **Cycle nearest target** (`KEYMAP_CYCLE_NEAREST_TARGET`, Tab by default, command 213 on the
  Keyboard page's Target list): Zeal's "Cycle through nearest NPCs" (`CycleTarget::get_next_ent(250,
  NPC)`). The NPCs drawn within 250 of us (across the ground), level above 0, not invisible to us,
  not a player's pet, with a clear line from our head or feet to their head or feet, nearest first;
  each press targets the next, skipping the one already targeted, and three seconds after the last
  press it starts again at the nearest. Not while the chat line has the keys (Zeal's
  `UI_ChatInputCheck`), where Tab is Cycle reply's. eqgame has no such command. Our commands are
  numbered from 200 (`KeyMap::OwnFirst`, Zeal's numbers where it has them) and can be bound past
  eqgame's 0x74 limit.
- **Toggle last two targets on no key:** eqgame's default is Tab (`default_key_bindings`), which
  is ours Cycle reply's (in the chat line) and Cycle nearest target's (outside it).
- **Old skeletons** (`LoadOldSkeletons`, on by default): eqgame loads `global6_chr.s3d` before
  `global_chr.s3d`, so its later skeleton model is the one drawn; we skip global6's `SKE` (its model
  and tracks) and draw global_chr's.

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
  --attack NAME         with --zone: target the nearest NPC whose name starts with NAME, turn auto attack
                        on and walk at it (facing it, 30 units a second), printing the OP_Damage and
                        OP_Death packets and our own swings
  --skill N             with --attack: use combat skill N on it too (OP_CombatAbility), every 9 s in reach
  --group-invite NAME   with --zone: 15 s in, once NAME is in the zone, invite them (OP_GroupInvite)
  --group-accept        with --zone: accept any group invitation 2 s after it comes (OP_GroupFollow);
                        every group packet prints
  --raid-invite NAME    with --zone: 15 s in, once NAME is in the zone, invite them to a raid
                        (OP_RaidInvite 3)
  --raid-accept         with --zone: accept any raid invitation 2 s after it comes (OP_RaidInvite 4);
                        every raid packet prints
  --idle SECONDS        without --char, stay at character select this long (default 0)
  --record DIR          with --zone: write each zone session's packets to DIR/<char>-<time>.eqr
  --replay FILE         play a recorded zone session back (no server): stay --zone seconds (default
                        10), print what's around, then camp out
  --no-retry            don't retry once after "Invalid username or password"
  -v                    hex dump every packet
```

## Preview tool (Linux)

```
build/frontend_preview EQDIR OUTDIR [HOST:PORT USER PASSWORD [charselect|create|createluclin|explore|shop|train|ground|commands|group|raid|guild|skills]]
build/frontend_preview EQDIR OUTDIR --replay FILE [SECONDS]
```

This drives the real front-end code with scripted input and writes PNG snapshots. Text is
drawn as outline boxes, since there's no GDI. With a login server it logs in, snapshots server
select and character select, and presses Enter World (add `charselect` after the password to stop
at character select instead, or `shop` to stand by the nearest merchant, open its shop with the
right button, sell the first thing worth anything and buy it back, then camp; or `train`, as
Replaytest, to stand by our class's nearest guildmaster, open its training window with the right
button, print the rows, sort by value both ways and by name, pick the first skill we have, Train
and Done; or `ground` to list
what's on the ground, drop an item and pick it back up, and open a forge and close it; or
`commands` to type /random, /lootnodrop, /fastdrop and socials (alone and at the nearest spawn),
print the chat, and snapshot the compass as it turns; or `group`, with Claudebot in the zone through
`eqclient -u clauderobot -p testpass1 --char Claudebot --zone 200 --group-invite Replaycaster
--group-accept`, to stand by them, follow their invitation in the group window, click their gauge,
type the /who lines, /follow them from 70 away (printing the gap each second) until Move back ends
it, /disband, then click them, /invite them, select them with Target party member 1 and /disband them,
printing the group and the window's buttons at each step); or `raid`, with Claudebot in the zone
through `eqclient -u clauderobot -p testpass1 --char Claudebot --zone 200 --raid-invite Replaycaster
--raid-accept`, to type the raid commands out of a raid, accept their invitation with /raidaccept,
/raidwindow, take it with the window's Accept, type them as a member, leave with Disband, then
/target them and Invite, type them as the leader, click their row, open Options and pick Leader
Selected, Add and Remove Looter, color warriors through the color picker, Make Leader them and
Disband, printing the raid, both lists with their colors, the labels and the buttons at each step,
then form it again, set the default channel with /channel (rs, tell Claudebot, gsay, say, and
the usage) typing plain text between, and camp out in the raid; or `guild`, as Replaytest leading a
guild (one made in the database: rows in `guilds` and `guild_members`, rank 2, an id under the
server's `Guild:MaxGuilds`, then a server restart), to type each guild command, set and ask for
the MOTD, answer /guilddelete's question yes (which deletes the guild) and camp; or `skills`, as the account's
Replayrogue, to target the nearest NPC and turn auto attack on for 4.5 s (our swings' messages and
fatigue), then use bind wound, feign death, hide, sneak, pick pockets, tracking, fishing, begging,
hide and sneak again from the abilities page (`UseSkill`), printing the chat and our stance,
visibility, sneaking and hiding after each, then hide and step forward. `create` instead makes an Erudite wizard from the first empty slot,
snapshots every creation screen and the face picker, submits it, and deletes it again (Yes in the
confirmation dialog). `createluclin` does the same with every Luclin model on, as a dwarf warrior
whose face picker steps through each feature, printing the rows shown and what the model takes,
then what's sent and what the server lists. `explore` goes into explore mode, turns to the chase
camera, prints the name tags, clicks the other character, walks forward, and leaves with Escape. It's for
checking layout and flow without Windows. `--replay` plays a packet log back instead (no server)
and snapshots the zone once it's in, then every 3 seconds for SECONDS (default 10), then drags,
opens, minimizes, resizes and closes windows (the layout goes to OUTDIR), types into the chat window
(a say, commands, /target) and prints what the chat window holds, then stands near the target to
snapshot the names over heads (from two distances and in first person) and prints what a player's
name says at each `/shownames` level. Along the way it opens a bag (a made-up backpack when the
character has none) and moves an item in and out, stands by a banker, right clicks it and
moves an item and coins into the bank, and gives an NPC coins and then an item through the give
window (the replay answers as the zone does for an NPC), Give once and Cancel once. It also turns
auto attack on and off with Q, /attack and /sit, printing whether it's on, the last chat line and
how many frames draw the player window's lasso, and snapshots it on (`q1-auto-attack-lasso`). On the
Actions window's combat page it right clicks the first skill button, prints what SkillsSelectWindow
lists, puts Kick on it (`s1-combat-skill-button`), targets the nearest NPC, stands in front of it
and uses Kick twice and by `/doability 7`, then types `/filter` and `/serverfilter` lines.

```
build/scene_preview EQDIR OUT.png --char RACE GENDER CLASS [--face F] [--equip M,M,...] [--velious 0|1] [--camera H] [--frames N] [--size WxH]
                    [--luclin all|RACE:GENDER,...] [--old-skeletons 0|1] [--hair STYLE COLOR] [--beard STYLE COLOR] [--eyes COLOR]
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
none of it). `--luclin all` (or `RACE:GENDER,...`) draws those races with their Luclin models, as
`UseLuclin*` would, `--old-skeletons 0` draws global6's skeleton, and `--hair STYLE COLOR`,
`--beard STYLE COLOR` and `--eyes COLOR` dress the character, or the `--npc` before them. `--frames N` times N renders.

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
`CEverQuest::DoCharacterSelection` (@0053b9cf) and `CCharacterSelect::WndNotification` (@0040f278)
do:

- **Enter World** (`CCharacterSelect::EnterWorld`, Enter, a double click): with a character
  selected, the user light goes and `0x8001` goes out with the name.
- **Delete** asks eqstr 10019 "Are you absolutely sure you want to delete %1?" in the confirmation
  dialog (category 0x1b, 10 s) and turns the character, Explore, Enter World and Delete buttons off
  (`SetButtonsEnabledState(0)`). Yes (`ProcessYes`, `CCharacterSelect::DeleteCharacter` @0040f93e)
  deletes the character's option file (`CEverQuest_RemoveCharacterOptionFile` @0053b149:
  `<name>_<server>.ini`, ours the hot buttons and socials) and sends `0x405a` with the name. The
  window's per-frame function (vtable +0xac, @0040f23b, which the decompile misses) turns them back
  on: when the confirmation dialog isn't up and the first character button is off, it calls
  `SetButtonsEnabledState(1)`. So they're off only while the dialog is.
- **Quit** (`ActivateBackButton`, Escape) sends `0x4023`. **Rotate** turns the camera's turning on
  and off; off, it follows the character's heading.
- **Explore** (`EnterExplorationMode` @0040f3f2) hides the window for `ExploreModeWindow`, whose
  `EXW_CharInfoLabel` is eqstr 6681 "%1 [%2 %3] in %4" (`CExploreModeWnd::Activate`), and switches to
  the default camera (`CDisplay_SwitchToDefaultCameraMode`: `DAT_0063b8ec`, first person until F9's
  `CDisplay_ToggleView` changes it). `CreateExplorationModePlayers` stands the account's other
  characters round ours (`SetRoomLocationByIndex`: our class's room spot 4 up, then by index 20 out
  in y and x, 15 for a magician, 12 for a necromancer's first, facing us), each playing
  `DoClassRandomAnimation`'s picks; ours doesn't (`Render_MinWorld`) and walks under
  `ProcessControls`. Keys go to `ProcessNewUICharacterSelectKeys`: Escape leaves
  (`LeaveExplorationMode`), Enter enters the world, the rest are `ProcessMovementKeys`. A left click
  on another character selects it (`CEverQuest::LeftClickedOnPlayer` by name), which leaves and comes
  back into explore mode with it (`SelectCharacter`). Every character has its name sprite.
- **The character buttons** select (`SelectCharacter`), or on an empty slot open character creation;
  `UpdateButtonNames` names the empty ones eqstr 1947 "Create a New Character". Up and Down are
  `SelectCharacter(1001)` and `(1000)`: the one before (from the first, the last filled slot) and
  the one after (past the last or onto an empty one, the first); none if the first slot is empty.
  On entering it's `SelectCharacter(1002)`: `[Defaults] LastCharSel`, else the first.
- **Nobody selected** (`SelectCharacter` with no valid character): the player stays as it looked
  (at first `initializeNetworkGame`'s human man) and is named eqstr 4000 "Newbie", a level 1
  warrior, in the warrior's room.
- **A zone that's unavailable** (`0x8005`) sets `DAT_0079831c`, and character select puts up eqstr
  1954 "That zone is unavailable." in the confirmation dialog (category 0x1d, 4 s).

The confirmation dialog is `CConfirmationDialog` (`ConfirmationDialogBox`, @00415a80): category 0x1d
shows OK alone, the rest Yes and No; it's centered, and its timeout answers No (unless category
0x20). Another one coming up answers the first No.

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
  for Vah Shir), or the Luclin ones `UseLuclin*` picks (see Luclin player models), play their `P01` standing idle; races without one borrow another model's (the human
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
- **Light:** `CEverQuest`'s constructor starts the zone's time type at 1, so the time of day passes:
  `SetLocationByClass` sets the hour (16, or 23 in the bard's, monk's, necromancer's, rogue's and
  wizard's rooms; `SetLoadZoneWeather` and the old load zone 24) and `SetSunLight`, which give the
  ambient light, the sun and the sky's brightness as in a zone (`frontend/day_light.cpp`). The zone is
  its baked light plus the ambient over its vertices' exposure; the character is lit as in a zone
  (its floor's exposure, the zone's lights). `SetAmbientLight` gives it its race's vision too:
  `SwitchModel`'s `SetHeights` runs `EQ_Character::InitInnates` on the PC for the selected race, and
  `DoCharacterSelection`'s `SetDefaultGameValues` sets the infravision and ultravision colors, so as in
  a zone the ambient rises to the vision's color and the world gets it as special ambient times
  1 - the exposure (the first call takes the exposure as it is, each later one moves it 0.01;
  ours: the character's floor), and infravision's glow is on in the 23:00 rooms and the old load zone's
  midnight (`CreatePlayerActor` gives it the explore mode characters too). `SelectCharacter` gives it a light
  (`CDisplay::CreateUserLight`: smlantern's color, radius 75), which `MovePlayer`'s `MoveLight`
  keeps 2 in front of it and 4 over its center and `EnterWorld` takes away.
- **Switches:** `CCharacterSelect::Activate` in load2 loads `load2_switches.dat`
  (`CEverQuest_LoadSwitchesNonAvatar` @005472a3), 0x90-byte records as `EQSwitch_DoorLoadingCode`
  reads them: the id at 0xc, the name at 0xd, the type at 0x1c and state at 0x1d, y, x, z, heading
  and incline (floats) at 0x34, inverted at 0x48, size at 0x60 and parameter at 0x62. It keeps those
  with a model and the particle effects (type 53): load2 has one door (`LOADDOOR300`, type 27, in
  the rogue's room) and eight `CRATEG` effects.
- **The old load zone** (`SetCharacterPositionInSelectScreen`, without load2): `IsSpecialLocationRequired`
  (@0040f511: Dark Elves, Trolls, Ogres, Iksar, shadow knights, necromancers, deities 1, 3, 4, 201,
  203, 206) stand at y 0, x -315, z 6 with the yon at 400; the rest at 0, 0, 7 with it at 140.
- **Name:** `CDisplay::SetNameSpriteState` with the character select window up is
  `"%s [%d %s]\n%s"` (name, level, `GetClassDesc`, `GetFullZoneName`), tinted yellow and lit as in a
  zone (the ambient + 0.4).
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
Only what was drawn on goes up: every draw marks the 64x16 tiles it touches in a `CanvasDirty`
(the GDI text renderer each string's box), Finish clears only the tiles drawn on last frame (all
of its block when the block moved or the last frame didn't clear it), and Present uploads each run
of tiles drawn on this frame or the last (`UpdateSubresource`).
In the zone the 2D drawing is kept on the canvas from frame to frame (`frontend/retained_ui.cpp`):
nothing on it depends on the view, so what was drawn last frame is still right wherever nothing
would draw differently. Every piece (each window, the menus and tooltip, the Alt tooltips, the stat
card, the cursor) is first drawn into a `CanvasRecording`, which draws nothing but hashes every call
and its arguments; where a piece's hash or box changed (or it came or went), its old and new boxes
are cleared to the view and everything over them drawn again in order, clipped to them. Finish
leaves the block alone then. Not with the software renderer, under a dialog, or with the debug
menu's wireframe, wall or floor lines.

Things learned about the files:

- WLD x/y are the server's y/x (z is up; +x is north), checked against the EQEmu map `load2.map`.
- Bone tracks are 8 int16s a frame, as EQGfx's `processTransformationNode` reads them: a quaternion
  `{w, x, y, z}` / 16384, a shift / 256, and the bone's scale / 256 (unsigned), which multiplies down to
  its children's shifts, scales and meshes. It isn't the shift's denominator: the halfling, dwarf and
  gnome males' pelvis is 1.6 (410), without which they're two thirds the size of their females.
- BMP textures are used bottom-up (v = 0 is the bottom row); the zones' DDS textures (DXT1/3/5) are
  stored that way already.
- A bitmap (0x03) can carry two names. A second `<file>_LAYER` one (`t3dDefineBMInfoMIPMapped`) is
  drawn over the first by its alpha (see Luclin player models); `WldMaterial::layer` keeps it.
- A masked texel is drawn at alpha 192 and over (`initializeRenderDeviceState`'s alpha reference), but
  16 and over for the textures of a file whose name has `_chr` or `equip` in it (`t3dLoadWorldFile`
  flags its bitmaps 0x80, and `flushSpecialEffects` uses 0x10 for them): the Luclin skirts' DXT5
  alpha is mostly 96..127. We draw at 128 and over, and load those files' masked textures as alpha
  0 or 255 by 16 (`TextureCache::SetMaskReference`).
- Our mipmaps average a masked texture's color over its opaque texels only. A texture with no opaque
  texel at all (vequip's DXT5 plate, alpha 0 throughout, on an opaque material) averages them all.
- Zone lighting is the `lights.wld` point lights (linear falloff) over a flat ambient, plus the meshes'
  small additive vertex colors. It's an approximation of the real client's lighting.
- TAKP's repacked `sky.s3d` names its filename table with CRC `0xFFFFFFFF` instead of `0x61580AC9`.

## Luclin player models

Each player race has a Luclin model besides its old one, chosen per race and sex
(`render/character_model.cpp`, from `CDisplay::InitWorld` @004a44f4 and the `CDisplay` appearance
functions):

- **Which:** `[Defaults] UseLuclin%sMale` / `%sFemale`, the race as `CDisplay_GetIniRaceName` names
  it (Human, Barbarian, Erudite, WoodElf, HighElf, DarkElf, HalfElf, Dwarf, Troll, Ogre, Halfling,
  Gnome, Iksar, VahShir). `CDisplay::GetNewPCIniFlag` (@004a42a3) takes T, t or 1 as on.
  `CDisplay::ShouldLoadNewPcModel` (@004a4361) loads the Vah Shir by its own key; anyone else only
  without `AllLuclinPcModelsOff` (which InitWorld sets itself under 129 MB of memory) and with the
  expansion flags (`DAT_007cf1f0`/`f4`, on with TAKP), and the Human's model loads for the Erudite's,
  the Wood Elf's for the High, Dark and Half Elf's of that sex: their Luclin models have no tracks of
  their own. So an Erudite's makes the Human of that sex Luclin too.
- **Loading order** (InitWorld): GEquip, GEquip8, GEquip2, GEquip5, GEquip4, GEquip3; for races 1..12,
  128 and 130 and each sex that loads, `global<tag>_chr2.s3d` then `global<tag>_chr.s3d`, and VEquip with
  a Luclin Vah Shir; Global5_chr2 and Global5_chr (the Luclin elementals, races 209..212) with
  `UseLuclinElementals`; LGEquip_amr2, LGEquip_amr, LGEquip2 and LGEquip with any other Luclin model;
  GEquip6 and Global7_chr (the low-poly Vah Shir) unless both Vah Shir keys are on; Global6_chr2,
  Global6_chr, Global4_chr, Global_chr; the `Global<n>_amr` Velious armor only with no other Luclin
  model or `LoadVeliousArmorsWithLuclin`; Global2_chr, Global3_chr. The world keeps the first
  definition of a name, so a loaded `globalhum_chr`'s `HUM_ACTORDEF` is the human, global4's
  barbarians beat global_chr's, and global6's skeleton (`SKE`, unless our `LoadOldSkeletons`) and
  `WOE` do too.
- **Recognized** by a `<tag>TUNIC_POINT_DAG` bone (`initializeHierarchicalSprite` @004d8cab sets the
  flag at `+0x3b0` of the model's animation info).
- **Animations:** tracks come in variants A..H, `<anim><variant><tag><bone>_TRACK`
  (`C01AHUMPEPELVIS_TRACK`). `mapCharacterPrefixToRaceCode` with the flag sends HU and ER to the
  Human of that sex, keeps BA's own, and sends BGM, TRI, ENA and SKE to HUM; the rest borrow as the old
  models do (DA, HI, HA... the Wood Elf's). `initializeHierarchicalSprite` loads each bone's A..H
  (and a plain track as A when there's no H). `PlayAnimationTrack` plays the variant in the player's
  byte at `+0x150`, which `EQPlayer::SetAnimVariation` (@005098d8) sets from what it holds, by
  `CDisplay_GetItemType` (@004aa22c: 0 nothing, 3 a shield IT200..300, 4 IT4, 5 IT61, 2 IT1, 2, 3, 5,
  7, 9, 16, 17... 62, 1 anything else): a secondary with a primary D, a secondary of type 4 H, a
  shield alone C, another secondary F; a primary alone of type 1 E, else B; nothing A. Without that
  variant `toggleAnimationState` tries B (A for B), then A. The Vah Shir male's A tracks of some
  combat moves (`C02A`, `C03A`...) hold the shield point turned round; with a shield it plays B.
  `PlayAnimationTrack2` swaps 50 (`S08`) and 62 (`S20`) on these models, and at the normal speed
  plays their idles (32, 38, 71, 74), sitting and standing (26, 72, 73) and a woman's 17 at half
  speed and socials (`S..`) at three quarters; we don't (TODO).
- **Two textures:** a Luclin material's bitmap is its skin and a `<file>_LAYER` (`HUMCHSK01.DDS`,
  `HUMCH0101.DDS_LAYER`: the armor). `setRenderEffectState` mode 2 binds the layer in stage 1 and
  `t3dRenderSpecialEffects` draws the skin, then the layer modulated by the same vertex color and
  blended by its alpha (SRCALPHA, INVSRCALPHA). The light being the same, we blend the two once
  (`TextureCache::GetLayered`). The low-poly Vah Shir's materials are made that way too (its shirt,
  sash and trousers).
- **Materials:** the face byte is variant x 10 + face (`EQPlayer::GetBaseFaceNbr`), its materials
  `<tag>HE<variant><face><piece>_MDF` (`CDisplay_SwapFace`). Body parts are `<tag><part><material>`
  + pieces as before (the chest three, the hands and feet up to five). A Vah Shir's fur is its face, on
  every part, `<tag><part><material><face><piece>_MDF` (`CDisplay_SwapMaterial`'s `%02d%01d1_MDF`), and
  its tail (`TA`) goes with the legs, else `TA00`; both its models. A robe (10..16) on a Luclin model
  is its chest material, and `CDisplay::HandleMaterialEx` puts it on the arms and wrists too, in the
  chest's color; there's no robed body or `CLK` cloth.
- **Eyes** (`CDisplay_SetEyeMaterial`, `FindDefaultEyeMaterialIndexes`): `<tag>L_EYE_MDF` and
  `R_EYE` (a halfling man's `R_01`) become `CHR_EYE%03d_MDF` (lgequip), the color + 20 for a Dark Elf,
  100 Troll, 80 Ogre, 40 Iksar, 60 Vah Shir. `SwapFace` makes the right eye the left's (1 for 0 or
  0xFF), but 0xD1 for a human man's face 3 or a barbarian man's 6 and 0xCF for a dwarf man's 5 or a
  barbarian man's 4.
- **Armor pieces** (`CDisplay_SetDefaultITAttachments` @004a02e8): item models `IT<n + offset>` on the
  point bones `CDisplay_GetDagPointerByITS` names, tinted in the slot's color (`SetDagSpriteTint`, 0 is
  white). The offset (`CDisplay_GetRaceOffsetForAttachmentITs`): Human 360, Barbarian 540, Erudite 480,
  Wood Elf 240, High Elf 300, Dark Elf 180, Half Elf 60, Dwarf 120, Troll 660, Ogre 720, Halfling 780,
  Gnome 0, Iksar 420, Vah Shir 600, and 30 more for a woman. By material m:
  - head (`HEAD_POINT`): 5000 + m; 3 and 240 are 5028 (an Iksar's or Vah Shir's 5003), 241 is 5029, and a
    barbarian's anything over 2 is 5029. None on an Erudite in a robe (its hood).
  - chest: m 0..3 a tunic, 4010 + m on `TUNIC_POINT` (not a gnome woman's leather), and plate 6000 on
    `CHEST_POINT` too; a robe 4000..4006 on `TUNIC_POINT`.
  - arms (`SHOULDR`, `SHOULDL`) 7000 + 2m and the next; wrists (`GAUNTR`, `GAUNTL`) 8000 + 2m; feet
    (`LEGL`, `LEGR`) 9000 + 2m; legs a belt, 4020 + m on `PELVIS_POINT`. Under a robe, none of these.
  - Many numbers have no model (a human woman's chain tunic); `ChangeDag` then hangs nothing.
- **Hair and beard** (`CDisplay::SetFHEB`, `SetHairOrBeard`, `UpdateAppearance`): IT1000 + style on
  `HAIR_POINT`, IT2000 + style on `BEARD_POINT`, in the hair colors at 00602850 (24, B G R) when the color
  is one the race may have (`EQPlayer::GetAllowedHairColorIndexRange`: High Elf 4..17, Dark Elf 11..19,
  Troll and Ogre 0..23, Gnome 0..22, Iksar and Vah Shir 0, the rest 0..18). The hair goes when a helm
  hangs on `HEAD_POINT` (`SetDefaultITAttachments_DagColor`). A Vah Shir has them only with face 7 (its
  mane, helm or not); the low-poly Vah Shir with face 7 and no helm wears helm 1 (IT661, IT666) instead
  (`SwapFace` -> `SwapHead`).
- **Default hair** (`CDisplay::CreatePlayerActor` -> `EQPlayer::SetDefaultFacialFeaturesByFace(-1, 0, 0)`
  @005204ee): a Luclin model of a `LegalPlayerRace` (0..12, 128, 130) whose first eye color is 0xff (a
  character made with the old models, which leave the Luclin fields 0xff, and most NPCs) has its hair
  and beard reset to none (a Vah Shir with face 7: hair style 0). With `HasAttachedHair` 1 (all but
  the Erudite, Iksar, Vah Shir, and troll and ogre men) it gets hair style 0 in the race's first
  allowed color; a dwarf man also beard 0 in that color. The face stays. With the third argument 1
  (character creation, the face picker) it picks them at random instead.
- **How the pieces move:** each is a hierarchical sprite whose skeleton mirrors the body's (a tunic's
  `NULL` -> `PEPELVIS` -> `CHCHEST`, thighs and `ROBEPOINT` bones; hair's `HEHAIR1`, `HEHAIR2`).
  `CDisplay::UpdateAttachmentAnimations` plays on each of its bones but the root the body's current
  track for the bone of the same name (the body track's first seven characters, `C01AHUM`, + the bone's
  name after the item's tag). We add those bones to the model's skeleton under the point, each playing
  the body bone's frames (`WldSkeleton::Bone::animation_bone`).
- **Bones** (`CDisplay_UpdateDAGPointers`): the head is `<tag>HEHEAD_DAG` and `DagHead` its
  `HEAD_POINT_DAG`; then the `TUNIC`, `HAIR`, `BEARD`, `CHEST`, `GAUNTL`/`R`, `SHOULDL`/`R`, `LEGL`/`R`
  and `PELVIS` points and `CHCHEST1`, `CHCHEST2`.
- **Packets:** `Spawn_Struct` bytes 21..26 are haircolor, beardcolor, eyecolor1, eyecolor2, hairstyle
  and beard, `ServerZoneEntry_Struct` 341..346, and `CharacterSelect_Struct` has them [10] each at
  1560..1610.

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
- Set Face opens `FacePickWindow` (`CFacePick`) with the camera close on the face. Its rows show for
  what the model has (TAKP's `SetDefaultFacialFeaturesByFace__` @0041a1bf, `ShowFacialFeatureUI`):
  the face always; on a Luclin model the eyes, the hair and its color (`HasAttachedHair`), the beard
  and its color (`HasAttachedBeard`) and a Barbarian's woad. The old models show the face alone.
  Each row's buttons step its feature (`CycleThroughFHEB` @0041a8d0) on until the model takes one
  (`CDisplay::SetFHEB`, `SetFHEB_Color`): hair and beard styles are IT1000 + n and IT2000 + n plus the
  race's offset on the hair and beard points, eye colors CHR_EYE<n + the race's>_MDF, hair colors
  `GetAllowedHairColorIndexRange`'s, and an Erudite's hair, an elf's or dwarf woman's beard and the
  woad are the face's variant (its tens). Random is `SetDefaultFacialFeaturesByFace(-1, 1, 1)`;
  Accept keeps the model's features, Cancel puts the new character's back.
- `RandomizeFacialFeatures` (on a race, gender or opening) rolls a face 0..7 and the features on the
  model before it changes race, so they're the old race's; `SetLoadZonePlayerLocation` then gives
  the new model the new character's. `FinalizeNewPCAndSendToWorld` sends a Luclin model's features
  (`CharCreate_Struct` 5422..5427: hair color, beard color, eye colors, hair style, beard) and the
  model's face (5428).
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

Its messages are the confirmation dialog's with OK (category 0x1d): 5 s, and 45 s for the name's
(`HandleNameApprovalResponse`: "Name is not valid." back on the first screen, the wait for approval,
the rejection).

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
  race, gender, face, the nine equipment materials and tints, the hair, beard and eye colors (for a
  Luclin model), and the size the model is scaled to. The
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
  Sitting, `ProcessControls` takes no movement keys or Space and the right button doesn't turn us
  (nor in any stance but standing, 100, and crouching, 111).
  Forward stands us up from sitting, closing the spellbook, and we move off at once while getting
  up (`P02` backwards) plays out. In first person the view follows the head bone down and up.
  `EQPlayer::ChangeStance` (@0050be3c) plays animation 36 (`P05`) kneeling (105), 16 (`D05`, held)
  lying (115, feign death), 33 (`P02`) sitting; to 100 it plays 36 backwards from kneeling, 24
  (`L08`) backwards from crouching, nothing from 102, else 33 backwards.
- **Collision** (`frontend/player_movement.cpp`, `render/zone_scene.cpp`), as `CDisplay::MovePlayer`
  (@00504765) does it: the body (`ModelHeightOffset`, `MHO`, 5/8 of the height, over the feet) is
  lifted by a step height (`MHO * 0.75`, 5.4375 at most; barbarians, trolls and ogres `MHO * 0.5`;
  none ducking, which a standing jump's wind-up is), and the actor's sphere there sweeps from where we
  were to where the velocity takes us (`s3dCollideActorWithWorld` into eqgfx's
  `s3dCollideSphereWithWorld`), then once more along the surface it met with what was left
  (`...AlongUnitDirection`). `CreatePlayerActor` makes a player's sphere `MHO * 0.5` across, times
  1.5 for dwarves and halflings, 0.9 trolls, 0.8 ogres, 2 gnomes (a human: 1.875, its bottom 4.7 over
  the feet). `DoFloorCheck` then drops a sphere `MHO * 0.25` across from half a unit over the lifted
  center (`GetPlayerFloorHeight`), the lift comes off, and `PutPlayerOnFloor` raises the feet onto
  that floor. So anything lower than the sphere's bottom is walked onto in a step, and a swimmer
  whose body is at the surface comes out onto a bank up to about a unit over the water. The walls
  never touch the velocity (`MovementSpeedX/Y`): pressed against a ledge we run in place, and a jump
  or `DoSwimJump` carries us over once the sphere clears it. Ours: up to three slides in a corner
  (eqgame does one), and keeping the feet on the floor walking down slopes and steps.
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
  `CEverQuest::MoveToZone_2`: `OP_ZoneChange` (name, zone, reason 0; 10 going home dead, see Combat)
  and wait for its answer
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
  a halfling (3.5) is 4.7 tall, a human (6) 8. Races 63, and 42 and 120 neuter, are shrunk to 0.45 of
  that, for global6's larger `WOL` and `WOE`. We search a zone's own `_chr` files before the globals
  (eqgame keeps the first loaded, the globals), so Misty Thicket's wolves are `misty_chr`'s old `WOL`: it
  keeps the usual size, and takes no tracks from global6, whose `WOL` has other bones (its root-only
  `L01WOL_TRACK` and the like would leave the old wolf in its base pose). Their names float over them within 120 units,
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

## Keymap

`frontend/key_map.*` and `frontend/options_window.*`, from eqgame's keyboard handling:

- **Commands** are `CEverquest_ExecuteCmd`'s numbers, 1 (`AUTORUN`) to 116 (`PITCHDOWN_CAM`) in the
  Options window (more past them are dev keys). Each has a key and an alternate:
  `instKeypressHandler` @007cd84c and the alternate map @007cdc4c, 256 entries each; a third array
  @007ce04c holds what's held down (zoom, camera moves). A key is DirectInput's code (the scan code,
  0x80 more when extended) plus 0x10000000 Shift, 0x20000000 Ctrl, 0x40000000 Alt; `0xffffffff` is
  none. The ini names are `__BindList` (@00611220, `*NULL*` first).
- **Defaults** (`default_key_bindings` @0055a83b, ours on W A S D, see Custom changes): Space jumps,
  F9 toggles the camera, F1 targets yourself and F2..F6 the group, F7 and F8 the nearest PC and
  NPC, 1..0 the hot buttons, Shift+1..0 their pages, Alt+1..8 the gems, Ctrl+B the book, Ctrl+A,
  C, O and M the Actions pages, Ctrl+S sit, Ctrl+R run, Ctrl+X camp, I inventory, C consider, H
  hail, Tab both Cycle reply and Toggle last two targets (ours Cycle reply and our Cycle nearest
  target, see Custom changes), Alt+letters the windows (Alt+O options,
  Alt+D compass). The chat line's history and cursor take Shift with a numpad key or an arrow, the
  camera moves Alt; Page Up and Down look up and down. A flag (`DAT_008092d8`) adds dev keys past 116.
- **A key down** (`ProcessKeyDown` @00525b04): Escape is `ExecuteCmd(200)`, Enter `ExecuteCmd(0xce)`
  (the chat line), neither bindable. The rest goes to `ProcessMovementKeys` (@005257fa): Move
  right, left, back and forward match the key without Ctrl (`& 0xd00000ff`), Zoom in and out without
  Alt (`& 0xb00000ff`); then every command 0..0x82 with exactly that key or alternate runs, unless
  the key is Cursor left's or right's. While the chat line has the keys, a key that types a
  character (`ConvertScanCodeToChar`, the US table @006253e8: digits, letters, punctuation, Space)
  runs nothing. And `ExecuteCmd` itself runs nothing, down or up, whose own key (the first map's,
  whichever key was pressed) types one while the chat line is open. Ctrl+W runs Move forward and
  Who both. With an edit box other than the chat's focused, only Escape, Delete and the arrows
  (no Shift) get past it (`ProcessKeyboardInput` @0055afe2). The right Shift, Ctrl and Alt count as
  the left ones.
- **A key up** (`ProcessKeyUp` @0052462a): the moves, zooms and camera moves stop when their key
  comes up whatever's held; then every command with that key and the modifiers held now gets
  `ExecuteCmd(n, 0)`.
- **What a command does** is `ExecuteCmd`'s switch. Hot button n is `CHotButtonWnd::DoHotButton(n)`
  on the page showing (whatever's on the cursor), a page `Activate_HotkeyPage`, a gem
  `CCastSpellWnd::HandleSpellLeftClick`, the window commands toggle their windows, Toggle inventory
  waits while a mouse button's down on the world or a transaction is, and the spellbook toggles.
  Autorun (Num Lock) flips `DAT_00798600` on its key down when we can move (not stunned, not
  frozen), and turned on it lets go of Move back; Move forward's or back's key down turns it off,
  as do `EQPlayer::ChangeStance` of ourselves (sitting, ducking, any stance the server gives us)
  and `EnterZone`. `ProcessControls` runs us forward while Move forward's held, autorun's on or
  both mouse buttons are down.
  Of the 111, Gnomebrew does the movement, Autorun, Jump, Look up and down, Toggle camera view, Consider,
  Say hail, Camp, Sit/stand, Run/walk, Target myself, Target party member 1..5, Invite/follow,
  Disband, Who, Target nearest PC and NPC, Toggle last two
  targets, Cycle reply forward and back, Send tell, Reply, Send tell to target, Command mode say
  and emote, the hot buttons and pages, the gems,
  History up and down, the spellbook, inventory, Actions pages and the window toggles it has
  windows for (`implemented` in `frontend/key_map.cpp`).
- **Target nearest PC and NPC** (`ExecuteCmd` 28 and 29, `CDisplay::GetNearestPlayerInView`
  @004ac17f with 500 and 0 or 1): of the actors `s3dGetWorldVisibleActorList` (flags 0x11) gives
  in the camera's view, not us, players (0) or NPCs (1) but not a player's pet, heard from in the
  last 12 s or moving or an NPC standing still, not invisible to us (`CDisplay_IsInvisible`: alive
  and body type over 0x40, or `VisibilityState` 1 unless `CanISeeInvis`, a GM to all but GMs), an
  NPC without a rider: the nearest to the camera within 500 with nothing of the zone's from 1
  over the camera to 2.5 over it (`s3dCollideSphereWithWorld`). For an NPC, one of body type 11,
  60, 65 or 67 below race 2253 leaves the target as it was, and a horse (race 216) gives its rider.
  The target is set locally; `DoPassageOfTime` tells the zone. Ours in `FrontEnd::NearestInView`
  over `ZoneView::SpawnsInView` (the spawns drawn last frame); we don't know riders or whether we
  see invisible.
- **Toggle last two targets** (`ExecuteCmd` 63, not while the chat line has the keys:
  `UI_ChatInputCheck() == 2`): targeting ourselves, back to the target kept in `DAT_00799788` (if
  it's still there); targeting anyone else (or no one), that one is kept and we target ourselves.
  Nothing else sets it, and `EnterZone` clears it, so it swaps between us and one other target.
- **Cycle reply forward and back** (`ExecuteCmd` 60 and 61, the new UI's branch: only while the
  chat line has the keys): when the line starts with eqstr 13974 `/tell`, eqstr 14202 `/rtarget`
  or `;tell`, `CycleNamesInTellList` (@004f8531) steps through `__LastTell`'s 50 names (forward
  to the next filled one or back to the first; back from the first to the last filled), and the
  line becomes `/rtarget <name> `, `/tell <name> ` or `;tell <name> ` for a name with a dot.
  Any other line is left alone; with no tells, eqstr 12364 (12365 for `/rtarget`).
  `AddNameToTellList` (@004f847d) puts a tell's sender first, once, and sets the cycle back to
  it; `/reply` goes to the first.
- **Send tell, Reply, Send tell to target, Command mode say and emote** (`ExecuteCmd` 13, 59, 100,
  87, 88, the new UI's branches; 0x81 and 0x82 are `;` and `/` the same way): only while the chat
  line doesn't have the keys (`__InChatMode`), the active chat window's input box gets `"%s "`
  of eqstr 13974 (`/tell `; a GM with a petitioner checked out gets `/tell <petitioner> `), `'`,
  `:`, or for Reply and Send tell to target the name at Cycle reply's place in `__LastTell`
  (`DAT_007cf0dc`): `/tell <name> ` (`;tell <name> ` for a name with a dot) or eqstr 14202's
  `/rtarget <name> `; with none, eqstr 12364 or 12365. The box then takes the focus. eqgame's
  DirectInput key reaches `CXWndManager::HandleKeyboardMsg` before `ProcessKeyDown` runs the
  command (`ProcessKeyboardInput` @0055afe2), so the key isn't typed into the line it opens; ours
  drops the `WM_CHAR` that follows the key (`m_chat_opened_by_key`).
- **Target myself** (`ExecuteCmd` 22): not stunned or without control (stance 102), nor while
  `DAT_007d0258` or `DAT_007d0254` waits. Targeting anyone else, ourselves; already ourselves,
  the first spawn in `pinstSpawnList` with our `PetOwnerSpawnId` and EQPlayer+0x15c clear (set by
  `UnPackNetPlayer` from Spawn_Struct's `temporaryPet` byte at 126 being 1), else unchanged.
- **The Keyboard page** (`COptionsWnd`): `initkeyboardassignments` (@0042a9d7) names each command
  (eqstr 6500..6697, 3144, 3150, 3153, 5107) and gives its categories (1 Movement, 2 Commands, 4
  Spell casting, 8 Target, 0x10 Camera, 0x20 Chat, 0x40 UI, 0x80 Macros). The combo box picks
  them (`UpdateKeyboardCategoryList`: choice n is bit n, All is 0xff) and the list (its headings
  light blue, `0xff8aa3ff`, as `COptionsWnd::COptionsWnd` sets them) shows those commands in number
  order with `GetKeypressDescription`'s text: `Shift `, `Ctrl `, `Alt `,
  `ShCtl `, `ShAlt `, `CtlAlt `, `ShCtlAlt ` then the key's character in capitals or its name
  (`NPAD 8`, `PGUP`, `F12`), `-------` for none. A click on a Keypress or Alternate cell shows eqstr
  6598 ("Press a key") there (`KeyboardPageWndNotification`); the next key binds it
  (`KeyboardPageHandleKeyboardMsg`): Shift, Ctrl and Alt alone are waited past, Escape unbinds, and
  Enter, `/` and the Windows keys can't be bound (`CEverQuest_IsValidKeyMap`: eqstr 12092
  in chat color 15, the key stays). The key goes ahead of everything else. Clicking another cell,
  changing page or closing the window puts the old text back. Reload defaults asks eqstr 6602 for
  20 s (`CConfirmationDialog::Activate(0x1c, 20000)`), then `ResetKeysToDefault` takes every
  command's keys out of the ini.
- **The ini** (`_writeKeyMapToIni` @0052505a, `readKeyMapFromIni` @00525520): `[KeyMaps]`
  `KEYMAP_<name>` and `KEYMAP_<name>_2`, the value `SHIFT+`, `CONTROL+`, `ALT+` in that order, then
  the key's lowercase character or its name (`NUMPAD8`, `LCONTROL`, `SPACE`, `PGUP`), `NOKEY` for
  none. Reading takes the prefixes in any order, a single character only from the 47 the table
  @00611420 has, and anything it doesn't know as no key; a missing key keeps the default.
- **The Colors page** (`COptionsWnd`, `frontend/options_window.*`): UserColor0..71 (the page's
  `OCP_UserColor<n>`) each draw their label in the text color they stand for, black as white
  (`COptionsWnd_GetUserDefinedColor` @0042a953 into the button's text color, +0x68), set when the
  window's made and each time the tab is picked (`ProcessUserDefinedColorTab`), not when a color
  changes: a button keeps its old color until the tab's picked again. A click opens the color
  picker with the button's color (`ColorPageWndNotification` @0042c85b), and its Accept
  (notification 0x1e) is `SaveColors` (@0042c92b): `CDisplay_SetUserDefinedColor` and `[TextColors]`
  `User_<n+1>_Red`, `_Green`, `_Blue` (eqclient.ini in eqgame, my-client.ini here). Lines already in
  the chat keep their color. Reset Default Colors asks eqstr 6602 for 20 s (`CConfirmationDialog`
  0x21), then `RestoreDefaultColors` (@0042ca2a) saves each `GetDefaultUserDefinedColor`. The page
  scrolls (Style_VScroll): a scrolling Page moves its pieces as a scrolling Screen does.
- **The Display page** (`COptionsWnd`'s `OptionsDisplayPage`, `frontend/options_window.*`):
  `SyncDisplayPage` (@0042a5e5) sets it when the window's made and, from
  `DisplayPageOnProcessFrame`, every frame the page is up; `DisplayPageWndNotification` (@0042bba3)
  takes a click, a pick or a slider's move, then `saveOptions`. The value labels change only when
  their slider does (`"%d %c"` or `"%.1f"`). Kept in my-client.ini's `[Defaults]` (eqgame:
  eqOptions1.opt, eqOptions2.opt, eqclient.ini) and, for the windows, the layout's `[Main]`.
  - Show PC Names (`__PCNames`, `TurnCharacterNameSpritesOn`/`Off`) and Show NPC Names
    (`DAT_00798af8`; TAKP's names for the two NPC functions are swapped): `SetNameSpriteState` shows
    a name only when its kind's switch is on, players' (ours too) by the first, NPCs' and corpses'
    by the second. Both on by default.
  - Level of Detail (`DAT_00798ae8`, on): `setLOD` says 6650 or 6651 and calls eqgfx's
    `s3dSetDynamicLOD(on, 1, 100)`.
  - Video Modes: `CVideoModesWnd::Activate`.
  - Sky (`DAT_00798ae0`, 1 by default): 0 sets `CDisplay`+0x12, the sky type, to 0 (no 3D sky; also at
    `SetFog`), 1 the zone's with `t3dSetSkyDualLayer(0)`, 2 with `(1)`: `t3dPaintSky` draws the
    LAYERn1 dome always and the LAYERn3 cloud layer only with the dual layer.
  - Other Particles density (`DAT_00798ae4`, 3): `SetPCloudDensity`'s 0, 0.33, 0.66 or 1 into
    eqgfx's multiplier, which `t3dCreateParticleCloud` gives each cloud as it's made (the
    weather's, the old spell effects', models'); `...AndRecalc` redoes the zone's own cloud actors.
  - Spell Particles: Near Clip Plane (`_DAT_007f6f74`, 2: Near 2, Medium 4, Far 6, Farthest 8;
    `RealRender_World` gives it to `t3dParticleSystemSetNearClipPlane` every frame, and
    `renderParticleSystem` leaves out particles nearer than it, less their depth bias), Density
    (`_DAT_007f6f6c`, Off 0, Low 0.25, Medium 0.5, High 1) and Opacity (`_DAT_007f6f70`, 101 ticks of
    hundredths), eqclient.ini's `SpellParticle*` (1, 1, 2 there).
  - Gamma (`_DAT_00798acc`, 8 ticks: the gamma - 1; eqclient.ini's `Gamma`, 3.0 without one):
    `t3dSetGammaLevel` sets a ramp only full screen with the device's gamma caps, which the windowed
    TAKP client never is, so it does nothing.
  - Far Clip Plane (`DAT_00798b28`, 21 ticks): `_DAT_00799660` = value * 0.2, at least 0.1, which
    `SetFog` moves the fog's end by (see Far Clip Plane above for ours).
  - Fade Delay (21 ticks, value * 500 ms), Fade Duration (20, value * 500 + 500), Window Transparency
    (230, alpha value + 26) and Fade-to Transparency (256, the alpha): `CXWndManager`'s
    `SetGlobalFadeDelay`, `SetGlobalFadeDuration`, `SetGlobalAlphaForAllWindows` and
    `SetGlobalFadeAlpha` (that one also turns fading on and refades), each only when it differs
    from the UI ini's `[Main]` `GlobalFadeDelay` (2000), `GlobalFadeDuration` (500), `GlobalAlpha`
    (255), `GlobalFadeToAlpha` (128), put on every window and kept there. `SetBagOptions` writes the
    bag windows' sections too (ours are all built, so they get it as the rest do).
- **The General page** (`COptionsWnd`'s `OptionsGeneralPage`, `frontend/options_window.*`):
  `SyncGeneralPage` (@0042a4b2) sets it when the window's made and when the tab's picked
  (`ProcessGeneralOptionsTab`); `GeneralPageWndNotification` (@0042b532) takes a click (1), a
  combo's pick or a slider's move (0xd), then `saveOptions`.
  - Fast Item Destroy: `DAT_00798b1c` (1 checked); a click says 12032 "Fast Item Destroy is ON." or
    12031 in color 15.
  - Receive Guild Invites: `DAT_00798adc` (1 checked, by default); `msg_invite_guild` reads it.
  - Autosplit: `IsAutoSplitEnabled` (the profile's byte at 3348); a click says 12036 or 12037 in
    0x111 and `CEverQuest_ToggleAutoSplit` (@005376af) sends `OP_SpawnAppearance` 28 (0x1c) with it.
  - Anonymous and Roleplaying: one state, `DAT_00798ad0` (our spawn's `AnonymousState`): 1
    anonymous, 2 roleplaying, 3 both. Anonymous checked at 1 or 3, Roleplaying at 2 or 3; a click
    adds or takes 1 (Anonymous) or 2 (Roleplaying), says 12034/12035 or 13233/13234 in 0x111, sends
    `OP_SpawnAppearance` 21 with the state, and `SavePC`; Roleplaying also retints our name
    (`CDisplay_SetNameSpriteTint`). Unlike /anonymous, the page doesn't stop Anonymous while
    roleplaying. `/roleplay` (@004fc433) on the same state: "off" (3 letters) sets 0 from 2 and 1
    from anything else (0 included: /roleplay off makes you anonymous), "on" (2) sets 2 from 0 and
    3 from anything else (2 included), anything else steps 0 -> 2, 1 -> 3, 2 -> 0, 3 -> 1; it says
    13233 or 13234 (by the state under 2) and sends it as the page does.
  - `/autosplit` (@004fb3cd): "off" (3), "on" (2) or toggled, says 13197 or 13198, and
    `CEverQuest_ToggleAutoSplit`. Coins from a corpse say 12071 "You receive %1 as your split." with
    it on, 12072 off (`EQ_Main::DoLoot`).
  - Allow trading with: `DAT_00798ad8`, 2 everyone (by default), 1 party members only, 0 no one, on
    the combo box's choices the other way round. `msg_req_trade` (@004e7231) turns a request down
    with `OP_TradeRefused` reason 'b' at 0 and 'c' at 1 unless the trader's in our group, then says
    1447.
  - Fast Drop: `/fastdrop`'s setting (`DAT_0079966c`): -1 never is the third choice, 1 the second
    (without confirmation), anything else the first. A pick runs `_fastdrop` with eqstr 12946 "off",
    12947 "on" or 12989 "never", which says so.
  - Load UI Skin: `CLoadskinWnd::Activate` (not ours yet).
  - The sliders, 21 ticks (`SetNumTicks(0x15)`): Sound Realism (`DAT_00798b04`,
    `EqSoundManager::SetEffectsLevel`), Music Volume (`DAT_00798afc`) and Sound Volume
    (`DAT_00798b00`), each at value * 100 / 20 hundredths, and the value label beside each "%d %c"
    (the same percentage, a space and '%'). `CSliderWnd::SetValue` notifies only when the value changes, so
    a label keeps the XML's "0 %" until its slider first moves off 0.
- **The Filters page** (`COptionsWnd`'s `OptionsChatPage`, `frontend/options_window.*`): `Chat0`..`Chat9`
  are guild chat, socials, group chat, shouts, auctions, OOC, my misses, others' misses, others'
  hits and attacker missing me (`ChatArray`, DAT_00798bf4..c18), checked when shown; `DamageShields`
  and `NPCSpells` are checked when their switch is 0 (DAT_00799670, DAT_00799658, 1 hidden); the
  `PCSpellsComboBox` (all, off, group), `BardSongsComboBox` (all, me, group, off),
  `CriticalSpellsComboBox` and `CriticalMeleeComboBox` (all, me, off) show their switch as the
  choice. `SyncChatPage` (@0042a897) sets them when the window's made and when the tab's picked
  (`ProcessChatOptionsTab`), so a `/filter` while the page is up shows on the next pick.
  `ChatPageWndNotification` (@0042c747) takes a click's check or a combo's choice (in range) into
  the switch, then `CEverQuest_send_update_filters`, `saveOptions` and `saveOptions2` (eqOptions1.opt
  and eqOptions2.opt in eqgame; my-client.ini's `[Filters]` here). The constructor also gives the
  four combo boxes `CComboWnd::SetColors(0xff808080, ...)`, which we don't do yet.
- **Combo boxes** (`CComboWnd`): the choice in white (grey disabled) down the middle of the client
  area left of the button, which is as wide as its `Button` template's Normal and drawn Pressed
  while the list is down. The list hangs from the box's bottom, as wide as the box left of the
  button and half a line plus a line a choice tall, `ListHeight` at most (`GetListRect`,
  `InsertChoice`). A press on the box drops it; a press on a choice picks it.

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
  and blinks their last ticks. With Alt down and the mouse over it, `CBuffWindow::PostDraw`
  (@004095fe) draws every button's tooltip at the button's right less 5, less its column's number
  times the button's width (plus 1) over the columns, and at its top plus 15 times the column's
  number mod 3. Buffs count in the stats, resists, AC, max mana and speed (SoW,
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

## Combat

- **Auto attack** (`FrontEnd::ToggleAutoAttack`, `SetAutoAttack`): eqgame's `__Attack`
  (`_EverQuestinfo_SetAutoAttack` @005493b5, which also turns the combat music, track 2, on or off
  when `/combatmusic` is on). `CEverquest_ExecuteCmd` 8 toggles it: the Autoattack key, the Actions
  window's Attack button (`CActionsWnd::CombatPageWndNotification`) and its hot button. With the
  book open and auto attack off it says eqstr 9027; with a merchant or bank up it does nothing;
  standing (100) or already attacking it toggles and says 12370 "Auto attack is on." or 12371 "Auto
  attack is off."; otherwise 12368 (stance 102) or 12369 "You must be standing to attack!", all in
  color 13. `/attack` (@004fd3da) takes `off` (3 letters), `on` (2) or nothing (toggle) and says
  nothing. `EQ_Character::DoPassageOfTime` runs ExecuteCmd 8 whenever we're attacking and not
  standing, so sitting, the book sitting us down and a standing jump's crouch all turn it off with
  12371, and sends `OP_AutoAttack2` (0x4161) then `OP_AutoAttack` (0x4151), `{u32 on}` each, as it
  changes. It goes off silently entering a zone, when our target dies (ReportDeath) or despawns
  (HandleWorldMessage), and when we die (`ReportDeath_ClearOutRentables`); with eqstr 1466 "Auto
  attack off." asking a merchant for its shop or a guildmaster to train, or opening a bank (`RightClickedOnPlayer`,
  `CBankWnd::Activate`). The assist's answer turns it on with AttackOnAssist (the TAKP exe reads
  Zeal's `_Zeal_attack_on_assist` there). While it's on, a trade asked of us is busy
  (`msg_req_trade`), and the player window draws a red one-pixel lasso round its client area every
  other frame (`CPlayerWnd::UpdatePlayerWindowState`, `DrawSelectionLasso`).
- **Melee is the zone's:** the client says auto attack's on, and swings for itself only to check
  and tell (below). EQMacEmu swings on its own
  attack timers (`Client::Process`: in `CombatRange`, in line of sight and facing the target),
  sends the attacker's swing as `OP_Animation` to everyone near, the attacker too (`Mob::DoAnim`),
  and each hit or miss as `OP_Damage` (`Damage_Struct`: target, source, type, spell, damage,
  force, heading, pitch). The type is `SkillDamageTypes[skill]`: 0 blunt, 1 slashing, 4 hand to
  hand, 7 archery, 8 backstab, 10 bash, 21 dragon punch, 23 eagle strike and tiger claw, 30 the
  kicks and intimidation, 36 piercing, 51 throwing; 231 a spell. Our own swing animation comes
  back to us and plays on our model with its sound (`CDisplay_PlayAnimationSound`).
- **Our own swings** (`frontend/melee_attack.cpp`, `CEverQuest::ProcessControls` @0053f337,
  `EQPlayer::DoAttack` @0050a0f8): with auto attack on and control (standing or crouching, not
  stunned, not stance 102), ProcessControls takes `__RangeAttackReady` for `DoAttack(13)` and
  `DAT_007cd845` for `DoAttack(14)`. DoPassageOfTime readies the secondary hand when its weapon's
  delay (hand to hand's reuse with the hand empty; the primary's delay with anything there but a
  weapon) has passed since ActorInfo +0xd0; a ready swing that wasn't taken (no control) restarts
  both hands' timers. Every DoAttack first takes off the invisibility buffs (SPA 12, 28, 29, 94) and
  ends hiding (`DAT_00798780`; eqstr 12682 in 0x111). For 13 and 14: from shooting, the weapons go
  back in hand; the hand's item must be a weapon (else nothing: a shield doesn't swing; empty is
  the hand to hand dummy, type 22 skill 28); its delay (Delay * 100, or the skill's reuse for type
  22: a monk past 26 takes 100 ms off per 4 levels over 24, an Iksar per 6, 1 s at most, over 60
  2400, 2200, 2000 (an Iksar's 100 more), 1600 over 50 with item 10652 on the hands; a beastlord
  100 per 5 levels over 24) hastened must have passed since the hand's timer, which restarts;
  then standing (12368 at 102, else 12369, in red), `CanIHitTarget` (its messages, so a target out
  of reach says 124 each hand's swing), and the fatigue: hand to hand or a weapon under 2.6 lb one
  point a third of the time, under 5.1 half the time, under 10.1 always, heavier 2
  (`EQ_PC::SetFatigue`, 0..100; shooting too). What follows (`DoMissileHit`'s to-hit and damage,
  `CheckArcherySuccess` and `CheckThrowingSkill`, which are the double attack and dual wield rolls
  despite their names) sends nothing for 13 and 14. EQMacEmu says 124 itself too, for its primary
  swing out of range.
- **Hits** (`frontend/combat.*`, `msg_successful_hit` @004e0907 ->
  `CEverQuest_ReportSuccessfulHit` @005297d2): someone else's hit on someone else is told within 75
  units of the victim. A hit is "%1 %2 %3 %4" (12162): the attacker ("You", 12185, when it's us),
  the verb, the victim ("YOU" 12187 on us, "yourself" 12186, someone's own "himself", "herself" or
  "itself" by gender) and "for %1 points of damage." (12160; 12161 for one). A miss (damage under
  1) is "%1 try to %2 %3, but %4!" (12158; 12159 "tries" for anyone else), %4 by the damage: -1
  block, -2 parry, -3 riposte, -4 dodge (12144..12151, "%1 blocks" and so on said of the victim),
  -5 INVULNERABLE, -6 magical skin (12152..12155), else "miss" or "misses". The verb is singular
  for us and for misses: by type, 0 and 2 crush, 1 and 3 slash, 7 and 51 hit, 8 backstab, 10 bash,
  21, 23 and 52 strike, 26, 30, 38 and 71 kick, 36 pierce; anything else, hand to hand included, is
  the attacker's race's (`CEverQuest_SetDefaultDamageDescByRace` @0052948c: maul, bite, claw,
  gore, sting, slice, smash, rend, slash, else punch for the player races and hit for the rest;
  `Combat::RaceVerb` has the table). Colors: `0x109` our hits, `0x10a` hits on us, `0x117`
  others', `0x10b` our misses, `0x10c` misses of us, `0x118` others'. The line's first letter is
  made upper case; an NPC's hit on itself isn't told. The new UI prints them through the hit modes
  (`HandleMyHitsMode` and the rest), whose 0 is the whole line.
- **/split** (`CEverQuest_DoSplit` @00537476): only in a group (else 12328) and not while the
  zone's answer to something is waited on; four numbers (`GrabFirstWord` three times, `atol` the
  rest), all under 1 the usage (12329..12331), each no more than we have (12332..12335); then
  `OP_Split` (0x4131, {i32 platinum, gold, silver, copper}) with them taken off ours.
- **/yell** (@004f82ea): "You yell for help." (13087) in red, then `OP_YellForHelp` (0x41da, no
  body). The zone sends {u32 the yeller} to everyone within 100 but them, and `msg_yell`
  (@004e17cc) says where from by `get_bearing` to them against our heading (`CDisplay_HeadingDiff`,
  0..256): 0..32 straight ahead (12907), past 224 behind (12908), else ahead (to 96), off (to 160)
  or behind (to 224) to the right (12909..12911) or the left (12912..12914), in color 0x11a.
- **/consent** (@004fb39e): the rest of the line and its NUL as `OP_Consent` (0x40b7).
  `OP_ConsentResponse` (0x40d5, {grantee[64], owner[64], u8 permission, zone[32]}):
  `msg_consent_verify` (@004e3314) says to the owner 1427 or 1428 with the grantee, to anyone else
  2080 or 2103 with the owner (not when the owner's ignored).
- **OP_ManaUpdate** (0x4219, {u16 spawn, u16 mana}) sets our mana, whoever it names. **OP_DespawnDoor**
  (0x409b, no body) deletes every door and switch. **OP_PlaySound** (0x4048, {u16 spawn, u16 sound})
  plays the wave where the spawn is, full to 100 and silent past it.
- **What the client tells the zone of its own** (`ZoneSession::SetRunMode`, `SavePC`,
  `ReportEnvDamage`, `SafeFallSuccess`, `TargetGroupBuff`):
  - `OP_SetRunMode` (0x401f, {u8 mode, 3}): `DoPassageOfTime` takes `__RunWalkState` the first time
    without a word, then sends it each time we go from walking to running or back. EQMacEmu tires a
    runner (fatigue) by it, and starts each of us running.
  - `OP_Save` (0x402e): `CEverQuest::SavePC` (@00535eca) every 181 six-second ticks
    (`DoPassageOfTime`'s `DAT_007cd847`), closing the bank, merchant (before `OP_ShopEnd`) and loot
    windows, a finished trade, /anonymous, /roleplay and the options' Anonymous and Roleplaying,
    not dead or under 1 HP, and not while the zone's answer to something is waited on (then the
    next tick). eqgame sends its 0x210c-byte profile; ours is the one the zone sent. EQMacEmu reads
    none of it and saves the character.
  - A fall: `CDisplay::CheckForUnderFloor` calls `EQPlayer::IHaveFallen` at 2.5 down or faster with
    the speed past 2.5; `EQ_Character::TakeFallDamage` (@004bfe16) adds a hundredth of what we carry
    (`cur_encumbrance`) and takes 1.5 off: 10 damage a unit of that, 20000 from 9. With safe fall
    (skill 39) it takes the skill off (plus Acrobatics' 10..30%) and sends `OP_SafeFallSuccess`
    (0x41ab, no body; the zone's skill up). Nothing on a mount under 200 or in the Bazaar (zone
    151). `ApplyDamage` (@004c0150) then sends `OP_EnvDamage` (0x401e: its own 0x24-byte struct, the
    type 0xfc at +0x18 and spell -1 at +0x1e; EQMacEmu ignores it) and `OP_Damage` (0x4058, the
    first 0x18 bytes: us from no one, type 252), which EQMacEmu takes the HP for and sends back,
    and `ReportSuccessfulHit` says it here ("YOU were injured by falling.", with `fall_hit`).
    `IHaveFallen`'s sound is `fall_hit` when it did damage, else `jumpland`.
  - `OP_TGB` (0x4220, u32): /targetgroupbuff and /tgb "off" (3 letters) or "on" (2) keep
    `[Defaults] TargetGroupBuff` (eqclient.ini in eqgame) and send 0 or 1; either way then 2, which
    asks the zone to say which it is (`TGB_ON`/`TGB_OFF`). `DoMainLoop` sends the kept one as each
    zone starts.
- **The environment's damage** (types 244..253): 244 rotting (5722, 5723), 245 chilled, 246
  freezing, 247 tormented, 248 burned, 249 thorns, 250 burning (12131..12142), 251 drowning
  (12128, 12130; suffocating, 12127 and 12129, in zone 171, `DAT_007f9488` set from the profile),
  252 falling (12118, 12119, with `fall_hit`), 253 a trap, by the packet's spell field: 140 and
  145..152 crushed (with `fall_hit`), 115 and 116 a saw blade, 120 and 125 a spear, 130 a pendulum,
  135 a blade, else 12126 (12120 for anyone else). Said only with damage, `0x10a` on us, `0x117`
  else.
- **What a hit does:** someone hitting us while we've no target becomes it (body type under 65;
  for a spell, a detrimental one). A hit with damage on anyone not a corpse plays the victim's
  animation (13, `D02`; 12 for 244..249, 15 burning and drowning, 14 falls and traps) and one of its
  race's four get-hit sounds (`SetSounds`' GetHit1..4). A melee hit on a race whose first get-hit
  wave is 32..89 also plays `FlshHit2` when the victim's hit points (ours, or its level x 30) are
  under ten times the damage, else `FlshHit1`. A block, parry or riposte plays
  `CEverQuest_GetSoundEffectForRiposte`: `SwordBlk` when both primaries are blades, `ShldBlk` half
  the time from a shield in the victim's secondary, else `StaffBlk`; magical skin `StaffBlk`. A hit
  on us closes the book and stands us up from sitting or kneeling, and someone else's melee takes
  its damage off our hit points at once (`ModifyCurHP`) until the zone's `OP_HPUpdate`.
- **Deaths** (`CEverQuest::ReportDeath` @00528e16, `OP_Death`: `Death_Struct` {spawn, killer,
  corpse, level, spell @8, attack skill @10, damage @12}): the trade, merchant or bank of the one who
  died closes, our target clears (with auto attack), then the killing blow is told as a hit (within
  50 units across the ground unless we're in it) and the death: on us 12107 "You have been slain by
  %1!", 12108 the unseen hand, 12106 "You died.", 12103..12105 falling (skill -4), choking (-5),
  burning (-6), in `0x115`; anyone else 12113 "You have slain %1!", 12114 "%1 has been slain by
  %2!", 12112 "%1 died.", 12115, 12109..12111, in `0x116` and told as the blow is (an NPC killing
  itself says nothing).
- **What a hit does to us** (`EQPlayer::IWasHit` @0050b408): a melee hit with damage takes off our
  invisibility (the first buffs with SPA 12, 28 and 29, `RemovePCAffect`) and pushes us: the
  hit's `force` (EQMacEmu: 0.05 to 2.5 by the attacker's size, 0 for none) along its `heading`
  (512ths; the X push by its sine, Y by its cosine) and up by its `pitch`'s sine, onto our motion.
- **Our death** (`EQPlayer::IDied` @0050b637, `ReportDeath_ClearOutRentables`): auto attack off, we
  fall (`D05`, held), the death music plays (track 8), and nothing moves us (stance 120:
  `ProcessControls` takes the movement keys only standing or crouching) until the zone moves us
  on.
- **Going home** (`ActorInfo`'s `DeathTimer`, `frontend/frontend.cpp` `UpdateDeath`): `IDied` leaves
  ours 0 and the next `CDisplay::MovePlayer` of us as a corpse starts it at the game time. More
  than 2 s on, `CDisplay::RealRender_World` prints eqstr 539 "Returning to home point, please
  wait..." in color 13 and sets it to 1; the frame after, `CEverQuest::PrepareLocalPCForRepop`
  (the options saved, `EQ_PC::PrepareForRepop`: the bind point as the zone entry, buffs, stats and
  memorized spells cleared, coins from level 10) and `MoveToZone(ZoneBoundId,
  "repop_to_home_at_death", 1, 10)`: `OP_ZoneChange` to the profile's `bind_point_zone[0]` (@3784)
  with reason 10, then zoning as through a zone line. EQMacEmu's `Client::GoToDeath` has the bind
  transfer waiting for that request (and drops a dead client that never sends it). The next zone's
  profile gives back what `PrepareForRepop` cleared, so we don't clear it ourselves. Checked with
  the sewer rat recording below through `frontend_preview`: the message 2 s after the death, then
  `OP_ZoneChange 1`.
- **Filters** (`frontend/filters.*`, `/filter` @004fbc3a): `badword`, `guildsay`, `social`, `gsay`,
  `shout`, `auction`, `ooc`, `mymisses`, `othersmisses`, `othershits` and `atkmissesme` toggle;
  `npcspells` and `dmgshield` toggle the other way (0 shown); `pcspells` steps all, off, group;
  `bardsongs` all, me, group, off; `critspells` and `critmelee` all, me, off. Each says "%1 *ON*"
  (eqstr 1351), `*OFF*` (1348), `*ALL*` (1347), `*GRP*` (1349) or `*ME*` (1350) with its label in
  color `0x111`; anything else prints the usage (1354..1360). `/serverfilter` (@004fecba) takes
  `off` or `on` (its texts are the exe's). `CEverQuest_send_update_filters` sends them all as
  `OP_SetServerFilter` (0x41ff, `UpdateFilters_Struct`: damage shields, NPC spells, PC spells, bard
  songs, the server filter, the six channels, my misses, others' misses, others' hits, misses of
  me, critical spells, critical melee), as `msg_stat_change` gets our spawn id and at every change;
  EQMacEmu stops sending what's filtered. The client side reads the four melee switches and
  damage shields (`ReportSuccessfulHit`).
- Checked against a live server: a level 1 warrior fighting a level 3 sewer rat in South Qeynos
  (`eqclient --attack a_sewer_rat --record`), then the recording through `frontend_preview`: "You
  try to punch a sewer rat, but miss!", "A sewer rat bites YOU for 8 points of damage.", "You punch
  a sewer rat for 1 point of damage.", "You have been slain by a sewer rat!".

## Skills and abilities

- **The skill table** (`frontend/skills.*`, `InitializeSkillData` @004d0e88's `__SkillDict`): each
  skill's name (eqstr), reuse time, whether it's used from a button (+9), its type (+10: 0 passive,
  1 an ability, 2 a combat skill on the combat timer), DoAttack's combat field (+0x8c), and each
  class's level to get it (+0xd) and cap (+0x4e). Taken from the exe by compiling the decompiled
  function and running it for skills 0..73. The combat skills: backstab 8 (10 s), bash 10 (8 s),
  dragon punch 21 (6.2 s), eagle strike 23 (6 s), flying kick 26 (8.5 s), kick 30 (8 s), round
  kick 38 (8.5 s), tiger claw 52 (5.5 s).
- **HasSkill** (`EQ_Character::HasSkill` @004b9f08): a value of 254 or 255, or a class cap of 0, is
  the race's own (`GetInherentRacialSkillLevel`: wood elves and Iksar forage, dark elves hide,
  dwarves sense heading, halflings and Vah Shir sneak, gnomes tinker, Vah Shir safe fall, all 50;
  Iksar swim 100); at the class's level or over, our value (1 for one at 0); else 0.
- **The buttons** (`frontend/abilities.*`, `CActionsWnd`): the abilities page's six (`DAT_007d026c`)
  and the combat page's four skill buttons (`__DoAbilityList`), kept as `[Abilities] Ability1..6`
  and `[CombatSkills] CombatSkill1..4` in `<character>_<server>.ini` (saveCustom,
  loadCustomFromINI); loadCustom drops a skill we don't have and the hot buttons that held it. A
  button shows its skill's name (`UpdateButtonText`) and is drawn down while it isn't ready
  (`UpdateAbilityButtonStates`). The right button opens `SkillsSelectWindow`
  (`SKSW_SkillSelectorList`), listing the skills we have that are used from a button: on the combat
  page those that aren't passive (`SetTypesToDisplay(2)`), on the abilities page all of them (3); a
  click puts one on the button. A left click, the hot button, or `/doability 1..10` (1..6 the
  abilities page's, 7..10 the combat page's; else eqstr 13256, 13257) uses it.
- **Timers** (`DoPassageOfTime`): an ability is ready (`__DoAbilityAvailable`) once its reuse time
  has gone by since its last use (`DAT_007cd650`); mend at its own time (`DAT_007cd6d0`). The combat
  skills share one timer (ActorInfo +0x164: the last one's reuse from its use), `__AltTimerReady`.
  `EQPlayer::ModifyAttackSpeed` (@0050a039) hastens a delay: `delay / (haste * 0.01)` with haste
  `TotalSpellAffects(11, 1)` (@004c6b6d, `Buffs::AttackSpeed`): over the buffs, the worst slow (a
  value 1..99), the best haste (100 and up), the best overhaste (SPA 98) and bard haste (SPA 119);
  over the worn slots 1..21, items not stackable (Stackable over 1) with a worn effect (EffectType1
  2) of a spell with a duration, at EffectLevel1: the worst slow and the best haste (110 at most
  under level 26). A slow wins (the worse of the two); else the spell and item haste are added,
  less 100 when both are 100 and up, plus overhaste's part over 100 (110 at most, over level 49),
  plus the bard haste (its part over 100 if over; 10 at most under level 51, else 25), and capped
  at level + 125 + the bard haste (185 + it over level 50, 200 + it over 59); bard haste alone is
  100 + it. EQMacEmu restarts its own combat ability timer at `reuse * 100 / GetHaste() - 1` whole
  seconds (kick and bash 8, backstab 10, round and flying kick 8, eagle strike and dragon punch 6,
  tiger claw 7), so a tiger claw at eqgame's 5.5 s comes too early for it and is dropped; a bow's also drops `delay * weight reduction * 0.0025 + 1` with a
  quiver in a pack slot, when that leaves over 1000.
- **UseSkill** (@004bdf2f): nothing at 0 HP or for a skill at 254 and up; stance 102 says 12368. A
  combat skill turns `__AltTimerReady` off; bash needs a shield in the secondary hand (or primary
  11050, 10099, 14383; trolls, ogres and barbarians bash with anything), else 12394. Then
  `EQPlayer::DoAttack(100, skill, target)`: not while casting, our invisibility goes (SPA 12, 28,
  29, 94), the skill must be a combat one we have, a backstab needs a piercer (12874), the combat
  timer must be up, and then it restarts with this skill's reuse even if what follows fails:
  standing (12368, 12369), `CanIHitTarget`, and `DoMissileHit` sends `OP_CombatAbility`
  `{u16 target, u16, u32 100, u32 skill}`. The zone swings it (`OPCombatAbility`; checked live: a
  kick's OP_Damage of type 30 and the kick animation, 1). Anything else must be ready, and its
  timer starts: forage (`OP_Forage`, standing and not attacking, else 148 or 149 and the timer
  back), mend (`OP_Mend`), disarm traps (`OP_DisarmTraps`), sense traps (`OP_SenseTraps`), sense
  heading (a 1..100 roll, under 8 as 0, against the skill, over 95 failing: 12426; else the compass
  true for 5 s, "You think you are heading %1." with North..NorthWest by 64ths of our heading, and
  `OP_SenseHeading`), intimidation (`OP_InstillDoubt`; 12398, 12691, 12400), taunt (an NPC within
  melee range, not races 141, 72, 73: 12397 and `OP_Taunt {target}`; 12395, 386, 12396) and disarm
  (within 15, `CanIHitTarget`, VisibilityState to 0 with the invisibility buffs off,
  `OP_Disarm {us, target, skill, 0xff}`; 12416..12419). On a horse (ActorInfo's Mount, or the
  SPA 113 buff) only tracking, bash, disarm, 30, mend, sense heading, fishing, intimidation, taunt
  and the AA abilities 105 and 107 go (else 12393). Sneak and hide skip the timer to stop; begging
  and pick pockets start each other's timer; hide's runs to `DAT_007cd6c4` (its reuse; an AA
  takes 1..3 s off) and isn't ready while hidden; sneak isn't ready while sneaking. The messages
  are color 0x10e unless said:
  - **Sneak** (42): sneaking, `IsSneaking` 0 and `OP_SpawnAppearance {us, 15, 0}`, 12338; else not
    attacking (12420) nor in water (12421), `OP_Sneak` and 12422. The zone rolls and answers with
    SpawnAppearance 15 (`msg_stat_change` sets IsSneaking); EQMacEmu tells a rogue SNEAK_SUCCESS or
    _FAIL. Sneaking, forward is `EQPlayer::InitSneakMod`'s modifier (0.5, a rogue's 0.1 more per
    level over 50, 10 at most) times the walk speed, back and beside half the walk speed.
  - **Hide** (29): hiding (`DAT_007989e2` 29) stops it (12415); attacking 12423; else we must stand
    still, each of MovementSpeedY, X and Z within 0.001 (a two-thousandth per skill point over 50,
    0.5 at most; `AltAdvManager_CheckHidingSkillBonus` lets it go anyway): hidden, our position
    kept as whole numbers, `OP_Hide`, 12425, `UpdateMyVisibleStatus`; else 12424. DoPassageOfTime
    unhides us (12682) when the whole numbers change, unless we're a rogue sneaking.
  - **`UpdateMyVisibleStatus`** (@004c0b2b): VisibilityState 1 hidden or with SPA 12, else 3 with
    SPA 28, 2 with 29, else 0; when it changes the buffs that don't match go and
    `OP_SpawnAppearance {us, 3, state}` is sent. `MakeMeVisible` (@004c0a0f) sets 0, takes all
    three off, tells the zone and calls `CEverQuest_CancelSneakHide` (12337 unhidden, 12338 with
    IsSneaking cleared, neither told). `EQPlayer::IWasHit` clears VisibilityState and IsSneaking
    without telling anyone.
  - **Feign death** (25): `ChangeStance(115)` and `OP_FeignDeath {us, DAT_007985ee, 0}` (6 bytes);
    the zone decides whether it took and tells those near a failure.
  - **Bind wound** (9): a player (us too) as the target (144), bandages (item type 18, found by
    `FindItemBySkillId` in slots 0..29 and their bags; 146), within 15 (420 with the name);
    `DAT_007d0258` and `DAT_007d0254` up, `OP_Bind_Wound {target, 0}`, `ChangeStance(105)`, 12436
    or 12437. Its answers (the 0x4093 case) by type: not 0 or 3 stands us up; 0 and 1 1432, 3 the
    counts down, 4..7 1433..1436, all in color 0xf.
  - **Begging** (67): an NPC target (12401), not attacking (12402), within 15 (12403), visible
    (12404); `DAT_007d0254` up and `OP_Begging` {target, us, skill, 0xff, .., u32 0} (18 bytes).
    `msg_beg` (@004dff87): the count down, then by byte 6: 0 12892, 1..4 12893..12896 with the u32
    at 8, added to our platinum .. copper.
  - **Pick pockets** (48): a target (12405), not a GM (nothing), not a corpse (12406), not stunned
    (12407), not attacking (12408), not us (12409); a player needs both of us PvP (12410, 12411;
    under PvP type 4 the deities and a 5 level range, 12412, 12413); within 15 (12414);
    `DAT_007d0254` up, `OP_PickPocket` as begging's, `MakeMeVisible`. `msg_steal` (@004e019a): the
    count down; 0 12898, 1..4 12899..12902 and the coins, 5 an item after the first 12 bytes
    (12903 with its name; 12904 when its slot is under 0) put in its slot; 0xff is someone picking
    ours (12897 in red, and the answer).
  - **Fishing** (55): not already (156), not in water (157), nothing on the cursor (158), not
    attacking (159), a pole (type 36; 160) in the primary or secondary hand (161), bait (type 37;
    162); 163, our position kept, standing animation 5 at half speed on us and `OP_Animation`
    `{us, 5, 0.5}`, then 2 s on the cast, `OP_Fishing`. `EQ_Character::DoFishingEvent` (@004bf8a7):
    moving (MovementSpeed over 0.01) calls it off (165); after the 2 s the hook lands 16..32 ahead,
    and if the floor there plus 3 is in water (GetEnvironment not 0, 3 or 7) and the surface
    (FindZoneTopZ) within 30 of our height, splash particles and sound 99 there and 9 s more;
    then it's over. EQMacEmu does the catch.
  - **Tracking** (53): `CTrackingWnd::Activate` (the old UI's list instead) and `OP_Track`; EQMacEmu
    kicks a class without it.
- **CanIHitTarget** (`EQPlayer::CanIHitTarget` @00509e09): not with divine aura (SPA 40: 12695), at
  ourselves (12696) or nobody (12697), `AllowedToAttack` (@0050996a: never at or as races 72, 141,
  73, nor as 114; an NPC or an NPC's pet yes; a player or a player's pet only both PvP, within 4
  levels, both 6 and up), in sight (`CastRay`: else eqstr 123), from foot to foot (3D) within
  `get_melee_range` plus the weapon's extra (else 124 in color `0x12f`), and the bearing to it
  within 80 of our heading (256 between players, with eqgame's wrap at north; else 108).
  `get_melee_range` (@004f3898): both models' `t3dGetScaledActorBoundingRadius` (the skeleton's
  bounding radius, flag 2 of the 0x10 fragment, times the model's scale) times 0.75, at least 14,
  plus the difference of their `ModelHeightOffset`s, plus 2, at most 75.

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
- **Guildmasters** (`frontend/train_window.*`, `CTrainWnd`, TrainWindow): the right button on a
  guildmaster (class 17..31) within 20 trains with our own class's (our class + 16), when none is
  training us already: `EQ_Character::MakeMeVisible`, auto attack off (1466), and `OP_GMTraining`
  (0x409c, 244 bytes `GMTrainee_Struct`: guildmaster, us, the rest 0), waiting (`DAT_007d0258`).
  Another class's says eqstr 1467 ("You are not a member of the %1 class guild.  Begone.", the
  class by `CEverQuest_GetClassDesc(class - 16)`) in color 10. Neither returns, so `_consider`
  follows as for anyone. The zone answers with the same struct (`msg_req_guildmaster` @004de70a):
  the most it teaches each skill (u16 @4), its rate (float @204), success (@208) and a byte per
  language (@209). Without success (the server's faction check, which has the guildmaster say why)
  it just stops waiting; with it, `SetGMData` (@0043ad3d) lists each skill with a cap of 1..252
  that our class can have (ours isn't 255) except 72, offense (33) and defense (15), then each
  language it teaches that `EQ_PC_checkLang` (@004ef555: `DAT_0060b0ac`'s level per race and
  language, the Iksar's at index 13, the Vah Shir's at 14, with class exceptions) lets us learn,
  capped at 100. `Activate` puts music track 7 on and shows it. Each row (`UpdateSkill`) is the
  name, the rank (`CSkillsWnd`'s; "LEVEL: %1" with our class's level for 254, "LEVEL: 0" for
  255), the value (blank for 254 and up), and `CostToTrain` (@004ef6c7): `(value - 10)^3 * greed *
  0.01` copper truncated, the value 0 for a skill `HasSkill` says we lack, written a digit each
  into the copper, silver and gold columns from the right and the rest as platinum, or "--" in all
  four for nothing. The list (rows 24 high, headings 0xff8aa3ff) has coin pictures as headings
  (a column's `<Header>` names a FrameTemplate, `Header_PlatCoin`, whose `Middle` is the picture;
  `CListWnd::DrawHeader` centers it 2 in from the column's edges, 2 down).
  `UpdateRight` shows the practices (profile +156, `PracticePoints`) and the coins we carry, and
  enables Train with a row picked, a practice, the money, and our level at the skill's. Clicking a
  row has the guildmaster say 12056 (12055 for a language) in color 15; clicking the name heading
  sorts by name and the rank or value heading by value, each the other way from last time
  (`SortNames` by strcmp and `SortSkillsByValue`, selection sorts; the first click is A to Z or
  highest first). `Train` (@0043ae1d) asks `EQ_PC::BitchCanTrain` (@004efc95, cdecl: pc, the
  guildmaster's cap, bank, skill): eqstr 117 in red for the trade skills (`EQ_PC_AtSkillLimit?`'s
  57, 59..61, 63..65, 68, 69) past 20; 12925 without practices; 12928 for 255; 12929 under our
  class's level; 12930 at `setskill`'s level limit or `GetSkillLimit2`'s cap; 12931 at the
  guildmaster's cap; for a language 12924, 12926 at 100 and 12927. Then 12065 if we can't pay;
  else `HandleMoney`, `OP_GMTrainSkill` (0x4140, 12 bytes: guildmaster, a u16 eqgame leaves unset,
  u32 bank 0 or 1 for a language, u32 skill), a practice spent and the point added ourselves: a new
  skill starts at our class's level for it (12066), a language says 12066 for its first point and
  12067 after, a skill 12068, all as "tells you" in 0x111. Done, the close box, Escape (after the
  spellbook, before the social editor), walking over 20 away (every 500 ms), its despawning and
  its death close it (`Deactivate`, `CEverQuest_LeaveGuildMaster`): music off, `OP_GMEndTraining`
  (0x409d: guildmaster, us) and our target cleared. `OP_LevelUpdate` going up adds 5 practices
  (`CEverQuest_HandleWorldMessage_LevelUpdate`).
- **Skill limits** (`frontend/skills.*`): `__SkillDict` +0x98 holds a second cap per class, used
  with any of the profile's expansion bits (+3392 in the packet, `+0xd3c`, `& 0x1f`):
  `GetSkillLimit2` picks it. `setskill` (@004d0808) is whether a skill is at the most our level
  allows: the trade skills (+0xb clear: 57, 60, 61, 63..65, 68, 69; tinkering never) stop at 250, or
  200 with more of the others over 200 than Trade Skill Mastery (`GetAbility(0x8d)`) allows; the
  rest by a formula per skill and class ((level + 1) * 5 mostly; offense, defense, dodge, hand to
  hand, the weapon skills, the specializations held to 50 by another over 50, swimming's +20 for an
  Iksar, 58 and 59 from their class level), then the class cap, or with an expansion
  `EQ_PC_GetKunarkThreshold`: past level 50 the classic cap rises `ceil((expansion - classic) /
  10)` (2 at least) a level, up to the expansion cap. Both tables were taken by compiling
  `InitializeSkillData`'s decompile and checked against the first cap table.
- **`OP_MerchantItemPacket`** (0x4031) isn't a merchant's: the zone's `ItemPacketTrade` (what
  `PutItemInInventory` sends: something bought, traded for or handed back by an NPC) goes out
  under it, and eqgame's `HandleWorldMessage` gives it to `msg_snd_ieq` as it does
  `OP_ItemPacket`: the item into our slot `equipSlot`.
- **Notes and books**: the right button on one in a slot under 1000 (`EQ_Note::SendTextRequestMsg`
  @004ce45d) closes its window (the note's or the book's) and, unless it was this item's
  (CheckNote, CheckBook: a second click only closes it), sends `OP_ReadBook` 0x40ce {u8 the
  item's BookType (byte 228), its Filename (byte 231)\0}. The zone answers with the same opcode
  {u8 type, the text (no terminator)}: 0 a note, `CNoteWnd::SetNote` (the window titled with the
  item's name, the STMLbox `Note` given `<c "#000000">` + text + `</c>`) and Activate; 1 a book,
  `CBookWnd::SetBook` and Activate (`frontend/book_window.*`). Escape closes a book's window, then a
  note's (`CDisplay_NewUIProcessEscape`, after the trade window), without forgetting the item, so
  the next right click on it only forgets it.
- **Book window** (`CBookWnd`, BookWindow): `ParseAndFormatBookContent` (@0040887f) lays the text
  out in pages of 12 lines, at most 100. A '`' ends a line (the zone's book texts break their lines
  with it); otherwise a character at a time is added until `CTextureFont::DrawFormattedText` with
  Page0's font in Page0's client width counts two lines, and the line ends at its last space, or
  after that character if it had none. The '`' or space it ends at is skipped. We measure the line
  in one piece against the width (`TextRenderer::Measure`), not by DrawFormattedText's wrapping.
  `DisplayText` puts the left page (+0x2e8) and the next in the multiline edit boxes Page0 and
  Page1, their numbers from 1 in Page0Number and Page1Number. `ShowButtons`: RightButton enabled
  while the page two on has text, LeftButton off the first page. `WndNotification` (@004086b2):
  Done deactivates, the arrows turn two pages; `HandleKeyboardMsg` (@00408b03) does the same for
  Left and Right with no Shift, Ctrl or Alt while the window has the keyboard (ours: while it's the
  top window). Nothing resets the page for another book: it opens where the last was left.
- **Archery and throwing** (`frontend/ranged_attack.cpp`). DoPassageOfTime readies the Range button
  (`__RangeAttackReady`) when the primary's delay (Delay * 100, hand to hand's reuse without one;
  a weapon is item type under 6, 13 or 22) has passed since ActorInfo's AttackTimer, or the range
  item's after a shot (LastAttack 11); auto attack's swings take it (see Our own swings).
  `ModifyAttackSpeed(delay, bow)` (@0050a039) hastens by SPA 11 (delay / (haste * 0.01)) and for a
  bow the first quiver (bag type 2) in the general slots with a weight reduction takes
  `(int)(WR * 0.0025 * delay) + 1` off, unless that leaves 1000 or less. The button
  (CombatPageWndNotification) clears it and calls `DoAttack(11, 0, target)`: the held models become
  nothing and the range item (UpdateItemSlot 7, 8), the range item's own hastened delay must have
  passed (else nothing, the timer kept), standing (12368, 12369), not within 30 of the target
  (12698, yellow) nor 40 above or below (12699; not zone 71), `CanIHitTarget` with the reach added
  (a bow's Range plus an arrow's, or the ammo's, or the thrown item's), then `HandleAmmo`
  (@0050b0fe): not a bow (5), type 6 or throwing (19), the item itself if it has a range; with no
  ammo a stack of throwing weapons moves all but one to the ammo slot (`OP_MoveItem` 11 to 21);
  arrows (27) for a bow, 28 for type 6, the same or other throwing for throwing; else 12875. The
  ammo (or the item) goes to `DoMissileHit` (@004f3228): AimAtTarget (face them if within 64 of our
  heading), `OP_CombatAbility` {target, 11, skill (SkillUsed of the item type; 231 for type 9)}. Then
  `OP_DeleteCharge` {slot, -1, -1} for the ammo (a lone one from the same item in the general slots
  or bags instead, the last found, as bag slot (slot * 5 + 15) * 2 + n) or a droppable thrown item
  with charges, or `OP_MoveItem` {11, -1, 0} for the item gone.
- **/who** (`CEverQuest::Who` @0052f614, `FrontEnd::Who`): up to six words (GrabFirstWord: a quoted
  one keeps its spaces), each taken as the first of: `corpse` (our corpses), `all`, `GM`, `NONGM`,
  `friend(s)`, `LFG`, `Trader`, `guild` (ours; 12336 without one) or `guild<n>`, a class the word
  starts (CEverQuest::GetClassDesc 1..32, the first only), a race it starts (EQ_Character::Race
  0..330: the playable races and the Froglok, "Unknown" for the rest, so `/who u` asks for race 0),
  the first and second number (a level, or a range either way round), else a name (and the guild
  it starts, FindGuildIndexName). Without `all` the zone's players are listed from the spawn list:
  `"%s%s%s%s[%d %s] %s (%s)%s%s"` (trader, AFK, linkdead, GM, level, class title, name, race,
  " <guild>", LFG) or `"[ANONYMOUS]"` for the anonymous, in color 0x119 between 12309/12310 and a
  count line (12316..12318). `all` sends `OP_WhoAllRequest` (0x40f4, Who_All_Struct: whom[64],
  race, class, level low and high, GM, guild; -1 unset). The answer `OP_WhoAllResponse` (0x200b)
  is a heading (an eqstr id at 4 and a dash line at 6), the closing line's id at 0x24 with numbers at
  0x28..0x34, a count at 0x38 and from 0x3a each player: u16 format, u16 pid string, name\0, u16
  rank (an eqstr id: `%T1`), guild\0, u32 GM level, u16 flag (LFG), u16 zone string, u32 zone, u16
  class, level, race, account\0, u16 (211). `msg_who_response` formats 5022 (GM view), 5023, 5024
  (anonymous) and 5025 (`"%T1[%2 %3] %4 (%5) %6 %7 %8 %9"`) in color 10, the zone's short name
  (EQWorldData::GetZoneNameFromIndex; the long name only in the Japanese client), and the closing
  line (5028..5036).
- **/follow** (@004fc259): a group member targeted (13225, 13226), not rooted (13227), in control of
  ourselves and not linkdead (13228), standing (13229); then 13230 and the two red warnings.
  `FollowPlayerAI` (@00507b08) runs from ProcessControls each frame we stand or crouch: they're dead
  (stance 120: 12692) or over 200 away (12693) ends it; seen (CastRay), within 20 we face them
  straight, further we turn toward them the short way at most 4 a frame (9 past a quarter turn);
  within both ModelHeightOffsets + 15 we stop (autorun off), within + 30 and they're slower or we're
  still we walk (`DAT_0079856d` 0), else run, autorun on. `get_bearing` is atan2 of the x and y
  differences in degrees times 511.5/360. Move back, turning and strafing call
  `TurnOffAutoFollow` (12694, autorun off, run on); their spawn going stops it silently.
- **The right button on an item** (`CInvSlot::HandleRButtonUp` @00422804): with no Shift, Ctrl or
  Alt, nothing waited on and nothing cast (a bard's song aside): a bag opens or closes, a book or
  note in a slot under 1000 is read, a common item in slots 0..30 goes to
  `CInvSlot_DoDrinkEatPoison` (@004223ca) by its ItemType: 14 food and 15 drink (over 5999 hunger or
  thirst: 12074, 12077; else 12075/12078 with its name, the level up by its CastTime * 100 / 2 to
  6000, a drink's sound 0x95, 12076/12079, and `OP_Consume` 0x4156 {slot, 999, -1, 1 or 2}); 38
  alcohol (Drunkness over 199: 12080; else 12081, Drunkness up by 40, or a roll r under the Alcohol
  Tolerance skill and under 96 makes it r * 40 / skill, to 200, `OP_DeleteCharge` 0x4147 {slot, -1,
  -1}, 12079); 42 a poison; anything else its click effect: IsStackable 2 or more and EffectType2
  (byte 279) not 0 or 2, a bard singing gets 12082 for kinds 1, 2 and 4, then `CastSpell(10, 0,
  item, slot)` and a potion's sound. eqgame takes one off the stack (Charges, byte 278) itself: the
  zone doesn't say. CastSpell's item branch: an item that isn't common or is type 20 does nothing;
  EffectType1 4 must be worn (slots 1..21, 179), 5 must pass can_use_item (181), either needs
  EffectLevel1 (136); IsStackable under 2 does nothing; Effect2 the spell; Charges 0 is 182; no mana
  check; a potion (type 21) targets us for a targeted spell; no CastingTimeout of ours; OP_CastSpell
  {10, spell, the item's slot, target}.
- **Groups** (`frontend/group.*`). The zone's packets: `OP_GroupInvite` (0x203e, both ways;
  `GroupInvite_Struct` {invitee[64], inviter[64], a group's name[64] (empty), u8 PvP}, 193 bytes; a
  leader's goes out as `OP_GroupInvite2` 0x4040, which the zone hands on as 0x203e),
  `OP_GroupFollow` (0x203d, {inviter[64], invitee[64]}: our yes, and the zone's to the inviter; empty
  on entering a zone ungrouped), `OP_GroupCancelInvite` (0x4041, the same and a why byte: 1
  considering another, 2 in a group, 3 declined, 4 and 5 the raid's), `OP_GroupDisband` (0x4044,
  a name twice: ours to leave, a member's for the leader to remove them; the zone disbands a group
  of two) and `OP_GroupUpdate` (0x2026: u32 action, yourname[64] at 4, a name at 68; action 3's
  new name and action 5's eqstr id at 132; action 7 the five names from 68 and the leader at 388).
  eqgame keeps two lists: the profile's `groupMembers` (byte 5012, six names with ours;
  `msg_wldgroup2` adds, removes, renames and replaces in it) and the window's five others with
  `__GroupLeader`. `CheckForGroupChanges` runs on every `OP_PlayerProfile`: coming in grouped with
  no list yet it fills it and says 12003 or 12004 (the leader "PENDING" until `OP_GroupUpdate` 8);
  otherwise it says who left (12005) and joined (1399) since, or 12001/12002 when the profile's list
  is empty. `msg_wldgroup`'s actions: 0 join (1399), 1 leave (12005, the rest moving up; ours
  12001), 3 rename (5039; eqgame sets the leader to the new name when we lead, whoever changed), 5
  and 6 disbanded (5's eqstr, 6's 12002, the invitation and selection gone), 7 the whole group
  (12004), 8 a leader (5040, 5041), 9 we formed it (12003). The leader's Disband removes the member
  selected at ActorInfo+0x278: Target party member n, a click on their gauge
  (`setTargetBasedOnIndex`, called by the gauge's own mouse handler at 0x5c2d1b with its EQType: 1
  us, 11..15 a member, 16 our pet, 17..21 a member's pet) or on them in the world
  (`EQ_3DView_MouseUp`); Target myself clears it. `/makeleader` (@004f397d), as the leader: the
  member named (EQUtil_FormatCharacterName: a capital, the rest small) or targeted gets
  `OP_GroupUpdate` {8, us, them} (136 bytes); else 5042. `/lfg on|off` (@004f9518): on refused in a
  group (13132) or as a trader (13133), 13134/13135 when it changes, 13136 otherwise; `OP_LFGCommand`
  0x41f0 {our name[64], u32}. Joining or forming a group calls it with "off"; `OP_SendExpZonein`
  sends "on" again into each zone while it's on. The group window's gauges: EQType 11..15 the
  member's name and HP, -2 (no bar) while they're out of the zone; 17..21 their pet's. The raid has
  its say in three places: `CEverQuest_Invite` refuses one in a raid who isn't one of its group
  leaders (5087); `CEverQuest_doInvite` answers 4 while a raid invitation's pending and 5 in a raid
  unless the inviter's in it and we lead none of its groups; `msg_wldgroup` 7 and 9 drop a raid
  invitation (`CRaid::ResetFromDeath`).
- **Raids** (`frontend/raid.*`, `ZoneSession::RaidCommand`). eqgame's `CRaid` is one global
  (`instCRaid`): a state at +0x39b4 (1 none, 2 invited, 4 in a raid; `CRaid::IsRaidLeader`
  @0049d26c is misnamed and tests 2), 72 members of 0xd0 bytes from +0x48 (name, the level and
  class title as text for the window, the class, a flag, group leader, group 0..9 or -1), the
  leader's name (+0x3934), our own leadership (+0x39b9, what `CRaidWnd::IsRaidLeader` reads), the
  inviter (+0x3974) and whether to a new raid (+0x39b8), the window's selected member (+0x39bc),
  the loot type (+0x39c0: 1 the leader, 2 and the group leaders, 3 and the leader's picks), nine
  looters (+0x39c4) and the levels added up (+0x3c04). `HandleWorldMessage` passes `OP_RaidInvite`
  (0x425f) to `HandleC2SRaidMessage` and `OP_RaidUpdate` (0x4260) to `HandleS2CRaidMessage`, only
  with the new UI (`DAT_008092d8`, set from eqclient.ini's NewUI: always ours). Both are
  `RaidGeneral_Struct` {u32 action, name[64] at 4, name[64] at 0x44, i32 at 0x84}; the name at
  0x44 is who it's about. `OP_RaidInvite` 0 and 3 invite us (`HandleCreateInviteRaid` @0049e54c:
  5065, then 5067 and the inviter kept, or back as 0x15 with why: 5066 invited already, 5060 in a
  raid, 12264 a group invitation pending); 0x15 is why ours failed (the eqstr id at 0x84 with the
  name). `OP_RaidUpdate`'s actions: 0 a member joined (`RaidAddMember_Struct`: class, level, group
  leader at 0x88..0x8a, the group at 0x84; 5059), 1 one left (5063; ours 5062 and the raid's reset),
  2 a rename (the new name at 0x84; 5068), 3 a member into group 0x84, 4 the invitation and
  leader's name dropped, 5 disbanded (5071), 6 the whole raid, 7 5072, 8 we formed it
  (`RaidCreate_Struct`, the raid id at 0x44; 5061), 10 none (5071 first if we were in one), 11 the
  loot type said (5100 with `%T1` 5101..5103), 12 and 0x15 an eqstr id said, 13 a group's new leader
  (the one before loses it, found first: the same member twice ends not leading), 0x14 the raid's
  new leader (5069, 5070), 0x16 the loot type set (1 and 2 clear the looters), 0x17 and 0x18 a looter
  added and taken off. Action 6 (`InitializeRaid` @0049d377) is a list, its numbers big-endian
  (`extractUInt32FromBytes`): action, leader's name, the raid id, the count, each member {u32 group
  (0xffffffff none), name, u8 class, level, a flag, group leader}, the loot type, the looter count,
  their names and the leader's name again. A pending group invitation goes; if we were in a raid
  already `ProcessRaidMembershipChanges` says who joined (5059) and left (5063) since, and 5070 with
  the old leader's name (eqgame's, not the new one's) when the leader changed; otherwise 5083.
  What we send is `CRaid_RaidInvite` (@0049e9b3): `OP_RaidInvite`, 0x88 bytes, {action, a name at 4,
  a name at 0x44, i32 at 0x84}. The commands work only with the Planes of Power (profile
  `Expansions` & 8) or on a test server (`OP_LogServer`'s u32 at 20, @00799760):
  `/raidinvite` (`CreateInviteRaid`: not while invited; only the leader (5073); a target (5074) who's
  a player (5075), not us (5076), on ruleset 4 our team (395), not in the raid (5077), room (5048),
  not in our group window (12272); 3 to a new raid, 0 to ours, {target, us}; 5064), `/raidaccept`
  (`SendInviteResponse`: the inviter in the zone, else 5078 in red; 5079 and {4 (1 when the
  invitation was 0), inviter, us}), `/raiddecline` (5080, {0x15, inviter, us, 5081}),
  `/raiddisband` (`RemoveRaidMember`: {5, us, the leader's selected member}, else {5, us, us},
  in a raid or not), `/makeraidleader` (`SendLeadershipChange`, as the leader: the target's name
  with nothing typed, else the name formatted; 5082 if not a member; {0x14, us, them}),
  `/setloottype N` (as the leader, one alphanumeric character 1..3 not the current: {0x16, us, us,
  N}; else 5094), `/addraidlooter` and `/removeraidlooter` (the leader with loot type 3 and a
  member selected: {0x17/0x18, the leader, them}; else 5095/5096) and `/rsay` (5108 in 0x147 out of
  a raid; 5109 and chat channel 15). EQMacEmu's zone answers an accepted invitation with 8 to a new
  raid's leader, then 0 for each member to all, 0x16 (loot type 1) and 6 to each; a raid that falls
  under two members is disbanded (5 to all). Checked live with Claudebot (`eqclient --raid-invite`,
  `--raid-accept`; `frontend_preview ... raid`).
- **The raid window** (`CRaidWnd` @0042fc89, `frontend/raid_window.*`): `/raidwindow` and Toggle Raid
  Window (`ExecuteCmd` 0x6d, Alt+R) open it (`Activate`: the buttons, then the lists rebuilt from
  `CRaid::UpdateRaidPlayerList`) or close it, both gated like the commands; `CDisplay_ActivateMainUI`
  activates it on entering a zone if it was up. Its two lists are white, yellow under the mouse, gray
  selected (`SetColors`), headings 0x8aa3ff. `RAID_PlayerList` is twelve groups of seven rows
  (`ClearPlayerList`: six rows with the group's number and "empty" in gray 0x808080, then a separator,
  83 rows in all); separator rows are item data 0x40000000, which `CListWnd::Draw` draws with
  `DrawSeparator` (`A_DividerMiddle`, 4 high, across the row's middle from 2 in to 3 short of the
  right). Group g's rows start at g * 7, its leader in the first: `AddPlayertoList` fills the group's
  first "empty" row (`FindEmptyRaidListItem`), a group leader takes the first row and whoever was
  there moves to the empty one; a member in no group (0xffffffff) is a row of
  `RAID_NotInGroupPlayerList`. Columns: group, name (white; the selected member's red, `0xffff0000`),
  level, the class title (`KunarkClass`) in its class's color, rank ("Group Leader", "Raid Leader",
  literal strings, not eqstr). `RemovePlayerFromList` writes "empty" back and moves the group's rows
  under it up one (`ResortRaidGroupList`); `ChangePosition` removes and adds; `ChangeRaidGroupLeader`
  swaps the new leader's row with the old leader's (or the group's first): text and the class
  color, not the name's color. Rows are found by their name's text (`FindPlayerIndexInGroup`,
  `FindIndexNotInGroupList`). The count and level average labels are `%d`. `UpdateRaidWindowUI` (on
  activating and every 250 ms, `ProcessRaidWindowFrame`): Accept and Decline shown while invited,
  Invite and Disband otherwise; Disband and Options enabled in a raid; another player targeted
  enables Invite out of a raid, or as the leader Invite (under 72) and Make Leader; Add and Remove
  Looter enabled for the leader. A click on a row is `CRaid::SetTargetRaidPlayer` with its name: a
  member becomes the selection (the old one's name white again, this one's red) and `/target` of
  them; "empty" and separators find no one. Make Leader is `SendLeadershipChange(0)`, the selection.
  The class colors are fifteen, alphabetical (bard, beastlord, cleric, druid, enchanter, magician,
  monk, necromancer, paladin, ranger, rogue, shadow knight, shaman, warrior, wizard), as ARGB ints
  in the UI ini (`UI_<name>_<server>.ini`) `[RaidWindow] ClassColor0..14`, -1 written for a missing
  one (`LoadClassColors`). The window's size (335x280) shows about six rows of the 83.
- **The raid options window** (`CRaidOptionsWnd` @0042f6db): opened by the raid window's Options
  (`Activate`). `UpdateRaidOptionsUI` runs only then: the combo box enabled for the raid's leader,
  and in a raid the loot type as a number and the combo at it. The looter list (`ClearLooterList`,
  `AddLooterToList`) is `CRaid::UpdateOptionsWindow`'s: the leader, then for loot type 2 the group
  leaders and for 3 the picked looters; 0x17 adds a row as it comes. Picking in the combo is
  `CRaid::SetLootType` of choice + 1. The fifteen `RAIDOPTIONS_ClassColor` buttons show their text
  in the class's color (`InitializeClassColor`) and open the color picker; its accept (`0x1e`)
  writes the ini, the button's color and `CRaid::UpdateClassColor` (the class's members' rows).
- **Guilds** (`frontend/guild.*`, `ZoneSession`'s guild packets). eqgame keeps ours apart from our
  spawn's: EQ_Character's `GuildId` and `GuildStatus` (the profile's i16 @4946 and u8 @4967; -1
  none, 0 member, 1 officer, 2 leader) and the spawn's (`Spawn_Struct` i16 @82 and u16 @122;
  `ServerZoneEntry_Struct` @164 and @336). The list (`__Guilds`, 512 slots of 96 bytes: u32 id,
  name[64] @4, u16 exists @72) comes whole as `OP_GuildsList` (0x4192, from the world and the zone)
  and one slot at a time as `OP_GuildAdded` (0x417b, {u32 slot, entry}); `FindGuildIndex` skips
  slots not marked existing, `get_player_guild_name` says "Unknown Guild" for those, while
  `msg_stat_change` and the war and peace questions read `__Guilds` by id (EQMacEmu puts each
  guild in the slot of its id). A player spawn (ours too) with a guild not in it asks
  `OP_GetGuildsList` (0x4128, {i32 guild}: `msg_eq_netplayer`, and
  `msg_eq_netplayer_missingGuilds` before `OP_ReqClientSpawn`); EQMacEmu answers with our own
  guild's entry whatever is asked. Every message is color 0x111 (`print_chat_dsp_chat_2` is it
  too) unless said.
  - `/guildinvite` (@004f4b68, `CEverQuest::GuildInvite` @0052edc0): no guild 12242, a member
    12243. GrabFirstWord's word empty, "o" or starting with 'l' takes the target (a player, not us:
    12248; ruleset 4 our deity team: 12244); else a name (ours does nothing), a spawn in the zone
    that's a player or a GM (`EQPlayer_get_player`, any case; else 12249). An officer's invite ("o"
    as the word, or as all that follows a name) is the leader's only (12247). 12245 or 12246, then
    `OP_GuildInvite` (0x4117, `GuildCommand_Struct` 136 bytes {other[64], us[64], i16 guild @128,
    u32 officer @132}). EQMacEmu uses it for promotions and demotions too.
  - `/guildremove` (`CEverQuest_GuildRemove` @0052f075): ourselves (our name exactly, or no name
    with us targeted): the leader 12251, else `OP_GuildRemove` (0x4119, the same struct with our
    rank) and 12252; another, named or targeted (a player, 12255), an officer's or leader's (12253)
    with 12254.
  - `/guilddelete` (`CEverQuest_GuildDelete` @0052f29f): under leader 10013; else the confirmation
    dialog (0x17, 20 s, 10014) and Yes sends `OP_GuildDelete` (0x411a, {i16 guild}) with 10015.
  - `/guildstatus` (`CEverQuest::GuildStatus` @0052f36b): the named spawn or the target, a player
    and not a GM (12260): "%s is not in a guild." (eqgame's own text, trimName), or 12256..12258 by
    rank (12259 above 2) with the guild's name.
  - `/guildleader` (`CEverQuest_GuildLeader` @0052f49e): the leader (12242, 12261) hands it to the
    name or the target, 12262 and `OP_GuildLeader` (0x4195, {name[64]}).
  - `/guildwar` (@004f4add, `CEverQuest_GuildWar` @00530a45): a name (or the target) and days,
    under 1 or over 14 made 14; `OP_GuildWar` (0x416f, 140 bytes {other[64], us[64], i32 guild
    @128, i32 days, i32 1}) from anyone, 12276 from the leader. `/guildpeace` (@004f4b3d,
    `CEverQuest_GuildPeace` @00530b05): the whole line or the target; the leader's says 12277 and
    sends `OP_GuildPeace` (0x4191, 0 days), anyone else's 12278. EQMacEmu ignores both.
  - `/guildmotd` (@004f4972), from our spawn: not in a guild (or over 0x1ff) 12966; an officer or
    leader with text sets "Name - text" (when name + 5 + text is under 512), anything else sends it
    empty, which asks for it: `OP_SetGuildMOTD` (0x4203, `GuildMOTD_Struct` 580 bytes {name[64],
    u32 guild @64, motd[512] @68}). `DoMainLoop` sends the empty one once after entering the game
    (`DAT_00799b8a`, set by `SetDefaultGameValues`) when our spawn has a guild. `OP_GuildMOTD`
    (0x4204, the same struct) prints the text in color 15; an empty one prints nothing.
  - An invite (`msg_invite_guild` @004dbfc5, `OP_GuildInvite` with us first and the inviter @64):
    another guild's with Receive Guild Invites off answers 5 and says 543..545 and 546; otherwise
    the question is 534..536 by rank. In another guild: 3 and the question said; the dialog up: 4
    and said; in none (rank 0..2): asked (category 0, 10 s). In ours: the rank we have answers rank
    + 10, a lower one answers it (547 too for member: "%1 has demoted you..."), a higher one asks
    548 or 549. Yes answers the rank, No and the time running out 5: `OP_GuildInviteAccept` (0x4118,
    {inviter[64], us[64], u32 response @128, i16 guild @132}).
  - War and peace asked of our leader (`msg_guild_war` @004e493c, `msg_guild_peace` @004e4b09,
    0x416f and 0x4191 with the asker @64 and a non-zero @136): 10002 or 10003 (the asker, their
    guild, the days) in the dialog (8 or 9, 10 s); Yes sends 0, No -2 back as `OP_GuildWarAnswer`
    (0x4193) or `OP_GuildPeaceAnswer` (0x4194); with the dialog up, -2 at once. From the zone
    those two say our petition failed (-5: "Could not declare war on that target's guild.  Try
    again." / "Could not sue for peace ...") or was turned down (-2: "%s has denied your request for
    war." / "... peace."); `handleRequestDenial` @004e48df and HandleWorldMessage's 0x4194.
  - 0x411e (`msg_guild_list` @004e483f: {us[64], name[64], i32 rank}) says 1429..1431; 0x4127
    (@004e473f, {i32 guild, i32 0}) takes us out of our guild (the profile's, not the spawn's)
    with 1377 in color 15. EQMacEmu sends neither.
  - `OP_SpawnAppearance` 22 (guild) and 23 (rank), `msg_stat_change`: someone else joining or
    leaving ours says 12881 or 12882, their rank in ours (0..2) 12886..12888; ours, 12878 (the
    new guild's name), 12880 (the old one's) or 12879, and 12883..12885 (and the profile's
    `GuildStatus`, only for 0..2). The spawn always takes the value. EQMacEmu sends 22 with -1
    when a guild's deleted.
  - eqgame keeps the list in `guilds2.dat` (`load_guilds`, `save_guilds`) and sends its CRC
    (`GuildFileChecksum`) with `OP_GuildsList`; EQMacEmu's world sends the list whatever it is, so
    ours keeps no file and sends 0.
  Checked live as Replaytest leading a guild: each command's messages, the MOTD set and asked
  for, and /guilddelete's Yes ending in 12880.
- **Trading** (`frontend/trade_window.*`, `CTradeWnd`, `CGiveWnd`): a left click on a player or NPC
  with an item or coins on the cursor (`LeftClickedOnPlayer`, `DropItemOrMoneyOnPlayer`) asks to
  trade when `IsOkToTransact` and they're within their `ModelHeightOffset` + 20 (3D; further:
  12294), a NO DROP item only to an NPC (12921, 12922): `OP_TradeRequest` (0x40d1,
  `BeginTrade_Struct` {target, source}). The zone answers an NPC's at once with
  `OP_TradeRequestAck` (0x40e6) and passes a player's on; their `msg_req_trade` opens the window
  and acks, or sends `OP_TradeRefused` (0x40d6, {target, source, reason}: 'b' nobody and 'c' group
  only from the General page's "Allow trading with", 1 busy) and says 1447. The ack (`msg_begin_trade`) opens ours with the cursor's item
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
- **Spell display** (`ToggleSpellDisplay`, `CItemDisplayWnd::SetSpell` @00425957): the item display
  shows a spell too. The title is eqstr 3101 "Spell:", two spaces and the name; the icon is
  `SetBuffIcon`'s (the `A_SpellIcons` cell on `Red`/`BlueIconBackground`), and `ItemValid` goes to
  0 so the icon links nothing. With details (the book and the gems) the text is "CANNOT USE" (3104)
  when our class's level is 255, else "Level: %1" (506), "Skill: %1" (3105), "Mana Cost: %1" (3006)
  only for skills 4, 5, 13, 14, 18, 24 and 43..47 (the casting skills, not a song's), and, but for
  a bard, "Cast Time: %1" (3107) in `%4.1f` seconds. With an AA or focus casting time modifier it's
  "Normal Cast Time: %1    Your Cast Time: %2" (3106), yours no less than half (neither modifier is
  ours, so we always say 3107). The buff window passes no details: title and icon only. Every
  caller toggles: with the item display up (`IsActivated`, CItemDisplayWnd +0x140) it closes
  whatever it shows. Alt+click (`GetKeyboardFlags` 4) and the right button held (`WndNotification`
  0x17) on a book slot (`DisplaySpellInfo`, `DisplaySpellDetails` @0043525a), a gem
  (`DisplaySpellInfoForGemSlot` @0040a480) or a buff (`HandleSpellInfoDisplay` @00409072) call it;
  letting go after the hold (0x19) toggles again for the book and the buffs, and only closes it for
  the gems. The decompile names the gem's 0x19 check `MemorizingSpellIndex` because it reads the
  item display through a `CSpellBookWnd` pointer; it's `IsActivated`.
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
  - **Destroy** (`CInventoryWnd_DestroyHeld` @00421637): with the options' FastItemDestroy off
    (`DAT_00798b1c`, the General page's checkbox, `[Defaults] FastItemDestroy`, off to start with) the
    confirmation dialog (category 5, 10 s) asks "Are you absolutely sure you want to destroy your
    %1?" (eqstr 2043) for the item on the cursor or 2061 for coins; nothing on the cursor, nothing
    happens. Yes is `EQ_PC::DestroyHeldItemOrMoney` (@004d0d88): `OP_MoveItem` {0, -1, 0} and the
    item gone, or `OP_MoveCoin` {0, -1, coin, coin, amount} for the biggest coin on the cursor and
    every coin taken off it.
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
- **Skills window** (`CSkillsWnd`): `Activate` (@0043287a) lists skills 0..99 that
  `EQ_Character::HasSkill` gives us (the class and level table, plus racial skills the server marks
  255), then the languages we know any of, and keeps those rows until it's opened again.
  `UpdateSkill` (@004329d6) shows eqstr names and ranks from Awful to Master (eqstr 3224..3234, the
  first rank whose limit in 10, 20 .. 100, 2000 the value is no more than; a language at 100 is
  Master). A 255 shows `HasSkill`'s value; a 254 shows "LEVEL: %1" (eqstr 2161) with the level our
  class gets it at and no value. `OP_SkillUpdate` prints
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
- **Reverb** (`audio/reverb.*`, Sound Realism): the Fast 2D provider (msssoft.m3d) isn't silent about
  room types and effects levels, though it has no EAX. `M3D_set_3D_room_type` is mss32's
  `AIL_set_digital_master_room_type` (@21115210), a preset from the table at 0x2114d070 in the EAX
  environments' order: the master wet level, and the reverb time, damping and predelay
  (`AIL_set_digital_master_reverb`). `M3D_set_3D_sample_effects_level` is the sample's wet level
  (`FUN_22401a50`: `AIL_set_sample_reverb_levels(1 - obstruction, (1 - exclusion) * effects)`; the
  client sets neither). `FUN_21116190` sends master wet * the sample's wet * its volume (from its left
  and right levels, `FUN_211146a0`) to a mono ring, at most 1; after the mix `FUN_21114d60` runs it,
  the predelay behind: a Butterworth low pass at damping * 22050 / 8 Hz (Miles' rate with
  `Sound44k=0`), four combs (29.7, 37.1, 41.1, 43.7 ms, fed back by 0.001^(length / reverb time)), and
  two allpasses (5 and 1.7 ms, gain about 0.7), the right side through the second with 0.15 in place
  of 0.25. The client's room types (`EqSoundManager` +0xfdc..+0xfe4, `SetListenerEnvironmentHigh`,
  `Low`, `Outside` from `SetGenericEnvironment`): 2 (room: wet 0.417, 0.4 s) under a high ceiling, 12
  (hallway: 0.361, 1.49 s) under a low one, 19 (plain: 0.12, 0.8 s) outside; `SetSpecialEnvironment`
  22 (underwater: 1.0, 1.5 s) with the head in water (`headEnvironmentChange`) and 23 (drugged)
  stunned (`StunMe`) or at 40 drunkenness and over (`DoPassageOfTime`), until `SetGenericEnvironment`.
  The effects level: `WavePlay`'s control's +0x24 (1 as `SoundControl` starts it) times
  `EqSoundManager::SetEffectsLevel`'s (Sound Realism / 20), so doors and actors' sounds; `WavePlay`
  without a control (our own sounds where we are, the weather's) has 0; an emitter's is the
  `EmitterManager`'s level times its own (`SoundEmitter::GiveTime`; the `.emt` column after the
  sequence, `parseEmitterDataFromCSV`'s +0x2c: 1.00 or 0 in TAKP's files). A sound keeps the level it
  started with.

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
  `0x4159` LogoutReply. On it `DoMainLoop` ends with 0xca, and before the connection closes
  `ProcessGameLoop` ("Cleaning up groups") clears the group selection and invitation, calls
  `CEverQuest_Disband` (`OP_GroupDisband` with our name, grouped or not), and in a raid
  `SetTargetRaidPlayer(0)` and `RemoveRaidMember` (`OP_RaidInvite` 5 with our name twice), then
  `ResetRaid`. `ZoneSession` keeps the session open after LogoutReply for these (`FinishLogout`, or
  the next `Pump`, closes it).


  ## Thanks
  Thank you to the following projects & their contributors
  - [EQMacEmu](https://github.com/EQMacEmu)
  - [Zeal](https://github.com/CoastalRedwood/Zeal)
  - [TakpUnderthehood](https://github.com/adonislive/TAKP-Client-Underthehood)
  - [eqgame_dll_takp](https://github.com/EQMacEmu/eqgame_dll_takp) (the client fixes TAKP's eqgame.dll makes)
  - [eqw_takp](https://github.com/CoastalRedwood/eqw_takp) (eqgame's input globals and focus handling)
  - [rdp](https://github.com/solar984/rdp) (rdplib, the RDP library vendored in `third_party/rdplib`)
