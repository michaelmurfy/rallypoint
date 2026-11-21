#include "doctest.h"

#include <cmath>

#include "../engine/flight.hpp"
#include "../engine/world.hpp"
#include <algorithm>
using namespace ra2engine;

TEST_CASE("flight: step_altitude ascends to exactly cruise, no overshoot") {
    // Climb 6 leptons/call toward 750; the last partial step lands EXACTLY on
    // 750 (no overshoot) [RE-GATE APPLIED: aircraft.md §2 -- JumpjetClimb=6].
    double alt = 0.0;
    int calls = 0;
    while (alt < 750.0 && calls < 1000) { alt = step_altitude(alt, 750.0, 6.0); ++calls; }
    CHECK(alt == doctest::Approx(750.0));
    CHECK(calls == 125);                      // ceil(750/6) = 125
    CHECK(step_altitude(750.0, 750.0, 6.0) == doctest::Approx(750.0));  // stays put
}

TEST_CASE("flight: step_altitude descends to exactly 0, no undershoot") {
    double alt = 750.0;
    int calls = 0;
    while (alt > 0.0 && calls < 1000) { alt = step_altitude(alt, 0.0, 12.0); ++calls; }
    CHECK(alt == doctest::Approx(0.0));
    CHECK(calls == 63);                        // ceil(750/12) = 63
}

TEST_CASE("flight: cruise_step advances straight-line and arrives exactly") {
    double fx = 10.0, fy = 10.0;
    // Toward (10, 20): 10 cells at 0.5/call -> arrival after 20 calls, exact.
    int calls = 0;
    bool arrived = false;
    while (!arrived && calls < 100) { arrived = cruise_step(fx, fy, 10.0, 20.0, 0.5); ++calls; }
    CHECK(arrived);
    CHECK(calls == 20);
    CHECK(fx == doctest::Approx(10.0));
    CHECK(fy == doctest::Approx(20.0));
    // A single big step that overshoots snaps exactly onto the goal.
    fx = 0.0; fy = 0.0;
    CHECK(cruise_step(fx, fy, 3.0, 4.0, 100.0));   // dist 5 <= 100 -> arrive
    CHECK(fx == doctest::Approx(3.0));
    CHECK(fy == doctest::Approx(4.0));
}

TEST_CASE("flight: cruise_step midpoint is on the straight line toward the goal") {
    double fx = 0.0, fy = 0.0;
    CHECK_FALSE(cruise_step(fx, fy, 6.0, 8.0, 5.0));  // dist 10, step 5 -> halfway
    CHECK(fx == doctest::Approx(3.0));                // 6 * 5/10
    CHECK(fy == doctest::Approx(4.0));                // 8 * 5/10
}

TEST_CASE("flight: heading_toward turns shortest-arc, exact land, OmniFire no-op") {
    // Toward cell +Y (South): facing_u16_from_delta(0,1) == 32768.
    const double want = facing_u16_from_delta(0.0, 1.0);
    double f = 0.0;                                   // start North
    f = heading_toward(f, 0.0, 1.0, 1000.0, /*omni=*/false);
    CHECK(f == doctest::Approx(1000.0));              // stepped toward want
    // Exact landing when within max_turn.
    f = heading_toward(want - 100.0, 0.0, 1.0, 1000.0, false);
    CHECK(f == doctest::Approx(want));
    // OmniFire: never turns, whatever the direction.
    CHECK(heading_toward(12345.0, 1.0, 0.0, 1000.0, /*omni=*/true) == doctest::Approx(12345.0));
    // Degenerate direction: unchanged.
    CHECK(heading_toward(4242.0, 0.0, 0.0, 1000.0, false) == doctest::Approx(4242.0));
}

TEST_CASE("flight: altitude_to_screen_dy is 0 at 0, monotone, ~107 px at cruise") {
    CHECK(altitude_to_screen_dy(0.0) == 0);
    CHECK(altitude_to_screen_dy(-5.0) == 0);
    CHECK(altitude_to_screen_dy(256.0) == 37);        // round(1 * 36.74)
    CHECK(altitude_to_screen_dy(750.0) == 108);       // round((750/256)*36.74)=107.6
    CHECK(altitude_to_screen_dy(750.0) > altitude_to_screen_dy(375.0));  // monotone
    CHECK(altitude_to_screen_dy(375.0) > altitude_to_screen_dy(0.0));
}

TEST_CASE("flight: advance_flight_state transitions on the altitude predicates") {
    CHECK(advance_flight_state(FlightState::TakingOff, 100.0, 750.0) == FlightState::TakingOff);
    CHECK(advance_flight_state(FlightState::TakingOff, 750.0, 750.0) == FlightState::Cruising);
    CHECK(advance_flight_state(FlightState::TakingOff, 800.0, 750.0) == FlightState::Cruising);
    CHECK(advance_flight_state(FlightState::Descending, 10.0, 750.0) == FlightState::Descending);
    CHECK(advance_flight_state(FlightState::Descending, 0.0, 750.0) == FlightState::Grounded);
    CHECK(advance_flight_state(FlightState::Cruising, 750.0, 750.0) == FlightState::Cruising);
    CHECK(advance_flight_state(FlightState::Grounded, 0.0, 750.0) == FlightState::Grounded);
}

// ---- W7-05-15 JumpjetLocomotionClass (ILocomotion vtable 0x7EBD68) ----------

TEST_CASE("flight: jumpjet ramp -- +Accel up to JumpjetSpeed, -1.5 Accel down [RE: FUN_0054CDA0]") {
    // Night Hawk: JumpjetSpeed 30, JumpJetAccel 12.
    double cur = 0.0;
    cur = jumpjet_ramp(cur, 30.0, 30.0, 12.0);
    CHECK(cur == doctest::Approx(12.0));
    cur = jumpjet_ramp(cur, 30.0, 30.0, 12.0);
    CHECK(cur == doctest::Approx(24.0));
    cur = jumpjet_ramp(cur, 30.0, 30.0, 12.0);
    CHECK(cur == doctest::Approx(30.0));               // capped at JumpjetSpeed
    // The rise caps at the TYPE speed, not the target, and the fall test runs
    // after it in the SAME frame: 24 -> 30 (> target 25) -> 30 - 18 = 12.
    CHECK(jumpjet_ramp(24.0, 25.0, 30.0, 12.0) == doctest::Approx(25.0));   // floor at a non-zero target (RE-FALLBACK)
    CHECK(jumpjet_ramp(0.0, 3.0, 30.0, 12.0) == doctest::Approx(3.0));      // no stall short of the goal
    CHECK(jumpjet_ramp(30.0, 0.0, 30.0, 12.0) == doctest::Approx(12.0));   // 30 - 18
    CHECK(jumpjet_ramp(12.0, 0.0, 30.0, 12.0) == doctest::Approx(0.0));
    // Kirov: Accel 10 > Speed 5 -> full speed in one frame.
    CHECK(jumpjet_ramp(0.0, 5.0, 5.0, 10.0) == doctest::Approx(5.0));
}

TEST_CASE("flight: jumpjet approach slow-down tiers [RE: Cruising FUN_0054BD20]") {
    // Night Hawk: Speed 30, TurnRate 6, facing aligned.
    CHECK(jumpjet_cruise_speed(5000.0, 30, 6, 0) == doctest::Approx(30.0));
    CHECK(jumpjet_cruise_speed(200.0, 30, 6, 0) == doctest::Approx(15.0));   // < 50*30/6 = 250
    CHECK(jumpjet_cruise_speed(50.0, 30, 6, 0) == doctest::Approx(7.0));     // < 2*Speed: 30/4
    CHECK(jumpjet_cruise_speed(25.0, 30, 6, 0) == doctest::Approx(3.0));     // < Speed: 30/8
    // Facing far off: Speed/5 inside the 50*S/TR band, Speed/10 inside 2*S.
    CHECK(jumpjet_cruise_speed(200.0, 30, 6, 31) == doctest::Approx(6.0));
    CHECK(jumpjet_cruise_speed(50.0, 30, 6, 7) == doctest::Approx(3.0));
    // Kirov (5, TurnRate 2): 2 lept/frame within 125 leptons, 1 when turning.
    CHECK(jumpjet_cruise_speed(100.0, 5, 2, 0) == doctest::Approx(2.0));
    CHECK(jumpjet_cruise_speed(100.0, 5, 2, 11) == doctest::Approx(1.0));
    CHECK(jumpjet_cruise_speed(130.0, 5, 2, 0) == doctest::Approx(5.0));
}

TEST_CASE("flight: facing_diff_256 and step_along_facing follow the engine convention") {
    CHECK(facing_diff_256(0.0, 0.0) == 0);
    CHECK(facing_diff_256(0.0, 512.0) == 2);            // 2/256 revolution
    CHECK(facing_diff_256(512.0, 0.0) == 2);            // |d|
    CHECK(facing_diff_256(65000.0, 500.0) == 4);        // shortest arc across 0
    double x = 10.0, y = 10.0;
    step_along_facing(x, y, 0.0, 256.0);                // North = -y
    CHECK(x == doctest::Approx(10.0));
    CHECK(y == doctest::Approx(9.0));
    step_along_facing(x, y, 16384.0, 128.0);            // East = +x
    CHECK(x == doctest::Approx(10.5));
    CHECK(y == doctest::Approx(9.0));
}

// ---- W7-05-02 FlyLocomotionClass (ILocomotion vtable 0x7E79F4) --------------

TEST_CASE("flight: fixed-wing climbs 20/frame and descends clamp(d/20,20,50) [RE: FUN_004CD520]") {
    CHECK(fly_altitude_step(0.0, 1500.0, 1.0) == doctest::Approx(20.0));
    CHECK(fly_altitude_step(1490.0, 1500.0, 1.0) == doctest::Approx(1500.0));
    int frames = 0;
    for (double h = 0.0; h < 1500.0 && frames < 1000; ++frames) h = fly_altitude_step(h, 1500.0, 1.0);
    CHECK(frames == 75);                                // 1500 / 20
    CHECK(fly_altitude_step(1500.0, 0.0, 1.0) == doctest::Approx(1450.0));   // d/20 = 75 -> 50
    CHECK(fly_altitude_step(600.0, 0.0, 1.0) == doctest::Approx(570.0));     // d/20 = 30
    CHECK(fly_altitude_step(100.0, 0.0, 1.0) == doctest::Approx(80.0));      // floor 20
    CHECK(fly_altitude_step(10.0, 0.0, 1.0) == doctest::Approx(0.0));        // lands exactly
    CHECK(fly_altitude_step(1510.0, 1500.0, 1.0) == doctest::Approx(1500.0)); // no undershoot
}

TEST_CASE("flight: fixed-wing throttle -- raw Speed*256/100, 0.1/frame ramp, SlowdownDistance") {
    CHECK(fly_full_speed_lpf(14) == 35);                // ORCA / BEAG: no GameSpeedBias
    CHECK(fly_full_speed_lpf(100) == 255);              // min(255, 256)
    CHECK(fly_throttle_ramp(0.0, 1.0, 1.0) == doctest::Approx(0.1));
    CHECK(fly_throttle_ramp(0.95, 1.0, 1.0) == doctest::Approx(1.0));
    CHECK(fly_throttle_ramp(0.5, 0.0, 1.0) == doctest::Approx(0.4));
    bool stop = false;
    CHECK(fly_throttle_target(100.0, 500, /*slow=*/false, &stop) == doctest::Approx(1.0));
    CHECK(fly_throttle_target(1000.0, 500, true, &stop) == doctest::Approx(1.0));
    CHECK(fly_throttle_target(250.0, 500, true, &stop) == doctest::Approx(0.5));
    CHECK_FALSE(stop);
    CHECK(fly_throttle_target(40.0, 500, true, &stop) == doctest::Approx(0.0));
    CHECK(stop);
}

// ---- World-level: the jumpjet and fixed-wing steppers -----------------------

#include "asset_fixture.hpp"

namespace {
bool w7_map(ra2assets::MapFile& out) {
    if (!ra2fixture::mapsmd03_available()) return false;
    out = ra2fixture::mapsmd03_map();
    return true;
}
UnitCombat w7_base() {
    UnitCombat c;
    c.strength = 300;
    c.armor = "light";
    c.image = "W7AIR";
    c.damage = 10;
    c.rof = 10;
    c.range = 6.0;
    c.verses = {100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100};
    c.is_vehicle = true;
    return c;
}
// [ZEP] JumpjetSpeed=5 JumpJetTurnRate=2 JumpJetAccel=10 JumpjetClimb=6
// JumpjetHeight=750, resolved exactly as main.cpp resolve_combat_unit does.
UnitCombat w7_kirov() {
    UnitCombat c = w7_base();
    c.is_aircraft = true;
    c.cruise_altitude = 750.0;
    c.climb_rate = 6.0 * 15.0;
    c.descend_rate = 12.0 * 15.0;
    c.flight_speed = 5.0 * 15.0 / 256.0;
    c.flight_turn = 2.0 * 256.0 * 15.0;
    c.an.jj_accel = 10.0;
    c.balloon_hover = true;
    c.omni_fire = true;
    return c;
}
// ORCA-shaped plane: Speed=14 (35 lept/frame), ROT=3 (768 u16/frame).
UnitCombat w7_plane(int ammo, bool fighter, bool strafe) {
    UnitCombat c = w7_base();
    c.is_aircraft = true;
    c.an.fixed_wing = true;
    c.an.ammo = ammo;
    c.an.fighter = fighter;
    c.an.strafe = strafe;
    c.an.fly_speed_cps = fly_full_speed_lpf(14) * 15.0 / 256.0;
    c.flight_speed = c.an.fly_speed_cps;
    c.flight_turn = 3.0 * 256.0 * 15.0;
    c.cruise_altitude = 1500.0;
    c.omni_fire = true;
    return c;
}
UnitCombat w7_tank() {
    UnitCombat c = w7_base();
    c.strength = 100000;
    c.image = "W7TGT";
    return c;
}
int count_shots(World& w, int shooter) {
    int n = 0;
    // Only the sortie's own event (it names the target; the generic
    // fire_flash event world.cpp adds afterwards carries target -1).
    for (const auto& e : w.take_fire_events()) if (e.unit == shooter && e.target_unit >= 0) ++n;
    return n;
}
}  // namespace

TEST_CASE("flight: World Kirov cruises at raw JumpjetSpeed 5 leptons/frame [RE: FUN_0054CDA0]") {
    ra2assets::MapFile map;
    if (!w7_map(map)) { MESSAGE("skipped: real map assets not found"); return; }
    World world(map);
    world.add_combat_unit(20, 20, 0, w7_kirov());
    world.order_unit_move(0, 20, 60);
    const double dt = 1.0 / 15.0;
    for (int f = 0; f < 400 && world.units()[0].flight_state != FlightState::Cruising; ++f) world.step(dt);
    REQUIRE(world.units()[0].flight_state == FlightState::Cruising);
    const double y0 = world.units()[0].fy;
    for (int f = 0; f < 150; ++f) {
        const double before = world.units()[0].fy;
        world.step(dt);
        CHECK(world.units()[0].fy - before <= 5.0 / 256.0 + 1e-9);   // never above 5 lept/frame
    }
    // 150 frames x 5 leptons = 750 leptons = ~2.93 cells (~0.29 cells/s).
    CHECK(world.units()[0].fy - y0 == doctest::Approx(750.0 / 256.0).epsilon(0.02));
    CHECK(world.units()[0].an.jj_speed == doctest::Approx(5.0));
}

TEST_CASE("flight: World jumpjet turns at JumpJetTurnRate<<8 and moves along its facing") {
    ra2assets::MapFile map;
    if (!w7_map(map)) { MESSAGE("skipped: real map assets not found"); return; }
    World world(map);
    world.add_combat_unit(30, 30, 0, w7_kirov());
    world.units()[0].body_facing = 0.0;               // facing North (-y)
    world.order_unit_move(0, 30, 60);                 // goal due South
    const double dt = 1.0 / 15.0;
    for (int f = 0; f < 200; ++f) {
        const double b0 = world.units()[0].body_facing;
        world.step(dt);
        const double b1 = world.units()[0].body_facing;
        double d = std::fabs(b1 - b0);
        if (d > 32768.0) d = 65536.0 - d;
        CHECK(d <= 512.0 + 1e-6);                     // JumpJetTurnRate 2 << 8 per frame
    }
    // Now (about) facing the goal, which lies south of where it drifted to.
    CHECK(std::fabs(world.units()[0].body_facing - 32768.0) < 1024.0);
    CHECK(world.units()[0].fy > 30.0);                // and travelling toward it
}

TEST_CASE("flight: World fixed-wing flies along its facing -- no instant reversal [RE: FUN_004CD520]") {
    ra2assets::MapFile map;
    if (!w7_map(map)) { MESSAGE("skipped: real map assets not found"); return; }
    World world(map);
    world.add_combat_unit(40, 40, 0, w7_plane(1, true, false));
    world.add_combat_unit(40, 60, 1, w7_tank());      // target due South
    {
        Unit& p = world.units()[0];
        p.altitude = 1500.0;
        p.flight_state = FlightState::Cruising;
        p.body_facing = 0.0;                          // flying North, away from it
        p.an.fly_throttle = 1.0;
    }
    REQUIRE(world.order_fixed_wing(0, 1, -1, -1, -1));
    const double dt = 1.0 / 15.0;
    const double y0 = world.units()[0].fy;
    world.step(dt);
    CHECK(world.units()[0].fy < y0);                  // kept going North this frame
    double miny = world.units()[0].fy;
    bool turned_south = false;
    for (int f = 0; f < 200; ++f) {
        const double b0 = world.units()[0].body_facing;
        world.step(dt);
        double d = std::fabs(world.units()[0].body_facing - b0);
        if (d > 32768.0) d = 65536.0 - d;
        CHECK(d <= 768.0 + 1e-6);                     // ROT 3 << 8 per frame
        miny = std::min(miny, world.units()[0].fy);
        if (world.units()[0].fy > y0 + 1.0) turned_south = true;
    }
    CHECK(miny < y0 - 1.0);                           // it overflew northward first
    CHECK(turned_south);                              // then looped back south
}

TEST_CASE("flight: World fixed-wing climbs 20 leptons/frame and holds until FlightLevel/2") {
    ra2assets::MapFile map;
    if (!w7_map(map)) { MESSAGE("skipped: real map assets not found"); return; }
    World world(map);
    world.add_combat_unit(40, 40, 0, w7_plane(1, true, false));
    REQUIRE(world.order_fixed_wing(0, -1, -1, 40, 70));
    const double dt = 1.0 / 15.0;
    const double x0 = world.units()[0].fx, y0 = world.units()[0].fy;
    for (int f = 1; f <= 37; ++f) {
        world.step(dt);
        CHECK(world.units()[0].altitude == doctest::Approx(20.0 * f));
        CHECK(world.units()[0].fx == doctest::Approx(x0));     // vertical take-off
        CHECK(world.units()[0].fy == doctest::Approx(y0));
    }
    for (int f = 0; f < 40; ++f) world.step(dt);
    CHECK(world.units()[0].altitude == doctest::Approx(1500.0));
    CHECK(std::fabs(world.units()[0].fy - y0) > 0.5);          // under way
}

TEST_CASE("flight: World Fighter re-attacks until Ammo is 0 [RE: Mission_Attack FUN_00417FE0]") {
    ra2assets::MapFile map;
    if (!w7_map(map)) { MESSAGE("skipped: real map assets not found"); return; }
    World world(map);
    world.add_combat_unit(40, 40, 0, w7_plane(3, /*fighter=*/true, false));
    world.add_combat_unit(40, 50, 1, w7_tank());
    {
        Unit& p = world.units()[0];
        p.altitude = 1500.0;
        p.flight_state = FlightState::Cruising;
        p.an.fly_throttle = 1.0;
    }
    REQUIRE(world.order_fixed_wing(0, 1, -1, -1, -1));
    const double dt = 1.0 / 15.0;
    int shots = 0;
    for (int f = 0; f < 900; ++f) { world.step(dt); shots += count_shots(world, 0); }
    CHECK(shots == 3);                                // one per Ammo point
    CHECK(world.units()[0].an.ammo == 0);
}

TEST_CASE("flight: World Strafe weapon fires 1 + 4 shots per Ammo point [RE: FUN_00417FE0 6..9]") {
    ra2assets::MapFile map;
    if (!w7_map(map)) { MESSAGE("skipped: real map assets not found"); return; }
    World world(map);
    world.add_combat_unit(40, 40, 0, w7_plane(1, false, /*strafe=*/true));
    world.add_combat_unit(40, 50, 1, w7_tank());
    {
        Unit& p = world.units()[0];
        p.altitude = 1500.0;
        p.flight_state = FlightState::Cruising;
        p.an.fly_throttle = 1.0;
    }
    REQUIRE(world.order_fixed_wing(0, 1, -1, -1, -1));
    const double dt = 1.0 / 15.0;
    int shots = 0;
    for (int f = 0; f < 900; ++f) { world.step(dt); shots += count_shots(world, 0); }
    CHECK(shots == 5);
}
