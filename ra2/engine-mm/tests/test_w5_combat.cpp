// W5-B (wave-5 combat and movement core) -- unit tests for the RE'd pieces:
// Modify_Damage (FUN_00489090), the DamageArea ring tables and applicator
// (FUN_00489190), infantry sub-cell spots (FindInfantrySubposition
// FUN_00481090), land-type speed and the crush/3-D range rules.
#include "doctest.h"

#include <cmath>

#include "combat.hpp"
#include "ra2assets/map.hpp"
#include "world.hpp"

using namespace ra2engine;

namespace {
const std::vector<int> kFlat(11, 100);

UnitCombat inf(int strength, int damage, double spread, double pam) {
    UnitCombat c;
    c.strength = strength;
    c.armor = "none";
    c.image = "E1";
    c.damage = damage;
    c.rof = 20;
    c.range = 5.0;
    c.verses = kFlat;
    c.cell_spread = spread;
    c.percent_at_max = pam;
    return c;
}

struct Rig {
    ra2assets::MapFile map;
    World world{map};
};
}  // namespace

TEST_CASE("w5 modify_damage: linear falloff to PercentAtMax at the CellSpread edge (FUN_00489090)") {
    // BlimpHE: CellSpread=2 (512 leptons), PercentAtMax=.5.
    CHECK(modify_damage(100, kFlat, 0, 2.0, 0.5, 0) == 100);
    CHECK(modify_damage(100, kFlat, 0, 2.0, 0.5, 256) == 75);
    CHECK(modify_damage(100, kFlat, 0, 2.0, 0.5, 512) == 50);
    // No clamp of the distance: past the edge it keeps falling.
    CHECK(modify_damage(100, kFlat, 0, 2.0, 0.5, 600) == 41);
    // Verses after the falloff, truncated: 75 * 0.5 = 37.
    std::vector<int> half(11, 50);
    CHECK(modify_damage(100, half, 0, 2.0, 0.5, 256) == 37);
    // PercentAtMax 1 (or CellSpread 0): no falloff; NO floor of 1 for a 0 product.
    CHECK(modify_damage(90, kFlat, 0, 0.3, 1.0, 70) == 90);
    std::vector<int> quarter(11, 25);
    CHECK(modify_damage(3, quarter, 0, 0.0, 1.0, 0) == 0);
    // Healing applies only within 8 leptons.
    CHECK(modify_damage(-50, kFlat, 0, 1.0, 1.0, 7) == -50);
    CHECK(modify_damage(-50, kFlat, 0, 1.0, 1.0, 8) == 0);
    // MaxDamage=10000.
    CHECK(modify_damage(50000, kFlat, 0, 0.0, 1.0, 0) == 10000);
}

TEST_CASE("w5 DamageArea ring tables: counts DAT_007EC3D0 and the first offsets") {
    CHECK(spread_ring_count(0) == 1);
    CHECK(spread_ring_count(1) == 9);
    CHECK(spread_ring_count(2) == 21);
    CHECK(spread_ring_count(10) == 309);
    int dx, dy;
    spread_ring_offset(0, dx, dy);
    CHECK((dx == 0 && dy == 0));
    // Every ring-2 cell is within Chebyshev 2 and not a 5x5 corner.
    for (int k = 9; k < 21; ++k) {
        spread_ring_offset(k, dx, dy);
        CHECK(std::max(std::abs(dx), std::abs(dy)) == 2);
        CHECK(!(std::abs(dx) == 2 && std::abs(dy) == 2));
    }
}

TEST_CASE("w5 damage_area: splash hits buildings and own units, not the shooter (R-060)") {
    Rig r;
    World& w = r.world;
    // Shooter (house 0) and a friendly bystander next to the enemy target.
    w.add_combat_unit(0, 5, 0, inf(100, 100, 2.0, 0.5));
    w.add_combat_unit(5, 5, 1, inf(1000, 0, 0.0, 1.0));   // enemy target
    w.add_combat_unit(6, 5, 0, inf(1000, 0, 0.0, 1.0));   // own unit, 1 cell away
    const int bi = w.add_building(0, 4, 3, 1, 1, 1, 1000, 0, 0.0, true);   // 2 cells off
    Unit shooter = w.units()[0];
    Occupancy occ = w.occupancy();
    const AreaResult ar = damage_area(w, w.units(), w.buildings(), occ, shooter, 0, -1,
                                      5 * 256, 5 * 256, 0);
    CHECK(w.units()[1].hp == 1000 - 100);   // distance 0
    CHECK(w.units()[2].hp == 1000 - 75);    // friendly fire, 256 leptons
    // Building cell (4,3): offset (-1,-2) is in ring 2; d = sqrt(256^2+512^2)=572 > 512.
    CHECK(w.buildings()[bi].hp == 1000);
    CHECK(w.units()[0].hp == 100);           // the shooter itself is excluded
    CHECK(ar.damage == 175);
    // A building whose cell is within the radius takes splash (floor 1).
    const int b2 = w.add_building(0, 5, 4, 1, 1, 1, 1000, 0, 0.0, true);   // 1 cell off
    damage_area(w, w.units(), w.buildings(), occ, shooter, 0, -1, 5 * 256, 5 * 256, 0);
    CHECK(w.buildings()[b2].hp == 1000 - 75);
}

TEST_CASE("w5 damage_area: a building is hit once per foundation cell in the ring set") {
    Rig r;
    World& w = r.world;
    w.add_combat_unit(0, 0, 0, inf(100, 100, 2.0, 0.5));
    const int bi = w.add_building(0, 4, 4, 3, 3, 1, 5000, 0, 0.0, true);   // cells 4..6
    Unit shooter = w.units()[0];
    Occupancy occ = w.occupancy();
    damage_area(w, w.units(), w.buildings(), occ, shooter, 0, -1, 5 * 256, 5 * 256, 0);
    // centre cell 100, 4 edge cells at 256 -> 75, 4 corners at 362 -> 64.
    CHECK(w.buildings()[bi].hp == 5000 - (100 + 4 * 75 + 4 * 64));
}

TEST_CASE("w5 damage_area: AffectsAllies=no spares own and allied units (ReceiveDamage 0x700DA0)") {
    Rig r;
    World& w = r.world;
    w.add_combat_unit(0, 5, 0, inf(100, 100, 2.0, 0.5));
    w.add_combat_unit(5, 5, 1, inf(1000, 0, 0.0, 1.0));
    w.add_combat_unit(6, 5, 0, inf(1000, 0, 0.0, 1.0));
    Unit shooter = w.units()[0];
    Occupancy occ = w.occupancy();
    shooter.w_affects_allies = false;
    damage_area(w, w.units(), w.buildings(), occ, shooter, 0, -1, 5 * 256, 5 * 256, 0);
    CHECK(w.units()[1].hp == 900);
    CHECK(w.units()[2].hp == 1000);
}

// ---- R-061 infantry sub-cells ------------------------------------------------
TEST_CASE("w5 occupancy: three infantry share a cell, a vehicle needs it empty (FUN_00481090)") {
    Occupancy occ(8, 8);
    occ.enable_subcells();
    for (int u = 0; u < 4; ++u) occ.mark_infantry(u, true);
    occ.mark_infantry(9, false);   // a vehicle
    CHECK(occ.can_enter(3, 3, 0));
    occ.set_spot(3, 3, 0, occ.free_spot(3, 3, 2));
    occ.set_spot(3, 3, 1, occ.free_spot(3, 3, 2));   // 2 taken -> row 2: 4
    occ.set_spot(3, 3, 2, occ.free_spot(3, 3, 2));   // -> 3
    CHECK(occ.spot_of(3, 3, 0) == 2);
    CHECK(occ.spot_of(3, 3, 1) == 4);
    CHECK(occ.spot_of(3, 3, 2) == 3);
    CHECK(occ.infantry_count(3, 3) == 3);
    CHECK_FALSE(occ.can_enter(3, 3, 3));   // full
    CHECK_FALSE(occ.can_enter(3, 3, 9));   // a vehicle may not drive into infantry
    occ.set(5, 5, 9);
    CHECK_FALSE(occ.can_enter(5, 5, 0));   // infantry may not enter a vehicle's cell
    CHECK(occ.free_spot(5, 5, 2) == -1);
    occ.release(3, 3, 1);
    CHECK(occ.can_enter(3, 3, 3));
    CHECK(occ.free_spot(3, 3, 3) == 4);    // left taken -> row 3: 4 first
    // spot offsets from the cell centre (DAT_0089D960 minus 128,128).
    int dx, dy;
    spot_offset(2, dx, dy);
    CHECK((dx == 64 && dy == -64));
    spot_offset(3, dx, dy);
    CHECK((dx == -64 && dy == 64));
    spot_offset(4, dx, dy);
    CHECK((dx == 64 && dy == 64));
    CHECK(spot_quadrant(0, 0) == 0);
    CHECK(spot_quadrant(90, -90) == 2);
    CHECK(spot_quadrant(-90, 90) == 3);
    CHECK(spot_quadrant(90, 90) == 4);
    CHECK(spot_quadrant(-90, -90) == 0);
}

namespace {
const AnimTable kNoAnims{};
Unit mover(int x, int y, Cellxy goal, const std::vector<uint8_t>& p, int w, int h, Occupancy& occ,
           int idx) {
    Unit u;
    u.cell_x = x;
    u.cell_y = y;
    u.fx = x;
    u.fy = y;
    u.path = find_path(p, w, h, {x, y}, goal);
    u.path_idx = u.path.size() > 1 ? 1 : 0;
    occ.mark_infantry(idx, true);
    occ.set(x, y, idx);
    return u;
}
}  // namespace

TEST_CASE("w5 step_units: three infantry walk into ONE cell; a fourth is held out") {
    const int w = 8, h = 3;
    std::vector<uint8_t> p(static_cast<std::size_t>(w) * h, 1);
    Occupancy occ(w, h);
    occ.enable_subcells();
    std::vector<Unit> us;
    for (int k = 0; k < 4; ++k) us.push_back(mover(0, k % 3, {6, 1}, p, w, h, occ, k));
    us[3].cell_x = us[3].cell_y = 0;   // (unit 3 shares unit 0's start cell)
    for (int f = 0; f < 15 * 12; ++f) step_units(us, p, p, w, h, occ, 1.0 / 15.0, 4.0, kNoAnims);
    CHECK(occ.infantry_count(6, 1) == 3);
    int in_goal = 0;
    for (int k = 0; k < 4; ++k) in_goal += (us[k].cell_x == 6 && us[k].cell_y == 1);
    CHECK(in_goal == 3);
    // The three hold three distinct spots.
    CHECK(occ.spot_unit(6, 1, 2) != occ.spot_unit(6, 1, 3));
    CHECK(occ.spot_unit(6, 1, 3) != occ.spot_unit(6, 1, 4));
}

// ---- R-062 land speed --------------------------------------------------------
TEST_CASE("w5 land_speed_mult: GroundType cost, slope, damage (DriveLocomotionClass FUN_004B2550)") {
    MoveSpeedCtx c;
    c.armed = true;
    std::vector<std::uint8_t> land(4, 0);
    std::vector<std::int8_t> level(4, 0);
    land[1] = 5;    // Tiberium
    land[2] = 1;    // Road
    level[3] = 1;   // a raised cell
    c.land = &land;
    c.level = &level;
    for (auto& row : c.cost)
        for (float& v : row) v = 1.0f;
    c.cost[5][1] = 0.7f;   // Tiberium Track=70%
    c.cost[5][2] = 0.5f;   // Tiberium Wheel=50%
    c.up[0] = 1.0;
    c.down[0] = 1.2;
    Unit tank;
    tank.is_vehicle = true;
    tank.land_speed_loco = true;   // Drive locomotor
    tank.speed_type = 1;
    tank.hp = tank.max_hp = 100;
    CHECK(land_speed_mult(c, tank, 4, 1, 0, 0) == doctest::Approx(1.0));
    CHECK(land_speed_mult(c, tank, 4, 1, 1, 0) == doctest::Approx(0.7));
    CHECK(land_speed_mult(c, tank, 4, 1, 2, 0) == doctest::Approx(1.0));   // Road = 100% (rulesmd)
    tank.speed_type = 2;
    CHECK(land_speed_mult(c, tank, 4, 1, 1, 0) == doctest::Approx(0.5));
    tank.speed_type = 1;
    tank.cell_x = 3;   // on the raised cell: driving down -> TrackedDownhill 1.2
    CHECK(land_speed_mult(c, tank, 4, 1, 2, 0) == doctest::Approx(1.2));
    tank.cell_x = 0;
    tank.hp = 25;      // ConditionRed 25% -> x0.75
    c.condition_red = 0.25;
    CHECK(land_speed_mult(c, tank, 4, 1, 0, 0) == doctest::Approx(0.75));
    Unit gi;           // the Walk locomotor does not read the table
    gi.hp = gi.max_hp = 100;
    CHECK(land_speed_mult(c, gi, 4, 1, 1, 0) == doctest::Approx(1.0));
}
