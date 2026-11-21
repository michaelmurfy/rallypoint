#include "doctest.h"
#include "../engine/building.hpp"
#include "../engine/occupancy.hpp"
using namespace ra2engine;

static std::vector<std::uint8_t> open_grid(int w, int h) {
    return std::vector<std::uint8_t>((size_t)w * h, 1);
}
static Building mk(int cx, int cy, int fw, int fh, int house = 0) {
    Building b; b.cx = cx; b.cy = cy; b.fw = fw; b.fh = fh; b.house = house;
    b.state = Building::State::Idle; b.hp = b.max_hp = 100; return b;
}

TEST_CASE("foundation_cells: 3x2 enumerates row-major from origin") {
    auto c = foundation_cells(5, 7, 3, 2);
    REQUIRE(c.size() == 6);
    CHECK(c[0].x == 5); CHECK(c[0].y == 7);
    CHECK(c[2].x == 7); CHECK(c[2].y == 7);
    CHECK(c[5].x == 7); CHECK(c[5].y == 8);
}

TEST_CASE("placement_check: cell tests") {
    auto pass = open_grid(20, 20);
    Occupancy occ(20, 20);
    std::vector<Building> bs = {mk(2, 2, 4, 4)};   // own ConYard at (2,2)

    // valid: adjacent to the ConYard (gap 1 <= Adjacent 2)
    auto ok = placement_check(7, 2, 2, 2, 0, 2, pass, 20, 20, occ, bs, false);
    CHECK(ok.ok); CHECK(ok.adjacency);
    REQUIRE(ok.cell_ok.size() == 4);
    CHECK(ok.cell_ok[0]);

    // out of bounds cell -> that cell flagged, ok false
    auto oob = placement_check(19, 19, 2, 2, 0, 2, pass, 20, 20, occ, bs, true);
    CHECK_FALSE(oob.ok);
    CHECK(oob.cell_ok[0]);          // (19,19) in bounds
    CHECK_FALSE(oob.cell_ok[3]);    // (20,20) out

    // impassable terrain
    auto pass2 = pass; pass2[3 * 20 + 8] = 0;   // cell (8,3)
    auto imp = placement_check(8, 3, 1, 1, 0, 2, pass2, 20, 20, occ, bs, true);
    CHECK_FALSE(imp.ok); CHECK_FALSE(imp.cell_ok[0]);

    // unit in the way
    occ.set(7, 3, /*unit*/ 0);
    auto unit = placement_check(7, 2, 2, 2, 0, 2, pass, 20, 20, occ, bs, false);
    CHECK_FALSE(unit.ok); CHECK_FALSE(unit.cell_ok[2]);  // (7,3) is index 2 of 2x2
    occ.clear(7, 3);

    // overlap an existing building
    auto ovl = placement_check(4, 4, 2, 2, 0, 2, pass, 20, 20, occ, bs, true);
    CHECK_FALSE(ovl.ok); CHECK_FALSE(ovl.cell_ok[0]);
}

TEST_CASE("placement_check: adjacency [RE: construction.md §9]") {
    auto pass = open_grid(30, 30);
    Occupancy occ(30, 30);
    std::vector<Building> bs = {mk(2, 2, 4, 4)};   // own, cells x/y in [2,5]

    // gap exactly Adjacent (2): candidate at x=8 -> gap = 8-5-1 = 2 -> OK
    CHECK(placement_check(8, 2, 2, 2, 0, 2, pass, 30, 30, occ, bs, false).adjacency);
    // gap 3 -> fails
    auto far = placement_check(9, 2, 2, 2, 0, 2, pass, 30, 30, occ, bs, false);
    CHECK_FALSE(far.adjacency); CHECK_FALSE(far.ok);
    CHECK(far.cell_ok[0]);   // cells individually fine; adjacency is the failure
    // enemy building doesn't grant adjacency
    std::vector<Building> enemy = {mk(2, 2, 4, 4, /*house*/ 1)};
    CHECK_FALSE(placement_check(8, 2, 2, 2, 0, 2, pass, 30, 30, occ, enemy, false).adjacency);
    // dead building doesn't grant adjacency
    std::vector<Building> dead = {mk(2, 2, 4, 4)};
    kill_building(dead[0]);
    CHECK_FALSE(placement_check(8, 2, 2, 2, 0, 2, pass, 30, 30, occ, dead, false).adjacency);
    // bypass skips adjacency only
    CHECK(placement_check(20, 20, 2, 2, 0, 2, pass, 30, 30, occ, bs, true).ok);
}

TEST_CASE("building_frame: healthy/damaged at ConditionYellow") {
    Building b = mk(0, 0, 2, 2);
    b.hp = 100; b.max_hp = 100;
    // 4-frame SHP: frames 0,1 image; 2,3 shadow [RE: building-render.md §4]
    CHECK(building_frame(b, 4, 0.5) == 0);
    b.hp = 51; CHECK(building_frame(b, 4, 0.5) == 0);
    b.hp = 50; CHECK(building_frame(b, 4, 0.5) == 1);   // <= threshold
    b.hp = 1;  CHECK(building_frame(b, 4, 0.5) == 1);
    CHECK(building_frame(b, 2, 0.5) == 0);              // 1 image frame only
}

TEST_CASE("buildup_frame + step_buildings: plays once then Idle") {
    Building b = mk(0, 0, 2, 2);
    b.state = Building::State::Buildup;
    b.buildup_total = 2.0;
    // 20-frame MK SHP -> 10 image frames, period = 2.0/10 = 0.2 s
    // [RE-GATE: building-render.md §5 — frame = trunc(time/period), clamped]
    b.buildup_time = 0.0;  CHECK(buildup_frame(b, 20) == 0);
    b.buildup_time = 0.19; CHECK(buildup_frame(b, 20) == 0);
    b.buildup_time = 1.0;  CHECK(buildup_frame(b, 20) == 5);   // trunc(1.0/0.2)
    b.buildup_time = 1.9;  CHECK(buildup_frame(b, 20) == 9);
    b.buildup_time = 9.9;  CHECK(buildup_frame(b, 20) == 9);   // clamped

    b.buildup_time = 0.0;  // Reset before testing step_buildings
    std::vector<Building> v = {b};
    auto done = step_buildings(v, 1.0);
    CHECK(done.empty());
    CHECK(v[0].state == Building::State::Buildup);
    done = step_buildings(v, 1.0);
    REQUIRE(done.size() == 1); CHECK(done[0] == 0);
    CHECK(v[0].state == Building::State::Idle);
}

TEST_CASE("kill_building + death tombstone") {
    std::vector<Building> v = {mk(0, 0, 2, 2)};
    kill_building(v[0]);
    CHECK(v[0].state == Building::State::Dead);
    CHECK(v[0].hp == 0);
    CHECK(v[0].die_time == doctest::Approx(0.0));
    kill_building(v[0]);                       // idempotent
    CHECK(v[0].die_time == doctest::Approx(0.0));
    step_buildings(v, kBuildingDieSecs + 0.01);
    CHECK_FALSE(v[0].alive);
    CHECK(v[0].state == Building::State::Dead);
}

TEST_CASE("building: Repair & Sell fields are inert by default [RE: repair-sell.md §A2/§B2]") {
    ra2engine::Building b;
    CHECK_FALSE(b.being_repaired);
    CHECK_FALSE(b.selling);
    CHECK(b.sell_time == doctest::Approx(0.0));
    CHECK_FALSE(b.sold);
    ra2engine::BuildingTypeDef t;
    CHECK_FALSE(t.unsellable);
    CHECK(t.click_repairable);          // the recovered DEFAULT YES
    CHECK_FALSE(t.crewed);
    CHECK(t.soylent == 0);
    // kill_building must not disturb any of them (sell sets `sold` itself).
    b.being_repaired = true;
    ra2engine::kill_building(b);
    CHECK(b.being_repaired);
    CHECK_FALSE(b.sold);
}

TEST_CASE("placement_check: BaseNormal=no neighbours do not extend the base (gap G-28) [RE: construction.md §9]") {
    auto pass = open_grid(30, 30);
    Occupancy occ(30, 30);
    Building wall = mk(10, 10, 1, 1);
    wall.type = 1;                                  // type 1 = a BaseNormal=no wall
    Building yard = mk(2, 2, 4, 4);
    yard.type = 0;                                  // type 0 = BaseNormal=yes ConYard
    const std::vector<char> base_normal = {1, 0};
    std::vector<Building> only_wall = {wall};
    // Next to the wall only: legacy (nullptr) accepts, BaseNormal-aware refuses.
    CHECK(placement_check(12, 10, 2, 2, 0, 2, pass, 30, 30, occ, only_wall, false).ok);
    CHECK_FALSE(placement_check(12, 10, 2, 2, 0, 2, pass, 30, 30, occ, only_wall, false,
                                &base_normal).ok);
    // A BaseNormal=yes neighbour still extends the base.
    std::vector<Building> both = {yard, wall};
    CHECK(placement_check(8, 2, 2, 2, 0, 2, pass, 30, 30, occ, both, false, &base_normal).ok);
}
