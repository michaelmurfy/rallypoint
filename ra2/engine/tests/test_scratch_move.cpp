// Scratch reproduction for movement defects (TEMPORARY -- revert before finishing).
#include "doctest.h"
#include <cstdio>
#include <vector>
#include "occupancy.hpp"
#include "pathfind.hpp"
#include "world.hpp"

using ra2engine::Cellxy;
using ra2engine::Occupancy;
using ra2engine::step_units;
using ra2engine::Unit;

namespace {
const ra2engine::AnimTable kNoAnims{};
std::vector<uint8_t> open_grid(int w, int h) { return std::vector<uint8_t>((size_t)w * h, 1); }

Unit make_unit(int x, int y, Cellxy goal, const std::vector<uint8_t>& p, int w, int h, Occupancy& occ,
               int idx, bool vehicle) {
    Unit u;
    u.alive = true;
    u.cell_x = x; u.cell_y = y; u.fx = x; u.fy = y;
    u.is_vehicle = vehicle;
    if (vehicle) { u.rot_u16_per_sec = 5.0 * ra2engine::kRotU16PerRotPerSec; u.speed_cps = 4.5; }
    u.path = ra2engine::find_path(p, w, h, {x, y}, goal);
    u.path_idx = u.path.size() > 1 ? 1 : 0;
    occ.set(x, y, idx);
    return u;
}

void run(std::vector<Unit>& units, const std::vector<uint8_t>& p, int w, int h, Occupancy& occ,
         double seconds, double cps = 4.0, const ra2engine::StepTerrain* terrain = nullptr) {
    const double dt = 1.0 / 60.0;
    for (double t = 0; t < seconds; t += dt)
        step_units(units, p, p, w, h, occ, dt, cps, kNoAnims, nullptr, nullptr, nullptr, nullptr, nullptr, terrain);
}
}  // namespace

TEST_CASE("scratch: vehicle descends a 3-step ramp") {
    const int w = 3, h = 6;
    auto p = open_grid(w, h);
    // Ramp column x=1: top flat level 3 (slope 0), ramp cells level 2/1/0
    // (slope 1), bottom flat level 0 (slope 0) -- the test_movement convention.
    std::vector<std::int8_t> level((size_t)w * h, 0);
    std::vector<std::uint8_t> slope((size_t)w * h, 0);
    for (int y = 0; y < h; ++y) {
        int lv = (y < 3) ? (3 - y) : 0;
        for (int x = 0; x < w; ++x) {
            level[(size_t)y * w + x] = lv;
            // y 1,2,3 are the ramp cells (lower cell of each step is a slope)
            slope[(size_t)y * w + x] = (y >= 1 && y <= 3) ? 1 : 0;
        }
    }
    ra2engine::StepTerrain terr;
    terr.land.level = &level;
    terr.land.slope = &slope;
    ra2engine::PathTerrain pt;
    pt.level = &level;
    pt.slope = &slope;
    Occupancy occ(w, h);
    std::vector<Unit> units;
    units.push_back(make_unit(1, 0, {1, 5}, p, w, h, occ, 0, /*vehicle=*/true));
    // Re-path WITH the terrain (as order_unit_move does) so the path respects
    // the height gate.
    units[0].path = ra2engine::find_path_t(p, w, h, {1, 0}, {1, 5}, nullptr, &pt);
    units[0].path_idx = 1;
    units[0].level = 3;
    std::printf("[scratch] ramp path:");
    for (auto c : units[0].path) std::printf(" (%d,%d)", c.x, c.y);
    std::printf("\n");
    run(units, p, w, h, occ, 30.0, 4.5, &terr);
    CHECK_FALSE(units[0].moving());
    std::printf("[scratch] vehicle ramp: final=(%d,%d) level=%d\n", units[0].cell_x, units[0].cell_y, units[0].level);
    CHECK(units[0].cell_y == 5);
    CHECK(units[0].level == 0);
}

TEST_CASE("scratch: vehicle around a 90-degree corner") {
    // L-shaped wall at x=5 (y 0..4) then y=4 (x 5..10); goal inside the bend.
    const int w = 12, h = 12;
    auto p = open_grid(w, h);
    for (int y = 0; y <= 4; ++y) p[(size_t)y * w + 5] = 0;   // vertical wall
    for (int x = 5; x <= 10; ++x) p[(size_t)4 * w + x] = 0;  // horizontal wall
    Occupancy occ(w, h);
    std::vector<Unit> units;
    units.push_back(make_unit(2, 7, {8, 1}, p, w, h, occ, 0, /*vehicle=*/true));
    run(units, p, w, h, occ, 60.0, 4.5);
    std::printf("[scratch] vehicle corner: start=(2,7) final=(%d,%d) moving=%d\n", units[0].cell_x, units[0].cell_y, units[0].moving()?1:0);
    CHECK(units[0].cell_x == 8);
    CHECK(units[0].cell_y == 1);
}

TEST_CASE("scratch: infantry around a 90-degree corner") {
    const int w = 12, h = 12;
    auto p = open_grid(w, h);
    for (int y = 0; y <= 4; ++y) p[(size_t)y * w + 5] = 0;
    for (int x = 5; x <= 10; ++x) p[(size_t)4 * w + x] = 0;
    Occupancy occ(w, h);
    std::vector<Unit> units;
    units.push_back(make_unit(2, 7, {8, 1}, p, w, h, occ, 0, /*vehicle=*/false));
    run(units, p, w, h, occ, 60.0, 4.5);
    std::printf("[scratch] inf corner: final=(%d,%d) moving=%d\n", units[0].cell_x, units[0].cell_y, units[0].moving()?1:0);
    CHECK(units[0].cell_x == 8);
    CHECK(units[0].cell_y == 1);
}

TEST_CASE("scratch: diagonal does not cut a wall corner") {
    // A wall cell at (3,3). Moving from (2,3) to (3,2) would cut the corner of
    // the wall if the wall cell were a solid block; but a single blocked cell
    // (not a wall line) does not block diagonals in the A* corner rule...
    // Build a proper wall corner: cells (3,3) and (3,2) blocked; a path from
    // (2,3) to (4,4) must NOT take the (3,3)-adjacent diagonal through the corner.
    const int w = 8, h = 8;
    auto p = open_grid(w, h);
    p[(size_t)3 * w + 3] = 0;  // wall
    p[(size_t)2 * w + 3] = 0;  // wall above it
    auto path = ra2engine::find_path(p, w, h, {2, 4}, {4, 2});
    std::printf("[scratch] corner-cut path:");
    for (auto c : path) std::printf(" (%d,%d)", c.x, c.y);
    std::printf("\n");
    // The wall cells must never be stepped on.
    for (auto c : path) {
        const bool on_wall1 = (c.x == 3 && c.y == 3);
        const bool on_wall2 = (c.x == 3 && c.y == 2);
        CHECK_FALSE(on_wall1);
        CHECK_FALSE(on_wall2);
    }
}

TEST_CASE("scratch: three vehicles into a tight goal do not overlap") {
    const int w = 8, h = 8;
    auto p = open_grid(w, h);
    Occupancy occ(w, h);
    std::vector<Unit> units;
    // Three vehicles spread out, all ordered to the same 1-wide gap cell.
    units.push_back(make_unit(1, 1, {4, 4}, p, w, h, occ, 0, true));
    units.push_back(make_unit(1, 4, {4, 4}, p, w, h, occ, 1, true));
    units.push_back(make_unit(4, 1, {4, 4}, p, w, h, occ, 2, true));
    const double dt = 1.0 / 60.0;
    for (double t = 0; t < 60.0; t += dt) {
        step_units(units, p, p, w, h, occ, dt, 4.5, kNoAnims);
        for (size_t i = 0; i < units.size(); ++i)
            for (size_t j = i + 1; j < units.size(); ++j)
                if (units[i].cell_x == units[j].cell_x && units[i].cell_y == units[j].cell_y)
                    std::printf("[scratch] OVERLAP u%zu,u%zu at (%d,%d) t=%.1f\n", i, j, units[i].cell_x, units[i].cell_y, t);
    }
    for (size_t i = 0; i < units.size(); ++i)
        std::printf("[scratch] group final u%zu=(%d,%d) moving=%d\n", i, units[i].cell_x, units[i].cell_y, units[i].moving()?1:0);
}
