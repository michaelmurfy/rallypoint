// L2: the per-house economy inside the World (engine/world_econ.cpp) --
// two human houses each build, place and fight in ONE World; production
// commands (PRODUCE / SUSPEND / ABANDON / PLACE) per issuing house; the
// issuer gate; save/load of the economy.
#include "doctest.h"

#include <cmath>
#include <vector>

#include "asset_fixture.hpp"
#include "../engine/savegame.hpp"
#include "../engine/world.hpp"
#include "../engine/harvest.hpp"   // OreValues

using ra2engine::ProdResult;
using ra2engine::QueueCat;

namespace {

// Roster: [0] ConYard, [1] power plant, [2] barracks. Unit roster: [0] GI.
std::vector<ra2engine::BuildingTypeDef> econ_roster() {
    std::vector<ra2engine::BuildingTypeDef> t(3);
    t[0].id = "GACNST"; t[0].conyard = true; t[0].cost = 3000; t[0].tech_level = 1;
    t[0].strength = 1000; t[0].adjacent = 3; t[0].fw = 1; t[0].fh = 1;
    t[1].id = "GAPOWR"; t[1].cost = 600; t[1].tech_level = 1; t[1].strength = 750;
    t[1].power = 200; t[1].adjacent = 3; t[1].fw = 1; t[1].fh = 1;
    t[2].id = "GAPILE"; t[2].cost = 500; t[2].tech_level = 1; t[2].strength = 500;
    t[2].power = -10; t[2].adjacent = 3; t[2].fw = 1; t[2].fh = 1;
    return t;
}
ra2engine::UnitTypeDef gi_def() {
    ra2engine::UnitTypeDef d;
    d.id = "E1";
    d.cost = 200;
    d.tech_level = 1;
    d.prerequisite = {"GAPILE"};
    d.combat.strength = 125;
    d.combat.armor = "none";
    d.combat.image = "GI";
    d.combat.damage = 15;
    d.combat.rof = 20;
    d.combat.range = 4.0;
    d.combat.verses = {100, 80, 80, 50, 25, 25, 75, 50, 25, 100, 100};
    return d;
}
ra2engine::PlayerEconRules econ_rules() {
    ra2engine::PlayerEconRules r;
    r.units = {gi_def()};
    r.factory_types[static_cast<int>(QueueCat::Structure)] = {0};
    r.factory_types[static_cast<int>(QueueCat::Defense)] = {0};
    r.factory_types[static_cast<int>(QueueCat::Infantry)] = {2};
    r.build_speed = 0.1;   // fast builds for the test
    r.wall_rules = false;
    return r;
}
constexpr double kDt = 1.0 / 15.0;

// A free, open cell (7x7 clear around it) near (x0, y0), at least `minr` away.
bool open_cell(const ra2engine::World& w, int x0, int y0, int minr, int& ox, int& oy) {
    for (int r = minr; r < 60; ++r)
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
                if (std::abs(dx) != r && std::abs(dy) != r) continue;
                const int x = x0 + dx, y = y0 + dy;
                bool ok = true;
                for (int yy = y - 3; yy <= y + 3 && ok; ++yy)
                    for (int xx = x - 3; xx <= x + 3 && ok; ++xx)
                        ok = w.walkable(xx, yy) && w.building_at_cell(xx, yy) < 0;
                if (ok) { ox = x; oy = y; return true; }
            }
    return false;
}

// PLACE the house's finished structure on the first cell the World accepts.
bool place_near(ra2engine::World& w, int house, QueueCat cat, int cx, int cy) {
    for (int r = 1; r <= 3; ++r)
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx)
                if (w.econ_place(house, cat, cx + dx, cy + dy) == ProdResult::Placed) return true;
    return false;
}

}  // namespace

TEST_CASE("house econ: the issuer gate (can_command / controllable) is per issuing house") {
    ra2assets::MapFile map;
    ra2engine::World w(map);
    w.set_house(0, ra2engine::World::HouseRec{});
    w.set_house(1, ra2engine::World::HouseRec{});
    ra2engine::World::HouseRec pc;
    pc.player_control = true;
    w.set_house(2, pc);
    CHECK(w.can_command(0, 0));
    CHECK_FALSE(w.can_command(0, 1));
    CHECK(w.can_command(1, 1));
    CHECK_FALSE(w.can_command(1, 0));
    CHECK(w.can_command(0, 2));          // house 0 is human by default
    CHECK_FALSE(w.can_command(1, 2));    // house 1 is not (yet)
    w.set_human(1, true);
    CHECK(w.can_command(1, 2));          // a PlayerControl house obeys any human
    CHECK(w.controllable(0));            // default issuer = house 0
    w.set_order_issuer(1);
    CHECK_FALSE(w.controllable(0));
    CHECK(w.controllable(1));
}

TEST_CASE("house econ: two human houses build, place and fight in one World") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("skipped: real map assets not found"); return; }
    ra2assets::MapFile map = ra2fixture::mapsmd03_map();
    ra2engine::World w(map);
    w.set_economy(econ_roster(), {}, ra2engine::OreValues{}, 600.0, 0.0, 0.0);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    int cx[2], cy[2];
    REQUIRE(open_cell(w, c->x, c->y, 0, cx[0], cy[0]));
    REQUIRE(open_cell(w, cx[0], cy[0], 9, cx[1], cy[1]));
    for (int h = 0; h < 2; ++h) {
        w.add_building(0, cx[h], cy[h], 1, 1, h, 1000, 0, 0.0, /*instant=*/true);
        w.enable_player_economy(h, 5000.0);
        w.set_player_econ_rules(h, econ_rules());
    }
    CHECK(w.is_human(0));
    CHECK(w.is_human(1));
    CHECK(w.player_economy(0));
    CHECK(w.player_economy(1));
    CHECK(w.credits(1) == doctest::Approx(5000.0));

    // Each house builds a power plant and a barracks, one queue at a time.
    for (int bt : {1, 2}) {
        for (int h = 0; h < 2; ++h) CHECK(w.econ_produce(h, QueueCat::Structure, bt) == ProdResult::Started);
        // A second building on a busy factory is refused (UnableToComply).
        CHECK(w.econ_produce(0, QueueCat::Structure, bt == 1 ? 2 : 1) == ProdResult::Busy);
        for (int f = 0; f < 3000; ++f) {
            w.step(kDt);
            if (w.econ(0)->queues.ready(QueueCat::Structure) && w.econ(1)->queues.ready(QueueCat::Structure)) break;
        }
        for (int h = 0; h < 2; ++h) {
            REQUIRE(w.econ(h)->queues.ready(QueueCat::Structure));
            CHECK(place_near(w, h, QueueCat::Structure, cx[h], cy[h]));
            CHECK(w.econ(h)->queues.producing(QueueCat::Structure) == -1);
        }
        for (int f = 0; f < 300; ++f) w.step(kDt);   // buildup -> Idle
    }
    // Each house paid for its own buildings out of its own wallet.
    CHECK(w.credits(0) == doctest::Approx(5000.0 - 600.0 - 500.0));
    CHECK(w.credits(1) == doctest::Approx(5000.0 - 600.0 - 500.0));
    int own0 = 0, own1 = 0;
    for (const ra2engine::Building& b : w.buildings()) (b.house == 0 ? own0 : own1) += b.alive;
    CHECK(own0 == 3);
    CHECK(own1 == 3);

    // Infantry: both train a GI; it exits at the house's own barracks.
    for (int h = 0; h < 2; ++h) CHECK(w.econ_produce(h, QueueCat::Infantry, 0) == ProdResult::Started);
    CHECK(w.econ_produce(0, QueueCat::Infantry, 0) == ProdResult::Queued);   // a second one behind it
    CHECK(w.econ_queued_count(0, QueueCat::Infantry, 0) == 2);
    int gi[2] = {-1, -1};
    for (int f = 0; f < 3000 && (gi[0] < 0 || gi[1] < 0); ++f) {
        w.step(kDt);
        for (const auto& [h, u] : w.take_econ_launches())
            if (h >= 0 && h < 2 && gi[h] < 0) gi[h] = u;
    }
    REQUIRE(gi[0] >= 0);
    REQUIRE(gi[1] >= 0);
    CHECK(w.units()[(std::size_t)gi[0]].house == 0);
    CHECK(w.units()[(std::size_t)gi[1]].house == 1);

    // Fight: each player orders its own GI at the other's (issuer per order).
    // House 1's order does not move house 0's GI even when it is selected too.
    // NET L1: orders carry the Whom ids and the issuing house.
    w.set_order_issuer(1);
    w.group_attack(1, {gi[0], gi[1]}, gi[0]);
    CHECK(w.units()[(std::size_t)gi[0]].target != gi[0]);
    w.set_order_issuer(0);
    w.group_attack(0, {gi[0]}, gi[1]);
    const int hp0 = w.units()[(std::size_t)gi[0]].hp, hp1 = w.units()[(std::size_t)gi[1]].hp;
    bool hit = false;
    for (int f = 0; f < 3000 && !hit; ++f) {
        w.step(kDt);
        const ra2engine::Unit& a = w.units()[(std::size_t)gi[0]];
        const ra2engine::Unit& b = w.units()[(std::size_t)gi[1]];
        hit = (a.hp < hp0 || !a.alive) && (b.hp < hp1 || !b.alive);
    }
    CHECK(hit);   // both houses' soldiers took fire from the other's
}

TEST_CASE("house econ: PLACE keeps the fractional buildup time (seconds, not truncated)") {
    // MR2: PlayerEconRules::buildup_total held ints, so a 2.6 s buildup became
    // 2 s -- every structure a human placed finished its Buildup early (the
    // pre-L2 scene passed ba_.art[t].buildup_total, a double, to add_building).
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("skipped: real map assets not found"); return; }
    ra2assets::MapFile map = ra2fixture::mapsmd03_map();
    ra2engine::World w(map);
    w.set_economy(econ_roster(), {}, ra2engine::OreValues{}, 600.0, 0.0, 0.0);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    int cx = 0, cy = 0;
    REQUIRE(open_cell(w, c->x, c->y, 0, cx, cy));
    w.add_building(0, cx, cy, 1, 1, 0, 1000, 0, 0.0, /*instant=*/true);
    w.enable_player_economy(0, 5000.0);
    ra2engine::PlayerEconRules r = econ_rules();
    r.buildup_total = {0.0, 2.6, 0.0};
    w.set_player_econ_rules(0, r);
    REQUIRE(w.econ_produce(0, QueueCat::Structure, 1) == ProdResult::Started);
    for (int f = 0; f < 3000 && !w.econ(0)->queues.ready(QueueCat::Structure); ++f) w.step(kDt);
    REQUIRE(w.econ(0)->queues.ready(QueueCat::Structure));
    REQUIRE(place_near(w, 0, QueueCat::Structure, cx, cy));
    int bi = -1;
    for (int i = 0; i < static_cast<int>(w.buildings().size()); ++i)
        if (w.buildings()[(std::size_t)i].type == 1) bi = i;
    REQUIRE(bi >= 0);
    CHECK(w.buildings()[(std::size_t)bi].buildup_total == doctest::Approx(2.6));
    // Still building up after 2 s (30 frames); Idle after 2.6 s (39 frames).
    for (int f = 0; f < 32; ++f) w.step(kDt);
    CHECK(w.buildings()[(std::size_t)bi].state != ra2engine::Building::State::Idle);
    for (int f = 0; f < 10; ++f) w.step(kDt);
    CHECK(w.buildings()[(std::size_t)bi].state == ra2engine::Building::State::Idle);
}

TEST_CASE("house econ: SUSPEND holds, ABANDON refunds, save/load continues identically") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("skipped: real map assets not found"); return; }
    ra2assets::MapFile map = ra2fixture::mapsmd03_map();
    ra2engine::World w(map);
    w.set_economy(econ_roster(), {}, ra2engine::OreValues{}, 600.0, 0.0, 0.0);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    int x = 0, y = 0;
    REQUIRE(open_cell(w, c->x, c->y, 0, x, y));
    w.add_building(0, x, y, 1, 1, 1, 1000, 0, 0.0, true);   // house 1 is the human here
    w.enable_player_economy(1, 3000.0);
    const ra2engine::PlayerEconRules r = econ_rules();
    w.set_player_econ_rules(1, r);
    CHECK_FALSE(w.player_economy(0));
    CHECK(w.econ_produce(0, QueueCat::Structure, 1) == ProdResult::Refused);   // no economy

    CHECK(w.econ_produce(1, QueueCat::Structure, 1) == ProdResult::Started);
    for (int f = 0; f < 20; ++f) w.step(kDt);
    const double spent = 3000.0 - w.credits(1);
    CHECK(spent > 0.0);
    CHECK(w.econ_suspend(1, QueueCat::Structure) == ProdResult::Suspended);
    CHECK(w.econ_held(1, QueueCat::Structure));
    const int stage = w.econ(1)->queues.state(QueueCat::Structure).slot.stage;
    for (int f = 0; f < 60; ++f) w.step(kDt);
    CHECK(w.econ(1)->queues.state(QueueCat::Structure).slot.stage == stage);   // held: no progress
    CHECK(w.credits(1) == doctest::Approx(3000.0 - spent));                     // and no charge

    // Save mid-hold, continue both copies: identical bytes.
    const std::vector<std::uint8_t> bytes = ra2engine::save::save_world(w);
    ra2engine::World w2(map);
    REQUIRE(ra2engine::save::load_world(w2, bytes));
    w2.set_player_econ_rules(1, r);   // asset-derived, re-installed by the host
    CHECK(w2.econ_held(1, QueueCat::Structure));
    CHECK(w2.credits(1) == doctest::Approx(w.credits(1)));
    for (ra2engine::World* ww : {&w, &w2}) {
        CHECK(ww->econ_produce(1, QueueCat::Structure, 1) == ProdResult::Resumed);
        for (int f = 0; f < 40; ++f) ww->step(kDt);
    }
    CHECK(ra2engine::save::save_world(w) == ra2engine::save::save_world(w2));

    // ABANDON refunds everything spent.
    CHECK(w.econ_abandon(1, QueueCat::Structure, 1, false) == ProdResult::Abandoned);
    CHECK(w.credits(1) == doctest::Approx(3000.0));
    CHECK(w.econ(1)->queues.producing(QueueCat::Structure) == -1);
    CHECK(w.econ_abandon(1, QueueCat::Structure, 1, false) == ProdResult::Refused);
}
