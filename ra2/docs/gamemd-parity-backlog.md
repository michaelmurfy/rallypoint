# gamemd.exe parity backlog

Prioritized gaps for cloning a complete modern RA2/YR engine from retail
`gamemd.exe`, grounded in this tree (`ra2/engine/` + Rallypoint patches through
`0058`). Docs only: nothing here is implemented by this file.

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
| T1.3 call-for-help | `0044-ai-call-for-help` | [#183](https://github.com/michaelmurfy/rallypoint/pull/183) |
| T1.3 call-for-help RE leftovers | `0057-ai-call-for-help-leftovers` | [#197](https://github.com/michaelmurfy/rallypoint/pull/197) |
| T3.4 MagBeam + Floating Disc DISKRAY (presentation) | `0045-magnetron-disc-beam-art` | [#186](https://github.com/michaelmurfy/rallypoint/pull/186) |
| T3.5 blank `Cameo=` → XXICON / Image+ICON (Yuri sidebar) | `0046-yuri-blank-cameos` | [#187](https://github.com/michaelmurfy/rallypoint/pull/187) |
| T2.4 undeployed SMIN slaves + killer-house liberation | `0047-slave-miner-undeployed` | [#188](https://github.com/michaelmurfy/rallypoint/pull/188) |
| T2.4 / T4.3 MC enter cursors + Bio Reactor eject | `0048-mc-cursors-bio-eject` | [#190](https://github.com/michaelmurfy/rallypoint/pull/190) |
| T2.4 AI `AISlaveMinerNumber` / `SlaveMinerShortScan` scoot | `0049-ai-slave-miner-scoot` | [#192](https://github.com/michaelmurfy/rallypoint/pull/192) |
| T2.3 / T4.2 Spy Plane SW Approach→Overfly fog reveal | `0050-spy-plane-sw` | [#191](https://github.com/michaelmurfy/rallypoint/pull/191) |
| T2.4 SMIN↔YAREFN SlaveManager deploy transplant | `0051-smin-yarefn-deploy-transplant` | (this PR) |
| T3.3 turret AI + T3.1 damage fires + T3.2 fear gate | `0055-vehicle-turret-deploy-damage-fires` | (this PR) |
| T2.2 country unique roster audit | `0052-country-unique-roster` | [#194](https://github.com/michaelmurfy/rallypoint/pull/194) |
| T2.4 Magnetron drop Crush blast + Floating Disc live Disc-over-base | `0058-magnetron-toss-disc-over-base` | [#198](https://github.com/michaelmurfy/rallypoint/pull/198) |
| T2.5 Industrial Plant `FactoryPlant=` cost bonuses | `0140-factory-plant-cost-bonuses` | (this PR) |

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

### T1.3 AI call-for-help / base defense response — landed (one RE note open)

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

**Landed in `0057-ai-call-for-help-leftovers`:** victim `+0x2DC` is
SlaveOwner (SlaveManager `FUN_006AF580`): `@0x7085AD` skips the whole vehicle
pass for a slave victim. Techno `+0x2E4` is the Tank Bunker link
(`FUN_00459301`), not cargo, so loaded transports answer again. ArchiveTarget
`==` victim is per unit (`UnitTgt::archive_self`, save v62; a unit that answered
its own call; buildings never have it; x100 infantry incl. negatives, x10
vehicle positives only; recruits Area_Guard around the victim). `FUN_0056D100` /
`FUN_0056D230` zone test = `pathfind::reach_mask` flood from the victim's cell
(outside usable area or Fly zone = ok).

**Open (not resolvable from the binary):** whether cells under a building
share the zone of the ground around it (the clone seeds the cells ringing the
foundation); the zone block table (`MapClass+0x68`) is built at runtime, so a
static read cannot pin it.

| | |
|---|---|
| **Files** | `engine/world_w7.cpp` (`call_for_help`), `engine/ai_base.hpp` (`kAiComputerBaseDefenseResponse`, `kAiPowerEmergency`, `power_emergency_sell`), `engine/world.hpp` (CombatW7Rules), `engine/aimd_parse.*` (`IsBaseDefense`) |
| **gamemd** | **FUN_00708080** (help); callers `0x4422BC` / `0x7027E9` / `0x4D749A`; zone **FUN_0056D100**; Rules+`0xB18` / +`0x10B0` |
| **Rules** | `[AI] ComputerBaseDefenseResponse`, `BaseDefenseDelay`, `*BaseDefenses`, `*BaseDefenseCounts`, `PowerEmergency`, `[IQ] RepairSell` |
| **Effort** | done (`0057`) |
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

Audited against live YR `rulesmd.ini` (all 10 countries). Sidebar /
`country_allows` / AI unit filter already honoured Owner/Required/Forbidden.
Gaps closed in patch `0052-country-unique-roster`: AI `*BaseDefenses`
country-filtered; `AIBuildThis` Combat buildings (French `GTGCAN`, …) join
the AI defense catalog; Yuri dogs `YADOG`/`YDOG` on the AI roster; doctest
matrix + realplay probe for every unique.

| | |
|---|---|
| **Files** | `engine/ai_side_catalog.inc`, `engine/realplay_probe.inc`, `engine/realplay_economy.inc`, production sidebar filters |
| **gamemd** | Owner / RequiredHouses / ForbiddenHouses checks in build/AI catalog build |
| **Rules** | those keys on unit/building types; `[AI] Build*` lists |
| **Effort** | ~~M~~ done (`0052`) |
| **OpenRA** | Yes — faction production lists |

### T2.3 Country superweapons / para drops

Amer/Ally/Sov/Yuri para-drop infantry counts are wired. Spy Plane SW path
landed in patch `0050-spy-plane-sw`
([#191](https://github.com/michaelmurfy/rallypoint/pull/191)) — grant from
NARADR, Approach→Overfly camera reveal per `FUN_006CD66F` / `FUN_004155F0` /
`FUN_004157C0`. Remaining feel polish (rules-driven Camera* keys into
`SwParams`, snapshot SFX/art) is optional follow-up. Do not re-implement the
Approach→Overfly core.

| | |
|---|---|
| **Files** | `engine/w2air_*.inc`, `engine/superweapon.*`, `engine/world_an.cpp`, `engine/world_sw.cpp`, `engine/realplay_economy.inc` |
| **gamemd** | SW type table; SpyPlaneSpecial / AmericanParaDropSpecial |
| **Rules** | `AmerParaDropInf`/`Num`, `AllyParaDrop*`, `SovParaDrop*`, `YuriParaDrop*`, `[SpyPlaneSpecial]`, `SpyPlaneCamera*` |
| **Effort** | ~~M~~ Spy Plane core done (`0050`); Camera* rules wiring S |
| **OpenRA** | Partial |

### T2.4 Yuri leftovers (from `ra2/README.md` Partial)

Already patched for playable YuriCountry + many uniques. **Landed on
`main`:** undeployed SMIN slaves + killer-house liberation
(`0047` / [#188](https://github.com/michaelmurfy/rallypoint/pull/188));
MC enter cursors + Bio Reactor eject (`0048` / [#190](https://github.com/michaelmurfy/rallypoint/pull/190));
AI Slave Miner scoot (`0049` / [#192](https://github.com/michaelmurfy/rallypoint/pull/192));
MagBeam + DISKRAY presentation (`0045` / [#186](https://github.com/michaelmurfy/rallypoint/pull/186));
blank `Cameo=` XXICON / Image+ICON (`0046` / [#187](https://github.com/michaelmurfy/rallypoint/pull/187)).
Do not re-land those.

**Still open (Slave Miner / Yuri):** Xvfb Yuri skirmish CI proof; what sets a
held Magnetron victim's Jumpjet destination in gamemd (the drag toward the
targeted building is ours).
Magnetron drop + Disc-over-base landed as `0058`.
SMIN↔YAREFN transplant landed as `0051` (`FUN_006AF580`).

| Item | Files / keys | Effort | OpenRA |
|---|---|---|---|
| Undeployed SMIN slaves | ~~done~~ `0047` / [#188](https://github.com/michaelmurfy/rallypoint/pull/188) (`FUN_006F3FC0`/`FUN_006AF1A0`) | — | Partial |
| Killer-house liberation on miner death | ~~done~~ `0047` / [#188](https://github.com/michaelmurfy/rallypoint/pull/188) | — | Partial |
| AI `AISlaveMinerNumber` / `SlaveMinerShortScan` scoot | ~~done~~ `0049` / [#192](https://github.com/michaelmurfy/rallypoint/pull/192) (`FUN_0066D530` / `FUN_006AFD60` / `FUN_006B1020` / `FUN_004FEACA`) | — | Yes |
| SMIN↔YAREFN deploy SlaveManager transplant | ~~done~~ `0051` (`FUN_006AF580` / `FUN_007383E0` @0x739956 / `FUN_00449C30` @0x44A03C; `FUN_006B0D10` dock) | — | Partial |
| MC enter cursors / Bio Reactor eject | ~~done~~ `0048` / [#190](https://github.com/michaelmurfy/rallypoint/pull/190) (`FUN_0051E010` Enter on Grinding; `FUN_004598A0` / `FUN_0044D880` absorb + eject) | — | No |
| Magnetron building toss | ~~done~~ `0058`: drop = `CrushWarhead` blast at the landing point owned by the Magnetron (Release `FUN_0070FEE0`; landing 0x746209..0x7462DA → `FUN_004690B0`); hold/refire gates `GetFireError` 0x6FC879..0x6FC8E9 and `IsLocomotor` detonate 0x4694CB read, not changed. Drag destination setter not found | S (drag RE) | No |
| Floating Disc live Disc-over-base check | ~~done~~ `0058`: `DrainStart FUN_0070FD70` / `DrainEnd FUN_0070FE50`, live check `Frame % 16 == 2` (UnitClass::AI 0x7361A9), ally stop 0x6FA1D6, money `Frame % DrainMoneyFrameDelay` 0x6FA167, power zero `HouseClass` 0x508D1E | — | No |
| Blank cameos (YACOMD, YAPPPT, YAROCK, …) | ~~done~~ `0046` / [#187](https://github.com/michaelmurfy/rallypoint/pull/187) (XXICON / AltCameo / Image+ICON); AltCameo *draw* swap still T3.5 | — | No |
| Xvfb Yuri skirmish CI proof | harness | S | n/a |

---

## Tier 3 — animation / presentation

### T3.1 Building ActiveAnim / IdleAnim / damage fires / SuperAnim — partial

Buildup + SuperAnim hold (`0024`) and charge-phase SuperAnim (`0033`) exist.
**Partial land:** patch `0041-building-infantry-anims-from-gamemd`
([#179](https://github.com/michaelmurfy/rallypoint/pull/179)) —
art `AnimActive` cycles the main SHP with SimpleDamage (`Shape_Number`
**FUN_0043ef90**); Tesla/Prism `IsAnimDelayedFire` SpecialAnim; Idle resumes
after finite Active*. Damage fires landed in `0055` (`FUN_0043c0d0`: random type + start frame, Yellow, or Red for CanBeOccupied, offset list ends at first unset). Remaining mis-bound SW art
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
Undeploy stays locked until done and can finish into Walk. `0055` proved against the binary: the DoControls table matches byte for byte (at
0x7EAF7C in the Steam exe), the fire sequence choice (`@0x520886` stand/prone/deploy,
`@0x520D45` secondary) and the Down/Up gate (`FUN_005200B0`; Down/Up only while fear>0 on
entry, now matched). Unit-specific SHP rows are art data. Do not re-patch the Undeploy lock.

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

**Landed in `0055` (turret AI `FUN_00736990`):** idle turret holds until
`GuardAreaTargetingDelay`+5 frames after the last shot, then faces the
Destination (moving) or the hull; `TurretSpins` (Floating Disc, `+0xD21`) keeps
turning; an OmniFire weapon never chases the target with the turret.
**Could not be resolved from the binary:** the per-type turret ROT source
(`FacingClass` is block-copied, no named setter) stays as the clone's ROT.
Deploy SHPs: the only retail vehicle deployers are SCHP/SCHD (`IsSimpleDeployer`,
`0043`); `Deployer=yes` is infantry-only and MCV/SMIN deploy into buildings
(buildup art), so there are no other vehicle deploy SHPs to match.

| | |
|---|---|
| **Files** | `weapon_resolve`, `world_abilities` / `world` / `world_an`, draw + `pres_audio_events`, tests |
| **gamemd** | `FUN_00739AC0` / `FUN_00739CD0` (IsSimpleDeployer), `FUN_0054BD30` (DeployToLand) |
| **Rules / art** | `SCHP`/`SCHD`; `IsSimpleDeployer`, `DeployToLand`, `UnloadingClass`, `DeployingAnim` |
| **Effort** | SCHP done; remaining vehicle turret/deploy M |
| **OpenRA** | Partial |

### T3.4 Weapon beams / lasers / prism / magnetron / disc rays — MagBeam/DISKRAY landed

Logic for prism support, magnetron lift, disc drain exists. Chrono Legionnaire
erase uses ChronoBeam (`0028`).

**Landed:** patch `0045-magnetron-disc-beam-art`
([#186](https://github.com/michaelmurfy/rallypoint/pull/186)) —
Magnetron `IsMagBeam` MagnaBeamColor wave (`WaveClass::Draw_Magnetic`
**FUN_00762070** / Update **FUN_00762af0**; Fire **FUN_006ff5f5**) while
`IsLocomotor` holds; Floating Disc `DrainWeapon` loops
`DrainAnimationType`/DISKRAY (**FUN_0070fd70** / AnimClass **FUN_00421ea0**).
Do not re-implement MagBeam / DISKRAY art.

**Still open:** fragment beams only via FX events; prism support visual
polish. Magnetron drop blast and Disc-over-base landed in `0058` (T2.4).

| | |
|---|---|
| **Files** | `engine/building_combat.cpp` (prism), magnetron / disc patches, `pres_fx.inc` / `pres_game.inc`, patch `0045` |
| **gamemd** | Laser / electric / magnetic beam drawers (`FUN_00762070`, `FUN_0070fd70`) |
| **Rules / art** | `IsLaser`, `IsElectric`, `IsLocomotor`, `IsMagBeam`, `DrainWeapon`, `DrainAnimationType`, `MagnaBeamColor` |
| **Effort** | MagBeam/DISKRAY done; remaining fragment/prism S–M |
| **OpenRA** | No |

### T3.5 Cameo / sidebar icon gaps — blank Cameo= landed

Cameos load from `language/cameo` + `langmd/cameomd` via AssetFS.
SW cameos sit in side2 sockets (`0031`).

**Landed:** patch `0046-yuri-blank-cameos`
([#187](https://github.com/michaelmurfy/rallypoint/pull/187)) —
blank artmd `Cameo=` falls back to `AltCameo` / `Image`+`ICON` / `XXICON`
(and `language/cameo.mix` is on AssetFS). Do not re-implement blank-cameo
resolve.

**Still open:** veteran/undeployed `AltCameo` swap when drawing (name is
loaded).

| | |
|---|---|
| **Files** | sidebar draw in `engine/main.cpp`, patches `0016`, `0031`, `0046` |
| **gamemd** | SidebarClass cameo strip / FlashCameo |
| **Rules / art** | artmd `Cameo=`, `AltCameo=`; `cameo.pal` |
| **Effort** | blank Cameo= done; remaining S (AltCameo draw swap) |
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
planes exist. Spy Plane SW fog-reveal sweep is patch `0050` /
[#191](https://github.com/michaelmurfy/rallypoint/pull/191) (loaner SPYP
Approach/Overfly). Remaining: recruit/idle polish; Siege Chopper overlaps T3.3.
Do not re-land the Spy Plane SW core.

| | |
|---|---|
| **Files** | `engine/air_naval.hpp`, `engine/w2air_*.inc`, `engine/campaign_runtime.inc` (recruit / PDPLANE), `engine/world.cpp` docks |
| **gamemd** | AircraftClass Mission_Attack **FUN_00417FE0**, Enter_Idle_Mode **FUN_004176F0**, SpawnManager **FUN_006B68B0**; SpyPlane Launch **FUN_006CD66F** |
| **Rules** | `AirportBound`, `Helipad`, `Spawns`/`SpawnsNumber`, `SpyPlane*` |
| **Effort** | L (Spy Plane SW ~~done~~ in `0050`) |
| **OpenRA** | Partial |

### T4.3 Capture / engineer / spy polish

Core capture, spy cash/blackout, veterancy-on-spawn helpers are in
`capture.hpp`. MC enter cursors / Bio Reactor eject landed in patch `0048` /
[#190](https://github.com/michaelmurfy/rallypoint/pull/190). Do not re-land
those cursors / eject. `EngineerCaptureLevel` appears in gamemd strings and
needs a behaviour audit.

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

1. Burn down remaining T2.4 Yuri Partial: Xvfb Yuri skirmish CI; the
   Magnetron drag-destination RE — do not re-land `0045`–`0058`
   (MC cursors / Bio eject `0048`, AI scoot `0049`, Spy Plane SW `0050`,
   SMIN↔YAREFN transplant `0051`, Magnetron drop / Disc-over-base `0058` are done).
2. ~~T1.3 remaining RE~~ done in `0057`; only the building-zone note above is open.
3. T2.2 country unique roster completeness (`RequiredHouses` /
   `ForbiddenHouses` kit gaps).
4. T3.3 leftovers: non-SCHP vehicle turret / deploy SHPs (SCHP done in `0043`).
5. T3.1 / T3.2 leftovers (damage fires, prone/crawl/fire proofs) — do not
   re-land AnimActive / Undeploy from `0041`.
6. T3.4 leftovers (fragment beams, prism polish) and T3.5 AltCameo draw swap
   — MagBeam/DISKRAY and blank Cameo= are done (`0045`/`0046`).
7. T4 systems as product priorities (naval AI, campaign Yuri, aircraft
   recruit polish, net gates) — Spy Plane SW core is done in `0050`.

Landed recently (skip): MagBeam/DISKRAY `0045` / [#186](https://github.com/michaelmurfy/rallypoint/pull/186);
blank cameos `0046` / [#187](https://github.com/michaelmurfy/rallypoint/pull/187);
undeployed SMIN `0047` / [#188](https://github.com/michaelmurfy/rallypoint/pull/188);
MC cursors / Bio eject `0048` / [#190](https://github.com/michaelmurfy/rallypoint/pull/190);
AI Slave Miner scoot `0049` / [#192](https://github.com/michaelmurfy/rallypoint/pull/192);
Spy Plane SW `0050` / [#191](https://github.com/michaelmurfy/rallypoint/pull/191);
SMIN↔YAREFN transplant `0051` / [#195](https://github.com/michaelmurfy/rallypoint/pull/195);
country roster `0052` / [#194](https://github.com/michaelmurfy/rallypoint/pull/194);
Magnetron drop / Disc-over-base `0058` / [#198](https://github.com/michaelmurfy/rallypoint/pull/198);
T1.2 tilt `0042` / [#184](https://github.com/michaelmurfy/rallypoint/pull/184);
T3.3 SCHP `0043` / [#182](https://github.com/michaelmurfy/rallypoint/pull/182);
T1.3 bulk `0044` / [#183](https://github.com/michaelmurfy/rallypoint/pull/183);
launcher AIBoost UI [#181](https://github.com/michaelmurfy/rallypoint/pull/181).

## Related in-flight work (do not duplicate blindly)

Before starting a new patch in an area listed under **Recently landed**,
rebase on `main` and read the cited patch — re-implementing
`0034`/`0035`/`0037`/`0040`–`0058` or the `0025`–`0033` presentation
stack is wasted work.

No open sibling parity PRs assumed for MagBeam / blank cameos / undeployed
SMIN / MC cursors / Bio eject / AI Slave Miner scoot / Spy Plane SW (those
landed in #186–#192), or SMIN↔YAREFN transplant (`0051`). Check open PRs
before starting another patch in T2.4 leftovers (Magnetron drag RE, Xvfb CI), T2.2 roster, T1.3 RE leftovers, or T3.4/T3.5 polish.

Next priorities (open): T2.4 Xvfb CI / Magnetron drag RE;
T1.3 RE leftovers; T2.2 roster; non-SCHP T3.3; T3.1/T3.2 leftovers;
T3.4/T3.5 polish; T4 (naval AI, campaign Yuri, net).

---

## Units, infantry, vehicles, aircraft, ships and buildings: rules-key audit (2026-11-25)

Method: every key that appears under a `[InfantryTypes]`, `[VehicleTypes]`, `[AircraftTypes]` or
`[BuildingTypes]` section of the real `rulesmd.ini` (YR 1.001) was looked up in the engine sources
(`"Key"` string, case-insensitive; computed keys such as `Weapon<N>` / `EliteWeapon<N>` / `Stage<N>`
checked by hand). Reference for formulas and addresses: github.com/YuriPlanet/vera20k (GPL-3, read only,
only addresses and formulas cited here, no code copied).

**Read and used (no work left):** weapons, `EliteWeapon<N>`, `Secondary`, turrets, `Crushable`/`Crusher`,
`Deployer`, `Passengers`, `Cloakable`, `Sensors*`, `Veteran*`/`Elite*` promotion, `SelfHealing`, `Powered`,
`PowerUp*`, `Factory`/`Naval`, `Overpowerable`, `SpecialThreatValue`, `ImmuneToPsionics`, `IsGattling` stages,
`DeployToLand`, `UnitRepair`, `Drainable`, `Enslaves`, `BuildLimit`.

**Landed:** `FactoryPlant=` (patch 0140). Soviet Industrial Plant (`NAINDP`, `UnitsCostBonus=.75`) never
made vehicles cheaper. gamemd keeps every owned plant in `House+140` (list changes on Unlimbo, ownership
change and death, no health or power filter) and `CalculateCostMultipliers @ 0x0050BF60` stores the product per
slot (Infantry, Units, Aircraft, Buildings, Defenses = `BuildCat=Combat`); `Cost_Of @ 0x00711F00` is
`ftol(cost * plant * country)`. The engine now does the same when a build starts (`World::plant_cost`, used by
the unit and building branches of `econ_produce`; build time follows the cheaper cost, like the country
multipliers). Save format 67. Test: `tests/test_re_units_buildings.cpp` (real `rulesmd` values and a synthetic
two-house World).

**Found, not done (ranked by gameplay impact; every key below is used by retail units or buildings):**

| Key(s) | Used by | gamemd / reference | Effort |
|---|---|---|---|
| `Bunker=yes` / `Bunkerable=no` (the Tank Bunker mechanic) | `NATBNK`; 17 vehicles opt out (`Bunkerable=no`: MCVs, miners, V3, Terror Drone...) | `+0x2E4` building<->unit link, `FUN_00459301` (already named in `world_w7.cpp`); the whole garrison-by-vehicle feature is missing | L |
| `ExitCoord=` | `GAWEAP`/`NAWEAP`/`YAWEAP` (512,256,0), `GAPILE`/`YABRCK` (-64,64,0) | `factory.cpp` `exit_cell` drops it ("cell resolution"); retail spawns the unit at the exit coordinate and lets it drive out | M |
| `CrateBeneath=` / `CrateBeneathIsMoney=` | 14 civilian buildings | `BuildingClass::AfterDestruction @ 0x00441F60`, crate tail `0x004421A4..0x0044222D`: `Place_Crate` at the origin cell, powerup Money or random, lobby Crates option not consulted. Our crate store only exists once `enable_crates` ran, so it needs that split first | M |
| `TypeImmune=yes` | `YURI`, `DLPH` | Damage is ignored when the attacker has the same type AND the same owner (friendly splash only), see `Receive_Damage` gate list | S (five damage sites in `combat.cpp`) |
| `HarvestRate=` on slaves | `SLAV` (150) | vera20k `infantry_mission_harvest` (`0x00522E70`): one level per `HarvestRate` frames. The engine digs one bale per second (`kGulpSecs`). Unit of "frames" and the payout per level need a binary check before changing the economy | S, risky |
| `CloakingSpeed=` | `SUB`, `SQD`, `DLPH`, `BSUB` | Our cloak is a state flag with no fade; the key only changes the fade length and the re-cloak step | S |
| `GateCloseDelay=` | `GAGATE_A` | Allied gate: the gate stays open this long after the mover has passed; our gate opens for allied movers on the path only | S |
| `ImmuneToPoison=` / `Poison=` warhead | `VIRUS`; one warhead | poison damage over time is not modelled | M |
| `RevealToAll=`, `RadarVisible=`, `DetectDisguiseRange=` | tech buildings, `NAPSIS` | reveal-on-capture, minimap visibility of neutral buildings, spy detection radius | S each |
| `Immune=yes` | `CABHUT` | take no damage | S |
| `Selectable=no`, `Repairable=no`, `TogglePower=no` | lamps, fences, walls, civilian art; power buildings | `pick_building` selects any building; repair/power-down only act on consumers anyway | S, cosmetic |
| `Points`, `Nominal`, `Assaulter`, `CloseRange`, `ThreatAvoidanceCoefficient`, `StupidHunt`, `Bombable`, `IsTilter`, `TooBigToFitUnderBridge`, `ZFudge*`, `PitchAngle`/`PitchSpeed`/`FireAngle`, `DebrisAnim`, `DamageSound`, `Bib`, `AlphaImage` | many | AI hints, score points, presentation | not worth a patch yet |

Real-data suite (`RA2_INSTALL_DIR=/home/mmurphy/rp-steam/game/RA2 ra2assets_tests`): 1284 cases, 0 skipped, all
pass with patch 0140.
