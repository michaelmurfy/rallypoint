// R-210 bridges and cliffs: the height-aware search [RE: FootClass height gate
// FUN_004D9B70 -- a neighbour is entered at the same height, or one level up /
// down when the LOWER cell is a slope; a high-bridge deck stands at Level + 4;
// CellClass::IsClearToMove FUN_004833B0] and the allied gate opening
// (IsCellOccupied -> Move::ClosedGate). Pure pathfind.hpp tests, no assets.
#include "doctest.h"
#include "pathfind.hpp"

#include <cstdlib>

using namespace ra2engine;

namespace {

struct Terrain {
    int w, h;
    std::vector<std::uint8_t> pass;
    std::vector<std::int8_t> level;
    std::vector<std::uint8_t> slope;
    std::vector<std::uint8_t> deck;
    Terrain(int w_, int h_)
        : w(w_), h(h_), pass((size_t)w_ * h_, 1), level((size_t)w_ * h_, 0), slope((size_t)w_ * h_, 0),
          deck((size_t)w_ * h_, 0) {}
    std::size_t i(int x, int y) const { return (size_t)y * w + x; }
    PathTerrain t(int start_layer = 0, bool decks = true) const {
        PathTerrain r;
        r.level = &level;
        r.slope = &slope;
        r.deck = decks ? &deck : nullptr;
        r.start_layer = start_layer;
        return r;
    }
};

// Walk `path` with the same gate the A* used; returns the max standing-height
// change of a single step (must be <= 1 for a legal walk), -1 on a refused step.
int max_step(const Terrain& g, const std::vector<Cellxy>& path, int start_layer = 0) {
    const PathTerrain t = g.t(start_layer);
    int layer = start_layer, worst = 0;
    for (std::size_t k = 1; k < path.size(); ++k) {
        const int nl = path_step_layer(t, g.w, path[k - 1].x, path[k - 1].y, layer, path[k].x, path[k].y);
        if (nl < 0) return -1;
        const int ha = g.level[g.i(path[k - 1].x, path[k - 1].y)] + 4 * layer;
        const int hb = g.level[g.i(path[k].x, path[k].y)] + 4 * nl;
        worst = std::max(worst, std::abs(hb - ha));
        layer = nl;
    }
    return worst;
}

}  // namespace

TEST_CASE("R-210 path_step_layer: same height, one level over a slope, never a cliff") {
    Terrain g(4, 1);
    g.level = {4, 4, 3, 0};
    const PathTerrain t = g.t();
    CHECK(path_step_layer(t, g.w, 0, 0, 0, 1, 0) == 0);    // flat
    CHECK(path_step_layer(t, g.w, 1, 0, 0, 2, 0) == -1);   // down 1, the lower cell is no slope
    g.slope[g.i(2, 0)] = 1;
    CHECK(path_step_layer(t, g.w, 1, 0, 0, 2, 0) == 0);    // down 1 onto a ramp
    CHECK(path_step_layer(t, g.w, 2, 0, 0, 1, 0) == 0);    // up 1 off the ramp
    CHECK(path_step_layer(t, g.w, 2, 0, 0, 3, 0) == -1);   // a 3-level cliff
    g.slope[g.i(1, 0)] = 1;                                // the HIGHER cell sloped: still no
    g.slope[g.i(2, 0)] = 0;
    CHECK(path_step_layer(t, g.w, 1, 0, 0, 2, 0) == -1);
}

TEST_CASE("R-210 cliff: an amphibious mover cannot drop from the plateau into the water") {
    // 8 x 5: a plateau (level 4) on x <= 3, water/low ground (level 0) on x >= 4.
    // An amphibious grid: everything passable.
    Terrain g(8, 5);
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x <= 3; ++x) g.level[g.i(x, y)] = 4;
    const PathTerrain t = g.t();
    // No ramp: the two halves are not connected.
    CHECK(find_path_t(g.pass, g.w, g.h, {1, 2}, {6, 2}, nullptr, &t).empty());
    // 2-D (no terrain) the old search walks straight off the cliff.
    CHECK(find_path(g.pass, g.w, g.h, {1, 2}, {6, 2}).size() == 6);
    // A ramp down the cliff at y = 4: levels 4,3,2,1 then 0, each lower cell sloped.
    g.level[g.i(4, 4)] = 3; g.slope[g.i(4, 4)] = 1;
    g.level[g.i(5, 4)] = 2; g.slope[g.i(5, 4)] = 1;
    g.level[g.i(6, 4)] = 1; g.slope[g.i(6, 4)] = 1;
    g.level[g.i(7, 4)] = 0; g.slope[g.i(7, 4)] = 1;
    PathTerrain t2 = g.t();
    t2.version = 1;   // the terrain changed: a new memo key
    const auto p = find_path_t(g.pass, g.w, g.h, {1, 2}, {6, 2}, nullptr, &t2);
    REQUIRE(p.size() > 1);
    CHECK(p.back().x == 6);
    CHECK(p.back().y == 2);
    CHECK(max_step(g, p) == 1);   // only one-level steps
    bool used_ramp = false;
    for (const Cellxy& c : p) used_ramp = used_ramp || (c.y == 4 && c.x >= 4);
    CHECK(used_ramp);
    // Nearest search toward an unreachable low cell stays on the plateau.
    g.slope.assign(g.slope.size(), 0);
    g.level[g.i(4, 4)] = 0; g.level[g.i(5, 4)] = 0; g.level[g.i(6, 4)] = 0; g.level[g.i(7, 4)] = 0;
    PathTerrain t3 = g.t();
    t3.version = 2;
    const auto q = find_path_nearest_t(g.pass, g.w, g.h, {1, 2}, {6, 2}, 0, nullptr, &t3);
    REQUIRE(!q.empty());
    CHECK(q.back().x == 3);   // the cliff edge, not below it
    CHECK(max_step(g, q) == 0);
}

TEST_CASE("R-210 high bridge: land units cross on the deck, ships pass under, a broken deck blocks") {
    // 9 x 7: banks at level 4 on y <= 1 and y >= 5, a river (level 0) on
    // y = 2..4. A deck over the river at x = 4 (Level 0 + 4 = the banks).
    Terrain land(9, 7);
    for (int y = 0; y < 7; ++y)
        for (int x = 0; x < 9; ++x) {
            const bool river = y >= 2 && y <= 4;
            land.level[land.i(x, y)] = river ? 0 : 4;
            land.pass[land.i(x, y)] = river ? 0 : 1;   // the Land grid: water closed
        }
    for (int y = 2; y <= 4; ++y) {
        land.pass[land.i(4, y)] = 1;   // set_high_bridges opens the deck cells
        land.deck[land.i(4, y)] = 2;   // deck only (water under it)
    }
    const PathTerrain lt = land.t();
    const auto p = find_path_t(land.pass, land.w, land.h, {1, 0}, {7, 6}, nullptr, &lt);
    REQUIRE(p.size() > 1);
    CHECK(p.back().x == 7);
    CHECK(max_step(land, p) == 0);   // flat at the deck height all the way
    bool crossed = false;
    for (const Cellxy& c : p) crossed = crossed || (c.x == 4 && c.y == 3);
    CHECK(crossed);

    // A ship on the water grid (no deck layer) sails along the river under it.
    Terrain sea(9, 7);
    sea.level = land.level;
    for (int y = 0; y < 7; ++y)
        for (int x = 0; x < 9; ++x) sea.pass[sea.i(x, y)] = (y >= 2 && y <= 4) ? 1 : 0;
    const PathTerrain st = sea.t(0, false);
    const auto s = find_path_t(sea.pass, sea.w, sea.h, {0, 3}, {8, 3}, nullptr, &st);
    REQUIRE(s.size() == 9);   // straight under the deck column
    CHECK(max_step(sea, s) == 0);

    // The span breaks: the deck is gone and the deck cells close again.
    for (int y = 2; y <= 4; ++y) {
        land.pass[land.i(4, y)] = 0;
        land.deck[land.i(4, y)] = 0;
    }
    PathTerrain broken = land.t();
    broken.version = 1;
    CHECK(find_path_t(land.pass, land.w, land.h, {1, 0}, {7, 6}, nullptr, &broken).empty());
}

TEST_CASE("R-210 deck over land: the bank under the span and the deck above are both walkable") {
    // 7 x 5, all land. Row y = 2 is a high-bridge span (deck at level 0 + 4)
    // between plateaus at level 4 (x = 0 and x = 6); the ground under it is
    // level 0 (a bank road the bridge passes over).
    Terrain g(7, 5);
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 7; ++x) g.level[g.i(x, y)] = (x == 0 || x == 6) ? 4 : 0;
    for (int x = 1; x <= 5; ++x) g.deck[g.i(x, 2)] = 1;   // deck AND ground
    const PathTerrain t = g.t();
    // Plateau to plateau: over the deck.
    const auto over = find_path_t(g.pass, g.w, g.h, {0, 2}, {6, 2}, nullptr, &t);
    REQUIRE(over.size() == 7);
    CHECK(max_step(g, over) == 0);
    // The low road under the span: from (3, 0) to (3, 4) through (3, 2) at level 0.
    const auto under = find_path_t(g.pass, g.w, g.h, {3, 0}, {3, 4}, nullptr, &t);
    REQUIRE(under.size() == 5);
    CHECK(max_step(g, under) == 0);
    // A unit ON the deck cannot drop to the road below (4 levels).
    const PathTerrain on = g.t(1);
    CHECK(path_step_layer(on, g.w, 3, 2, 1, 3, 3) == -1);
    CHECK(path_step_layer(on, g.w, 3, 2, 1, 4, 2) == 1);   // along the deck
    CHECK(path_step_layer(on, g.w, 5, 2, 1, 6, 2) == 0);   // onto the plateau
}

TEST_CASE("R-210 gates: a closed (0) gate cell opens for its owner's allies only") {
    const int w = 5, h = 3;
    std::vector<std::uint8_t> pass((size_t)w * h, 0);
    for (int x = 0; x < w; ++x) pass[(size_t)1 * w + x] = 1;   // a one-row corridor
    pass[(size_t)1 * w + 2] = 0;                                  // the gate (a building)
    CHECK(find_path(pass, w, h, {0, 1}, {4, 1}).empty());
    std::vector<int> open{1 * w + 2};
    PathTerrain t;
    t.open_cells = &open;
    const auto p = find_path_t(pass, w, h, {0, 1}, {4, 1}, nullptr, &t);
    REQUIRE(p.size() == 5);
    CHECK(p[2].x == 2);
    // Without the allied opening the corridor stays shut (and the memo keys on it).
    PathTerrain closed;
    CHECK(find_path_t(pass, w, h, {0, 1}, {4, 1}, nullptr, &closed).empty());
}

TEST_CASE("R-210 a null terrain is the historical search") {
    const int w = 12, h = 9;
    std::vector<std::uint8_t> pass((size_t)w * h, 1);
    for (int y = 1; y < 8; ++y) pass[(size_t)y * w + 6] = 0;
    const auto a = find_path(pass, w, h, {1, 4}, {10, 4});
    const auto b = find_path_t(pass, w, h, {1, 4}, {10, 4}, nullptr, nullptr);
    REQUIRE(a.size() == b.size());
    for (std::size_t k = 0; k < a.size(); ++k) {
        CHECK(a[k].x == b[k].x);
        CHECK(a[k].y == b[k].y);
    }
    // A flat terrain (all level 0, no decks) finds the same path too.
    std::vector<std::int8_t> lv(pass.size(), 0);
    PathTerrain flat;
    flat.level = &lv;
    const auto c = find_path_t(pass, w, h, {1, 4}, {10, 4}, nullptr, &flat);
    REQUIRE(c.size() == a.size());
    for (std::size_t k = 0; k < a.size(); ++k) {
        CHECK(a[k].x == c[k].x);
        CHECK(a[k].y == c[k].y);
    }
}
