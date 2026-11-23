// W7-08-14: skirmish/MP mind control (World::step_mind_control).
#include "doctest.h"

#include "asset_fixture.hpp"
#include "world.hpp"

using namespace ra2engine;

namespace {

UnitCombat mk_gi() {
    UnitCombat c;
    c.image = "E1";
    c.strength = 125;
    c.armor = "none";
    c.damage = 15;
    c.rof = 20;
    c.range = 4.0;
    c.verses = std::vector<int>(11, 100);
    return c;
}

UnitCombat mk_yuri(int capacity = 1, bool infinite = false) {
    UnitCombat c = mk_gi();
    c.image = "YURI";
    c.w7.mind_control = true;
    c.w7.mc_capacity = capacity;
    c.w7.mc_infinite = infinite;
    c.damage = capacity;
    c.rof = 15;
    c.range = 7.0;
    return c;
}

bool open_cell(const World& w, int& x, int& y) {
    for (int yy = 2; yy + 4 < w.grid_h() - 2; ++yy)
        for (int xx = 2; xx + 8 < w.grid_w() - 2; ++xx)
            if (w.walkable(xx, yy) && w.walkable(xx + 2, yy) && w.unit_at_cell(xx, yy) < 0 &&
                w.unit_at_cell(xx + 2, yy) < 0) {
                x = xx;
                y = yy;
                return true;
            }
    return false;
}

}  // namespace

#define MC_WORLD(world, x0, y0)                                              \
    if (!ra2fixture::mapsmd03_available()) {                                 \
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR"); \
        return;                                                              \
    }                                                                        \
    World world(ra2fixture::mapsmd03_map());                                 \
    int x0 = 0, y0 = 0;                                                      \
    REQUIRE(open_cell(world, x0, y0));

TEST_CASE("mind control: Controller shot captures (no damage), ImmuneToPsionics refuses") {
    MC_WORLD(w, x0, y0);
    w.add_combat_unit(x0, y0, 0, mk_yuri());
    w.add_combat_unit(x0 + 2, y0, 1, mk_gi());
    Unit& yuri = w.units()[0];
    Unit& gi = w.units()[1];
    const int hp = gi.hp;
    yuri.w7.mc_shot = 1;
    w.step_mind_control();
    CHECK(gi.house == 0);
    CHECK(gi.w7.mc_controller == 0);
    CHECK(gi.w7.mc_orig_house == 1);
    CHECK(yuri.w7.mc_victims.size() == 1);
    CHECK(yuri.w7.mc_victims[0] == 1);
    CHECK(gi.hp == hp);

    UnitCombat dog = mk_gi();
    dog.image = "DOG";
    dog.tg.immune_psionics = true;
    w.add_combat_unit(x0 + 4, y0, 1, dog);
    CHECK_FALSE(w.mc_capture(0, 2));
    CHECK(w.units()[2].house == 1);
    CHECK(gi.house == 0);
}

TEST_CASE("mind control: capacity-1 swap; capacity-3 holds three; release on death") {
    MC_WORLD(w, x0, y0);
    w.add_combat_unit(x0, y0, 0, mk_yuri(1));
    w.add_combat_unit(x0 + 2, y0, 1, mk_gi());
    w.add_combat_unit(x0 + 4, y0, 1, mk_gi());
    REQUIRE(w.mc_capture(0, 1));
    REQUIRE(w.mc_capture(0, 2));   // frees 1, takes 2
    CHECK(w.units()[1].house == 1);
    CHECK(w.units()[1].w7.mc_controller < 0);
    CHECK(w.units()[2].house == 0);
    CHECK(w.units()[0].w7.mc_victims.size() == 1);

    World w2(ra2fixture::mapsmd03_map());
    int a = 0, b = 0;
    REQUIRE(open_cell(w2, a, b));
    w2.add_combat_unit(a, b, 0, mk_yuri(3));
    w2.add_combat_unit(a + 2, b, 1, mk_gi());
    w2.add_combat_unit(a + 4, b, 1, mk_gi());
    w2.add_combat_unit(a + 6, b, 1, mk_gi());
    w2.add_combat_unit(a + 8, b, 1, mk_gi());
    CHECK(w2.mc_capture(0, 1));
    CHECK(w2.mc_capture(0, 2));
    CHECK(w2.mc_capture(0, 3));
    CHECK_FALSE(w2.mc_capture(0, 4));   // full, Max > 1
    CHECK(w2.units()[0].w7.mc_victims.size() == 3);

    w2.units()[0].alive = false;
    w2.step_mind_control();
    CHECK(w2.units()[0].w7.mc_victims.empty());
    CHECK(w2.units()[1].house == 1);
    CHECK(w2.units()[2].house == 1);
    CHECK(w2.units()[3].house == 1);
}

TEST_CASE("mind control: campaign flag skips World pass; pairs after capture") {
    MC_WORLD(w, x0, y0);
    w.set_campaign_mind_control(true);
    w.add_combat_unit(x0, y0, 0, mk_yuri());
    w.add_combat_unit(x0 + 2, y0, 1, mk_gi());
    w.units()[0].w7.mc_shot = 1;
    w.step_mind_control();
    CHECK(w.units()[1].house == 1);   // untouched

    World w2(ra2fixture::mapsmd03_map());
    int a = 0, b = 0;
    REQUIRE(open_cell(w2, a, b));
    w2.add_combat_unit(a, b, 0, mk_yuri());
    w2.add_combat_unit(a + 2, b, 1, mk_gi());
    w2.units()[0].w7.mc_shot = 1;
    w2.step_mind_control();
    CHECK(w2.units()[1].house == 0);
    const auto pairs = w2.mind_control_pairs();
    REQUIRE(pairs.size() == 1);
    CHECK(pairs[0].first == 0);
    CHECK(pairs[0].second == 1);
}

TEST_CASE("mind control: already-controlled target is refused (no steal)") {
    MC_WORLD(w, x0, y0);
    w.add_combat_unit(x0, y0, 0, mk_yuri());
    w.add_combat_unit(x0 + 2, y0, 0, mk_yuri());
    w.add_combat_unit(x0 + 4, y0, 1, mk_gi());
    REQUIRE(w.mc_capture(0, 2));
    CHECK_FALSE(w.mc_capture(1, 2));
    CHECK(w.units()[2].w7.mc_controller == 0);
}
