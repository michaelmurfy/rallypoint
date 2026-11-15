# Fixes from TheSuperHackers/GeneralsGameCode

The engine is based on [TheSuperHackers/GeneralsGameCode](https://github.com/TheSuperHackers/GeneralsGameCode) as it was around **2026-05-15** (through the macOS port in `engine/`). Upstream had 539 newer commits by 2026-10-25. Fixes are taken as patches (`patches/0034`-`0040`, each carrying the upstream commit and title in its first line), after checking with `git apply --check` against our tree. Test a candidate with:

```bash
git clone --shallow-since=<date> https://github.com/TheSuperHackers/GeneralsGameCode up && git -C up show <sha> -- Core GeneralsMD > /tmp/c.diff
git apply --check -p1 --directory=engine /tmp/c.diff   # after sh patches/apply.sh
```

## Taken

| Patch | Upstream | What it fixes |
|---|---|---|
| 0034 | `82f4b857a` #2796 | Spurious checksum mismatches for disconnected players at low CRC intervals (sync logs run at 10) |
| 0035 | `9a402fbe2` #2742 | Particle systems no longer feed the logic checksum: the logic random numbers they used are drawn whether or not a particle system gets created (it can be refused by detail settings or limits, which differ per machine) |
| 0036 | `50f27e057` #3055 | The same for the Spectre Gunship |
| 0037 | `4a4a1b23f` #3499 | The LAN lobby player is deleted when they leave the lobby |
| 0038 | `12a9b944b` #3311 | A game slot's IP is cleared on reset |
| 0039 | `a8464bcff` #3498 | The disconnect chat message is released after sending |
| 0040 | `5a4187cea` #2708 | The type of incoming game messages is verified |

### 2026-11-15 AI survey (same head)

`patches/0095` takes the AI and pathfinder fixes that apply cleanly. They change how the AI plays, so `patches/0096` raises `kRallypointSimulationId` to 8.

| Upstream | What |
|---|---|
| `2119cc9b` #3438 | AI no longer leaves a temporary state that reset itself during its own update |
| `1b1212f0` #3426 | Uninitialised variables in `AIPlayer::getPlayerStructureBounds` |
| `b0c29eba` #3379 | Uninitialised variable in `AIPlayer::onUnitProduced` |
| `ec0658a3` #3098 | Infantry forced out of a vehicle no longer path to their target |
| `78edd877` #2197 | Units attacked in guard mode behave correctly |
| `dc540878`, `55ccd620` #3113 | Pathfinder crashes: dangling cell list pointer, closed list head without info |
| `039bd17f` #3296 | Crash in `Pathfinder::processHierarchicalCell` |

Not taken: `5430d9de` #3380 (supply attack detection), `1abf73d5` #3332 and `8207c684` #2296 (pathfinder movement) do not apply; they need the newer pathfinder files. `bfa3bbf4` only restores compatibility for a change we do not have.

### 2026-11-16 performance survey (upstream head `e655cb28`, 2026-11-15)

Looked at every `perf(...)` commit since the base while finding out why a three-player Art of Defense game ran at 15 frames a second (see `patches/0112` to `0115` in `agent.md`). None of them is the cause of that game's slowness, and upstream has none of the fixes in those patches, so nothing was taken this time. For the next round:

| Upstream | What | State |
|---|---|---|
| `288a3ea4`, `e61aa09d`, `fc278ed2` #3198 | Pathfinder: no repeated crusher test, parent position out of the loop, faster append to a path | apply cleanly, not taken (pathfinding was 0 to 2% of this game) |
| `d37051f6` #2791 | `Pathfinder::checkForAdjust` skips a redundant path-exists check | applies cleanly, not taken |
| `8a794058` #3096 | `ScriptEngine::findScript` about 85% cheaper | applies cleanly, not taken |
| `f06f1838` #2761 | Waypoint and polygon trigger labels 90% cheaper | applies cleanly, not taken |
| `deb728c0` #2700 | Game message arguments in vectors | applies cleanly, not taken |
| `b1fec67c` #3613 | `XferCRC` without the byte swaps | applies cleanly, not taken: check first that the checksum value stays the same |
| `34ff975b` #3266 | `preMainInitMemoryManager` inline | applies cleanly; the rest of #3266 (`e2a2c933`, `1fe815cd`, `9977605b`) conflicts |
| `de20ae0c` #3155 | Particles of one type drawn in one batch (15 to 30% of particle drawing) | conflicts (Enhanced renderer changes in the same files): worth a hand port |
| `923bb911` #2854, `d95a9f8c` #3140 | Sorting renderer and smudge early returns | conflict |
| `28170001` #3258, `36a1a68b` #3270, `b3d3a062` #2992, `810371bb` #2659 | Terrain flatten bounds, production cancel, view cache, transport loop | conflict |

Anything taken from this table has to pass the replay check in `agent.md` (same sync logs before and after) or raise `kRallypointSimulationId`.

### 2026-11-14 survey (upstream head `f5e823ba`, 2026-11-12)

`patches/0088` takes the fixes below, all of which applied cleanly and leave the simulation alone (`kRallypointSimulationId` unchanged).

| Upstream | What |
|---|---|
| `d6c32650` #3546 | LAN game tooltip crash after the host left |
| `c96b610f` #3633 | Crash in `LocomotorStore::reset` when `map.ini` defines a locomotor |
| `02b5b56e` #3489 | Rivers that overflow the dynamic index buffer are skipped |
| `3d82630f` #3529 | Weapon slot index sanity check |
| `9298f33b` #3361 | Missing null terminators in `FieldParse` arrays |
| `a30d0613` #3612 | The leave is sent at once when quitting from the disconnect screen |
| `afc2a103` #3641 | Held modifier keys are released when keyboard focus resets (relevant to the macOS dead-keyboard reports) |
| `d1105e30` #3651 | `Matrix4x4` comparison without undefined behaviour |
| `6e4a328b` #3346 | Stack overflow in the firewall helper |

Written by hand: the BIG archive reader (`StdBIGFileSystem`, `Win32BIGFileSystem`) now refuses an entry name that does not end inside the buffer and the file, a file count that cannot fit the file, and entries outside the archive. Mods are downloaded files, so before this a bad `.big` overran a stack buffer. Upstream's own version is `c7b7814e` #3588; it needs the newer file layout.

Left for later, because they change how the game plays (each needs `kRallypointSimulationId` raised and a real game): `ea40abbd` #3533 (hackers keep hacking on Stop), `c529745c` #3602 (Emergency Repair under an Ambulance aura), `d1bd190a` #3003 (2D diagonal movement speed), `f3730001` #3282 (Helix experience), `b7f0e31c` #3544 (wander destination), `1abf73d5` #3332 (path after allies move), `ba7a17a6` #3433 (group force attack), plus the save/load fixes (#3117, #3127, #3145, #3148, #3151, #3491, #3492, #3130), which matter only once multiplayer save/load exists.

Do not apply yet (need commits we lack): `5d69c00a` #3486 (map sizes that overflow the cell arrays: our `MapReaderWriterInfo.h` is under `GeneralsMD/`), `88faea1a` #3090 (replay frame range), `c0031256` (repeat commands in a net packet), `fa16f49d` #3603 and `7c414328` #3566 (missing texture), `0c9f66b5` #3344 and `aee68d6e` #3436 (INI parsing, near our 0016), `e2a1216b` #3609 (8-byte pool alignment, matters on x64), `2bdb1bee` #3459 (x64 pointer truncations: overlaps 0079), `63c78982` #3530 (path case), and the UB clean-ups #3658, #3639, #3648. The network and replay ones are the most worth hand-porting next.

`0041` raises `kRallypointSimulationId` to 4: 0035 and 0036 change when the logic random numbers are drawn, so builds from before must not join builds from after.

## Does not apply yet (needs commits we do not have)

| Upstream | What | Why it matters |
|---|---|---|
| `ff6ac21aa` #2857 | Map player index to slot index for CRC validation | Checksum attribution in games with AI or empty slots |
| `48e9fe9c8` #3039 | LAN game room hang from long player names | Our lobby name limit is 12 |
| `2d6761cc0` #3488 | Close sockets that fail to bind | Network |
| `9a9b1c2b2` #2736, `b720c3554` + `5943d3856` #3052, `c00312568` | Network command ordering and packet parsing | Lockstep command order |
| `171701e80` #3212, `a82fab89c` #3180 | Mismatches in Generals caused by update and module changes | Generals only, so not needed for Zero Hour |
| #2670 (`f12cfbbe8`, `6f14ae8da`, `aeeb97c61`, `91baad276`, `65dd652bd`) | Upstream's deterministic math: fdlibm via GameMath, `-ffp-contract=off`, Windows x86 and macOS ARM64 checked equal | Our own `DetMath` (patch 0017) does the same job; a switch needs the five commits and the GameMath dependency |

Larger upstream moves since the base (files moved between Generals, Zero Hour and Core, for example #3189 and #3561) are why a straight merge of upstream is a large job: most of our patches touch moved files.
