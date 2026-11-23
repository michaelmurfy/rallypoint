#include "doctest.h"
#include "../engine/ai_boost.hpp"

using namespace ra2engine;

TEST_CASE("ai_boost: clamp and delay/credit multipliers") {
    CHECK(clamp_ai_boost(-1) == 0);
    CHECK(clamp_ai_boost(0) == 0);
    CHECK(clamp_ai_boost(3) == 3);
    CHECK(clamp_ai_boost(9) == 3);

    // Same step curve as Generals AI::boostSpeed (GEN_AI_BOOST).
    CHECK(ai_boost_delay_div(0) == doctest::Approx(1.0));
    CHECK(ai_boost_delay_div(1) == doctest::Approx(1.35));
    CHECK(ai_boost_delay_div(2) == doctest::Approx(1.7));
    CHECK(ai_boost_delay_div(3) == doctest::Approx(2.0));

    CHECK(ai_boost_credit_reserve_mul(0) == doctest::Approx(1.0));
    CHECK(ai_boost_credit_reserve_mul(1) == doctest::Approx(0.75));
    CHECK(ai_boost_credit_reserve_mul(2) == doctest::Approx(0.55));
    CHECK(ai_boost_credit_reserve_mul(3) == doctest::Approx(0.40));
}

TEST_CASE("ai_boost: L2 harvester and defense response multipliers") {
    CHECK(ai_boost_extra_harvesters(0) == 0);
    CHECK(ai_boost_extra_harvesters(1) == 0);
    CHECK(ai_boost_extra_harvesters(2) == 1);
    CHECK(ai_boost_extra_harvesters(3) == 1);

    // ComputerBaseDefenseResponse (Rules+0xB18) scale.
    CHECK(ai_boost_defense_response_mul(0) == doctest::Approx(1.0));
    CHECK(ai_boost_defense_response_mul(1) == doctest::Approx(1.0));
    CHECK(ai_boost_defense_response_mul(2) == doctest::Approx(1.5));
    CHECK(ai_boost_defense_response_mul(3) == doctest::Approx(2.0));

    CHECK(ai_boost_defense_delay_mul(2) == doctest::Approx(0.75));
    CHECK(ai_boost_defense_delay_mul(3) == doctest::Approx(0.5));
}

TEST_CASE("ai_boost: L3 success nudge and compose flag") {
    CHECK(ai_boost_success_weight_nudge(0) == doctest::Approx(0.0));
    CHECK(ai_boost_success_weight_nudge(2) == doctest::Approx(0.0));
    CHECK(ai_boost_success_weight_nudge(3) == doctest::Approx(10.0));
    CHECK_FALSE(ai_boost_compose_counters(2));
    CHECK(ai_boost_compose_counters(3));
}

TEST_CASE("ai_boost: apply_ai_boost mutates TeamDelays / BuildDelay / CreditReserve") {
    // Duck-typed stand-in for AiHouseConfig (header-only apply_ai_boost).
    struct TeamRules {
        int team_delay_frames = 2000;
        double credit_reserve = 100.0;
    };
    struct Team {
        TeamRules rules;
        int harvesters_per_refinery = 2;
    };
    struct Mods {
        double build_delay_min = 0.03;
    };
    struct Ai {
        double credit_reserve = 100.0;
    };
    struct Cfg {
        int ai_boost = 0;
        Team team;
        Mods mods;
        Ai ai;
    };

    Cfg cfg;
    apply_ai_boost(cfg);
    CHECK(cfg.team.rules.team_delay_frames == 2000);
    CHECK(cfg.ai.credit_reserve == doctest::Approx(100.0));

    cfg.ai_boost = 1;
    apply_ai_boost(cfg);
    CHECK(cfg.team.rules.team_delay_frames == static_cast<int>(2000 / 1.35 + 0.5));
    CHECK(cfg.mods.build_delay_min == doctest::Approx(0.03 / 1.35));
    CHECK(cfg.ai.credit_reserve == doctest::Approx(75.0));
    CHECK(cfg.team.rules.credit_reserve == doctest::Approx(75.0));
    CHECK(cfg.team.harvesters_per_refinery == 2);  // L1: no extra miners

    cfg = Cfg{};
    cfg.ai_boost = 2;
    apply_ai_boost(cfg);
    CHECK(cfg.team.harvesters_per_refinery == 3);  // +1 at L2
    CHECK(cfg.ai.credit_reserve == doctest::Approx(55.0));
    CHECK(cfg.team.rules.team_delay_frames == static_cast<int>(2000 / 1.7 + 0.5));
}

TEST_CASE("ai_boost: resolve_ai_boost clamps launch values") {
    CHECK(resolve_ai_boost(0) == 0);
    CHECK(resolve_ai_boost(2) == 2);
    CHECK(resolve_ai_boost(99) == 3);
}
