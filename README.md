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
  person). Over the view are your details, the nearest spawns, a 2D radar, and the chat log (Enter
  to chat), plus a CAMP button for a proper camp-out.
- **`eqclient`** (console, Linux and Windows): logs in, authenticates to world, lists
  characters, can create a test character (`--create`), and with `--char` and `--zone` enters
  the zone, lists nearby spawns, can `--say` something, then camps out. Used for testing
  against a server.

## Layout

```
core/       portable: RDP session, login/world protocol, DES, inflate, PFS (.pak/.s3d), BMP, DDS, TGA, XML, WLD, eqtext.txt,
            character creation rules
console/    eqclient
render/     portable 3D: software rasterizer, textures, zone scenes, sky, skeletons, race models
frontend/   the front end: canvas, widgets, screens, SIDL UI engine, character select scene, character creation (portable)
            + win32_main.cpp / win32_text.cpp (Windows host)
tools/      frontend_preview and scene_preview (Linux), cross-check-windows.sh
third_party/rdplib/  vendored rdplib (MIT, by solar), unmodified
cmake/      llvm-mingw.cmake (Linux -> Windows cross toolchain)
```

The core and the front-end logic are portable, so both compile and run on Linux too. Only the
window, input and GDI text glue is Windows-specific.

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
  Character select is drawn at this size, like eqgame: the SIDL windows at their real pixel size, the
  3D scene filling the rest. A window that doesn't fit the screen's work area is shrunk to fit.
- eqmain's screens (start, login, server select) are 640x480 art. They're
  drawn at their native size in the middle of the window, black around them, not scaled.
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
```

`scene_preview` renders the 3D view to a PNG: the character select scene for any race, gender,
class, face and armor, a free camera anywhere in a zone (server coordinates, heading 0..512), or
the in-zone view with a character standing at a server position (`--fog` and `--sky` stand in for
the zone's `OP_NewZone`). In that mode `--walk MS` runs forward first (and `--turn`), `--npc RACE
GENDER SIZE X Y Z HEADING` stands spawns around, and `--time HOUR MINUTE` sets the time of day.
`--frames N` times N renders.

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
  body textures (`HUMCH0201.BMP` is the chest in material 2, chain), and tints multiply them. Models
  are scaled to their race's default height.

The renderer (`render/`) is a portable software rasterizer drawing into the same surface as the
front end, standing in for `eqgfx_dx8.dll`: perspective-correct textures with mipmaps and bilinear
filtering where they're magnified, masked and blended materials, vertex lighting, and strips
rasterized on worker threads. The sky is a background layer: it's drawn after the solid geometry,
only on pixels the zone left empty, and before translucent surfaces, so a hidden sky costs nothing.
At 1920x1080 a character select frame takes about 24-31 ms on a 4-core VM. Things
learned about the files:

- WLD x/y are the server's y/x (z is up; +x is north), checked against the EQEmu map `load2.map`.
- Bone tracks are quaternions `{w, x, y, z}` plus a shift divided by its own denominator.
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
  back in past 3 units. Holding the right button and dragging looks around; running brings the camera
  back behind you. F9 switches between first person and the chase camera; Page Up/Down tilt it.
- **Moving** (`frontend/player_movement.cpp`), ported from `CEverQuest::ProcessControls` (@0053f337) and
  `process_physics` (@0054d964): Up/W runs (or walks: Ctrl+R, `/walk`, `/run`) at the zone entry's
  `runspeed` (0.7) or `walkspeed` (0.46), Down/S backs up at the walk speed, Left/A and Right/D turn,
  speeding up to 12 heading units a tick. Time runs in 50 ms ticks: on the ground the velocity is
  `v * 0.625 + push * 0.8` (`MovementFriction1/2`), in the air there's no push and it keeps 0.99, and
  gravity is `OP_NewZone`'s. That's about 30 units a second running. Space jumps
  (`1.65 * JumpHeightModifier`, 1.35, plus a push of 1 along the heading) and sends `OP_Jump`. The
  collision is ours: the feet follow the floor (stepping up to 40% of the character's height) and moves
  slide along walls, tested at knee and chest height. There's no water yet; you sink to the bottom.
- **Position updates:** `OP_ClientUpdate` as `pack_physics` packs it (the speed * 10, the heading in
  256ths, its change per tick, y/x/z*10, and the velocities in 16ths per tick). `ProcessControls` flags an
  update when the heading moves 2 or the speed 0.2, `DoPassageOfTime` every second, and `DoMainLoop` sends
  at most every 150 ms and stops repeating an unchanged update after 6.
- **Other spawns:** each race's model is `EQPlayer::GetActorTag`'s (`render/actor_tags.cpp`, the client's
  race table): `<tag>_ACTORDEF` from `<zone>_chr.s3d`, `<zone>_chr2.s3d` or `global*_chr.s3d`, else race 75's
  (`CreatePlayerActor`). Player races are scaled to their height like ours; the rest by `EQPlayer::SetHeights`:
  size / 5, with the model's origin at the spawn's z. They carry on from their last update with its
  velocity, as `process_physics` moves everyone, and play walk, run, idle or (dead) `D05`. Their names
  float over them within 120 units, hidden by anything in front.
- **Time of day:** `OP_TimeOfDay`'s hour (1..24, 1 is midnight) carried on at 20 game seconds a second
  (`EQWorldData::AdvanceTime`). Zones of time type 1, 2 and 5 take their ambient light from the client's
  hourly red, green and blue tables (`SetAmbientLight`); other zones get 0.1. As eqgfx's
  `calculateMeshLighting` does, a zone vertex is its baked color plus its alpha times the ambient light,
  so nights outdoors go dark blue but lamp-lit corners stay lit. The sky dims along `SetSkyBackground`'s
  60 day periods (night until 3:00, dawn to 9:00, day to 15:00, dusk to 20:48), and the fog darkens with
  the light. The sun and moon aren't drawn yet.

The zone screen fills the window: the 3D view, with the chat at the bottom left, our details and the
nearest spawns at the top left, and the radar at the top right.

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
