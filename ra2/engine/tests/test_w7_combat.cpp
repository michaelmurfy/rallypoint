// Wave 7 shared combat (w7fix-combat): the mission model and the cross-cutting
// combat fixes of docs/gap/wave7/07-shared-combat.md and its cross-references.
#include "doctest.h"

#include <cmath>

#include "asset_fixture.hpp"
#include "combat.hpp"
#include "occupancy.hpp"
#include "ra2assets/map.hpp"
#include "weapon_unit.hpp"  // fv7c range_infinite
#include "world.hpp"

using namespace ra2engine;

namespace {
World::CombatW7Rules armed_rules() {
    World::CombatW7Rules r;
    r.armed = true;
    return r;
}

Unit soldier(int x, int house, Occupancy& occ, int idx, int damage = 15) {
    Unit u;
    u.cell_x = x;
    u.fx = x;
    u.house = house;
    u.hp = u.max_hp = 125;
    u.w_verses = std::vector<int>(11, 100);
    u.w_damage = damage;
    u.w_rof = 20;
    u.w_range = 4.0;
    u.vet.base.range = 4.0;
    u.vet.base.damage = damage;
    u.vet.base.verses = u.w_verses;
    u.sight = 6;
    occ.set(x, 0, idx);
    return u;
}

struct Rig {
    ra2assets::MapFile map;
    World w{map};
    Occupancy occ{60, 1};
    std::vector<Unit> units;
    std::vector<Building> bs;
    void step(int frames = 1) {
        for (int f = 0; f < frames; ++f) step_combat(w, units, bs, occ, 1.0 / 15.0);
    }
};
}  // namespace

TEST_CASE("w7 mission control table (rulesmd [Sleep]..[Selling], FUN_005B3430)") {
    CHECK(World::mission_control(0).zombie);
    CHECK_FALSE(World::mission_control(0).retaliate);
    CHECK_FALSE(World::mission_control(0).scatter);
    CHECK(World::mission_control(23).no_threat);
    CHECK_FALSE(World::mission_control(23).retaliate);
    CHECK(World::mission_control(23).scatter);
    CHECK(World::mission_control(6).paralyzed);
    CHECK_FALSE(World::mission_control(6).scatter);
    CHECK_FALSE(World::mission_control(1).scatter);
    CHECK(World::mission_control(1).retaliate);
    CHECK_FALSE(World::mission_control(10).retaliate);
    CHECK(World::mission_control(19).no_threat);
    CHECK(World::mission_control(5).retaliate);
    CHECK(World::mission_control(11).scatter);
}

TEST_CASE("w7 force-fire: an explicit order on an own unit is kept and fired on (GetFireError has no house test)") {
    Rig r;
    r.w.arm_combat_w7(armed_rules());
    r.units.push_back(soldier(0, 0, r.occ, 0));
    r.units.push_back(soldier(3, 0, r.occ, 1, 0));   // own, in range
    r.units[0].target = 1;
    r.units[0].attack_order = true;
    r.step(2);
    CHECK(r.units[0].target == 1);
    CHECK(r.units[1].hp < 125);
    // Legacy (unarmed) World: valid_target's is_enemy drops it.
    Rig l;
    l.units.push_back(soldier(0, 0, l.occ, 0));
    l.units.push_back(soldier(3, 0, l.occ, 1, 0));
    l.units[0].target = 1;
    l.units[0].attack_order = true;
    l.step(1);
    CHECK(l.units[0].target == -1);
}

TEST_CASE("w7 1% Verses: no passive acquire at <= 2%, acquire at 3% (Evaluate_Object 0x6F7270)") {
    for (int pct : {1, 2, 3}) {
        Rig r;
        r.w.arm_combat_w7(armed_rules());
        r.units.push_back(soldier(0, 0, r.occ, 0));
        r.units.push_back(soldier(3, 1, r.occ, 1, 0));
        r.units[0].w_verses = std::vector<int>(11, pct);
        r.units[0].vet.base.verses = r.units[0].w_verses;
        r.step(1);
        CHECK((r.units[0].target == 1) == (pct > 2));
    }
}

TEST_CASE("w7 threat scoring: the greatest threat wins over the nearest (FUN_0070C190)") {
    Rig r;
    r.w.arm_combat_w7(armed_rules());
    r.units.push_back(soldier(0, 0, r.occ, 0));
    r.units.push_back(soldier(2, 1, r.occ, 1, 0));   // nearer, unarmed, full health
    r.units.push_back(soldier(4, 1, r.occ, 2));      // armed (threatens me), wounded
    r.units[2].hp = 20;
    // Both in the radius/2 band? radius = 5 cells -> band 2.5 cells: only unit
    // 1 is inside it, so the early band exit keeps the nearer one.
    r.step(1);
    CHECK(r.units[0].target == 1);
    // With both in the same band the higher threat wins: unit 2 is armed
    // against my armor (TgtEff) and weaker (TgtStrength -200 x ratio).
    Rig q;
    q.w.arm_combat_w7(armed_rules());
    q.units.push_back(soldier(0, 0, q.occ, 0));
    q.units.push_back(soldier(4, 1, q.occ, 1, 0));
    q.units.push_back(soldier(5, 1, q.occ, 2));
    q.units[2].hp = 20;
    q.step(1);
    CHECK(q.units[0].target == 2);
    CHECK(threat_value_unit(q.w, q.units[0], 0, q.units[2]) > threat_value_unit(q.w, q.units[0], 0, q.units[1]));
}

TEST_CASE("w7 scan cadence: NormalTargetingDelay between scans (0x708CA0)") {
    Rig r;
    r.w.arm_combat_w7(armed_rules());
    r.units.push_back(soldier(0, 0, r.occ, 0));
    r.step(1);   // nothing in range: the scan ran, the timer is armed
    CHECK(r.units[0].tg.scan_timer == 27);
    r.units.push_back(soldier(3, 1, r.occ, 1, 0));
    r.step(5);
    CHECK(r.units[0].target == -1);   // not before the next tick
    r.step(25);
    CHECK(r.units[0].target == 1);
}

TEST_CASE("w7 OpportunityFire: a moving unit acquires and fires without leaving its path (0x70885F)") {
    for (bool opp : {false, true}) {
        Rig r;
        r.w.arm_combat_w7(armed_rules());
        r.units.push_back(soldier(0, 0, r.occ, 0));
        r.units.push_back(soldier(3, 1, r.occ, 1, 0));
        Unit& u = r.units[0];
        u.tg.t.opportunity_fire = opp;
        u.path = {{0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}};
        u.path_idx = 1;
        r.step(1);
        CHECK((r.units[0].target == 1) == opp);
        CHECK(r.units[0].tg.opportunity == opp);
        CHECK(r.units[0].path.size() == 5);   // still driving
        if (opp) CHECK(r.units[1].hp < 125);
    }
}

TEST_CASE("w7 NoThreat: a Harmless unit is not a legal threat (Evaluate_Object 0x6F7331)") {
    ra2assets::MapFile map;
    World w(map);
    Unit v;
    v.hp = v.max_hp = 100;
    CHECK(w.legal_threat_unit(1, v));
    v.tg.mission = 23;
    CHECK_FALSE(w.legal_threat_unit(1, v));
    v.tg.mission = 0;   // Sleep units stay targetable
    CHECK(w.legal_threat_unit(1, v));
}

TEST_CASE("w7 retaliation gate (CanRetaliateToAttacker 0x707C40)") {
    ra2assets::MapFile map;
    World w(map);
    w.arm_combat_w7(armed_rules());
    UnitCombat gi;
    gi.strength = 125;
    gi.damage = 15;
    gi.range = 4.0;
    gi.rof = 20;
    gi.verses = std::vector<int>(11, 100);
    w.add_combat_unit(5, 5, 0, gi);   // 0: human (house 0)
    w.add_combat_unit(8, 5, 1, gi);   // 1: attacker
    w.add_combat_unit(5, 7, 2, gi);   // 2: computer house 2
    CHECK(w.can_retaliate(w.units()[0], 1, -1));   // idle Guard: yes
    w.units()[0].path = {{5, 5}, {6, 5}};
    w.units()[0].path_idx = 1;
    CHECK_FALSE(w.can_retaliate(w.units()[0], 1, -1));   // a human under Move: no
    w.units()[2].path = {{5, 7}, {6, 7}};
    w.units()[2].path_idx = 1;
    CHECK(w.can_retaliate(w.units()[2], 1, -1));   // a computer under Move: yes
    w.units()[2].tg.mission = 0;
    CHECK_FALSE(w.can_retaliate(w.units()[2], 1, -1));   // Sleep: Retaliate=no
    w.units()[2].tg.mission = -1;
    w.units()[2].w_verses = std::vector<int>(11, 1);
    CHECK_FALSE(w.can_retaliate(w.units()[2], 1, -1));   // Verses 1%: no
    w.units()[2].w_verses = std::vector<int>(11, 100);
    CHECK_FALSE(w.can_retaliate(w.units()[2], 2, -1));   // own house: allied
}

TEST_CASE("w7 Override_Mission: a computer unit retaliates while moving and resumes its move") {
    ra2assets::MapFile map;
    World w(map);
    w.arm_combat_w7(armed_rules());
    UnitCombat gi;
    gi.strength = 125;
    gi.damage = 15;
    gi.range = 4.0;
    gi.rof = 20;
    gi.verses = std::vector<int>(11, 100);
    w.add_combat_unit(5, 5, 2, gi);
    w.add_combat_unit(8, 5, 1, gi);
    Unit& v = w.units()[0];
    v.path = {{5, 5}, {5, 6}, {5, 7}, {5, 8}};
    v.path_idx = 1;
    CHECK(w.can_retaliate(v, 1, -1));
    w.w7_override_attack(0, 1, -1);
    CHECK(w.units()[0].target == 1);
    CHECK(w.units()[0].attack_order);
    CHECK(w.units()[0].tg.overridden);
    CHECK(w.units()[0].tg.resume_x == 5);
    CHECK(w.units()[0].tg.resume_y == 8);
}

TEST_CASE("w7 Enter_Idle_Mode: computer and DefaultToGuardArea units idle in Area Guard (0x737B90)") {
    ra2assets::MapFile map;
    World w(map);
    w.arm_combat_w7(armed_rules());
    UnitCombat gi;
    gi.strength = 125;
    gi.damage = 15;
    gi.range = 4.0;
    gi.verses = std::vector<int>(11, 100);
    w.add_combat_unit(5, 5, 0, gi);   // human GI
    w.add_combat_unit(7, 5, 2, gi);   // computer GI
    UnitCombat dog = gi;
    dog.tg.default_to_guard_area = true;
    w.add_combat_unit(9, 5, 0, dog);   // human dog
    for (int i = 0; i < 3; ++i) w.enter_idle_mode(i);
    CHECK_FALSE(w.units()[0].tg.area_guard);
    CHECK(w.units()[1].tg.area_guard);
    CHECK(w.units()[2].tg.area_guard);
    CHECK(w.units()[2].tg.home_x == 9);
    CHECK(w.house_iq(0) == 0);
    CHECK(w.house_iq(2) == 5);
    w.set_house_iq(2, 1);   // a campaign IQ=1 house stays in Guard
    w.units()[1].tg.area_guard = false;
    w.enter_idle_mode(1);
    CHECK_FALSE(w.units()[1].tg.area_guard);
}

TEST_CASE("w7 area guard leash: 13..22 cells from home, drop beyond (FUN_004D41A0)") {
    Rig r;
    r.w.arm_combat_w7(armed_rules());
    r.units.push_back(soldier(0, 0, r.occ, 0));
    r.units.push_back(soldier(30, 1, r.occ, 1, 0));
    Unit& u = r.units[0];
    u.tg.area_guard = true;
    u.tg.home_x = 0;
    u.tg.home_y = 0;
    u.target = 1;   // range 4 -> 2x4x256 = 2048 leptons -> 8 + 6 = 14 cells
    u.tg.scan_timer = 10;
    r.step(1);
    CHECK(r.units[0].target == -1);
    r.units[1].cell_x = 12;
    r.units[1].fx = 12;
    r.units[0].target = 1;
    r.units[0].tg.scan_timer = 10;
    r.step(1);
    CHECK(r.units[0].target == 1);
}

TEST_CASE("w7 crush: an ally is never squashed, an enemy is warned and squashed on arrival (0x5F64C0, 0x7404B0)") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("no install"); return; }
    for (int victim_house : {1, 2}) {
        World w(ra2fixture::mapsmd03_map());
        w.enable_subcells();
        World::CrushRules cr;
        cr.armed = true;
        w.arm_crush(cr);
        w.arm_combat_w7(armed_rules());
        World::HouseRec h0, h1, h2;
        h0.alliance_team = 0;
        h1.alliance_team = 1;
        h2.side = World::Side::Neutral;
        h2.combatant = false;
        h2.alliance_team = 102;
        w.set_house(0, h0);
        w.set_house(1, h1);
        w.set_house(2, h2);
        w.set_ally_view(0, 2, true);   // house 0 lists the civilians as allies
        w.set_house_iq(1, 0);          // the enemy infantry does not dodge
        int x0 = -1, y0 = -1;
        for (int y = 20; y < w.grid_h() - 20 && x0 < 0; ++y)
            for (int x = 20; x < w.grid_w() - 20 && x0 < 0; ++x)
                if (w.walkable(x, y) && w.walkable(x + 1, y) && w.walkable(x + 2, y) && w.walkable(x + 3, y) &&
                    x + y > w.grid_w())
                    { x0 = x; y0 = y; }
        REQUIRE(x0 >= 0);
        UnitCombat tank;
        tank.strength = 300;
        tank.is_vehicle = true;
        tank.speed_cps = 2.0;
        tank.rot = 127;
        tank.crusher = true;
        UnitCombat inf;
        inf.strength = 125;
        inf.crushable = true;
        inf.is_infantry = true;
        w.add_combat_unit(x0, y0, 0, tank);
        w.add_combat_unit(x0 + 1, y0, victim_house, inf);
        CHECK_FALSE(w.allied_with(0, 1));
        CHECK(w.allied_with(0, 2));
        CHECK_FALSE(w.are_allied(0, 2));   // the combat alliance is unchanged
        w.order_unit_move(0, x0 + 3, y0);
        for (int f = 0; f < 90; ++f) w.step(1.0 / 15.0);
        if (victim_house == 1) CHECK(w.units()[1].die_time >= 0.0);
        else CHECK(w.units()[1].die_time < 0.0);
    }
}

TEST_CASE("w7 scan: an ability weapon (the dog's ParasiteDog jaw) still acquires (m-sov09 dogs)") {
    Rig r;
    r.w.arm_combat_w7(armed_rules());
    r.units.push_back(soldier(0, 0, r.occ, 0));
    r.units.push_back(soldier(1, 1, r.occ, 1, 0));
    r.units[0].abil.fire_kind = AbilityKind::Parasite;
    r.units[0].w_range = 1.5;
    r.units[0].vet.base.range = 1.5;
    r.units[0].tg.area_guard = true;
    r.step(1);
    CHECK(r.units[0].target == 1);
}

TEST_CASE("w7r1 cargo: an infantry created inside a transport holds no sub-cell spot (ALL04 phantom GI)") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("no install"); return; }
    World w(ra2fixture::mapsmd03_map());
    w.enable_subcells();
    int x0 = -1, y0 = -1;
    for (int y = 20; y < w.grid_h() - 20 && x0 < 0; ++y)
        for (int x = 20; x < w.grid_w() - 20 && x0 < 0; ++x)
            if (w.walkable(x, y) && w.walkable(x + 1, y) && w.walkable(x + 2, y)) { x0 = x; y0 = y; }
    REQUIRE(x0 >= 0);
    UnitCombat craft;
    craft.strength = 300;
    craft.is_vehicle = true;
    craft.speed_cps = 2.0;
    craft.rot = 127;
    w.add_combat_unit(x0, y0, 0, craft);
    w.units()[0].passengers_max = 5;
    UnitCombat gi;
    gi.strength = 125;
    gi.is_infantry = true;
    const int r1 = w.add_unit_in_transport(0, 0, gi);
    const int r2 = w.add_unit_in_transport(0, 0, gi);
    REQUIRE(r1 >= 0);
    REQUIRE(r2 >= 0);
    const Occupancy& o = w.occupancy();
    CHECK(o.at(x0, y0) == 0);                 // the transport still owns its cell
    CHECK(o.spot_of(x0, y0, r1) < 0);         // the riders hold no spot
    CHECK(o.spot_of(x0, y0, r2) < 0);
    CHECK(o.infantry_count(x0, y0) == 0);
    w.order_unit_move(0, x0 + 2, y0);         // the transport drives off
    for (int f = 0; f < 60; ++f) w.step(1.0 / 15.0);
    REQUIRE(w.units()[0].cell_x != x0);
    CHECK(o.at(x0, y0) == Occupancy::kEmpty);   // nothing left behind
}

// fv7c (13a #2) [RE: FUN_006F8320 0x6F8520.. radius (max(GetWeaponRange(1),
// GetWeaponRange(0)) >> 8) + 1 on the RAW ranges; FUN_006F6750 head `Range ==
// -0x200 -> in range`]: ASWLauncher Range=-2 no longer stands in as 30 cells.
TEST_CASE("fv7c: a Range=-2 secondary does not widen the threat scan, and is always in range") {
    Rig r;
    r.w.arm_combat_w7(armed_rules());
    r.units.push_back(soldier(0, 0, r.occ, 0));
    r.units[0].w2.valid = true;
    r.units[0].w2.range = -2.0;                       // [ASWLauncher] Range=-2
    r.units[0].w2.verses = std::vector<int>(11, 100);
    r.units[0].w2.ag = true;
    r.units.push_back(soldier(12, 1, r.occ, 1, 0));   // 12 cells: outside 4+1, inside the old 31
    r.step(30);
    CHECK(r.units[0].target == -1);
    Unit asw = r.units[0];
    asw.w_range = -2.0;                               // the ASW slot active
    CHECK(weapon_in_range(r.w, asw, 25 * 256 + 128, 128, 0));
    CHECK(range_infinite(-2.0));
    CHECK_FALSE(range_infinite(-1.0));
}
