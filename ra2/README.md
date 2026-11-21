# Red Alert 2 engine

Rallypoint's Red Alert 2: Yuri's Revenge engine. It is a from-scratch C++17 + SDL2 reimplementation by
omarelarab, built in CI for Linux, macOS and Windows and shipped beside the launcher as `ra2engine/`.
The player brings their own Yuri's Revenge files; nothing of the game is in here.

## Layout

- `engine/`: a trimmed plain copy of https://github.com/omarelarab/ra2-engine at
  `a8cfdccfef8592c13a6517c3fb76d834b1534027` (2026-10-14). It is the pristine base: don't commit patched files.
  Licence: 0BSD (`engine/LICENSE`); `engine/THIRD_PARTY_NOTICES.md` lists what it carries (doctest, stb).
- `patches/`: our changes, applied with `sh ra2/patches/apply.sh` (idempotent, same rules as `patches/apply.sh`).
  Regenerate one: edit the tree, `git add -N <new files> && git diff --relative=ra2/engine -- ra2/engine/<paths> > ra2/patches/NNNN-name.patch`,
  then restore (`git checkout ra2/engine && git clean -fd ra2/engine`) and re-apply. The patches are under the engine's 0BSD terms.
- `third-party/SDL2-2.30.9.tar.gz`: the SDL release tarball the engine's CMake pins (SHA-256
  `24b574f7...6beba4`, from github.com/libsdl-org/SDL releases, zlib licence). Vendored so configure downloads nothing.
- `mingw-w64-x86_64.cmake`: toolchain file for the Windows cross build.

## What was left out of the copy, and why

| Removed | Why |
|---|---|
| `engine/gen/orig_math_tables.inc` (460 KB) | EA data: the FastMath sin/asin/tan/atan/sqrt tables and constants copied from `gamemd.exe` `.data`. Replaced by tables we compute (below). |
| `tools/extract_orig_math_tables.py`, `tools/orig_math_oracle.py` | They read `gamemd.exe` to make the two files above. |
| `tests/orig_math_oracle.inc` (340 KB) | Outputs of the original game code (run in an emulator), so EA-derived. The math test now checks our tables instead. |
| `tools/simdiff/` (incl. `hook/simhook.c`, `simlaunch.c`), `tools/xplat_gate.ps1` | Inject into or drive `gamemd.exe`. |
| `server/` (Go lobby server), `android/`, `docs/`, `scripts/`, `tests/data/movies/` | Not needed to build or test; the docs are the reverse-engineering notes. |

Kept, but worth knowing (small tables recovered from the executable, kept because the engine needs them to read
the player's own files or to play the same way): `engine/voxel_normals.inc` (the 36 + 245 VXL normal vectors, a
file-format table also published by OpenRA and XCC), `engine/locomotion.cpp` (the drive track point arrays),
`engine/inf_anim.cpp` (42 infantry DoControls rows), `engine/net/event.hpp` (48-byte event size table), and many
single constants (sidebar geometry, timings) cited as `[RE: ...]`. `src/bink.cpp` has Bink tables copied from
FFmpeg's `binkdata.h` (LGPL-2.1+), which the upstream 0BSD notice does not mention.

## The maths tables

The simulation never calls libm: like the original it reads sin/cos/tan/asin/acos/atan/atan2/sqrt from lookup
tables (`engine/orig_math.cpp`). Patch 0001 makes CMake run `tools/gen_math_tables.py` at configure time
(Python 3 is required to build). It writes the same names, sizes and formats the routines expect, each value
computed from its definition with Python integers (fixed point, 2^-160) and rounded half-to-even to float32/
float64, so the output is the same bytes on every build machine and the tables are correctly rounded. The atan
table covers [0, 100] in 4096 steps (our choice; the original's step was a constant from the exe), tan's two poles
hold FLT_MAX, and the sqrt table samples the start of each bucket so exact squares stay exact.

These are our own values, not EA's. The only cost is bit-fidelity with `gamemd.exe` (a replay or sync check against
the original game would differ); every one of our builds simulates identically, which
`tests/test_orig_math.cpp` pins with a golden hash of every routine's results.

## Patches

| Patch | What |
|---|---|
| `0001-math-tables-from-their-definitions` | The generator above, its CMake hook, and the math test rewritten for our tables. |
| `0002-trimmed-tree-builds-offline` | Drops the `simdiff_l0` target, uses the vendored SDL tarball, and real-install tests skip (instead of throwing) when no game is installed. |
| `0003-linux-and-macos-link-sdl-statically` | Linux and macOS link SDL statically: one executable, no rpath. Windows keeps `SDL2.dll`. |
| `0004-launcher-interface` | `--launch`, window size/fullscreen/title, map files by path, the CnCNet V2 tunnel. |
| `0005-crt-lround-check-only-with-ucrt` | One test compares `om::lround` with the Windows C runtime's; it now runs only against the UCRT (our MinGW build links msvcrt, which differs out of range; the sim never calls it). |
| `0005a-macos-interfaces-with-getifaddrs` | macOS (and other BSDs): list local interfaces with `getifaddrs` (no `ifr_netmask` on `SIOCGIFCONF` there). |
| `0005b-extract-maps` | `--extract-maps <outdir>`: no window; writes every multiplayer map (multimd.mix, loose `.map`/`.mpr`/`.yrm`, and `.mmx`/`.yro` MIX packs) as plain INI plus `index.json` (`name`, `source`, exact `Map=` for `--launch`). |
| `0005c-ra2fixture-linux-paths` | Real-asset tests join install paths with `/` and resolve file names case-insensitively so `RA2_INSTALL_DIR=… ./ra2assets_tests` finds Steam's mixed-case files on Linux. |

## Building

```sh
sh ra2/patches/apply.sh
# Linux (needs SDL's build deps: libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev
#        libxkbcommon-dev libwayland-dev libasound2-dev libpulse-dev, plus python3)
cmake -S ra2/engine -B ra2/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
# macOS (universal)
cmake -S ra2/engine -B ra2/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
# Windows (cross, from Linux with mingw-w64)
cmake -S ra2/engine -B ra2/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=ra2/mingw-w64-x86_64.cmake
cmake --build ra2/build && ra2/build/ra2assets_tests
```

Ship `ra2engine` (`ra2engine.exe` + `ra2game.exe` + `SDL2.dll` on Windows; `ra2game.exe` has no console window).

## Launcher interface

Command line (any order):

| Argument / variable | Meaning |
|---|---|
| `--data <dir>` or `RA2_INSTALL_DIR` | Folder with the Yuri's Revenge files (`ra2.mix`, `ra2md.mix`, `language.mix`, `multimd.mix`, ...). |
| `--launch <file.ini>` | Start the game described in the file (below): no menu, no lobby. When the score page is closed the process exits. |
| `--width <w> --height <h>` | Window size. The engine draws a 720-line canvas scaled to the window (its own design); RA2.INI's size is then ignored. |
| `--fullscreen` / `--windowed` | Desktop fullscreen at the display's size, or a window. |
| `--title <text>` or `RA2_WINDOW_TITLE` | Window title. |
| `--no-audio` | Silent. |
| `--extract-maps <dir>` | Write every multiplayer map as plain INI under `<dir>` and an `index.json` (display name, source, `Map=`). No window; the process exits when done. Used by the launcher to list official and `.mmx` maps. |

The log goes to stderr (`ra2game.exe`: `<pref dir>/ra2engine/RA2/ra2engine.log`). Player settings (volumes,
scroll speed) still come from the engine's own `RA2.INI`.

### The launch file

An INI file (the engine's own parser: names are case-insensitive, `;` starts a comment, so no value can contain
one). Every machine of a network game gets the same file except `LocalPlayer` (the rest must agree, `Seed`
included, or the games go out of sync).

```ini
[Game]
Mode=skirmish        ; skirmish (one human, the rest AI) or network
Map=xmp01t4.map      ; an entry of multimd.mix, or a path to a .map/.mpr file (any '/' or '\' makes it a path)
Seed=123456789       ; required for network; skirmish picks one when absent
LocalPlayer=0        ; the [PlayerN] this machine plays (default: the first human)
Credits=10000
GameSpeed=1          ; 0 fastest .. 6 slowest
UnitCount=10         ; 0..10
TechLevel=10         ; 1..10
GameMode=1           ; MPModesMD.ini index: 1 Battle, 2 Free For All, ...
ShortGame=yes
SuperWeapons=yes
Crates=yes
BuildOffAlly=yes
MCVRedeploy=yes
AITakeover=no        ; network: a player who drops is taken over by the AI instead of defeated

[Tunnel]             ; network only
Address=127.0.0.1
Port=50000
Version=2            ; 2 = CnCNet tunnel v2 header (the Rallypoint agent), 3 = v3

[Player0]            ; up to [Player7]; slot 0 must be human in a network game: it is the host (lockstep master)
Type=human           ; human, ai, or closed (a missing section is closed)
Name=Alice
Country=Americans    ; Americans Alliance French Germans British Africans Arabs Confederation Russians, an index 0-8, or random
Color=0              ; 0..7 or random
Start=1              ; 1..8 (the map's start waypoints) or random
Team=A               ; A..D (or 1..4) or none
TunnelId=3           ; network, humans: this player's tunnel id (Rallypoint: last octet of the 10.77.0.x address)

[Player1]
Type=ai
AI=brutal            ; easy, medium, brutal
Country=Russians
```

Random countries, colours and starts are resolved from `Seed`, the same on every machine.

### Network play over the Rallypoint agent

The engine speaks CnCNet tunnel v2 to `[Tunnel]` from one UDP socket (an ephemeral local port):

- Every datagram is `[u16 BE sender id][u16 BE receiver id][payload]`. The payload is the engine's own
  `[u32 LE checksum][packet]`, at most 640 bytes (636 + 4), so well under the agent's 1020.
- Ids are the players' `TunnelId`s, 1..65535; 0 is never a player.
- Keep-alive every 5 s: sender = own id, receiver `0xFFFE`, one zero byte of payload. Drop it (it lets a real
  tunnel learn the sender's address).
- Ping every ~5 s: 50 bytes, all zero except a u32 LE cookie at offset 8 (so sender = receiver = 0). Answer with
  the first 12 bytes back to the sender; the engine shows the round trip.
- Received datagrams whose receiver id is not ours, or shorter than 5 bytes, are ignored.

No lobby is involved: the engine knows every peer from the file, and the match waits up to 180 s at frame 0 for
the slower machines to load. There is no build check between machines (the engine's own lobby did that), so
everyone must run the same engine build.

## Releases

RA2 work lives on `feat/ra2`. A tag `ra2-alpha.N` pushed from there runs the whole release workflow and publishes a
GitHub pre-release; the app is built with `RALLYPOINT_VERSION=ra2-alpha.N` (what the updater looks for), the
installers and packages are named `<tauri.conf.json version>-ra2.alpha.N`, and the relay image is pushed as
`ra2-alpha` and `ra2-alpha.N`, never `latest`. `v*` tags are unchanged.

## Not verified

- Nothing has been played: this VM has no Yuri's Revenge files, so `--launch` (skirmish and network), the window
  options and the map-by-path loader are compiled and reviewed only. The V2 tunnel path is covered by a test
  (`mp: lobby through a CnCNet V2 tunnel`) against a minimal forwarder.
- macOS is built only by CI.
