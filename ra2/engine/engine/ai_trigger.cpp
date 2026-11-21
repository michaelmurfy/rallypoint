#include "ai_trigger.hpp"
#include "orig_math.hpp"   // Net L0: om::ftol (_ftol 0x7C4D00) for double->int
#include <cctype>
#include <cmath>   // std::lround — the round(Weight_Current) draw [RE: §3]

namespace ra2engine {
namespace {

bool iequal(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::toupper((unsigned char)a[i]) != std::toupper((unsigned char)b[i]))
            return false;
    return true;
}

// Count of the condition object in the given view slices; -1 = untestable
// (no object / index out of the view's range) -> the condition is FALSE.
int object_count(const TriggerObject& o, const std::vector<int>& units,
                 const std::vector<int>& blds) {
    if (o.kind == TechnoKind::Unit)
        return (o.index >= 0 && o.index < (int)units.size()) ? units[o.index] : -1;
    if (o.kind == TechnoKind::Building)
        return (o.index >= 0 && o.index < (int)blds.size()) ? blds[o.index] : -1;
    return -1;
}

// Live-instance count vs the per-TeamType Max (Max < 0 = UNCAPPED) — the
// caps sit INSIDE eligibility, before the draw, for BOTH Team1 and Team2
// [RE-GATE APPLIED: ai-trigger.md §5/§6 — CONFIRMED: ConditionMet checks
// cntInstances < Max at its tail].
bool team_capped(const AimdData& aimd, const std::vector<TeamInstance>& live,
                 int teamtype) {
    const int max = aimd.teams[teamtype].max;
    if (max < 0) return false;                   // Max < 0 = uncapped
    int cnt = 0;
    for (const TeamInstance& ti : live)
        if (ti.teamtype == teamtype) ++cnt;
    return cnt >= max;
}

}  // namespace

AiTriggerState make_trigger_state(const AimdData& aimd) {
    AiTriggerState st;
    st.weights.reserve(aimd.triggers.size());
    for (const AITriggerType& t : aimd.triggers)
        st.weights.push_back(t.starting_weight);
    st.times_completed.assign(aimd.triggers.size(), 0);   // C [RE: §4]
    st.times_executed.assign(aimd.triggers.size(), 0);    // E [RE: §4]
    return st;
}

bool compare_count(int lhs, const TriggerComparator& c) {
    // [RE-GATE APPLIED: ai-trigger.md §2 — CONFIRMED operator semantics,
    // `lhs OP c.value`.]
    switch (c.op) {
        case kCompLess:      return lhs < c.value;
        case kCompLessEq:    return lhs <= c.value;
        case kCompEq:        return lhs == c.value;
        case kCompGreaterEq: return lhs >= c.value;
        case kCompGreater:   return lhs > c.value;
        case kCompNotEq:     return lhs != c.value;
        default:             return false;   // unknown operator -> inert FALSE
    }
}

bool trigger_condition_true(const AITriggerType& t, const TriggerView& view) {
    // [RE-GATE APPLIED: ai-trigger.md §2 — the CONFIRMED −1..7 dispatch
    // (switch(ConditionType+1) in FUN_0041e740); inert values -> FALSE, the
    // AI-2 inert discipline; >= 8 / unknown -> FALSE (the original rejects).]
    switch (t.condition_type) {
        case kCondNone:
            return true;   // no condition -> always TRUE
        case kCondEnemyOwns: {
            const int n = object_count(t.condition_object, view.enemy_units,
                                       view.enemy_buildings);
            return n >= 0 && compare_count(n, t.comparator);
        }
        case kCondOwnerOwns: {
            const int n = object_count(t.condition_object, view.own_units,
                                       view.own_buildings);
            return n >= 0 && compare_count(n, t.comparator);
        }
        case kCondHouseCredits:
            // ct=4 HouseCredits [RE-GATE APPLIED: ai-trigger.md §2 — the
            // OWNER house money scalar through the comparator, NOT enemy
            // credits. Stock is always N=0/op>= (effectively always-true);
            // the REAL compare is implemented — our economy exposes credits
            // [RE: §2 deviations].]
            return compare_count(om::ftol(view.credits), t.comparator);
        case kCondCivilianHouseOwns: {
            // ct=7: counts the NEUTRAL/civilian house; FALSE unless a
            // neutral house exists (empty neutral slices -> -1) [RE: §2].
            const int n = object_count(t.condition_object, view.neutral_units,
                                       view.neutral_buildings);
            return n >= 0 && compare_count(n, t.comparator);
        }
        case kCondIronCurtainCharged:
        case kCondChronoSphereCharged: {
            // W6-03-05 [RE: FUN_0041f0f0 / FUN_0041f1a0]: the owner's SW of
            // that type is present (+0x6d) and (1.0 - Rules+0xd70) >= the
            // fraction of its recharge still to run, i.e. charged >=
            // AIMinorSuperReadyPercent. The comparator is not consulted.
            const double ch = t.condition_type == kCondIronCurtainCharged ? view.iron_charge
                                                                          : view.chrono_charge;
            return ch >= 0.0 && (float)(1.0 - view.minor_ready) >= (float)(1.0 - ch);
        }
        default:
            // kCondHouseMetric2 / kCondHouseMetric3 (unused in stock) + every
            // unknown value: INERT -> FALSE [RE: ai-trigger.md §2].
            return false;
    }
}

bool trigger_filters_pass(const AITriggerType& t, const TriggerView& view) {
    // [RE-GATE APPLIED: ai-trigger.md §5 — ConditionMet's CONFIRMED filter
    // set, in the recovered order. Filter #2 (IsGlobal) passes vacuously:
    // aimd-loaded triggers are never Global in the original (IsGlobal=0),
    // so the field is not parsed or checked.]
    // #1 base-defense vs offense split: AI-3's scope is OFFENSIVE triggers;
    // ALL stock triggers are IsForBaseDefense=0. [RE-FALLBACK: the recovered
    // split consults the house's defensive-team need (Rules+0x1394[diff] vs
    // house+0x566c), inputs this engine does not track — base-defense
    // triggers are rejected until a defensive-need input exists.]
    if (t.base_defense) return false;
    // #3 per-trigger [AITriggerTypesEnable] flag [RE: §5].
    if (!t.is_enabled) return false;
    // #4 IsForSkirmish must be 1 — skirmish-only engine [RE: §5].
    if (!t.is_for_skirmish) return false;
    // #5 per-difficulty enable, INVERTED [RE-GATE APPLIED: ai-trigger.md §5
    // — CONFIRMED]: house difficulty index 0 (=HARDEST) consults
    // Enabled_Hard = enabled[2], 1 -> enabled[1], 2 -> Enabled_Easy =
    // enabled[0] — the ai-base/ai-team hardest<->easiest inversion.
    if (view.difficulty < 0 || view.difficulty > 2 ||
        !t.enabled[2 - view.difficulty])
        return false;
    // #6 OwnerHouse [RE: §5 — CONFIRMED]: "<all>" (Any) -> pass; a country
    // name -> case-insensitive match; "<none>"/"" (OwnerHouseType 0) -> REJECT.
    if (!iequal(t.owner_house, "<all>")) {
        if (t.owner_house.empty() || iequal(t.owner_house, "<none>"))
            return false;
        if (!iequal(t.owner_house, view.house_name)) return false;
    }
    // #7 SideIndex [RE: §5 — CONFIRMED]: 0=any, else must equal the house
    // side (1=Allied, 2=Soviet, 3=Yuri).
    if (t.side != 0 && t.side != view.side) return false;
    // #8 TechLevel [RE: §5 — CONFIRMED]: require house tech level >= field.
    if (t.tech_level > view.tech_level) return false;
    return true;
}

bool trigger_eligible(const AITriggerType& t, const TriggerView& view) {
    return trigger_condition_true(t, view) && trigger_filters_pass(t, view);
}

TriggerDecision select_trigger(const AimdData& aimd,
                               const std::vector<double>& weights,
                               const std::vector<TeamInstance>& live,
                               const TriggerView& view, std::uint32_t& rng) {
    TriggerDecision d;
    // Gather eligible triggers with ROUNDED integer weights [RE-GATE APPLIED:
    // ai-trigger.md §3 — CONFIRMED: the driver draws over
    // round(Weight_Current) (FUN_007c4d00) via rand(1, Σw)].
    std::vector<int> elig;
    std::vector<long> wts;
    long total = 0;
    bool tier5000 = false;
    for (int i = 0; i < (int)aimd.triggers.size() && i < (int)weights.size(); ++i) {
        const AITriggerType& t = aimd.triggers[i];
        const long w = om::lround(weights[i]);
        if (w <= 0) continue;                        // no probability mass
        if (!trigger_eligible(t, view)) continue;
        // Per-TeamType Max cap for BOTH Team1 and Team2, INSIDE eligibility,
        // before the draw (Max < 0 = UNCAPPED) [RE-GATE APPLIED:
        // ai-trigger.md §5/§6 — CONFIRMED at ConditionMet's tail]. The
        // buildable predicate (FUN_0041ff00) is deferred to AI-2's
        // recruit/produce machinery [RE-FALLBACK].
        if (team_capped(aimd, live, t.team1)) continue;
        if (t.team2 >= 0 && team_capped(aimd, live, t.team2)) continue;
        // [G-60] Team1/Team2 must be buildable (FUN_0041ff00 x2 in
        // ConditionMet, ai-trigger.md §5) -- caller-computed per TeamType.
        auto unbuildable = [&](int tt) {
            return tt >= 0 && tt < (int)view.team_buildable.size() && !view.team_buildable[tt];
        };
        if (unbuildable(t.team1) || unbuildable(t.team2)) continue;
        // The 5000 MANDATORY tier [RE-GATE APPLIED: ai-trigger.md §3 —
        // CONFIRMED]: if ANY eligible trigger's rounded weight == 5000, the
        // candidate set resets to ONLY 5000-weight triggers.
        if (w == 5000 && !tier5000) {
            tier5000 = true;
            elig.clear();
            wts.clear();
            total = 0;
        }
        if (tier5000 && w != 5000) continue;
        elig.push_back(i);
        wts.push_back(w);
        total += w;
    }
    if (elig.empty() || total <= 0) return d;
    // r = rand(1, Σw), then the cumulative-sum walk: the FIRST entry with
    // r <= cum wins [RE-GATE APPLIED: ai-trigger.md §3 — CONFIRMED; the
    // seeded xorshift32 substitutes the lockstep FUN_0065c220(1, Σw)
    // (deviation, see ai-trigger.md deviations)].
    const long r = 1 + (long)(ai_rand(rng) % (std::uint32_t)total);
    long cum = 0;
    for (std::size_t k = 0; k < elig.size(); ++k) {
        cum += wts[k];
        if (r <= cum) {
            d.trigger = elig[k];
            d.teamtype1 = aimd.triggers[elig[k]].team1;
            d.teamtype2 = aimd.triggers[elig[k]].team2;  // BOTH created for
            return d;                                    // the owner [RE: §3]
        }
    }
    return d;
}

void adjust_weight(AiTriggerState& st, const AimdData& aimd, int trigger,
                   bool success) {
    if (trigger < 0 || trigger >= (int)st.weights.size() ||
        trigger >= (int)aimd.triggers.size())
        return;
    const AITriggerType& t = aimd.triggers[trigger];
    const double old = st.weights[trigger];
    // [RE-GATE APPLIED: ai-trigger.md §4 — the CONFIRMED additive asymmetric
    // track-record rule (RegisterSuccess FUN_0041fd80 / RegisterFailure
    // FUN_0041fe40), clamped at BOTH bounds; E/C pre-increment.]
    const int E = st.times_executed[trigger];
    const int C = st.times_completed[trigger];
    double delta = 0.0;
    double w = old;
    if (success) {
        if (E > 0) {
            delta = ((double)C / (double)E - 0.5) * (double)E;
            if (delta < 0.0) delta = 0.0;            // bonus >= 0
        }
        w += kAiTriggerSuccessWeightDelta + delta;
    } else {
        if (E > 0) {
            delta = ((double)C / (double)E - 0.5) *
                    kAiTriggerTrackRecordCoefficient * (double)E;
            if (delta > 0.0) delta = 0.0;            // penalty <= 0
        }
        w += kAiTriggerFailureWeightDelta + delta;
    }
    if (w < t.min_weight) w = t.min_weight;
    if (w > t.max_weight) w = t.max_weight;
    st.weights[trigger] = w;
    if (success) ++st.times_completed[trigger];      // ++C on success ONLY
    ++st.times_executed[trigger];                    // ++E on every outcome
    ++st.adjustments;
    st.last_adjusted = trigger;
    st.last_success = success;
    st.last_old_weight = old;
    st.last_new_weight = w;
}

}  // namespace ra2engine
