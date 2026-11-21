# Red Alert 2 engine

Rallypoint's Red Alert 2: Yuri's Revenge engine. It is a from-scratch C++17 + SDL2 reimplementation by
omarelarab, built in CI for Linux, macOS and Windows and shipped beside the launcher as `ra2engine/`.
The player brings their own Yuri's Revenge files; nothing of the game is in here.

## Layout

- `docs/gamemd-parity-backlog.md`: prioritized gaps vs retail `gamemd.exe` (tiers 1–4; docs only).
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
| `0006-tunnel-token-auth` | `[Tunnel] Token=` (32 hex digits): the engine proves itself to the Rallypoint agent before any tunnel traffic (`0xFFFF`/`0xFFFF` + `RPAU` + token); keep-alives wait for the 8-byte answer. |
| `0010-yuri-side-helpers` | Pure helpers for the third side (`yuri_side.hpp`) and `tests/test_yuri.cpp`. |
| `0011-yuri-lobby-ai-and-economy` | `YuriCountry` as lobby index 9, ThirdSide country/AI keys, Yuri AI roster and catalog. |
| `0012-yuri-buildings-roster-and-crews` | Load Yuri structures/units, cameomd cameos, ThirdCrew survivors, Soviet chrome for Yuri. |
| `0013-slave-miner-economy` | Yuri Slave Miner: `Enslaves=` / `SlavesNumber` / regen+reload rates; deployed YAREFN spawns SLAV gatherers that deliver ore into house credits. |
| `0014-skirmish-mind-control` | Skirmish/MP CaptureManager: `World::step_mind_control` arms `w_mind_control`, capacity, release on death/board/Temporal; campaign path unchanged. |
| `0015-gattling-weapon-stages` | `IsGattling` RateUp/RateDown timer, Stage1..N / EliteStage, Weapon1..N (AG odd / AA even); YTNK and GTGCAN. |
| `0016-yuri-cameo-mix-lookup` | `cameomd.mix` is opened from `langmd.mix` (stock YR), so Yuri sidebar icons resolve. |
| `0017-magnetron-magnetic-beam` | Magnetron Magnetic Beam: warhead `IsLocomotor` lift/hold/drop (`magnetron.hpp`). |
| `0018-floating-disc-drain` | Floating Disc `DrainWeapon`: foundation hover, power blackout, money drain. |
| `0019-magnetron-save-fields` | Save visitors for Magnetron `weight_milli` / `magnet_*` / AbilityRules mag_* (format 52). |
| `0020-lobby-game-options` | `--launch` `AlliesAllowed=` overrides mid-game alliances (format 53). Crates/SuperWeapons already honour the launch file; Play/Online toggles write them. |
| `0021-auto-repair-and-crate-density` | `--launch` `AutoRepair=` (`[IQ] RepairSell=0`) and `CrateMinimum=` (CnCNet Increased=8 / Extreme=40); Play/Online crate density + Auto repair (format 54). |
| `0022-grinder-bio-reactor-mc` | Yuri Grinder (`Grinding=` / Soylent) and Bio Reactor (`InfantryAbsorb=` / ExtraPower); MC victims can be ground or absorbed (format 55). |
| `0023-world-sizeof-pin` | Pin `World` layout size for save-format stability. |
| `0024-namisil-superanim-hold` | SuperWeapon buildings (Nuke Silo, Chronosphere, Iron Curtain, Weather Controller): one-shot SuperAnim open/close, then hold the last frame instead of looping SuperAnimTwo/Four (doors no longer cycle while ready/charging); skip grand-opening Active* loops when `SuperAnim` is set. Presentation only. |
| `0025-hidpi-bitmap-font-scaling` | Nearest-neighbour present + integer shell-font glyph blits; macOS `SDL_WINDOW_ALLOW_HIGHDPI` (view only). |
| `0026-crisp-credits-macos-cursor` | Credits tally: integer-scaled 12metfnt glyphs with nearest sampling on HiDPI; nearest logical-to-output upscale; macOS focus regained re-hides the OS cursor when the game draws its own. |
| `0027-desktop-canvas-follows-window` | Desktop match canvas height follows the window (720..1440) so 1080p/1440p shows more map. Menus stay 1280x720. Android and `--play set canvas` are unchanged. |
| `0028-cleg-chrono-beam` | Chrono Legionnaire: Temporal weapons use ChronoBeamColor RadBeam (not EBolt); beam held on the victim while erasing. |
| `0029-building-health-pips` | Own buildings: art-based pick on desktop (not only touch) so left-click selects them; Legacy scheme can select non-factories; hover shows the diagonal PIPS health line when nothing is selected. Presentation only. |
| `0030-tesla-low-power-visual` | Presentation: powered defenses (Tesla Coil, Prism Tower, etc.) stop their ActiveAnim glow and show `LowPowerAnim` when the house is low power or the building is powered down — same predicate as defense fire (`house_low_power` / `power_off`). |
| `0031-sw-sidebar-slots` | Superweapon cameos sit in the side2 cameo sockets under the radar (clock, READY text and the click target use that rect). The build icons start on the next row. |
| `0032-smooth-unit-presentation` | Units are simulated at 15 Hz and were drawn at the pose after the last tick, so on a 60 Hz display they held still and then jumped. The picture now blends the previous tick's `fx`/`fy` (and the infantry sub-cell) by how far the logic accumulator has got toward the next tick. A step longer than 2 cells is drawn at the new pose. The simulation, saves and net checksums are unchanged. |
| `0033-superweapon-building-charge-anim` | Artmd-accurate SuperAnim..Four phases for superweapon buildings: charge hold (SuperAnim), `ChargedAnimTime` ready sting (Two then Three), fire one-shot (Four) then back to charge; stops Weather Control's green fire crescents for the whole recharge. Requires 0024 for grand-opening Active* skip. Presentation only. |
| `0034-ramp-slope-level` | Ground units follow TMP ramps: the FootClass height gate (`FUN_004D9C60`) already allows a one-level step when the lower cell's SlopeIndex is set; `step_units` now adopts the cell's level as the unit walks, and the sprite blends the two cells of the step. |
| `0035-stock-ai-min-defense-rule` | Skirmish AI: `UseMinDefenseRule` / `MinimumAIDefensiveTeams` from gamemd `FUN_0041e740` — under the minimum only `IsBaseDefense` Team1 triggers fire and autocreate is held off so stock base-guard teams form; call_for_help then has guard members (save v56). Simulation change. |
| `0037-ai-boost-smarter` | Optional `[RP-AI-BOOST]` smarter skirmish AI (`AIBoost=` / `RA2_AI_BOOST` 1..3): shorter TeamDelays/BuildDelay, lower CreditReserve, faster harvester replace, stronger base-defense response, L3 anti-tank/anti-inf mix bias + trigger success nudge. Off by default; computer houses only. |
| `0040-country-bonuses-from-gamemd` | HouseType country bonuses from gamemd `FUN_00511850`: `Cost`/`Armor`/`ROF`/`Firepower`/`Groundspeed`/`Airspeed`/`BuildTime` and `*Mult` keys, `VeteranInfantry`/`VeteranUnits`/`VeteranAircraft`, plus `Owner`/`RequiredHouses`/`ForbiddenHouses` on start pools (TNKD/SNIPE/AMRADR/…). Stock rulesmd leaves the Mult/Veteran keys commented (identity); they apply when a mod or rules uncomment them. |
| `0041-building-infantry-anims-from-gamemd` | Building/infantry anim parity from `gamemd.exe`: art `AnimActive` cycles the main SHP (ConYard/Depot) with SimpleDamage (`Shape_Number` FUN_0043ef90); Tesla/Prism `IsAnimDelayedFire` stops Active/Idle for SpecialAnim then restores them (FUN_0044c565 / FUN_00451b30); Idle resumes after finite Active*; infantry Undeploy stays locked until done and can finish into Walk. Presentation only (sim/hash unchanged). |
| `0042-unit-tilt-on-slopes` | Vehicles pitch on TMP ramps (T1.2): Drive Draw_Matrix `FUN_0055A7D0` reads `CellClass` SlopeIndex (+0x11C), copies the slope matrix via `FUN_007559B0` (table from `FUN_00754CB0` / `FUN_005AE6F0`). Presentation-only voxel pitch; sim/CRC unchanged. |
| `0043-siege-chopper-deploy` | Siege Chopper (`SCHP`) deploy vs gamemd: `IsSimpleDeployer` arms `DeployFire` without `Deployer` (`FUN_00739AC0`); `DeployToLand` lands/lifts on deploy/undeploy (`FUN_0054BD30`); deployed draws `UnloadingClass=SCHD` and plays `DeployingAnim=SCHPDEPL`. Simulation: deploy post + weapon slot; save format unchanged. |
| `0044-ai-call-for-help` | T1.3 call-for-help vs Steam `FUN_00708080`: building victims get ArchiveTarget x100/x10 help ranking; skip ResourceGatherer and loaded transports; Amphibious domain zone stand-in; `PowerEmergency` sell + IQ `RepairSell` on ConditionRed sell. Simulation change (no `World` sizeof growth). |

## Factions (Country=)

Launch-file / launcher `Country=` is an index into the lobby list, or the country name:

| Index | Country | Side (rules `Side=`) |
|---|---|---|
| 0 | Americans | GDI (Allied) |
| 1 | Alliance | GDI |
| 2 | French | GDI |
| 3 | Germans | GDI |
| 4 | British | GDI |
| 5 | Africans | Nod (Soviet) |
| 6 | Arabs | Nod |
| 7 | Confederation | Nod |
| 8 | Russians | Nod |
| 9 | YuriCountry | ThirdSide (Yuri) |

`Country=9` and `Country=YuriCountry` are the same. The launcher's Yuri seat is side index 9 and writes `Country=9`.

### Country bonuses (all 10)

- **Ownership**: sidebar, AI catalogs and skirmish start pools honour `Owner=` / `RequiredHouses=` / `ForbiddenHouses=` (Americans `AMRADR` paradrop radar, French `GTGCAN`, British `SNIPE`, Germans `TNKD`, Korea `BEAG`, Iraq `DESO`, …; `GAAIRC` forbidden for Americans).
- **Multipliers / veterans**: `country_bonuses.hpp` loads the HouseType keys gamemd reads (`CostUnitsMult`, `VeteranInfantry`, …). Stock YR comments them out (all 1.0 / empty lists); when present they scale production cost/build time, firepower/ROF, spawn speed/HP, and arm listed types as veterans at factory exit and at the UnitCount start force.
- Doctests cover `CostUnitsMult` cost scaling and `VeteranInfantry` arming (synthetic INI).

### What works as Yuri

- Selectable in `--launch` skirmish; human roster is rules-driven for `YuriCountry` (INIT, SMIN, YTNK, YURI, …).
- Yuri buildings load (YACNST, YAPOWR, YABRCK, YAWEAP, YAREFN, …) and AI skirmish arms with `conyard=YACNST`.
- PCV is the BaseUnit; deploy/build tree follows prerequisites.

### Partial / missing

- **Country Armor=/Speed= on every combat path**: spawn bakes Armor into HP and Speed into `speed_cps`; Firepower/ROF go through `house_*_mul`. A mid-game rules reload does not refresh stored bonuses (re-arm is at skirmish setup).
- **Slave Miner**: done for deployed YAREFN (`Enslaves=SLAV`, spawn/regen/reload, gather + deliver into Storage/credits). Remaining: undeployed SMIN does not run slaves (ModEnc: infantry only leave BuildingTypes); killer-house liberation on miner death is not wired from combat; AI `AISlaveMinerNumber` / redeploy scoot (`SlaveMinerShortScan`) not modelled; no Xvfb Yuri skirmish check in CI.
- **Mind control**: skirmish/MP CaptureManager (`step_mind_control`); campaign keeps its own path. Grinder (`grind_now`) and Bio Reactor (`absorb_now`) free the MC slot (patch 0022); enter cursors / reactor eject still open.
- **Gattling** (`IsGattling` / YTNK, GTGCAN): RateUp while engaging, RateDown when idle (0 resets); Stage/EliteStage endpoints pick Weapon1..N (odd AG, even AA). Doctests cover the timer and slot pick.
- Yuri structure/unit cameos load from `langmd.mix`/`cameomd.mix` (patch 0016). Types with no `Cameo=` in artmd (e.g. YACOMD, YAPPPT, YAROCK) stay blank.
- **Magnetron**: lift/hold/drop via `IsLocomotor` (patch 0017); building toss / beam art not drawn.
- **Floating Disc**: DrainWeapon power/money drain (patch 0018); DISKRAY anim not hooked; no live Disc-over-base check.

## Building

```sh
sh ra2/patches/apply.sh
# Linux (needs SDL's build deps: libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev
#        libxkbcommon-dev libwayland-dev libasound2-dev libpulse-dev, plus python3)
cmake -S ra2/engine -B ra2/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
# macOS (Apple Silicon)
cmake -S ra2/engine -B ra2/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
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
| `--width <w> --height <h>` | Window size. Menus stay a 1280x720 canvas, letterboxed. The match canvas uses the window's height (720 minimum, 1440 maximum) so a 1080p or 1440p window shows more of the map; width is that height times the aspect, kept between 16:9 and about 21:9. Passing this ignores RA2.INI's size. |
| `--fullscreen` / `--windowed` | Desktop fullscreen at the display's size, or a window. |
| `RA2_LOGICAL_HEIGHT` | Optional logical canvas height (720, 900 or 1080). The launcher sets it from Settings > Display > UI scale when not default. Current engine builds still fix the canvas at 720 px and ignore this variable; a future patch will honour it so large windows show more of the map instead of only upscaling. |
| `--title <text>` or `RA2_WINDOW_TITLE` | Window title. |
| `--no-audio` | Silent. |
| `--extract-maps <dir>` | Write every multiplayer map as plain INI under `<dir>` and an `index.json` (display name, source, `Map=`). No window; the process exits when done. Used by the launcher to list official and `.mmx` maps. |

The log goes to stderr (`ra2game.exe`: `<pref dir>/ra2engine/RA2/ra2engine.log`). Player settings (volumes,
scroll speed) still come from the engine's own `RA2.INI`. `[Video] ScreenWidth` / `ScreenHeight` (the Options
resolution list) size the window the same way when `--width` / `--height` are absent, and the match canvas follows.

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
CrateMinimum=8       ; optional floor when Crates=yes (CnCNet Increased=8, Extreme=40); absent = rules
AlliesAllowed=yes    ; mid-game alliances; absent = the GameMode's own AlliesAllowed
AutoRepair=yes       ; AI auto-repair (CnCNet [IQ] RepairSell=0); absent = rules RepairSell
AIBoost=2            ; optional 1..3 smarter skirmish AI (patch 0037); absent / 0 = stock; else RA2_AI_BOOST
BuildOffAlly=yes
MCVRedeploy=yes
AITakeover=no        ; network: a player who drops is taken over by the AI instead of defeated

[Tunnel]             ; network only
Address=127.0.0.1
Port=50000
Version=2            ; 2 = CnCNet tunnel v2 header (the Rallypoint agent), 3 = v3
Token=a1b2...        ; 32 hex digits from the agent (`arm_tunnel`); required for the Rallypoint agent

[Player0]            ; up to [Player7]; slot 0 must be human in a network game: it is the host (lockstep master)
Type=human           ; human, ai, or closed (a missing section is closed)
Name=Alice
Country=Americans    ; Americans..Russians, YuriCountry (index 9), an index 0-9, or random
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

- With `Token=` (Rallypoint): before anything else it sends `[u16 BE 0xFFFF][u16 BE 0xFFFF]RPAU` + the 16-byte
  token. That is not a valid v2 header (sender equals receiver). The agent answers with the 8-byte prefix alone
  and then only hears this endpoint; keep-alives wait for that answer.
- Every game datagram is `[u16 BE sender id][u16 BE receiver id][payload]`. The payload is the engine's own
  `[u32 LE checksum][packet]`, at most 640 bytes (636 + 4), so well under the agent's 1020. The agent and relay
  drop anything that is not such a packet (`is_ra2_game_packet`).
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

## Multiplayer harness

Automated two-client proof of the Rallypoint RA2 path (relay room → launcher lobby → tunnel token → game
packets), without the Maps UI.

**Layer A (CI):** always runs as part of `cargo test -p generals-core --test agent` (`red_alert_2_*` tests) and
as the default example:

```sh
cargo test -p generals-core --test agent red_alert_2_
cargo run -p generals-core --example ra2_mp_harness
# or: sh scripts/ra2-mp-harness.sh
```

That spins an in-process relay, two agents in `ra2:harness`, drives `ra2lobby` (map + ready + start),
`arm_tunnel`, token auth (`RPAU` + 16 bytes), proves a spoofed local peer is ignored, and that only
`is_ra2_game_packet` payloads cross. Logs under `/tmp/ra2-mp-harness` (`RA2_MP_LOG`).

**Layer B (optional, real engines):**

```sh
# engine must include patch 0006 (Token=). Rebuild if ~/rp-build/ra2engine is older:
sh ra2/patches/apply.sh
cmake -S ra2/engine -B ra2/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build ra2/build && mkdir -p ~/rp-build && cp -f ra2/build/ra2engine ~/rp-build/ra2engine

RA2_DATA=~/rp-steam/game/RA2 RA2ENGINE=~/rp-build/ra2engine \
  sh scripts/ra2-mp-harness.sh --real --secs 25
```

Writes `host.ini` / `guest.ini`, starts two `ra2engine --launch` processes (via `xvfb-run` when present),
keeps the agents alive, and samples tunnel kbps. Still manual: confirming both clients stay in sync in a
long fight, and any UI/Maps-page flow.

## CnCNet Game Options (ideas only)

The CnCNet YR client's `INI/Game Options/` overlays are a useful checklist of lobby toggles. We do **not**
ship Phobos, Ares, Syringe, `gamemd-spawn.exe`, or the GPL client. What we reimplemented as plain launch-file
keys (and Play/Online UI) so far:

| CnCNet option | Our key / UI |
|---|---|
| Crates on/off + density (`CrateMinimum` 8 / 40) | `Crates=` + optional `CrateMinimum=`; density Off/Normal/Increased/Extreme |
| Super Weapons on/off | `SuperWeapons=` |
| Allies Allowed | `AlliesAllowed=` |
| Auto Repair (`[IQ] RepairSell=0`) | `AutoRepair=` |

Left for later (need more engine work or big rules overlays): SW offensive/defensive only, Brutal/Extreme AI
INIs, No Spy / No Dog Engi Eat / Map Code modes.

## Not verified

- A Linux Xvfb `--launch` skirmish as `Country=9` / `YuriCountry` (map `xmp31s2.map`, real YR data) loads the
  Yuri roster and AI (`side=Yuri`, `conyard=YACNST`); Slave Miner gather (deployed YAREFN) and skirmish mind
  control were not proven in play. Gattling stage math is covered by `test_gattling` (synthetic rules); a live
  YTNK spool-up in Xvfb is still optional. Network play end-to-end with two real engines is covered optionally by
  the harness above (`--real`); the V2 tunnel path is also covered by a unit test (`mp: lobby through a CnCNet V2
  tunnel`) against a minimal forwarder.
- macOS is built only by CI.
