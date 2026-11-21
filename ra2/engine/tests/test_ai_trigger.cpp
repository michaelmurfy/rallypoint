#include "doctest.h"
#include "../engine/ai_trigger.hpp"
using namespace ra2engine;

namespace {
// One-trigger AimdData: TT0 = a Max=1 team; TRG0 = enemy owns >= 2 of
// building 0, weights 50/[10,90], all difficulties, skirmish, any side/owner.
AimdData one_trigger_aimd() {
    AimdData d;
    TaskForce tf; tf.id = "TF"; tf.members = {{1, 0}};
    d.taskforces.push_back(tf);
    ScriptType sc; sc.id = "SC"; sc.actions = {{49, 0}};
    d.scripts.push_back(sc);
    TeamType tt; tt.id = "TT0"; tt.taskforce = 0; tt.script = 0; tt.max = 1;
    d.teams.push_back(tt);
    AITriggerType t;
    t.id = "TRG0"; t.name = "enemy owns 2 bld0";
    t.team1 = 0; t.team2 = -1;
    t.owner_house = "<all>"; t.tech_level = 0;
    t.condition_type = kCondEnemyOwns;
    t.condition_object = TriggerObject{TechnoKind::Building, 0};
    t.comparator = TriggerComparator{2, kCompGreaterEq};
    t.starting_weight = 50.0; t.min_weight = 10.0; t.max_weight = 90.0;
    t.is_for_skirmish = true; t.side = 0; t.base_defense = false;
    t.enabled[0] = t.enabled[1] = t.enabled[2] = true;
    t.is_enabled = true;    // [AITriggerTypesEnable] default [RE: §5]
    d.triggers.push_back(t);
    return d;
}
// A view where TRG0's condition is TRUE (enemy owns 3 of building 0).
// Soviet house (side 2), Normal difficulty index 1, no neutral house.
TriggerView firing_view() {
    TriggerView v;
    v.own_units = {0}; v.enemy_units = {0};
    v.own_buildings = {0}; v.enemy_buildings = {3};
    v.difficulty = 1;
    v.house_name = "Russians"; v.side = 2; v.tech_level = 10;
    return v;
}
}  // namespace

TEST_CASE("ai_trigger: compare_count implements all six operators") {
    CHECK(compare_count(1, {2, kCompLess}));
    CHECK_FALSE(compare_count(2, {2, kCompLess}));
    CHECK(compare_count(2, {2, kCompLessEq}));
    CHECK(compare_count(2, {2, kCompEq}));
    CHECK_FALSE(compare_count(3, {2, kCompEq}));
    CHECK(compare_count(2, {2, kCompGreaterEq}));
    CHECK_FALSE(compare_count(1, {2, kCompGreaterEq}));
    CHECK(compare_count(3, {2, kCompGreater}));
    CHECK_FALSE(compare_count(2, {2, kCompGreater}));
    CHECK(compare_count(3, {2, kCompNotEq}));
    CHECK_FALSE(compare_count(2, {2, kCompNotEq}));
    CHECK_FALSE(compare_count(2, {2, 99}));   // unknown operator -> FALSE
}

TEST_CASE("ai_trigger: condition dispatch over the recovered -1..7 enum") {
    AimdData d = one_trigger_aimd();
    TriggerView v = firing_view();
    AITriggerType t = d.triggers[0];
    // ct=0 EnemyHouseOwns (building object): 3 >= 2 -> TRUE; 1 >= 2 -> FALSE.
    CHECK(trigger_condition_true(t, v));
    v.enemy_buildings[0] = 1;
    CHECK_FALSE(trigger_condition_true(t, v));
    // ct=0 over a UNIT object.
    t.condition_object = TriggerObject{TechnoKind::Unit, 0};
    v.enemy_units[0] = 5;
    CHECK(trigger_condition_true(t, v));
    // ct=1 OwnerHouseOwns.
    t.condition_type = kCondOwnerOwns;
    v.own_units[0] = 2;
    CHECK(trigger_condition_true(t, v));
    v.own_units[0] = 1;
    CHECK_FALSE(trigger_condition_true(t, v));
    // ct=-1 kCondNone: always TRUE, no object consulted.
    t.condition_type = kCondNone;
    t.condition_object = TriggerObject{};
    CHECK(trigger_condition_true(t, v));
    // ct=4 HouseCredits: OWNER credits OP N — a REAL compare [RE-GATE
    // APPLIED: ai-trigger.md §2 — NOT enemy credits]. Stock shape
    // (N=0, op >=) is always true.
    t.condition_type = kCondHouseCredits;
    t.comparator = TriggerComparator{0, kCompGreaterEq};
    v.credits = 0.0;
    CHECK(trigger_condition_true(t, v));       // the stock always-true shape
    t.comparator = TriggerComparator{1000, kCompGreaterEq};
    v.credits = 999.0;
    CHECK_FALSE(trigger_condition_true(t, v));
    v.credits = 1000.0;
    CHECK(trigger_condition_true(t, v));
    // ct=7 CivilianHouseOwns: counts the NEUTRAL house; FALSE while no
    // neutral house exists (empty neutral slices) [RE: ai-trigger.md §2].
    t.condition_type = kCondCivilianHouseOwns;
    t.condition_object = TriggerObject{TechnoKind::Building, 0};
    t.comparator = TriggerComparator{1, kCompGreaterEq};
    CHECK_FALSE(trigger_condition_true(t, v));  // no neutral house
    v.neutral_buildings = {2};
    CHECK(trigger_condition_true(t, v));
    v.neutral_buildings = {0};
    CHECK_FALSE(trigger_condition_true(t, v));
    // Inert values evaluate FALSE [RE: ai-trigger.md §2]: ct=2/3 (unused
    // inline house-metric), ct=5/6 without the superweapon, >= 8 (rejected).
    v.neutral_buildings = {5};
    for (int cond : {kCondHouseMetric2, kCondHouseMetric3,
                     kCondIronCurtainCharged, kCondChronoSphereCharged,
                     8, 99}) {
        t.condition_type = cond;
        CHECK_FALSE(trigger_condition_true(t, v));
    }
    // W6-03-05 [RE: FUN_0041f0f0 / FUN_0041f1a0]: ct=5/6 are TRUE once the
    // owner's Curtain / Sphere is at least AIMinorSuperReadyPercent charged.
    v.minor_ready = 0.7;
    t.condition_type = kCondIronCurtainCharged;
    v.iron_charge = 0.5;
    CHECK_FALSE(trigger_condition_true(t, v));
    v.iron_charge = 0.7;
    CHECK(trigger_condition_true(t, v));
    v.iron_charge = 1.0;
    CHECK(trigger_condition_true(t, v));
    t.condition_type = kCondChronoSphereCharged;
    CHECK_FALSE(trigger_condition_true(t, v));   // no Sphere
    v.chrono_charge = 0.75;
    CHECK(trigger_condition_true(t, v));
    // Untestable counting condition (object None / out-of-range) -> FALSE.
    t.condition_type = kCondEnemyOwns;
    t.comparator = TriggerComparator{2, kCompGreaterEq};
    t.condition_object = TriggerObject{};
    CHECK_FALSE(trigger_condition_true(t, v));
    t.condition_object = TriggerObject{TechnoKind::Building, 99};
    CHECK_FALSE(trigger_condition_true(t, v));
}

TEST_CASE("ai_trigger: the enable-filter matrix") {
    AimdData d = one_trigger_aimd();
    TriggerView v = firing_view();
    AITriggerType t = d.triggers[0];
    CHECK(trigger_filters_pass(t, v));
    // #1 base-defense split: offensive scope -> base-defense triggers
    // rejected [RE-FALLBACK: no defensive-need input; all stock are
    // IsForBaseDefense=0].
    t.base_defense = true;
    CHECK_FALSE(trigger_filters_pass(t, v));
    t.base_defense = false;
    // #3 [AITriggerTypesEnable] per-trigger flag [RE: ai-trigger.md §5].
    t.is_enabled = false;
    CHECK_FALSE(trigger_filters_pass(t, v));
    t.is_enabled = true;
    // #4 IsForSkirmish must be 1 (skirmish-only engine) [RE: §5].
    t.is_for_skirmish = false;
    CHECK_FALSE(trigger_filters_pass(t, v));
    t.is_for_skirmish = true;
    // #5 per-difficulty enable INVERTED [RE-GATE APPLIED: ai-trigger.md §5 —
    // house difficulty index 0 = HARDEST -> Enabled_Hard = enabled[2];
    // 1 -> enabled[1]; 2 -> Enabled_Easy = enabled[0]].
    t.enabled[1] = false;            // Normal slot; view difficulty 1 ->
    CHECK_FALSE(trigger_filters_pass(t, v));  // enabled[2-1] (inversion-
    t.enabled[1] = true;                      // neutral middle)
    v.difficulty = 0;                // HARDEST -> consults enabled[2] (Hard)
    t.enabled[2] = false; t.enabled[0] = true;
    CHECK_FALSE(trigger_filters_pass(t, v));
    t.enabled[2] = true; t.enabled[0] = false;
    CHECK(trigger_filters_pass(t, v));  // Easy flag off does NOT gate hardest
    v.difficulty = 2;                // EASIEST -> consults enabled[0] (Easy)
    CHECK_FALSE(trigger_filters_pass(t, v));
    t.enabled[0] = true;
    CHECK(trigger_filters_pass(t, v));
    v.difficulty = 1;
    // #7 SideIndex: 0=any, 1=Allied, 2=Soviet, 3=Yuri; the view is a Soviet
    // house (side 2) [RE: §5 — CONFIRMED 1-based, 0=any].
    t.side = 1;                      // Allied trigger, Soviet house
    CHECK_FALSE(trigger_filters_pass(t, v));
    t.side = 3;                      // Yuri trigger, Soviet house
    CHECK_FALSE(trigger_filters_pass(t, v));
    t.side = 2;
    CHECK(trigger_filters_pass(t, v));
    t.side = 0;
    // #6 OwnerHouse: <all> = Any -> pass; country name -> case-insensitive
    // match; <none>/"" -> REJECT (OwnerHouseType 0) [RE-GATE APPLIED:
    // ai-trigger.md §5 — <none> is NOT "any"].
    t.owner_house = "Russians";
    CHECK(trigger_filters_pass(t, v));
    t.owner_house = "RUSSIANS";
    CHECK(trigger_filters_pass(t, v));
    t.owner_house = "Americans";
    CHECK_FALSE(trigger_filters_pass(t, v));
    t.owner_house = "<none>";
    CHECK_FALSE(trigger_filters_pass(t, v));
    t.owner_house = "";
    CHECK_FALSE(trigger_filters_pass(t, v));
    t.owner_house = "<all>";
    // #8 TechLevel gate: house tech level >= field.
    t.tech_level = 11;
    CHECK_FALSE(trigger_filters_pass(t, v));
    t.tech_level = 10;
    CHECK(trigger_filters_pass(t, v));
    // Eligibility = condition && filters.
    CHECK(trigger_eligible(t, v));
    v.enemy_buildings[0] = 0;
    CHECK_FALSE(trigger_eligible(t, v));
}

TEST_CASE("ai_trigger: selection - sole eligible, determinism, caps, none") {
    AimdData d = one_trigger_aimd();
    TriggerView v = firing_view();
    std::vector<TeamInstance> live;
    // Sole eligible trigger is picked with probability 1 across a seed sweep
    // (r = rand(1, 50) always lands inside its cumulative span).
    for (std::uint32_t seed = 1; seed <= 100; ++seed) {
        std::uint32_t rng = seed;
        std::vector<double> w = {50.0};
        const TriggerDecision td = select_trigger(d, w, live, v, rng);
        CHECK(td.trigger == 0);
        CHECK(td.teamtype1 == 0);
        CHECK(td.teamtype2 == -1);    // Team2 "<none>" -> no second create
    }
    // None eligible -> {-1,-1,-1} (condition false).
    TriggerView cold = v;
    cold.enemy_buildings[0] = 0;
    std::uint32_t rng = 7u;
    CHECK(select_trigger(d, {50.0}, live, cold, rng).trigger == -1);
    // round(weight) <= 0 -> skipped (no probability mass) [RE: §3 — the
    // draw runs over round(Weight_Current)].
    CHECK(select_trigger(d, {0.0}, live, v, rng).trigger == -1);
    CHECK(select_trigger(d, {0.4}, live, v, rng).trigger == -1);  // rounds to 0
    // Team1 Max cap: TT0 has max=1; one live instance blocks the trigger —
    // the cap sits INSIDE eligibility, before the draw [RE-GATE APPLIED:
    // ai-trigger.md §5/§6].
    TeamInstance ti; ti.teamtype = 0;
    live.push_back(ti);
    CHECK(select_trigger(d, {50.0}, live, v, rng).trigger == -1);
    live.clear();
    // Max < 0 = UNCAPPED [RE-GATE APPLIED: ai-trigger.md §5/§6 — CONFIRMED].
    d.teams[0].max = -1;
    live.push_back(ti); live.push_back(ti);
    CHECK(select_trigger(d, {50.0}, live, v, rng).trigger == 0);
    // Team2 rides the SAME cap: a Team2 at its Max blocks the whole trigger;
    // under the cap, BOTH team indices come back [RE: §3/§6].
    live.clear();
    TeamType tt1 = d.teams[0]; tt1.id = "TT1"; tt1.max = 1;
    d.teams.push_back(tt1);
    d.triggers[0].team2 = 1;
    TeamInstance t2i; t2i.teamtype = 1;
    live.push_back(t2i);
    CHECK(select_trigger(d, {50.0}, live, v, rng).trigger == -1);
    live.clear();
    const TriggerDecision both = select_trigger(d, {50.0}, live, v, rng);
    CHECK(both.trigger == 0);
    CHECK(both.teamtype1 == 0);
    CHECK(both.teamtype2 == 1);       // BOTH Team1 and Team2 created [RE: §3]
}

TEST_CASE("ai_trigger: selection determinism, weight convergence, 5000 tier") {
    // Two eligible triggers over the same team-less shapes.
    AimdData d = one_trigger_aimd();
    d.teams[0].max = -1;                       // uncapped for this case
    AITriggerType second = d.triggers[0];
    second.id = "TRG1";
    d.triggers.push_back(second);
    TriggerView v = firing_view();
    std::vector<TeamInstance> live;
    // Same seed -> identical pick sequence.
    std::vector<int> seq1, seq2;
    std::uint32_t r1 = 0x2545F491u, r2 = 0x2545F491u;
    for (int i = 0; i < 10; ++i) {
        seq1.push_back(select_trigger(d, {50.0, 50.0}, live, v, r1).trigger);
        seq2.push_back(select_trigger(d, {50.0, 50.0}, live, v, r2).trigger);
    }
    CHECK(seq1 == seq2);
    // Dominant weight: with rounded weights 90 vs 10, the dominant trigger's
    // empirical pick frequency tracks the ratio across a seed sweep
    // (>= 70% is a safe bound for a 90/10 split over 500 seeds).
    int dominant = 0;
    for (std::uint32_t seed = 1; seed <= 500; ++seed) {
        std::uint32_t rng = seed;
        if (select_trigger(d, {90.0, 10.0}, live, v, rng).trigger == 0)
            ++dominant;
    }
    CHECK(dominant >= 350);
    CHECK(dominant < 500);   // the low-weight trigger IS reachable
    // The 5000 MANDATORY tier [RE-GATE APPLIED: ai-trigger.md §3 —
    // CONFIRMED]: an eligible trigger whose ROUNDED weight == 5000 resets
    // the candidate set to the 5000 tier only — the 90-weight trigger is
    // NEVER picked, any seed.
    for (std::uint32_t seed = 1; seed <= 200; ++seed) {
        std::uint32_t rng = seed;
        CHECK(select_trigger(d, {90.0, 5000.0}, live, v, rng).trigger == 1);
    }
    // Rounding feeds the tier check: 4999.6 rounds to 5000.
    std::uint32_t rr = 3u;
    CHECK(select_trigger(d, {90.0, 4999.6}, live, v, rr).trigger == 1);
}

TEST_CASE("ai_trigger: weight dynamics - additive +20/-50 track-record rule, clamped") {
    AimdData d = one_trigger_aimd();               // 50 / [10, 90]
    AiTriggerState st = make_trigger_state(d);
    REQUIRE(st.weights.size() == 1);
    REQUIRE(st.times_completed.size() == 1);
    REQUIRE(st.times_executed.size() == 1);
    CHECK(st.weights[0] == 50.0);              // seeded from starting_weight
    CHECK(st.times_completed[0] == 0);
    CHECK(st.times_executed[0] == 0);
    // Success #1 [RE-GATE APPLIED: ai-trigger.md §4 — CONFIRMED formula]:
    // E=0 -> d=0 -> 50 + 20 = 70; then ++C, ++E.
    adjust_weight(st, d, 0, /*success=*/true);
    CHECK(st.weights[0] == 50.0 + kAiTriggerSuccessWeightDelta);
    CHECK(st.times_completed[0] == 1);
    CHECK(st.times_executed[0] == 1);
    CHECK(st.adjustments == 1);
    CHECK(st.last_adjusted == 0);
    CHECK(st.last_success);
    CHECK(st.last_old_weight == 50.0);
    CHECK(st.last_new_weight == 70.0);
    // Success #2: E=1, C=1 -> d = max(0, (1/1 - 0.5)*1) = 0.5 ->
    // 70 + 20 + 0.5 = 90.5 -> CLAMPED to max_weight 90.
    adjust_weight(st, d, 0, true);
    CHECK(st.weights[0] == 90.0);
    CHECK(st.times_completed[0] == 2);
    CHECK(st.times_executed[0] == 2);
    // Failure #1: E=2, C=2 -> d = min(0, (2/2 - 0.5)*1*2) = 0 ->
    // 90 - 50 = 40; C NOT incremented [RE: §4].
    adjust_weight(st, d, 0, /*success=*/false);
    CHECK(st.weights[0] == 40.0);
    CHECK(st.times_completed[0] == 2);
    CHECK(st.times_executed[0] == 3);
    CHECK_FALSE(st.last_success);
    // Failure #2: E=3, C=2 -> ratio > 0.5 -> penalty term clamps to 0 ->
    // 40 - 50 = -10 -> CLAMPED to min_weight 10.
    adjust_weight(st, d, 0, false);
    CHECK(st.weights[0] == 10.0);
    CHECK(st.times_executed[0] == 4);
    // The track-record term never brightens a failure or dims a success:
    // fresh state, two failures (-> E=2, C=0), then a success — the bonus is
    // max(0, (0 - 0.5)*2) = 0, so 10 + 20 = 30.
    AiTriggerState st2 = make_trigger_state(d);
    adjust_weight(st2, d, 0, false);               // E=0: 50-50 -> clamp 10
    CHECK(st2.weights[0] == 10.0);
    adjust_weight(st2, d, 0, false);               // E=1,C=0: d=-0.5 -> clamp 10
    CHECK(st2.weights[0] == 10.0);
    adjust_weight(st2, d, 0, true);                // E=2,C=0: bonus 0 -> 30
    CHECK(st2.weights[0] == 30.0);
    CHECK(st2.times_completed[0] == 1);
    CHECK(st2.times_executed[0] == 3);
    // start == min == max PINS the weight — most stock triggers never move
    // [RE: ai-trigger.md §4].
    d.triggers[0].starting_weight = 70.0;
    d.triggers[0].min_weight = 70.0;
    d.triggers[0].max_weight = 70.0;
    AiTriggerState st3 = make_trigger_state(d);
    adjust_weight(st3, d, 0, true);
    CHECK(st3.weights[0] == 70.0);
    adjust_weight(st3, d, 0, false);
    CHECK(st3.weights[0] == 70.0);
    // Out-of-range trigger: no-op, no crash.
    const int before = st.adjustments;
    adjust_weight(st, d, 5, true);
    CHECK(st.adjustments == before);
}
