#pragma once
// [RP-AI-BOOST] Rallypoint optional smarter skirmish AI layer.
// Off by default. Levels 1..3 scale the stock three-layer brain's timers and
// knobs (TeamDelays / DifficultyClass BuildDelay / CreditReserve / IQ Harvester
// / ComputerBaseDefenseResponse) without replacing aimd or AITriggers.
// Multipliers mirror Generals GEN_AI_BOOST speed steps where comparable; the
// credit / defense / composition extras are Rallypoint-only expansions.
#include <cstdlib>
#include <algorithm>

namespace ra2engine {

inline int clamp_ai_boost(int level) {
    if (level < 0) return 0;
    if (level > 3) return 3;
    return level;
}

// Launch key AIBoost= wins when present (>= 0); else RA2_AI_BOOST; else 0.
inline int resolve_ai_boost(int launch_value /* -1 = absent */) {
    if (launch_value >= 0) return clamp_ai_boost(launch_value);
    const char* e = std::getenv("RA2_AI_BOOST");
    if (!e || !*e) return 0;
    return clamp_ai_boost(std::atoi(e));
}

// Level 1+: divide TeamDelays / BuildDelay (gamemd DifficultyClass BuildDelay=
// minutes; TeamDelays= CSV frames). Same step curve as Generals AI::boostSpeed.
inline double ai_boost_delay_div(int level) {
    static const double k[4] = {1.0, 1.35, 1.7, 2.0};
    return k[clamp_ai_boost(level)];
}

// Level 1+: lower CreditReserve floor -> higher production pressure (AI only).
// [RP-AI-BOOST] Expansion: stock CreditReserve=100 is a hard floor, not a
// DifficultyClass field; we scale it rather than inventing a second brain.
inline double ai_boost_credit_reserve_mul(int level) {
    static const double k[4] = {1.0, 0.75, 0.55, 0.40};
    return k[clamp_ai_boost(level)];
}

// Level 2+: extra miners beyond HarvestersPerRefinery (gamemd General CSV).
inline int ai_boost_extra_harvesters(int level) {
    static const int k[4] = {0, 0, 1, 1};
    return k[clamp_ai_boost(level)];
}

// Level 2+: multiply ComputerBaseDefenseResponse (Rules+0xB18) for call_for_help.
inline double ai_boost_defense_response_mul(int level) {
    static const double k[4] = {1.0, 1.0, 1.5, 2.0};
    return k[clamp_ai_boost(level)];
}

// Level 2+: shorten BaseDefenseDelay cooldown after a help call (frames).
inline double ai_boost_defense_delay_mul(int level) {
    static const double k[4] = {1.0, 1.0, 0.75, 0.5};
    return k[clamp_ai_boost(level)];
}

// Level 3: additive nudge on AITrigger RegisterSuccess (stock delta is +20).
// [RP-AI-BOOST] Expansion: keeps the adaptive track-record rule, only biases
// toward re-picking a successful attack composition.
inline double ai_boost_success_weight_nudge(int level) {
    return clamp_ai_boost(level) >= 3 ? 10.0 : 0.0;
}

inline bool ai_boost_compose_counters(int level) {
    return clamp_ai_boost(level) >= 3;
}

// Apply L1/L2 timer and economy knobs onto a computer house's resolved config.
// Caller sets cfg.ai_boost first. Safe at level 0 (no-op).
template <class AiHouseConfigT>
inline void apply_ai_boost(AiHouseConfigT& cfg) {
    const int level = clamp_ai_boost(cfg.ai_boost);
    if (level <= 0) return;
    const double delay_div = ai_boost_delay_div(level);
    const double credit_mul = ai_boost_credit_reserve_mul(level);

    // TeamDelays frames (gamemd [General] TeamDelays= CSV at house+0x184).
    {
        const int frames = cfg.team.rules.team_delay_frames;
        cfg.team.rules.team_delay_frames =
            std::max(1, static_cast<int>(frames / delay_div + 0.5));
    }
    // DifficultyClass BuildDelay= minutes between AI build passes.
    cfg.mods.build_delay_min = cfg.mods.build_delay_min / delay_div;

    cfg.ai.credit_reserve = cfg.ai.credit_reserve * credit_mul;
    cfg.team.rules.credit_reserve = cfg.team.rules.credit_reserve * credit_mul;

    if (level >= 2) {
        // IQ Harvester path: keep more miners per refinery.
        if (cfg.team.harvesters_per_refinery > 0)
            cfg.team.harvesters_per_refinery += ai_boost_extra_harvesters(level);
    }
}

}  // namespace ra2engine
