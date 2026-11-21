# gamemd.exe parity backlog

Prioritized gaps for cloning a complete modern RA2/YR engine from retail
`gamemd.exe`, grounded in this tree (`ra2/engine/` + Rallypoint patches through
`0044`). Docs only: nothing here is implemented by this file.

Binary reference: Steam YR `gamemd.exe` (tools on the RE VM). Upstream RE notes
lived in `docs/re/` / `docs/gap/` of omarelarab/ra2-engine and were trimmed from
our copy; FUN_* cites below come from comments still in the sources.

**Effort:** S = days, M = 1–2 weeks, L = multi-week / multi-patch.
**OpenRA:** whether OpenRA’s RA2/YR rules or code is a useful *secondary*
reference (never a substitute for gamemd behaviour).

### Recently landed on `main` (do not re-implement)

| Item | Patch | PR |
|---|---|---|
| Presentation stack (HiDPI fonts, credits/cursor, canvas zoom, CLEG ChronoBeam, building health pips, Tesla low-power visual, SW sidebar slots, smooth unit draw, SW building charge anim) | `0025`–`0033` | #162–#171 |
| T1.1 ramp / slope Level while walking | `0034-ramp-slope-level` | [#172](https://github.com/michaelmurfy/rallypoint/pull/172) |
| Stock AI `UseMinDefenseRule` / `MinimumAIDefensiveTeams` (base-guard teams so call-for-help has members) | `0035-stock-ai-min-defense-rule` | [#174](https://github.com/michaelmurfy/rallypoint/pull/174) |
| Optional AIBoost smarter skirmish AI (`AIBoost=` / `RA2_AI_BOOST` 1..3) + launcher Play/lobby UI | `0037-ai-boost-smarter` | [#173](https://github.com/michaelmurfy/rallypoint/pull/173) / [#181](https://github.com/michaelmurfy/rallypoint/pull/181) |
| T2.1 country bonuses (HouseType Cost/Armor/ROF/… Mult + Veteran*) | `0040-country-bonuses-from-gamemd` | [#178](https://github.com/michaelmurfy/rallypoint/pull/178) |
| T3.1 / T3.2 building + infantry anims (partial) | `0041-building-infantry-anims-from-gamemd` | [#179](https://github.com/michaelmurfy/rallypoint/pull/179) |
| T1.2 unit tilt on TMP slopes (presentation) | `0042-unit-tilt-on-slopes` | [#184](https://github.com/michaelmurfy/rallypoint/pull/184) |
| T3.3 Siege Chopper deploy / Undeploy (`IsSimpleDeployer` / SCHD) | `0043-siege-chopper-deploy` | [#182](https://github.com/michaelmurfy/rallypoint/pull/182) |
| T1.3 call-for-help (partial; RE leftovers below) | `0044-ai-call-for-help` | [#183](https://github.com/michaelmurfy/rallypoint/pull/183) |

---

## Tier 1 — play-breaking (pathfind / slopes, AI defense, sync, crashes)

### T1.1 Ramp / slope height while walking — landed

Units used to keep the Level they spawned on, so a tank ordered down a paved
stair never left the terrace even when A* allowed the step.

**Landed:** patch `0034-ramp-slope-level` ([#172](https://github.com/michaelmurfy/rallypoint/pull/172)) —
`step_units` adopts the cell’s Level on TMP ramps (FootClass height gate
**FUN_004D9C60** already allowed the one-level step); draw blends the step.
Still worth a real-map sign-off on stairs / bridges; do not re-patch the Level
adoption itself.

| | |
|---|---|
| **Files** | `engine/pathfind.hpp`, `engine/pathfind.cpp` (`path_step_layer`, `PathTerrain`); unit step in `engine/world.cpp` / locomotion |
| **gamemd** | FootClass height gate **FUN_004D9C60** (comments historically said `FUN_004D9B70`); `CellClass` Level `+0x11B`, SlopeIndex `+0x11C` (TMP hdr `+0x2A`); `IsClearToMove` **FUN_004833B0**; A* neighbours **FUN_00429A10** |
| **Rules / art** | TMP ramp SlopeIndex ≠ 0; deck / bridge Level+4 |
| **Effort** | done (verify S) |
| **OpenRA** | Partial — terrain height models differ; use only for vocabulary |

### T1.2 Cliff / slope *presentation* (unit tilt) — landed

Pathing can be correct while vehicles stay flat on ramps. Upstream README
lists “units tilting on slopes” as a known miss.

**Landed:** patch `0042-unit-tilt-on-slopes`
([#184](https://github.com/michaelmurfy/rallypoint/pull/184)) —
`slope_tilt` pitches drive-track vehicles from the cell SlopeIndex at draw
time (presentation only; rockets keep their own pitch). Do not re-implement
the tilt helper.

| | |
|---|---|
| **Files** | `engine/slope_tilt.*`, voxel / facing draw in `engine/main.cpp`, `engine/facing_cache.*`; tests `tests/test_slope_tilt.cpp` |
| **gamemd** | Drive-track / Facing pitch from cell SlopeIndex (**FUN_0055A7D0** / **FUN_007559B0**); rocket pitch already special-cased |
| **Rules / art** | artmd Facing / voxel sections; TMP slope types |
| **Effort** | done (verify S on real ramps) |
| **OpenRA** | No — different render stack |

### T1.3 AI call-for-help / base defense response — partial

`World::call_for_help` ports Steam YR **FUN_00708080** (older notes said
`FUN_00707500` — wrong VA in this binary).

**Landed:** `0035-stock-ai-min-defense-rule`
([#174](https://github.com/michaelmurfy/rallypoint/pull/174)) —
`UseMinDefenseRule` / `MinimumAIDefensiveTeams` so stock `IsBaseDefense`
teams form. Optional AIBoost (`0037` / [#173](https://github.com/michaelmurfy/rallypoint/pull/173);
launcher UI [#181](https://github.com/michaelmurfy/rallypoint/pull/181))
scales defense response when enabled. Patch `0044-ai-call-for-help`
([#183](https://github.com/michaelmurfy/rallypoint/pull/183)): building
victims get ArchiveTarget x100/x10 help ranking; skip ResourceGatherer and
loaded transports; Amphibious domain zone stand-in; `PowerEmergency` sell +
IQ `RepairSell` on ConditionRed sell. Do not re-land those pieces.

**Still open (RE):** victim `+0x2DC` vehicle-pass gate; full `FUN_0056D100`
MapZone ids; per-unit ArchiveTarget==self tracking.

| | |
|---|---|
| **Files** | `engine/world_w7.cpp` (`call_for_help`), `engine/ai_base.hpp` (`kAiComputerBaseDefenseResponse`, `kAiPowerEmergency`, `power_emergency_sell`), `engine/world.hpp` (CombatW7Rules), `engine/aimd_parse.*` (`IsBaseDefense`) |
| **gamemd** | **FUN_00708080** (help); callers `0x4422BC` / `0x7027E9` / `0x4D749A`; zone **FUN_0056D100**; Rules+`0xB18` / +`0x10B0` |
| **Rules** | `[AI] ComputerBaseDefenseResponse`, `BaseDefenseDelay`, `*BaseDefenses`, `*BaseDefenseCounts`, `PowerEmergency`, `[IQ] RepairSell` |
| **Effort** | M remaining after 0044 |
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

### T2.1 Per-country cost / combat multipliers — landed

`CountryCostMult` is mentioned on the sell path; country section bonuses from
rules (`*CostBonus`, Speed/Armor/ROF/Firepower style keys in gamemd strings)
are now loaded as HouseType handicaps.

**Landed:** patch `0040-country-bonuses-from-gamemd`
([#178](https://github.com/michaelmurfy/rallypoint/pull/178)) —
`country_bonuses.hpp` loads Cost/Armor/ROF/Firepower/Groundspeed/Airspeed/
BuildTime Mult keys and `VeteranInfantry` / `VeteranUnits` / `VeteranAircraft`.
Stock `rulesmd` leaves most Mult/Veteran keys commented (identity); they apply
when a mod or rules uncomment them. Do not re-implement the loader.

| | |
|---|---|
| **Files** | `engine/country_bonuses.hpp`, `engine/repair_sell.cpp`, `engine/ai_base.hpp` (`DifficultyMods`), house setup in `engine/gf_start.inc` / `engine/main.cpp` |
| **gamemd** | HouseType / country LoadFromINI (bonus fields beside `Side=` / `Prefix=`) |
| **Rules** | `[Americans]`…`[YuriCountry]`: Mult keys + `VeteranInfantry` / `VeteranUnits` / `VeteranAircraft` |
| **Effort** | done |
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
| Undeployed SMIN slaves | ~~done~~ patch `0047` (`FUN_006F3FC0`/`FUN_006AF1A0`; ModEnc = Enslaves names InfantryType) | — | Partial |
| Killer-house liberation on miner death | ~~done~~ patch `0047` (`Building::killed_by` / `Unit::vet.killed_by`) | — | Partial |
| AI `AISlaveMinerNumber` / `SlaveMinerShortScan` scoot | `[General]` SlaveMiner* / AISlaveMinerNumber | M | Yes |
| SMIN↔YAREFN deploy SlaveManager transplant | deploy/undeploy path | M | Partial |
| MC enter cursors / Bio Reactor eject | `engine` capture/MC + patch 0022 | S | No |
| Magnetron building toss | patch 0017 `IsLocomotor`; toss still open (beam art in `0045`) | M | No |
| Floating Disc live Disc-over-base check | patch 0018 + `0045` DISKRAY; over-base still open | S | No |
| Blank cameos (YACOMD, YAPPPT, YAROCK, …) | artmd `Cameo=`; patches 0016 + `0046-yuri-blank-cameos` (XXICON / AltCameo / Image+ICON) | S | No |
| Xvfb Yuri skirmish CI proof | harness | S | n/a |

---

## Tier 3 — animation / presentation

### T3.1 Building ActiveAnim / IdleAnim / damage fires / SuperAnim — partial

Buildup + SuperAnim hold (`0024`) and charge-phase SuperAnim (`0033`) exist.
**Partial land:** patch `0041-building-infantry-anims-from-gamemd`
([#179](https://github.com/michaelmurfy/rallypoint/pull/179)) —
art `AnimActive` cycles the main SHP with SimpleDamage (`Shape_Number`
**FUN_0043ef90**); Tesla/Prism `IsAnimDelayedFire` SpecialAnim; Idle resumes
after finite Active*. Damage-fire overlays and remaining mis-bound SW art
sequences still need a pass — do not re-do the AnimActive / SpecialAnim core.

| | |
|---|---|
| **Files** | `engine/pres_bldanim.inc`, `engine/building.*`, building draw in `engine/main.cpp`, patches `0024` / `0033` / `0041` |
| **gamemd** | Building anim state machine (ActiveAnim…Four, IdleAnim, SuperAnim…Four, DamageFire) |
| **Rules / art** | artmd `ActiveAnim*`, `IdleAnim`, `SuperAnim*`, `Buildup`, `DamageFire*` |
| **Effort** | remaining M (core cycles done) |
| **OpenRA** | No |

### T3.2 Infantry sequence completeness — partial

DoControls rows + deploy edges (GI / Guardian GI) are in `inf_anim`.
**Partial land:** patch `0041`
([#179](https://github.com/michaelmurfy/rallypoint/pull/179)) —
Undeploy stays locked until done and can finish into Walk. Prone / crawl /
fire / idle transitions still need parity proof vs SHP sequences for Yuri and
country uniques — do not re-patch the Undeploy lock.

| | |
|---|---|
| **Files** | `engine/inf_anim.*`, tests `tests/test_inf_anim.cpp` |
| **gamemd** | Infantry DoControls / sequence table (42 rows recovered) |
| **Rules / art** | artmd Sequence / PrimaryFire / Deployed* |
| **Effort** | remaining M |
| **OpenRA** | Partial — sequence names only |

### T3.3 Vehicle anims / turret / deploy (Siege Chopper) — SCHP landed

**Landed (SCHP):** patch `0043-siege-chopper-deploy`
([#182](https://github.com/michaelmurfy/rallypoint/pull/182)) —
`IsSimpleDeployer` + `DeployToLand` + `UnloadingClass=SCHD` +
`DeployingAnim=SCHPDEPL` (deploy mission, landed-body draw, anim). Do not
re-implement Siege Chopper deploy.

**Still open:** turret facings and deploy SHPs for other vehicles may still
diverge.

| | |
|---|---|
| **Files** | `weapon_resolve`, `world_abilities` / `world` / `world_an`, draw + `pres_audio_events`, tests |
| **gamemd** | `FUN_00739AC0` / `FUN_00739CD0` (IsSimpleDeployer), `FUN_0054BD30` (DeployToLand) |
| **Rules / art** | `SCHP`/`SCHD`; `IsSimpleDeployer`, `DeployToLand`, `UnloadingClass`, `DeployingAnim` |
| **Effort** | SCHP done; remaining vehicle turret/deploy M |
| **OpenRA** | Partial |

### T3.4 Weapon beams / lasers / prism / magnetron / disc rays

Logic for prism support, magnetron lift, disc drain exists. Chrono Legionnaire
erase uses ChronoBeam (`0028`). **Partial land:** patch
`0045-magnetron-disc-beam-art` — Magnetron `IsMagBeam` MagnaBeamColor wave
(`WaveClass::Draw_Magnetic` **FUN_00762070** / Update **FUN_00762af0**; Fire
**FUN_006ff5f5**) while `IsLocomotor` holds; Floating Disc `DrainWeapon`
loops `DrainAnimationType`/DISKRAY (**FUN_0070fd70** / AnimClass
**FUN_00421ea0**). Remaining: some fragment beams only via FX events; prism
support visual polish.

| | |
|---|---|
| **Files** | `engine/building_combat.cpp` (prism), magnetron / disc patches, `pres_fx.inc` / `pres_game.inc`, patch `0045` |
| **gamemd** | Laser / electric / magnetic beam drawers (`FUN_00762070`, `FUN_0070fd70`) |
| **Rules / art** | `IsLaser`, `IsElectric`, `IsLocomotor`, `IsMagBeam`, `DrainWeapon`, `DrainAnimationType`, `MagnaBeamColor` |
| **Effort** | remaining S–M |
| **OpenRA** | No |
| **Status** | MagBeam + DISKRAY presentation (this PR); fragment/prism leftovers |

### T3.5 Cameo / sidebar icon gaps — partial

Cameos load from `language/cameo` + `langmd/cameomd` via AssetFS.
Blank artmd `Cameo=` falls back to `AltCameo` / `Image`+`ICON` / `XXICON`
(patch `0046-yuri-blank-cameos`). SW cameos sit in side2 sockets (`0031`).
Remaining: veteran/undeployed `AltCameo` swap when drawing (name is loaded).

| | |
|---|---|
| **Files** | sidebar draw in `engine/main.cpp`, patches `0016`, `0031`, `0046` |
| **gamemd** | SidebarClass cameo strip / FlashCameo |
| **Rules / art** | artmd `Cameo=`, `AltCameo=`; `cameo.pal` |
| **Effort** | remaining S (AltCameo draw swap) |
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

1. Burn down T2.4 Yuri Partial list (mostly S/M: undeployed SMIN slaves,
   Magnetron/Disc beam art, blank cameos, MC cursors, …).
2. T1.3 remaining RE only (victim `+0x2DC`, MapZone ids, ArchiveTarget==self)
   — do not re-land `0044` / [#183](https://github.com/michaelmurfy/rallypoint/pull/183).
3. T3.3 leftovers: non-SCHP vehicle turret / deploy SHPs (SCHP done in `0043`).
4. T3.1 / T3.2 leftovers (damage fires, prone/crawl/fire proofs) — do not
   re-land AnimActive / Undeploy from `0041`.
5. T3.4 weapon beams / lasers (Magnetron toss beam, `DISKRAY`, fragment beams)
   where not already covered by a T2.4 item.
6. T4 systems as product priorities (campaign Yuri, naval AI, net gates).

Landed recently (skip): T1.2 tilt `0042` / [#184](https://github.com/michaelmurfy/rallypoint/pull/184);
T3.3 SCHP `0043` / [#182](https://github.com/michaelmurfy/rallypoint/pull/182);
T1.3 bulk `0044` / [#183](https://github.com/michaelmurfy/rallypoint/pull/183);
launcher AIBoost UI [#181](https://github.com/michaelmurfy/rallypoint/pull/181).

## Related in-flight work (do not duplicate blindly)

Before starting a new patch in an area listed under **Recently landed**,
rebase on `main` and read the cited patch — re-implementing
`0034`/`0035`/`0037`/`0040`–`0044` or the `0025`–`0033` presentation stack is
wasted work.

Sibling work often in flight on T2.4 / T3.4 / T3.5 (undeployed Slave Miner,
Magnetron + Disc beam art, blank Yuri cameos): check open PRs before starting
another patch in those areas.

Next priorities (open): T2.4 Yuri Partial; T1.3 RE leftovers; non-SCHP T3.3;
T3.1/T3.2 leftovers; T4.
