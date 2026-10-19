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
  person). Over the view are the chat log (Enter to chat), a CAMP button for a proper camp-out, and
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
core/       portable: RDP session, login/world protocol, DES, inflate, PFS (.pak/.s3d), BMP, DDS, TGA, XML, WLD, eqtext.txt,
            character creation rules
console/    eqclient
audio/      portable sound: the mix (WAV, MP3, positional voices), XMI music and its synths, the client's sound manager
render/     portable 3D: renderer interface, software rasterizer, textures, zone scenes, sky, skeletons, race models
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
assets; its `eqclient.ini` is never used. On startup the GUI adds any missing keys with their
defaults, so the file always lists every setting:

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

Command-line flags override the ini: `--eqdir`, `--login`, `--size WxH`. The GUI saves the last
username as `[Login] User=` and the selected character as `[Defaults] LastCharSel=`, and writes a log to `my-client.log` next to the exe.

Like the real client on an EQMacEmu server with `auto_create_accounts`, the first login of a new
name creates the account but is still rejected. Click CONNECT again.

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
  --idle SECONDS        without --char, stay at character select this long (default 0)
  --no-retry            don't retry once after "Invalid username or password"
  -v                    hex dump every packet
```

## Preview tool (Linux)

```
build/frontend_preview EQDIR OUTDIR [HOST:PORT USER PASSWORD]
```

This drives the real front-end code with scripted input and writes PNG snapshots. Text is
drawn as outline boxes, since there's no GDI. With a login server it logs in, snapshots server
select and character select, and presses Enter World (add `charselect` after the password to stop
at character select instead). `create` instead makes an Erudite wizard from the first empty slot,
snapshots every creation screen and the face picker, submits it, and deletes it again. It's for
checking layout and flow without Windows.

```
build/scene_preview EQDIR OUT.png --char RACE GENDER CLASS [--face F] [--equip M,M,...] [--camera H] [--frames N] [--size WxH]
build/scene_preview EQDIR OUT.png [--zone NAME] [--at X Y Z] [--heading H] [--pitch P] [--frames N] [--size WxH]
build/scene_preview EQDIR OUT.png --player RACE GENDER --zone NAME --at X Y Z [--heading H] [--face F] [--equip M,M,...]
                    [--orbit H] [--pitch P] [--first-person] [--zoom N] [--walk MS [--turn -1|1]] [--npc ...]
                    [--time HOUR MINUTE] [--time-type T] [--fog START END RRGGBB] [--sky N] [--frames N] [--size WxH]
                    [--debug-draw wireframe,nofog,walls,floor] [--door NAME TYPE X Y Z HEADING INCLINE SIZE PARAM]
                    [--door-open MS] [--click X Y] [--jump MS] [--sound OUT.wav]
```

`scene_preview` renders the 3D view to a PNG: the character select scene for any race, gender,
class, face and armor, a free camera anywhere in a zone (server coordinates, heading 0..512), or
the in-zone view with a character standing at a server position (`--fog` and `--sky` stand in for
the zone's `OP_NewZone`). In that mode `--walk MS` runs forward first (and `--turn`), `--npc RACE
GENDER SIZE X Y Z HEADING` stands spawns around (each followed by any of `--npc-look TEXTURE HELM`,
`--npc-equip M,M,...` (the nine equipment slots as the server sends them), `--npc-move SPEED TURN`, `--npc-stance S` and `--npc-fly F`), `--spawn-ms MS` runs them that long first
and prints what they do, `--time HOUR MINUTE` sets the time of day, and `--debug-draw` turns on the
debug menu's render toggles. `--door` puts a door there as `OP_SpawnDoor` would (copy a row of the
server's `doors` table), `--door-open MS` opens them all and runs that long before any `--walk`, and
`--click X Y` reports what a left click there would do. `--jump MS` jumps that far into the walk, and
`--sound OUT.wav` mixes what the doors, we and the spawns sound like as it runs (no zone music or
ambience), printing each wave played. `--frames N` times N renders.

```
build/sound_preview EQDIR OUT.wav [--zone NAME] [--at X Y Z] [--heading H] [--walk X Y Z] [--seconds S]
                    [--music opening|charselect|combat|death|ID]... [--night] [--environment 1|2|3]
                    [--soundfont FILE] [--music-volume V] [--sound-volume V]
build/sound_preview EQDIR --emitters ZONE
build/sound_preview EQDIR --xmi FILE INDEX OUT.mid
```

`sound_preview` runs the client's sound without a window or a sound card and writes the mix to a WAV:
standing at a server position in a zone (or walking from there to `--walk`'s over the time), with the
music selections given turned on, the XMI music through a SoundFont (default `synthus2.sf2`). Each second
it prints the music track and the voices playing, and at the end each second's loudness. `--night` and
`--environment` (1 outside, 2 a high ceiling, 3 a low one) set what the emitters see. `--emitters` prints
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
- SIDL `Font` numbers index the fonts `CDisplay::InitDDraw` creates: Arial 10, 12, 14, 15, 16
  and 20, numbered from 1, with 3 (14px) as the schema default.

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
  the start zone `EQ_PC::InitPlayerStart` picks. Character select then opens on the new character.

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
  (`sky.s3d` `LAYER<sky>1` / `LAYER<sky>3`). Time of day isn't applied yet.
- **Our character:** dressed from `OP_ZoneEntry` (`ServerZoneEntry_Struct`, 356 bytes, not encrypted):
  race, gender, face, the nine equipment materials and tints, and the size the model is scaled to. The
  server's z is the model's center, `size / 5 * 3.125` over the feet (`Mob::CalcZOffset`). The feet go there
  and then settle onto the floor right below. Headings in the profile and `OP_ZoneEntry` are 0..512;
  spawn packets send them as a byte (0..256).
- **Camera:** a chase camera behind and above the character, looking at their head. It comes in when a
  wall or the floor is in the way (tested over a box around the camera, so surfaces it backs into at a
  shallow angle don't cut into the view), and hides the model once it's inside them. The wheel zooms
  from first person out to 200 units, `DoCamAi`'s limit: out of first person into the chase camera, and
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
  (`1.65 * JumpHeightModifier`, 1.35, plus a push of 1 along the heading) and sends `OP_Jump`. The
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

The zone screen fills the window: the 3D view, with the chat at the bottom left, our details and the
nearest spawns at the top left, and the radar at the top right.

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
- **The debug menu's SOUND section** shows the track playing and its number (1 the opening, 4 character
  select, 17 on the zones' music), the voices, the ambient ones by name and distance (of four), the
  creatures' loops, the emitters switched on and in range, the time of day and ceiling the emitters see,
  and the files the zone wanted that aren't there; and switches for all sound, the music (MusicManager's
  own, `/music`: what's playing fades out), the zone's ambience and the game's sounds (doors, steps, jumps,
  splashes, creatures). The switches last until the client exits.

Not yet: everything triggered by game events the client doesn't have yet (combat and death music,
spell, item and hit sounds, the merchant/bank music, rain and wind, thunder), and EAX reverb (the
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
