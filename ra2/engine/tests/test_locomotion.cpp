// tests/test_locomotion.cpp -- W7 locomotion (docs/gap/wave7/11-locomotion.md):
// the track tables, the per-locomotor speeds, curved bends, hover ramp,
// acceleration, the damage slow-down and the blocked-move state machine.
#include "doctest.h"

#include <cmath>
#include <vector>

#include "locomotion.hpp"
#include "occupancy.hpp"
#include "pathfind.hpp"
#include "world.hpp"

using namespace ra2engine;

namespace {

const AnimTable kNoAnims{};
constexpr double kDt = 1.0 / 15.0;   // one logic frame

std::vector<std::uint8_t> open_grid(int w, int h) { return std::vector<std::uint8_t>((size_t)w * h, 1); }

// Leptons/frame -> the speed_cps a type's Speed= resolves to (raw, no bias).
double cps_of_lept(int lept) { return lept * 15.0 / 256.0; }

Unit make_unit(int x, int y, Occupancy& occ, int idx) {
    Unit u;
    u.cell_x = x;
    u.cell_y = y;
    u.fx = x;
    u.fy = y;
    occ.set(x, y, idx);
    return u;
}

void give_path(Unit& u, std::vector<Cellxy> path) {
    u.path = std::move(path);
    u.path_idx = 1;
}

void step(std::vector<Unit>& units, const std::vector<std::uint8_t>& p, int w, int h, Occupancy& occ,
          const MoveSpeedCtx* sc = nullptr) {
    step_units(units, p, p, w, h, occ, kDt, 0.0, kNoAnims, nullptr, sc);
}

// Frames until unit 0 stops moving (cap `max`).
int frames_to_arrive(std::vector<Unit>& units, const std::vector<std::uint8_t>& p, int w, int h,
                     Occupancy& occ, int max = 2000, const MoveSpeedCtx* sc = nullptr) {
    for (int f = 1; f <= max; ++f) {
        step(units, p, w, h, occ, sc);
        if (!units[0].moving()) return f;
    }
    return -1;
}

}  // namespace

TEST_CASE("W7 tracks: TurnTrack / RawTrack data and the FUN_004B46A0 transform") {
    // Straight north (TT[0] -> RawTrack 1): 23 points, 11 leptons apart.
    CHECK(raw_track(1).n == 23);
    CHECK(raw_track(2).n == 31);   // straight diagonal
    int x, y, f;
    track_point(1, 0, 0, x, y, f);
    CHECK(x == 0);
    CHECK(y == 245);
    CHECK(f == 0);
    // East straight = TT[2*8+2]: RawTrack 1 with swap + negate-x.
    const TurnTrack& e = turn_track(2, 2);
    CHECK(e.track == 1);
    track_point(e.track, 0, e.flags & 7, x, y, f);
    CHECK(x == -245);
    CHECK(y == 0);
    CHECK(f == 64 * 256);   // facing East
    // A 90 degree bend N -> E is the two-cell curve RawTrack 4.
    const TurnTrack& b = turn_track(0, 2);
    CHECK(b.track == 4);
    CHECK((b.flags & 8) != 0);
    // 135 / 180 degree bends have no track (turn in place, then straight).
    CHECK(turn_track(0, 3).track == 0);
    CHECK(turn_track(0, 4).track == 0);
    CHECK(dir8_of_step(0, -1) == 0);
    CHECK(dir8_of_step(1, -1) == 1);
    CHECK(dir8_of_step(1, 0) == 2);
    CHECK(dir8_of_step(-1, -1) == 7);
    CHECK(dir8_of_step(2, 0) == -1);
}

TEST_CASE("W7-11-01: speed_cps is raw leptons; crawling is s - s/3") {
    CHECK(leptons_per_frame(cps_of_lept(10)) == 10);
    CHECK(leptons_per_frame(cps_of_lept(17)) == 17);
    CHECK(infantry_speed(10, false, true) == 10);
    CHECK(infantry_speed(10, true, true) == 7);    // [RE: FUN_00521AB0] prone GI
    CHECK(infantry_speed(10, true, false) == 15);  // Crawls=no prone: s + s/2
    CHECK(apply_speed_pct(17, 0.75) == 12);        // ftol(17 x .75)
}

TEST_CASE("W7-11-01: a GI walks 10 raw leptons per frame (no GameSpeedBias)") {
    auto p = open_grid(12, 1);
    Occupancy occ(12, 1);
    std::vector<Unit> units{make_unit(0, 0, occ, 0)};
    units[0].speed_cps = cps_of_lept(10);
    give_path(units[0], find_path(p, 12, 1, {0, 0}, {10, 0}));
    // 10 cells = 2560 leptons at 10 per frame = 256 frames.
    const int f = frames_to_arrive(units, p, 12, 1, occ);
    CHECK(f >= 255);
    CHECK(f <= 257);
}

TEST_CASE("W7-11-05: a prone GI crawls at 7 leptons per frame") {
    auto p = open_grid(12, 1);
    Occupancy occ(12, 1);
    std::vector<Unit> units{make_unit(0, 0, occ, 0)};
    units[0].speed_cps = cps_of_lept(10);
    units[0].ia.prone = true;
    give_path(units[0], find_path(p, 12, 1, {0, 0}, {7, 0}));
    // 7 cells = 1792 leptons at 7 per frame = 256 frames.
    const int f = frames_to_arrive(units, p, 12, 1, occ);
    CHECK(f >= 255);
    CHECK(f <= 257);
}

TEST_CASE("W7-11-01/04: a Drive vehicle covers 161 speed units per straight cell (x1.59)") {
    auto p = open_grid(12, 1);
    Occupancy occ(12, 1);
    std::vector<Unit> units{make_unit(0, 0, occ, 0)};
    Unit& u = units[0];
    u.is_vehicle = true;
    u.speed_cps = cps_of_lept(17);   // Grizzly Speed 7
    u.rot_u16_per_sec = 5 * 256 * 15;
    u.body_facing = 16384.0;         // already facing East: no turn
    give_path(u, find_path(p, 12, 1, {0, 0}, {10, 0}));
    // 10 straight cells = 230 track points x 7 = 1610 units at 17/frame = 94.7 frames.
    const int f = frames_to_arrive(units, p, 12, 1, occ);
    CHECK(f >= 94);
    CHECK(f <= 96);
    CHECK(units[0].cell_x == 10);
    CHECK(units[0].fx == 10.0);
}

TEST_CASE("W7-11-04: a 90 degree bend is driven as one curve, without stopping") {
    auto p = open_grid(8, 8);
    Occupancy occ(8, 8);
    std::vector<Unit> units{make_unit(1, 6, occ, 0)};
    Unit& u = units[0];
    u.is_vehicle = true;
    u.speed_cps = cps_of_lept(17);
    u.rot_u16_per_sec = 5 * 256 * 15;
    u.body_facing = 0.0;   // North
    // North two cells, then East two cells.
    give_path(u, {{1, 6}, {1, 5}, {1, 4}, {2, 4}, {3, 4}});
    double px = u.fx, py = u.fy;
    int still = 0, frames = 0;
    bool turned_while_moving = false;
    while (units[0].moving() && frames < 400) {
        const double before = units[0].body_facing;
        step(units, p, 8, 8, occ);
        ++frames;
        const bool moved = units[0].fx != px || units[0].fy != py;
        if (!moved && units[0].moving()) ++still;
        if (moved && units[0].body_facing != before && units[0].body_facing != 0.0 &&
            units[0].body_facing != 16384.0)
            turned_while_moving = true;
        px = units[0].fx;
        py = units[0].fy;
    }
    CHECK(units[0].cell_x == 3);
    CHECK(units[0].cell_y == 4);
    CHECK(units[0].body_facing == 16384.0);   // ends facing East exactly
    CHECK(still == 0);                        // never stopped to turn
    CHECK(turned_while_moving);
}

TEST_CASE("W7-11-04: a 180 degree reversal turns in place first (no reverse track)") {
    auto p = open_grid(6, 1);
    Occupancy occ(6, 1);
    std::vector<Unit> units{make_unit(3, 0, occ, 0)};
    Unit& u = units[0];
    u.is_vehicle = true;
    u.speed_cps = cps_of_lept(17);
    u.rot_u16_per_sec = 5 * 256 * 15;   // 1280 u16 per frame
    u.body_facing = 16384.0;            // East
    give_path(u, {{3, 0}, {2, 0}, {1, 0}});
    step(units, p, 6, 1, occ);
    CHECK(units[0].fx == 3.0);          // turning, not moving
    // 180 degrees at 1280/frame = 25.6 frames before the first track starts.
    for (int i = 0; i < 25; ++i) step(units, p, 6, 1, occ);
    CHECK(units[0].fx == 3.0);
    for (int i = 0; i < 5; ++i) step(units, p, 6, 1, occ);
    CHECK(units[0].fx < 3.0);
}

TEST_CASE("W7-11-06: Drive slows to ftol(speed x .75) at ConditionYellow (50%)") {
    auto p = open_grid(12, 1);
    Occupancy occ(12, 1);
    std::vector<Unit> units{make_unit(0, 0, occ, 0)};
    Unit& u = units[0];
    u.is_vehicle = true;
    u.speed_cps = cps_of_lept(17);
    u.rot_u16_per_sec = 5 * 256 * 15;
    u.body_facing = 16384.0;
    u.max_hp = 300;
    u.hp = 150;   // exactly 50%
    give_path(u, find_path(p, 12, 1, {0, 0}, {10, 0}));
    // 1610 units at ftol(17 x .75) = 12 per frame = 134.2 frames.
    const int f = frames_to_arrive(units, p, 12, 1, occ);
    CHECK(f >= 134);
    CHECK(f <= 136);
}

TEST_CASE("W7-11-07: Accelerates ramps SpeedPercentage by AccelerationFactor per frame") {
    auto p = open_grid(30, 1);
    Occupancy occ(30, 1);
    std::vector<Unit> units{make_unit(0, 0, occ, 0)};
    Unit& u = units[0];
    u.is_vehicle = true;
    u.speed_cps = cps_of_lept(17);
    u.rot_u16_per_sec = 5 * 256 * 15;
    u.body_facing = 16384.0;
    u.loco.t.accelerates = true;
    give_path(u, find_path(p, 30, 1, {0, 0}, {28, 0}));
    step(units, p, 30, 1, occ);
    CHECK(units[0].loco.pct == doctest::Approx(0.03));
    for (int i = 0; i < 9; ++i) step(units, p, 30, 1, occ);
    CHECK(units[0].loco.pct == doctest::Approx(0.30));
    for (int i = 0; i < 40; ++i) step(units, p, 30, 1, occ);
    CHECK(units[0].loco.pct == doctest::Approx(1.0));
    // Inside SlowdownDistance (500 leptons) it brakes toward a 0.3 floor.
    while (units[0].moving() && (28.0 - units[0].fx) * 256.0 > 400.0) step(units, p, 30, 1, occ);
    CHECK(units[0].loco.pct < 1.0);
    CHECK(units[0].loco.pct >= 0.3);
}

TEST_CASE("W7-11-02: Hover ramps up at 1/18 per frame and cruises at raw Speed leptons") {
    auto p = open_grid(40, 1);
    Occupancy occ(40, 1);
    std::vector<Unit> units{make_unit(0, 0, occ, 0)};
    Unit& u = units[0];
    u.is_vehicle = true;
    u.loco.t.kind = static_cast<int>(LocoKind::Hover);
    u.speed_cps = cps_of_lept(25);   // ROBO Speed 10
    u.rot_u16_per_sec = 10 * 256 * 15;
    u.body_facing = 16384.0;
    give_path(u, find_path(p, 40, 1, {0, 0}, {38, 0}));
    step(units, p, 40, 1, occ);
    CHECK(units[0].loco.hover == doctest::Approx(1.0 / 18.0));
    for (int i = 0; i < 30; ++i) step(units, p, 40, 1, occ);
    CHECK(units[0].loco.hover == doctest::Approx(1.0));
    const double x0 = units[0].fx;
    for (int i = 0; i < 10; ++i) step(units, p, 40, 1, occ);
    CHECK((units[0].fx - x0) * 256.0 == doctest::Approx(250.0).epsilon(0.03));   // ~25 per frame, not 40
}

TEST_CASE("W7-11-03/09: a blocked unit keeps its order, waits on PathDelay and scatters a parked friend") {
    // A 1-wide corridor: the parked friendly at (3,0) cannot be routed around.
    auto p = open_grid(10, 1);
    Occupancy occ(10, 1);
    std::vector<Unit> units{make_unit(0, 0, occ, 0), make_unit(3, 0, occ, 1)};
    units[0].speed_cps = cps_of_lept(10);
    give_path(units[0], find_path(p, 10, 1, {0, 0}, {8, 0}));
    std::vector<int> scatter;
    MoveSpeedCtx sc;
    sc.scatter_out = &scatter;
    for (int f = 0; f < 150; ++f) step(units, p, 10, 1, occ, &sc);   // 10 s: far past the old 2 s give-up
    CHECK(units[0].moving());          // still holding the order
    CHECK(units[0].cell_x == 2);       // waiting next to the blocker
    CHECK_FALSE(scatter.empty());      // the parked friend was asked to scatter
    CHECK(scatter[0] == 1);
}

TEST_CASE("W7-11-10: a unit within CloseEnough of a friend-held goal stops there") {
    auto p = open_grid(10, 1);
    Occupancy occ(10, 1);
    std::vector<Unit> units{make_unit(0, 0, occ, 0), make_unit(5, 0, occ, 1)};
    units[0].speed_cps = cps_of_lept(10);
    give_path(units[0], find_path(p, 10, 1, {0, 0}, {5, 0}));
    const int f = frames_to_arrive(units, p, 10, 1, occ, 400);
    CHECK(f > 0);                     // the move ended...
    CHECK(units[0].cell_x == 4);      // ...one cell short (256 < 576 leptons)
}

TEST_CASE("W7-11-08: uniform step cost -- a diagonal costs what an orthogonal does") {
    // (0,0) -> (4,2) on an open grid: 4 steps (Chebyshev), not 4+ octile.
    auto p = open_grid(8, 8);
    const std::vector<Cellxy> path = find_path(p, 8, 8, {0, 0}, {4, 2});
    CHECK(path.size() == 5);
    // A parked friendly costs 8: a one-cell detour (same step count) avoids it.
    Occupancy occ(8, 8);
    std::vector<Unit> units{make_unit(0, 3, occ, 0), make_unit(2, 3, occ, 1)};
    std::vector<std::uint16_t> cost;
    build_occupancy_cost(units, occ, 8, 8, 0, false, nullptr, cost);
    CHECK(cost[3 * 8 + 2] == 8);
    const std::vector<Cellxy> around =
        find_path_bounded_t(p, 8, 8, {0, 3}, {4, 3}, 0, nullptr, nullptr, &cost);
    REQUIRE(around.size() == 5);
    for (const Cellxy& c : around) CHECK_FALSE((c.x == 2 && c.y == 3));
}

TEST_CASE("W7-11-02: a slow-turning hover (LCRF ROT 5) still reaches its goal round bends") {
    auto p = open_grid(20, 20);
    Occupancy occ(20, 20);
    std::vector<Unit> units{make_unit(2, 2, occ, 0)};
    Unit& u = units[0];
    u.is_vehicle = true;
    u.loco.t.kind = static_cast<int>(LocoKind::Hover);
    u.speed_cps = cps_of_lept(15);
    u.rot_u16_per_sec = 5 * 256 * 15;
    u.body_facing = 32768.0;   // facing South, path heads East then North
    give_path(u, {{2, 2}, {3, 2}, {4, 2}, {5, 1}, {5, 0}, {6, 0}, {6, 1}, {7, 2}, {8, 3}});
    const int f = frames_to_arrive(units, p, 20, 20, occ, 1500);
    CHECK(f > 0);
    CHECK(units[0].cell_x == 8);
    CHECK(units[0].cell_y == 3);
}

#include "combat.hpp"
TEST_CASE("W7-05-11: CellRangefinding measures the range cell centre to cell centre") {
    static ra2assets::MapFile map;
    World w(map);
    Unit u;
    u.w_range = 1.5;
    u.fx = 0.6;   // cell 1 (centre 1.0)
    u.fy = 0.0;
    const int tx = static_cast<int>(2.4 * 256.0), ty = 0;   // cell 2
    CHECK_FALSE(weapon_in_range(w, u, tx, ty, 0));   // 1.8 cells apart: out of 1.5
    u.an.t.cell_rangefinding = true;
    CHECK(weapon_in_range(w, u, tx, ty, 0));         // cell 1 -> cell 2: 1.0 cell
}
