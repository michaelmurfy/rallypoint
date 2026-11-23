// Wave-7 AI per-unit rules (engine/ai_unit.cpp): W7-01-08 auto-deploy,
// W7-04-18 ToProtect, W7-09-17 rules-id unit typing.
#include "doctest.h"

#include <vector>

#include "asset_fixture.hpp"
#include "world.hpp"

namespace {

ra2engine::UnitCombat w7_gi(bool deployer) {
    ra2engine::UnitCombat c;
    c.strength = 125;
    c.armor = "none";
    c.image = "GI";
    c.damage = 15;
    c.rof = 20;
    c.range = 4.0;
    c.verses = {100, 80, 80, 50, 25, 25, 75, 50, 25, 100, 100};
    c.tg.rules_id = "E1";
    if (deployer) {
        c.deploy_fire = true;
        c.deploy_fire_slot = 1;
        c.secondary.valid = true;
        c.secondary.id = "Para";
        c.secondary.damage = 15;
        c.secondary.rof = 10;
        c.secondary.range = 5.0;
        c.secondary.verses = c.verses;
    }
    return c;
}

bool free_cell(const ra2engine::World& w, int x, int y) {
    return w.walkable(x, y) && w.unit_at_cell(x, y) < 0 && w.building_at_cell(x, y) < 0;
}

// A free walkable cell in the ring around (cx,cy) at Chebyshev radius >= r0.
bool ring_cell(const ra2engine::World& w, int cx, int cy, int r0, int& ox, int& oy) {
    for (int r = r0; r <= r0 + 10; ++r)
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                if (free_cell(w, cx + dx, cy + dy)) { ox = cx + dx; oy = cy + dy; return true; }
            }
    return false;
}

void three_houses(ra2engine::World& world) {
    for (int h = 0; h < 3; ++h) {
        ra2engine::World::HouseRec r;
        r.side = h == 1 ? ra2engine::World::Side::Soviet : ra2engine::World::Side::Allied;
        r.color_ix = h;
        world.set_house(h, r);
    }
}

}  // namespace

// [RE: InfantryClass FUN_00521050 @0x52128C, rulesmd [General]
// AIAutoDeployFrameDelay=15,25,100]: a computer house's GI standing in Guard
// deploys once the delay has run; a house without the rule (or a human one)
// never does; a move order packs it up again.
TEST_CASE("W7-01-08: a computer GI auto-deploys after AIAutoDeployFrameDelay frames in Guard") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("skipped: real map assets not found"); return; }
    ra2engine::World world(ra2fixture::mapsmd03_map());
    three_houses(world);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(ra2fixture::mapsmd03_map());
    REQUIRE(c != nullptr);
    ra2engine::AiHouseConfig cfg;
    cfg.auto_deploy_frames = 15;            // the Hard (house+0x184 = 0) entry
    world.enable_ai_house(1, cfg);
    ra2engine::AiHouseConfig off;          // auto_deploy_frames = -1: no rule
    world.enable_ai_house(2, off);
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0, x0 = 0, y0 = 0;
    REQUIRE(ring_cell(world, c->x, c->y, 0, x1, y1));
    world.add_combat_unit(x1, y1, 1, w7_gi(true));
    REQUIRE(ring_cell(world, c->x, c->y, 3, x2, y2));
    world.add_combat_unit(x2, y2, 2, w7_gi(true));
    world.set_pair_alliance(1, 2, true);
    REQUIRE(ring_cell(world, c->x + 40, c->y + 40, 0, x0, y0));
    world.add_combat_unit(x0, y0, 0, w7_gi(true));   // human house: never auto-deploys
    for (int f = 0; f < 10; ++f) world.step(1.0 / 15.0);
    CHECK_FALSE(world.units()[0].deployed);          // the delay has not run yet
    for (int f = 0; f < 20; ++f) world.step(1.0 / 15.0);
    CHECK(world.units()[0].deployed);                 // computer house, rule armed
    CHECK_FALSE(world.units()[1].deployed);           // rule off
    CHECK_FALSE(world.units()[2].deployed);           // human house
    // A move undeploys (FUN_0051F370) and the guard clock restarts on arrival.
    int mx = 0, my = 0;
    REQUIRE(ring_cell(world, x1, y1, 2, mx, my));
    world.order_unit_move(0, mx, my);
    CHECK_FALSE(world.units()[0].deployed);
    for (int f = 0; f < 600 && world.units()[0].moving(); ++f) world.step(1.0 / 15.0);
    CHECK_FALSE(world.units()[0].moving());
    for (int f = 0; f < 20; ++f) world.step(1.0 / 15.0);
    CHECK(world.units()[0].deployed);                 // re-deployed at the new post
}

// [RE: FUN_00522070]: in Attack, a computer GI deploys once its target is inside
// the deploy weapon's range, and undeploys (to pursue) when it is not.
TEST_CASE("W7-01-08: a computer GI deploys on an in-range attack target and undeploys for a far one") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("skipped: real map assets not found"); return; }
    ra2engine::World world(ra2fixture::mapsmd03_map());
    three_houses(world);
    const ra2assets::MapCell* c = ra2engine::pick_start_cell(ra2fixture::mapsmd03_map());
    REQUIRE(c != nullptr);
    ra2engine::AiHouseConfig cfg;
    cfg.auto_deploy_frames = 100000;   // keep the Guard branch out of the way
    world.enable_ai_house(1, cfg);
    int x1 = 0, y1 = 0;
    REQUIRE(ring_cell(world, c->x, c->y, 0, x1, y1));
    world.add_combat_unit(x1, y1, 1, w7_gi(true));
    int tx = 0, ty = 0;
    REQUIRE(ring_cell(world, x1, y1, 3, tx, ty));     // 3 cells: inside Para's 5
    ra2engine::UnitCombat tgt = w7_gi(false);
    tgt.strength = 100000;                            // survives the test
    tgt.damage = 0;
    world.add_combat_unit(tx, ty, 0, tgt);
    world.order_unit_attack(0, 1);
    world.step(1.0 / 15.0);
    CHECK(world.units()[0].deployed);
}

// fv7c: the call for help (FUN_00707500) is ONE routine, World::call_for_help,
// run from the W7 hit log for every computer house; the type's ToProtect flag
// (not the AI roster) triggers it, and the candidates are ranked by
// FUN_004D96B0 (ThreatPosed) within attacker ThreatPosed x 3.
namespace {
void arm_w7(ra2engine::World& world) {
    ra2engine::World::CombatW7Rules r;
    r.armed = true;
    world.arm_combat_w7(r);
}
struct HelpRig {
    ra2engine::World world{ra2fixture::mapsmd03_map()};
    int vx = 0, vy = 0;
    bool ok = false;
    HelpRig() {
        three_houses(world);
        arm_w7(world);
        const ra2assets::MapCell* c = ra2engine::pick_start_cell(ra2fixture::mapsmd03_map());
        if (!c || !ring_cell(world, c->x, c->y, 0, vx, vy)) return;
        ok = true;
    }
    int add(int r0, int house, ra2engine::UnitCombat u) {
        int x = 0, y = 0;
        if (!ring_cell(world, vx, vy, r0, x, y)) return -1;
        world.add_combat_unit(x, y, house, u);
        return (int)world.units().size() - 1;
    }
    bool helped(int i, int a) const {
        const ra2engine::Unit& u = world.units()[(std::size_t)i];
        return u.target == a;
    }
};
ra2engine::UnitCombat protect_victim(bool to_protect) {
    ra2engine::UnitCombat v = w7_gi(false);
    v.tg.rules_id = "HARV";
    v.tg.to_protect = to_protect;
    v.strength = 100000;
    v.damage = 0;
    return v;
}
ra2engine::UnitCombat threat_gi(int threat) {
    ra2engine::UnitCombat u = w7_gi(false);
    u.tg.threat_posed = threat;
    return u;
}
}  // namespace

TEST_CASE("W7-04-18 / fv7c: an attacked ToProtect unit calls idle units onto its attacker (FUN_00707500)") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("skipped: real map assets not found"); return; }
    HelpRig g;
    REQUIRE(g.ok);
    const int v = g.add(0, 1, protect_victim(true));                 // the "miner"
    ra2engine::UnitCombat atk = threat_gi(10);
    atk.strength = 100000;
    const int a = g.add(3, 0, atk);                                    // the enemy
    ra2engine::UnitCombat helper = threat_gi(10);
    helper.range = 1.0;                                                // cannot see the attacker itself
    const int h = g.add(9, 1, helper);                                 // an idle defender
    REQUIRE((v >= 0 && a >= 0 && h >= 0));
    g.world.order_unit_attack(a, v);
    bool called = false;
    for (int f = 0; f < 120 && !called; ++f) {
        g.world.step(1.0 / 15.0);
        called = g.helped(h, a);
    }
    CHECK(g.world.units()[(std::size_t)v].hp < 100000);   // the miner was hit
    CHECK(called);                                         // and its house answered
}

TEST_CASE("fv7c: the call for help needs GetFireError(attacker, 0) != ILLEGAL -- a dog is not sent at a tank") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("skipped: real map assets not found"); return; }
    HelpRig g;
    REQUIRE(g.ok);
    const int v = g.add(0, 1, protect_victim(true));
    ra2engine::UnitCombat tank = threat_gi(10);
    tank.strength = 100000;
    tank.armor = "heavy";
    const int a = g.add(3, 0, tank);
    ra2engine::UnitCombat dog = threat_gi(20);
    dog.range = 1.0;
    dog.verses = {100, 100, 100, 0, 0, 0, 0, 0, 0, 0, 0};   // [ParasiteDog]-like: 0% vs vehicles
    const int d = g.add(2, 1, dog);
    ra2engine::UnitCombat e2 = threat_gi(5);
    e2.range = 1.0;
    const int e = g.add(9, 1, e2);
    REQUIRE((v >= 0 && a >= 0 && d >= 0 && e >= 0));
    g.world.order_unit_attack(a, v);
    bool e_called = false;
    for (int f = 0; f < 120 && !e_called; ++f) {
        g.world.step(1.0 / 15.0);
        e_called = g.helped(e, a);
    }
    CHECK(e_called);
    CHECK_FALSE(g.helped(d, a));
}

TEST_CASE("fv7c: a hit unit WITHOUT ToProtect calls nobody") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("skipped: real map assets not found"); return; }
    HelpRig g;
    REQUIRE(g.ok);
    const int v = g.add(0, 1, protect_victim(false));
    ra2engine::UnitCombat atk = threat_gi(10);
    atk.strength = 100000;
    const int a = g.add(3, 0, atk);
    ra2engine::UnitCombat helper = threat_gi(10);
    helper.range = 1.0;
    const int h = g.add(9, 1, helper);
    REQUIRE((v >= 0 && a >= 0 && h >= 0));
    g.world.order_unit_attack(a, v);
    for (int f = 0; f < 120; ++f) g.world.step(1.0 / 15.0);
    CHECK(g.world.units()[(std::size_t)v].hp < 100000);
    CHECK_FALSE(g.helped(h, a));
}

TEST_CASE("T1.3: call_for_help skips ResourceGatherer helpers (UnitType +0x5EC)") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("skipped: real map assets not found"); return; }
    HelpRig g;
    REQUIRE(g.ok);
    const int v = g.add(0, 1, protect_victim(true));
    ra2engine::UnitCombat atk = threat_gi(10);
    atk.strength = 100000;
    const int a = g.add(3, 0, atk);
    ra2engine::UnitCombat harv = threat_gi(40);
    harv.range = 1.0;
    const int hv = g.add(2, 1, harv);
    REQUIRE(hv >= 0);
    g.world.units()[(std::size_t)hv].is_harvester = true;
    g.world.units()[(std::size_t)hv].is_infantry = false;
    ra2engine::UnitCombat helper = threat_gi(5);
    helper.range = 1.0;
    const int h = g.add(9, 1, helper);
    REQUIRE((v >= 0 && a >= 0 && h >= 0));
    g.world.order_unit_attack(a, v);
    bool called = false;
    for (int f = 0; f < 120 && !called; ++f) {
        g.world.step(1.0 / 15.0);
        called = g.helped(h, a);
    }
    CHECK(called);
    CHECK_FALSE(g.helped(hv, a));
}

TEST_CASE("0054: a loaded transport still answers -- techno +0x2E4 is the Tank Bunker link, not cargo") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("skipped: real map assets not found"); return; }
    HelpRig g;
    REQUIRE(g.ok);
    // Victim is a building so call_for_help runs the vehicle pass filters
    // (ResourceGatherer / cargo) the same way a structure hit does.
    ra2engine::BuildingTypeDef bt;
    bt.id = "GACNST";
    bt.strength = 100000;
    bt.fw = bt.fh = 2;
    g.world.set_building_types({bt});
    int bx = 0, by = 0;
    REQUIRE(ring_cell(g.world, g.vx, g.vy, 0, bx, by));
    const int bi = g.world.add_building(/*type*/ 0, bx, by, /*fw*/ 2, /*fh*/ 2, /*house*/ 1,
                                       /*strength*/ 100000, /*armor*/ 0, /*buildup*/ 0.0, /*instant*/ true);
    REQUIRE(bi >= 0);
    ra2engine::UnitCombat atk = threat_gi(10);
    atk.strength = 100000;
    atk.is_infantry = false;
    atk.is_vehicle = true;
    const int a = g.add(3, 0, atk);
    ra2engine::UnitCombat apc = threat_gi(30);
    apc.range = 1.0;
    apc.is_infantry = false;
    apc.is_vehicle = true;
    apc.passengers = 5;
    const int tr = g.add(2, 1, apc);
    ra2engine::UnitCombat pass = threat_gi(1);
    pass.is_infantry = true;
    const int p = g.add(4, 1, pass);
    REQUIRE((tr >= 0 && p >= 0 && a >= 0));
    g.world.units()[(std::size_t)tr].cargo.push_back(p);
    g.world.units()[(std::size_t)p].cargo_of = tr;
    REQUIRE_FALSE(g.world.units()[(std::size_t)tr].cargo.empty());
    ra2engine::UnitCombat helper = threat_gi(5);
    helper.range = 1.0;
    helper.is_infantry = false;
    helper.is_vehicle = true;
    const int h = g.add(9, 1, helper);
    REQUIRE(h >= 0);
    g.world.call_for_help(1, -1, bi, a);
    CHECK(g.helped(h, a));
    CHECK(g.helped(tr, a));
}

// Rallypoint 0057 [RE: FUN_00708080 leftovers]. These call call_for_help
// directly (no world step) so the RNG and the hit log stay out of the way.
namespace {
ra2engine::UnitCombat inf_gi(int threat) {
    ra2engine::UnitCombat u = threat_gi(threat);
    u.range = 1.0;
    u.is_infantry = true;
    return u;
}
ra2engine::UnitCombat vehicle_gi(int threat) {
    ra2engine::UnitCombat u = threat_gi(threat);
    u.range = 1.0;
    u.is_infantry = false;
    u.is_vehicle = true;
    return u;
}
int add_house1_building(HelpRig& g) {
    ra2engine::BuildingTypeDef bt;
    bt.id = "GACNST";
    bt.strength = 100000;
    bt.fw = bt.fh = 2;
    g.world.set_building_types({bt});
    int bx = 0, by = 0;
    if (!ring_cell(g.world, g.vx, g.vy, 0, bx, by)) return -1;
    return g.world.add_building(0, bx, by, 2, 2, 1, 100000, 0, 0.0, true);
}
}  // namespace

TEST_CASE("0054: no vehicle answers a slave's call (victim +0x2DC SlaveOwner, @0x7085AD)") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("skipped: real map assets not found"); return; }
    HelpRig g;
    REQUIRE(g.ok);
    const int v = g.add(0, 1, protect_victim(true));
    ra2engine::UnitCombat atk = threat_gi(10);
    atk.strength = 100000;
    const int a = g.add(3, 0, atk);
    ra2engine::UnitCombat inf = inf_gi(5);
    const int i = g.add(6, 1, inf);
    const int t = g.add(8, 1, vehicle_gi(5));
    REQUIRE((v >= 0 && a >= 0 && i >= 0 && t >= 0));
    g.world.units()[(std::size_t)v].is_slave = true;
    g.world.units()[(std::size_t)v].slave_master = 0;
    g.world.call_for_help(1, v, -1, a);
    CHECK(g.helped(i, a));
    CHECK_FALSE(g.helped(t, a));
    // A free victim gets the vehicle too.
    g.world.units()[(std::size_t)v].is_slave = false;
    g.world.units()[(std::size_t)v].slave_master = -1;
    g.world.call_for_help(1, v, -1, a);
    CHECK(g.helped(t, a));
}

TEST_CASE("0054: the x100 infantry boost needs victim ArchiveTarget == victim; a building never has it") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("skipped: real map assets not found"); return; }
    HelpRig g;
    REQUIRE(g.ok);
    const int bi = add_house1_building(g);
    const int v = g.add(0, 1, protect_victim(true));
    ra2engine::UnitCombat atk = threat_gi(10);   // budget 10 x 3 = 30
    atk.strength = 100000;
    const int a = g.add(3, 0, atk);
    ra2engine::UnitCombat busy = inf_gi(1);   // already on the attacker: help_value -1
    const int d = g.add(5, 1, busy);
    ra2engine::UnitCombat inf = inf_gi(5);
    const int h = g.add(7, 1, inf);
    REQUIRE((bi >= 0 && v >= 0 && a >= 0 && d >= 0 && h >= 0));
    REQUIRE(d < h);   // the infantry array is walked in index order
    auto reset = [&] {
        g.world.units()[(std::size_t)d].target = a;
        g.world.units()[(std::size_t)h].target = -1;
    };
    // A building victim: budget 30 - 1 -> the helper goes.
    reset();
    g.world.call_for_help(1, -1, bi, a);
    CHECK(g.helped(h, a));
    // A unit victim without ArchiveTarget == self: the same.
    reset();
    g.world.call_for_help(1, v, -1, a);
    CHECK(g.helped(h, a));
    // ArchiveTarget == self: the busy unit costs 100 and nobody goes.
    reset();
    g.world.units()[(std::size_t)v].tg.archive_self = true;
    g.world.call_for_help(1, v, -1, a);
    CHECK_FALSE(g.helped(h, a));
}

TEST_CASE("0054: recruits guard around the victim (ArchiveTarget = victim, @0x70871F)") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("skipped: real map assets not found"); return; }
    HelpRig g;
    REQUIRE(g.ok);
    const int bi = add_house1_building(g);
    ra2engine::UnitCombat atk = threat_gi(10);
    atk.strength = 100000;
    const int a = g.add(3, 0, atk);
    std::vector<int> hs;
    for (int k = 0; k < 6; ++k) {
        ra2engine::UnitCombat inf = inf_gi(1);
        hs.push_back(g.add(6, 1, inf));
        REQUIRE(hs.back() >= 0);
    }
    REQUIRE((bi >= 0 && a >= 0));
    const ra2engine::Building& B = g.world.buildings()[(std::size_t)bi];
    int guards = 0;
    for (int round = 0; round < 20 && guards == 0; ++round) {   // Area_Guard on roll(0,99) >= 66
        for (int h : hs) {
            g.world.units()[(std::size_t)h].target = -1;
            g.world.units()[(std::size_t)h].tg.area_guard = false;
        }
        g.world.call_for_help(1, -1, bi, a);
        for (int h : hs) {
            const ra2engine::Unit& u = g.world.units()[(std::size_t)h];
            CHECK(u.target == a);
            CHECK_FALSE(u.tg.archive_self);
            if (u.tg.area_guard) {
                ++guards;
                CHECK(u.tg.home_x == B.cx + B.fw / 2);
                CHECK(u.tg.home_y == B.cy + B.fh / 2);
            }
        }
    }
    CHECK(guards > 0);
}

TEST_CASE("0054: an armed victim that answers its own call archives itself; a move clears it") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("skipped: real map assets not found"); return; }
    HelpRig g;
    REQUIRE(g.ok);
    ra2engine::UnitCombat vic = threat_gi(5);
    vic.tg.to_protect = true;
    vic.strength = 100000;
    const int v = g.add(0, 1, vic);
    ra2engine::UnitCombat atk = threat_gi(10);
    atk.strength = 100000;
    const int a = g.add(2, 0, atk);
    REQUIRE((v >= 0 && a >= 0));
    g.world.call_for_help(1, v, -1, a);
    CHECK(g.helped(v, a));
    CHECK(g.world.units()[(std::size_t)v].tg.archive_self);
    int mx = 0, my = 0;
    REQUIRE(ring_cell(g.world, g.vx, g.vy, 4, mx, my));
    g.world.order_unit_move(v, mx, my);
    CHECK_FALSE(g.world.units()[(std::size_t)v].tg.archive_self);
}

TEST_CASE("0054: the zone is the victim's under the helper's movement rules (FUN_0056D100)") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("skipped: real map assets not found"); return; }
    HelpRig g;
    REQUIRE(g.ok);
    const int v = g.add(0, 1, protect_victim(true));
    ra2engine::UnitCombat atk = threat_gi(10);
    atk.strength = 100000;
    const int a = g.add(3, 0, atk);
    ra2engine::UnitCombat inf = inf_gi(5);
    const int land = g.add(6, 1, inf);
    const int boat = g.add(8, 1, inf);
    REQUIRE((v >= 0 && a >= 0 && land >= 0 && boat >= 0));
    // The attacker's domain does not matter (the 0044 stand-in compared it).
    g.world.units()[(std::size_t)a].movement_domain = ra2engine::MovementDomain::Water;
    // A Water mover on dry land is not in the victim's water zone.
    g.world.units()[(std::size_t)boat].movement_domain = ra2engine::MovementDomain::Water;
    g.world.call_for_help(1, v, -1, a);
    CHECK(g.helped(land, a));
    CHECK_FALSE(g.helped(boat, a));
}
