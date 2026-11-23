// Yuri Grinder / Bio Reactor helpers + World MC fates (no install required
 // for the pure cases; World cases skip without mapsmd03).
#include "doctest.h"

#include "asset_fixture.hpp"
#include "grinder_bio.hpp"
#include "production.hpp"
#include "world.hpp"

using namespace ra2engine;
using namespace ra2engine::grinder_bio;

TEST_CASE("grinder_bio: absorb_power and grind_refund (pure)") {
    CHECK(absorb_power(150, 100, 0) == 150);
    CHECK(absorb_power(150, 100, 2) == 350);   // YAPOWR: Power + ExtraPower*n
    CHECK(absorb_power(150, 0, 5) == 150);
    CHECK(absorb_power(-50, 100, 1) == 50);     // ExtraPower still adds when Power drain
    CHECK(grind_refund(200, 0, 0.50, true) == 100);
    CHECK(grind_refund(200, 150, 0.50, true) == 150);   // Soylent absolute
    CHECK(grind_refund(200, 0, 0.50, false) == 200);    // AI: no RefundPercent
}

TEST_CASE("grinder_bio: type predicates") {
    BuildingTypeDef g;
    g.grinding = true;
    CHECK(type_grinds(g));
    CHECK_FALSE(type_absorbs(g));
    BuildingTypeDef r;
    r.infantry_absorb = true;
    r.passengers = 5;
    r.extra_power = 100;
    r.power = 150;
    CHECK(type_absorbs(r));
    Building b;
    CHECK(absorb_has_room(r, b));
    b.occupants = {0, 1, 2, 3, 4};
    CHECK_FALSE(absorb_has_room(r, b));
}

TEST_CASE("power_totals: InfantryAbsorb ExtraPower counts occupants") {
    BuildingTypeDef t;
    t.id = "YAPOWR";
    t.power = 150;
    t.infantry_absorb = true;
    t.extra_power = 100;
    t.passengers = 5;
    std::vector<BuildingTypeDef> types = {t};
    Building b;
    b.type = 0;
    b.house = 0;
    b.alive = true;
    b.state = Building::State::Idle;
    b.hp = b.max_hp = 700;
    b.occupants = {1, 2};
    auto p = power_totals({b}, types, 0);
    CHECK(p.output == 350);   // 150 + 100*2
}

namespace {

UnitCombat mk_gi() {
    UnitCombat c;
    c.image = "E1";
    c.strength = 125;
    c.armor = "none";
    c.cost = 200;
    c.soylent = 100;
    c.is_infantry = true;
    return c;
}

UnitCombat mk_yuri() {
    UnitCombat c = mk_gi();
    c.image = "YURI";
    c.w7.mind_control = true;
    c.w7.mc_capacity = 1;
    c.damage = 1;
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

#define GB_WORLD(world, x0, y0)                                              \
    if (!ra2fixture::mapsmd03_available()) {                                 \
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR"); \
        return;                                                              \
    }                                                                        \
    World world(ra2fixture::mapsmd03_map());                                 \
    int x0 = 0, y0 = 0;                                                      \
    REQUIRE(open_cell(world, x0, y0));

TEST_CASE("grinder: MC victim pays Soylent to controller house and frees slot") {
    GB_WORLD(w, x0, y0);
    BuildingTypeDef grind;
    grind.id = "YAGRND";
    grind.fw = 2;
    grind.fh = 2;
    grind.grinding = true;
    grind.power = -50;
    w.set_building_types({grind});
    w.enable_player_economy(0, 0.0);
    const int bi = w.add_building(0, x0 + 4, y0, 2, 2, 0, 900, 0, 0.0, true);
    w.add_combat_unit(x0, y0, 0, mk_yuri());
    w.add_combat_unit(x0 + 2, y0, 1, mk_gi());
    REQUIRE(w.mc_capture(0, 1));
    CHECK(w.units()[1].house == 0);
    CHECK(w.units()[0].w7.mc_victims.size() == 1);
    REQUIRE(w.grind_now(1, bi));
    CHECK(w.units()[1].die_time >= 0.0);
    CHECK(w.units()[0].w7.mc_victims.empty());
    CHECK(w.house_credits(0) == doctest::Approx(100.0));   // Soylent=100
}

TEST_CASE("bio reactor: MC infantry absorb keeps house and frees MC capacity") {
    GB_WORLD(w, x0, y0);
    BuildingTypeDef reactor;
    reactor.id = "YAPOWR";
    reactor.fw = 2;
    reactor.fh = 2;
    reactor.power = 150;
    reactor.infantry_absorb = true;
    reactor.extra_power = 100;
    reactor.passengers = 5;
    w.set_building_types({reactor});
    const int bi = w.add_building(0, x0 + 4, y0, 2, 2, 0, 700, 0, 0.0, true);
    w.add_combat_unit(x0, y0, 0, mk_yuri());
    w.add_combat_unit(x0 + 2, y0, 1, mk_gi());
    REQUIRE(w.mc_capture(0, 1));
    REQUIRE(w.absorb_now(1, bi));
    CHECK(w.units()[1].house == 0);
    CHECK(w.units()[1].garrison_of == bi);
    CHECK(w.units()[0].w7.mc_victims.empty());
    CHECK(w.units()[1].w7.mc_controller < 0);
    CHECK(w.buildings()[bi].occupants.size() == 1);
    w.step_mind_control();   // boarded absorb must not restore orig house
    CHECK(w.units()[1].house == 0);
    const auto pt = power_totals(w.buildings(), w.btypes(), 0);
    CHECK(pt.output == 250);   // 150 + 100
}

TEST_CASE("group_occupy: orders grind and absorb enters (Mission_Enter)") {
    GB_WORLD(w, x0, y0);
    BuildingTypeDef grind;
    grind.id = "YAGRND";
    grind.fw = 2;
    grind.fh = 2;
    grind.grinding = true;
    BuildingTypeDef reactor;
    reactor.id = "YAPOWR";
    reactor.fw = 2;
    reactor.fh = 2;
    reactor.infantry_absorb = true;
    reactor.extra_power = 100;
    reactor.passengers = 5;
    reactor.power = 150;
    w.set_building_types({grind, reactor});
    const int gi = w.add_building(0, x0 + 4, y0, 2, 2, 0, 900, 0, 0.0, true);
    const int ri = w.add_building(1, x0 + 8, y0, 2, 2, 0, 700, 0, 0.0, true);
    w.add_combat_unit(x0, y0, 0, mk_gi());
    w.add_combat_unit(x0 + 2, y0, 0, mk_gi());
    CHECK(w.group_occupy_count(0, {0}, gi) == 1);
    CHECK(w.group_occupy(0, {0}, gi) == 1);
    CHECK(w.units()[0].occupy_target == gi);
    CHECK(w.group_occupy_count(0, {1}, ri) == 1);
    CHECK(w.group_occupy(0, {1}, ri) == 1);
    CHECK(w.units()[1].occupy_target == ri);
}

TEST_CASE("bio reactor: Deploy unload_occupants ejects and keeps house") {
    GB_WORLD(w, x0, y0);
    BuildingTypeDef reactor;
    reactor.id = "YAPOWR";
    reactor.fw = 2;
    reactor.fh = 2;
    reactor.infantry_absorb = true;
    reactor.extra_power = 100;
    reactor.passengers = 5;
    reactor.power = 150;
    w.set_building_types({reactor});
    const int bi = w.add_building(0, x0 + 4, y0, 2, 2, 0, 700, 0, 0.0, true);
    w.add_combat_unit(x0, y0, 0, mk_gi());
    w.add_combat_unit(x0 + 2, y0, 0, mk_gi());
    REQUIRE(w.absorb_now(0, bi));
    REQUIRE(w.absorb_now(1, bi));
    CHECK(w.buildings()[bi].occupants.size() == 2);
    CHECK(w.buildings()[bi].house == 0);
    w.unload_occupants(bi);
    CHECK(w.buildings()[bi].occupants.empty());
    CHECK(w.buildings()[bi].house == 0);   // must not flip to neutral
    CHECK(w.units()[0].garrison_of < 0);
    CHECK(w.units()[0].cargo_of < 0);
    CHECK(w.units()[0].alive);
    CHECK(w.units()[1].alive);
    const auto pt = power_totals(w.buildings(), w.btypes(), 0);
    CHECK(pt.output == 150);   // ExtraPower gone with occupants
}
