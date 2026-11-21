#pragma once
// Engineer/Spy Capture phase [RE: docs/re/capture.md]: pure, SDL/World/RNG-free
// arithmetic shared by World::capture_building / infiltrate_building and the
// gate. World owns the mutation entry points; this header owns only the numeric
// helpers so they are host-doctestable without a World. No RNG (the hunt §E
// CONFIRMED no RNG on any capture/infiltrate/cash path -> the gate is
// bit-deterministic).
#include <cstdint>
#include "production.hpp"  // PowerTotals (blackout wrapper operates on it)

namespace ra2engine {

// The NEUTRAL house owner id [RE: capture.md §D1]. Buildings only; per-house
// arrays are widened to 3 with index 2 inert (no credits/units/AI for neutral).
constexpr int kNeutralHouse = 2;

// Spy cash steal = ftol(SpyMoneyStealPercent * victim credits) [RE: capture.md
// §B3 CONFIRMED, SpyMoneyStealPercent=0.5]. Truncates toward zero (the binary
// _ftol), never negative. Default pct 0.5 documents the recovered value; the
// caller passes the resolved rules value.
inline int cash_steal_amount(double victim_credits, double steal_pct = 0.5) {
    if (victim_credits <= 0.0 || steal_pct <= 0.0) return 0;
    double v = victim_credits * steal_pct;
    return v <= 0.0 ? 0 : static_cast<int>(v);   // trunc toward zero
}

// Power blackout [RE: capture.md §B2 CONFIRMED, SpyPowerBlackout=1000 frames].
// A pure wrapper over the UNTOUCHED power_totals: when a house is blacked out
// its power OUTPUT is forced to 0 (drain unaffected) -> low-power build clamp +
// offline defenses follow from the existing build_speed_multiplier. Identity
// when !blacked_out, so every existing caller that never blacks out is
// byte-identical (production.cpp is not edited).
inline PowerTotals apply_blackout(PowerTotals pt, bool blacked_out) {
    if (blacked_out) pt.output = 0;
    return pt;
}

// ProduceCashStartup is granted to the NEW owner ONLY when captured from the
// neutral house [RE: capture.md §A3 CONFIRMED - gated on the OLD owner being
// neutral].
inline bool grants_startup_from_neutral(int old_house) { return old_house == kNeutralHouse; }

// Minimal veterancy [RE-GATE APPLIED: capture.md §B5]. A rank-1 veteran unit
// gets its firepower and toughness (HP) scaled at SPAWN (combat.cpp is
// untouched; a veteran simply spawns with higher w_damage + hp). rank 0 -> the
// value is returned unchanged (byte-identical). Multipliers PINNED at the
// RE-GATE from rulesmd [General]: VeteranCombat=1.1 (firepower), VeteranArmor=1.5
// (toughness/HP); rank 1 = veteran, VeteranCap=2 (elite is a carry-forward).
constexpr double kVeteranFirepowerMult = 1.1;  // [RE-GATE APPLIED: capture.md §B5 — VeteranCombat]
constexpr double kVeteranArmorMult     = 1.5;  // [RE-GATE APPLIED: capture.md §B5 — VeteranArmor]
inline int veteran_firepower(int base_damage, int rank) {
    if (rank <= 0) return base_damage;
    double v = base_damage * kVeteranFirepowerMult;
    return static_cast<int>(v + 0.5);   // round to nearest
}
inline int veteran_hp(int base_hp, int rank) {
    if (rank <= 0) return base_hp;
    double v = base_hp * kVeteranArmorMult;
    return static_cast<int>(v + 0.5);
}

// Disguise detection radius [RE-FALLBACK: capture.md §C4 DetectDisguiseRange
// default]. A disguised unit within this many cells of an alive, non-dying
// ENEMY DetectDisguise unit (a dog) is revealed (disguise cleared). Deterministic
// (no RNG -- §C5: dog-vs-PermaDisguise is a flag test, not a roll).
constexpr double kDetectDisguiseRange = 6.0;  // cells

}  // namespace ra2engine
