# my-client

A from-scratch EQMac (TAKP-era) client. Two programs share one core:

- **`eqclient_gui`** (Windows): the original front end, meaning the start screen, the login screen and
  server select, drawn from your EverQuest install's own art. After PLAY it authenticates to
  world and shows character select, built from the install's SIDL UI files
  (`uifiles/default/EQUI_CharacterSelect.xml`), over the 3D character select scene: the
  `load2` zone with the selected character standing in their class's room while the camera
  circles them. Enter World connects to the zone. In place
  of the in-zone 3D view (not written yet) the zone screen shows your character, the nearest spawns,
  a 2D radar, and the chat log with a /say line, plus a CAMP button for a proper camp-out.
  Character creation in the GUI isn't written yet.
- **`eqclient`** (console, Linux and Windows): logs in, authenticates to world, lists
  characters, can create a test character (`--create`), and with `--char` and `--zone` enters
  the zone, lists nearby spawns, can `--say` something, then camps out. Used for testing
  against a server.

## Layout

```
core/       portable: RDP session, login/world protocol, DES, inflate, PFS (.pak/.s3d), BMP, DDS, TGA, XML, WLD, eqtext.txt
console/    eqclient
render/     portable 3D: software rasterizer, textures, zone scenes, sky, skeletons, race models
frontend/   the front end: canvas, widgets, screens, SIDL UI engine, character select scene (portable)
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

Put a `my-client.ini` next to `eqclient_gui.exe`:

```ini
[Paths]
EQDir=C:\TAKP

[Login]
Server=192.168.0.18:6000

[Display]
Scale=2
```

- `EQDir` is the EverQuest folder. It needs `eqfeart*.pak` (the front-end art), `eqtext.txt`
  (the front-end strings), `uifiles/default` (the SIDL UI for character select),
  `eqstr_en.txt` (the server message strings), and for the 3D character select scene
  `load2.s3d`, `load2_obj.s3d`, `sky.s3d` and `global_chr.s3d` / `global4_chr.s3d` /
  `global7_chr.s3d`. Nothing from
  the install is copied into this repo. If `EQDir` isn't
  set, the GUI tries the current folder, then the exe's folder.
- `Server` is the login server. It isn't read from `eqhost.txt`, so you can't hit the live TAKP
  server by accident.
- `Scale` is the integer window scale for the 640x480 front end. 0 or missing fits the screen.

Command-line flags override the ini: `--eqdir`, `--login`, `--scale`. The GUI saves the last
username as `[Login] User=`, and writes a log to `my-client.log` next to the exe.

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
at character select instead). It's for checking layout and flow without Windows.

```
build/scene_preview EQDIR OUT.png --char RACE GENDER CLASS [--face F] [--equip M,M,...] [--camera H] [--frames N]
build/scene_preview EQDIR OUT.png [--zone NAME] [--at X Y Z] [--heading H] [--pitch P] [--frames N]
```

`scene_preview` renders the 3D view to a PNG: the character select scene for any race, gender,
class, face and armor, or a free camera anywhere in a zone (server coordinates, heading 0..512).
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
- `Screen`, `Button`, `Label` and `StaticAnimation` pieces. A child's `Location` is relative to
  the window's client area.
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

The renderer (`render/`) is a portable software rasterizer drawing into the same 640x480 surface as
the front end, standing in for `eqgfx_dx8.dll`: perspective-correct textures with mipmaps, masked and
blended materials, vertex lighting, a sky pass, and strips rasterized on worker threads. Things
learned about the files:

- WLD x/y are the server's y/x (z is up; +x is north), checked against the EQEmu map `load2.map`.
- Bone tracks are quaternions `{w, x, y, z}` plus a shift divided by its own denominator.
- BMP textures are used bottom-up (v = 0 is the bottom row); the zones' DDS textures (DXT1/3/5) are
  stored that way already.
- Zone lighting is the `lights.wld` point lights (linear falloff) over a flat ambient, plus the meshes'
  small additive vertex colors. It's an approximation of the real client's lighting.
- TAKP's repacked `sky.s3d` names its filename table with CRC `0xFFFFFFFF` instead of `0x61580AC9`.

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
