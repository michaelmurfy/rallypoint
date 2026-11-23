#pragma once
// Yuri Grinder (Grinding=) and Bio Reactor (InfantryAbsorb=) — ModEnc / rulesmd /
// gamemd BuildingType (+0x16AD Grinding, +0x16AF InfantryAbsorb, +0xEE8 ExtraPower).
// YAGRND: Grinding=yes. YAPOWR: InfantryAbsorb=yes, Passengers=5, ExtraPower=100,
// Power=150 (YABRCK is the barracks). Pure helpers; World hooks live in
// world_grinder_bio.cpp. Grind refund is sell_refund (Soylent absolute, else
// Cost * RefundPercent for humans).
#include "building.hpp"
#include "repair_sell.hpp"

namespace ra2engine {
namespace grinder_bio {

// Power_Output for an InfantryAbsorb plant: Power + ExtraPower * occupants
 // (ExtraPower <= 0 adds nothing). Health scaling is left to power_totals.
inline int absorb_power(int power, int extra_power, int occupants) {
    if (extra_power > 0 && occupants > 0) power += extra_power * occupants;
    return power;
}

inline int grind_refund(int cost, int soylent, double refund_percent, bool owner_is_human) {
    return sell_refund(cost, soylent, refund_percent, owner_is_human);
}

inline bool type_grinds(const BuildingTypeDef& t) { return t.grinding; }

inline bool type_absorbs(const BuildingTypeDef& t) {
    return t.infantry_absorb && t.passengers > 0;
}

inline bool absorb_has_room(const BuildingTypeDef& t, const Building& b) {
    return type_absorbs(t) && static_cast<int>(b.occupants.size()) < t.passengers;
}

}  // namespace grinder_bio
}  // namespace ra2engine
