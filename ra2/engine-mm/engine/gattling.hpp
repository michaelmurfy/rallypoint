#pragma once
// Rallypoint: Yuri's Revenge IsGattling weapon stages (ModEnc "Gattling Weapon
// System"). Pure integers: a firing timer climbs by RateUp while engaging and
// falls by RateDown when idle (RateDown=0 clears). Stage1..N are endpoints;
// the last is the timer cap. Odd WeaponX = anti-ground, even = anti-air.
// EliteStageX / EliteWeaponX replace the base ones when the techno is elite.

#include <algorithm>
#include <vector>

#include "weapon.hpp"

namespace ra2engine {

inline constexpr int kGattlingMaxStages = 8;

struct GattlingRules {
    bool enabled = false;
    int rate_up = 0;
    int rate_down = 0;
    int weapon_stages = 0;                 // WeaponStages= (pairs of weapons)
    int stage_end[kGattlingMaxStages] = {};       // Stage1..N
    int elite_stage_end[kGattlingMaxStages] = {}; // EliteStage1..N
    bool has_elite_stages = false;
};

// Timer after one logic frame. `engaging` = has a live attack target.
// Caps at the last Stage endpoint (StageMax). RateDown=0 resets when idle.
inline int gattling_tick(int timer, bool engaging, const GattlingRules& r) {
    if (!r.enabled || r.weapon_stages <= 0) return 0;
    const int cap = r.stage_end[std::max(0, r.weapon_stages - 1)];
    if (engaging) {
        long long t = static_cast<long long>(timer) + r.rate_up;
        if (t < 0) t = 0;
        if (t > cap) t = cap;
        return static_cast<int>(t);
    }
    if (r.rate_down == 0) return 0;
    long long t = static_cast<long long>(timer) - r.rate_down;
    if (t < 0) t = 0;
    return static_cast<int>(t);
}

// 0-based stage from the timer and the (elite) endpoints. Monotonic YR tables
// (Stage1 < Stage2 < Stage3) map timer in [StageK, StageK+1) to stage K.
inline int gattling_stage(int timer, const GattlingRules& r, bool elite) {
    const int n = r.weapon_stages;
    if (n <= 0) return 0;
    const int* ends = (elite && r.has_elite_stages) ? r.elite_stage_end : r.stage_end;
    int s = 0;
    for (int i = 0; i < n - 1; ++i) {
        if (timer >= ends[i]) s = i + 1;
    }
    if (s >= n) s = n - 1;
    return s;
}

// 0-based WeaponX index: stage 0 -> Weapon1/2, stage 1 -> Weapon3/4, ...
inline int gattling_weapon_index(int stage, bool air) {
    if (stage < 0) stage = 0;
    return stage * 2 + (air ? 1 : 0);
}

// Pick a slot from a loaded weapon list; invalid elite falls back to base.
inline const WeaponSlot& gattling_pick(const std::vector<WeaponSlot>& base,
                                      const std::vector<WeaponSlot>& elite_w, int index,
                                      bool elite) {
    static const WeaponSlot kEmpty;
    if (index < 0) return kEmpty;
    const std::size_t i = static_cast<std::size_t>(index);
    if (elite && i < elite_w.size() && elite_w[i].valid) return elite_w[i];
    if (i < base.size() && base[i].valid) return base[i];
    return kEmpty;
}

}  // namespace ra2engine
