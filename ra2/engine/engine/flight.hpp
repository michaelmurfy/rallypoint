#pragma once
// Pure CB-2 flight-locomotor logic [RE: docs/re/aircraft.md]: the altitude
// climb/descend integrator (step_altitude), the straight-line cruise advance
// (cruise_step -- never consults passable_/occ_, they are not even parameters),
// the shortest-arc heading turn (heading_toward -- no-op on OmniFire), the
// altitude->screen pixel offset (altitude_to_screen_dy), and the altitude-
// driven state transitions (advance_flight_state). Layered on Unit exactly
// like cargo.hpp/harvest.hpp (world.hpp defines Unit + FlightState; this module
// includes world.hpp; world.hpp does NOT include this back). No SDL/OS.
#include "world.hpp"  // Unit, FlightState, facing_u16_from_delta, turn_toward
// W7 (wave-7 05): the jumpjet + fixed-wing per-frame locomotor models below
// (jumpjet_*, fly_*, step_along_facing) are pure too.

namespace ra2engine {

// Integrate `altitude` (leptons) toward `target` (leptons) by at most
// `max_delta` (leptons this call = rate_per_sec * dt), clamped so it NEVER
// overshoots [RE-GATE APPLIED: aircraft.md §2 -- JumpjetClimb=6 ascend /
// JumpjetCrash=12 descend leptons/frame (CONFIRMED offsets +0xD78/+0xD7C)].
// Returns the new altitude; lands exactly on `target`.
double step_altitude(double altitude, double target, double max_delta);

// Advance (fx, fy) (cell space) in a STRAIGHT LINE toward (gx, gy) by at most
// `max_step` cells this call (= flight_speed_cps * dt). Writes fx/fy in place;
// snaps exactly onto the goal and returns true on arrival, else false. NEVER
// consults passable_/occ_ -- they are not parameters [RE-GATE APPLIED:
// aircraft.md §1 -- straight-line, unpathed flight CONFIRMED (Process is
// atan2 + cos/sin translate, no A*/passable_/cell walk)].
bool cruise_step(double& fx, double& fy, double gx, double gy, double max_step);

// Turn `body_facing` (u16) toward the (dx, dy) travel direction by at most
// `max_turn` u16 this call (= flight_turn_per_sec * dt), shortest arc, exact
// landing (reuses turn_toward + facing_u16_from_delta -- the engine's one
// facing convention). Returns `body_facing` unchanged when `omni_fire` (the
// OmniFire weapon needs no heading) or the direction is degenerate (dx == 0 && dy == 0). W7: the jumpjet and fixed-wing
// steppers pass omni_fire=false -- OmniFire is a weapon property; the
// locomotors always turn toward the goal [RE: FUN_0054BD20 / FUN_004CD520].
// Their rate is JumpJetTurnRate<<8 (jumpjets: Link FUN_0054AA60 ->
// FUN_004C9100) or ROT<<8 (aircraft: FUN_00413F80 -> FUN_004C95A0) u16/frame.
double heading_toward(double body_facing, double dx, double dy,
                      double max_turn, bool omni_fire);

// The §5 raised-sprite vertical pixel offset for a given `altitude` (leptons):
// 0 at altitude 0, monotone increasing [RE-GATE APPLIED: aircraft.md §3 --
// [RE-FALLBACK] (the loco pixel-offset formula was NOT unwound): dy =
// round((altitude/256) * 36.74) (voxel-render.md §6 up/z term); ~107 px at
// JumpjetHeight=750].
int altitude_to_screen_dy(double altitude);

// Altitude-DRIVEN state transitions only (order-driven ones are set by World):
// TakingOff -> Cruising once altitude >= cruise_altitude; Descending ->
// Grounded once altitude <= 0; Grounded/Cruising pass through unchanged.
FlightState advance_flight_state(FlightState state, double altitude,
                                 double cruise_altitude);

// --- W7-05-15 JumpjetLocomotionClass (ILocomotion vtable 0x7EBD68; Link
// FUN_0054AA60, Process FUN_0054ABF0, integrator FUN_0054CDA0, Cruising
// FUN_0054BD20, Crashing FUN_0054C7C0). All quantities are PER LOGIC FRAME.
// (vtable 0x7E9CFC / FUN_00514130 is HoverLocomotionClass, not Jumpjet.)

// The Cruising-state target speed (leptons/frame) at `dist` leptons from the
// goal [RE: FUN_0054BD20]: `speed` = JumpjetSpeed (int, raw leptons/frame),
// `turn_rate` = JumpJetTurnRate, `facing_diff` = the remaining turn in 1/256
// revolutions (facing_diff_256). Tiers: dist < Speed -> Speed/8; < 2*Speed ->
// Speed/4 (Speed/10, at least 1, while facing_diff > TurnRate); < 50*Speed/
// TurnRate -> Speed/2 (Speed/5, at least 1, while facing_diff > 5*TurnRate);
// else Speed. Integer divisions as in the binary. The caller handles arrival
// (dist < 20 leptons).
double jumpjet_cruise_speed(double dist, int speed, int turn_rate, int facing_diff);

// The integrator's speed ramp [RE: FUN_0054CDA0 head]: CurSpeed < target ->
// min(cur + Accel, JumpjetSpeed) (capped at the type's speed, NOT the target);
// target < CurSpeed -> max(cur - 1.5*Accel, 0) (DAT_007E38F0 = 1.5,
// DAT_007E1800 = 0.0); the fall test runs after the rise in the same frame.
// `accel` is JumpJetAccel x the frames this call.
double jumpjet_ramp(double cur, double target, double max_speed, double accel);

// The remaining turn from `from` to `to` (u16 facings) in 1/256 revolutions,
// rounded like the FUN_004C9450 consumer in FUN_0054BD20: ((|d| >> 7) + 1) >> 1
// with d the shortest-arc difference [PROBABLE: the binary masks the value
// with 0xFF, so a negative remainder reads as a large one; the clone takes |d|].
int facing_diff_256(double from, double to);

// Translate (fx, fy) (cells) by `leptons` along the u16 `facing` (the engine
// convention: 0 = -y / North, 16384 = +x) [RE: FUN_0054CDA0 @0x54D1FB --
// x += sin*ftol(CurSpeed), y -= cos*ftol(CurSpeed); the FlyLocomotion step
// FUN_004CD520 @0x4CD9A4 is the same]. The unit moves along its CURRENT body
// facing, not toward the goal.
void step_along_facing(double& fx, double& fy, double facing, double leptons);

// --- W7-05-02 FlyLocomotionClass (ILocomotion vtable 0x7E79F4; Process
// FUN_004CCA60, movement FUN_004CD520, Apparent_Speed FUN_004CFD40) ---

// One frame of height change toward `target` (leptons) for an ordinary
// (IsDropship=no, TechnoType +0xC95) plane [RE: FUN_004CD520 @0x4CDD84-
// 0x4CDEA1]: climbing, +min(target - h, 20); above the target (cruise descent
// AND landing), -clamp(d/20, 20, 50) with d = h - target, never below the
// target here (the binary may undershoot a cruise target by < 20 leptons and
// re-climb next frame; clamped). `frames` scales the per-frame step.
double fly_altitude_step(double h, double target, double frames);

// The throttle target (fraction of the full speed) [RE: FUN_004CD520
// @0x4CE100]: 1.0 unless the plane slows (FUN_004D00A0: not on an attack run
// -- returning / moving / landing). Slowing: min(1, dist/SlowdownDistance);
// below 0.1 it is 0.1, or 0 (stop) within 0x56 leptons. `stop` is set when the
// binary also halves the current throttle (cur *= DAT_007E0738 = 0.5).
double fly_throttle_target(double dist, int slowdown_distance, bool slow, bool* stop);

// The throttle ramp [RE: FUN_004CD520 tail @0x4CE390]: CurrentSpeed moves
// toward TargetSpeed by DAT_007E2860 = 0.1 per frame (x `frames`), exact land.
double fly_throttle_ramp(double cur, double target, double frames);

// The fixed-wing full speed in leptons/frame [RE: Apparent_Speed FUN_004CFD40
// = ftol(TechnoType+0x678 x CurrentSpeed); +0x678 = min(255, Speed*256/100)
// (voxel-render.md §5)] -- no GameSpeedBias.
int fly_full_speed_lpf(int speed_key);

}  // namespace ra2engine
