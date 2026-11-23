// P4 abilities -- World-level behavior on the real mapsmd03 map
// (install-gated, the test_world precedent) [RE: docs/re/abilities.md].
#include "doctest.h"

#include "asset_fixture.hpp"
#include "combat.hpp"
#include "world.hpp"

using namespace ra2engine;

namespace {
using Ev = World::AbilityEvent;

const std::vector<int> kAll100 = {100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100};

UnitCombat mk(const char* image, int strength, const char* armor, int dmg, int rof, double range,
              std::vector<int> verses, bool vehicle = false) {
    UnitCombat c;
    c.image = image;
    c.strength = strength;
    c.armor = armor;
    c.damage = dmg;
    c.rof = rof;
    c.range = range;
    c.verses = std::move(verses);
    c.is_vehicle = vehicle;
    return c;
}

// A fully walkable, unoccupied W x H rectangle origin, or false.
bool find_open_rect(const World& w, int W, int H, int& x0, int& y0) {
    for (int y = 4; y + H < w.grid_h() - 4; ++y)
        for (int x = 4; x + W < w.grid_w() - 4; ++x) {
            bool ok = true;
            for (int dy = 0; dy < H && ok; ++dy)
                for (int dx = 0; dx < W && ok; ++dx)
                    if (!w.walkable(x + dx, y + dy) || w.unit_at_cell(x + dx, y + dy) >= 0) ok = false;
            if (ok) { x0 = x; y0 = y; return true; }
        }
    return false;
}

int count_kind(const std::vector<Ev>& ev, Ev::Kind k) {
    int n = 0;
    for (const Ev& e : ev)
        if (e.kind == k) ++n;
    return n;
}
}  // namespace

#define ABIL_WORLD(world, x0, y0)                                           \
    if (!ra2fixture::mapsmd03_available()) {                                 \
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR"); \
        return;                                                              \
    }                                                                        \
    World world(ra2fixture::mapsmd03_map());                                 \
    int x0 = 0, y0 = 0;                                                      \
    REQUIRE(find_open_rect(world, 16, 6, x0, y0));

TEST_CASE("abilities world: FIREWALL -- no profile = plain combat, frame still ticks") {
    ABIL_WORLD(world, x0, y0);
    const std::vector<int> sa = {100, 80, 80, 50, 25, 25, 75, 50, 25, 100, 100};
    world.add_combat_unit(x0, y0, 0, mk("GI", 125, "none", 15, 20, 4.0, sa));
    world.add_combat_unit(x0 + 2, y0, 1, mk("CONS", 125, "flak", 15, 20, 4.0, sa));
    for (int k = 0; k < 30; ++k) world.step(1.0 / 15.0);
    CHECK_FALSE(world.abilities_enabled());
    CHECK(world.ability_frame() == 30);
    CHECK(world.units()[1].hp < 125);
    CHECK(world.take_ability_events().empty());
    CHECK_FALSE(world.units()[0].frozen);
}

TEST_CASE("abilities world: terror drone infects a vehicle, 50 dmg every 60 frames [RE: §B]") {
    ABIL_WORLD(world, x0, y0);
    AbilityProfile dp;
    dp.fire_kind = AbilityKind::Parasite;
    dp.parasiteable = false;
    dp.suppression_threshold = 5;
    world.register_ability_profile("DRON", dp);
    const std::vector<int> para = {100, 100, 100, 100, 100, 100, 0, 0, 0, 0, 0};
    world.add_combat_unit(x0, y0, 1, mk("DRON", 100, "special_1", 50, 60, 1.83, para));
    world.add_combat_unit(x0 + 1, y0, 0, mk("HTNK", 400, "heavy", 0, 60, 0.0, {}, true));
    world.units()[0].target = 1;
    world.units()[0].attack_order = true;
    std::vector<int> hp_after;
    std::vector<std::uint64_t> tick_frames;
    for (int k = 0; k < 200; ++k) {
        const std::uint64_t f = world.ability_frame();
        world.step(1.0 / 15.0);
        for (const Ev& e : world.take_ability_events())
            if (e.kind == Ev::Kind::ParasiteTick) {
                tick_frames.push_back(f);
                hp_after.push_back(e.y);
            }
    }
    REQUIRE(tick_frames.size() >= 3);
    CHECK(world.units()[0].cargo_of == 1);
    CHECK(world.units()[1].parasite_on_me == 0);
    CHECK(tick_frames[1] - tick_frames[0] == 60);
    CHECK(tick_frames[2] - tick_frames[1] == 60);
    CHECK(hp_after[0] == 350);
    CHECK(hp_after[1] == 300);
}

TEST_CASE("abilities world: drone kills infantry on the first tick and pops out [RE: §B2/§B3]") {
    ABIL_WORLD(world, x0, y0);
    AbilityProfile dp;
    dp.fire_kind = AbilityKind::Parasite;
    dp.parasiteable = false;
    world.register_ability_profile("DRON", dp);
    const std::vector<int> para = {100, 100, 100, 100, 100, 100, 0, 0, 0, 0, 0};
    world.add_combat_unit(x0, y0, 1, mk("DRON", 100, "special_1", 50, 60, 1.83, para));
    world.add_combat_unit(x0 + 1, y0, 0, mk("GI", 125, "none", 0, 20, 0.0, {}));
    world.units()[0].target = 1;
    world.units()[0].attack_order = true;
    std::vector<Ev> all;
    for (int k = 0; k < 40; ++k) {
        world.step(1.0 / 15.0);
        for (const Ev& e : world.take_ability_events()) all.push_back(e);
    }
    CHECK(count_kind(all, Ev::Kind::Infect) == 1);
    CHECK(count_kind(all, Ev::Kind::ParasiteTick) == 1);
    CHECK(count_kind(all, Ev::Kind::DroneExit) == 1);
    CHECK((!world.units()[1].alive || world.units()[1].die_time >= 0.0));
    CHECK(world.units()[0].cargo_of == -1);
    CHECK(world.units()[0].alive);
}

TEST_CASE("abilities world: teleport is immediate, frozen max(dist/48,16) frames [RE: §C1]") {
    ABIL_WORLD(world, x0, y0);
    AbilityProfile cp;
    cp.teleporter = true;
    world.register_ability_profile("CLEG", cp);
    world.add_combat_unit(x0, y0, 0, mk("CLEG", 125, "none", 8, 120, 5.0, {}));
    world.step(1.0 / 15.0);   // bind
    world.order_unit_move(0, x0 + 10, y0);
    CHECK(world.units()[0].cell_x == x0 + 10);
    CHECK(world.units()[0].frozen);
    int frozen_frames = 0;
    while (world.units()[0].frozen && frozen_frames < 200) {
        world.step(1.0 / 15.0);
        ++frozen_frames;
    }
    CHECK(frozen_frames == 53);   // 2560 / 48
    world.order_unit_move(0, x0 + 11, y0);
    frozen_frames = 0;
    while (world.units()[0].frozen && frozen_frames < 200) {
        world.step(1.0 / 15.0);
        ++frozen_frames;
    }
    CHECK(frozen_frames == 16);   // the ChronoMinimumDelay floor
}

TEST_CASE("abilities world: legionnaire erases a GI in 157 frames, no death [RE: §C2]") {
    ABIL_WORLD(world, x0, y0);
    AbilityProfile cp;
    cp.fire_kind = AbilityKind::Temporal;
    cp.teleporter = true;
    world.register_ability_profile("CLEG", cp);
    world.add_combat_unit(x0, y0, 0, mk("CLEG", 125, "none", 8, 120, 5.0, kAll100));
    world.add_combat_unit(x0 + 3, y0, 1, mk("GI", 125, "none", 0, 20, 0.0, {}));
    world.units()[0].target = 1;
    world.units()[0].attack_order = true;
    int attach = -1, erase = -1;
    for (int k = 0; k < 400 && erase < 0; ++k) {
        world.step(1.0 / 15.0);
        for (const Ev& e : world.take_ability_events()) {
            if (e.kind == Ev::Kind::TemporalAttach && attach < 0) attach = k;
            if (e.kind == Ev::Kind::Erase) erase = k;
        }
    }
    REQUIRE(attach >= 0);
    REQUIRE(erase >= 0);
    CHECK(erase - attach == 156);   // 157 decrements: frames attach..attach+156
    CHECK_FALSE(world.units()[1].alive);
    CHECK(world.units()[1].die_time < 0.0);   // removed, not killed
}

TEST_CASE("abilities world: C4 blows the building 27 frames after the plant [RE: §D]") {
    ABIL_WORLD(world, x0, y0);
    AbilityProfile tp;
    tp.c4 = true;
    tp.c4_range = 1.5;
    world.register_ability_profile("TANY", tp);
    const int b = world.add_building(0, x0 + 6, y0, 2, 2, 1, 750, armor_index("wood"), 0.0,
                                     /*instant=*/true);
    world.add_combat_unit(x0, y0, 0, mk("TANY", 200, "flak", 125, 5, 6.0, kAll100));
    world.units()[0].target_building = b;
    world.units()[0].attack_order = true;
    int plant = -1, blast = -1;
    bool armed_seen = false;
    for (int k = 0; k < 600 && blast < 0; ++k) {
        world.step(1.0 / 15.0);
        for (const Ev& e : world.take_ability_events()) {
            if (e.kind == Ev::Kind::C4Plant) plant = k;
            if (e.kind == Ev::Kind::C4Blast) blast = k;
        }
        if (plant >= 0 && blast < 0 && world.buildings()[b].c4_applied) armed_seen = true;
    }
    REQUIRE(plant >= 0);
    REQUIRE(blast >= 0);
    CHECK(armed_seen);
    CHECK(blast - plant == 27);
    CHECK(world.buildings()[b].state == Building::State::Dead);
    CHECK(world.buildings()[b].hp <= 0);
}

TEST_CASE("abilities world: Ivan bomb detonates on plant+451 with IvanWH splash [RE: §E]") {
    ABIL_WORLD(world, x0, y0);
    AbilityProfile ip;
    ip.fire_kind = AbilityKind::IvanBomb;
    ip.infantry = true;
    world.register_ability_profile("IVAN", ip);
    world.add_combat_unit(x0, y0, 0, mk("IVAN", 125, "none", 400, 50, 1.5, kAll100));
    world.add_combat_unit(x0 + 1, y0, 1, mk("HTNK", 1000, "heavy", 0, 60, 0.0, {}, true));
    world.add_combat_unit(x0 + 2, y0, 1, mk("HTNK", 1000, "heavy", 0, 60, 0.0, {}, true));
    world.units()[0].target = 1;
    world.units()[0].attack_order = true;
    int plant = -1, blast = -1;
    for (int k = 0; k < 600 && blast < 0; ++k) {
        world.step(1.0 / 15.0);
        for (const Ev& e : world.take_ability_events()) {
            if (e.kind == Ev::Kind::BombPlant) plant = k;
            if (e.kind == Ev::Kind::BombBlast) blast = k;
        }
    }
    REQUIRE(plant >= 0);
    REQUIRE(blast >= 0);
    CHECK(blast - plant == 451);
    CHECK(world.units()[1].hp == 1000 - 450);   // center: full IvanDamage, heavy 100%
    // 1 cell = 256 of 384 leptons: 450*(1 - .75*256/384) = 225.
    CHECK(world.units()[2].hp == 1000 - 225);
    CHECK(world.units()[0].target == -1);       // FireOnce
}

TEST_CASE("abilities world: terrorist suicide fires TerrorBomb at his position [RE: §F]") {
    ABIL_WORLD(world, x0, y0);
    AbilityProfile tp;
    tp.fire_kind = AbilityKind::Suicide;
    tp.explodes = true;
    tp.no_passive_acquire = true;
    tp.no_retaliate = true;
    tp.death.valid = true;
    tp.death.damage = 225;
    tp.death.verses = {150, 100, 100, 90, 50, 50, 100, 150, 30, 100, 100};
    tp.death.cell_spread = 2.0;
    tp.death.percent_at_max = 0.5;
    tp.death.kind = AbilityKind::Suicide;
    world.register_ability_profile("TRST", tp);
    world.add_combat_unit(x0, y0, 0, mk("TRST", 75, "flak", 225, 10, 1.5, tp.death.verses));
    world.add_combat_unit(x0 + 1, y0, 1, mk("HTNK", 400, "heavy", 0, 60, 0.0, {}, true));
    world.units()[0].target = 1;
    world.units()[0].attack_order = true;
    std::vector<Ev> all;
    for (int k = 0; k < 30; ++k) {
        world.step(1.0 / 15.0);
        for (const Ev& e : world.take_ability_events()) all.push_back(e);
    }
    CHECK(count_kind(all, Ev::Kind::Suicide) == 1);
    CHECK(count_kind(all, Ev::Kind::DeathWeapon) == 1);
    // 1 cell: .75 -> ftol(168.75)=168 -> heavy 50% = 84.
    CHECK(world.units()[1].hp == 400 - 84);
}

TEST_CASE("abilities world: deployed desolator irradiates on the 16-frame phase [RE: §A]") {
    ABIL_WORLD(world, x0, y0);
    AbilityProfile dp;
    dp.deployer = true;
    dp.immune_radiation = true;
    dp.deploy.valid = true;
    dp.deploy.damage = 1;
    dp.deploy.rof = 60;
    dp.deploy.verses = {100, 100, 100, 20, 10, 10, 0, 0, 0, 100, 100};
    dp.deploy.cell_spread = 10;
    dp.deploy.rad_level = 500;
    world.register_ability_profile("DESO", dp);
    world.add_combat_unit(x0, y0, 0, mk("DESO", 150, "plate", 0, 50, 0.0, {}));
    world.add_combat_unit(x0 + 5, y0, 1, mk("CONS", 900, "flak", 0, 20, 0.0, {}));
    world.step(1.0 / 15.0);
    REQUIRE(world.order_deploy(0));
    std::vector<std::uint64_t> rad_frames;
    for (int k = 0; k < 100; ++k) {
        const std::uint64_t f = world.ability_frame();
        world.step(1.0 / 15.0);
        for (const Ev& e : world.take_ability_events())
            if (e.kind == Ev::Kind::RadDamage && e.a == 1) rad_frames.push_back(f);
    }
    REQUIRE(rad_frames.size() >= 3);
    for (std::uint64_t f : rad_frames) CHECK(f % 16 == 0);
    CHECK(world.rad_field().live_sites() == 1);
    CHECK(world.units()[0].hp == 150);   // immune
    CHECK(world.units()[0].frozen);      // deployed = immobile
}

TEST_CASE("abilities world: IFV weapon follows the passenger's IFVMode [RE: §G]") {
    ABIL_WORLD(world, x0, y0);
    AbilityProfile fv;
    fv.gunner = true;
    for (int k = 0; k < 17; ++k) {
        WeaponSpec w;
        w.valid = true;
        w.damage = 100 + k;
        w.rof = 10 + k;
        w.range = 6;
        fv.ifv_weapons.push_back(w);
    }
    AbilityProfile gi;
    gi.ifv_mode = 2;
    world.register_ability_profile("FV", fv);
    world.register_ability_profile("GI", gi);
    UnitCombat fvc = mk("FV", 200, "light", 100, 10, 6.0, {}, true);
    fvc.passengers = 1;
    world.add_combat_unit(x0, y0, 0, fvc);
    world.add_combat_unit(x0 + 1, y0, 0, mk("GI", 125, "none", 15, 20, 4.0, {}));
    world.step(1.0 / 15.0);
    CHECK(world.units()[0].w_damage == 100);
    world.units()[0].cargo.push_back(1);
    world.units()[1].cargo_of = 0;
    world.step(1.0 / 15.0);
    CHECK(world.units()[0].ifv_current == 2);
    CHECK(world.units()[0].w_damage == 102);
    world.units()[0].cargo.clear();
    world.units()[1].cargo_of = -1;
    world.step(1.0 / 15.0);
    CHECK(world.units()[0].ifv_current == 0);
    CHECK(world.units()[0].w_damage == 100);
}
