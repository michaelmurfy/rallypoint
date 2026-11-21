// Gap fix C (combat / units / movement) -- unit tests for the pure pieces and
// the real-rulesmd resolution. The real-play proof (worlds built through
// launch_skirmish + the GameScene wiring) is `ra2engine --check-gap-combat`.
#include "doctest.h"

#include <cmath>

#include "asset_fixture.hpp"
#include "combat.hpp"
#include "weapon.hpp"
#include "weapon_resolve.hpp"
#include "weapon_unit.hpp"
#include "world.hpp"

using namespace ra2engine;

namespace {
WeaponSlot slot(bool aa, bool ag, std::vector<int> verses = {}) {
    WeaponSlot s;
    s.valid = true;
    s.aa = aa;
    s.ag = ag;
    s.verses = std::move(verses);
    return s;
}
}  // namespace

TEST_CASE("gapC weapon: choose_weapon = primary if it can engage, else secondary (G-41/G-44)") {
    const WeaponSlot ground = slot(false, true);
    const WeaponSlot aa = slot(true, false);
    CHECK(choose_weapon(ground, aa, /*air=*/false, 0) == 0);
    CHECK(choose_weapon(ground, aa, /*air=*/true, 0) == 1);
    CHECK(choose_weapon(ground, WeaponSlot{}, /*air=*/true, 0) == -1);
    // 0 % Verses forbids the shot (GetFireError, combat.md §2).
    const WeaponSlot no_vs_heavy = slot(false, true, {100, 100, 100, 100, 100, 0});
    CHECK(choose_weapon(no_vs_heavy, WeaponSlot{}, false, 5) == -1);
    CHECK(choose_weapon(no_vs_heavy, ground, false, 5) == 1);
}

TEST_CASE("gapC weapon: real rulesmd slots (SREF Weapon1, APOC/HTK secondary AA, FV no secondary)") {
    if (!ra2fixture::assets_available()) { MESSAGE("no install"); return; }
    const ra2assets::Rules& r = ra2fixture::rulesmd();
    const TypeWeapons sref = resolve_type_weapons(r, "SREF");
    CHECK(sref.slot[0].id == "Comet");
    CHECK(sref.slot[0].damage > 0);
    CHECK(sref.slot[0].range > 0.0);
    CHECK(sref.elite[0].id == "SuperComet");
    CHECK_FALSE(sref.slot[1].valid);
    const TypeWeapons htk = resolve_type_weapons(r, "HTK");
    CHECK(htk.slot[0].id == "FlakTrackGun");
    CHECK(htk.slot[1].id == "FlakTrackAAGun");
    CHECK(htk.slot[1].aa);
    CHECK_FALSE(htk.slot[0].aa);
    const TypeWeapons apoc = resolve_type_weapons(r, "APOC");
    CHECK(apoc.slot[1].id == "MammothTusk");
    CHECK(apoc.slot[1].aa);
    const TypeWeapons fv = resolve_type_weapons(r, "FV");
    CHECK(fv.slot[0].id == "HoverMissile");
    CHECK_FALSE(fv.slot[1].valid);   // Weapon2=RepairBullet is an IFV mode, not a secondary
    const TypeWeapons gi = resolve_type_weapons(r, "E1");
    CHECK(gi.slot[0].id == "M60");
    CHECK(gi.slot[1].id == "Para");
    CHECK(gi.elite[1].id == "ParaE");
    const TypeWeapons ggi = resolve_type_weapons(r, "GGI");
    CHECK(ggi.slot[1].id == "MissileLauncher");   // ";GEF New Guardian GI weapon" cut (RC-5)
    CHECK(ggi.slot[1].aa);
    UnitCombat c;
    fill_unit_combat_extras(r, "E1", gi, c);
    CHECK(c.deploy_fire);
    CHECK(c.deploy_fire_slot == 1);
    CHECK(c.crushable);
    UnitCombat d;
    fill_unit_combat_extras(r, "DESO", resolve_type_weapons(r, "DESO"), d);
    CHECK_FALSE(d.deploy_fire);   // RadEruptionWeapon has RadLevel -> P4 eruption
    UnitCombat m;
    fill_unit_combat_extras(r, "MTNK", resolve_type_weapons(r, "MTNK"), m);
    CHECK(m.crusher);
}

TEST_CASE("gapC weapon: a Flak Track engages an airborne Kirov with its AA secondary") {
    ra2assets::MapFile map;
    World w(map);
    UnitCombat flak;
    flak.strength = 300;
    flak.damage = 25;
    flak.rof = 25;
    flak.range = 5.0;
    flak.secondary = slot(true, false);
    flak.secondary.damage = 30;
    flak.secondary.rof = 20;
    flak.secondary.range = 8.0;
    Unit& f = w.add_combat_unit(2, 2, 0, flak);
    f.vet.base.range = 5.0;
    UnitCombat zep;
    zep.strength = 2000;
    zep.is_aircraft = true;
    Unit& k = w.add_combat_unit(8, 2, 1, zep);
    k.altitude = 500.0;
    k.flight_state = FlightState::Cruising;
    Unit& fu = w.units()[0];
    const Unit& ku = w.units()[1];
    CHECK(unit_weapon_for_unit(fu, ku) == 1);
    activate_weapon_slot(fu, 1, nullptr);
    CHECK(fu.w_range == 8.0);
    CHECK(fu.w_damage == 30);
    activate_weapon_slot(fu, 0, nullptr);
    CHECK(fu.w_range == 5.0);
    CHECK(fu.w_damage == 25);
}

TEST_CASE("gapC deploy: a GI deploys into a firing post with Para, not a frozen eruption (G-45)") {
    ra2assets::MapFile map;
    World w(map);
    UnitCombat gi;
    gi.strength = 125;
    gi.damage = 15;
    gi.rof = 20;
    gi.range = 4.0;
    gi.secondary = slot(false, true);
    gi.secondary.damage = 25;
    gi.secondary.rof = 15;
    gi.secondary.range = 5.0;
    gi.deploy_fire = true;
    gi.deploy_fire_slot = 1;
    w.add_combat_unit(2, 2, 0, gi);
    UnitCombat e2;
    e2.strength = 125;
    w.add_combat_unit(5, 2, 1, e2);
    CHECK(unit_weapon_for_unit(w.units()[0], w.units()[1]) == 0);
    REQUIRE(w.order_deploy(0));
    CHECK(w.units()[0].deployed);
    CHECK_FALSE(w.units()[0].frozen);
    CHECK(unit_weapon_for_unit(w.units()[0], w.units()[1]) == 1);
    // It keeps fighting from the post: step until the Para shot lands.
    for (int f = 0; f < 30; ++f) w.step(1.0 / 15.0);
    CHECK(w.units()[1].hp < 125);
    CHECK(w.units()[0].w_active == 1);
    CHECK(w.units()[0].cell_x == 2);
    // W7-01-05 [RE: 0x51F510 / 0x51F370]: a human's deployed GI refuses a
    // move order (it stays dug in).
    w.order_unit_move(0, 2, 4);
    CHECK(w.units()[0].deployed);
    CHECK_FALSE(w.units()[0].moving());
}

TEST_CASE("gapC defenses: a pillbox acquires, turns and kills; Powered goes offline at low power (G-40)") {
    ra2assets::MapFile map;
    World w(map);
    BuildingTypeDef pill;
    pill.id = "GAPILL";
    pill.strength = 400;
    pill.weapon[0] = slot(false, true);
    pill.weapon[0].damage = 20;
    pill.weapon[0].rof = 10;
    pill.weapon[0].range = 5.0;
    pill.armed = true;
    pill.turret = true;
    pill.rot = 10;
    BuildingTypeDef sam = pill;
    sam.id = "NASAM";
    sam.powered = true;
    sam.power = -50;
    sam.turret = false;
    w.set_building_types({pill, sam});
    w.add_building(0, 2, 2, 1, 1, 0, 400, 0, 0.0, /*instant=*/true);
    UnitCombat e2;
    e2.strength = 125;
    w.add_combat_unit(5, 2, 1, e2);
    for (int f = 0; f < 120; ++f) w.step(1.0 / 15.0);
    CHECK(w.units()[0].hp <= 0);
    bool fired = false;
    for (const auto& ev : w.take_fire_events()) fired = fired || ev.building == 0;
    CHECK(fired);

    // Powered defense with no power plant: drain 50 > output 0 -> offline.
    World w2(map);
    w2.set_building_types({pill, sam});
    w2.add_building(1, 2, 2, 1, 1, 0, 400, 0, 0.0, true);
    w2.add_combat_unit(4, 2, 1, e2);
    for (int f = 0; f < 60; ++f) w2.step(1.0 / 15.0);
    CHECK(w2.units()[0].hp == 125);
    CHECK(w2.buildings()[0].def_offline);
    CHECK_FALSE(w2.defense_power_ok(0));
}

TEST_CASE("gapC passability: water / cliff / wall / beach cells close the Land grid (G-43; fv7c Beach)") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("no install"); return; }
    World w(ra2fixture::mapsmd03_map());
    const int gw = w.grid_w(), gh = w.grid_h();
    REQUIRE(gw > 0);
    // Pick four materialized land cells.
    std::vector<int> picks;
    for (int i = 0; i < gw * gh && picks.size() < 4; ++i)
        if (w.passable()[i]) picks.push_back(i);
    REQUIRE(picks.size() == 4);
    std::vector<ra2assets::LandType> lt(static_cast<std::size_t>(gw) * gh, ra2assets::LandType::Clear);
    lt[picks[0]] = ra2assets::LandType::Water;
    lt[picks[1]] = ra2assets::LandType::Rock;
    lt[picks[2]] = ra2assets::LandType::Wall;
    lt[picks[3]] = ra2assets::LandType::Beach;
    w.set_land_terrain(lt);
    CHECK_FALSE(w.walkable(picks[0] % gw, picks[0] / gw));
    CHECK_FALSE(w.walkable(picks[1] % gw, picks[1] / gw));
    CHECK_FALSE(w.walkable(picks[2] % gw, picks[2] / gw));
    // fv7c (13a #1): the LandType is per sub-tile now, so Beach is blocked for
    // Foot/Track/Wheel as rulesmd [Beach] says -- and open to the amphibious grid.
    CHECK_FALSE(w.walkable(picks[3] % gw, picks[3] / gw));
    CHECK(w.amphib_grid()[picks[3]] == 1);
    CHECK(w.grid_for(MovementDomain::Water)[picks[3]] == 0);
    CHECK(w.grid_for(MovementDomain::Water)[picks[0]] == 1);   // ships still sail it
}

TEST_CASE("gapC G-50: min-1 damage after Verses, 0 % stays 0; ROF rearm jitter is Random(0,2) frames") {
    const std::vector<int> v = {100, 80, 80, 50, 25, 25, 75, 50, 25, 100, 100};
    CHECK(damage_vs(3, v, 4) == 1);     // 3 x 25 % = 0.75 -> 1 (ReceiveDamage clamp)
    CHECK(damage_vs(15, v, 4) == 3);
    CHECK(damage_vs(0, v, 4) == 0);
    const std::vector<int> z = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    CHECK(damage_vs(50, z, 3) == 0);
    ProjRng rng;
    int seen[3] = {0, 0, 0};
    for (int i = 0; i < 300; ++i) {
        const int extra = rearm_frames_for(20, &rng) - 20;   // Net L0: whole frames
        REQUIRE(extra >= 0);
        REQUIRE(extra <= 2);
        ++seen[extra];
    }
    CHECK(seen[0] > 0);
    CHECK(seen[1] > 0);
    CHECK(seen[2] > 0);
    CHECK(rearm_frames_for(20, nullptr) == 20);
}

TEST_CASE("gapC crush + stop/scatter on the real map (G-47)") {
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("no install"); return; }
    World w(ra2fixture::mapsmd03_map());
    const ra2assets::MapCell* c = pick_start_cell(ra2fixture::mapsmd03_map());
    REQUIRE(c != nullptr);
    // A free straight 1x6 corridor along +x from the start cell.
    int x0 = c->x, y0 = c->y;
    UnitCombat tank;
    tank.strength = 300;
    tank.is_vehicle = true;
    tank.speed_cps = 2.0;
    tank.rot = 127;
    tank.crusher = true;
    UnitCombat inf;
    inf.strength = 125;
    inf.crushable = true;
    REQUIRE(w.walkable(x0, y0));
    REQUIRE(w.walkable(x0 + 1, y0));
    REQUIRE(w.walkable(x0 + 2, y0));
    w.add_combat_unit(x0, y0, 0, tank);
    w.add_combat_unit(x0 + 1, y0, 1, inf);
    w.order_unit_move(0, x0 + 2, y0);
    for (int f = 0; f < 60; ++f) w.step(1.0 / 15.0);
    CHECK(w.units()[1].die_time >= 0.0);   // squashed
    CHECK(w.units()[0].cell_x == x0 + 2);

    // Stop: the tank halts at the cell it is entering.
    w.order_unit_move(0, x0 + 2, y0 + 6);
    w.step(1.0 / 15.0);
    w.order_stop(0);
    for (int f = 0; f < 30; ++f) w.step(1.0 / 15.0);
    CHECK_FALSE(w.units()[0].moving());
    CHECK(std::abs(w.units()[0].cell_y - y0) <= 1);
    // Scatter: steps to an adjacent free cell.
    const int sx = w.units()[0].cell_x, sy = w.units()[0].cell_y;
    w.order_scatter(0);
    for (int f = 0; f < 30; ++f) w.step(1.0 / 15.0);
    CHECK((w.units()[0].cell_x != sx || w.units()[0].cell_y != sy));
}

TEST_CASE("last-fix: an owner change drops target and move and re-plants the guard post (SetOwningHouse FUN_00700940)") {
    ra2assets::MapFile map;
    World w(map);
    UnitCombat ifv;
    ifv.strength = 200;
    ifv.is_vehicle = true;
    w.add_combat_unit(5, 5, 1, ifv);
    w.add_combat_unit(9, 5, 0, ifv);
    Unit& u = w.units()[0];
    // Walking its old owner's path (standing on a cell), chasing a target,
    // with a guard post far away.
    u.target = 1;
    u.attack_order = false;
    u.guard_x = 30;
    u.guard_y = 30;
    u.path = {{5, 5}, {6, 5}, {7, 5}};
    u.path_idx = 1;
    w.set_unit_owner(0, 6);
    const Unit& v = w.units()[0];
    CHECK(v.house == 6);
    CHECK(v.target == -1);
    CHECK(v.target_building == -1);
    CHECK_FALSE(v.moving());
    CHECK(v.guard_x == 5);
    CHECK(v.guard_y == 5);
    // Same owner: a no-op (the guard post set by hand stays).
    w.units()[0].guard_x = 12;
    w.set_unit_owner(0, 6);
    CHECK(w.units()[0].guard_x == 12);
}

TEST_CASE("gapC G-46: an idle unit engages an enemy base defense in range, not a plain building") {
    ra2assets::MapFile map;
    World w(map);
    BuildingTypeDef pill;
    pill.id = "GAPILL";
    pill.strength = 400;
    pill.weapon[0] = slot(false, true);
    pill.weapon[0].damage = 0;     // harmless here: only the unit's reaction is tested
    pill.weapon[0].rof = 10;
    pill.weapon[0].range = 1.0;
    pill.armed = true;
    BuildingTypeDef pow;
    pow.id = "GAPOWR";
    pow.strength = 750;
    w.set_building_types({pill, pow});
    w.add_building(1, 2, 6, 1, 1, 1, 750, 0, 0.0, true);   // plain, in range
    w.add_building(0, 2, 2, 1, 1, 1, 400, 0, 0.0, true);   // defense, in range
    UnitCombat tank;
    tank.strength = 400;
    tank.damage = 65;
    tank.rof = 60;
    tank.range = 5.0;
    w.add_combat_unit(5, 4, 0, tank);
    for (int f = 0; f < 30; ++f) w.step(1.0 / 15.0);
    CHECK(w.buildings()[1].hp < 400);
    CHECK(w.buildings()[0].hp == 750);
}

// Net L0 (docs/gap/net/03-sim-bitexact.md D3): the rearm timer is a whole-frame
// countdown, so a k-frame ROF fires exactly every k ticks -- the old seconds
// timer ((k)/15 minus 1/15 per tick) slipped to k+1 for most k.
TEST_CASE("netL0: a k-frame rearm expires after exactly k decrements, for every k") {
    for (int k = 1; k <= 300; ++k) {
        int t = ra2engine::rearm_frames_for(k, nullptr);
        int ticks = 0;
        while (t > 0) { --t; ++ticks; }
        CHECK(ticks == k);
    }
}

TEST_CASE("w7r3: a Prism Tower / Tesla Coil never engages an airborne Kirov (no AA projectile, FUN_007718B0)") {
    ra2assets::MapFile map;
    for (int kind = 0; kind < 2; ++kind) {   // 0 = Prism (PrismType), 1 = Tesla (Overpowerable)
        World w(map);
        w.arm_w5(RulesW5{}, {});
        BuildingTypeDef tower;
        tower.id = kind == 0 ? "ATESLA" : "TESLA";
        tower.strength = 600;
        tower.weapon[0] = slot(/*aa=*/false, /*ag=*/true);   // PrismShot / CoilBolt: InvisibleHigh / Invisible
        tower.weapon[0].damage = 120;
        tower.weapon[0].rof = 45;
        tower.weapon[0].range = 8.0;
        tower.weapon[1] = tower.weapon[0];                    // PrismSupport / OPCoilBolt
        tower.armed = true;
        if (kind == 0) tower.w5.prism = true;
        else tower.w5.overpowerable = true;
        w.set_building_types({tower});
        w.add_building(0, 2, 2, 1, 1, 0, 600, 0, 0.0, /*instant=*/true);
        UnitCombat zep;
        zep.strength = 2000;
        zep.is_aircraft = true;
        Unit& k = w.add_combat_unit(5, 2, 1, zep);
        k.altitude = 500.0;
        k.flight_state = FlightState::Cruising;
        for (int f = 0; f < 120; ++f) w.step(1.0 / 15.0);
        CHECK(w.units()[0].hp == 2000);
        CHECK(w.buildings()[0].def_target == -1);
        // the same tower takes a ground unit
        UnitCombat htnk;
        htnk.strength = 400;
        Unit& g = w.add_combat_unit(5, 4, 1, htnk);
        for (int f = 0; f < 120; ++f) w.step(1.0 / 15.0);
        CHECK(w.units()[1].hp < 400);
        CHECK(w.units()[0].hp == 2000);
    }
}

TEST_CASE("w7r3: no legal weapon (-1) keeps the active slot -- a dog's NeverUse scanner never loads") {
    ra2assets::MapFile map;
    World w(map);
    UnitCombat dog;
    dog.strength = 100;
    dog.damage = 30;
    dog.rof = 20;
    dog.range = 1.5;
    dog.verses = {100, 100, 100, 0, 0, 0, 0, 0, 0, 0, 0};   // ParasiteDog
    dog.secondary = slot(true, true, {100, 80, 80, 50, 25, 25, 75, 50, 25, 100, 100});   // VirtualScanner / SA
    dog.secondary.never_use = true;
    dog.secondary.range = 5.0;
    w.add_combat_unit(2, 2, 1, dog);
    UnitCombat tank;
    tank.strength = 400;
    tank.armor = "heavy";
    w.add_combat_unit(3, 2, 0, tank);
    Unit& du = w.units()[0];
    const Unit& tu = w.units()[1];
    CHECK(unit_weapon_for_unit(du, tu) == -1);
    const std::vector<int> before = du.w_verses;
    activate_weapon_slot(du, unit_weapon_for_unit(du, tu), nullptr);
    CHECK(du.w_active == 0);
    CHECK(du.w_verses == before);
    CHECK(du.w_verses[5] == 0);
}
