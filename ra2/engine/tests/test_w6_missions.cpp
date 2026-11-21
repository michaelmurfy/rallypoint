// Wave-6 fix #2 (missions): target legality, IsInPlayfield, house handicap.
#include "doctest.h"

#include "asset_fixture.hpp"
#include "combat.hpp"
#include "occupancy.hpp"
#include "world.hpp"

using namespace ra2engine;

namespace {
bool open_2x2(const World& w, int& x, int& y) {
    for (int yy = 20; yy < w.grid_h() - 20; ++yy)
        for (int xx = 20; xx < w.grid_w() - 20; ++xx)
            if (xx + yy > w.grid_w() + 4 && w.walkable(xx, yy) && w.walkable(xx + 1, yy) && w.walkable(xx, yy + 1) &&
                w.walkable(xx + 1, yy + 1)) { x = xx; y = yy; return true; }
    return false;
}
}  // namespace

TEST_CASE("w6 legal_threat: campaign Insignificant + passive owner, LegalTarget, playfield") {
    if (!ra2fixture::mapsmd03_available()) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    World w(ra2fixture::mapsmd03_map());
    int x = -1, y = -1;
    REQUIRE(open_2x2(w, x, y));
    const int b = w.add_building(-1, x, y, 2, 2, 2, 500, 0, 0.0, true);
    UnitCombat c;
    c.strength = 100;
    c.tg.insignificant = true;
    w.add_combat_unit(x + 4, y + 4, 2, c);
    const int u = static_cast<int>(w.units().size()) - 1;
    // Inert outside a campaign host: everything is a legal threat.
    w.set_building_target_flags(b, /*insignificant=*/true, /*legal=*/true);
    CHECK(w.legal_threat_building(1, b));
    CHECK(w.legal_threat_unit(1, u));
    // Campaign: an Insignificant unit is rejected; an Insignificant building
    // only when its owner is MultiplayPassive.
    w.set_playfield(/*campaign=*/true, /*valid=*/false, 0, 0, 0, 0, 0);
    CHECK_FALSE(w.legal_threat_unit(1, u));
    CHECK(w.legal_threat_building(1, b));
    w.set_passive_house(2, true);
    CHECK_FALSE(w.legal_threat_building(1, b));
    w.set_building_target_flags(b, false, true);
    CHECK(w.legal_threat_building(1, b));
    w.set_building_target_flags(b, false, /*legal=*/false);
    CHECK_FALSE(w.legal_threat_building(1, b));
    w.set_building_target_flags(b, false, true);
    // A usable area that excludes the building: not in the playfield until a
    // ResizeView (set_playfield again) covers it.
    w.set_playfield(true, true, w.grid_w(), 0, 0, 1, 1);
    CHECK_FALSE(w.cell_in_playfield(x, y));
    CHECK_FALSE(w.legal_threat_building(1, b));
    w.set_playfield(true, true, w.grid_w(), 0, 0, w.grid_w(), w.grid_h());
    CHECK(w.legal_threat_building(1, b));
}

TEST_CASE("w6 handicap: the house ROF multiplies a defence rearm (ftol)") {
    if (!ra2fixture::mapsmd03_available()) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    World w(ra2fixture::mapsmd03_map());
    CHECK(w.house_rof_mul(1) == 1.0);
    CHECK(w.house_rof_frames(1, 80) == 80);
    w.set_house_handicap(1, 1.0, 1.2);   // [Difficult] ROF=1.2
    CHECK(w.house_rof_frames(1, 80) == 96);
    w.set_house_handicap(0, 1.0, 0.8);   // [Easy] ROF=.8
    CHECK(w.house_rof_frames(0, 60) == 48);
    CHECK(w.house_rof_frames(2, 60) == 60);
}

namespace {
Unit w6_soldier(int x, int house, Occupancy& occ, int idx) {
    Unit u;
    u.cell_x = x;
    u.fx = x;
    u.house = house;
    u.hp = u.max_hp = 125;
    u.w_verses = std::vector<int>(11, 100);
    u.w_damage = 15;
    u.w_rof = 20;
    u.w_range = 4.0;
    u.vet.base.range = 4.0;
    u.sight = 12;   // never read by the threat scan any more
    occ.set(x, 0, idx);
    return u;
}
int acquire_at(int dist, bool area_guard, double guard_range = 0.0) {
    static ra2assets::MapFile map;
    static World w(map);
    Occupancy occ(40, 1);
    std::vector<Unit> units;
    units.push_back(w6_soldier(0, 0, occ, 0));
    units.push_back(w6_soldier(dist, 1, occ, 1));
    units[1].w_damage = 0;
    units[0].tg.area_guard = area_guard;
    units[0].tg.t.guard_range = guard_range;
    std::vector<Building> bs;
    ra2engine::step_combat(w, units, bs, occ, 1.0 / 60.0);
    return units[0].target;
}
}  // namespace

TEST_CASE("w6 threat radius: weapon range + 1, never Sight; area guard doubles; GuardRange wins") {
    CHECK(acquire_at(5, false) == 1);    // range 4 + 1
    CHECK(acquire_at(6, false) == -1);   // Sight 12 is not the radius
    CHECK(acquire_at(8, true) == 1);     // area guard: 2 x 4
    CHECK(acquire_at(9, true) == -1);
    CHECK(acquire_at(3, false, 2.0) == -1);   // GuardRange=2 replaces the weapon range
    CHECK(acquire_at(2, false, 2.0) == 1);
}