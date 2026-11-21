# gamemd.exe parity backlog

Prioritized gaps for cloning a complete modern RA2/YR engine from retail
`gamemd.exe`, grounded in this tree (`ra2/engine/` + Rallypoint patches through
`0025`). Docs only: nothing here is implemented by this file.

Binary reference: Steam YR `gamemd.exe` (tools on the RE VM). Upstream RE notes
lived in `docs/re/` / `docs/gap/` of omarelarab/ra2-engine and were trimmed from
our copy; FUN_* cites below come from comments still in the sources.

**Effort:** S = days, M = 1–2 weeks, L = multi-week / multi-patch.
**OpenRA:** whether OpenRA’s RA2/YR rules or code is a useful *secondary*
reference (never a substitute for gamemd behaviour).

---

## Tier 1 — play-breaking (pathfind / slopes, AI defense, sync, crashes)

### T1.1 Ramp / slope height while walking

Units used to keep the Level they spawned on, so a tank ordered down a paved
stair never left the terrace even when A* allowed the step.

| | |
|---|---|
| **Files** | `engine/pathfind.hpp`, `engine/pathfind.cpp` (`path_step_layer`, `PathTerrain`); unit step in `engine/world.cpp` / locomotion |
| **gamemd** | FootClass height gate **FUN_004D9C60** (comments historically said `FUN_004D9B70`); `CellClass` Level `+0x11B`, SlopeIndex `+0x11C` (TMP hdr `+0x2A`); `IsClearToMove` **FUN_004833B0**; A* neighbours **FUN_00429A10** |
| **Rules / art** | TMP ramp SlopeIndex ≠ 0; deck / bridge Level+4 |
| **Effort** | M (logic in flight / recently patched; still needs real-map sign-off) |
| **OpenRA** | Partial — terrain height models differ; use only for vocabulary |

### T1.2 Cliff / slope *presentation* (unit tilt)

Pathing can be correct while vehicles stay flat on ramps. Upstream README
lists “units tilting on slopes” as a known miss.

| | |
|---|---|
| **Files** | Voxel / facing draw in `engine/main.cpp`, `engine/facing_cache.*`, `engine/locomotion.*`; iso project `engine/iso_project.*` |
| **gamemd** | Drive-track / Facing pitch from cell SlopeIndex (same cells as T1.1); rocket pitch already special-cased |
| **Rules / art** | artmd Facing / voxel sections; TMP slope types |
| **Effort** | M |
| **OpenRA** | No — different render stack |

### T1.3 AI call-for-help / base defense response (remaining RE-OPEN)

`World::call_for_help` ports **FUN_00707500**, but several multipliers and
reachability tests are still stand-ins. `ComputerBaseDefenseResponse` is
read; sell-on-`PowerEmergency` and full IQ repair/sell loops are only partly
consumed.

| | |
|---|---|
| **Files** | `engine/world_w7.cpp` (`call_for_help`), `engine/ai_base.hpp` (`kAiComputerBaseDefenseResponse`, `kAiPowerEmergency`), `engine/world.hpp` (CombatW7Rules), `engine/aimd_parse.*` (`IsBaseDefense`) |
| **gamemd** | **FUN_00707500** (help); ReceiveDamage callers ~`0x701C74` / `0x44223C`; zone reach **FUN_0056CC30**; Rules+`0xB18` |
| **Rules** | `[AI] ComputerBaseDefenseResponse`, `BaseDefenseDelay`, `*BaseDefenses`, `*BaseDefenseCounts`, `PowerEmergency`, `[IQ] RepairSell` |
| **Effort** | L |
| **OpenRA** | Partial — AI defense scripts are analogous, not bit-compatible |

### T1.4 Lockstep sync vs long MP / mixed builds

Per-frame CRC (`net_frame_crc`) is stronger than stock `ComputeFrameCRC` but
deliberately not identical. Maths tables are regenerated (not EA bytes). No
engine build-id gate between peers (lobby used to do that). Long fights with
two real engines are still a manual harness check.

| | |
|---|---|
| **Files** | `engine/net_frame_crc.*`, `engine/net/lockstep.*`, `engine/orig_math.*`, `ra2/README.md` (Multiplayer harness) |
| **gamemd** | **FUN_0064D520** ComputeFrameCRC; **FUN_00647090** Queue_AI_Multiplayer; FRAMESYNC **FUN_00649A70** |
| **Rules** | n/a (sim + RNG streams) |
| **Effort** | L (parity of CRC shape optional; catching clone desyncs is M) |
| **OpenRA** | No — different net model |

### T1.5 Save-format / `sizeof(World)` layout crashes

Patches that grow `World` without re-pinning `sizeof(World)` break MinGW /
save tests (already hit; pin is patch `0023`). Cross-version saves are
explicitly unsupported (upstream README).

| | |
|---|---|
| **Files** | `ra2/patches/0023-world-sizeof-pin.patch`, `engine/tests/test_saveload.cpp`, `engine/save_fields.hpp`, `engine/world.hpp` |
| **gamemd** | n/a (our layout) |
| **Rules** | n/a |
| **Effort** | S per patch that touches `World` (process), L if soft versioning is wanted |
| **OpenRA** | No |

### T1.6 Pathfind cost / occupy parity leftovers

Bounded A* and leak masks exist; RE comments still list Can_Enter cost table
cases and cloaked-cell costs that need live skirmish proof on dense maps.

| | |
|---|---|
| **Files** | `engine/pathfind.cpp` (`find_path_bounded_t`, cost vector), occupancy in `engine/world.*` |
| **gamemd** | Step cost **FUN_004297B0**; cost table `DAT_0081770C`; IsCellOccupied vtable |
| **Rules** | MovementZone / SpeedType interactions |
| **Effort** | M |
| **OpenRA** | Partial — path cost ideas only |

---

## Tier 2 — country / faction completeness

### T2.1 Per-country cost / combat multipliers

`CountryCostMult` is mentioned on the sell path; country section bonuses from
rules (`*CostBonus`, Speed/Armor/ROF/Firepower style keys in gamemd strings)
are not loaded as a first-class house handicap the way difficulty
`FirePower`/`ROF` are.

| | |
|---|---|
| **Files** | `engine/repair_sell.cpp` (comment), `engine/ai_base.hpp` (`DifficultyMods`), house setup in `engine/gf_start.inc` / `engine/main.cpp` |
| **gamemd** | HouseType / country LoadFromINI (bonus fields beside `Side=` / `Prefix=`) |
| **Rules** | `[Americans]`…`[YuriCountry]`: `InfantryCostBonus`, `UnitsCostBonus`, `AircraftCostBonus`, `BuildingsCostBonus`, `DefensesCostBonus`, related Speed/Armor/ROF/Firepower bonuses; `VeteranInfantry` / `VeteranUnits` / `VeteranAircraft` |
| **Effort** | M |
| **OpenRA** | Yes — country traits tables are a readable checklist |

### T2.2 Country unique units / buildings (RequiredHouses / ForbiddenHouses)

Roster filtering exists (probes cover Americans vs GAAIRC, Germans Tank
Destroyer, British Sniper, etc.). Full unique kit + AI catalogs for every
country (French Grand Cannon, Korea Black Eagle, Iraq Desolator, Libya
Demolition Truck, Cuba Terrorist, …) needs a pass against live `rulesmd`.

| | |
|---|---|
| **Files** | `engine/ai_side_catalog.inc`, `engine/realplay_probe.inc`, `engine/realplay_economy.inc`, production sidebar filters |
| **gamemd** | Owner / RequiredHouses / ForbiddenHouses checks in build/AI catalog build |
| **Rules** | those keys on unit/building types; `[AI] Build*` lists |
| **Effort** | M |
| **OpenRA** | Yes — faction production lists |

### T2.3 Country superweapons / para drops

Amer/Ally/Sov/Yuri para-drop infantry counts are wired; Spy Plane is typed
but upstream still lists it as incomplete vs retail feel.

| | |
|---|---|
| **Files** | `engine/w2air_main.inc`, `engine/superweapon.*`, `engine/world_an.cpp`, `engine/world.cpp` (SW dispatch) |
| **gamemd** | SW type table; SpyPlaneSpecial / AmericanParaDropSpecial |
| **Rules** | `AmerParaDropInf`/`Num`, `AllyParaDrop*`, `SovParaDrop*`, `YuriParaDrop*`, `[SpyPlaneSpecial]`, `SpyPlaneCamera*` |
| **Effort** | M |
| **OpenRA** | Partial |

### T2.4 Yuri leftovers (from `ra2/README.md` Partial)

Already patched for playable YuriCountry + many uniques; remaining:

| Item | Files / keys | Effort | OpenRA |
|---|---|---|---|
| Undeployed SMIN slaves | slave-miner patches; ModEnc: infantry leave BuildingTypes only | S–M | Partial |
| Killer-house liberation on miner death | combat → slave free | M | Partial |
| AI `AISlaveMinerNumber` / `SlaveMinerShortScan` scoot | `[General]` SlaveMiner* / AISlaveMinerNumber | M | Yes |
| MC enter cursors / Bio Reactor eject | `engine` capture/MC + patch 0022 | S | No |
| Magnetron building toss + beam art | patch 0017 `IsLocomotor`; presentation | M | No |
| Floating Disc `DISKRAY` anim | patch 0018 `DrainWeapon` | S | No |
| Blank cameos (YACOMD, YAPPPT, YAROCK, …) | artmd `Cameo=`; patch 0016 mix lookup | S | No |
| Xvfb Yuri skirmish CI proof | harness | S | n/a |

---

## Tier 3 — animation / presentation

### T3.1 Building ActiveAnim / IdleAnim / damage fires / SuperAnim

Buildup + SuperAnim hold (patch `0024`) exist; always-on slot anims and
damage-fire overlays are partially gated (`G-85` comments). Some SW buildings
still look wrong if art sequences are mis-bound.

| | |
|---|---|
| **Files** | `engine/pres_bldanim.inc`, building draw in `engine/main.cpp`, patch `0024-namisil-superanim-hold` |
| **gamemd** | Building anim state machine (ActiveAnim…Four, IdleAnim, SuperAnim…Four, DamageFire) |
| **Rules / art** | artmd `ActiveAnim*`, `IdleAnim`, `SuperAnim*`, `Buildup`, `DamageFire*` |
| **Effort** | M |
| **OpenRA** | No |

### T3.2 Infantry sequence completeness

DoControls rows + deploy edges (GI / Guardian GI) are in `inf_anim`; prone /
crawl / fire / idle transitions need parity proof vs SHP sequences for Yuri
and country uniques.

| | |
|---|---|
| **Files** | `engine/inf_anim.*`, tests `tests/test_inf_anim.cpp` |
| **gamemd** | Infantry DoControls / sequence table (42 rows recovered) |
| **Rules / art** | artmd Sequence / PrimaryFire / Deployed* |
| **Effort** | M |
| **OpenRA** | Partial — sequence names only |

### T3.3 Vehicle anims / turret / deploy (Siege Chopper)

Upstream known miss: Siege Chopper deploy. Turret facings and deploy SHPs for
other vehicles still diverge on some types.

| | |
|---|---|
| **Files** | vehicle draw in `engine/main.cpp`, `engine/unit_w6.*`, deploy mission paths in `engine/world.cpp` |
| **gamemd** | UnitClass deploy / turret facing |
| **Rules / art** | `CSCRW` / Siege Chopper art; `Deployer`, turret HVA |
| **Effort** | M |
| **OpenRA** | Partial |

### T3.4 Weapon beams / lasers / prism / magnetron / disc rays

Logic for prism support, magnetron lift, disc drain exists; several beams are
not drawn (`IsLocomotor` beam art, DISKRAY, some fragment beams only via FX
events).

| | |
|---|---|
| **Files** | `engine/building_combat.cpp` (prism), magnetron / disc patches, `frag_events` in W7 pres |
| **gamemd** | Laser / electric / magnetic beam drawers |
| **Rules / art** | `IsLaser`, `IsElectric`, `IsLocomotor`, `DrainWeapon`, anim names |
| **Effort** | M |
| **OpenRA** | No |

### T3.5 Cameo / sidebar icon gaps

Cameos load from cameo / cameomd mixes; types without `Cameo=` stay blank.
SW cameo seating has been a recurring UI bug class.

| | |
|---|---|
| **Files** | sidebar draw in `engine/main.cpp`, patch `0016-yuri-cameo-mix-lookup` |
| **gamemd** | SidebarClass cameo strip / FlashCameo |
| **Rules / art** | artmd `Cameo=`, `AltCameo=`; `cameo.pal` |
| **Effort** | S–M |
| **OpenRA** | No |

---

## Tier 4 — systems (naval AI, aircraft recruit, capture/engineer, campaign, net)

### T4.1 Naval AI (yard placement + ship teams)

Catalog can take `BuildNavalYard` with `AINavalYardAdjacency`; wall placement
around defenses is already a documented RE-FALLBACK. Ship production /
naval attack teams vs water maps need a dedicated pass.

| | |
|---|---|
| **Files** | `engine/ai_base.hpp` (`naval_yard`), `engine/ai_team.*`, `engine/air_naval.hpp` |
| **gamemd** | AI naval yard placement + naval TeamTypes |
| **Rules** | `[AI] BuildNavalYard`, `[General] AINavalYardAdjacency`, naval `MovementZone` |
| **Effort** | L |
| **OpenRA** | Yes — naval production / waypoints as checklist |

### T4.2 Aircraft recruit / airport-bound / Spy Plane feel

AirportBound crash-if-no-dock, spawner (Carrier/Dreadnought/V3), paradrop
planes exist. Spy Plane SW and some recruit/idle paths still called out as
incomplete. Siege Chopper overlaps T3.3.

| | |
|---|---|
| **Files** | `engine/air_naval.hpp`, `engine/w2air_*.inc`, `engine/campaign_runtime.inc` (recruit / PDPLANE), `engine/world.cpp` docks |
| **gamemd** | AircraftClass Mission_Attack **FUN_00417FE0**, Enter_Idle_Mode **FUN_004176F0**, SpawnManager **FUN_006B68B0** |
| **Rules** | `AirportBound`, `Helipad`, `Spawns`/`SpawnsNumber`, `SpyPlane*` |
| **Effort** | L |
| **OpenRA** | Partial |

### T4.3 Capture / engineer / spy polish

Core capture, spy cash/blackout, veterancy-on-spawn helpers are in
`capture.hpp`. README still lists MC enter cursors and Bio Reactor eject;
`EngineerCaptureLevel` appears in gamemd strings and needs a behaviour audit.

| | |
|---|---|
| **Files** | `engine/capture.*`, `World::capture_building` / `infiltrate_building` in `engine/world.cpp` |
| **gamemd** | Engineer / Spy capture & infiltrate paths |
| **Rules** | `EngineerCaptureLevel`, `SpyMoneyStealPercent`, `SpyPowerBlackout`, `Capture=`, `Agent=` / `Infiltrate=` |
| **Effort** | M |
| **OpenRA** | Partial |

### T4.4 Campaign (Allied/Soviet complete-ish; Yuri campaign menu)

Campaign runtime + triggers are large; upstream README: Yuri campaign not in
the menu yet. Some trigger actions are campaign-mode-only firewalls.

| | |
|---|---|
| **Files** | `engine/campaign.*`, `engine/campaign_*.inc`, `engine/map_trigger.*` |
| **gamemd** | campaign briefing / map select; trigger action table |
| **Rules / maps** | `Maps*.mix` / `mapsmd03.mix`; Yuri mission packs |
| **Effort** | L |
| **OpenRA** | No |

### T4.5 Net / tunnel / lobby product gaps

CnCNet V2 tunnel + Rallypoint token auth work; original UDP global-channel
lobby is separate. No build/CRC gate between engines; mixed-platform lobby
notes desync risk; phone-vs-PC MP off by default upstream.

| | |
|---|---|
| **Files** | `engine/net/*`, `engine/mp/*`, Rallypoint patches `0004`/`0006`, `ra2/README.md` harness |
| **gamemd** | IPX/UDP managers **FUN_00540620** / **FUN_0048BE50**; spawner MaxAhead |
| **Rules** | n/a |
| **Effort** | M–L |
| **OpenRA** | No (use CnCNet protocol docs instead) |

### T4.6 Random map generator

Upstream known limitation.

| | |
|---|---|
| **Files** | (mostly absent) |
| **gamemd** | RMG / `RMGLevelLightSettings` strings |
| **Rules** | RMG sections |
| **Effort** | L |
| **OpenRA** | Yes — map generator as inspiration only |

---

## Suggested attack order

1. Land / verify T1.1 ramp Level adoption on real stairs; then T1.2 tilt.
2. Close T1.3 call-for-help RE-OPEN and IQ repair/sell so skirmish AI feels stock.
3. T2.1 country bonuses + T2.2 unique roster audit (data-driven).
4. Burn down T2.4 Yuri Partial list (mostly S/M).
5. T3 presentation pass (buildings → infantry → beams → cameos).
6. T4 systems as product priorities (campaign Yuri, naval AI, net gates).

## Related in-flight work (do not duplicate blindly)

Sibling PRs / agents (as of this doc): ramp Level climbing, stock AI parity,
AIBoost, country bonuses, building/infantry anims. Rebase against those before
starting a new patch in the same area.
