// Wave-8 bridge fixes (docs/gap/wave8 root causes 2, 4, 5): the low bridge is
// Road (FUN_0047D1C0), the collapse kills (FUN_0047DC80 high, FUN_00487920
// low) and the bridge damage sources (DamageArea FUN_00489190's roll, its deck
// window 0x489E87, the storm bolt FUN_0053A030, the jumpjet crash on a deck
// FUN_0054C7C0).
#include "doctest.h"

#include "asset_fixture.hpp"
#include "bridges.hpp"
#include "unit_w6.hpp"   // kW6Wall
#include "world.hpp"

using namespace ra2engine;

namespace {

bool dead(const Unit& u) { return !u.alive || u.die_time >= 0.0 || u.hp <= 0; }

// A 3-cell low-bridge cross-section (LOBRDG01 0x4A, ColumnY: (x, y-1..y+1))
// over `tile` cells, the rest of the map Clear, land speed armed with the TMP
// land bytes (the tile type under the bridge).
struct LowBridgeWorld {
    World w;
    int x = -1, y = -1;
    explicit LowBridgeWorld(ra2assets::LandType tile) : w(ra2fixture::mapsmd03_map()) {
        const int gw = w.grid_w(), gh = w.grid_h();
        const std::size_t n = static_cast<std::size_t>(gw) * gh;
        for (int cy = 2; cy < gh - 2 && x < 0; ++cy)
            for (int cx = 2; cx < gw - 2 && x < 0; ++cx) {
                bool ok = true;
                for (int dy = -2; dy <= 2 && ok; ++dy)
                    for (int dx = -2; dx <= 2 && ok; ++dx) ok = w.walkable(cx + dx, cy + dy);
                if (ok) { x = cx; y = cy; }
            }
        REQUIRE(x >= 0);
        std::vector<ra2assets::LandType> lt(n, ra2assets::LandType::Clear), tiles = lt;
        std::vector<int> ovl(n, -1);
        std::vector<std::uint8_t> land(n, 0);
        for (int dy = -1; dy <= 1; ++dy) {
            const std::size_t i = static_cast<std::size_t>(y + dy) * gw + x;
            tiles[i] = tile;
            lt[i] = ra2assets::LandType::Road;   // the load path (depth_set_terrain)
            ovl[i] = 0x4A;
            land[i] = static_cast<std::uint8_t>(tile);
        }
        w.set_land_terrain(lt);
        w.set_bridges(ovl, tiles);
        World::LandSpeedRules r;
        w.arm_land_speed(land, std::vector<std::uint8_t>(n, 0), r);
        World::DepthRules d;
        d.bridge_strength = 1;   // Random(1, 1) = 1 < any damage >= 2
        d.armed = true;
        w.set_depth_rules(d);
    }
    void collapse() {   // intact -> damaged -> destroyed
        w.force_bridge_break(x, y);
        w.force_bridge_break(x, y);
        REQUIRE(low_bridge_destroyed(w.bridge_overlay_at(x, y)));
    }
};

// A 5-cell high-bridge deck over water along y = 20, x = 10..14.
void arm_high(World& w, bool armed_rules = true) {
    const int W = w.grid_w();
    std::vector<std::uint8_t> hb(static_cast<std::size_t>(W) * w.grid_h(), 0);
    for (int x = 10; x <= 14; ++x) hb[20 * W + x] = 1;
    w.set_high_bridges(hb);
    World::DepthRules d;
    d.bridge_strength = 1;
    d.armed = armed_rules;
    w.set_depth_rules(d);
}

}  // namespace

TEST_CASE("w8 W8-01-04/W8-03-03/W8-04-01: an intact low bridge is Road (FUN_0047D1C0)") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("no install"); return; }
    LowBridgeWorld b(ra2assets::LandType::Water);
    World& w = b.w;
    CHECK(w.speed_land_at(b.x, b.y) == 1);              // Road, not the Water tile
    CHECK_FALSE(w.an_victim_naval_cell(b.x, b.y));      // subs / Aegis refuse it (0x6FBC86)
    CHECK(w.walkable(b.x, b.y));
    CHECK(w.grid_for(MovementDomain::Water)[static_cast<std::size_t>(b.y) * w.grid_w() + b.x] == 0);
    b.collapse();
    CHECK(w.speed_land_at(b.x, b.y) == 2);              // the tile's Water again
    CHECK(w.an_victim_naval_cell(b.x, b.y));
    CHECK_FALSE(w.walkable(b.x, b.y));
    const int hut = w.add_building(0, b.x + 2, b.y, 1, 1, 0, 100, 0, 0.0, true);
    REQUIRE(hut >= 0);
    CHECK(w.repair_bridge_near(hut) > 0);
    CHECK(w.speed_land_at(b.x, b.y) == 1);

    // Drive speed reads the same Road row (W8-03-03: full speed, not 0.5).
    MoveSpeedCtx c;
    c.armed = true;
    std::vector<std::uint8_t> land(3, 2);   // Water tiles
    std::vector<std::int16_t> ovl = {-1, 0x4A, 100};
    c.land = &land;
    c.bridge_ovl = &ovl;
    for (auto& row : c.cost)
        for (float& v : row) v = 1.0f;
    c.cost[2][1] = 0.0f;   // [Water] Track=0%
    Unit tank;
    tank.is_vehicle = true;
    tank.land_speed_loco = true;
    tank.speed_type = 1;
    tank.hp = tank.max_hp = 100;
    CHECK(land_speed_mult(c, tank, 3, 1, 0, 0) == doctest::Approx(0.5));   // the 0% fall-back
    CHECK(land_speed_mult(c, tank, 3, 1, 1, 0) == doctest::Approx(1.0));   // intact bridge: Road
    CHECK(land_speed_mult(c, tank, 3, 1, 2, 0) == doctest::Approx(0.5));   // destroyed piece: Water
}

TEST_CASE("w8 W8-01-06: a low section broken at runtime over Beach is not walkable") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("no install"); return; }
    LowBridgeWorld b(ra2assets::LandType::Beach);
    World& w = b.w;
    CHECK(w.speed_land_at(b.x, b.y) == 1);   // no Tread / water sounds on the intact bridge
    b.collapse();
    CHECK_FALSE(w.walkable(b.x, b.y));       // the load path's Beach rule
    CHECK(w.amphib_grid()[static_cast<std::size_t>(b.y) * w.grid_w() + b.x] == 1);
    CHECK(w.speed_land_at(b.x, b.y) == 6);
}

TEST_CASE("w8 W8-01-03/W8-03-07/W8-03-06: low collapse kills what cannot enter (FUN_00487920)") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("no install"); return; }
    LowBridgeWorld b(ra2assets::LandType::Water);
    World& w = b.w;
    const int x = b.x, y = b.y;
    // Three GIs on one cell, an amphibious Tanya, a hover Robot Tank, a tank
    // carrying an amphibious SEAL, a landed jumpjet; a GI heading in from
    // (x, y+2); an amphibious unit heading in survives.
    std::vector<int> gis;
    for (int k = 0; k < 3; ++k) {
        w.add_unit(x, y, "GI");
        gis.push_back(static_cast<int>(w.units().size()) - 1);
    }
    w.add_unit(x, y, "TANY");
    const int tanya = static_cast<int>(w.units().size()) - 1;
    w.units()[tanya].movement_domain = MovementDomain::Amphibious;
    w.add_unit(x, y - 1, "ROBO");
    const int robo = static_cast<int>(w.units().size()) - 1;
    w.units()[robo].is_vehicle = true;
    w.units()[robo].movement_domain = MovementDomain::Amphibious;
    w.add_unit(x, y + 1, "MTNK");
    const int tank = static_cast<int>(w.units().size()) - 1;
    w.units()[tank].is_vehicle = true;
    w.add_unit(x, y + 1, "GHOST");
    const int seal = static_cast<int>(w.units().size()) - 1;
    w.units()[seal].movement_domain = MovementDomain::Amphibious;
    w.units()[seal].cargo_of = tank;
    w.units()[tank].cargo.push_back(seal);
    w.add_unit(x, y - 1, "JUMPJET");
    const int jj = static_cast<int>(w.units().size()) - 1;
    w.units()[jj].is_aircraft = true;   // landed: altitude 0
    w.add_unit(x + 2, y, "GI");
    const int incoming = static_cast<int>(w.units().size()) - 1;
    w.units()[incoming].path = {{x + 2, y}, {x + 1, y}, {x, y}};
    w.units()[incoming].path_idx = 0;
    // (x+1, y) is land: the incoming GI's NEXT cell is not the bridge -> lives.
    w.add_unit(x + 1, y, "GI");
    const int next_in = static_cast<int>(w.units().size()) - 1;
    w.units()[next_in].path = {{x + 1, y}, {x, y}};
    w.units()[next_in].path_idx = 0;

    b.collapse();
    for (int g : gis) CHECK(dead(w.units()[static_cast<std::size_t>(g)]));   // all three, not one
    CHECK_FALSE(dead(w.units()[static_cast<std::size_t>(tanya)]));
    CHECK_FALSE(dead(w.units()[static_cast<std::size_t>(robo)]));
    CHECK(dead(w.units()[static_cast<std::size_t>(tank)]));
    CHECK(dead(w.units()[static_cast<std::size_t>(seal)]));   // no passenger escape
    CHECK(w.units()[static_cast<std::size_t>(tank)].cargo.empty());
    CHECK(dead(w.units()[static_cast<std::size_t>(jj)]));     // JumpJet, destroy pass
    CHECK_FALSE(dead(w.units()[static_cast<std::size_t>(incoming)]));
    CHECK(dead(w.units()[static_cast<std::size_t>(next_in)]));   // heading into the cell
}

TEST_CASE("w8 W8-01-01/02, W8-03-05, W8-06-02/03: a high span kills every object on and under it") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("no install"); return; }
    World w(ra2fixture::mapsmd03_map());
    arm_high(w);
    std::vector<int> ids;
    for (int k = 0; k < 3; ++k) {   // three GIs on the deck cell
        w.add_unit(12, 20, "E1");
        ids.push_back(static_cast<int>(w.units().size()) - 1);
        w.units().back().on_deck = 1;
    }
    w.add_unit(12, 20, "GHOST");   // a SEAL swimming under the deck
    ids.push_back(static_cast<int>(w.units().size()) - 1);
    w.units().back().movement_domain = MovementDomain::Amphibious;
    w.add_unit(13, 20, "DEST");    // a destroyer under the span
    ids.push_back(static_cast<int>(w.units().size()) - 1);
    w.units().back().is_naval = true;
    w.units().back().movement_domain = MovementDomain::Water;
    w.add_unit(11, 20, "FV");      // an IFV on the deck with a passenger
    const int ifv = static_cast<int>(w.units().size()) - 1;
    w.units().back().is_vehicle = true;
    w.units().back().on_deck = 1;
    w.add_unit(11, 20, "GHOST");
    const int pas = static_cast<int>(w.units().size()) - 1;
    w.units()[pas].cargo_of = ifv;
    w.units()[ifv].cargo.push_back(pas);
    w.add_unit(12, 20, "ORCA");    // an aircraft flying over it lives
    const int air = static_cast<int>(w.units().size()) - 1;
    w.units()[air].is_aircraft = true;
    w.units()[air].altitude = 800.0;
    w.add_unit(16, 20, "E1");      // off the span
    const int off = static_cast<int>(w.units().size()) - 1;

    CHECK(w.force_bridge_break(12, 20) == 5);
    for (int i : ids) CHECK(dead(w.units()[static_cast<std::size_t>(i)]));
    CHECK(dead(w.units()[static_cast<std::size_t>(ifv)]));
    CHECK(dead(w.units()[static_cast<std::size_t>(pas)]));   // PreventPassengerEscape
    CHECK_FALSE(dead(w.units()[static_cast<std::size_t>(air)]));
    CHECK_FALSE(dead(w.units()[static_cast<std::size_t>(off)]));
}

TEST_CASE("w8 W8-04-03/W8-05-05/W8-03-09: the high-bridge roll needs the deck window (0x489E87)") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("no install"); return; }
    World w(ra2fixture::mapsmd03_map());
    arm_high(w);
    CHECK_FALSE(w.bridges_enabled());   // no low bridge on this world
    const int lv = w.cell_level(12, 20);
    CHECK(w.deck_z(12, 20) == (lv + 4) * 104);
    // At the water line (a shell at a ship under the span): never.
    CHECK(w.bridge_explosion(12, 20, 5000, true, lv * 104) == 0);
    CHECK(w.bridge_explosion(12, 20, 5000, true, (lv + 2) * 104 + 0) == 0);    // (L-2)*104+416: exclusive
    CHECK(w.bridge_explosion(12, 20, 5000, true, (lv + 5) * 104 + 1) == 0);    // > (L+1)*104+416
    CHECK(w.bridge_explosion(12, 20, 5000, false, w.deck_z(12, 20)) == 0);     // Wall=no
    CHECK_FALSE(w.high_bridge_destroyed(12, 20));
    CHECK(w.bridge_area_roll(12.0, 20.0, 5000, kW6Wall, (lv + 5) * 104) == 5);   // top of the window
    CHECK(w.high_bridge_destroyed(12, 20));
}

TEST_CASE("w8 W8-06-04: a Lightning Storm bolt rolls the bridge (FUN_0053A030 -> FUN_00489190)") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("no install"); return; }
    World w(ra2fixture::mapsmd03_map());
    arm_high(w);
    SwParams p;
    p.storm_damage = 250;
    p.storm_wall_flags = kW6Wall;   // [IonWH] Wall=yes
    w.set_sw_params(p);
    CHECK(w.trigger_lightning_strike(0, 12, 20));
    CHECK(w.high_bridge_destroyed(12, 20));   // BridgeStrength 1: always
    // BridgeStrength 1500: Random(1, 1500) < 250, about one bolt in six.
    World w2(ra2fixture::mapsmd03_map());
    arm_high(w2);
    World::DepthRules d;
    d.armed = true;
    w2.set_depth_rules(d);
    w2.set_sw_params(p);
    int bolts = 0;
    while (!w2.high_bridge_destroyed(12, 20) && bolts < 200) {
        w2.trigger_lightning_strike(0, 12, 20);
        ++bolts;
    }
    CHECK(w2.high_bridge_destroyed(12, 20));
    CHECK(bolts > 0);
}

TEST_CASE("w8 W8-05-04: a crashing Kirov stops on the deck and its death weapon rolls the bridge") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("no install"); return; }
    World w(ra2fixture::mapsmd03_map());
    arm_high(w);
    Unit& k = w.add_unit(12, 20, "ZEP");
    const int ki = static_cast<int>(w.units().size()) - 1;
    k.is_aircraft = true;
    k.balloon_hover = true;
    k.an.t.crashable = true;
    k.altitude = 1200.0;
    k.descend_rate = 600.0;
    k.an.t.crash_weapon.valid = true;
    k.an.t.crash_weapon.damage = 250;
    k.an.t.crash_weapon.cell_spread = 1.0;
    k.an.t.crash_weapon.percent_at_max = 1.0;
    k.an.t.crash_weapon.verses = std::vector<int>(11, 100);
    k.an.t.crash_weapon.wall_flags = kW6Wall;   // [BlimpHE] Wall=yes
    w.campaign_kill_unit(ki);
    double lowest = 1e9;
    for (int f = 0; f < 120 && !w.high_bridge_destroyed(12, 20); ++f) {
        w.step(1.0 / 15.0);
        lowest = std::min(lowest, w.units()[static_cast<std::size_t>(ki)].altitude);
    }
    CHECK(w.high_bridge_destroyed(12, 20));   // BridgeStrength 1: the deck-height blast breaks it
    CHECK(lowest == doctest::Approx(416.0));  // it came to rest on the deck, not in the river
}
