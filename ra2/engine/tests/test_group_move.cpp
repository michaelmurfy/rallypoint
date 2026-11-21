#include "doctest.h"

#include <algorithm>
#include <vector>

#include "group_move.hpp"
#include "occupancy.hpp"
#include "pathfind.hpp"  // Cellxy

using ra2engine::assign_formation;
using ra2engine::Cellxy;
using ra2engine::nearest_free_cells;
using ra2engine::Occupancy;

namespace {
// Fully-passable w*h grid.
std::vector<uint8_t> open_grid(int w, int h) { return std::vector<uint8_t>((size_t)w * h, 1); }
bool has_cell(const std::vector<Cellxy>& v, int x, int y) {
    return std::any_of(v.begin(), v.end(), [&](Cellxy c) { return c.x == x && c.y == y; });
}
}  // namespace

TEST_CASE("nearest_free_cells: returns the goal first, then nearest free walkable cells") {
    auto p = open_grid(10, 10);
    Occupancy occ(10, 10);
    auto cells = nearest_free_cells({5, 5}, 5, p, 10, 10, occ);
    REQUIRE(cells.size() == 5);
    CHECK(cells[0].x == 5);
    CHECK(cells[0].y == 5);                 // the center itself is nearest
    // All distinct.
    for (size_t i = 0; i < cells.size(); ++i)
        for (size_t j = i + 1; j < cells.size(); ++j)
            CHECK_FALSE((cells[i].x == cells[j].x && cells[i].y == cells[j].y));
    // All within one ring of the center (Chebyshev distance <= 1 for 5 cells).
    for (auto c : cells) {
        CHECK(std::abs(c.x - 5) <= 1);
        CHECK(std::abs(c.y - 5) <= 1);
    }
}

TEST_CASE("nearest_free_cells: skips impassable and occupied cells") {
    // Center (2,2) blocked (impassable). Ring-1 scan order is
    // (1,1),(1,3),(2,1),(2,3),(3,1),(3,3),(1,2),(3,2); occupy the first two
    // candidates so the occupancy filter is genuinely load-bearing: with
    // count=3 the result must be the NEXT three free cells, not the occupied
    // ones (if the is_free filter were dropped, (1,1)/(1,3) would leak in).
    auto p = open_grid(5, 5);
    p[2 * 5 + 2] = 0;              // block the center (2,2)
    Occupancy occ(5, 5);
    occ.set(1, 1, 0);              // occupy 1st ring-1 candidate
    occ.set(1, 3, 1);              // occupy 2nd ring-1 candidate
    auto cells = nearest_free_cells({2, 2}, 3, p, 5, 5, occ);
    REQUIRE(cells.size() == 3);
    CHECK_FALSE(has_cell(cells, 2, 2));   // impassable center excluded
    CHECK_FALSE(has_cell(cells, 1, 1));   // occupied cells excluded
    CHECK_FALSE(has_cell(cells, 1, 3));
    CHECK(has_cell(cells, 2, 1));         // ...replaced by the next free ones
    CHECK(has_cell(cells, 2, 3));
    CHECK(has_cell(cells, 3, 1));
}

TEST_CASE("nearest_free_cells: returns fewer than requested when the area is crowded") {
    // 3x1 strip, two of three cells occupied -> only 1 free walkable cell.
    auto p = open_grid(3, 1);
    Occupancy occ(3, 1);
    occ.set(0, 0, 0);
    occ.set(2, 0, 1);
    auto cells = nearest_free_cells({1, 0}, 5, p, 3, 1, occ);
    REQUIRE(cells.size() == 1);
    CHECK(cells[0].x == 1);
}

TEST_CASE("assign_formation: one target per mover, all distinct, mover cells count as free") {
    auto p = open_grid(10, 10);
    Occupancy occ(10, 10);
    // Three movers standing near the goal; their own cells are occupied by
    // themselves but must be treated as free candidate targets.
    std::vector<Cellxy> movers = {{5, 5}, {6, 5}, {5, 6}};
    occ.set(5, 5, 0);
    occ.set(6, 5, 1);
    occ.set(5, 6, 2);
    auto targets = assign_formation({5, 5}, movers, p, 10, 10, occ);
    REQUIRE(targets.size() == 3);
    // Distinct targets.
    for (size_t i = 0; i < targets.size(); ++i)
        for (size_t j = i + 1; j < targets.size(); ++j)
            CHECK_FALSE((targets[i].x == targets[j].x && targets[i].y == targets[j].y));
    // The goal cell (currently owned by mover 0) is a valid target.
    CHECK(has_cell(targets, 5, 5));
}

TEST_CASE("assign_formation: a mover with no available slot keeps its own cell") {
    // 2x1 grid where cell (1,0) is IMPASSABLE. Mover 1 stands on it (the pure
    // function takes mover cells as given). nearest_free_cells can only offer
    // the single walkable cell (0,0), so mover 0 (distance 0) wins it and
    // mover 1 is left unassigned -> it must keep its own current cell. This
    // exercises assign_formation's fallback default (targets = mover_cells).
    std::vector<uint8_t> p = {1, 0};
    Occupancy occ(2, 1);
    std::vector<Cellxy> movers = {{0, 0}, {1, 0}};
    occ.set(0, 0, 0);
    occ.set(1, 0, 1);
    auto targets = assign_formation({0, 0}, movers, p, 2, 1, occ);
    REQUIRE(targets.size() == 2);
    CHECK(targets[0].x == 0);
    CHECK(targets[0].y == 0);
    CHECK(targets[1].x == 1);   // unassigned mover keeps its own cell
    CHECK(targets[1].y == 0);
}
