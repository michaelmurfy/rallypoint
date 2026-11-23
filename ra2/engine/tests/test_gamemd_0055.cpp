// Patch 0055: turret AI (FUN_00736990), building damage fires (FUN_0043c0d0 / FUN_0043fb4a),
// fear gate (FUN_005200b0). Integer / fixed-order maths only, so every toolchain agrees.
#include "doctest.h"
#include "building.hpp"
#include "inf_anim.hpp"
#include "world.hpp"
#include <set>
#include <vector>

using namespace ra2engine;

namespace {
Unit tank() {
    Unit u;
    u.is_vehicle = true;
    u.alive = true;
    u.fx = u.fy = 5.0;
    u.rot_u16_per_sec = 5 * kRotU16PerRotPerSec;
    return u;
}
std::vector<Building> none;
}  // namespace

TEST_CASE("0055 damage fires: Yellow, or Red for CanBeOccupied") {
    CHECK_FALSE(building_damage_fires_on(51, 100, false, 0.5, 0.25));
    CHECK(building_damage_fires_on(50, 100, false, 0.5, 0.25));
    CHECK_FALSE(building_damage_fires_on(40, 100, true, 0.5, 0.25));   // garrisonable: waits for Red
    CHECK(building_damage_fires_on(25, 100, true, 0.5, 0.25));
    CHECK_FALSE(building_damage_fires_on(1, 0, false, 0.5, 0.25));
}

TEST_CASE("0055 damage fires: type and start frame are deterministic and in range") {
    std::set<int> types;
    for (unsigned seed = 1; seed < 200; ++seed) {
        const int t0 = building_damage_fire_type(seed, 0, 3);
        CHECK(t0 == building_damage_fire_type(seed, 0, 3));
        CHECK(building_damage_fire_type(seed, 1, 3) == (t0 + 1) % 3);   // steps through the list
        types.insert(t0);
        const int s = building_damage_fire_start(seed, 2, 20);
        CHECK(s >= 0);
        CHECK(s < 20);
    }
    CHECK(types.size() == 3);   // the first pick is random, not always index 0
    CHECK(building_damage_fire_type(7, 0, 0) == -1);
    CHECK(building_damage_fire_start(7, 0, 0) == 0);
}

TEST_CASE("0055 turret: idle turret holds until GuardAreaTargetingDelay + 5, then returns") {
    std::vector<Unit> us = {tank()};
    us[0].body_facing = 0.0;
    us[0].turret_facing = 20000.0;
    us[0].turret_idle_frames = 0;   // just fired
    for (int i = 0; i < 20; ++i) step_turrets(us, none, 1.0 / 15.0, 41);
    CHECK(us[0].turret_facing == doctest::Approx(20000.0));
    for (int i = 0; i < 40; ++i) step_turrets(us, none, 1.0 / 15.0, 41);
    CHECK(us[0].turret_facing == doctest::Approx(0.0));
}

TEST_CASE("0055 turret: TurretSpins keeps turning; OmniFire turret does not chase") {
    std::vector<Unit> us = {tank(), tank()};
    us[0].w7.t.turret_spins = true;
    const double before = us[0].turret_facing;
    step_turrets(us, none, 1.0 / 15.0, 41);
    CHECK(us[0].turret_facing != doctest::Approx(before));
    us[1].omni_fire = true;
    us[1].target_building = 0;
    Building b;
    b.alive = true; b.cx = 20; b.cy = 5; b.fw = b.fh = 1;
    std::vector<Building> bs = {b};
    const double f1 = us[1].turret_facing;
    for (int i = 0; i < 30; ++i) step_turrets(us, bs, 1.0 / 15.0, 41);
    CHECK(us[1].turret_facing == doctest::Approx(f1));
}

TEST_CASE("0055 fear AI: Down only while afraid on entry (FUN_005200b0 @0x5200BB)") {
    InfTable t;
    t.e[kSqReady] = {0, 1, 0, -1, true};
    t.e[kSqDown] = {1, 2, 0, -1, true};
    t.e[kSqProne] = {3, 1, 0, -1, true};
    t.e[kSqUp] = {4, 2, 0, -1, true};
    t.fraidycat = false;
    InfAnimState s;
    s.armed = true;
    InfTickCtx c;
    int facing = 0;
    s.fear = 0;
    inf_anim_tick(s, t, facing, c);
    CHECK_FALSE(s.prone);
    s.fear = 60;   // 60 -> 59 this tick, still > 49: goes prone
    inf_anim_tick(s, t, facing, c);
    CHECK(s.prone);
}
