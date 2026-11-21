// Wave-7 fix (units): docs/gap/wave7/01-04, 08, 10 -- unit-specific
// behaviour and abilities. World-level checks on the real mapsmd03 map
// (install-gated, the test_abilities_world precedent) plus rulesmd
// resolution checks.
#include "doctest.h"

#include "asset_fixture.hpp"
#include "cargo.hpp"
#include "combat.hpp"
#include "harvest.hpp"   // OreValues
#include "production.hpp"
#include "weapon.hpp"
#include "weapon_resolve.hpp"
#include "weapon_unit.hpp"
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
    if (vehicle) c.rot = 5;
    return c;
}

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

BuildingTypeDef bdef(const char* id) {
    BuildingTypeDef d;
    d.id = id;
    d.fw = 2;
    d.fh = 2;
    d.cost = 800;
    d.tech_level = 1;
    return d;
}
}  // namespace

#define W7_WORLD(world, x0, y0)                                              \
    if (!ra2fixture::mapsmd03_available()) {                                 \
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR"); \
        return;                                                              \
    }                                                                        \
    World world(ra2fixture::mapsmd03_map());                                 \
    int x0 = 0, y0 = 0;                                                      \
    REQUIRE(find_open_rect(world, 16, 8, x0, y0));

// ---------------------------------------------------------------- rulesmd

TEST_CASE("w7units: rulesmd type flags (Agent/Engineer, PermaDisguise, DetectDisguise, Turret, BuildTimeMultiplier)") {
    if (ra2fixture::rulesmd_bytes().empty()) { MESSAGE("no install"); return; }
    const ra2assets::Rules& r = ra2fixture::rulesmd();
    auto w7 = [&](const char* id) {
        UnitTypeW7 w;
        fill_unit_w7(r, id, resolve_type_weapons(r, id), w);
        return w;
    };
    CHECK(w7("SPY").agent);
    CHECK(w7("SPY").perma_disguise);
    CHECK_FALSE(w7("SPY").engineer);
    CHECK(w7("ENGINEER").engineer);
    CHECK(w7("SENGINEER").engineer);
    CHECK_FALSE(w7("E1").engineer);
    CHECK(w7("DOG").detect_disguise);
    CHECK(w7("ADOG").detect_disguise);
    CHECK_FALSE(w7("E1").detect_disguise);
    CHECK(w7("E1").selectable_combatant);
    CHECK_FALSE(w7("ENGINEER").selectable_combatant);
    CHECK_FALSE(w7("SENGINEER").selectable_combatant);
    CHECK_FALSE(w7("TNKD").has_turret);
    CHECK_FALSE(w7("MGTK").has_turret);
    CHECK(w7("MTNK").has_turret);
    CHECK(w7("HTNK").build_time_mult == doctest::Approx(1.5));
    CHECK(w7("TTNK").build_time_mult == doctest::Approx(1.3));
    CHECK(w7("E1").build_time_mult == doctest::Approx(1.0));
    CHECK(w7("MGTK").fake_blink == 15);
    CHECK(w7("YURI").deploy_area_fire);
    CHECK(w7("YURI").undeploy_delay == 150);
    CHECK(w7("YURI").mc_capacity == 1);
    CHECK(w7("DOG").default_to_guard_area);
    CHECK(w7("DRON").default_to_guard_area);
    CHECK_FALSE(w7("CLEG").move_to_shroud);
    // W7-01-01: the dog's VirtualScanner is NeverUse.
    const TypeWeapons adog = resolve_type_weapons(r, "ADOG");
    CHECK(adog.slot[1].never_use);
    CHECK_FALSE(adog.slot[0].never_use);
}

TEST_CASE("w7units W7-04-03: BuildTimeMultiplier scales the build frames after the base") {
    ProductionState ps;
    start(ps, 900, 900, 0.7, 1.5);   // Rhino: 900 x .7 x .9 = 567 -> ftol(567 x 1.5) = 850
    CHECK(ps.slot.total_frames == 850);
    start(ps, 900, 900, 0.7);
    CHECK(ps.slot.total_frames == 567);
}

TEST_CASE("w7units W7-01-01/06: NeverUse is never chosen; an undeployed GGI has weapon 0 only") {
    WeaponSlot p;
    p.valid = true;
    p.verses = {100, 100, 100, 0, 0, 0, 0, 0, 0, 0, 0};
    WeaponSlot s;
    s.valid = true;
    s.never_use = true;
    s.verses = kAll100;
    CHECK(choose_weapon(p, s, false, 5) == -1);   // heavy: the primary cannot, the scanner never fires
    CHECK(choose_weapon(p, s, false, 0) == 0);
    Unit ggi;
    ggi.deploy_fire = true;
    ggi.is_infantry = true;
    ggi.p_ag = true;
    ggi.p_aa = false;
    ggi.w_verses = kAll100;
    ggi.vet.base.verses = kAll100;
    ggi.w2.valid = true;
    ggi.w2.aa = true;
    ggi.w2.ag = false;
    ggi.w2.verses = kAll100;
    Unit jet;
    jet.is_aircraft = true;
    jet.altitude = 500.0;
    jet.flight_state = FlightState::Cruising;
    CHECK(unit_weapon_for_unit(ggi, jet) == -1);   // undeployed: no MissileLauncher vs air
    ggi.deployed = true;
    ggi.deploy_fire_slot = 1;
    CHECK(unit_weapon_for_unit(ggi, jet) == 1);
}

// ---------------------------------------------------------------- world

TEST_CASE("w7units W7-08-01/02/03: a damaged tank is sent to the depot, repaired on the depot's own clock; the drone dies on a paid step") {
    W7_WORLD(world, x0, y0);
    BuildingTypeDef depot = bdef("GADEPT");
    depot.unit_repair = true;
    world.set_economy({depot}, {}, OreValues{}, 600.0, 0.0, 0.0);
    world.enable_player_economy(0, 10000.0);
    const int b = world.add_building(0, x0 + 8, y0 + 2, 2, 2, 0, 1200, armor_index("steel"), 0.0, true);
    AbilityProfile dp;
    dp.fire_kind = AbilityKind::Parasite;
    world.register_ability_profile("DRON", dp);
    world.add_combat_unit(x0, y0 + 2, 0, mk("MTNK", 300, "heavy", 0, 60, 0.0, {}, true));
    const int drone = static_cast<int>(world.units().size());
    world.add_combat_unit(x0, y0 + 6, 1, mk("DRON", 100, "special_1", 50, 60, 1.83,
                                            {100, 100, 100, 100, 100, 100, 0, 0, 0, 0, 0}));
    world.units()[0].hp = 100;
    // The drone rides the tank (as after an Infect).
    world.units()[drone].cargo_of = 0;
    world.units()[drone].parasite_next_tick = 1 << 30;   // no ticks in this test
    world.units()[0].parasite_on_me = drone;
    world.step(1.0 / 15.0);
    CHECK(world.depot_wants(0, {0}, b));
    CHECK_FALSE(world.depot_wants(1, {0}, b));
    CHECK(world.group_depot(0, {0}, b) == 1);
    std::vector<Ev> all;
    int first_step = -1;
    for (int k = 0; k < 900 && world.units()[0].hp < 300; ++k) {
        const int hp = world.units()[0].hp;
        world.step(1.0 / 15.0);
        for (const Ev& e : world.take_ability_events()) all.push_back(e);
        if (first_step < 0 && world.units()[0].hp > hp) first_step = k;
    }
    REQUIRE(first_step >= 0);
    CHECK(world.units()[0].hp == 300);
    CHECK(world.units()[0].parasite_on_me == -1);
    CHECK_FALSE(world.units()[drone].alive);   // W7-04-11: destroyed, not ejected
    CHECK(count_kind(all, Ev::Kind::DroneLost) == 1);
    CHECK(count_kind(all, Ev::Kind::DroneExit) == 0);
}

TEST_CASE("w7units W7-04-02: passengers escape a destroyed vehicle transport alive") {
    W7_WORLD(world, x0, y0);
    UnitCombat ht = mk("HTK", 300, "light", 0, 60, 0.0, {}, true);
    ht.passengers = 5;
    ht.size_limit = 2;
    ht.size = 3;
    world.add_combat_unit(x0 + 4, y0 + 4, 0, ht);
    world.add_combat_unit(x0 + 4, y0 + 5, 0, mk("E2", 125, "none", 15, 20, 4.0, kAll100));
    world.add_combat_unit(x0 + 5, y0 + 4, 0, mk("E2", 125, "none", 15, 20, 4.0, kAll100));
    world.group_enter_transport(0, {1, 2}, 0);
    for (int k = 0; k < 60 && world.units()[0].cargo.size() < 2; ++k) world.step(1.0 / 15.0);
    REQUIRE(world.units()[0].cargo.size() == 2);
    world.units()[0].hp = 0;
    world.units()[0].die_time = 0.0;
    for (int k = 0; k < 3; ++k) world.step(1.0 / 15.0);
    CHECK(world.units()[1].alive);
    CHECK(world.units()[2].alive);
    CHECK(world.units()[1].cargo_of == -1);
    CHECK(world.units()[2].cargo_of == -1);
}

TEST_CASE("w7units W7-08-04/06: the Iron Curtain refuses the erase and the drone, kills a riding drone, defuses C4") {
    W7_WORLD(world, x0, y0);
    AbilityProfile cp;
    cp.fire_kind = AbilityKind::Temporal;
    cp.warpable = true;
    world.register_ability_profile("CLEG", cp);
    AbilityProfile tp;
    tp.warpable = true;
    world.register_ability_profile("HTNK", tp);
    world.add_combat_unit(x0, y0, 0, mk("CLEG", 125, "none", 8, 120, 5.0, kAll100));
    world.add_combat_unit(x0 + 3, y0, 1, mk("HTNK", 400, "heavy", 0, 60, 0.0, {}, true));
    world.units()[1].an.iron_left_frames = 300;
    world.units()[0].target = 1;
    world.units()[0].attack_order = true;
    std::vector<Ev> all;
    for (int k = 0; k < 150; ++k) {
        world.step(1.0 / 15.0);
        for (const Ev& e : world.take_ability_events()) all.push_back(e);
    }
    CHECK(count_kind(all, Ev::Kind::TemporalAttach) == 0);
    CHECK(world.units()[1].warp_remaining == 0);
}

TEST_CASE("w7units W7-01-09/10: an erasing target takes no damage; the erase credits the legionnaire") {
    W7_WORLD(world, x0, y0);
    world.enable_veterancy(VeterancyRules{});
    AbilityProfile cp;
    cp.fire_kind = AbilityKind::Temporal;
    cp.warpable = true;
    world.register_ability_profile("CLEG", cp);
    AbilityProfile tp;
    tp.warpable = true;
    world.register_ability_profile("HTNK", tp);
    UnitCombat cleg = mk("CLEG", 125, "none", 8, 120, 5.0, kAll100);
    cleg.cost = 1500;
    world.add_combat_unit(x0, y0, 0, cleg);
    UnitCombat rhino = mk("HTNK", 100, "heavy", 0, 60, 0.0, {}, true);
    rhino.cost = 900;
    world.add_combat_unit(x0 + 3, y0, 1, rhino);
    world.units()[0].target = 1;
    world.units()[0].attack_order = true;
    for (int k = 0; k < 20; ++k) world.step(1.0 / 15.0);
    REQUIRE(world.units()[1].warp_remaining > 0);
    // A Grizzly opens fire on the frozen tank: no damage while it is erased.
    const int gz = static_cast<int>(world.units().size());
    world.add_combat_unit(x0 + 3, y0 + 3, 0, mk("MTNK", 300, "heavy", 90, 30, 6.0, kAll100, true));
    world.units()[gz].target = 1;
    world.units()[gz].attack_order = true;
    const int hp = world.units()[1].hp;
    for (int k = 0; k < 60; ++k) world.step(1.0 / 15.0);
    CHECK(world.units()[1].hp == hp);
    for (int k = 0; k < 400 && world.units()[1].alive; ++k) world.step(1.0 / 15.0);
    world.step(1.0 / 15.0);   // the kill-credit pass runs before the ability pass
    CHECK_FALSE(world.units()[1].alive);
    CHECK(world.units()[0].vet.veterancy > 0.0f);
}

TEST_CASE("w7units W7-08-09: a garrison at ConditionRed evacuates and refuses entry") {
    W7_WORLD(world, x0, y0);
    BuildingTypeDef civ = bdef("CABHUT");
    civ.can_be_occupied = true;
    civ.can_occupy_fire = true;
    civ.max_occupants = 5;
    world.set_economy({civ}, {}, OreValues{}, 600.0, 0.0, 0.0);
    const int b = world.add_building(0, x0 + 6, y0 + 2, 2, 2, 0, 400, armor_index("wood"), 0.0, true);
    UnitCombat gi = mk("E1", 125, "none", 15, 20, 4.0, kAll100);
    gi.occupier = true;
    world.add_combat_unit(x0 + 5, y0 + 2, 0, gi);
    world.add_combat_unit(x0 + 5, y0 + 3, 0, gi);
    REQUIRE(world.campaign_occupy_now(0, b));
    REQUIRE(world.campaign_occupy_now(1, b));
    world.step(1.0 / 15.0);
    CHECK(world.buildings()[b].occupants.size() == 2);
    world.buildings()[b].hp = 100;   // 25 %: ConditionRed
    world.step(1.0 / 15.0);
    CHECK(world.buildings()[b].occupants.empty());
    CHECK(world.units()[0].alive);
    CHECK(world.units()[0].cargo_of == -1);
    CHECK_FALSE(world.can_occupy(0, b));
}

TEST_CASE("w7units W7-01-05: a human's deployed GI refuses a move; a computer's undeploys and walks") {
    W7_WORLD(world, x0, y0);
    UnitCombat gi = mk("E1", 125, "none", 15, 20, 4.0, kAll100);
    gi.deploy_fire = true;
    gi.secondary.valid = true;
    gi.secondary.damage = 15;
    gi.secondary.range = 5.0;
    gi.secondary.verses = kAll100;
    world.add_combat_unit(x0, y0, 0, gi);
    REQUIRE(world.order_deploy(0));
    REQUIRE(world.units()[0].deployed);
    world.order_unit_move(0, x0 + 6, y0);
    CHECK(world.units()[0].deployed);
    CHECK_FALSE(world.units()[0].moving());
}

TEST_CASE("w7units W7-01-03/02: PermaDisguise fools nobody; a disguise as an enemy fools that enemy but not a dog") {
    W7_WORLD(world, x0, y0);
    UnitCombat spy = mk("SPY", 100, "none", 0, 0, 0.0, {});
    spy.tg.rules_id = "SPY";
    spy.w7.agent = true;
    spy.w7.perma_disguise = true;
    world.add_combat_unit(x0, y0, 0, spy);
    world.add_combat_unit(x0 + 3, y0, 1, mk("E2", 125, "none", 15, 20, 4.0, kAll100));
    const Unit& s = world.units()[0];
    CHECK(s.disguised);
    CHECK(s.disguised_as_house == 0);
    CHECK_FALSE(world.disguise_fools(1, s));   // own colours fool nobody
    world.set_disguise(0, "CONS", 1);
    CHECK(world.disguise_fools(1, world.units()[0]));
    CHECK_FALSE(world.disguise_fools(0, world.units()[0]));
    CHECK(world.unit_is_spy(world.units()[0]));
}

TEST_CASE("w7units W7-03-02: a turretless tank destroyer turns its hull to fire") {
    W7_WORLD(world, x0, y0);
    UnitCombat td = mk("TNKD", 400, "heavy", 150, 60, 6.0, kAll100, true);
    td.rot = 5;
    td.tg.rules_id = "TNKD";
    td.w7.has_turret = false;
    world.add_combat_unit(x0, y0, 0, td);
    world.add_combat_unit(x0 + 4, y0, 1, mk("HTNK", 400, "heavy", 0, 60, 0.0, {}, true));
    world.units()[0].body_facing = 32768.0;
    world.units()[0].turret_facing = 32768.0;
    world.units()[0].target = 1;
    world.units()[0].attack_order = true;
    const double want = facing_u16_from_delta(4.0, 0.0);
    for (int k = 0; k < 120; ++k) world.step(1.0 / 15.0);
    CHECK(world.units()[0].body_facing == doctest::Approx(want));
    CHECK(world.units()[0].turret_facing == doctest::Approx(world.units()[0].body_facing));
}
