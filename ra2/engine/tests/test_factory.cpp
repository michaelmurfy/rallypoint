#include "doctest.h"
#include "../engine/factory.hpp"
using namespace ra2engine;

namespace {
Building fac(int type, int cx, int cy, int fw, int fh, int house = 0) {
    Building b; b.type = type; b.cx = cx; b.cy = cy; b.fw = fw; b.fh = fh;
    b.house = house; b.alive = true; b.state = Building::State::Idle;
    b.hp = b.max_hp = 100; return b;
}
BuildingTypeDef def(const char* id, int fw, int fh) {
    BuildingTypeDef d; d.id = id; d.fw = fw; d.fh = fh; return d;
}
}  // namespace

TEST_CASE("factory: exit_cell walks the 8-ring to the first free cell [RE-GATE §1.3]") {
    Building b = fac(3, 10, 20, 3, 2);           // GAWEAP 3x2 at (10,20)
    BuildingTypeDef d = def("GAWEAP", 3, 2);
    auto in_foundation = [](int x, int y) { return x >= 10 && x < 13 && y >= 20 && y < 22; };
    // Everything outside the foundation free -> the door cell: SOUTH of the
    // anchor (11,21), i.e. (cx + fw/2, cy + fh) = (11, 22).
    Cellxy e = exit_cell(b, d, [&](int x, int y) { return !in_foundation(x, y); });
    CHECK(e.x == 10 + 3 / 2);                     // cx + fw/2 = 11
    CHECK(e.y == 20 + 2);                         // cy + fh   = 22
    // Door cell occupied -> the NEXT free ring cell. Exit facing is
    // deterministic: anchor (11,21), r=1 ring is probed south-first, then
    // column-centered (|dx| asc), then west-before-east. South door (11,22)
    // is blocked, so the next probe is (dx,dy)=(-1,1) -> (10,22).
    Cellxy e2 = exit_cell(b, d, [&](int x, int y) {
        return !in_foundation(x, y) && !(x == 11 && y == 22);
    });
    CHECK(e2.x == 10);
    CHECK(e2.y == 22);
    CHECK_FALSE(in_foundation(e2.x, e2.y));
    // Everything occupied -> {-1,-1} (caller HOLDs the ready slot):
    Cellxy e3 = exit_cell(b, d, [](int, int) { return false; });
    CHECK(e3.x == -1);
    CHECK(e3.y == -1);
}

TEST_CASE("factory: primary_factory prefers the flagged primary, else first-built") {
    std::vector<Building> bs = {fac(3, 0, 0, 3, 2), fac(3, 8, 0, 3, 2)};  // two GAWEAP
    std::vector<int> ftypes = {3};
    CHECK(primary_factory(bs, ftypes, 0) == 0);   // none flagged -> first-built
    bs[1].primary = true;
    CHECK(primary_factory(bs, ftypes, 0) == 1);   // flagged wins
    // A Buildup / dead / enemy factory is not eligible:
    bs[1].state = Building::State::Buildup;
    CHECK(primary_factory(bs, ftypes, 0) == 0);   // falls back to the Idle one
    std::vector<Building> none;
    CHECK(primary_factory(none, ftypes, 0) == -1);
    std::vector<Building> enemy = {fac(3, 0, 0, 3, 2, /*house=*/1)};
    CHECK(primary_factory(enemy, ftypes, 0) == -1);
}

TEST_CASE("factory: set_primary flags one, clears every other same-category same-house") {
    std::vector<Building> bs = {fac(3, 0, 0, 3, 2), fac(3, 8, 0, 3, 2), fac(3, 16, 0, 3, 2)};
    std::vector<int> ftypes = {3};
    bs[0].primary = true;
    set_primary(bs, ftypes, 2);
    CHECK_FALSE(bs[0].primary);
    CHECK_FALSE(bs[1].primary);
    CHECK(bs[2].primary);
    // Another house's factory of the same type is untouched:
    bs.push_back(fac(3, 24, 0, 3, 2, /*house=*/1));
    bs[3].primary = true;
    set_primary(bs, ftypes, 0);
    CHECK(bs[0].primary);
    CHECK(bs[3].primary);                          // house 1 kept its own primary
    // A non-factory type is not touched:
    set_primary(bs, ftypes, /*b_idx=*/999);        // out of range -> no crash, no change
    CHECK(bs[0].primary);
}
