// w8fix-layer (wave-8 root causes 3 and 6): the high-bridge deck and the ground
// under it are separate occupancy layers [RE: CellClass FirstObject +0xE4 /
// AltObject +0xE8, occupation words +0x124 / +0x128; add/remove 0x47E7B0 /
// 0x47E9A0; IsClearToMove 0x4833B0; UnitClass::Crush 0x7404B0], DamageArea
// picks one layer by the impact height [RE: 0x489190], and the deck counts 4
// levels for unit Z / the elevation bonus / the cliff tracer [RE: 0x487C60,
// 0x6F6490] and for a jumpjet's floor [RE: 0x54D4B0].
#include "doctest.h"

#include <vector>

#include "asset_fixture.hpp"
#include "combat.hpp"
#include "locomotion.hpp"
#include "occupancy.hpp"
#include "pathfind.hpp"
#include "world.hpp"

using namespace ra2engine;

TEST_CASE("w8 Occupancy: the deck plane and the ground plane never see each other") {
    Occupancy occ(6, 3);
    occ.enable_subcells();
    occ.mark_infantry(2, true);
    occ.mark_infantry(3, true);
    occ.set(2, 1, 0, 0);   // a ship on the water
    CHECK(occ.at(2, 1, 0) == 0);
    CHECK(occ.at(2, 1, 1) == Occupancy::kEmpty);
    CHECK(occ.can_enter(2, 1, 1, 1));        // a tank onto the deck above it
    CHECK_FALSE(occ.can_enter(2, 1, 1, 0));  // ... not onto the water
    CHECK_FALSE(occ.can_enter(2, 1, 1));     // the any-layer (placement) view: taken
    occ.set(2, 1, 1, 1);
    CHECK(occ.at(2, 1, 1) == 1);
    CHECK(occ.at(2, 1, 0) == 0);             // the ship keeps its cell
    CHECK(occ.at(2, 1) == 0);                // any layer: the ground first
    // Three soldiers on the deck fill ITS spots only; a swimmer below still
    // finds three free spots on the water (the original's two occupation words).
    occ.release(2, 1, 1);
    occ.release(2, 1, 0);
    for (int u = 10; u < 13; ++u) {
        occ.mark_infantry(u, true);
        const int sp = occ.free_spot(2, 1, 0, 0, 1);
        REQUIRE(sp >= 0);
        occ.set_spot(2, 1, u, sp, 1);
    }
    CHECK(occ.infantry_count(2, 1, 1) == 3);
    CHECK(occ.free_spot(2, 1, 0, 0, 1) < 0);
    CHECK(occ.infantry_count(2, 1, 0) == 0);
    CHECK(occ.can_enter(2, 1, 2, 0));        // a SEAL swims in under them
    CHECK_FALSE(occ.can_enter(2, 1, 2, 1));
    // set on one plane drops the unit from the other plane of the cell.
    occ.set(3, 1, 3, 0);
    occ.set(3, 1, 3, 1);
    CHECK(occ.at(3, 1, 0) == Occupancy::kEmpty);
    CHECK(occ.at(3, 1, 1) == 3);
    // release / release_all_of clear both planes.
    occ.release_all_of(3, -1, -1);
    CHECK(occ.at(3, 1) == Occupancy::kEmpty);
    // The default plane of set() is the unit's recorded layer.
    occ.mark_layer(5, 1);
    occ.set(4, 1, 5);
    CHECK(occ.at(4, 1, 1) == 5);
    CHECK(occ.at(4, 1, 0) == Occupancy::kEmpty);
}

namespace {

// A 7 x 5 river crossing: banks (level 4) on y <= 1 and y >= 3, the river
// (level 0) on y = 2, a high-bridge deck over the river at (3, 2).
struct Crossing {
    static constexpr int W = 7, H = 5;
    std::vector<std::uint8_t> land, water, amph;
    std::vector<std::int8_t> level;
    std::vector<std::uint8_t> deck_land, deck_amph;
    StepTerrain st;
    Occupancy occ{W, H};
    Crossing() : land(W * H, 1), water(W * H, 0), amph(W * H, 1), level(W * H, 4), deck_land(W * H, 0), deck_amph(W * H, 0) {
        for (int x = 0; x < W; ++x) {
            level[2 * W + x] = 0;
            land[2 * W + x] = 0;
            water[2 * W + x] = 1;
        }
        land[2 * W + 3] = 1;        // the deck carries land units
        deck_land[2 * W + 3] = 2;   // deck only for a land mover
        deck_amph[2 * W + 3] = 1;   // an amphibious mover may swim under
        st.land.level = &level;
        st.land.deck = &deck_land;
        st.amph.level = &level;
        st.amph.deck = &deck_amph;
        st.water.level = &level;
        occ.enable_subcells();
    }
    void step(std::vector<Unit>& units, std::vector<int>* scatter = nullptr) {
        MoveSpeedCtx sc;
        sc.scatter_out = scatter;
        const AnimTable anims{};
        step_units(units, land, water, W, H, occ, 1.0 / 15.0, 0.0, anims, &amph, &sc, nullptr, nullptr, nullptr, &st);
    }
};

Unit make(int x, int y, bool vehicle, MovementDomain d, int house) {
    Unit u;
    u.cell_x = x;
    u.cell_y = y;
    u.fx = x;
    u.fy = y;
    u.is_vehicle = vehicle;
    u.house = house;
    u.movement_domain = d;
    u.speed_cps = 20 * 15.0 / 256.0;
    return u;
}

}  // namespace

TEST_CASE("w8 W8-01-05 / W8-03-04: a ship parked under the deck neither blocks nor is pushed by a deck tank") {
    Crossing c;
    std::vector<Unit> units{make(3, 0, true, MovementDomain::Land, 0), make(3, 2, true, MovementDomain::Water, 0)};
    units[1].is_naval = true;
    c.occ.set(3, 0, 0, 0);
    c.occ.set(3, 2, 1, 0);
    PathTerrain t = c.st.land;
    units[0].path = find_path_t(c.land, Crossing::W, Crossing::H, {3, 0}, {3, 4}, nullptr, &t);
    REQUIRE(units[0].path.size() == 5);
    units[0].path_idx = 1;
    std::vector<int> scatter;
    bool on_deck_seen = false;
    for (int f = 0; f < 600 && units[0].moving(); ++f) {
        scatter.clear();
        c.step(units, &scatter);
        CHECK(scatter.empty());   // the ship below is never told to scatter
        if (units[0].cell_x == 3 && units[0].cell_y == 2) on_deck_seen = on_deck_seen || units[0].on_deck == 1;
    }
    CHECK(units[0].cell_y == 4);   // crossed the deck
    CHECK(on_deck_seen);
    CHECK(units[1].cell_x == 3);   // the ship kept its cell
    CHECK(units[1].cell_y == 2);
    CHECK(c.occ.at(3, 2, 0) == 1);
}

TEST_CASE("w8 W8-01-05: a SEAL swims under deck tanks; a crusher on the deck spares a swimmer below") {
    Crossing c;
    // A tank (house 1, not allied) parked on the deck; the SEAL (house 0) swims
    // the river from x=0 to x=6 right under it.
    std::vector<Unit> units{make(3, 2, true, MovementDomain::Land, 1), make(0, 2, false, MovementDomain::Amphibious, 0)};
    units[0].on_deck = 1;
    units[0].crusher = true;
    units[1].crushable = true;
    c.occ.mark_infantry(1, true);
    c.occ.set(3, 2, 0, 1);
    c.occ.set(0, 2, 1, 0);
    PathTerrain t = c.st.amph;
    units[1].path = find_path_t(c.amph, Crossing::W, Crossing::H, {0, 2}, {6, 2}, nullptr, &t);
    REQUIRE(units[1].path.size() == 7);
    units[1].path_idx = 1;
    for (int f = 0; f < 900 && units[1].moving(); ++f) c.step(units);
    CHECK(units[1].cell_x == 6);   // through (3,2) under the tank
    CHECK(units[1].on_deck == 0);
    CHECK(units[0].cell_x == 3);

    // The crusher drives off the deck and back over a swimmer resting under it.
    Crossing d;
    std::vector<Unit> v{make(3, 0, true, MovementDomain::Land, 1), make(3, 2, false, MovementDomain::Amphibious, 0)};
    v[0].crusher = true;
    v[1].crushable = true;
    d.occ.mark_infantry(1, true);
    d.occ.set(3, 0, 0, 0);
    d.occ.set(3, 2, 1, 0);
    PathTerrain lt = d.st.land;
    v[0].path = find_path_t(d.land, Crossing::W, Crossing::H, {3, 0}, {3, 4}, nullptr, &lt);
    REQUIRE(v[0].path.size() == 5);
    v[0].path_idx = 1;
    for (int f = 0; f < 600 && v[0].moving(); ++f) d.step(v);
    CHECK(v[0].cell_y == 4);
    CHECK(v[1].hp > 0);   // never crushed from the other layer
    CHECK(v[1].die_time < 0.0);
}

namespace {
const std::vector<int> kFlat(11, 100);
UnitCombat inf_combat(int strength, int damage, double spread) {
    UnitCombat c;
    c.strength = strength;
    c.armor = "none";
    c.image = "E1";
    c.damage = damage;
    c.rof = 20;
    c.range = 5.0;
    c.verses = kFlat;
    c.cell_spread = spread;
    c.percent_at_max = 1.0;
    return c;
}
}  // namespace

TEST_CASE("w8 W8-04-02 / W8-05-06: a blast hits the deck layer or the ground layer, never both") {
    if (!ra2fixture::mapsmd03_available()) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    World w(ra2fixture::mapsmd03_map());
    const int W = w.grid_w();
    std::vector<std::uint8_t> hb(static_cast<std::size_t>(W) * w.grid_h(), 0);
    for (int x = 10; x <= 14; ++x) hb[20 * W + x] = 1;
    w.set_high_bridges(hb);
    REQUIRE(w.deck_intact(12, 20));
    w.add_combat_unit(2, 2, 1, inf_combat(100, 100, 1.0));    // the shooter, far away
    w.add_combat_unit(12, 20, 0, inf_combat(1000, 0, 0.0));  // under the deck
    w.add_combat_unit(12, 20, 0, inf_combat(1000, 0, 0.0));  // on the deck
    w.add_combat_unit(13, 20, 0, inf_combat(1000, 0, 0.0));  // on the deck, next cell
    w.units()[2].on_deck = 1;
    w.units()[3].on_deck = 1;
    Unit shooter = w.units()[0];
    Occupancy occ = w.occupancy();
    const int ground = w.cell_level(12, 20) * 104;
    // A shell at the waterline (a Destroyer at a landing craft under the span).
    damage_area(w, w.units(), w.buildings(), occ, shooter, 0, -1, 12 * 256, 20 * 256, ground);
    CHECK(w.units()[1].hp < 1000);
    CHECK(w.units()[2].hp == 1000);   // the MCV on the deck is untouched
    CHECK(w.units()[3].hp == 1000);
    // A burst at the deck: the deck list only.
    const int hp1 = w.units()[1].hp;
    damage_area(w, w.units(), w.buildings(), occ, shooter, 0, -1, 12 * 256, 20 * 256, ground + 416);
    CHECK(w.units()[1].hp == hp1);
    CHECK(w.units()[2].hp < 1000);
    CHECK(w.units()[3].hp < 1000);
    // Half the bridge height is the split [0x489190: Z > floor + BridgeHeight/2].
    const int hp2 = w.units()[2].hp;
    damage_area(w, w.units(), w.buildings(), occ, shooter, 0, -1, 12 * 256, 20 * 256, ground + 208);
    CHECK(w.units()[2].hp == hp2);
    CHECK(w.units()[1].hp < hp1);
    // Unit Z: a deck unit stands BridgeHeight up.
    CHECK(unit_lept_z(w.units()[2]) == unit_lept_z(w.units()[1]) + 416);
    // FUN_00487C60: an intact deck cell counts 4 levels; a broken one does not.
    CHECK(w.cell_bridge_level(12, 20) == w.cell_level(12, 20) + 4);
    CHECK(w.cell_bridge_level(12, 21) == w.cell_level(12, 21));
}

TEST_CASE("w8 W8-05-03: a jumpjet's floor over a deck and ahead (FUN_0054D4B0)") {
    if (!ra2fixture::mapsmd03_available()) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    World w(ra2fixture::mapsmd03_map());
    const int W = w.grid_w();
    std::vector<std::uint8_t> hb(static_cast<std::size_t>(W) * w.grid_h(), 0);
    for (int x = 10; x <= 14; ++x) hb[20 * W + x] = 1;
    w.set_high_bridges(hb);
    Unit u;
    u.is_aircraft = true;
    u.cell_x = 12;
    u.cell_y = 20;
    u.fx = 12;
    u.fy = 20;
    CHECK(w.jumpjet_floor(u) == doctest::Approx(416.0));   // hovering over the deck
    u.cell_y = 22;
    u.fy = 22;
    const bool flat = w.cell_level(12, 22) == w.cell_level(12, 21) && w.cell_level(12, 21) == w.cell_level(12, 20);
    CHECK(w.jumpjet_floor(u) == doctest::Approx(0.0));     // not moving: its own cell
    u.an.jj_speed = 10.0;
    u.body_facing = 0.0;   // north, toward (12, 21): no deck there
    if (flat) CHECK(w.jumpjet_floor(u) == doctest::Approx(0.0));
    u.cell_y = 21;
    u.fy = 21;   // the deck one cell ahead: BridgeHeight is added to A (0x54D596), then (A + B) / 2
    if (flat) CHECK(w.jumpjet_floor(u) == doctest::Approx(208.0));
}
