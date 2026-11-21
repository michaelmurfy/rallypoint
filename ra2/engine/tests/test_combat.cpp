#include "doctest.h"

#include <cstdint>
#include <vector>

#include "building.hpp"
#include "combat.hpp"
#include "occupancy.hpp"
#include "world.hpp"

using ra2engine::Building;
using ra2engine::Occupancy;
using ra2engine::Unit;

namespace {

std::vector<uint8_t> open_grid(int w, int h) { return std::vector<uint8_t>((size_t)w * h, 1); }

// Builds a combat-ready soldier at cell (x,y): GI (house 0, M60: 15 dmg / 20
// ROF frames / 4.0 range, armor "none") or Conscript (house 1, M1Carbine:
// 15 dmg / 25 ROF frames / 4.0 range, armor "flak") -- ground truth from
// docs/re/combat.md / the Combat Phase's extracted rulesmd values (both
// Strength=125, SA warhead). Claims its cell in `occ`, mirroring
// test_movement.cpp's make_mover helper.
Unit make_soldier(int x, int y, int house, int idx, Occupancy& occ) {
    Unit u;
    u.cell_x = x;
    u.cell_y = y;
    u.fx = x;
    u.fy = y;
    u.house = house;
    u.hp = u.max_hp = 125;
    u.w_verses = {100, 80, 80, 50, 25, 25, 75, 50, 25, 100, 100};  // kSAVerses
    u.w_damage = 15;
    u.w_range = 4.0;
    if (house == 0) {
        u.armor_ix = 0;  // none
        u.w_rof = 20;
    } else {
        u.armor_ix = 1;  // flak
        u.w_rof = 25;
    }
    occ.set(x, y, idx);
    return u;
}

// Multi-house/Alliances [RE: multihouse.md §3/§4]: step_combat now takes a
// const World& to consult World::is_enemy for target validation. These pure
// combat tests configure NO houses, so is_enemy(a,b) == (a != b) -- exactly the
// pre-alliances inlined `v.house != self_house` -- and a single shared default
// World (empty map, firewall closed) is sufficient and stateless for all of them.
const ra2engine::World& test_world() {
    static ra2assets::MapFile map;
    static ra2engine::World w(map);
    return w;
}

}  // namespace

TEST_CASE("armor_index maps the 11 class names, unknown -> none") {
    CHECK(ra2engine::armor_index("none") == 0);
    CHECK(ra2engine::armor_index("flak") == 1);
    CHECK(ra2engine::armor_index("plate") == 2);
    CHECK(ra2engine::armor_index("light") == 3);
    CHECK(ra2engine::armor_index("medium") == 4);
    CHECK(ra2engine::armor_index("heavy") == 5);
    CHECK(ra2engine::armor_index("wood") == 6);
    CHECK(ra2engine::armor_index("steel") == 7);
    CHECK(ra2engine::armor_index("concrete") == 8);
    CHECK(ra2engine::armor_index("special_1") == 9);
    CHECK(ra2engine::armor_index("special_2") == 10);
    CHECK(ra2engine::armor_index("Flak") == 1);      // case-insensitive (ini convention)
    CHECK(ra2engine::armor_index("weird") == 0);
}

TEST_CASE("damage_vs: hand-computed vectors from the real SA warhead") {
    const std::vector<int> sa = {100, 80, 80, 50, 25, 25, 75, 50, 25, 100, 100};
    CHECK(ra2engine::damage_vs(15, sa, 0) == 15);  // M1Carbine vs GI (none)
    CHECK(ra2engine::damage_vs(15, sa, 1) == 12);  // M60 vs Conscript (flak): 15*80/100
    CHECK(ra2engine::damage_vs(15, sa, 6) == 11);  // 15*75/100 = 11.25 -> trunc (Task 1 confirms)
    CHECK(ra2engine::damage_vs(15, {}, 1) == 15);  // absent verses -> 100% (defensive)
}

TEST_CASE("step_combat: in-range hostile is auto-acquired and killed on exact ROF cadence") {
    // GI (house 0) at (0,0); Conscript (house 1) at (3,0): distance 3 cells <= range 4.
    // M60: 12 dmg/shot vs flak; 125 hp -> 11 shots; ROF 20 frames = 4/3 s between shots.
    // Disarm the Conscript (w_damage=0) so only one direction fires.
    auto p = open_grid(10, 1);
    Occupancy occ(10, 1);
    std::vector<Unit> units;
    units.push_back(make_soldier(0, 0, 0, 0, occ));
    units.push_back(make_soldier(3, 0, 1, 1, occ));
    units[1].w_damage = 0;

    std::vector<Building> bs;
    // Net L0: one step_combat call is one game frame (whole-frame rearm timer),
    // so the cadence is checked at the logic rate.
    const double dt = 1.0 / 15.0;
    double t = 0;
    while (units[1].die_time < 0 && t < 30.0) { ra2engine::step_combat(test_world(), units, bs, occ, dt); t += dt; }
    REQUIRE(units[1].die_time >= 0);          // died
    // 11th shot fires after 10 full cooldowns: t = 10 * (20/15) = 13.333s (+1 frame slop)
    CHECK(t >= 10.0 * (20.0 / 15.0) - 2 * dt);
    CHECK(t <= 10.0 * (20.0 / 15.0) + 2 * dt);
    CHECK(units[1].hp <= 0);
}

TEST_CASE("step_combat: out-of-range hostile is NOT auto-acquired; explicit order requests pursuit") {
    // Conscript at (8,0): distance 8 > range 4 -> no auto-acquire, no fire.
    auto p = open_grid(10, 1);
    Occupancy occ(10, 1);
    std::vector<Unit> units;
    units.push_back(make_soldier(0, 0, 0, 0, occ));
    units.push_back(make_soldier(8, 0, 1, 1, occ));

    std::vector<Building> bs;
    auto reqs = ra2engine::step_combat(test_world(), units, bs, occ, 1.0 / 60.0);
    CHECK(units[0].target == -1);
    CHECK(reqs.empty());
    // Explicit attack order pursues: target set + MoveRequest toward the victim.
    // approach == true: the goal is the target's OWN cell; World (caller)
    // resolves a distinct firing position near it rather than pathing the
    // unit exactly there (docs/re/infantry-anim.md §7).
    units[0].target = 1; units[0].attack_order = true;
    reqs = ra2engine::step_combat(test_world(), units, bs, occ, 1.0 / 60.0);
    REQUIRE(reqs.size() == 1);
    CHECK(reqs[0].unit == 0);
    CHECK(reqs[0].goal_x == 8);
    CHECK(reqs[0].goal_y == 0);
    CHECK(reqs[0].approach == true);
}

TEST_CASE("step_combat: attack_order approach — live path within weapon range of the target needs no re-request") {
    // GI (idx0) pursuing a Conscript (idx1) that is out of firing range, but
    // the GI already has a LIVE path whose back() sits within weapon range
    // (4.0 cells) of the target's CURRENT position -- even though back() !=
    // the target's own cell. No fresh MoveRequest should be emitted: the
    // approach goal is still a valid firing position (range-based dedup,
    // replacing the old exact-cell-equality dedup).
    auto p = open_grid(20, 1);
    Occupancy occ(20, 1);
    std::vector<Unit> units;
    units.push_back(make_soldier(0, 0, 0, 0, occ));
    units.push_back(make_soldier(10, 0, 1, 1, occ));  // dist 10 > range 4 -> out of range
    units[0].target = 1;
    units[0].attack_order = true;
    // Live path already headed toward a firing position 3 cells short of the
    // target's cell (dist(7,0 -> 10,0) == 3 <= range 4).
    units[0].path = {{7, 0}};
    units[0].path_idx = 0;
    REQUIRE(units[0].moving());

    std::vector<Building> bs;
    auto reqs = ra2engine::step_combat(test_world(), units, bs, occ, 1.0 / 60.0);
    CHECK(reqs.empty());
}

TEST_CASE("step_combat: attack_order approach — target moving out of the live path's range triggers a fresh re-request") {
    // Same live-path setup as above, but the target has since moved further
    // away: path.back() (7,0) is now 6 cells from it (> range 4) -> the
    // approach goal has stopped being a firing position, so a fresh request
    // is emitted, aimed at the target's (new) cell.
    auto p = open_grid(20, 1);
    Occupancy occ(20, 1);
    std::vector<Unit> units;
    units.push_back(make_soldier(0, 0, 0, 0, occ));
    units.push_back(make_soldier(10, 0, 1, 1, occ));
    units[0].target = 1;
    units[0].attack_order = true;
    units[0].path = {{7, 0}};
    units[0].path_idx = 0;

    units[1].cell_x = 13;
    units[1].fx = 13.0;

    std::vector<Building> bs;
    auto reqs = ra2engine::step_combat(test_world(), units, bs, occ, 1.0 / 60.0);
    REQUIRE(reqs.size() == 1);
    CHECK(reqs[0].unit == 0);
    CHECK(reqs[0].approach == true);
    CHECK(reqs[0].goal_x == 13);  // fresh request aimed at the target's cell
    CHECK(reqs[0].goal_y == 0);
}

TEST_CASE("step_combat: return fire — idle victim retaliates against its attacker") {
    // GI fires first (in range, victim idle, victim.w_damage kept): once the
    // first shot lands the Conscript is no longer idle and must retaliate.
    auto p = open_grid(10, 1);
    Occupancy occ(10, 1);
    std::vector<Unit> units;
    units.push_back(make_soldier(0, 0, 0, 0, occ));
    units.push_back(make_soldier(3, 0, 1, 1, occ));

    std::vector<Building> bs;
    // Net L0: one step_combat call is one game frame (whole-frame rearm timer),
    // so the cadence is checked at the logic rate.
    const double dt = 1.0 / 15.0;
    double t = 0;
    // Run until the first GI shot lands.
    while (units[1].hp >= 125 && t < 5.0) { ra2engine::step_combat(test_world(), units, bs, occ, dt); t += dt; }
    REQUIRE(units[1].hp < 125);
    // After the first GI shot lands, the Conscript's target must be the GI.
    CHECK(units[1].target == 0);
}

TEST_CASE("step_combat: R3 a victim cruising to a flight goal does not return fire") {
    // A flyer on a move order (has_flight_goal, empty ground path) is not idle:
    // like a walking unit it keeps going instead of retaliating [aircraft.md
    // §6 -- a Move order overrides auto-attack; the same gate as acquisition].
    auto p = open_grid(10, 1);
    Occupancy occ(10, 1);
    std::vector<Unit> units;
    units.push_back(make_soldier(0, 0, 0, 0, occ));
    units.push_back(make_soldier(3, 0, 1, 1, occ));
    units[1].has_flight_goal = true;
    std::vector<Building> bs;
    // Net L0: one step_combat call is one game frame (whole-frame rearm timer),
    // so the cadence is checked at the logic rate.
    const double dt = 1.0 / 15.0;
    double t = 0;
    while (units[1].hp >= 125 && t < 5.0) { ra2engine::step_combat(test_world(), units, bs, occ, dt); t += dt; }
    REQUIRE(units[1].hp < 125);
    CHECK(units[1].target == -1);
}

TEST_CASE("step_combat: auto-acquired target dropped beyond guard stray; anchor return requested") {
    // Auto-acquire at range, then teleport the victim beyond range+kGuardStray:
    // target dropped, MoveRequest back to the guard anchor.
    auto p = open_grid(10, 1);
    Occupancy occ(10, 1);
    std::vector<Unit> units;
    units.push_back(make_soldier(0, 0, 0, 0, occ));
    units.push_back(make_soldier(3, 0, 1, 1, occ));
    units[1].w_damage = 0;  // isolate: only the GI's own targeting is under test

    std::vector<Building> bs;
    auto reqs = ra2engine::step_combat(test_world(), units, bs, occ, 1.0 / 60.0);
    REQUIRE(units[0].target == 1);
    CHECK(units[0].guard_x == 0);
    CHECK(units[0].guard_y == 0);

    // Teleport the Conscript to (8,0): 8 cells from the GI's anchor (0,0),
    // beyond the threat radius (W6-02-11: range 4 + 1 = 5) + kGuardStray (2) = 7.
    units[1].cell_x = 8;
    units[1].fx = 8.0;
    reqs = ra2engine::step_combat(test_world(), units, bs, occ, 1.0 / 60.0);
    CHECK(units[0].target == -1);
    CHECK(reqs.empty());  // the GI never left its own anchor, so no return move is needed.
}

TEST_CASE("step_combat: death invariants — dying then tombstoned, occupancy freed, no dangling targets") {
    // 3 units: two GIs both targeting one Conscript; kill it.
    auto p = open_grid(10, 2);
    Occupancy occ(10, 2);
    std::vector<Unit> units;
    units.push_back(make_soldier(0, 0, 0, 0, occ));
    units.push_back(make_soldier(0, 1, 0, 1, occ));
    units.push_back(make_soldier(3, 0, 1, 2, occ));
    units[2].w_damage = 0;  // isolate: the Conscript never damages either GI back

    std::vector<Building> bs;
    // Net L0: one step_combat call is one game frame (whole-frame rearm timer),
    // so the cadence is checked at the logic rate.
    const double dt = 1.0 / 15.0;
    double t = 0;
    while (units[2].die_time < 0 && t < 20.0) { ra2engine::step_combat(test_world(), units, bs, occ, dt); t += dt; }
    REQUIRE(units[2].die_time >= 0);
    CHECK(occ.at(units[2].cell_x, units[2].cell_y) == ra2engine::Occupancy::kEmpty);
    // Both attackers' targets cleared by validation on the next step.
    ra2engine::step_combat(test_world(), units, bs, occ, 1.0 / 60.0);
    CHECK(units[0].target == -1);
    CHECK(units[1].target == -1);
    // Die anim completes -> tombstone.
    for (double t = 0; t < 1.5; t += 1.0 / 60.0) ra2engine::step_combat(test_world(), units, bs, occ, 1.0 / 60.0);
    CHECK_FALSE(units[2].alive);
}

TEST_CASE("step_combat: leash drop off-anchor emits a return-to-anchor move request") {
    // GI auto-acquires Conscript at range; GI drifts off anchor while target
    // remains near the anchor, triggering leash drop. GI must emit a MoveRequest
    // back to the ORIGINAL anchor, not the target's cell.
    auto p = open_grid(20, 1);
    Occupancy occ(20, 1);
    std::vector<Unit> units;
    units.push_back(make_soldier(0, 0, 0, 0, occ));
    units.push_back(make_soldier(3, 0, 1, 1, occ));
    units[1].w_damage = 0;  // isolate: only the GI's targeting is under test

    std::vector<Building> bs;
    auto reqs = ra2engine::step_combat(test_world(), units, bs, occ, 1.0 / 60.0);
    REQUIRE(units[0].target == 1);
    CHECK(units[0].guard_x == 0);
    CHECK(units[0].guard_y == 0);

    // Simulate the GI having drifted to (8,0): 8 cells from anchor (0,0),
    // beyond weapon range (4) + kGuardStray (2) = 6; also > 4 cells from
    // Conscript at (3,0) to ensure out-of-range firing check.
    occ.release_all_of(0, -1, -1);
    units[0].cell_x = 8;
    units[0].fx = 8.0;
    units[0].fy = 0.0;
    occ.set(8, 0, 0);

    reqs = ra2engine::step_combat(test_world(), units, bs, occ, 1.0 / 60.0);
    // Target should be dropped due to leash violation (GI strayed too far).
    CHECK(units[0].target == -1);
    // Exactly one MoveRequest back to the ORIGINAL anchor.
    REQUIRE(reqs.size() == 1);
    CHECK(reqs[0].unit == 0);
    CHECK(reqs[0].goal_x == 0);  // anchor x, not target's cell_x
    CHECK(reqs[0].goal_y == 0);  // anchor y, not target's cell_y
    CHECK(reqs[0].approach == false);  // guard-return: move exactly there, no firing-position resolution
}

TEST_CASE("step_combat: an auto-acquired target that vanishes mid-approach -> the unit finishes the walk and guards there") {
    // The GI auto-acquires the Conscript (anchor (0,0)) and is walking an
    // approach path when the target dies. As in gamemd (Approach_Target's
    // Destination is a cell, which FootClass::PointerExpired does not clear),
    // it walks on to the end of the path -- no walk back to the anchor -- and
    // that end becomes its guard post.
    Occupancy occ(20, 1);
    std::vector<Unit> units;
    units.push_back(make_soldier(0, 0, 0, 0, occ));
    units.push_back(make_soldier(3, 0, 1, 1, occ));
    units[1].w_damage = 0;
    std::vector<Building> bs;
    ra2engine::step_combat(test_world(), units, bs, occ, 1.0 / 60.0);
    REQUIRE(units[0].target == 1);
    REQUIRE(units[0].attack_order == false);
    // On its approach path, one cell out from the anchor.
    occ.release_all_of(0, -1, -1);
    units[0].cell_x = 1;
    units[0].fx = 1.0;
    occ.set(1, 0, 0);
    units[0].path = {{1, 0}, {2, 0}, {3, 0}, {4, 0}};
    units[0].path_idx = 1;
    // The target vanishes (dies).
    units[1].alive = false;
    const auto reqs = ra2engine::step_combat(test_world(), units, bs, occ, 1.0 / 60.0);
    CHECK(units[0].target == -1);
    CHECK(reqs.empty());                       // no return-to-anchor request
    CHECK(units[0].path.size() == 4);          // the walk goes on
    CHECK(units[0].path_idx == 1);
    CHECK(units[0].guard_x == 4);              // guards the path's end
    CHECK(units[0].guard_y == 0);
}
TEST_CASE("step_combat: a vanished target leaves a unit standing at its post where it is") {
    Occupancy occ(20, 1);
    std::vector<Unit> units;
    units.push_back(make_soldier(0, 0, 0, 0, occ));
    units.push_back(make_soldier(3, 0, 1, 1, occ));
    units[1].w_damage = 0;
    std::vector<Building> bs;
    ra2engine::step_combat(test_world(), units, bs, occ, 1.0 / 60.0);
    REQUIRE(units[0].target == 1);
    units[1].alive = false;   // killed while the GI stands at its post
    const auto reqs = ra2engine::step_combat(test_world(), units, bs, occ, 1.0 / 60.0);
    CHECK(units[0].target == -1);
    CHECK(reqs.empty());
}

// ---- Task 4: attack-building (docs/superpowers/plans buildings phase) ----

TEST_CASE("combat: attack-building — range to nearest foundation cell, kill, tombstone") {
    // Grid-free: step_combat needs only units+buildings+occ.
    std::vector<Unit> units(1);
    Unit& u = units[0];
    u.alive = true; u.house = 0; u.hp = u.max_hp = 100;
    u.fx = u.cell_x = 0; u.fy = u.cell_y = 0;
    u.w_damage = 90; u.w_rof = 15; u.w_range = 4.0;
    u.w_verses = {100,100,100,100,100,100,100,100,100,100,100};
    u.attack_order = true; u.target_building = 0;

    std::vector<Building> bs(1);
    Building& b = bs[0];
    b.state = Building::State::Idle; b.alive = true; b.house = 1;
    b.cx = 3; b.cy = 0; b.fw = 4; b.fh = 4;       // nearest cell (3,0), dist 3 <= 4
    b.hp = b.max_hp = 180; b.armor_ix = 0;

    Occupancy occ(32, 32);
    occ.set(0, 0, 0);
    auto reqs = ra2engine::step_combat(test_world(), units, bs, occ, 1.0 / 15.0);
    CHECK(reqs.empty());                            // in range -> no pursuit
    CHECK(b.hp == 90);                              // one 90-damage hit
    for (int f = 0; f < 15; ++f)   // Net L0: the 15-frame rof elapses one frame per call
        ra2engine::step_combat(test_world(), units, bs, occ, 1.0 / 15.0);   // second hit
    CHECK(b.hp <= 0);
    CHECK(b.state == Building::State::Dead);
    CHECK(units[0].target_building == -1);          // target cleared on death
}

TEST_CASE("combat: attack-building out of range emits approach MoveRequest") {
    std::vector<Unit> units(1);
    Unit& u = units[0];
    u.alive = true; u.house = 0; u.hp = u.max_hp = 100;
    u.fx = u.cell_x = 0; u.fy = u.cell_y = 0;
    u.w_damage = 10; u.w_rof = 15; u.w_range = 2.0;
    u.w_verses = {100};
    u.attack_order = true; u.target_building = 0;
    std::vector<Building> bs(1);
    bs[0].state = Building::State::Idle; bs[0].alive = true; bs[0].house = 1;
    bs[0].cx = 6; bs[0].cy = 0; bs[0].fw = 2; bs[0].fh = 2; bs[0].hp = bs[0].max_hp = 50;
    Occupancy occ(32, 32);
    auto reqs = ra2engine::step_combat(test_world(), units, bs, occ, 1.0 / 15.0);
    REQUIRE(reqs.size() == 1);
    CHECK(reqs[0].unit == 0);
    CHECK(reqs[0].goal_x == 6); CHECK(reqs[0].goal_y == 0);   // nearest foundation cell
    CHECK(reqs[0].approach);
    CHECK(bs[0].hp == 50);
}

TEST_CASE("combat: empty buildings vector == legacy behavior") {
    // Byte-identity guard: with no buildings, the 4-arg step_combat must be
    // indistinguishable from the old 3-arg one. Re-run the exact setup of
    // "step_combat: in-range hostile is auto-acquired and killed on exact ROF
    // cadence" (this file's first infantry-vs-infantry TEST_CASE) with an
    // empty buildings vector and assert the same outcomes that test asserts.
    auto p = open_grid(10, 1);
    Occupancy occ(10, 1);
    std::vector<Unit> units;
    units.push_back(make_soldier(0, 0, 0, 0, occ));
    units.push_back(make_soldier(3, 0, 1, 1, occ));
    units[1].w_damage = 0;

    std::vector<Building> bs;  // empty -- legacy behavior guard
    const double dt = 1.0 / 15.0;   // Net L0: one call = one frame
    double t = 0;
    while (units[1].die_time < 0 && t < 30.0) { ra2engine::step_combat(test_world(), units, bs, occ, dt); t += dt; }
    REQUIRE(units[1].die_time >= 0);          // died
    CHECK(t >= 10.0 * (20.0 / 15.0) - 2 * dt);
    CHECK(t <= 10.0 * (20.0 / 15.0) + 2 * dt);
    CHECK(units[1].hp <= 0);
}
