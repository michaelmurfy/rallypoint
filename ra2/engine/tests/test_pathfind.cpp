#include "doctest.h"
#include "pathfind.hpp"

#include <cstdlib>

using ra2engine::Cellxy;
using ra2engine::find_path;

namespace {

std::vector<uint8_t> open_grid(int width, int height) {
    return std::vector<uint8_t>(static_cast<size_t>(width) * height, 1);
}

int idx(int width, int x, int y) { return y * width + x; }

// Every consecutive pair in the path must be 8-adjacent, both cells must be
// passable, and (per the corner-cutting rule) a diagonal step must have both
// orthogonal "corner" cells passable too.
void check_valid_path(const std::vector<Cellxy>& path, const std::vector<uint8_t>& passable,
                       int width, int height) {
    REQUIRE(!path.empty());
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        const Cellxy& a = path[i];
        const Cellxy& b = path[i + 1];
        int dx = b.x - a.x;
        int dy = b.y - a.y;
        CHECK(std::abs(dx) <= 1);
        CHECK(std::abs(dy) <= 1);
        CHECK((dx != 0 || dy != 0));
        REQUIRE(a.x >= 0); REQUIRE(a.x < width); REQUIRE(a.y >= 0); REQUIRE(a.y < height);
        REQUIRE(b.x >= 0); REQUIRE(b.x < width); REQUIRE(b.y >= 0); REQUIRE(b.y < height);
        CHECK(passable[idx(width, a.x, a.y)] != 0);
        CHECK(passable[idx(width, b.x, b.y)] != 0);
        if (dx != 0 && dy != 0) {
            // Diagonal move: both orthogonal "corners" must be passable too.
            CHECK(passable[idx(width, a.x + dx, a.y)] != 0);
            CHECK(passable[idx(width, a.x, a.y + dy)] != 0);
        }
    }
}

}  // namespace

TEST_CASE("path: open grid corner-to-corner is octile-optimal") {
    const int w = 10, h = 10;
    auto grid = open_grid(w, h);
    Cellxy start{0, 0}, goal{9, 9};

    auto path = find_path(grid, w, h, start, goal);

    REQUIRE(!path.empty());
    CHECK(path.front().x == start.x);
    CHECK(path.front().y == start.y);
    CHECK(path.back().x == goal.x);
    CHECK(path.back().y == goal.y);
    // 9 diagonal steps + the start cell = 10 cells total (octile-optimal: no
    // detour, pure diagonal run corner to corner).
    CHECK(path.size() == 10);
    check_valid_path(path, grid, w, h);
}

TEST_CASE("path: vertical wall with a gap forces a detour") {
    const int w = 10, h = 10;
    auto grid = open_grid(w, h);
    // Wall at x=5 blocking every row except y=0 (the gap, at the far edge
    // from the start/goal row). Note: with 8-connected octile movement a gap
    // placed on the SAME row as start/goal (e.g. y=9 for a start/goal row of
    // y=5) would let diagonal moves "absorb" the detour for free -- the total
    // step count would tie the straight-line distance. Placing the gap far
    // from the row (y=0, 5 rows away) makes the round trip's vertical travel
    // exceed the horizontal travel, so it provably can't be hidden by
    // diagonal shortcuts and the path must come out strictly longer than the
    // direct (no-wall) route.
    for (int y = 0; y < h; ++y) {
        if (y != 0) grid[idx(w, 5, y)] = 0;
    }
    Cellxy start{0, 5}, goal{9, 5};

    auto path = find_path(grid, w, h, start, goal);

    REQUIRE(!path.empty());
    CHECK(path.front().x == start.x);
    CHECK(path.front().y == start.y);
    CHECK(path.back().x == goal.x);
    CHECK(path.back().y == goal.y);
    // Straight-line (no wall) would be 9 steps / 10 cells. The wall cells
    // immediately below the gap, (5,1), are impassable, so the corner-cutting
    // rule also forbids any DIAGONAL approach into/out of the gap cell (5,0)
    // itself (both diagonal neighbors on either side share (5,1) as one of
    // their two required corner cells) -- the only legal moves through the
    // gap are the cardinal ones from/to (4,0) and (6,0). That forces a
    // mandatory flat 3-cell run (4,0)-(5,0)-(6,0):
    //   leg1 (0,5)->(4,0): max(4,5)=5 steps
    //   +1 step (4,0)->(5,0), +1 step (5,0)->(6,0)
    //   leg2 (6,0)->(9,5): max(3,5)=5 steps
    // total 12 steps / 13 cells -- strictly longer than the direct route.
    CHECK(path.size() > 10);
    CHECK(path.size() == 13);
    check_valid_path(path, grid, w, h);

    bool through_gap = false;
    for (const auto& c : path) {
        if (c.x == 5 && c.y == 0) through_gap = true;
        if (c.x == 5) CHECK(c.y == 0);  // the only passable cell in column 5
    }
    CHECK(through_gap);
}

TEST_CASE("path: diagonal corner-cutting through a blocked orthogonal cell is forbidden") {
    const int w = 5, h = 5;
    auto grid = open_grid(w, h);
    // Block one of the two orthogonal cells adjacent to the diagonal move
    // from (1,1) to (2,2); the other, (1,2), stays passable.
    grid[idx(w, 2, 1)] = 0;
    Cellxy start{1, 1}, goal{2, 2};

    auto path = find_path(grid, w, h, start, goal);

    REQUIRE(!path.empty());
    CHECK(path.front().x == start.x);
    CHECK(path.front().y == start.y);
    CHECK(path.back().x == goal.x);
    CHECK(path.back().y == goal.y);
    // The direct diagonal (2 cells) must be rejected; the only legal route is
    // via (1,2), i.e. 3 cells.
    CHECK(path.size() == 3);
    check_valid_path(path, grid, w, h);
}

TEST_CASE("path: W5-A walls touching only at a corner leave a diagonal gap") {
    // Walls along row 2 and column 2 with the crossing cell (2,2) open: the
    // wall cells around it touch only at corners, so (1,1)->(2,2)->(3,3) is
    // the only way through.
    const int w = 5, h = 5;
    auto grid = open_grid(w, h);
    for (int i = 0; i < w; ++i) { grid[idx(w, i, 2)] = 0; grid[idx(w, 2, i)] = 0; }
    grid[idx(w, 2, 2)] = 1;   // the gap cell
    std::vector<uint8_t> leak(grid.size(), 0);
    for (std::size_t i = 0; i < grid.size(); ++i) leak[i] = grid[i] ? 0 : 1;   // every block is a wall
    // Without the mask the corner rule seals the gap.
    CHECK(find_path(grid, w, h, Cellxy{1, 1}, Cellxy{3, 3}).empty());
    // With the wall mask the units walk through it diagonally.
    auto path = find_path(grid, w, h, Cellxy{1, 1}, Cellxy{3, 3}, &leak);
    REQUIRE(path.size() == 3);
    CHECK(path[1].x == 2);
    CHECK(path[1].y == 2);
    // A non-wall blocked corner still blocks even with a mask present.
    std::vector<uint8_t> none(grid.size(), 0);
    CHECK(find_path(grid, w, h, Cellxy{1, 1}, Cellxy{3, 3}, &none).empty());
}

TEST_CASE("path: blocked goal returns empty") {
    const int w = 5, h = 5;
    auto grid = open_grid(w, h);
    grid[idx(w, 4, 4)] = 0;
    auto path = find_path(grid, w, h, Cellxy{0, 0}, Cellxy{4, 4});
    CHECK(path.empty());
}

TEST_CASE("path: blocked start returns empty") {
    const int w = 5, h = 5;
    auto grid = open_grid(w, h);
    grid[idx(w, 0, 0)] = 0;
    auto path = find_path(grid, w, h, Cellxy{0, 0}, Cellxy{4, 4});
    CHECK(path.empty());
}

TEST_CASE("path: out-of-range start or goal returns empty") {
    const int w = 5, h = 5;
    auto grid = open_grid(w, h);
    CHECK(find_path(grid, w, h, Cellxy{-1, 0}, Cellxy{4, 4}).empty());
    CHECK(find_path(grid, w, h, Cellxy{0, 0}, Cellxy{5, 4}).empty());
    CHECK(find_path(grid, w, h, Cellxy{0, -1}, Cellxy{4, 4}).empty());
    CHECK(find_path(grid, w, h, Cellxy{0, 0}, Cellxy{4, 5}).empty());
}

TEST_CASE("path: start == goal returns a single-cell path") {
    const int w = 5, h = 5;
    auto grid = open_grid(w, h);
    auto path = find_path(grid, w, h, Cellxy{2, 2}, Cellxy{2, 2});
    REQUIRE(path.size() == 1);
    CHECK(path[0].x == 2);
    CHECK(path[0].y == 2);
}

TEST_CASE("path: fully walled-off goal returns empty") {
    const int w = 5, h = 5;
    auto grid = open_grid(w, h);
    // Surround (2,2) on all 8 sides with impassable cells, isolating it.
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            grid[idx(w, 2 + dx, 2 + dy)] = 0;
        }
    }
    auto path = find_path(grid, w, h, Cellxy{0, 0}, Cellxy{2, 2});
    CHECK(path.empty());
}

TEST_CASE("find_path returns empty on passable/dimension size mismatch") {
    const int w = 5, h = 5;
    const int expected_size = w * h;  // 25

    // Test 1: passable vector is too small (size = expected_size - 1 = 24)
    {
        auto grid = open_grid(w, h);
        grid.pop_back();  // Remove one element, making it 24 instead of 25
        REQUIRE(grid.size() == static_cast<size_t>(expected_size - 1));
        auto path = find_path(grid, w, h, Cellxy{0, 0}, Cellxy{4, 4});
        CHECK(path.empty());
    }

    // Test 2: passable vector is too large (size = expected_size + 5 = 30)
    {
        auto grid = open_grid(w, h);
        grid.resize(static_cast<size_t>(expected_size + 5), 1);
        REQUIRE(grid.size() == static_cast<size_t>(expected_size + 5));
        auto path = find_path(grid, w, h, Cellxy{0, 0}, Cellxy{4, 4});
        CHECK(path.empty());
    }

    // Test 3: correctly-sized all-passable 5x5 with start != goal should yield non-empty path
    {
        auto grid = open_grid(w, h);
        REQUIRE(grid.size() == static_cast<size_t>(expected_size));
        auto path = find_path(grid, w, h, Cellxy{0, 0}, Cellxy{4, 4});
        CHECK(!path.empty());  // Proves the guard doesn't reject valid input
    }
}

// W2-SK-05/16: nearest-reachable relaxation and the expansion budget.
TEST_CASE("pathfind: nearest walks to the closest reachable cell") {
    const int W = 10, H = 10;
    std::vector<uint8_t> p(W * H, 1);
    for (int y = 0; y < H; ++y) p[y * W + 6] = 0;   // a full wall at x=6
    // Impassable goal on the wall -> relaxed to a passable neighbour.
    auto a = ra2engine::find_path_nearest(p, W, H, {1, 5}, {6, 5}, 0);
    REQUIRE(!a.empty());
    CHECK(a.back().x == 5);
    CHECK(a.back().y == 5);
    // Unreachable goal behind the wall -> ends at the wall face nearest it.
    auto b = ra2engine::find_path_nearest(p, W, H, {1, 5}, {8, 5}, 0);
    REQUIRE(!b.empty());
    CHECK(b.back().x == 5);
    CHECK(b.back().y == 5);
    CHECK(ra2engine::find_path(p, W, H, {1, 5}, {8, 5}).empty());   // exact search unchanged
    // Bounded: a tiny budget gives up; a big one matches find_path.
    std::vector<uint8_t> q(W * H, 1);
    CHECK(ra2engine::find_path_bounded(q, W, H, {0, 0}, {9, 9}, 2).empty());
    CHECK(ra2engine::find_path_bounded(q, W, H, {0, 0}, {9, 9}, 1000).size() ==
          ra2engine::find_path(q, W, H, {0, 0}, {9, 9}).size());
}
