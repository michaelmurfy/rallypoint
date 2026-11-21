#pragma once
// Pure combat core: armor lookups, warhead damage vs armor, and per-frame
// engagement (targeting, rate-of-fire cadence, damage, deaths, pursuit
// requests). Formulas confirmed in docs/re/combat.md (Combat Phase Task 1);
// see that file's numbered sections for the RE evidence behind each rule
// referenced in engine/combat.cpp's comments.
//
// Pure: no SDL/OS deps (same pattern as pathfind.hpp/occupancy.hpp), so it is
// unit-testable and compiled into both ra2assets_tests and ra2engine from a
// single source of truth (see CMakeLists.txt / cmake/ra2_sources.cmake).
#include <functional>
#include <string>
#include <vector>

#include "world.hpp"  // Unit; pulls in Occupancy (occupancy.hpp) transitively

namespace ra2engine {

// Maps an [ArmorType] ini class name to its Verses[] index. docs/re/combat.md
// §1/§4 (High): TechnoTypeClass::Armor is used directly, UNSCALED, as the
// index into WarheadTypeClass::Verses[0..10] -- none..special_2 = 0..10, no
// remapping table, no off-by-one. Case-insensitive (ini convention);
// unrecognized names default to "none" (0) rather than failing, since a
// mistyped/absent Armor= key should behave like the softest class rather
// than crash.
int armor_index(const std::string& armor);

// One hit's damage from `damage` vs a target of armor index `ai`, given the
// firing weapon's warhead `verses` percents (Verses[0..10], ini order).
// Result is `damage * verses[ai] / 100`, truncated toward zero:
// docs/re/combat.md §3 (Medium-High confidence -- the exact multiply
// instruction wasn't isolated, but the pervasive truncate-toward-zero
// double->int convention, the disassembled `_ftol`-shaped helper, was).
// Plain integer division truncates toward zero for non-negative operands,
// which is exactly this case (damage and percent are never negative), so no
// separate std::trunc call is needed.
// `verses` empty -> 100% (defensive: a warhead with no Verses table hits
// everything at full damage rather than silently dealing 0).
// `ai` outside `verses`'s bounds clamps to armor index 0 ("none") rather
// than indexing out of range.
int damage_vs(int damage, const std::vector<int>& verses, int ai);

// CB-4 Superweapons [RE: superweapon.md §2/§5]: the CB-2 area-damage applicator,
// PROMOTED from combat.cpp's anonymous namespace to ra2engine so World::
// fire_superweapon can REUSE it for the nuke detonation (a transient virtual
// detonator Unit passed as BOTH attacker and center). LINKAGE-ONLY change: the
// function body + its existing caller (try_fire) are byte-for-byte unchanged, so
// every existing combat path computes identical results and all 11 gate outputs
// stay bit-identical (re-verified below). This is the one deviation from the
// spec's "combat.cpp literally untouched" wording -- necessitated because the
// applicator has internal linkage and is otherwise uncallable from world.cpp;
// reimplementing the falloff loop (a "new damage system") is what §5/§8 forbid.
// W6-03-04 damage -> anger feed [RE: TechnoClass::ReceiveDamage FUN_00700da0
// @0x7012CD-0x7012FD -- after the damage lands, victim->Owner->AddAnger(
// ftol(type->Cost * damage / type->Strength), attacker->Owner) (FUN_005045b0)].
// The pure combat helpers have no World, so the World arms this sink for the
// length of its step and every damage tail reports (victim, attacker house,
// damage) through it. Unset (nullptr) everywhere else: a strict no-op.
struct DamageFeed {
    void* ctx = nullptr;
    void (*fn)(void* ctx, bool building, int victim, int attacker_house, int dmg) = nullptr;
};
DamageFeed& damage_feed();
inline void feed_damage(bool building, int victim, int attacker_house, int dmg) {
    const DamageFeed& f = damage_feed();
    if (f.fn && dmg > 0 && attacker_house >= 0) f.fn(f.ctx, building, victim, attacker_house, dmg);
}

// w7fix-combat: the ReceiveDamage tail's behaviour hooks (retaliation
// CanRetaliateToAttacker 0x707C40, call-for-help FUN_00707500, the damage
// scatter 0x701ED5..0x702196, Harmless wake FUN_004D7250) need the World;
// the pure damage helpers log every landed hit here instead and the World
// resolves the log (World::step_missions_w7). `sink` is armed by World::step
// only while the W7 rules are armed; null -> the pre-W7 inline retaliation.
// (HitRec: world.hpp)
struct HitLog { std::vector<HitRec>* sink = nullptr; };
// W7-07-03 [RE: FUN_0070C190]: the threat `u` (index self_idx, -1 = none) sees
// in unit `v` / building `b` (combat.cpp).
int threat_value_unit(const World& world, const Unit& u, int self_idx, const Unit& v);
int threat_value_building(const World& world, const Unit& u, const Building& b);
HitLog& hit_log();
inline bool log_hit(const HitRec& r) {
    HitLog& h = hit_log();
    if (!h.sink) return false;
    h.sink->push_back(r);
    return true;
}

void apply_spread_damage(std::vector<Unit>& units, Occupancy& occ,
                         const Unit& attacker, const Unit& center);

// Projectile phase (P2) [RE: projectile.md §10]: one shot whose projectile
// flies. Emitted by step_combat ONLY when the caller passes a non-null
// `launches` sink (World's projectiles_enabled_ firewall); World turns each
// into a Projectile in launch (ascending attacker) order. Exactly one of
// target_unit / target_building is >= 0.
struct ProjectileLaunch {
    int attacker = -1;
    int target_unit = -1;
    int target_building = -1;
};

// P2: the single-target damage tail (damage_vs, idle retaliation, death) --
// the pre-P2 try_fire else-branch extracted verbatim, shared by the instant
// path and World's projectile detonation.
void deliver_unit_hit(std::vector<Unit>& units, Occupancy& occ, int attacker_idx, int victim_idx);
// Gap fix C: the same tail with an explicit shooter -- a unit (attacker_idx) or
// a base defense (attacker_building, `shooter` = its transient weapon carrier).
void deliver_hit_from(std::vector<Unit>& units, Occupancy& occ, const Unit& shooter,
                      int attacker_idx, int attacker_building, int victim_idx);

// P2: the attack-building damage tail (damage_vs vs armor, kill_building, clear
// attackers) -- extracted from step_combat's building branch.
void deliver_building_hit(std::vector<Unit>& units, std::vector<Building>& buildings,
                          int attacker_idx, int b_idx);

// A request from step_combat for World to path a unit toward a cell (pursuit
// of an explicitly-ordered target, or return to a guard anchor). step_combat
// never touches Unit::path/find_path itself -- it only asks; World (later
// tasks) is what actually paths, keeping this module pure and pathing-
// agnostic (same "produce requests, let the caller act" shape as the rest of
// the pure engine layer).
struct MoveRequest {
    int unit = -1;
    int goal_x = 0, goal_y = 0;   // W9-05: default-initialised (no indeterminate scalar)
    // true: `goal_x/goal_y` is the TARGET's own cell and the caller (World)
    // must resolve a distinct firing position near it (docs/re/
    // infantry-anim.md §7 -- the original never paths an attacker to the
    // victim's own occupied cell; it approaches until in weapon range).
    // false (default): move exactly to (goal_x, goal_y) -- guard-anchor
    // returns are always false.
    bool approach = false;
};

// Advances combat by dt seconds for every unit, ascending index (fully
// deterministic -- docs/re/combat.md §5: CONFIRMED no ROF jitter/RNG
// anywhere in vanilla combat, so this whole module is intentionally free of
// any randomness). Per living, non-dying unit: decays rearm_frames/fire_flash,
// validates/acquires a target, fires on ROF cadence when in range (applying
// damage_vs and starting the victim's death/retaliation), and emits a
// MoveRequest when an out-of-range target needs pursuing (explicit
// attack_order) or the unit needs to fall back to its guard anchor
// (auto-acquired target strayed too far). Dying units (die_time >= 0) only
// advance their die timer and are skipped for everything else; at
// kDieAnimSecs they tombstone (alive=false) but are never erased from
// `units`. Returns the accumulated MoveRequests for the caller (World) to
// path.
//
// Buildings & Construction Task 4: `buildings` adds an explicit-order-only
// attack-building path (Unit::target_building) alongside the pre-existing
// unit-targeting machinery above -- units never auto-acquire buildings, and
// buildings never retaliate (no weapons in this roster), so the unit-vs-unit
// code path is BIT-IDENTICAL to the pre-Task-4 3-arg step_combat whenever
// `buildings` is empty (the byte-identity demo gates depend on this). For a
// unit with target_building >= 0: the target is first validated (alive &&
// state != Dead, else cleared and normal targeting resumes); range is the
// distance to the NEAREST foundation cell (a 4x4 building is in range at its
// edge, not its center); in range, it fires on the same rearm_frames cadence as
// unit combat (damage_vs against the building's armor_ix, hp -= dmg, hp <= 0
// -> kill_building, which also clears every OTHER unit's target_building
// pointing at it); out of range with attack_order set, it emits a
// MoveRequest at the nearest foundation cell (approach = true, same
// firing-position-resolution contract as the unit-pursuit case).
// Multi-house/Alliances [RE: multihouse.md §3/§4]: `world` is consulted ONLY
// for the hostility predicate (World::is_enemy) used by target validation /
// auto-acquire; nothing in `world` is mutated. With houses_configured_ FALSE
// (every existing gate + host test), is_enemy(a,b) == (a != b) -> the targeting
// scan is byte-identical to the pre-alliances inlined `v.house != self_house`.
// Projectile phase (P2): `launches` (default nullptr) is the firewall. nullptr
// -> every shot hits instantly exactly as before (byte-identical); non-null ->
// a shot whose Unit::w_proj flies() appends a ProjectileLaunch instead of
// dealing damage (ROF/fire_flash reset unchanged).
std::vector<MoveRequest> step_combat(const World& world, std::vector<Unit>& units,
                                     std::vector<Building>& buildings, Occupancy& occ, double dt,
                                     std::vector<ProjectileLaunch>* launches = nullptr,
                                     ProjRng* rof_rng = nullptr);
// W5-B R-060 [RE: Modify_Damage FUN_00489090, disassembled]: one object's
// share of an explosion. damage<0 (healing) applies only within 8 leptons.
// Otherwise the falloff is LINEAR in float: atmax = damage*PercentAtMax,
// spread = ftol(CellSpread*256.0f), and when atmax != damage and spread != 0
// dmg = ftol((damage-atmax)*(spread-distance)/spread + atmax) (no clamp of the
// distance: past the edge it keeps falling), then max(0,.) and
// ftol(dmg * Verses[armor]), clamped to [CombatDamage] MaxDamage (10000).
// No minimum of 1 here (ObjectClass::ReceiveDamage FUN_005f4ba0 floors only
// buildings at 1 after this call).
int modify_damage(int damage, const std::vector<int>& verses, int armor_ix, double cell_spread,
                  double percent_at_max, int distance);

// W5-B R-060 [RE: DamageArea FUN_00489190 + the ring tables DAT_007EC3D0 /
// DAT_00ABC408 (369 offsets built by the static initializer @0x561540)]:
// cells scanned for an explosion of CellSpread `s` = the first
// spread_ring_count(ftol(s)) entries of spread_ring_offset().
int spread_ring_count(int r);
void spread_ring_offset(int k, int& dx, int& dy);

// W5-B R-060 [RE: DamageArea FUN_00489190]: the one damage applicator every
// hit goes through, at impact point (ix, iy, iz) in clone leptons (cell
// centre = cell*256; z = level*104 + altitude). Objects in the ring cells of
// ftol(CellSpread), within ftol(CellSpread*256) leptons (3-D distance; an
// airborne aircraft only when the impact is above the ground, its distance
// halved), ALL houses -- own and allied units and buildings take splash
// unless the warhead is AffectsAllies=no (ReceiveDamage 0x700DA0: the
// shooter's house allied with the victim -> 0). The shooter itself is
// excluded. A building is hit once per foundation cell in the ring set:
// distance 0 in the impact cell, else from that cell's centre. `shooter`
// carries the weapon (w_damage/w_verses/w_cell_spread/...); `src_unit` /
// `src_building` name the firer (-1 none) for exclusion, retaliation and
// kill credit.
struct AreaResult {
    int damage = 0;              // total HP removed (units + buildings)
    int first_unit = -1;         // lowest-index unit damaged (event victim)
};
// W6-04-03: `building_hook` (optional) sees every building hit before it is
// applied -- (building index, damage, distance in leptons); returning true
// means it handled the hit (the P4 CausesDelayKill rule of ability blasts).
using AreaBuildingHook = std::function<bool(int, int, int)>;
AreaResult damage_area(const World& world, std::vector<Unit>& units,
                       std::vector<Building>& buildings, Occupancy& occ, const Unit& shooter,
                       int src_unit, int src_building, int ix, int iy, int iz,
                       const AreaBuildingHook* building_hook = nullptr);

// Lepton position of a unit for damage/aim: cell-centred fx/fy plus the
// infantry sub-cell offset, z = level*104 + altitude when airborne.
int unit_lept_x(const Unit& u);
int unit_lept_y(const Unit& u);
int unit_lept_z(const Unit& u);
// Building aim point: the foundation centre (BuildingClass target coords).
void building_aim_lept(const Building& b, int level, int& x, int& y, int& z);

// W5-B R-063 [RE: FUN_006F6750 / FUN_006F6490 / FUN_004CC020]: is a target
// point (clone leptons, z absolute) inside u's ACTIVE weapon range? 3-D
// distance (ftol) <= ftol(Range*256) + `extra` leptons (+ the [ElevationModel]
// bonus for a SubjectToElevation projectile when the shooter stands >= 4
// levels above the target), and no upward cliff (> 3 levels) on the line for
// a SubjectToCliffs projectile. The elevation bonus and the trace are inert
// until World::arm_elevation (unit-test Worlds keep the plain 3-D check).
bool weapon_in_range(const World& world, const Unit& u, int tx, int ty, int tz, int extra = 0);
bool unit_in_weapon_range(const World& world, const Unit& u, const Unit& t);
// W6-04-14/29: may crusher `u` squash `v` (OmniCrusher / OmniCrushResistant,
// DeployedCrushable, else Crushable infantry)?
bool can_crush(const Unit& u, const Unit& v);

// W6-04-04 [RE: Reduce_Wall FUN_00480BC0] one hit on a wall (see WallStep in
// unit_w6.hpp). `rng` null (pure tests) = the random gate always passes.
WallStep reduce_wall(int stage, int levels, int strength, int dmg, bool connected, ProjRng* rng);
// The damage Reduce_Wall gets from a warhead (DamageArea @0x48958B): -1 for
// WallAbsoluteDestroyer, `damage` for Wall (or Wood on a wood wall), else 0 =
// the wall is untouched.
int wall_hit_damage(int wall_flags, bool wood_wall, int damage);
// A built wall (BuildingTypeDef::w6.wall_levels > 0) hit by a warhead with
// `wall_flags`: the stage model instead of HP. Returns true when `bi` is a
// wall (handled, hit or not).
bool w6_wall_building_hit(const World& world, std::vector<Unit>& units, std::vector<Building>& buildings,
                          int bi, int wall_flags, int damage);
// The same test from an explicit shooter point.
bool weapon_in_range_from(const World& world, const Unit& u, int ux, int uy, int uz, int tx, int ty,
                          int tz, int extra = 0);

// Gap fix C (G-50): RearmTimer in whole frames = ROF + Random(0,2)
// [veterancy.md V7]; rng null -> no jitter. (Net L0: frames, not seconds.)
int rearm_frames_for(int rof_frames, ProjRng* rng);

// W6-04-01 [RE: TechnoClass::Fire FUN_006FD210 @0x6FDD2C..: CurrentBurstIndex
// (+0x3B8)++, RearmTimer = GetROF(), CurrentBurstIndex %= Burst (+0x9C);
// GetROF FUN_006FC460 @0x6FC4D6: while CurrentBurstIndex < Burst the rearm is
// Random(3,5) (the per-burst delay table at UnitType+0xE44 has no rulesmd
// key on this install), after the last shot of the burst the full
// ROF + Random(0,2)]. Burst=1 is exactly rearm_frames_for (same RNG draw).
int burst_rearm_frames(int& burst_index, int burst, int rof_frames, ProjRng* rng);
// The unit form: the active weapon's Burst (Unit::w6.burst).
int unit_rearm_frames(Unit& u, ProjRng* rng);

}  // namespace ra2engine
