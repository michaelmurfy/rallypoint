#include <set>
#include "doctest.h"
#include "../engine/ai_base.hpp"
#include "ra2assets/rules.hpp"
#include "ra2assets/ini.hpp"
#include "ra2assets/mix.hpp"
#include "asset_fixture.hpp"
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
using namespace ra2engine;

TEST_CASE("ai_base: ai_rand is deterministic xorshift32") {
    std::uint32_t a = 0x2545F491u, b = 0x2545F491u;
    for (int i = 0; i < 8; ++i) CHECK(ai_rand(a) == ai_rand(b));
    std::uint32_t s = 1u;
    CHECK(ai_rand(s) == 270369u);  // xorshift32(1): 1^<<13=8193, ^>>17=8193, ^<<5 -> 270369
}

TEST_CASE("ai_base: difficulty_mods resolves the three recovered groups") {
    // Groups hand-built FROM THE NAMED CONSTANTS [RE: ai-base.md §4] — the
    // constants now carry the REAL recovered values (not 1.0 placeholders);
    // the direct pins below keep them honest [RE-GATE APPLIED: ai-base.md §4].
    DifficultyGroupRaw g[3];
    for (int i = 0; i < 3; ++i) {
        g[i].cost = kDifficultyCost[i];
        g[i].build_time = kDifficultyBuildTime[i];
        g[i].build_delay = kDifficultyBuildDelayMin[i];
        g[i].build_slowdown = kDifficultyBuildSlowdown[i];
    }
    // G-63: lobby d uses the DifficultyClass 2-d (lobby Brutal -> [Easy]).
    for (int i = 0; i < 3; ++i) {
        const int c = 2 - i;
        const DifficultyMods m = difficulty_mods(static_cast<AiDifficulty>(i), g);
        CHECK(m.cost_mul == doctest::Approx(kDifficultyCost[c]));
        CHECK(m.build_time_mul == doctest::Approx(1.0));   // W6-02-04: never applied
        CHECK(m.build_delay_min == doctest::Approx(kDifficultyBuildDelayMin[c]));
        CHECK(m.build_slowdown == kDifficultyBuildSlowdown[c]);
        CHECK(m.defense_target == kAiDefenseTarget[i]);
    }
    // Brutal gets the [Easy] bonuses: no BuildSlowdown (W6-02-04: the BuildTime
    // handicap is CRC-only in the original, never applied).
    CHECK(difficulty_mods(AiDifficulty::Difficult, g).build_time_mul == doctest::Approx(1.0));
    CHECK_FALSE(difficulty_mods(AiDifficulty::Difficult, g).build_slowdown);
    CHECK(difficulty_mods(AiDifficulty::Easy, g).build_delay_min == doctest::Approx(0.1));
    // The REAL recovered per-difficulty values [RE: ai-base.md §4 — Easy/
    // Normal/Difficult: Cost 1.0/1.0/1.0, BuildTime 0.8/1.0/1.0,
    // BuildDelay(min) 0.03/0.03/0.1, BuildSlowdown no/yes/yes]:
    CHECK(kDifficultyCost[0] == doctest::Approx(1.0));
    CHECK(kDifficultyBuildTime[0] == doctest::Approx(0.8));      // Easy builds FASTER
    CHECK(kDifficultyBuildTime[1] == doctest::Approx(1.0));
    CHECK(kDifficultyBuildDelayMin[2] == doctest::Approx(0.1));  // Difficult waits longer
    CHECK_FALSE(kDifficultyBuildSlowdown[0]);
    CHECK(kDifficultyBuildSlowdown[2]);
    // Difficulty visibly separates the defense targets (Easy 6 < Difficult 25)
    // [RE: ai-base.md §2; RE-FALLBACK: count-array index binding].
    CHECK(kAiDefenseTarget[0] < kAiDefenseTarget[2]);
}

TEST_CASE("ai_base: AiBaseState defaults are the documented seeds") {
    AiBaseState s;
    CHECK(s.rng == 0x2545F491u);
    CHECK(s.plan.empty());
    CHECK(s.pending[0] == -1);
    CHECK(s.pending[1] == -1);
    CHECK(s.difficulty == AiDifficulty::Normal);  // default group [RE-FALLBACK: ai-base.md §4]
    CHECK(s.build_gate == 0.0);   // first build pass fires immediately [RE: ai-base.md §1]
    CHECK(s.build_clock == 0.0);
}

// --- Install-gated real-rulesmd pins (skip clean without an install, exactly
//     like tests/test_rules.cpp's install-gated cases) ---
TEST_CASE("ai_base: real rulesmd [AI]/difficulty pins match the recovered constants") {
    // Borrow the shared, load-once ra2md.mix + rulesmd parse (asset_fixture)
    // instead of re-slurping the ~196 MB archive and re-parsing rulesmd.
    if (!ra2fixture::assets_available()) { MESSAGE("skipped: ra2md.mix not found"); return; }
    if (ra2fixture::rulesmd_bytes().empty()) {
        MESSAGE("skipped: rulesmd.ini not found in ra2md.mix top level or in "
                "localmd.mix/expandmd*.mix/cachemd.mix/conqmd.mix");
        return;
    }
    const ra2assets::Rules& rules = ra2fixture::rulesmd();

    // [RE-GATE APPLIED: ai-base.md §2/§4 — these assert the REAL file values
    // equal the committed constants, so a rules/constant drift is caught.
    // Key spellings are the RECOVERED ones (PowerEmergency, NOT
    // PowerEmergencyFraction; no HarvestersPerRefinery exists in [AI]).]
    CHECK(rules.section_double("AI", "PowerSurplus", -1.0) ==
          doctest::Approx(ra2engine::kAiPowerSurplus));
    // PowerEmergency=75% -> strtod stops at '%' -> 75; the constant is the
    // fraction 0.75 [RE: ai-base.md §2].
    CHECK(rules.section_double("AI", "PowerEmergency", -1.0) ==
          doctest::Approx(ra2engine::kAiPowerEmergency * 100.0));
    CHECK(rules.section_double("AI", "BaseSizeAdd", -1.0) ==
          doctest::Approx((double)ra2engine::kAiBaseSizeAdd));
    CHECK(rules.section_double("AI", "AIBaseSpacing", -1.0) ==
          doctest::Approx((double)ra2engine::kAiBaseSpacing));
    CHECK(rules.section_double("AI", "RefineryRatio", -1.0) ==
          doctest::Approx(ra2engine::kAiRefineryRatio));
    CHECK(rules.section_double("AI", "RefineryLimit", -1.0) ==
          doctest::Approx((double)ra2engine::kAiRefineryLimit));
    CHECK(rules.section_double("AI", "WarRatio", -1.0) ==
          doctest::Approx(ra2engine::kAiWarRatio));
    CHECK(rules.section_double("AI", "WarLimit", -1.0) ==
          doctest::Approx((double)ra2engine::kAiWarLimit));
    CHECK(rules.section_double("AI", "DefenseRatio", -1.0) ==
          doctest::Approx(ra2engine::kAiDefenseRatio));
    CHECK(rules.section_double("AI", "DefenseLimit", -1.0) ==
          doctest::Approx((double)ra2engine::kAiDefenseLimit));
    CHECK(rules.section_double("AI", "CreditReserve", -1.0) ==
          doctest::Approx(ra2engine::kAiCreditReserve));
    CHECK(rules.section_double("General", "AISafeDistance", -1.0) ==
          doctest::Approx((double)ra2engine::kAiSafeDistance));
    CHECK(rules.section_double("General", "BaseBias", -1.0) ==
          doctest::Approx(ra2engine::kAiBaseBias));
    // Difficulty groups: the RECOVERED section names [Easy]/[Normal]/
    // [Difficult] (NOT Medium/Hard) -> Rules+0x1538/+0x1588/+0x15d8
    // [RE: ai-base.md §4]:
    const char* kGroups[3] = {"Easy", "Normal", "Difficult"};  // [RE: ai-base.md §4]
    for (int i = 0; i < 3; ++i) {
        CHECK(rules.section_double(kGroups[i], "Cost", -1.0) ==
              doctest::Approx(ra2engine::kDifficultyCost[i]));
        CHECK(rules.section_double(kGroups[i], "BuildTime", -1.0) ==
              doctest::Approx(ra2engine::kDifficultyBuildTime[i]));
        CHECK(rules.section_double(kGroups[i], "BuildDelay", -1.0) ==
              doctest::Approx(ra2engine::kDifficultyBuildDelayMin[i]));
    }
}

TEST_CASE("ai_base: find_build_location scans nearest-first from the base center") {
    BaseView v;
    v.center_x = 10; v.center_y = 10;
    v.threat_x = 30; v.threat_y = 10;
    // Only (12, 10) is placeable -> ring r=2 finds it; nothing nearer passes.
    auto only = [](int x, int y) { return x == 12 && y == 10; };
    Cellxy c = find_build_location(v, /*is_defense=*/false, only);
    CHECK(c.x == 12);
    CHECK(c.y == 10);
    // Everything placeable -> the center itself (r=0) wins (the caller's
    // can_place normally rejects occupied cells; the scan is pure).
    Cellxy c0 = find_build_location(v, false, [](int, int) { return true; });
    CHECK(c0.x == 10);
    CHECK(c0.y == 10);
    // Determinism: same inputs -> same cell, every time.
    for (int i = 0; i < 3; ++i) {
        Cellxy cc = find_build_location(v, false, only);
        CHECK(cc.x == c.x);
        CHECK(cc.y == c.y);
    }
}

TEST_CASE("ai_base: find_build_location prefers the nearer ring") {
    BaseView v;
    v.center_x = 0; v.center_y = 0;
    // (0,2) at Chebyshev r=2 and (5,0) at r=5 both pass -> r=2 wins.
    auto two = [](int x, int y) { return (x == 0 && y == 2) || (x == 5 && y == 0); };
    Cellxy c = find_build_location(v, false, two);
    CHECK(c.x == 0);
    CHECK(c.y == 2);
}

TEST_CASE("ai_base: defense placement biases toward the threat within a ring") {
    BaseView v;
    v.center_x = 10; v.center_y = 10;
    v.threat_x = 40; v.threat_y = 10;  // threat due +x
    // Two cells on the SAME ring r=3: (13,10) faces the threat, (7,10) faces
    // away. A defense picks the threat-facing one; a structure picks the
    // row-major-first one ((7,7)..: with only these two passing, row-major
    // order within the ring reaches (7,10) before (13,10)).
    auto two = [](int x, int y) { return (x == 13 && y == 10) || (x == 7 && y == 10); };
    Cellxy def = find_build_location(v, /*is_defense=*/true, two);
    CHECK(def.x == 13);
    CHECK(def.y == 10);
    Cellxy str = find_build_location(v, /*is_defense=*/false, two);
    CHECK(str.x == 7);
    CHECK(str.y == 10);
}

TEST_CASE("ai_base: find_build_location boxed in -> {-1,-1}") {
    BaseView v;
    v.center_x = 0; v.center_y = 0;
    Cellxy c = find_build_location(v, false, [](int, int) { return false; });
    CHECK(c.x == -1);
    CHECK(c.y == -1);
    // A cell OUTSIDE the scan cap is never reached:
    auto far = [](int x, int y) { return x == kAiMaxScanRadius + 5 && y == 0; };
    Cellxy cf = find_build_location(v, false, far);
    CHECK(cf.x == -1);
}

namespace {
// Soviet-shaped test roster mirroring the recovered build set [RE-GATE
// APPLIED: ai-base.md §2]: [0]=NACNST [1]=NAPOWR [2]=NAREFN [3]=NAHAND
// [4]=NAWEAP [5]=NARADR [6]=NALASR [7]=NAWALL. Costs/power are TEST values
// (the real ones come from rulesmd via main.cpp); the plan ORDER under test
// is the recovered one.
std::vector<ra2engine::BuildingTypeDef> ai_roster() {
    std::vector<ra2engine::BuildingTypeDef> r(8);
    const char* ids[8] = {"NACNST", "NAPOWR", "NAREFN", "NAHAND",
                          "NAWEAP", "NARADR", "NALASR", "NAWALL"};
    const int cost[8] = {3000, 600, 2000, 500, 2000, 1000, 500, 100};
    const int power[8] = {0, 150, -20, -10, -30, -20, -50, 0};
    for (int i = 0; i < 8; ++i) {
        r[i].id = ids[i];
        r[i].cost = cost[i];
        r[i].power = power[i];
        r[i].tech_level = 1;
    }
    r[0].conyard = true;
    // Real rulesmd prereq edges [RE-GATE APPLIED: ai-base.md §2a — the topo
    // sort under test orders by THESE, via the engine's existing prereq
    // tokens (BuildingTypeDef::prerequisite; PROC = the refinery GROUP)]:
    // NAWEAP needs PROC and NARADR needs NAREFN, so both must sort AFTER
    // the refinery.
    r[1].prerequisite = {"NACNST"};          // NAPOWR
    r[2].prerequisite = {"NAPOWR"};          // NAREFN
    r[3].prerequisite = {"NACNST"};          // NAHAND
    r[4].prerequisite = {"NAHAND", "PROC"};  // NAWEAP — PROC = refinery group
    r[5].prerequisite = {"NAREFN"};          // NARADR
    r[6].prerequisite = {"NAHAND"};          // NALASR
    return r;
}
ra2engine::AiCatalog ai_catalog() {
    ra2engine::AiCatalog c;
    c.conyard = 0; c.power = 1; c.refinery = 2; c.barracks = 3;
    c.war_factory = 4; c.radar = 5;
    c.defenses = {6};  // SovietBaseDefenses head (NALASR) — test set
    c.wall = 7;
    return c;
}
ra2engine::BaseBuildingView bv(int type, int cx = 0, int cy = 0) {
    ra2engine::BaseBuildingView b;
    b.type = type; b.cx = cx; b.cy = cy; b.operational = true;
    return b;
}
ra2engine::DifficultyMods mods_for(ra2engine::AiDifficulty d) {
    ra2engine::DifficultyGroupRaw g[3];
    for (int i = 0; i < 3; ++i) {
        g[i].cost = ra2engine::kDifficultyCost[i];
        g[i].build_time = ra2engine::kDifficultyBuildTime[i];
        g[i].build_delay = ra2engine::kDifficultyBuildDelayMin[i];
        g[i].build_slowdown = ra2engine::kDifficultyBuildSlowdown[i];
    }
    return ra2engine::difficulty_mods(d, g);
}
int first_node_of(const std::vector<ra2engine::PlannedNode>& p, int type) {
    for (int i = 0; i < (int)p.size(); ++i)
        if (p[i].type == type) return i;
    return -1;
}
int count_nodes_of(const std::vector<ra2engine::PlannedNode>& p, int type) {
    int n = 0;
    for (const auto& node : p)
        if (node.type == type) ++n;
    return n;
}
}  // namespace

TEST_CASE("ai_base: build_base_plan topologically prereq-sorts the plan") {
    // [RE-GATE APPLIED: ai-base.md §2a — FUN_005052d0 is a Kahn-style
    // topological prereq-sort: ConYard + Power append first; Barracks/War
    // Factory are HOISTED preferences the sort OVERRIDES when a prereq is
    // unmet. NAWEAP needs PROC (refinery group) and NARADR needs NAREFN, so
    // the Refinery is emitted BEFORE the War Factory AND the Radar.
    // Effective order: ConYard -> Power -> Barracks -> Refinery -> War
    // Factory -> Radar -> extra Refineries -> defenses -> walls.]
    auto roster = ai_roster();
    auto cat = ai_catalog();
    AiRules ai;
    BaseView v;
    v.own = {bv(0)};  // lone ConYard
    const DifficultyMods easy = mods_for(AiDifficulty::Easy);
    const auto plan = build_base_plan(v, cat, roster, ai, easy);
    REQUIRE(plan.size() >= 6);
    CHECK(plan[0].type == cat.conyard);
    CHECK(plan[1].type == cat.power);
    CHECK(plan[2].type == cat.barracks);     // hoist preference HELD (prereq met)
    CHECK(plan[3].type == cat.refinery);     // topo sort OVERRIDES the War
                                             // Factory hoist: PROC unmet ->
                                             // Refinery first [RE: §2a]
    CHECK(plan[4].type == cat.war_factory);  // after its refinery
    CHECK(plan[5].type == cat.radar);        // NARADR after NAREFN
    // The §2a invariants: Refinery strictly BEFORE War Factory AND Radar;
    // Barracks (no refinery prereq) keeps its hoisted slot:
    CHECK(first_node_of(plan, cat.refinery) < first_node_of(plan, cat.war_factory));
    CHECK(first_node_of(plan, cat.refinery) < first_node_of(plan, cat.radar));
    CHECK(first_node_of(plan, cat.barracks) < first_node_of(plan, cat.refinery));
    CHECK(count_nodes_of(plan, cat.refinery) == ai.refinery_limit);  // RefineryLimit=4 [RE: §2]
    // Defenses after the prereq-sorted core, per-difficulty count; walls at
    // the tail:
    CHECK(first_node_of(plan, cat.war_factory) < first_node_of(plan, cat.defenses[0]));
    CHECK(count_nodes_of(plan, cat.defenses[0]) == easy.defense_target);  // Easy=6 [RE: §2, RE-FALLBACK index binding]
    CHECK(first_node_of(plan, cat.defenses[0]) < first_node_of(plan, cat.wall));
    // Per-difficulty counts separate Easy(6) and Difficult(25) [RE: §2]:
    const auto hard_plan =
        build_base_plan(v, cat, roster, ai, mods_for(AiDifficulty::Difficult));
    CHECK(count_nodes_of(hard_plan, cat.defenses[0]) == kAiDefenseTarget[2]);
    CHECK(kAiDefenseTarget[0] < kAiDefenseTarget[2]);
}

TEST_CASE("ai_base: W6-03-06 skirmish plan spreads defences and walls through the build-out") {
    // [RE: FUN_005052d0 -- one refinery in the topo sort, AIExtraRefineries
    // more at rand(first,n-1)+1, the per-difficulty defence placeholders at
    // rand(3,n-1)+1; FUN_004fe200 resolves a placeholder into a wall with
    // AIPickWallDefensePercent once a defence stands ahead of it.]
    auto roster = ai_roster();
    auto cat = ai_catalog();
    AiRules ai;
    ai.extra_refineries = 2;   // Brutal
    ai.pick_wall_pct = 50;
    BaseView v;
    v.own = {bv(0)};
    const DifficultyMods hard = mods_for(AiDifficulty::Difficult);
    std::uint32_t rng = 0x1234567u;
    const auto plan = build_base_plan(v, cat, roster, ai, hard, &rng);
    CHECK(plan[0].type == cat.conyard);
    CHECK(plan[1].type == cat.power);
    CHECK(count_nodes_of(plan, cat.refinery) == 3);   // 1 + AIExtraRefineries
    const int ndef = count_nodes_of(plan, cat.defenses[0]);
    const int nwall = count_nodes_of(plan, cat.wall);
    CHECK(ndef >= 1);
    CHECK(nwall >= 1);
    CHECK(ndef + nwall <= hard.defense_target);
    // Interleaved: a defence comes before the LAST core (non-defence,
    // non-wall) node -- not a tail block after the whole build-out.
    int last_core = -1;
    for (int i = 0; i < (int)plan.size(); ++i)
        if (plan[i].type != cat.defenses[0] && plan[i].type != cat.wall) last_core = i;
    CHECK(first_node_of(plan, cat.defenses[0]) < last_core);
    // A wall never precedes the first defence; every node is buildable from
    // the nodes before it (the head-only walk never wedges).
    CHECK(first_node_of(plan, cat.defenses[0]) < first_node_of(plan, cat.wall));
    std::vector<int> built;
    for (const auto& n : plan) {
        for (const std::string& tok : roster[n.type].prerequisite) {
            bool met = false;
            for (int b : built) {
                if (roster[b].id == tok) met = true;
                if (tok == "PROC" && b == cat.refinery) met = true;
            }
            CHECK_MESSAGE(met, "node type ", n.type, " blocked on ", tok);
        }
        built.push_back(n.type);
    }
    // Ids stay unique; the same seed gives the same plan.
    std::set<int> ids;
    for (const auto& n : plan) ids.insert(n.id);
    CHECK(ids.size() == plan.size());
    std::uint32_t rng2 = 0x1234567u;
    const auto again = build_base_plan(v, cat, roster, ai, hard, &rng2);
    REQUIRE(again.size() == plan.size());
    for (std::size_t i = 0; i < plan.size(); ++i) CHECK(again[i].type == plan[i].type);
    // Without the skirmish knobs the legacy plan is untouched.
    AiRules legacy;
    std::uint32_t rng3 = 0x1234567u;
    const auto old = build_base_plan(v, cat, roster, legacy, hard, &rng3);
    CHECK(count_nodes_of(old, cat.refinery) == legacy.refinery_limit);
    CHECK(rng3 == 0x1234567u);
}

TEST_CASE("ai_base: the plan is head-first prereq-satisfiable from a lone ConYard") {
    // [RE-GATE APPLIED: ai-base.md §2a — the dispatcher acts on the HEAD
    // node only and NEVER skips, so the plan itself must be buildable
    // strictly in order. Walk the plan keeping a built-so-far set and assert
    // every node's prereqs are met by its PREDECESSORS — the pre-§2a
    // War-Factory-before-Refinery order deadlocked exactly here.]
    auto roster = ai_roster();
    auto cat = ai_catalog();
    AiRules ai;
    BaseView v;
    v.own = {bv(0)};  // lone ConYard
    const auto plan =
        build_base_plan(v, cat, roster, ai, mods_for(AiDifficulty::Normal));
    std::vector<int> built;  // roster types standing so far
    for (const auto& n : plan) {
        for (const std::string& tok : roster[n.type].prerequisite) {
            bool met = false;
            for (int b : built) {
                if (roster[b].id == tok) met = true;
                if (tok == "PROC" && b == cat.refinery) met = true;  // group token
            }
            CHECK_MESSAGE(met, "node type ", n.type, " blocked on ", tok);
        }
        built.push_back(n.type);
    }
}

TEST_CASE("ai_base: force-emit cycle-breaker terminates on an unsatisfiable prereq") {
    // [RE-GATE APPLIED: ai-base.md §2a — FUN_005052d0's !progress branch
    // force-appends an unused candidate so the loop ALWAYS terminates. Drop
    // the refinery from the catalog: NAWEAP's PROC prereq (and NARADR's
    // NAREFN) is now unsatisfiable, yet both must still be emitted
    // (force-emitted), never hang the builder or vanish.]
    auto roster = ai_roster();
    auto cat = ai_catalog();
    cat.refinery = -1;  // no refinery in this install -> PROC never met
    AiRules ai;
    BaseView v;
    v.own = {bv(0)};
    const auto plan =
        build_base_plan(v, cat, roster, ai, mods_for(AiDifficulty::Easy));
    CHECK(first_node_of(plan, cat.war_factory) >= 0);  // force-emitted, not dropped
    CHECK(first_node_of(plan, cat.radar) >= 0);        // force-emitted too
    CHECK(count_nodes_of(plan, 2) == 0);               // roster 2 = NAREFN: absent
    // Determinism: the force-emit order is stable run to run.
    const auto again =
        build_base_plan(v, cat, roster, ai, mods_for(AiDifficulty::Easy));
    REQUIRE(again.size() == plan.size());
    for (std::size_t i = 0; i < plan.size(); ++i) CHECK(again[i].type == plan[i].type);
}

TEST_CASE("ai_base: next_build walks the plan in order, one structure at a time") {
    // [RE-GATE APPLIED: ai-base.md §1/§2/§2a — HEAD-node dispatch
    // (FUN_004fe200): the first UNSATISFIED node in plan order wins, a
    // prereq-blocked head is NEVER skipped (the topo-sorted plan makes the
    // head buildable), and the single-slot house+0x564c gate lives with the
    // caller. Recovered opening: Power -> Barracks -> Refinery -> War
    // Factory -> Radar — the Refinery BEFORE the War Factory, per §2a.]
    auto roster = ai_roster();
    auto cat = ai_catalog();
    AiRules ai;
    AiBaseState st;
    const DifficultyMods m = mods_for(AiDifficulty::Normal);
    BaseView v;
    v.own = {bv(0)};       // lone ConYard satisfies the plan's ConYard node
    v.credits = 100000;    // money never the constraint here
    v.power_output = 0; v.power_drain = 0;
    v.rival_base_size = 30;  // BaseSizeAdd cap never the constraint here
    BuildDecision d = next_build(st, v, cat, roster, ai, m);
    CHECK(d.type == cat.power);                    // 1) Power
    CHECK(st.plan[d.node].type == cat.power);
    v.own.push_back(bv(1)); v.power_output = 150;
    d = next_build(st, v, cat, roster, ai, m);
    CHECK(d.type == cat.barracks);                 // 2) Barracks (hoist held)
    v.own.push_back(bv(3)); v.power_drain = 10;
    d = next_build(st, v, cat, roster, ai, m);
    CHECK(d.type == cat.refinery);                 // 3) Refinery BEFORE War Factory [RE: §2a]
    v.own.push_back(bv(2)); v.power_drain = 30;
    d = next_build(st, v, cat, roster, ai, m);
    CHECK(d.type == cat.war_factory);              // 4) War Factory (PROC now standing)
    v.own.push_back(bv(4)); v.power_drain = 60;
    d = next_build(st, v, cat, roster, ai, m);
    CHECK(d.type == cat.radar);                    // 5) Radar (NAREFN standing)
    // Determinism: same state + view -> same decision.
    const BuildDecision d2 = next_build(st, v, cat, roster, ai, m);
    CHECK(d2.type == d.type);
    CHECK(d2.node == d.node);
}

TEST_CASE("ai_base: power preemption inserts a Power node before a drainer") {
    // [RE-GATE APPLIED: ai-base.md §1/§2 — FUN_004fe200: a drainer that
    // would push drain > output - PowerSurplus(50) gets a Power node
    // INSERTED ahead of it. PowerSurplus (the reserve), this preemption
    // gate, and PowerEmergency=0.75 (a SELL trigger, unused in AI-1) are
    // THREE DISTINCT rules — the pre-hunt conflated them.]
    auto roster = ai_roster();
    auto cat = ai_catalog();
    AiRules ai;
    AiBaseState st;
    const DifficultyMods m = mods_for(AiDifficulty::Normal);
    BaseView v;
    v.own = {bv(0), bv(1), bv(3)};  // ConYard + power + barracks
    v.credits = 100000;
    v.power_output = 150; v.power_drain = 90;
    v.rival_base_size = 30;
    // Head unsatisfied node = REFINERY (the §2a topo order puts it right
    // after the barracks; drain 20): 90+20 > 150-50 -> preempt with power.
    // NOT a PowerEmergency (150 >= 0.75 x 90): the preemption gate alone
    // fires [RE: ai-base.md §2].
    CHECK(v.power_output >= ai.power_emergency * v.power_drain);
    const BuildDecision d = next_build(st, v, cat, roster, ai, m);
    CHECK(d.type == cat.power);
    CHECK(st.plan[d.node].type == cat.power);
    // Inserted AHEAD of the refinery node it preempted [RE-GATE APPLIED:
    // ai-base.md §2a — expected head position moved with the topo order]:
    REQUIRE(d.node + 1 < (int)st.plan.size());
    CHECK(st.plan[d.node + 1].type == cat.refinery);
}

TEST_CASE("ai_base: economy expansion appends a node when affordable and under limit") {
    // [RE-GATE APPLIED: ai-base.md §2 — FUN_004fdb30: a SEPARATE trigger
    // from the plan walk; appends an extra refinery/war-factory node at the
    // tail when under its Ratio/Limit and affordable. Opportunistic — never
    // waits. The whole base is capped at rival_base_size + BaseSizeAdd(3).]
    auto roster = ai_roster();
    auto cat = ai_catalog();
    AiRules ai;
    ai.refinery_limit = 1;  // tiny plan for the test
    AiBaseState st;
    const DifficultyMods m = mods_for(AiDifficulty::Easy);
    BaseView v;
    v.credits = 100000;
    v.power_output = 1000; v.power_drain = 0;
    v.rival_base_size = 30;
    v.own = {bv(0)};
    st.plan = build_base_plan(v, cat, roster, ai, m);
    // Satisfy EVERY plan node (one standing building per node):
    for (const auto& n : st.plan)
        if (n.type != cat.conyard) v.own.push_back(bv(n.type));
    const int before = (int)st.plan.size();
    // refinery count == RefineryLimit(1) -> next under-limit economy add is
    // the war factory (count 1 < WarLimit 2) [RE: ai-base.md §2]:
    BuildDecision d = next_build(st, v, cat, roster, ai, m);
    CHECK(d.type == cat.war_factory);
    CHECK((int)st.plan.size() == before + 1);
    CHECK(d.node == before);  // APPENDED at the tail
    st.plan.pop_back();       // undo the append for the next branches
    // Opportunistic: unaffordable -> no append, NO wait (just bank):
    BaseView poor = v;
    poor.credits = 10;
    d = next_build(st, poor, cat, roster, ai, m);
    CHECK(d.type == -1);
    CHECK_FALSE(d.wait);
    CHECK((int)st.plan.size() == before);
    // BaseSizeAdd cap: own base >= rival + kAiBaseSizeAdd -> build nothing
    // [RE: ai-base.md §2 — AI base <= largest rival base + 3]:
    BaseView capped = v;
    capped.rival_base_size = 0;
    d = next_build(st, capped, cat, roster, ai, m);
    CHECK(d.type == -1);
    CHECK_FALSE(d.wait);
}

TEST_CASE("ai_base: affordability-wait banks instead of substituting") {
    auto roster = ai_roster();
    auto cat = ai_catalog();
    AiRules ai;
    AiBaseState st;
    const DifficultyMods m = mods_for(AiDifficulty::Normal);
    BaseView v;
    v.own = {bv(0), bv(1), bv(3), bv(4), bv(5)};  // built out through radar
    v.power_output = 500; v.power_drain = 60;
    v.rival_base_size = 30;
    v.credits = 100;  // head node = refinery (2000 x cost_mul) unaffordable
    BuildDecision d = next_build(st, v, cat, roster, ai, m);
    CHECK(d.type == -1);
    CHECK(d.wait);  // banks income; NEVER picks something cheaper [RE: ai-base.md §2]
    // CreditReserve=100 stays banked: covering cost alone is NOT enough
    // [RE: ai-base.md §2 — CreditReserve]:
    v.credits = 2000.0 * m.cost_mul + ai.credit_reserve - 1.0;
    d = next_build(st, v, cat, roster, ai, m);
    CHECK(d.wait);
    v.credits = 2000.0 * m.cost_mul + ai.credit_reserve;
    d = next_build(st, v, cat, roster, ai, m);
    CHECK(d.type == cat.refinery);
}

TEST_CASE("ai_base: rebuild re-queues the exact destroyed node before tail expansion") {
    // [RE-GATE APPLIED: ai-base.md §5 — CONFIRMED, kept: a dead node keeps
    // its stored cell AND its list position, so the plan walk reaches it
    // ahead of the unsatisfied tail; no rebuild counter/limit exists.]
    auto roster = ai_roster();
    auto cat = ai_catalog();
    AiRules ai;
    AiBaseState st;
    const DifficultyMods m = mods_for(AiDifficulty::Normal);
    BaseView v;
    v.own = {bv(0), bv(3), bv(4), bv(5)};  // ConYard+barracks+war+radar; power DIED
    v.credits = 100000;
    v.power_output = 0; v.power_drain = 60;
    v.rival_base_size = 30;
    (void)next_build(st, v, cat, roster, ai, m);  // lazy-builds the plan
    const int pi = first_node_of(st.plan, cat.power);
    REQUIRE(pi >= 0);
    st.plan[pi].cx = 7; st.plan[pi].cy = 3;  // the stored (placed) cell
    st.plan[pi].building = -1;
    st.plan[pi].dead = true;                 // World marked the death (Task 5)
    const BuildDecision d = next_build(st, v, cat, roster, ai, m);
    CHECK(d.type == cat.power);  // the node's exact type
    CHECK(d.node == pi);         // the SAME node — stored cell reused (Task 5)
    CHECK(st.plan[pi].cx == 7);
    CHECK(st.plan[pi].cy == 3);
    // Ahead of the unsatisfied refinery/defense tail (rebuild-before-expansion):
    CHECK(d.node < first_node_of(st.plan, cat.refinery));
    // Unaffordable rebuild WAITS at the rebuild node (never skipped past):
    BaseView poor = v;
    poor.credits = 10;
    const BuildDecision w = next_build(st, poor, cat, roster, ai, m);
    CHECK(w.type == -1);
    CHECK(w.wait);
}
