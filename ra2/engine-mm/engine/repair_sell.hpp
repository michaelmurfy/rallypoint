#pragma once
// Pure Repair & Sell arithmetic and predicates, recovered from YR-1.001
// gamemd.exe -- docs/re/repair-sell.md (§ refs inline). World-free and
// SDL-free: this TU sees only Building/BuildingTypeDef, exactly like
// production.cpp and factory.cpp, so every number below is host-doctestable.
//
// THE THREE TRAPS this file exists to make impossible to re-introduce:
//   1. the repair interval is trunc(RepairRate*900) = 14, NOT 15 (__ftol
//      truncates 14.4) and it is a GLOBAL frame phase, not a per-building timer;
//   2. stepCost uses TWO INTEGER divisions -- the naive
//      cost*percent*damage_fraction form is ~17% wrong on a Construction Yard;
//   3. the sell refund has NO health term at all.
#include <cstdint>

#include "building.hpp"

namespace ra2engine {

// The [General] values this phase reads. The DEFAULTS ARE this install's real
// rulesmd.ini values [RE: repair-sell.md §Ground truth / §A1 / §D9], so a World
// that never calls set_repair_sell_rules already behaves like the original.
// NOTE [§Ground truth, CONFIRMED]: URepairStep and URepairPercent DO NOT EXIST
// in this build -- there is exactly one RepairStep (buildings + vehicles), one
// IRepairStep (infantry) and one RepairPercent. SurvivorRate is present in the
// INI but DEAD (never read by the engine); the divisors are the whole story.
struct RepairSellRules {
    int    repair_step    = 8;      // RepairStep    (Rules+0x16CC) [§A1]
    double repair_percent = 0.15;   // RepairPercent (Rules+0x16D0) [§A1]
    double repair_rate    = 0.016;  // RepairRate    (Rules+0x16E0) [§A1] minutes/step
    double refund_percent = 0.50;   // RefundPercent (Rules+0x1738) [§A1]
    // W7-08-03 [RE: BuildingClass::Mission_Repair 0x44B780 @0x44BD38]: the
    // Service Depot steps on its OWN accumulator every URepairRate minutes.
    double u_repair_rate  = 0.016;  // URepairRate   (Rules+0x16E8) (save v35)
    int    allied_survivor_divisor = 500;   // Rules+0x14F8 [§B3]
    int    soviet_survivor_divisor = 250;   // Rules+0x14FC [§B3]
    int    third_survivor_divisor  = 750;   // Rules+0x1500 [§B3]
};

// interval = trunc(RepairRate * 900.0) frames [RE: §A2, CONFIRMED].
// TRAP: 0.016 * 900 = 14.4 and __ftol TRUNCATES -> 14, not 15 (a 7% speed
// difference). Floored at 1 so a zero/negative rate can never divide by zero.
int repair_interval_frames(double repair_rate);

// The tick fires iff Frame % interval == 0 [RE: §A2, CONFIRMED]. This is a
// GLOBAL PHASE: every repairing building of every house steps on the SAME
// frames, and a building that starts repairing waits 0..interval-1 frames for
// its first step. It does NOT get its own timer.
bool repair_due(std::uint64_t frame, int interval);

// stepCost = max(1, trunc((Cost / (Strength / RepairStep)) * RepairPercent))
// [RE: §A2, CONFIRMED -- FUN_00711470, whole function].
// BOTH divisions are INTEGER and truncating; only then does the RepairPercent
// multiply happen, and __ftol truncates that too. Worked from the real rules
// (RepairStep=8, RepairPercent=0.15) using the §D9 MEASURED table: GAPOWR
// 800/750 -> 1 [RE-GATE APPLIED §D0.1: Strength is 750, NOT 1000 -- 750/8=93,
// 800/93=8, trunc(8*0.15)=1], GACNST 3000/1000 -> 3 (375 credits to full),
// GAWEAP 2000/1000 -> 2 (250 to full), GATECH 2000/500 -> 4 (the roster's max).
// Do NOT "simplify" to cost*percent*damage_fraction: that yields 450 for a
// GACNST, ~17% wrong. Depends only on the TYPE, so callers may cache it.
// The per == 0 / repair_step == 0 guards are OURS and load-bearing [§D0.4]:
// GATOW has no rulesmd section at all (Cost 0, Strength 0) and the original's
// IDIV would raise #DE -- unreachable there, reachable here.
int repair_step_cost(int cost, int strength, int repair_step, double repair_percent);

// refund [RE: §B1, CONFIRMED -- FUN_00711300 GetRefund + FUN_0070A220]:
//   Soylent != 0     -> trunc(Soylent * country_mult)          (RefundPercent NOT applied)
//   human owner      -> trunc(trunc(Cost*house_mult*country_mult) * RefundPercent)
//   AI owner         -> trunc(Cost*house_mult*country_mult)    (UNSCALED -- a real asymmetry)
// THERE IS NO HEALTH TERM. A building at 1 HP refunds exactly what a full-
// health one does. The two multipliers are the FactoryPlant/OrePurifier/
// difficulty modifiers, 1.0 in a default skirmish; they are parameters so a
// later phase can supply them without changing this signature.
int sell_refund(int cost, int soylent, double refund_percent, bool owner_is_human,
                double country_mult = 1.0, double house_mult = 1.0);

// The per-SIDE survivor divisor [RE: §B3, CONFIRMED]: side_ix is
// World::Side's underlying value -- 0 Allied (500), 1 Soviet (250),
// 2 Yuri/Third (750). Anything else (Neutral) returns 0 == "no survivors".
int survivor_divisor(const RepairSellRules& r, int side_ix);

// survivors = clamp(refund / divisor, 1, 5), integer division of the REFUND
// [RE: §B3, CONFIRMED -- FUN_00451320]. Zero when the type is not Crewed (or
// the instance has no crew). The divisor is DOUBLED for a captured building.
// SurvivorRate is never read (dead key). Worked from §D9: GACNST refund 1500 ->
// Allied 3, Soviet 5 (clamped from 6), Yuri 2; GAPOWR is Crewed=YES with refund
// 400 -> 1/1 [RE-GATE APPLIED §D0.1: the hunt recorded Crewed=no, which was
// WRONG]; GAREFN's Soylent refund of 300 -> 1/1, not the Cost-derived 2/4.
int survivor_count(int refund, int divisor, bool crewed, bool has_been_captured);

// Can_Be_Click_Repaired [RE: §A4, CONFIRMED -- FUN_00452620]: alive, not dead,
// not still building (the "in limbo / being built" test), the TYPE opts in via
// ClickRepairable (default yes), and there is damage to undo. OWNERSHIP is NOT
// tested here -- World::set_repair does that (mirroring the network event).
bool can_be_click_repaired(const Building& b, const BuildingTypeDef& t);

// Can_Be_Sold [RE: §B5, CONFIRMED -- FUN_00449410 + Sell_Back's own C4 test].
// Refused for, and ONLY for: Unsellable types; buildings still under
// construction (BState == 0 / mission Construction); buildings already selling
// (mission 0x13); and buildings with C4 planted.
//
// THERE IS NO LAST-CONSTRUCTION-YARD RULE AND NO CAPTURED-BUILDING RULE.
// Both were searched for in the binary and neither exists (§B5, "CONFIRMED by
// absence"): HasBeenCaptured affects only the survivor divisor and the AI
// repair trigger, never sellability. DO NOT ADD EITHER.
//
// c4_applied is an explicit parameter because the clone has no C4 mechanic
// until roadmap P4 -- every caller passes false today, and P4 wires it in one
// line without touching this predicate. Ownership is enforced by
// World::begin_sell (the SELL event's own check, §C1).
bool can_be_sold(const Building& b, const BuildingTypeDef& t, bool c4_applied);

// The sell animation's frame [RE: §B2, CONFIRMED -- BuildingClass::Shape_Number
// FUN_0043EF10]: during mission 0x13 the index is MIRRORED inside the BState-0
// buildup range, `(start + count - stage) - 1`, i.e. the buildup runs BACKWARDS
// over the same frame count at the same rate. Deliberately the exact mirror of
// building.cpp's buildup_frame, reading sell_time instead of buildup_time, so
// the two provably share one period (buildup_total / image_frames).
int mirrored_buildup_frame(const Building& b, int buildup_shp_frame_count);

}  // namespace ra2engine
