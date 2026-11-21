// Wave-5 unit and building abilities (agent C) [RE: docs/re/abilities-w5.md].
#include "doctest.h"

#include "asset_fixture.hpp"
#include "cargo.hpp"
#include "world.hpp"

using namespace ra2engine;

TEST_CASE("w5 stolen tech: the roster gate follows the house's stolen bits") {
    std::vector<UnitTypeDef> defs(3);
    defs[0].id = "E1";
    defs[0].tech_level = 1;
    defs[1].id = "CIVAN";
    defs[1].w5.requires_stolen = kStolenSoviet;
    defs[1].w5.base_tech_level = 9;
    defs[2].id = "CCOMAND";
    defs[2].w5.requires_stolen = kStolenAllied;
    defs[2].w5.base_tech_level = 9;
    if (!ra2fixture::mapsmd03_available()) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    World w(ra2fixture::mapsmd03_map());
    w.apply_stolen_tech_gate(defs, 0);
    CHECK(defs[0].tech_level == 1);
    CHECK(defs[1].tech_level == 0);
    CHECK(defs[2].tech_level == 0);
    w.grant_stolen_tech(0, kStolenSoviet);
    w.apply_stolen_tech_gate(defs, 0);
    CHECK(defs[1].tech_level == 9);
    CHECK(defs[2].tech_level == 0);
    CHECK(w.stolen_tech(1) == 0);
}

TEST_CASE("w5 stolen tech: a spy in a BuildTech building sets the side bit") {
    if (!ra2fixture::mapsmd03_available()) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    World w(ra2fixture::mapsmd03_map());
    int x = -1, y = -1;
    for (int yy = 6; yy < w.grid_h() - 6 && x < 0; ++yy)
        for (int xx = 6; xx < w.grid_w() - 6 && x < 0; ++xx)
            if (w.walkable(xx, yy) && w.walkable(xx + 1, yy) && w.walkable(xx, yy + 1) &&
                w.walkable(xx + 1, yy + 1)) { x = xx; y = yy; }
    REQUIRE(x >= 0);
    const int b = w.add_building(-1, x, y, 2, 2, 1, 500, 0, 0.0, true);
    CHECK(w.infiltrate_building(b, 0, World::InfiltratePurpose::StolenSoviet, 0.5, 1000));
    CHECK(w.stolen_tech(0) == kStolenSoviet);
    CHECK_FALSE(w.infiltrate_building(b, 1, World::InfiltratePurpose::StolenSoviet, 0.5, 1000));
}

TEST_CASE("w5 cargo: an ally boards only when the alliance predicate says so") {
    std::vector<Unit> u(2);
    u[0].house = 1;
    u[0].passengers_max = 5;
    u[0].size_limit = 2;
    u[1].house = 0;
    u[1].size = 1;
    CHECK_FALSE(can_load(u, 0, 1));
    CHECK(can_load(u, 0, 1, [](int, int) { return true; }));
    CHECK_FALSE(can_load(u, 0, 1, [](int, int) { return false; }));
}
