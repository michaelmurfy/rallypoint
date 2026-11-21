#pragma once
// Pure AITriggerTypes weighted trigger engine [RE: docs/re/ai-trigger.md].
// No SDL, no World — consumes a read-only TriggerView the caller (World)
// fills, plus per-house AiTriggerState. doctest-testable. EVERY numeric
// constant below is a HUNT OUTPUT recorded in docs/re/ai-trigger.md; none is
// a guess — tests assert against the named constants so a RE-GATE amendment
// updates constant, doc, and test together. All draws come from the seeded
// ai_rand xorshift32 (ai_base.hpp) over AiTriggerState::rng — a SEPARATE
// stream from AiTeamState::rng, so enabling triggers never perturbs the AI-2
// draw sequence. NEVER wall-clock.
#include <cstdint>
#include <string>
#include <vector>
#include "ai_base.hpp"     // ai_rand, AiDifficulty
#include "aimd_parse.hpp"  // AimdData / AITriggerType / TriggerObject / TriggerComparator
#include "ai_team.hpp"     // TeamInstance (live-instance Max cap check)

namespace ra2engine {

// --- Recovered ConditionType enum [RE-GATE APPLIED: ai-trigger.md §2 —
// CONFIRMED: the dispatch is switch(ConditionType+1) in FUN_0041e740, values
// −1..7, >= 8 rejected. Implementable core: −1 (always true), 0
// (EnemyHouseOwns), 1 (OwnerHouseOwns), 4 (HouseCredits — a REAL
// credits-OP-N compare; ALL 8 stock uses are N=0/op>= so it is effectively
// always-true on stock data, but our economy exposes credits), 7
// (CivilianHouseOwns — FALSE unless a neutral house exists).
// Inert-required-subsystem-absent (evaluate FALSE): 5/6 (superweapons).
// Inert/unused (evaluate FALSE): 2/3 (inline house-metric, 0 stock uses).] ---
constexpr int kCondNone = -1;               // no condition -> always TRUE
constexpr int kCondEnemyOwns = 0;           // EnemyHouseOwns: enemy owns
                                            // [op] N of condition_object
constexpr int kCondOwnerOwns = 1;           // OwnerHouseOwns: owner owns [op] N
constexpr int kCondHouseMetric2 = 2;        // inline house-metric — UNUSED in
                                            // stock, inert -> FALSE
constexpr int kCondHouseMetric3 = 3;        // inline house-metric — UNUSED in
                                            // stock, inert -> FALSE
constexpr int kCondHouseCredits = 4;        // HouseCredits: OWNER house
                                            // credits [op] N (NOT "enemy
                                            // credits") [RE: §2]
constexpr int kCondIronCurtainCharged = 5;  // W6-03-05: own Curtain charge >= AIMinorSuperReadyPercent
constexpr int kCondChronoSphereCharged = 6; // W6-03-05: own Sphere charge >= AIMinorSuperReadyPercent
constexpr int kCondCivilianHouseOwns = 7;   // neutral/civilian house owns
                                            // [op] N — FALSE without a
                                            // neutral house [RE: §2]

// --- Comparator operator encoding [RE-GATE APPLIED: ai-trigger.md §2 —
// CONFIRMED: identical switch in every condition slave; tested as
// `actualCount OP value`; 3 (>=) is the stock default (148/165 lines)] ---
constexpr int kCompLess = 0;
constexpr int kCompLessEq = 1;
constexpr int kCompEq = 2;
constexpr int kCompGreaterEq = 3;
constexpr int kCompGreater = 4;
constexpr int kCompNotEq = 5;

// --- Weight dynamics [RE-GATE APPLIED: ai-trigger.md §4 — CONFIRMED; this
// REPLACES the pre-hunt additive ±3.0. rulesmd [General]:
// AITriggerSuccessWeightDelta=20 (Rules+0xc0), AITriggerFailureWeightDelta=
// -50 (Rules+0xc8), AITriggerTrackRecordCoefficient=1 (Rules+0xd0). The rule
// is ADDITIVE and ASYMMETRIC, applied ONLY at team disband, plus a
// track-record ratio term, clamped [min_weight, max_weight] — see
// adjust_weight.] ---
constexpr double kAiTriggerSuccessWeightDelta = 20.0;
constexpr double kAiTriggerFailureWeightDelta = -50.0;
constexpr double kAiTriggerTrackRecordCoefficient = 1.0;

// Read-only battlefield snapshot, filled by the caller (World) each trigger
// pass. Count vectors are indexed by the caller's unit/building roster
// indices (the SAME indices parse_ai_triggers resolved condition_object to).
// The field set follows the RECOVERED enum [RE-GATE APPLIED: ai-trigger.md
// §2]: per-house owned-count-by-TechnoType for owner + enemy (+ neutral for
// ct=7) and the OWNER house credits — there is NO enemy-credits scalar, no
// power condition, and no time condition in the recovered enum.
struct TriggerView {
    std::vector<int> own_units;        // per unit-roster-index counts, owner house
    std::vector<int> own_buildings;    // per building-roster-index counts
    std::vector<int> enemy_units;      // enemy-owned counts (same indexing)
    std::vector<int> enemy_buildings;
    std::vector<int> neutral_units;    // neutral/civilian-house counts for
    std::vector<int> neutral_buildings;// kCondCivilianHouseOwns — leave EMPTY
                                       // when no neutral house exists (ct=7
                                       // then evaluates FALSE) [RE: §2]
    double credits = 0.0;              // OWNER house credits — kCondHouseCredits
                                       // tests credits OP N [RE-GATE APPLIED:
                                       // ai-trigger.md §2 — ct=4 is
                                       // HouseCredits, NOT enemy credits]
    int difficulty = 1;                // house difficulty index (0 = HARDEST);
                                       // the enable consult INVERTS onto
                                       // enabled[] [RE-GATE APPLIED: §5]
    std::string house_name;            // owner_house match ("<all>" = any)
    int side = 0;                      // the house's side, 1=Allied, 2=Soviet,
                                       // 3=Yuri (side-specific triggers only
                                       // match an equal house side; trigger
                                       // side 0 = any) [RE: §5]
    int tech_level = 10;               // house tech level for the TechLevel gate
    // [G-60] per TeamType: the house can currently build every TaskForce
    // member (the original's FUN_0041ff00 buildable test inside
    // ConditionMet, ai-trigger.md §5). EMPTY = no filter.
    std::vector<char> team_buildable;
    // W6-03-05 [RE: FUN_0041f0f0 / FUN_0041f1a0]: the owner's Iron Curtain
    // (SW type 1) / Chronosphere (type 3) charge fraction, -1 when the house
    // has none; ct=5/6 are TRUE once it is >= minor_ready (Rules+0xd70,
    // [General] AIMinorSuperReadyPercent=.7).
    double iron_charge = -1.0;
    double chrono_charge = -1.0;
    double minor_ready = 0.7;
};

// select_trigger's answer: which trigger fired and which TeamType(s) to
// create. BOTH Team1 AND Team2 are created for the OWNING house — Team2 is
// NOT a reciprocal team, and may equal Team1 (-> two instances) [RE-GATE
// APPLIED: ai-trigger.md §3 — CONFIRMED]. trigger = -1: none eligible,
// create nothing.
struct TriggerDecision {
    int trigger = -1;    // AimdData::triggers index (witness/weight key)
    int teamtype1 = -1;  // AimdData::teams index — Team1, always created
    int teamtype2 = -1;  // Team2 — ALSO created when >= 0 (-1 = "<none>")
};

// Per-house runtime trigger state [spec §2]. The parsed AITriggerType table
// stays IMMUTABLE — only the state here moves. Attribution needs NO
// team->trigger link: the original scans the trigger array at disband for
// Team1 == the disbanding team's TeamType [RE-GATE APPLIED: ai-trigger.md §6].
struct AiTriggerState {
    std::vector<double> weights;      // CURRENT weight per trigger, PARALLEL to
                                      // AimdData::triggers (init: starting_weight)
    std::vector<int> times_completed; // C: per-trigger success count (the
                                      // original's +0x104), PARALLEL to
                                      // triggers [RE: §4]
    std::vector<int> times_executed;  // E: per-trigger total-attempt count
                                      // (+0x108) — C/E is the track-record
                                      // ratio [RE: §4]
    std::uint32_t rng = 0x2545F491u;  // deterministic fixed seed — a SEPARATE
                                      // stream from AiTeamState::rng
    int triggers_fired = 0;        // lifetime count (witness + hash)
    int last_fired = -1;           // triggers index of the latest fire (witness)
    int adjustments = 0;           // lifetime weight adjustments (witness + hash)
    int last_adjusted = -1;        // triggers index of the latest adjust (witness)
    bool last_success = false;     // latest outcome (witness)
    double last_old_weight = 0.0;  // witness old->new
    double last_new_weight = 0.0;
};

// Fresh per-house state: weights seeded from each trigger's starting_weight;
// times_completed/times_executed zeroed [RE: ai-trigger.md §4].
AiTriggerState make_trigger_state(const AimdData& aimd);

// Comparator test [RE-GATE APPLIED: ai-trigger.md §2 — CONFIRMED operator
// semantics, applied as `lhs OP c.value`]. Unknown operator -> FALSE (inert,
// never a crash).
bool compare_count(int lhs, const TriggerComparator& c);

// Condition dispatch over the FULL recovered −1..7 enum [RE-GATE APPLIED:
// ai-trigger.md §2 — CONFIRMED]: kCondNone always TRUE; kCondEnemyOwns/
// kCondOwnerOwns/kCondCivilianHouseOwns count the condition_object in the
// enemy/own/neutral view slices through the comparator; kCondHouseCredits
// tests the OWNER house credits through the comparator (stock is always
// N=0/op>= i.e. always-true, but the REAL compare is implemented — our
// economy exposes credits [RE: §2 deviations]); kCondHouseMetric2/3
// (unused in stock) and kCondIronCurtainCharged/kCondChronoSphereCharged
// (superweapons absent) and every unknown value evaluate FALSE — exactly
// AI-2's inert-opcode discipline. An untestable counting condition (object
// None / index out of the view's range / empty neutral slices) is FALSE.
bool trigger_condition_true(const AITriggerType& t, const TriggerView& view);

// Enable-filter matrix [RE-GATE APPLIED: ai-trigger.md §5 — ConditionMet's
// CONFIRMED filter set, applied BEFORE the condition]: (1) base-defense
// split — base_defense triggers rejected (offensive scope; [RE-FALLBACK]:
// the recovered split consults the house's defensive-team need, an input
// this engine does not track; ALL stock triggers are IsForBaseDefense=0);
// (2) IsGlobal — passes vacuously (aimd-loaded triggers are never Global in
// the original); (3) is_enabled from [AITriggerTypesEnable]; (4)
// IsForSkirmish must be 1 (skirmish-only engine); (5) per-difficulty enable
// INVERTED — house difficulty index 0 (=HARDEST) -> enabled[2] (Hard), 1 ->
// enabled[1], 2 -> enabled[0] (Easy); (6) OwnerHouse — "<all>"/Any pass,
// country name match, "<none>"/"" reject; (7) SideIndex — 0=any, else must
// equal view.side (1=Allied/2=Soviet/3=Yuri); (8) TechLevel — require
// view.tech_level >= t.tech_level.
bool trigger_filters_pass(const AITriggerType& t, const TriggerView& view);

// ELIGIBLE = condition TRUE && filters pass [spec §3].
bool trigger_eligible(const AITriggerType& t, const TriggerView& view);

// Weighted-random selection [RE-GATE APPLIED: ai-trigger.md §3 — CONFIRMED
// draw shape: gather eligible triggers with w = round(Weight_Current) (int);
// if ANY eligible w == 5000, reset the candidate set to ONLY 5000-weight
// triggers (the mandatory tier); then r = rand(1, Σw) and a cumulative-sum
// walk — the first entry with r <= cum wins. The seeded xorshift32 ai_rand
// substitutes the original's lockstep FUN_0065c220(1,Σw) [RE: deviations]].
// Eligibility additionally requires round(weight) > 0 and the per-TeamType
// Max cap for BOTH Team1 AND Team2 (Max < 0 = UNCAPPED, else live instances
// < Max) — enforced INSIDE eligibility, before the draw, as the original
// does at ConditionMet's tail [RE-GATE APPLIED: ai-trigger.md §5/§6]; the
// original's team-buildable predicate (FUN_0041ff00) is deferred to AI-2's
// recruit/produce machinery downstream [RE-FALLBACK: an unfillable team
// idles and disbands as a failure]. `weights` is the house's
// AiTriggerState::weights (PARALLEL to aimd.triggers). Deterministic: same
// seed -> same pick sequence. Returns {-1,-1,-1} when none eligible. ONE
// trigger per call — the caller enforces one per trigger pass. The caller
// checks TotalAITeamCap (a call-site gate).
TriggerDecision select_trigger(const AimdData& aimd,
                               const std::vector<double>& weights,
                               const std::vector<TeamInstance>& live,
                               const TriggerView& view, std::uint32_t& rng);

// Adaptive weight feedback [RE-GATE APPLIED: ai-trigger.md §4 — the
// CONFIRMED additive asymmetric track-record rule, applied ONLY at team
// disband; REPLACES the pre-hunt ±3.0]. With E = times_executed[trigger]
// (pre-increment), C = times_completed[trigger], K =
// kAiTriggerTrackRecordCoefficient:
//   success: d = (E > 0) ? max(0.0, (C/E - 0.5) * E) : 0;
//            weight += kAiTriggerSuccessWeightDelta + d;  then ++C, ++E
//   failure: d = (E > 0) ? min(0.0, (C/E - 0.5) * K * E) : 0;
//            weight += kAiTriggerFailureWeightDelta + d;  then ++E only
//            (C is NOT incremented)
// then CLAMP to [min_weight, max_weight] at BOTH bounds — a trigger with
// start == min == max never moves (most stock). Records the witness fields
// (adjustments/last_adjusted/last_success/last_old_weight/last_new_weight).
// Out-of-range `trigger` is a no-op (never a crash).
void adjust_weight(AiTriggerState& st, const AimdData& aimd, int trigger,
                   bool success);

}  // namespace ra2engine
