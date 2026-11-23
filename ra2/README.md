# Red Alert 2 engine

Rallypoint's Red Alert 2: Yuri's Revenge engine. It is a from-scratch C++17 + SDL2 reimplementation by
omarelarab, built in CI for Linux, macOS and Windows and shipped beside the launcher as `ra2engine/`.
The player brings their own Yuri's Revenge files; nothing of the game is in here.

## Layout

- `docs/gamemd-parity-backlog.md`: prioritized gaps vs retail `gamemd.exe` (tiers 1–4; docs only).
- `engine/`: a trimmed plain copy of https://github.com/omarelarab/ra2-engine at
  `a8cfdccfef8592c13a6517c3fb76d834b1534027` (2026-10-14). It is the pristine base: don't commit patched files.
  Licence: 0BSD (`engine/LICENSE`); `engine/THIRD_PARTY_NOTICES.md` lists what it carries (doctest, stb).
- `patches/`: our changes, applied with `sh ra2/patches/apply.sh` (idempotent, same rules as `patches/apply.sh`; a patch that fails to apply stops it with a non-zero exit; CI applies and builds the series on every PR).
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
| `0047-slave-miner-undeployed` | Undeployed SMIN runs slaves (VehicleTypes `Enslaves=`; gamemd `FUN_006F3FC0` / `FUN_006AF1A0`); killer-house liberation via `killed_by`. Save v57 (Unit/UnitTypeDef fields). `sizeof(World)` unchanged. AI `AISlaveMinerNumber` scoot deferred. |
| `0048-mc-cursors-bio-eject` | MC/own units: Enter cursor + `Mission_Enter` into Grinder/Bio Reactor (`FUN_0051E010` / `FUN_004598A0`); Deploy on a selected Bio Reactor ejects via `UnloadOccupants` (`FUN_0044D880` / `FUN_00457DD0`) without flipping the plant to neutral. |
| `0049-ai-slave-miner-scoot` | AI Slave Miner scoot/redeploy: `[General]` `AISlaveMinerNumber` (FUN_004FEACA) + `SlaveMinerShortScan`/`LongScan`/`ScanCorrection`/`KickFrameDelay` (FUN_0066D530 / FUN_006AFD60 / FUN_006B1020). Computer houses build toward the difficulty target and move SMIN / slide YAREFN toward ore. Save v58; `sizeof(World)` +16 (10248 MinGW). Sim change (AI production + AI-only scoot). |
| `0051-smin-yarefn-deploy-transplant` | SMIN↔YAREFN SlaveManager brain-transplant (`FUN_006AF580`): deploy (`FUN_007383E0` @0x739956 after `FUN_006B0D10`) and undeploy (`FUN_00449C30` @0x44A03C) retarget SLAV workers and copy spawn/regen/guard timers. `order_undeploy_building` for Unsellable YAREFN. Sim change; save format / `sizeof(World)` unchanged (v58). |
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
| `0045-magnetron-disc-beam-art` | Presentation: Magnetron `IsMagBeam` MagnaBeamColor wave (`WaveClass::Draw_Magnetic` FUN_00762070) while `IsLocomotor` holds; Floating Disc `DrainWeapon` loops `DrainAnimationType` (DISKRAY) via FUN_0070fd70. No sim/CRC change. |
| `0050-spy-plane-sw` | Spy Plane special (T2.3 / T4.2): NARADR grants `Type=SpyPlane`; Launch `FUN_006CC390` case 8 @`0x6CD66F` → loaner `SPYP` on Mission Approach (`FUN_004155F0`) → Overfly (`FUN_004157C0`) with SpyCameraWeapon Damage=6 / Range=20 / SpyPlaneCameraFrames=16 fog reveal. Sidebar `spypicon` (+ cameomd fallback), `EVA_SpyPlaneReady`. Stock constants (no `SwParams` / save growth). |
| `0055-vehicle-turret-deploy-damage-fires` | gamemd turret AI `FUN_00736990` (hold GuardAreaTargetingDelay+5 after firing, face Destination, TurretSpins, OmniFire no-chase; `Unit::turret_idle_frames`, save v63), building damage fires `FUN_0043c0d0` (random type/start frame, ConditionRed for CanBeOccupied), infantry fear Down/Up gate `FUN_005200b0`. Turret change affects the simulation; fires are presentation. |
| `0052-country-unique-roster` | T2.2: audit Owner/RequiredHouses/ForbiddenHouses vs live rulesmd for all 10 countries. AI `*BaseDefenses` filtered by ownership; `AIBuildThis` Combat buildings (French Grand Cannon `GTGCAN`, Gap Generator, …) enter the AI defense catalog; Yuri `YADOG`/`YDOG` on the AI unit roster; `rp_country_allows` shares `country::country_allows`; doctest matrix + realplay probe. |
| `0058-magnetron-toss-disc-over-base` | T2.4 vs gamemd. **Magnetron:** the drop is a `CrushWarhead` blast at the landing point owned by the Magnetron (Release `FUN_0070FEE0` stamps victim +0x428/+0x42C; UnitClass landing 0x746209..0x7462DA bullet + `FUN_004690B0`), so any building under the victim is hit, not only the targeted one; `[CombatDamage] CrushWarhead` read into `AbilityRules`. **Floating Disc:** DrainTarget/DrainingMe pair (`DrainStart FUN_0070FD70`, `DrainEnd FUN_0070FE50`) that outlives the FireOnce shot (`SetTarget(NULL)` @0x6FDF97); live Disc-over-base check on `Frame % 16 == 2` (UnitClass::AI 0x7361A9); ally owners end it (0x6FA1D6); money on `Frame % DrainMoneyFrameDelay` once per building (0x6FA167). Save v63. Sim change. |
| `0059-spy-plane-cameo-eva-and-build-limit` | **Spy plane:** 0050 looked for `cameomd.mix` inside `language.mix`, where it is not (it is in `langmd.mix`), so the SW slot fell back to the Nuke cameo ("NUKE ATTACK"); YR cameos now load from `langmd.mix/cameomd.mix` and a SW without one draws a plain tile, never the Nuke's. Each spy camera shot reused `SwEvent::Dropped`, which played "Reinforcements have arrived" per shot: it now plays `SpyPlaneCamera`, and a paradrop says it once per drop (450-frame gap), not once per man. `EVA_SpyPlaneEnRoute` on launch. **BuildLimit=:** read for buildings (`BuildingTypeDef::build_limit`, `build_limit_reached`): `can_start` refuses and the sidebar hides a building the house already owns up to its limit (Nuclear Missile Silo: 1). Not in the save format. Sim change for AI too (same `can_start`). |
| `0060-tesla-trooper-recharge-sound` | A Tesla Trooper feeding a coil (patch 0018's `AssaultBolt` loop in `world_w5.cpp`) played its Primary's report (`TeslaTroopAttack`) with no bolt, because charging has no target and `fired_weapon` picks slot 0. It now fires the Secondary: `Report=TeslaTroopRechargeCoil` (soundmd `iteschaa`, Volume 15, Limit 3) and the alternate-colour bolt to the coil. gamemd never plays `[AudioVisual] TeslaCharge` (the only reader of Rules+0x704 is the loader), so the weapon Report is the whole sound. Presentation only. |
| `0061-build-limit-in-save` | `BuildingTypeDef::build_limit` (0059) joins the save visitor and `kFormatVersion` is 64, which the save-coverage test requires. Old RA2 saves will not load. |
| `0062-hidpi-pointer-edge-grab-window-size` | HiDPI follow-up (SDL already maps pointer, wheel and finger events through the logical size and the window/drawable ratio; this covers what it does not). The pointer is clamped to the canvas, so a cursor over a letterbox bar stays drawn at the edge and still edge-scrolls. The edge-scroll band is 24 screen pixels (`edge_scroll_band(fit)`, 12 canvas pixels at 2x), not 24 canvas pixels (48 or 72 screen pixels on Retina and 4K). Fullscreen with focus grabs the mouse (edge scrolling no longer leaves for a second monitor, like cnc-ddraw's cursor lock). A display change (a window dragged to a monitor with another scale) re-fits the canvas like a resize. On Windows the first window, when the launcher gave no size, is scaled by the display DPI in quarter steps and kept inside 90% of the display (`scaled_window_size`): a per-monitor-aware 1280x720 window was 2/3 the size it had under DPI stretching. Tests in `tests/test_logical_canvas.cpp`. Not tried on a HiDPI display. |
| `0071-hidpi-sidebar-and-menu-fonts` | Automatic picture size on big screens: `desktop_pixel_scale` is 1 up to 1299 lines, 2 from 1300 (1440p, ultrawide 3440x1440 gives a 1720x720 canvas so the fixed 168 px sidebar is 336 screen pixels; 4K, Retina), 3 from 2201 (5K); the zoom-out keeps the wider view. Menu art, pages and the shell font are drawn with linear filtering (`render_copy_smooth`) instead of nearest, and the options labels use the exact glyph scale (it was rounded to a whole number, so a 12 px label jumped from 16 to 32 px tall at 1.5x). Glyph shapes still come from the player's own `12metfnt.fnt`; no vector font. Not seen on a real ultrawide display. |
| `0063-yuri-loading-palette-and-campaign-trigger-side` | A Yuri player's skirmish loading screen drew nothing: `ls800yuri.shp` exists in `loadmd.mix` but no per-country palette does (`mplsy*.pal` absent), so the art was skipped; it now uses `mplsobs.pal` (colours not checked on a real screen). A campaign AI house of the third side gets trigger side 3 (`yuri::trigger_side`), not 1. |
| `0070-yuri-psychic-tower-and-cloning-vats` | Psychic Tower: a building with a MindControl primary holds victims (`BuildingTypeDef::mc_capacity`/`mc_infinite`, `Building::mc_victims`, controller id `kTowerCtl + building`), and lets them go when it dies, is captured, sold or loses power. Cloning Vats (`Cloning=yes`): each infantry unit a barracks trains gets a free copy. Save v65. Sim change. |
| `0072-building-damaged-one-predicate` | One damaged-art test, `building_damaged(b, ConditionYellow)`, for the main frame, the anim and bib layers and the fire overlay. They used `hp <= yellow * max_hp` in one place and `hp / max_hp <= yellow` in another, so the layers could disagree by one hit point at the threshold. Presentation only. |
| `0073-fog-passengers-do-not-reveal` | A passenger or garrisoned infantry no longer reveals the map around the cell it was last on (a parachutist still does). The terrain pass no longer dims explored-but-not-visible tiles on top of the FOG.SHP post-pass, which darkened fog twice. Visibility feeds the AI, so this is a sim change. |
| `0074-minimap-orders` | Radar clicks behave like clicks on the map: right click orders the selection, an armed superweapon, repair or sell takes the click, Ctrl+Shift attack-moves, Alt force-moves (`Input::radar_order`), and a held left drag keeps scrolling. The pixel to cell maths is `RadarMap` (`radar_map.hpp`, tested), centred on the cell. |
| `0075-plane-crash-gravity-and-spiral` | A shot-down fixed-wing plane keeps its forward speed, spirals and accelerates under gravity (it used to drop at a constant rate and hit the ground in under a second) and trails red damage smoke until impact. |
| `0076-leave-without-shell-shows-score` | A `--launch` game has no main menu, so Abort or leaving after a multiplayer match found no scene to go to. It now shows the score page, whose Continue ends the process (`gf_leave_scene`). |
| `0077-group-formation-slots-stay-behind-walls` | `nearest_free_cells` takes formation slots only from cells connected to the goal within a 12 cell box, so part of a group is no longer sent to the far side of a wall; it falls back to any walkable cell when the region is too small. Tests in `test_group_move.cpp` and the new `test_pathfind_scenarios.cpp`. Sim change. |
| `0078-yuri-genetic-mutator-dominator-reveal-chaos-gas-overload` | Yuri's remaining specials. Genetic Mutator (`GeneticConverter`, strike kind 4: infantry in `MutateExplosion` turn into the Brute, same house and health fraction), Psychic Dominator (kind 3: at `DominatorFireAtPercentage` of the head animation the warhead hits and enemies in `DominatorCaptureRange` change sides for a while), Psychic Reveal (clears the shroud in `PsychicRevealRadius`), Chaos Drone gas (`Psychedelic=yes` warheads confuse units in `CellSpread`, which then take every unit for an enemy; no damage) and the Master Mind overload (`OverloadCount/Damage/Frames`). Durations the game does not expose are our guesses and marked `[RE-FALLBACK]`. Sidebar icons for the three specials, skirmish roster accepts them. `MapFile::blank` for synthetic test worlds. Save v66. Sim change. |
| `0079-world-sizeof-not-pinned` | The hard `sizeof(World)` pins in `test_saveload.cpp` failed CI on almost every change to those classes. They are now only reported; the source scan in the same file still checks every member against the save visitor. |
| `0081-building-anim-active-stays-in-the-shp` | Random damaged-looking buildings at full health (macOS report, Allied Construction Yard in its wrecked art, health bar full). The ConYards' art says `AnimActive=0,26,3` and the depots' `0,7,2`, but their main SHP has 3 images (4 for `YACOMD`): `0041` read the cycle as frames of that SHP and clamped stages 3..25 to the last image, so a healthy yard flickered through the damaged art as the stage turned. A cycle that does not fit inside the SHP's image half is not a cycle of the main image (those stages belong to the `ActiveAnim` overlay): the static frame is used (0 healthy, 1 below ConditionYellow). Verified against `Shape_Number` (`FUN_0043ef90`: it returns the stage raw when healthy and stage + 1 or + the anim length when damaged, so a stage beyond the SHP cannot be a frame). The frame choice is now one pure function, `building_main_frame` (occupiable, gate, AnimActive), that `pres_building_main_frame` calls. `tests/test_building_frames.cpp` runs every `[BuildingTypes]` entry of the real `rulesmd`/`artmd` and SHPs (389 types) at full health over 400 AnimActive ticks and below Yellow, and asserts healthy art / damaged art. Dev aids: `RA2_AUTOSHOT_FRAMES=300,900` with `RA2_AUTOSHOT_DIR` writes frames of any scene; `--gf-play yuri` and the other skirmish scenarios work again (the driver clicked the lobby where it no longer is) and shoot the deployed yard over several ticks. |
| `0085-hidpi-native-world-and-interface-scale` | HiDPI, world and interface scaled apart. The interface (sidebar, text, menus) keeps a whole k screen pixels per canvas pixel (0071); the world starts at native pixels (`zoom_start` = 1/k), so a 3440x1440 or Retina window shows twice the map at 1:1 sprites instead of a magnified, soft picture. `RA2_UI_SCALE` / `RA2_WORLD_ZOOM`, zoom range 1/k to k screen pixels per world pixel (also above 1 canvas pixel), start camera and campaign pan use the world-view size. Dev hook: `RA2_AUTOSHOT_DIR` writes `game.png` after 150 frames of a match. Checked on Linux with Xvfb at 3440x1440, 5120x3200 and 1920x1080; not tried on a real Retina Mac. |
| `0090-menu-font-stb-truetype-and-liberation-sans` / `0091-menu-text-in-truetype-at-device-size` | Menu and settings text (main menu, Options, in-game Game Options / Sound / Keyboard, lobby pages, popups: everything drawn through `ShellPainter` and `draw_shell`) is rasterised at the real pixel size from Liberation Sans Bold (SIL OFL, embedded unmodified, licence in `third_party/fonts`) with stb_truetype, drawn 16% narrow so it matches the 12metfnt proportions (measured against the player's own font in `tests/test_ttfont.cpp`: widths 1.00 to 1.11, capitals 9.5 vs 10 rows), colour from the same palette ramps (gold, control yellow, disabled red) with a top-to-bottom shade and a shadow. In-game HUD text keeps the game's own fonts. `RA2_NO_TTF=1` forces the bitmap path (also the fallback if the face fails to load). `--text-shot <prefix>` (`RA2_SHOT_SIZES=1920x1080,3440x1440`) renders those pages with the real assets. |
| `0095-pathfind-heuristic-verified-euclid` | Decompiled `FUN_00429A90`, `FUN_0042A460` (`f = g + FUN_004CAC40(dx*dx + dy*dy)`, table-driven float `sqrt`) and `FUN_00429830` (`Can_Enter` cost -> `DAT_0081870C` + tie-break `DAT_0081872C`) from `gamemd.exe`: the A* heuristic is Euclidean, not Chebyshev (the only `max(|dx|,|dy|)` in `FUN_00429A90` is the 9th tunnel/link neighbour). Fixes the RE comments and adds nearest-wall tests. |
| `0096-start-force-needs-an-owner` | `FUN_006880F0` picks a skirmish house's starting force from types whose `Owner=` lists the house's country (`start_pool_allows`). `gf_start_pools` previously used `country_allows()`, which treats a missing `Owner=` as everyone, letting `[DeathDummy]` (no `Owner=`, no `TechLevel=`, `Cost=0`) make up around half of every starting force as invisible dummy vehicles that blocked harvesters and base cells. Sim change. |
| `0097-straighten-route-staircases` | Like retail's post-A* pass (`FUN_0042B7F0` / `FUN_0042BE20` / `FUN_0042B210`, called by `FUN_00429A90` on the finished `PathType`), `straighten_runs` rewrites single-step diagonal/orthogonal staircases (e.g. `E SE E SE`) into their diagonal steps followed by their straight steps (or straight first when blocked), checked with the search's own step test. Sim change. |
| `0098-movement-probe` | `ra2engine --probe-move [map.map]`: headless real-data movement harness across 16 unit types from the player's `rulesmd.ini` (route length vs shortest, turns, staircases, speed vs `Speed=`, stalls, group moves, harvesters). |
| `0100-real-data-tests-run-on-linux` | Real-data tests in `test_map.cpp`, `test_pkt.cpp`, `test_mix.cpp`, `test_asset_fs.cpp` and `test_audio*.cpp` now use `ra2fixture::install_file` so they run on Linux when `RA2_INSTALL_DIR` is set (and skip `ecache01.mix` when absent, as on Steam); updates `test_input_touch_w6.cpp` for patch 0029's sprite pick and adds a real-map `RadarMap` roundtrip test in `test_radar_map.cpp`. |
| `0101-yuri-dominator-permanent-and-ai-factory-gates` | Verified `PsyDom::Update` (`FUN_0053af40`) and `PsyDom::Fire` (`FUN_0053b080`) in `gamemd.exe`: `PDFXCLD.SHP` has 30 image frames at `Rate=300` (90 game frames; fires at 20% = frame 18), and capture is permanent (`IsPermaControlled` `+0xb1 = 1`, `dominator_hold_frames = 0` by default) over the `CellSpread` octile spiral `DAT_007ed3d0[DominatorCaptureRange]` (`max(|dx|,|dy|) + min(|dx|,|dy|)/2 <= r`, so `r=1` covers all 9 cells of the 3x3 square) while freeing any prior mind-control link (`FUN_00471ff0`). Also gates AI naval and fixed-wing aircraft production start and factory-lost cancellation in `step_ai_teams` on `tc.nav_factory_types` / `air_ft` instead of `tc.veh_factory_types`, and adds `--probe-yuri-sw`, `--probe-crash` and `--probe-start-pool`. Sim change. |
| `0112-engineer-bridge-repair` | Adds an end-to-end regression test for a selected engineer entering a neutral bridge hut, restoring a broken low bridge, and being consumed. Test coverage only; no simulation change. |
| `0113-menu-needle-speed` | The main menu's WARNING monitor "needle" (SDWRNANM, 91 frames) looped in ~1.1 s (12 ms a frame) and read as a fast flicker; it now advances ~1/15 s a frame for a ~6 s sweep. Presentation only. |
| `0114-paradrop-selectable` | Paratroopers can be selected and given deploy orders while descending, so they deploy as soon as they land. Selection is local UI state; no simulation or save-format change. Presentation only. |
| `0115-chronosphere-anim-range` | Building slot anim variants sharing one SHP stop at the next sibling's `Start=` frame, keeping the Chronosphere charge overlay on its healthy art instead of flickering into the damaged frame. Presentation only. |
| `0116-guard-holds-post` | Area Guard keeps its original home cell through target acquisition and anchors G to the cell a moving unit finishes entering. Sim change. |
| `0117-radar-move-cursor` | Show the move cursor over the active radar when units are selected and no command mode is armed. Presentation only. |
| `0118-high-bridge-destruction` | Let normal projectile impacts reach the high-bridge deck-height destruction window instead of double-counting the deck. Sim change. |
| `0119-garrison-firing-test` | Adds asset-independent regression coverage that verifies a neutral garrison changes owner, does not fire at an out-of-range enemy, and fires once that enemy enters range. Test coverage only; no simulation change. |
| `0120-aircraft-flight-crash` | Draw airborne aircraft at level 0 and use terrain only after touchdown, so cliffs cannot add a step to their flight height. Crashing voxel aircraft nose-dive while their existing fall/spin and impact blast run. Adds flight-height and crash-damage regressions. Presentation only. |
| `0121-force-fire-cursor` | Ctrl force-fire over the playfield keeps the Attack cursor even on shrouded or out-of-range ground. Presentation only. |
| `0122-chrono-erase-building-visual` | Buildings being erased by a Chrono Legionnaire draw at 50% opacity and emit `CHRONOSK` sparkles every 24 ticks when visible. Presentation only. |
| `0123-slave-miner-movement` | Slave workers keep their reserved delivery pad while returning, so their move path is not reset every frame and they can deliver ore. Sim change. |
| `0124-chrono-warp-effect` | Chrono teleports of anything but a Chrono Legionnaire jump showed no effect: the warp anim was spawned only when a 2+ cell jump happened to coincide with the sim's warp fields, and the Chrono Miner's warp home never sets them, so the miner arrived with just its sound. The warp is now drawn from the events both paths already report (the World's teleport event, and the Chrono Miner's `ChronoIn` harvest event): Rules `WarpOut` at the cell left and again at the cell arrived on, 50 % translucency and `ChronoSparkle1` for the warp's length, and Rules `WarpAway` when an object is erased (the `Erase` event had no visual). The four anim names come from `[General]` `WarpIn`/`WarpOut`/`WarpAway`/`ChronoSparkle1` instead of hard-coded `WARPOUT`/`CHRONOSK`, keeping the stock name when a key is absent. Presentation only: simulation, saves and sync untouched. Not looked at on screen yet; the art `YSortAdjust`/`ZAdjust` of these anims is still not applied (see the note in `pres_fx.inc`). |
| `0057-ai-call-for-help-leftovers` | T1.3 leftovers from `FUN_00708080`: victim SlaveOwner (`+0x2DC`) skips the vehicle pass, `+0x2E4` is the Tank Bunker link (loaded transports answer), per-unit ArchiveTarget==self (`archive_self`, save v62), recruits guard around the victim, zone test `FUN_0056D100` as an integer flood (`reach_mask`). Changes AI play. |
| `0056-hidpi-text-and-zoom-out` | HiDPI and zoom, SDL2 only (same code on Linux, Windows, macOS). Window: `SDL_WINDOW_ALLOW_HIGHDPI` on every desktop, Windows per-monitor DPI awareness (`SDL_HINT_WINDOWS_DPI_AWARENESS`), size read with `SDL_GetWindowSizeInPixels`. The match canvas is the window's pixels over a whole number (`desktop_canvas`: 1080p and 1440p 1x, 4K and Retina 2x, 5K 2x), so each canvas pixel is exactly k x k screen pixels. `Platform::begin_frame/present` pick the path per frame (`present_mode`): a whole scale uses SDL's integer scale (no resampling); a fractional one (1280x720 menus on 1080p, a window smaller than the canvas) draws into a canvas texture, enlarges it with nearest sampling and filters it down (sharp bilinear). The credits text is 1:1 on the canvas (the old `hud_present_scale` guess is gone). `RA2_LOGICAL_HEIGHT` now works (480..2160). Tactical zoom out, presentation only: wheel (pointer-anchored, trackpad fractions summed to notches), PgUp / PgDn; zoom 1 down to 0.75 at 1 screen pixel per canvas pixel and 0.5 at 2 or more (`zoom_min`), steps of 1/8. The world and its overlays draw into a texture of canvas / zoom and are scaled onto the playfield; the pointer, `Input`, the camera clamp, the radar viewport box and cursor picks work in world-view pixels (`zx`/`zy`). Simulation, commands, saves and sync untouched. Screenshot hook `RA2_AUTOSHOT_DIR` reads the real frame size. Tests in `tests/test_logical_canvas.cpp`. |

### In-game zoom

Mouse wheel or trackpad scroll zooms the tactical view out (anchored at the pointer) and back in; PgUp and PgDn do the same from the keyboard. The limit scales with the screen: 0.75 where a canvas pixel is one screen pixel, 0.5 on 2x displays, so tiles stay legible. It never touches the simulation, saves or network.

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
The launcher reads the ten countries' 47×23 official flag pictures (`usai.pcx`..`rusi.pcx` from `ra2.mix/local.mix`, `yrii.pcx` from `ra2md.mix/localmd.mix`) at runtime from the player's install (`launcher/core/src/ra2flags.rs`, Tauri command `ra2_country_flags`) and never ships or writes EA art to disk.

### Country bonuses (all 10)

- **Ownership**: sidebar, AI catalogs and skirmish start pools honour `Owner=` / `RequiredHouses=` / `ForbiddenHouses=` (Americans `AMRADR`/`ORCA`, Korea `BEAG`, French `GTGCAN`, British `SNIPE`, Germans `TNKD`, Libya `DTRUCK`, Iraq `DESO`, Cuba `TERROR`, Russia `TTNK`, Yuri `YADOG`/`YDOG`; `GAAIRC` forbidden for Americans). AI also plans country Combat `AIBuildThis` defenses (Grand Cannon) and filters `*BaseDefenses` by ownership.
- **Multipliers / veterans**: `country_bonuses.hpp` loads the HouseType keys gamemd reads (`CostUnitsMult`, `VeteranInfantry`, …). Stock YR comments them out (all 1.0 / empty lists); when present they scale production cost/build time, firepower/ROF, spawn speed/HP, and arm listed types as veterans at factory exit and at the UnitCount start force.
- Doctests cover `CostUnitsMult` cost scaling and `VeteranInfantry` arming (synthetic INI).

### What works as Yuri

- Selectable in `--launch` skirmish; human roster is rules-driven for `YuriCountry` (INIT, SMIN, YTNK, YURI, …).
- Yuri buildings load (YACNST, YAPOWR, YABRCK, YAWEAP, YAREFN, …) and AI skirmish arms with `conyard=YACNST`.
- PCV is the BaseUnit; deploy/build tree follows prerequisites.

### Partial / missing

- **Country Armor=/Speed= on every combat path**: spawn bakes Armor into HP and Speed into `speed_cps`; Firepower/ROF go through `house_*_mul`. A mid-game rules reload does not refresh stored bonuses (re-arm is at skirmish setup).
- **Slave Miner**: deployed YAREFN and undeployed SMIN both host SlaveManager (`Enslaves=SLAV`; ModEnc's BuildingTypes note means the Enslaves *type* is infantry). Killer-house liberation on master death. AI `AISlaveMinerNumber` + Short/Long scoot (`0049`). SMIN↔YAREFN deploy transplant (`0051` / `FUN_006AF580`) keeps slaves across vehicle↔building. Remaining: no Xvfb Yuri skirmish check in CI.
- **Mind control**: skirmish/MP CaptureManager (`step_mind_control`); campaign keeps its own path. Grinder (`grind_now`) and Bio Reactor (`absorb_now`) free the MC slot (patch 0022); Enter cursor / `Mission_Enter` into Grinder and Bio Reactor, and Deploy eject from a selected Bio Reactor (patch 0048).
- **Gattling** (`IsGattling` / YTNK, GTGCAN): RateUp while engaging, RateDown when idle (0 resets); Stage/EliteStage endpoints pick Weapon1..N (odd AG, even AA). Doctests cover the timer and slot pick.
- Yuri structure/unit cameos load from `langmd.mix`/`cameomd.mix` (patch 0016); RA2 cameos (`XXICON`, `FIXICON`, …) are on the same AssetFS list via `language.mix`/`cameo.mix` (patch 0046). Types with no `Cameo=` in artmd (e.g. YACOMD, YAPPPT, YAROCK) fall back to `AltCameo`, `Image`+`ICON` when that SHP exists, else retail `XXICON`.
- **Magnetron**: lift/hold/drop via `IsLocomotor` (patch 0017); MagBeam art while lifting (patch 0045); the drop is a Crush blast at the landing point, so a victim tossed onto a building damages it (patch 0058). What sets the held victim's Jumpjet destination in gamemd was not found; the drag toward the targeted building is still ours (its arrival-then-drop matches Jumpjet `0x54C1BD`).
- **Floating Disc**: DrainWeapon power/money drain (patch 0018); DISKRAY anim while draining (patch 0045); gamemd drain lifecycle with the live Disc-over-base check every 16 frames, ally stop and one payout per building (patch 0058). Money/delay use the stock `[CombatDamage]` 30/30 constants (not read from rules).

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
| `--width <w> --height <h>` | Window size. Menus stay a 1280x720 canvas, letterboxed. The match canvas uses the window's pixels (720 lines minimum, 1440 maximum, HiDPI windows divided by 2 or 3) so a 1080p or 1440p window shows more of the map; width is that height times the aspect, kept between 16:9 and about 21:9. Passing this ignores RA2.INI's size. |
| `--fullscreen` / `--windowed` | Desktop fullscreen at the display's size, or a window. |
| `RA2_UI_SCALE` | Interface size: 1 to 3 screen pixels per interface pixel (launcher: Settings > Display > Interface size; never under 720 lines). `RA2_WORLD_ZOOM` (1, 1.5, 2; launcher: World zoom) makes units bigger than the screen's own pixels. `RA2_LOGICAL_HEIGHT` still sets a canvas height (480 to 2160), but then the world is no longer pixel-exact. Without them the canvas is the window's pixels divided by a whole number (patch 0056). Menus stay 1280x720. |
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
