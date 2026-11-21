#include "doctest.h"

#include <cmath>
#include <vector>

#include "occupancy.hpp"
#include "pathfind.hpp"
#include "world.hpp"

using ra2engine::Cellxy;
using ra2engine::Occupancy;
using ra2engine::step_units;
using ra2engine::Unit;

namespace {

// step_units's trailing AnimTable arg (Task 2): these tests don't exercise
// per-image animation, so an empty table (-> default_anim() fallback for
// every unit) is enough; named+reused since every call site needs it.
const ra2engine::AnimTable kNoAnims{};

std::vector<uint8_t> open_grid(int w, int h) { return std::vector<uint8_t>((size_t)w * h, 1); }

// Build a unit at (x,y) with a straight-line intent toward `goal` computed by
// find_path over `passable`; seeds occupancy ownership of its start cell.
Unit make_mover(int x, int y, Cellxy goal, const std::vector<uint8_t>& p, int w, int h,
                Occupancy& occ, int idx) {
    Unit u;
    u.cell_x = x;
    u.cell_y = y;
    u.fx = x;
    u.fy = y;
    u.path = ra2engine::find_path(p, w, h, {x, y}, goal);
    u.path_idx = u.path.size() > 1 ? 1 : 0;
    occ.set(x, y, idx);
    return u;
}

// Step the whole set for `seconds` at 60 Hz.
void run(std::vector<Unit>& units, const std::vector<uint8_t>& p, int w, int h, Occupancy& occ,
         double seconds, double cps = 4.0) {
    const double dt = 1.0 / 60.0;
    for (double t = 0; t < seconds; t += dt) step_units(units, p, p,w, h, occ, dt, cps, kNoAnims);
}

// Assert no two units share an integer cell.
void check_no_overlap(const std::vector<Unit>& units) {
    for (size_t i = 0; i < units.size(); ++i)
        for (size_t j = i + 1; j < units.size(); ++j)
            CHECK_FALSE((units[i].cell_x == units[j].cell_x &&
                         units[i].cell_y == units[j].cell_y));
}

}  // namespace

TEST_CASE("step_units: a single unit walks its path to the goal") {
    auto p = open_grid(10, 1);
    Occupancy occ(10, 1);
    std::vector<Unit> units;
    units.push_back(make_mover(0, 0, {9, 0}, p, 10, 1, occ, 0));
    REQUIRE(units[0].moving());
    run(units, p, 10, 1, occ, 10.0);
    CHECK_FALSE(units[0].moving());
    CHECK(units[0].cell_x == 9);
    CHECK(units[0].cell_y == 0);
    CHECK(units[0].fx == 9.0);
}

TEST_CASE("step_units: reservation prevents two units ever sharing a cell (funnel)") {
    // Two rooms (x<=2 and x>=4 on y=0..2) joined only by the single cell (3,1).
    const int w = 7, h = 3;
    std::vector<uint8_t> p((size_t)w * h, 0);
    auto open = [&](int x, int y) { p[(size_t)y * w + x] = 1; };
    for (int y = 0; y < 3; ++y) { open(0, y); open(1, y); open(2, y); }  // left room
    open(3, 1);                                                          // the gap
    for (int y = 0; y < 3; ++y) { open(4, y); open(5, y); open(6, y); }  // right room
    Occupancy occ(w, h);
    std::vector<Unit> units;
    units.push_back(make_mover(0, 0, {6, 0}, p, w, h, occ, 0));
    units.push_back(make_mover(0, 1, {6, 1}, p, w, h, occ, 1));
    units.push_back(make_mover(0, 2, {6, 2}, p, w, h, occ, 2));
    REQUIRE(units[0].moving());
    // Step and assert the no-overlap invariant on EVERY frame.
    const double dt = 1.0 / 60.0;
    for (double t = 0; t < 20.0; t += dt) {
        step_units(units, p, p,w, h, occ, dt, 4.0, kNoAnims);
        check_no_overlap(units);
    }
    // All three funnel through and reach the right room at distinct cells.
    for (const auto& u : units) CHECK_FALSE(u.moving());
    check_no_overlap(units);
    for (const auto& u : units) CHECK(u.cell_x == 6);
}

TEST_CASE("step_units: boxed-in unit cannot move and stops cleanly (no spin)") {
    auto p = open_grid(3, 3);
    Occupancy occ(3, 3);
    std::vector<Unit> units;
    // Center unit surrounded on all 8 neighbours by parked units.
    int idx = 0;
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x)
            if (!(x == 1 && y == 1)) {
                Unit u;
                u.cell_x = x; u.cell_y = y; u.fx = x; u.fy = y;
                occ.set(x, y, idx);
                units.push_back(u);
                ++idx;
            }
    // The center unit is index 8; order it toward a far cell.
    Unit c;
    c.cell_x = 1; c.cell_y = 1; c.fx = 1; c.fy = 1;
    c.path = ra2engine::find_path(p, 3, 3, {1, 1}, {2, 2});
    c.path_idx = c.path.size() > 1 ? 1 : 0;
    occ.set(1, 1, idx);
    units.push_back(c);

    run(units, p, 3, 3, occ, 5.0);
    // It never left its cell and it is no longer "moving" (gave up cleanly).
    CHECK(units.back().cell_x == 1);
    CHECK(units.back().cell_y == 1);
    CHECK_FALSE(units.back().moving());
    check_no_overlap(units);
}

TEST_CASE("step_units: head-on in a 1-wide corridor -> no overlap, both stop within CloseEnough") {
    auto p = open_grid(4, 1);  // cells (0..3, 0)
    Occupancy occ(4, 1);
    std::vector<Unit> units;
    units.push_back(make_mover(0, 0, {3, 0}, p, 4, 1, occ, 0));  // wants to go right
    units.push_back(make_mover(3, 0, {0, 0}, p, 4, 1, occ, 1));  // wants to go left
    const double dt = 1.0 / 60.0;
    for (double t = 0; t < 10.0; t += dt) {
        step_units(units, p, p,4, 1, occ, dt, 4.0, kNoAnims);
        check_no_overlap(units);
    }
    // W7-11-03/10 [RE: FUN_004B2550 Move 6]: once both are held up each
    // reads the other as parked (Temp) and, within CloseEnough (2.25 cells)
    // of its goal, stops there -- the move ends; neither overlaps.
    CHECK_FALSE(units[0].moving());
    CHECK_FALSE(units[1].moving());
    check_no_overlap(units);
}

TEST_CASE("step_units: head-on with room routes both around to their goals") {
    auto p = open_grid(4, 2);  // 2 rows give a detour lane
    Occupancy occ(4, 2);
    std::vector<Unit> units;
    units.push_back(make_mover(0, 0, {3, 0}, p, 4, 2, occ, 0));
    units.push_back(make_mover(3, 0, {0, 0}, p, 4, 2, occ, 1));
    const double dt = 1.0 / 60.0;
    for (double t = 0; t < 20.0; t += dt) {
        step_units(units, p, p,4, 2, occ, dt, 4.0, kNoAnims);
        check_no_overlap(units);
    }
    CHECK(units[0].cell_x == 3);
    CHECK(units[0].cell_y == 0);
    CHECK(units[1].cell_x == 0);
    CHECK(units[1].cell_y == 0);
}

TEST_CASE("step_units: 3-unit rotational contention resolves without overlap") {
    // Three units on a 2x2 open square, each ordered to the cell the next one
    // occupies (a rotational near-deadlock): 0 at (0,0)->(1,0), 1 at
    // (1,0)->(1,1), 2 at (1,1)->(0,0). The free cell (0,1) plus the seniority
    // stagger must let the ring resolve -- every unit terminates (arrives or
    // gives up) and the one-unit-per-cell invariant holds on every frame.
    // This executes the N>=3 termination argument the pairwise tests miss.
    auto p = open_grid(2, 2);
    Occupancy occ(2, 2);
    std::vector<Unit> units;
    units.push_back(make_mover(0, 0, {1, 0}, p, 2, 2, occ, 0));
    units.push_back(make_mover(1, 0, {1, 1}, p, 2, 2, occ, 1));
    units.push_back(make_mover(1, 1, {0, 0}, p, 2, 2, occ, 2));
    const double dt = 1.0 / 60.0;
    for (double t = 0; t < 15.0; t += dt) {
        step_units(units, p, p,2, 2, occ, dt, 4.0, kNoAnims);
        check_no_overlap(units);
    }
    // Every unit terminated (no perpetual churn) and no two share a cell.
    for (const auto& u : units) CHECK_FALSE(u.moving());
    check_no_overlap(units);
    // All three cells they occupy are distinct AND on the grid.
    for (const auto& u : units) {
        CHECK(u.cell_x >= 0); CHECK(u.cell_x <= 1);
        CHECK(u.cell_y >= 0); CHECK(u.cell_y <= 1);
    }
}

// W4-MC-01 [RE: gamemd MovementZone name table @0x81AA98]: every Amphibious*
// zone and SpeedType=Amphibious resolve to the amphibious (land+water) domain.
TEST_CASE("W4-MC-01: Amphibious* MovementZones and SpeedType=Amphibious are amphibious") {
    using ra2engine::is_amphibious_zone;
    CHECK(is_amphibious_zone("AmphibiousDestroyer", "Amphibious"));   // TANY / GHOST
    CHECK(is_amphibious_zone("AmphibiousCrusher", "Amphibious"));     // APC
    CHECK(is_amphibious_zone("Amphibious ", "Hover"));                // SAPC (trailing blank)
    CHECK(is_amphibious_zone("amphibiousdestroyer", ""));             // CPHAL (no SpeedType)
    CHECK(is_amphibious_zone("", "Amphibious"));
    CHECK_FALSE(is_amphibious_zone("Normal", "Foot"));                // E1
    CHECK_FALSE(is_amphibious_zone("Infantry", "Foot"));
    CHECK_FALSE(is_amphibious_zone("Water", "Float"));
    CHECK_FALSE(is_amphibious_zone("WaterBeach", "Float"));
    CHECK_FALSE(is_amphibious_zone("", "Hover"));                      // hover alone is not enough
}
