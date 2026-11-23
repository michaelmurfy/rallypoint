#pragma once
// Rallypoint: Floating Disc drain (YR [DISK] Secondary=DiskDrain, DrainWeapon=yes).
// Pure helpers — no World, Rules or assets. Key names from rulesmd [CombatDamage]
// DrainMoneyFrameDelay / DrainMoneyAmount and building Drainable= /
// ResourceDestination= / Power= (public modenc DrainWeapon).
#include <algorithm>
#include <cstdint>

namespace ra2engine {
namespace disc_drain {

// Stock YR [CombatDamage] values (verified against the player's rulesmd).
constexpr int kDefaultMoneyDelay = 30;   // DrainMoneyFrameDelay=
constexpr int kDefaultMoneyAmount = 30;  // DrainMoneyAmount=

enum class Effect : std::uint8_t {
    None = 0,
    Disable,       // Drainable only (no money, no positive Power)
    Money,         // ResourceDestination=yes
    Power,         // Power= > 0 (blackout the owner while draining)
    MoneyAndPower  // both (rare; treat as money + blackout)
};

// Can a DrainWeapon engage this object? Buildings need Drainable=yes; a
// non-building may be ordered but never yields an effect (modenc).
inline bool can_target(bool drainable, bool is_building) {
    return is_building && drainable;
}

// The Disc's cell is one of the target's foundation cells [RE: DrainStart
// FUN_0070FD70 / UnitClass::AI 0x7361D5: Disc cell -> GetBuilding (FUN_0047C520)
// must be the drainee].
inline bool above_foundation(int firer_cx, int firer_cy, int bx, int by, int fw, int fh) {
    if (fw < 1) fw = 1;
    if (fh < 1) fh = 1;
    return firer_cx >= bx && firer_cx < bx + fw && firer_cy >= by && firer_cy < by + fh;
}

// Classify the drain effect from building type keys. Power uses the rules
// Power= field (positive = generator). ResourceDestination selects money.
inline Effect classify(bool drainable, bool resource_destination, int power) {
    if (!drainable) return Effect::None;
    const bool money = resource_destination;
    const bool power_out = power > 0;
    if (money && power_out) return Effect::MoneyAndPower;
    if (money) return Effect::Money;
    if (power_out) return Effect::Power;
    return Effect::Disable;
}

inline bool causes_blackout(Effect e) {
    return e == Effect::Power || e == Effect::MoneyAndPower;
}

inline bool causes_money(Effect e) {
    return e == Effect::Money || e == Effect::MoneyAndPower;
}

// How many credits move on one money tick. Never more than the victim has;
// never negative. Deterministic integer math (no float).
inline int money_transfer(int victim_credits, int amount) {
    if (amount <= 0 || victim_credits <= 0) return 0;
    return std::min(victim_credits, amount);
}

// [RE: TechnoClass::AI @0x6FA14B..0x6FA1C0, run by the DRAINED building]:
// ResourceDestination=yes and Frame % DrainMoneyFrameDelay == 0 (the global
// frame, not time on target) -> move min(DrainMoneyAmount, owner's money) from
// the owner (FUN_004F9790) to the Disc's house (FUN_004F9950). One payout per
// building per tick, however many Discs hover. `delay` <= 0 is treated as 1.
inline bool money_due(std::uint64_t frame, int delay) {
    const std::uint64_t d = delay < 1 ? 1u : static_cast<std::uint64_t>(delay);
    return frame % d == 0;
}

// [RE: UnitClass::AI @0x7361A9..0x7361E4]: a draining unit checks
// `Frame % 16 == 2` that the building on its own cell is still DrainTarget,
// else DrainEnd. Between checks the drain keeps running off the foundation.
inline bool live_check_due(std::uint64_t frame) { return frame % 16 == 2; }

// [RE: DrainStart FUN_0070FD70]: drainee DrainingMe = Disc, Disc DrainTarget
// = drainee (the latest Disc takes the building's single slot).
inline void start(int& drain_target, int& drained_by, int building, int disc) {
    drain_target = building;
    drained_by = disc;
}

// [RE: DrainEnd FUN_0070FE50]: clears the drainee's DrainingMe whoever holds
// it (a second Disc that took the slot loses it too), then DrainTarget.
inline void end(int& drain_target, int& drained_by) {
    drained_by = -1;
    drain_target = -1;
}

}  // namespace disc_drain
}  // namespace ra2engine
