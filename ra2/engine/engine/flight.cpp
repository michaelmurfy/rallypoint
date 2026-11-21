#include "flight.hpp"
#include "orig_math.hpp"   // Net L0: the original's FastMath, never libm in the sim

#include <algorithm>
#include <cmath>

namespace ra2engine {

double step_altitude(double altitude, double target, double max_delta) {
    const double d = target - altitude;
    if (std::fabs(d) <= max_delta) return target;      // no overshoot: land exactly
    return altitude + (d > 0.0 ? max_delta : -max_delta);
}

bool cruise_step(double& fx, double& fy, double gx, double gy, double max_step) {
    const double dx = gx - fx;
    const double dy = gy - fy;
    const double dist = om::sqrt(dx * dx + dy * dy);
    if (dist <= max_step || dist == 0.0) {             // arrival: snap exactly
        fx = gx;
        fy = gy;
        return true;
    }
    fx += dx / dist * max_step;                        // straight-line advance
    fy += dy / dist * max_step;
    return false;
}

double heading_toward(double body_facing, double dx, double dy,
                      double max_turn, bool omni_fire) {
    if (omni_fire) return body_facing;                 // OmniFire: never turns
    if (dx == 0.0 && dy == 0.0) return body_facing;    // degenerate direction
    const double want = facing_u16_from_delta(dx, dy);
    return turn_toward(body_facing, want, max_turn);   // shortest arc, exact land
}

int altitude_to_screen_dy(double altitude) {
    // [RE-GATE APPLIED: aircraft.md §3 -- [RE-FALLBACK] (loco pixel-offset not
    // unwound): the voxel up/z term is ~36.74 px per cell-of-z (voxel-render.md
    // §6), so dy = (altitude/256) * 36.74.]
    if (altitude <= 0.0) return 0;
    return static_cast<int>(om::lround(altitude / 256.0 * 36.74));
}

FlightState advance_flight_state(FlightState state, double altitude,
                                 double cruise_altitude) {
    if (state == FlightState::TakingOff && altitude >= cruise_altitude)
        return FlightState::Cruising;
    if (state == FlightState::Descending && altitude <= 0.0)
        return FlightState::Grounded;
    return state;
}

double jumpjet_cruise_speed(double dist, int speed, int turn_rate, int facing_diff) {
    // [RE: FUN_0054BD20 @0x54BE5x-0x54BF9x; DAT_007E0718 = 1.0 floor]
    const int tr = std::max(1, turn_rate);
    if (dist < speed) return static_cast<double>(speed / 8);
    if (dist < speed * 2) {
        if (tr < facing_diff) return std::max(1.0, static_cast<double>(speed / 10));
        return static_cast<double>(speed / 4);
    }
    if (dist < (speed * 50) / tr) {
        if (tr * 5 < facing_diff) return std::max(1.0, static_cast<double>(speed / 5));
        return static_cast<double>(speed / 2);
    }
    return static_cast<double>(speed);
}

double jumpjet_ramp(double cur, double target, double max_speed, double accel) {
    if (cur < target) cur = std::min(cur + accel, max_speed);   // capped at JumpjetSpeed
    // DAT_007E38F0 = 1.5; the original floors the drop at 0 (DAT_007E1800).
    // [RE-FALLBACK, w7fix-move]: taken literally that floor stalls a cruiser
    // whose approach target (Speed/8, Speed/4 -- FUN_0054BD20) is below its
    // JumpjetAccel (Night Hawk 30 / 12) at CurSpeed 0 short of the goal (each
    // frame +Accel then -1.5 Accel); whatever moves the original on from there
    // is not traced, so a non-zero target is the floor instead.
    if (target < cur) cur = std::max(cur - accel * 1.5, target > 0.0 ? target : 0.0);
    return cur;
}

int facing_diff_256(double from, double to) {
    int d = static_cast<int>(om::lround(to - from)) & 0xFFFF;
    if (d >= 0x8000) d = 0x10000 - d;                // shortest arc, |d|
    return ((d >> 7) + 1) >> 1;
}

void step_along_facing(double& fx, double& fy, double facing, double leptons) {
    if (leptons == 0.0) return;
    const double a = facing / 65536.0 * 6.283185307179586;
    fx += om::sin(a) * leptons / 256.0;
    fy -= om::cos(a) * leptons / 256.0;
}

double fly_altitude_step(double h, double target, double frames) {
    if (h < target) {                                 // @0x4CDD9D: min(d, 20)
        return std::min(target, h + std::min(target - h, 20.0) * frames);
    }
    if (h > target) {                                 // @0x4CDE74: clamp(d/20, 20, 50)
        const double d = h - target;
        double step = std::floor(d / 20.0);
        if (step > d) step = d;
        step = std::clamp(step, 20.0, 50.0);
        return std::max(target, h - step * frames);
    }
    return h;
}

double fly_throttle_target(double dist, int slowdown_distance, bool slow, bool* stop) {
    if (stop) *stop = false;
    if (!slow) return 1.0;
    const double sd = slowdown_distance > 0 ? slowdown_distance : 500.0;
    double t = std::min(1.0, dist / sd);              // DAT_007E0718 = 1.0 cap
    if (t < 0.1) {                                    // DAT_007E2860 = 0.1
        if (dist < 0x56) {
            if (stop) *stop = true;                   // cur *= 0.5, target 0
            return 0.0;
        }
        t = 0.1;
    }
    return t;
}

double fly_throttle_ramp(double cur, double target, double frames) {
    const double step = 0.1 * frames;
    if (cur < target) return std::min(target, cur + step);
    if (cur > target) return std::max(target, cur - step);
    return cur;
}

int fly_full_speed_lpf(int speed_key) {
    return std::min(255, std::max(0, std::min(100, speed_key)) * 256 / 100);
}

}  // namespace ra2engine
