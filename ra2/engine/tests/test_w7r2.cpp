// w7r2 (wave-7 mission-script restore): the engine fixes behind m-sov01-real,
// m-sov07-real and m-sov09-real.
#include "doctest.h"

#include <vector>

#include "asset_fixture.hpp"
#include "combat.hpp"
#include "locomotion.hpp"
#include "occupancy.hpp"
#include "pathfind.hpp"
#include "ra2assets/map.hpp"
#include "world.hpp"

using namespace ra2engine;

namespace {

World::CombatW7Rules armed_rules() {
    World::CombatW7Rules r;
    r.armed = true;
    return r;
}

Unit vehicle_at(int x, int y, Occupancy& occ, int idx) {
    Unit u;
    u.cell_x = x;
    u.cell_y = y;
    u.fx = x;
    u.fy = y;
    u.is_vehicle = true;
    u.speed_cps = 10 * 15.0 / 256.0;
    occ.set(x, y, idx);
    return u;
}

// A fully walkable, unoccupied W x H rectangle origin, or false.
bool open_rect(const World& w, int W, int H, int& x0, int& y0) {
    for (int y = 4; y + H < w.grid_h() - 4; ++y)
        for (int x = 4; x + W < w.grid_w() - 4; ++x) {
            bool ok = true;
            for (int dy = 0; dy < H && ok; ++dy)
                for (int dx = 0; dx < W && ok; ++dx)
                    if (!w.walkable(x + dx, y + dy) || w.unit_at_cell(x + dx, y + dy) >= 0 ||
                        w.building_at_cell(x + dx, y + dy) >= 0)
                        ok = false;
            if (ok) { x0 = x; y0 = y; return true; }
        }
    return false;
}

}  // namespace

// [RE: UnitClass::Scatter FUN_00742880 @0x74294D: a unit with a Destination
// returns only when nokidding is 0; Drive Move 6 calls ScatterContent with
// force = nokidding = 1 (0x4B2CD6..0x4B2CE0)].
TEST_CASE("w7r2: two vehicles meeting head-on in a one-cell lane push each other aside (no deadlock)") {
    const int w = 12, h = 1;
    std::vector<std::uint8_t> p((std::size_t)w * h, 1);
    Occupancy occ(w, h);
    std::vector<Unit> units{vehicle_at(1, 0, occ, 0), vehicle_at(10, 0, occ, 1)};
    units[0].path = find_path(p, w, h, {1, 0}, {10, 0});
    units[0].path_idx = 1;
    units[1].path = find_path(p, w, h, {10, 0}, {1, 0});
    units[1].path_idx = 1;
    std::vector<int> scatter;
    MoveSpeedCtx sc;
    sc.scatter_out = &scatter;
    const AnimTable anims{};
    bool pushed_held_up = false;
    for (int f = 0; f < 300 && !pushed_held_up; ++f) {
        scatter.clear();
        step_units(units, p, p, w, h, occ, 1.0 / 15.0, 0.0, anims, nullptr, &sc);
        for (int s : scatter) pushed_held_up = pushed_held_up || (units[(std::size_t)s].moving() && units[(std::size_t)s].is_vehicle);
    }
    CHECK(pushed_held_up);   // the held-up vehicle (it still has a destination) is told to scatter
}

TEST_CASE("w7r2: World::order_scatter's cell push moves a held-up vehicle, the player's X does not") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("skipped: real map assets not found"); return; }
    World w(ra2fixture::mapsmd03_map());
    int x0 = 0, y0 = 0;
    REQUIRE(open_rect(w, 12, 5, x0, y0));
    UnitCombat tank;
    tank.strength = 300;
    tank.is_vehicle = true;
    w.add_combat_unit(x0 + 1, y0 + 2, 0, tank);
    w.order_unit_move(0, x0 + 9, y0 + 2);
    REQUIRE(w.units()[0].moving());
    w.units()[0].wait_time = 1.0;          // held up on its route
    w.order_scatter(0);                    // the player's X: a unit on the move ignores it
    CHECK(w.units()[0].path.back().x == x0 + 9);
    w.order_scatter(0, /*cell_push=*/true);   // ScatterContent from a blocked mover
    const Cellxy g = w.units()[0].path.back();
    CHECK(std::abs(g.x - (x0 + 1)) <= 1);
    CHECK(std::abs(g.y - (y0 + 2)) <= 1);
}

// [RE: FootClass::Approach_Target FUN_004D55B0 @0x4D5EE0..0x4D67B2, the in-range
// test FUN_006F6750]: the approach cell is on the ring from the weapon range
// inward, on the unit's side, and passes the building in-range rule.
TEST_CASE("w7r2: an attack on a building approaches a free in-range cell on the unit's side") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("skipped: real map assets not found"); return; }
    World w(ra2fixture::mapsmd03_map());
    int x0 = 0, y0 = 0;
    REQUIRE(open_rect(w, 16, 7, x0, y0));
    w.arm_combat_w7(armed_rules());
    BuildingTypeDef pill;
    pill.id = "GAPILL";
    pill.strength = 400;
    w.set_building_types({pill});
    const int bx = x0 + 13, by = y0 + 3;
    w.add_building(0, bx, by, 1, 1, 1, 400, 0, 0.0, /*instant=*/true);
    UnitCombat e2;
    e2.strength = 125;
    e2.damage = 15;
    e2.rof = 25;
    e2.range = 4.0;
    for (int k = 0; k < 3; ++k) w.add_combat_unit(x0 + 1, by - 1 + k, 0, e2);
    for (int k = 0; k < 3; ++k) w.order_unit_attack_building(k, 0);
    w.step(1.0 / 15.0);
    std::vector<Cellxy> goals;
    for (int k = 0; k < 3; ++k) {
        const Unit& u = w.units()[(std::size_t)k];
        REQUIRE(u.moving());
        const Cellxy g = u.path.back();
        // range 4 + (1+1)*64 leptons -> the first ring is 4 cells out
        const int dx = g.x - bx, dy = g.y - by;
        CHECK(dx < 0);                       // the unit's (west) side
        CHECK(dx * dx + dy * dy <= 5 * 5);   // within range + the (w+h)*64 widening
        CHECK(dx * dx + dy * dy >= 3 * 3);   // on the outer ring, not next to the pillbox
        goals.push_back(g);
    }
}

// [RE: GetFireError FUN_006FB5D0 -- the warhead's Verses gate every weapon]:
// the dog's ParasiteDog jaw (0 % vs light) never holds a vehicle target, even
// as an explicit (team / call-for-help) order.
TEST_CASE("w7r2: an ability weapon drops an explicit target its warhead cannot hurt") {
    ra2assets::MapFile map;
    World w(map);
    w.arm_combat_w7(armed_rules());
    Occupancy occ(60, 1);
    std::vector<Unit> units(2);
    std::vector<Building> bs;
    Unit& dog = units[0];
    dog.house = 1;
    dog.hp = dog.max_hp = 100;
    dog.w_verses = {100, 100, 100, 0, 0, 0, 0, 0, 0, 0, 0};   // ParasiteDog
    dog.vet.base.verses = dog.w_verses;
    dog.w_damage = 30;
    dog.w_range = 1.5;
    dog.vet.base.range = 1.5;
    dog.abil.fire_kind = AbilityKind::Parasite;
    dog.target = 1;
    dog.attack_order = true;
    occ.set(0, 0, 0);
    Unit& ifv = units[1];
    ifv.cell_x = 3;
    ifv.fx = 3;
    ifv.house = 6;
    ifv.hp = ifv.max_hp = 200;
    ifv.is_vehicle = true;
    ifv.armor_ix = 3;   // light
    occ.set(3, 0, 1);
    step_combat(w, units, bs, occ, 1.0 / 15.0);
    CHECK(units[0].target == -1);
    // ...while an infantry target (armor none) stays legal.
    units[0].target = 1;
    units[0].attack_order = true;
    units[1].is_vehicle = false;
    units[1].armor_ix = 0;
    step_combat(w, units, bs, occ, 1.0 / 15.0);
    CHECK(units[0].target == 1);
}
