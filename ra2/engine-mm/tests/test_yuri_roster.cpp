// Rallypoint: Yuri roster and structures (Psychic Tower mind control, Cloning Vats).
// Synthetic values only (no game data).
#include "doctest.h"

#include "asset_fixture.hpp"
#include "world.hpp"

using namespace ra2engine;

namespace {

WeaponSlot controller_weapon() {   // [MultipleMindControlTower], warhead [Controller]
    WeaponSlot s;
    s.valid = true;
    s.id = "MultipleMindControlTower";
    s.damage = 3;
    s.rof = 100;
    s.range = 7.0;
    s.ag = true;
    s.verses = std::vector<int>(11, 100);
    return s;
}

BuildingTypeDef psychic_tower() {
    BuildingTypeDef t;
    t.id = "YAPSYT";
    t.strength = 455;
    t.weapon[0] = controller_weapon();
    t.armed = true;
    t.mc_capacity = 3;
    return t;
}

UnitCombat gi() {
    UnitCombat c;
    c.image = "E1";
    c.strength = 125;
    c.armor = "none";
    c.verses = std::vector<int>(11, 100);
    return c;
}

int owned_by(const World& w, int house) {
    int n = 0;
    for (const Unit& u : w.units()) n += u.alive && u.house == house;
    return n;
}

}  // namespace

TEST_CASE("yuri: a Psychic Tower takes three units without hurting them, then lets go") {
    ra2assets::MapFile map;
    World w(map);
    w.set_building_types({psychic_tower()});
    w.add_building(0, 2, 2, 1, 1, 0, 455, 0, 0.0, /*instant=*/true);
    for (int i = 0; i < 4; ++i) w.add_combat_unit(5 + i % 2, 2 + i / 2, 1, gi());
    for (int f = 0; f < 600; ++f) w.step(1.0 / 15.0);
    CHECK(owned_by(w, 0) == 3);   // capacity = the weapon's Damage=
    CHECK(owned_by(w, 1) == 1);
    for (const Unit& u : w.units()) CHECK(u.hp == 125);
    CHECK(w.buildings()[0].mc_victims.size() == 3);

    w.buildings()[0].alive = false;   // the tower is destroyed
    w.step(1.0 / 15.0);
    CHECK(owned_by(w, 1) == 4);
    CHECK(w.buildings()[0].mc_victims.empty());
}

TEST_CASE("yuri: a Psychic Tower refuses ImmuneToPsionics units, aircraft and its own side") {
    ra2assets::MapFile map;
    World w(map);
    w.set_building_types({psychic_tower()});
    w.add_building(0, 2, 2, 1, 1, 0, 455, 0, 0.0, /*instant=*/true);
    w.add_combat_unit(5, 2, 1, gi());
    w.add_combat_unit(5, 3, 1, gi());
    w.add_combat_unit(6, 2, 0, gi());
    w.units()[1].tg.t.immune_psionics = true;
    CHECK(w.tower_mc_ok(0, 0));
    CHECK_FALSE(w.tower_mc_ok(0, 1));   // ImmuneToPsionics
    CHECK_FALSE(w.tower_mc_ok(0, 2));   // already the tower's house
    CHECK(w.tower_capture(0, 0));
    CHECK_FALSE(w.tower_mc_ok(0, 0));   // no stealing a controlled unit
    CHECK(w.units()[0].house == 0);
    CHECK(w.units()[0].w7.mc_orig_house == 1);
}

TEST_CASE("yuri: Cloning Vats copy each infantry unit trained, only while they stand") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("skipped: real map assets not found"); return; }
    World w(ra2fixture::mapsmd03_map());
    int x = 0, y = 0;
    for (int yy = 4; yy + 6 < w.grid_h() && x == 0; ++yy)
        for (int xx = 4; xx + 6 < w.grid_w() && x == 0; ++xx) {
            bool ok = true;
            for (int dy = -2; dy <= 2; ++dy)
                for (int dx = -2; dx <= 2; ++dx) ok = ok && w.walkable(xx + dx, yy + dy);
            if (ok) { x = xx; y = yy; }
        }
    REQUIRE(x > 0);
    BuildingTypeDef vats;
    vats.id = "NACLON";
    vats.strength = 1000;
    vats.w5.cloning = true;
    w.set_building_types({vats});
    w.add_building(0, x, y, 1, 1, 0, 1000, 0, 0.0, /*instant=*/true);
    const std::size_t before = w.units().size();
    w.clone_infantry(0, gi());
    REQUIRE(w.units().size() == before + 1);
    CHECK(w.units().back().house == 0);
    CHECK(w.units().back().image == "E1");
    w.clone_infantry(1, gi());   // another house has no vats
    CHECK(w.units().size() == before + 1);
    w.buildings()[0].alive = false;
    w.clone_infantry(0, gi());
    CHECK(w.units().size() == before + 1);
}
