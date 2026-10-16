# my-client

A headless EQMac (TAKP-era) client. So far it logs in to the login server, picks a world,
authenticates to world, and lists the characters at character select. With `--char` it
enters world and prints the zone server address it was handed. Connecting to the zone isn't
written yet.

## Build

Needs a C/C++ compiler, CMake 3.16+ and OpenSSL's libcrypto (for the login DES). Nothing else.

```bash
sudo apt install -y build-essential cmake libssl-dev   # ninja-build optional
cmake -S . -B build -G Ninja
cmake --build build
```

The binary is `build/eqclient`. The POSIX build is the one that has been tested. rdplib also
has a Windows platform layer, but the client's compile flags assume GCC or Clang.

## Usage

```
build/eqclient -u USER -p PASS [options]
  --login HOST[:PORT]   login server (default 127.0.0.1:6000)
  --world NAME|INDEX    world from the server list (default: first)
  --world-host HOST     override the world address from the server list
  --world-port PORT     world port (default 9000)
  --char NAME           enter world with this character and print the zone address
  --idle SECONDS        without --char, stay at character select this long (default 0)
  --no-retry            don't retry once after "Invalid username or password"
  -v                    hex dump every packet
```

On an EQMacEmu server with `auto_create_accounts`, the first login of a new name creates the
account but is still rejected, and the server won't take a second login on the same connection.
The client reconnects once to cover this.

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

**World** (port 9000):

| Step | Client sends | Server replies |
|------|--------------|----------------|
| 1 | `0x1858` SendLoginInfo: 200 bytes, `"LS#id\0key\0"`, zoning at 188, `0xc3` at 196 (layout as built by `WorldAuthenticate` at `0x0054c9ac`) | `0x41c3` LogServer, `0x1007` ApproveWorld, `0x8001` EnterWorld, `0x41d8` ExpansionInfo, `0x4047` SendCharInfo (`CharacterSelect_Struct`, 1620 bytes) |
| 2 | `0x8001` EnterWorld with `name\0` | `0x41dd` MOTD, `0x8009` SetChatServer, then `0x8004` ZoneServerInfo (`ip[128], u16 port`) or `0x8005` ZoneUnavail |

A rejected world login gets no error packet. The server just drops the connection.

## Layout

```
src/rdp_session.*    RDP connection wrapper (connect, send opcode+body, receive)
src/login_client.*   login server session
src/world_client.*   world server session
src/opcodes.h        wire opcodes
third_party/rdplib/  vendored rdplib (MIT, by solar), unmodified
```

## Sources

The protocol details came from:
- the EQMacEmu server source (`loginserver/`, `world/client.cpp`, `common/patches/mac*`, `utils/patches/patch_Mac.conf`)
- the eqgame.exe decompile in TAKP-Client-Underthehood (`WorldAuthenticate`, `net_connect`, `send_message/OpCode`)

Neither one is needed to build.
