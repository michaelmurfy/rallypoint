#include "doctest.h"
#include <cmath>
#include <set>
#include <vector>
#include "../engine/ai_team.hpp"
using namespace ra2engine;

namespace {
// Hand-built aimd tables (no Ini): TaskForce = 2 x type0 + 1 x type1;
// script exercises every implementable opcode class + an inert one.
AimdData make_aimd(bool full = false) {
    AimdData d;
    TaskForce tf;
    tf.id = "TF1";
    tf.members = {{2, 0}, {1, 1}};              // 2 x type0, 1 x type1
    d.taskforces.push_back(tf);
    ScriptType sc;
    sc.id = "SC1";
    sc.actions = {{kOpMoveToWaypoint, 0},            // pc 0: move to waypoint 0 (op 3)
                  {kOpAttackQuarry, kQuarryAnything},  // pc 1: attack anything (op 0)
                  {kOpTimedGuard, 2},                // pc 2: timed guard 2 -> 30 frames
                  {kSaLoad, 0},                      // pc 3: load onto transport (14) -> Inert
                  {kOpSuccess, 0}};                  // pc 4: success flag (49), continues
    d.scripts.push_back(sc);
    TeamType tt;
    tt.id = "TT1";
    tt.house = "Russians";
    tt.taskforce = 0;
    tt.script = 0;
    tt.priority = 7;
    tt.max = 2;
    tt.full = full;
    tt.autocreate = true;
    d.teams.push_back(tt);
    return d;
}
TeamInstance make_team() {
    TeamInstance t;
    t.teamtype = 0;
    return t;
}
// A full-strength view: members parallel to the claimed 3 slots.
TeamView full_view(bool idle) {
    TeamView v;
    v.base_x = 10; v.base_y = 10;
    v.enemy_x = 40; v.enemy_y = 10;
    v.waypoints = {Cellxy{30, 12}, Cellxy{35, 15}};
    for (int i = 0; i < 3; ++i) {
        TeamMemberView m;
        m.unit = 100 + i;
        m.alive = true;
        m.cx = 11 + i; m.cy = 10;
        m.idle = idle;
        v.members.push_back(m);
    }
    return v;
}
void claim_full(TeamInstance& t) {          // 2 x type0 + 1 x type1
    t.members = {100, 101, 102};
    t.member_types = {0, 0, 1};
}
constexpr double kDt = 1.0 / 15.0;
}  // namespace

TEST_CASE("ai_team: full=yes stays Recruiting under strength; musters at the own base") {
    AimdData d = make_aimd(/*full=*/true);
    TeamInstance t = make_team();
    std::uint32_t rng = 0x2545F491u;
    TeamView v = full_view(true);
    // Below strength + Full=yes: stays Recruiting, emits nothing (never launches
    // under strength) [RE-GATE APPLIED: ai-team.md §2].
    t.members = {100};
    t.member_types = {0};
    v.members.resize(1);
    TeamCommand c = step_team(t, v, d, rng, kDt);
    CHECK(t.state == TeamState::Recruiting);
    CHECK(c.kind == TeamCommand::Kind::None);
    // Full strength -> Gathering. W2-SK-06: the pre-script muster is within
    // +-3 cells of the OWN base centre (the scripts' own 53 line gathers on the
    // AISafeDistance ring around the enemy, see gather_cell).
    claim_full(t);
    v = full_view(true);
    c = step_team(t, v, d, rng, kDt);
    CHECK(t.state == TeamState::Gathering);
    CHECK(std::abs(t.gather_x - v.base_x) <= 3);
    CHECK(std::abs(t.gather_y - v.base_y) <= 3);
}

TEST_CASE("ai_team: full=no launches immediately under strength; skirmish never dissolves, MP does") {
    AimdData d = make_aimd(/*full=*/false);
    std::uint32_t rng = 1u;
    // Full=no + at least one member: launches AT ONCE (acts under strength),
    // no wait for the dissolve timer [RE-GATE APPLIED: ai-team.md §2].
    TeamInstance t = make_team();
    TeamView v = full_view(true);
    t.members = {100};             // 1 of 3 slots
    t.member_types = {0};
    v.members.resize(1);
    (void)step_team(t, v, d, rng, kDt);
    CHECK(t.state == TeamState::Gathering);
    // Zero members: the DEFAULT skirmish path (multiplay=false) NEVER dissolves
    // on the timer — it keeps recruiting/producing [RE-GATE APPLIED: §2].
    const int frames = kAiDissolveUnfilledDelayFrames + 2;
    TeamInstance skirm = make_team();
    TeamView ev = full_view(true);
    ev.members.clear();
    for (int i = 0; i < frames; ++i)
        (void)step_team(skirm, ev, d, rng, kDt);   // multiplay defaults false
    CHECK(skirm.state == TeamState::Recruiting);
    // The MULTIPLAY path DOES dissolve an unfilled team past the timer.
    TeamInstance mp = make_team();
    for (int i = 0; i < frames && mp.state == TeamState::Recruiting; ++i)
        (void)step_team(mp, ev, d, rng, kDt, /*multiplay=*/true);
    CHECK(mp.state == TeamState::Disbanding);
}

TEST_CASE("ai_team: full=yes waits past the timeout and bounces back on a death") {
    AimdData d = make_aimd(/*full=*/true);
    TeamInstance t = make_team();
    std::uint32_t rng = 1u;
    TeamView v = full_view(true);
    t.members = {100};
    t.member_types = {0};
    v.members.resize(1);
    for (int i = 0; i < kAiDissolveUnfilledDelayFrames + 2; ++i)
        (void)step_team(t, v, d, rng, kDt);
    CHECK(t.state == TeamState::Recruiting);     // full=yes: no timeout launch
    // Fill it -> Gathering; then a death reopens a slot -> back to Recruiting
    // [RE: ai-team.md §2].
    claim_full(t);
    v = full_view(true);
    (void)step_team(t, v, d, rng, kDt);
    REQUIRE(t.state == TeamState::Gathering);
    t.members = {100, 102};                      // one type0 died (caller pruned)
    t.member_types = {0, 1};
    v.members.resize(2);
    (void)step_team(t, v, d, rng, kDt);
    CHECK(t.state == TeamState::Recruiting);
}

TEST_CASE("ai_team: gathering issues ONE move order then completes when members reach the ring (Stray)") {
    AimdData d = make_aimd();
    TeamInstance t = make_team();
    std::uint32_t rng = 1u;
    claim_full(t);
    TeamView v = full_view(true);
    (void)step_team(t, v, d, rng, kDt);          // Recruiting -> Gathering (sets gather cell)
    REQUIRE(t.state == TeamState::Gathering);
    TeamCommand c = step_team(t, v, d, rng, kDt);
    CHECK(c.kind == TeamCommand::Kind::MoveTeam);  // the ONE gather order
    CHECK(c.x == t.gather_x);
    CHECK(c.y == t.gather_y);
    // Members still far from the gather focus -> stays Gathering, no re-order.
    c = step_team(t, v, d, rng, kDt);
    CHECK(c.kind == TeamCommand::Kind::None);
    CHECK(t.state == TeamState::Gathering);
    // Move every member onto the gather cell (within Stray=2.0) -> Executing
    // [RE-GATE APPLIED: ai-team.md §2].
    for (TeamMemberView& m : v.members) { m.cx = t.gather_x; m.cy = t.gather_y; }
    (void)step_team(t, v, d, rng, kDt);
    CHECK(t.state == TeamState::Executing);
    CHECK(t.script_pc == 0);
}

TEST_CASE("ai_team: move-to-waypoint opcode targets the waypoint table then advances at CloseEnough") {
    AimdData d = make_aimd();
    TeamInstance t = make_team();
    std::uint32_t rng = 1u;
    claim_full(t);
    t.state = TeamState::Executing;              // enter the interpreter directly
    TeamView v = full_view(true);
    TeamCommand c = step_team(t, v, d, rng, kDt);
    CHECK(c.kind == TeamCommand::Kind::MoveTeam);
    CHECK(c.x == 30);                            // waypoints[0] = (30,12)
    CHECK(c.y == 12);
    CHECK(t.script_pc == 0);                     // still on the move action (far)
    // The caller snapped the ideal waypoint (30,12) to a free cell and recorded
    // it as the realized focus (here the waypoint itself is free). Move members
    // within CloseEnough=2.25 of that realized focus -> advance [FIX: arrival is
    // measured against the realized focus, not the ideal cell].
    t.gather_x = 30; t.gather_y = 12;
    for (TeamMemberView& m : v.members) { m.cx = 30; m.cy = 12; }
    c = step_team(t, v, d, rng, kDt);            // arrived
    CHECK(t.script_pc == 1);                     // pc++ -> attack action
    // Unmapped waypoint argument -> enemy base centre [RE-FALLBACK]:
    AimdData d2 = make_aimd();
    d2.scripts[0].actions[0].argument = 99;
    TeamInstance t2 = make_team();
    claim_full(t2);
    t2.state = TeamState::Executing;
    TeamView v2 = full_view(true);
    c = step_team(t2, v2, d2, rng, kDt);
    CHECK(c.kind == TeamCommand::Kind::MoveTeam);
    CHECK(c.x == v2.enemy_x);
    CHECK(c.y == v2.enemy_y);
}

TEST_CASE("ai_team: move-arrival is measured against the REALIZED focus, not the ideal cell") {
    // [FIX: Executing-move wedge] The caller sends members to a SNAPPED cell and
    // writes it back as the realized focus (team.gather_x/gather_y). step_team
    // must measure arrival against THAT, not the ideal waypoint whose CloseEnough
    // neighborhood may be blocked — otherwise an unreachable ideal wedges the
    // team in Executing forever (mirrors the original's snapped-Focus semantics).
    AimdData d = make_aimd();
    std::uint32_t rng = 1u;
    // --- arrives when members sit on the realized focus, FAR from the ideal ---
    {
        TeamInstance t = make_team();
        claim_full(t);
        t.state = TeamState::Executing;             // pc 0 = move to waypoint (30,12)
        TeamView v = full_view(true);
        TeamCommand c = step_team(t, v, d, rng, kDt);
        REQUIRE(c.kind == TeamCommand::Kind::MoveTeam);
        REQUIRE(c.x == 30);                         // still emits the IDEAL cell
        REQUIRE(c.y == 12);
        REQUIRE(t.script_pc == 0);
        // Caller snapped (30,12) to a free cell (5,5) and recorded it; members
        // now sit ON (5,5) but ~30 cells from the ideal waypoint.
        t.gather_x = 5; t.gather_y = 5;
        for (TeamMemberView& m : v.members) { m.cx = 5; m.cy = 5; }
        c = step_team(t, v, d, rng, kDt);
        CHECK(t.script_pc == 1);                    // arrived vs REALIZED focus -> next
        CHECK(t.move_issued == false);
    }
    // --- non-vacuous inverse: members near NEITHER cell -> the move is held ---
    {
        TeamInstance t = make_team();
        claim_full(t);
        t.state = TeamState::Executing;
        TeamView v = full_view(true);
        (void)step_team(t, v, d, rng, kDt);         // emit -> move_issued
        t.gather_x = 5; t.gather_y = 5;             // realized focus
        for (TeamMemberView& m : v.members) { m.cx = 20; m.cy = 20; }  // near neither
        (void)step_team(t, v, d, rng, kDt);
        CHECK(t.script_pc == 0);                    // NOT arrived -> stays on move
        CHECK(t.move_issued == true);
    }
}

TEST_CASE("ai_team: attack-quarry emits while a target exists, advances when extinct") {
    AimdData d = make_aimd();
    TeamInstance t = make_team();
    std::uint32_t rng = 1u;
    claim_full(t);
    t.state = TeamState::Executing;
    t.script_pc = 1;                             // the attack action
    TeamView v = full_view(true);
    v.quarry_exists = true;
    TeamCommand c = step_team(t, v, d, rng, kDt);
    CHECK(c.kind == TeamCommand::Kind::AttackQuarry);
    CHECK(c.quarry == kQuarryAnything);          // [RE: ai-team.md §5]
    CHECK(t.script_pc == 1);                     // held while targets remain
    c = step_team(t, v, d, rng, kDt);
    CHECK(c.kind == TeamCommand::Kind::AttackQuarry);  // re-emitted
    v.quarry_exists = false;                     // class extinct
    c = step_team(t, v, d, rng, kDt);
    CHECK(c.kind == TeamCommand::Kind::None);
    CHECK(t.script_pc == 2);                     // advanced to guard
}

TEST_CASE("ai_team: timed-guard holds for arg x 15 frames then advances") {
    AimdData d = make_aimd();
    TeamInstance t = make_team();
    std::uint32_t rng = 1u;
    claim_full(t);
    t.state = TeamState::Executing;
    t.script_pc = 2;                             // timed guard 2 -> 30 frames [c14 idx 5]
    TeamView v = full_view(true);
    int guard_ticks = 0;
    while (t.script_pc == 2) {
        const TeamCommand c = step_team(t, v, d, rng, kDt);
        if (t.script_pc == 2) {
            CHECK(c.kind == TeamCommand::Kind::GuardArea);
            CHECK(c.radius == kAiGatherRadius);
            ++guard_ticks;
        }
        REQUIRE(guard_ticks < 50);               // 2 x 15 = 30 frames + slack
    }
    CHECK(guard_ticks >= 29);                    // held ~30 frames at 15 Hz
    CHECK(t.script_pc == 3);
}

TEST_CASE("ai_team: load opcode emits Load until every member is ordered, then advances (W2-SK-09, fv9a)") {
    AimdData d = make_aimd();
    TeamInstance t = make_team();
    std::uint32_t rng = 1u;
    claim_full(t);
    t.state = TeamState::Executing;
    t.script_pc = 3;                             // action 14 (load onto transport)
    TeamView v = full_view(true);
    // fv9a [RE: FUN_006EC760]: the line HOLDS while a member is still to be
    // ordered aboard (the handler orders one member per pass and returns) ...
    v.load_pending = true;
    for (int pass = 0; pass < 3; ++pass) {
        const TeamCommand c = step_team(t, v, d, rng, kDt);
        CHECK(c.kind == TeamCommand::Kind::Load);    // W2-SK-09: no longer inert
        CHECK(t.script_pc == 3);
    }
    // ... and completes (team+0x80) on a pass with nobody left to order.
    v.load_pending = false;
    const TeamCommand c = step_team(t, v, d, rng, kDt);
    CHECK(c.kind != TeamCommand::Kind::Load);
    CHECK(t.script_pc == 4);
}

TEST_CASE("ai_team: W6-03-05 IronCurtainMe/ChronoPrep wait, fire or complete") {
    // [RE: FUN_006ef1d0 / FUN_006ef690] sw_state 2 = charging past
    // AIMinorSuperReadyPercent -> the line waits; 1 = charged -> fire and
    // complete; 0 = absent / far from charged -> complete without firing.
    for (int op : {55, 57}) {
        AimdData d = make_aimd();
        d.scripts[0].actions[3] = {op, 9};
        TeamInstance t = make_team();
        std::uint32_t rng = 1u;
        claim_full(t);
        t.state = TeamState::Executing;
        t.script_pc = 3;
        TeamView v = full_view(true);
        v.sw_state = 2;
        TeamCommand c = step_team(t, v, d, rng, kDt);
        CHECK(c.kind == TeamCommand::Kind::Wait);
        CHECK(t.script_pc == 3);
        v.sw_state = 1;
        c = step_team(t, v, d, rng, kDt);
        CHECK(c.kind == (op == 55 ? TeamCommand::Kind::IronCurtain : TeamCommand::Kind::ChronoShift));
        if (op == 57) CHECK(c.quarry == 9);
        CHECK(t.script_pc == 4);
        t.script_pc = 3;
        v.sw_state = 0;
        c = step_team(t, v, d, rng, kDt);
        CHECK(c.kind == TeamCommand::Kind::None);
        CHECK(t.script_pc == 4);
    }
}

TEST_CASE("ai_team: jump consumes the tick; success flags and continues") {
    AimdData d = make_aimd();
    d.scripts[0].actions[3] = {kOpJumpToLine, 2};  // pc 3: jump to line 2 (1-based) -> pc 1
    TeamInstance t = make_team();
    std::uint32_t rng = 1u;
    claim_full(t);
    t.state = TeamState::Executing;
    t.script_pc = 3;
    TeamView v = full_view(true);
    (void)step_team(t, v, d, rng, kDt);
    CHECK(t.script_pc == 1);                     // jumped, one action this tick
    CHECK(t.state == TeamState::Executing);
    // 49 sets the success flag and the script CONTINUES [c14 idx 49]; the
    // team then disbands only by running off the end.
    t.script_pc = 4;
    (void)step_team(t, v, d, rng, kDt);
    CHECK(t.completed_success);
    CHECK(t.state == TeamState::Executing);
    CHECK(t.script_pc == 5);
    (void)step_team(t, v, d, rng, kDt);
    CHECK(t.state == TeamState::Disbanding);
    // A script that runs off the end also disbands.
    TeamInstance t2 = make_team();
    claim_full(t2);
    t2.state = TeamState::Executing;
    t2.script_pc = (int)d.scripts[0].actions.size();  // past the last line
    (void)step_team(t2, v, d, rng, kDt);
    CHECK(t2.state == TeamState::Disbanding);
}

TEST_CASE("ai_team: a wiped team disbands from any post-recruiting state (non-vacuous)") {
    // The top-of-step liveness guard [RE: ai-team.md §2] disbands a launched team
    // the instant it has zero LIVE members, BEFORE any state-specific handling.
    // Without it a wiped Gathering team would satisfy all_within(Stray) VACUOUSLY
    // (true over an empty live set) and advance to Executing, where an attack
    // action re-emits forever (nothing sets Disbanding) — the dead team then
    // permanently occupies its Max/TotalAITeamCap slot and the AI goes inert.
    AimdData d = make_aimd();
    std::uint32_t rng = 1u;

    // --- Executing with zero live members (all flags cleared, not yet pruned) ---
    {
        TeamInstance t = make_team();
        claim_full(t);
        t.state = TeamState::Executing;
        t.script_pc = 1;                        // an attack action (quarry)
        TeamView v = full_view(true);
        for (TeamMemberView& m : v.members) m.alive = false;
        (void)step_team(t, v, d, rng, kDt);
        CHECK(t.state != TeamState::Executing);  // non-vacuous: did NOT stay Executing
        CHECK(t.state == TeamState::Disbanding);
    }

    // --- Gathering with zero live members: must NOT vacuously advance to
    // Executing via all_within over an empty live set. Members sit FAR from the
    // gather ring, so the only path to Executing would be the vacuous predicate. ---
    {
        TeamInstance t = make_team();
        claim_full(t);
        TeamView v = full_view(true);
        (void)step_team(t, v, d, rng, kDt);      // Recruiting -> Gathering
        REQUIRE(t.state == TeamState::Gathering);
        (void)step_team(t, v, d, rng, kDt);      // issues the ONE gather move
        REQUIRE(t.state == TeamState::Gathering);
        for (TeamMemberView& m : v.members) m.alive = false;  // whole team dies
        (void)step_team(t, v, d, rng, kDt);
        CHECK(t.state != TeamState::Executing);  // non-vacuous: did NOT go Executing
        CHECK(t.state == TeamState::Disbanding);
    }

    // --- Gathering with a fully EMPTY member view (caller already pruned dead) ---
    {
        TeamInstance t = make_team();
        claim_full(t);
        TeamView v = full_view(true);
        (void)step_team(t, v, d, rng, kDt);      // -> Gathering
        REQUIRE(t.state == TeamState::Gathering);
        v.members.clear();                       // all members pruned away
        (void)step_team(t, v, d, rng, kDt);
        CHECK(t.state != TeamState::Executing);
        CHECK(t.state == TeamState::Disbanding);
    }
}

TEST_CASE("ai_team: step_team is deterministic") {
    for (int run = 0; run < 2; ++run) {
        AimdData d = make_aimd();
        TeamInstance t = make_team();
        std::uint32_t rng = 0x2545F491u;
        claim_full(t);
        TeamView idle = full_view(true);
        idle.quarry_exists = true;
        static std::vector<int> first_pcs;
        std::vector<int> pcs;
        for (int i = 0; i < 40; ++i) {
            (void)step_team(t, idle, d, rng, kDt);
            pcs.push_back(t.script_pc * 10 + (int)t.state);
        }
        if (run == 0) first_pcs = pcs;
        else CHECK(first_pcs == pcs);
    }
}

namespace {
// Two-type test unit roster: [0] infantry 200c, [1] vehicle 700c.
std::vector<UnitTypeDef> unit_roster2() {
    std::vector<UnitTypeDef> r(2);
    r[0].id = "E2";
    r[0].cost = 200;
    r[0].is_vehicle = false;
    r[1].id = "HTNK";
    r[1].cost = 700;
    r[1].is_vehicle = true;
    return r;
}
// aimd with TWO autocreate TeamTypes sharing TF1/SC1: priorities 7 and 9.
AimdData make_aimd2() {
    AimdData d = make_aimd();
    TeamType hi = d.teams[0];
    hi.id = "TT2";
    hi.priority = 9;
    hi.max = 1;
    d.teams.push_back(hi);
    return d;
}
}  // namespace

TEST_CASE("ai_team: select_autocreate_team picks highest priority under caps") {
    const AimdData d = make_aimd2();
    AiTeamRules rules;
    std::uint32_t rng = 1u;
    std::vector<TeamInstance> live;
    // Highest priority first (TT2 pri 9 beats TT1 pri 7) [RE: ai-team.md §4]:
    CHECK(select_autocreate_team(d, "Russians", live, 10000.0, rules, rng) == 1);
    // Max cap: one live TT2 (max=1) -> TT1 is next [RE: ai-team.md §4]:
    TeamInstance t2;
    t2.teamtype = 1;
    live.push_back(t2);
    CHECK(select_autocreate_team(d, "Russians", live, 10000.0, rules, rng) == 0);
    // TT1 at max=2 -> nothing eligible:
    TeamInstance t0a, t0b;
    t0a.teamtype = 0;
    t0b.teamtype = 0;
    live.push_back(t0a);
    live.push_back(t0b);
    CHECK(select_autocreate_team(d, "Russians", live, 10000.0, rules, rng) == -1);
}

TEST_CASE("ai_team: select_autocreate_team gates on house and CreditReserve") {
    const AimdData d = make_aimd();
    AiTeamRules rules;
    std::uint32_t rng = 1u;
    const std::vector<TeamInstance> live;
    // House mismatch -> ineligible; case-insensitive match; "<none>" = any
    // [RE-GATE APPLIED: ai-team.md §4 — house-token matching pre-hunt]:
    CHECK(select_autocreate_team(d, "Americans", live, 10000.0, rules, rng) == -1);
    CHECK(select_autocreate_team(d, "RUSSIANS", live, 10000.0, rules, rng) == 0);
    AimdData any = make_aimd();
    any.teams[0].house = "<none>";
    CHECK(select_autocreate_team(any, "Americans", live, 10000.0, rules, rng) == 0);
    // CreditReserve floor [RE-GATE APPLIED: ai-team.md §4/§6 — pre-hunt:
    // creation requires credits >= kAiCreditReserve(100), the banked floor]:
    CHECK(select_autocreate_team(d, "Russians", live, rules.credit_reserve - 1.0,
                                 rules, rng) == -1);
    CHECK(select_autocreate_team(d, "Russians", live, rules.credit_reserve,
                                 rules, rng) == 0);
    // autocreate=false is NEVER selected (AITriggerTypes creation is AI-3):
    AimdData off = make_aimd();
    off.teams[0].autocreate = false;
    CHECK(select_autocreate_team(off, "Russians", live, 10000.0, rules, rng) == -1);
    // IsBaseDefense teams are DEFENSIVE -> never selected on the offensive path
    // [RE-GATE APPLIED: ai-team.md §1/§4]:
    AimdData def = make_aimd();
    def.teams[0].is_base_defense = true;
    CHECK(select_autocreate_team(def, "Russians", live, 10000.0, rules, rng) == -1);
    // Max=0 -> UNCAPPED (not cap 1) [RE-GATE APPLIED: ai-team.md §4]:
    AimdData zero = make_aimd();
    zero.teams[0].max = 0;
    CHECK(select_autocreate_team(zero, "Russians", live, 10000.0, rules, rng) == 0);
    std::vector<TeamInstance> many(5);
    for (auto& t : many) t.teamtype = 0;
    CHECK(select_autocreate_team(zero, "Russians", many, 10000.0, rules, rng) == 0);  // still eligible
    // TotalAITeamCap is the hard per-house team-count cap [RE-GATE APPLIED: §4]:
    AiTeamRules capped = rules;
    capped.total_team_cap = 2;
    std::vector<TeamInstance> at_cap(2);
    for (auto& t : at_cap) t.teamtype = 0;
    CHECK(select_autocreate_team(zero, "Russians", at_cap, 10000.0, capped, rng) == -1);
}

TEST_CASE("ai_team: select_autocreate_team breaks equal-priority ties with seeded ai_rand") {
    // Build an aimd with THREE eligible teams at the SAME highest Priority=5,
    // one eligible LOWER-priority team, and two INELIGIBLE teams that carry a
    // higher Priority=9 (one base-defense, one autocreate=false). The tie-break
    // must draw among {0,1,2} only — never the lower-priority or ineligible ones
    // [RE-FALLBACK: ai-team.md §4].
    AimdData d = make_aimd();                 // seeds teams[0] (pri 7, max 2)
    d.teams.clear();
    TeamType base = make_aimd().teams[0];     // TF1/SC1 template, house Russians
    base.max = 0;                             // uncapped -> stays eligible every draw
    auto add = [&](int pri, bool autoc, bool basedef) {
        TeamType t = base;
        t.priority = pri;
        t.autocreate = autoc;
        t.is_base_defense = basedef;
        d.teams.push_back(t);
    };
    add(5, true, false);   // 0: eligible, top priority
    add(5, true, false);   // 1: eligible, top priority
    add(5, true, false);   // 2: eligible, top priority
    add(3, true, false);   // 3: eligible but LOWER priority -> never picked
    add(9, true, true);    // 4: base-defense -> ineligible despite pri 9
    add(9, false, false);  // 5: autocreate=false -> ineligible despite pri 9
    AiTeamRules rules;
    const std::vector<TeamInstance> live;     // none live: all caps clear each draw

    // (a) non-vacuous: only ever returns an equal-top-priority eligible team.
    std::uint32_t rng = 0x2545F491u;
    std::set<int> seen;
    std::vector<int> seq_a;
    for (int i = 0; i < 200; ++i) {
        const int pick = select_autocreate_team(d, "Russians", live, 10000.0, rules, rng);
        REQUIRE(pick >= 0);
        CHECK((pick == 0 || pick == 1 || pick == 2));  // never 3/4/5
        seen.insert(pick);
        seq_a.push_back(pick);
    }
    // (c) randomization is LIVE, not stuck on index 0: more than one tied team
    // is returned across the draws.
    CHECK(seen.size() > 1);
    // (b) deterministic/reproducible: the SAME fixed seed replays the SAME
    // sequence (seeded ai_rand only, never wall-clock).
    std::uint32_t rng2 = 0x2545F491u;
    std::vector<int> seq_b;
    for (int i = 0; i < 200; ++i)
        seq_b.push_back(select_autocreate_team(d, "Russians", live, 10000.0, rules, rng2));
    CHECK(seq_a == seq_b);
    // A DIFFERENT seed still stays within the eligible tied set (a sanity check
    // that eligibility, not the seed, bounds the output).
    std::uint32_t rng3 = 12345u;
    for (int i = 0; i < 50; ++i) {
        const int pick = select_autocreate_team(d, "Russians", live, 10000.0, rules, rng3);
        CHECK((pick == 0 || pick == 1 || pick == 2));
    }
}

TEST_CASE("ai_team: recruit_idle claims ascending idle units per TaskForce slot") {
    const AimdData d = make_aimd();   // needs 2 x type0 + 1 x type1
    TeamInstance t = make_team();
    // Idle pool (ascending unit keys): type1, type0, type0, type0.
    const std::vector<IdleUnitView> idle = {
        {200, 1}, {205, 0}, {209, 0}, {214, 0}};
    const int claimed = recruit_idle(t, d, idle);
    CHECK(claimed == 3);
    // File-order slots, ascending claim within a type [RE: ai-team.md §2 —
    // the deterministic ascending walk]: type0 gets 205 then 209 (214 spare),
    // type1 gets 200.
    REQUIRE(t.members.size() == 3);
    CHECK(t.members[0] == 205);
    CHECK(t.members[1] == 209);
    CHECK(t.members[2] == 200);
    CHECK(t.member_types == std::vector<int>{0, 0, 1});
    CHECK(team_shortfall_type(t, d) == -1);      // complete
    // Idempotent on a complete team; partial pools claim what exists:
    CHECK(recruit_idle(t, d, idle) == 0);
    TeamInstance p = make_team();
    const std::vector<IdleUnitView> one = {{300, 0}};
    CHECK(recruit_idle(p, d, one) == 1);
    CHECK(team_shortfall_type(p, d) == 0);       // still owes a type0
}

TEST_CASE("ai_team: team_shortfall_type walks slots in file order") {
    const AimdData d = make_aimd();
    TeamInstance t = make_team();
    CHECK(team_shortfall_type(t, d) == 0);       // first slot first
    t.member_types = {0, 0};
    t.members = {1, 2};
    CHECK(team_shortfall_type(t, d) == 1);       // type0 filled -> type1 owed
    t.member_types = {0, 0, 1};
    t.members = {1, 2, 3};
    CHECK(team_shortfall_type(t, d) == -1);
}

TEST_CASE("ai_team: next_produce_type fills the oldest recruiting team, banked") {
    const AimdData d = make_aimd();
    const auto roster = unit_roster2();
    AiTeamRules rules;
    std::vector<TeamInstance> teams(2);
    teams[0].teamtype = 0;                        // oldest first
    teams[1].teamtype = 0;
    // Oldest team owes type0 (infantry, 200c): affordable at
    // cost x cost_mul + CreditReserve [RE-GATE APPLIED: ai-team.md §6 —
    // bank-never-substitute, the AI-1 affordability rule]:
    CHECK(next_produce_type(teams, d, 200.0 * 1.0 + rules.credit_reserve, 1.0,
                            roster, rules) == 0);
    CHECK(next_produce_type(teams, d, 200.0 * 1.0 + rules.credit_reserve - 1.0,
                            1.0, roster, rules) == -1);   // bank, no substitute
    // cost_mul scales the gate (difficulty handicap):
    CHECK(next_produce_type(teams, d, 200.0 * 2.0 + rules.credit_reserve - 1.0,
                            2.0, roster, rules) == -1);
    CHECK(next_produce_type(teams, d, 200.0 * 2.0 + rules.credit_reserve, 2.0,
                            roster, rules) == 0);
    // Oldest team's infantry filled -> its vehicle slot (700c) drives next:
    teams[0].member_types = {0, 0};
    teams[0].members = {9, 10};
    CHECK(next_produce_type(teams, d, 10000.0, 1.0, roster, rules) == 1);
    // Only non-Recruiting teams left -> nothing to produce:
    teams[0].state = TeamState::Executing;
    teams[1].state = TeamState::Gathering;
    CHECK(next_produce_type(teams, d, 10000.0, 1.0, roster, rules) == -1);
    // InfantryReserve GATE [RE-GATE APPLIED: ai-team.md §6]: infantry shortfall
    // is produced FREELY once credits exceed rules.infantry_reserve (50000),
    // and via the normal cost check below it.
    std::vector<TeamInstance> fresh(1);
    fresh[0].teamtype = 0;
    // Above the reserve: infantry produced freely.
    CHECK(next_produce_type(fresh, d, rules.infantry_reserve + 1.0, 1.0, roster, rules) == 0);
    // Below the reserve but above the normal cost floor: still produced.
    CHECK(next_produce_type(fresh, d, 200.0 + rules.credit_reserve, 1.0, roster, rules) == 0);
    // Below the reserve AND below the cost floor: banked, nothing starts.
    CHECK(next_produce_type(fresh, d, 200.0 + rules.credit_reserve - 1.0, 1.0, roster, rules) == -1);
    // A VEHICLE shortfall is NEVER gated by InfantryReserve — only the cost
    // check applies (below the cost floor -> nothing, even above the reserve):
    std::vector<TeamInstance> vfresh(1);
    vfresh[0].teamtype = 0;
    vfresh[0].member_types = {0, 0};             // infantry filled -> owes the vehicle
    vfresh[0].members = {1, 2};
    CHECK(next_produce_type(vfresh, d, rules.infantry_reserve + 1.0, 1.0, roster, rules) == 1);
    CHECK(next_produce_type(vfresh, d, 700.0 + rules.credit_reserve - 1.0, 1.0, roster, rules) == -1);
}

TEST_CASE("ai_team: build_slowdown_blocks is the over-queue damp, not a time multiplier") {
    AiTeamRules rules;
    // Easy (build_slowdown off): never damps [RE-GATE APPLIED: ai-team.md §6].
    CHECK_FALSE(build_slowdown_blocks(rules, /*category_has_queued_or_onhand=*/true));
    CHECK_FALSE(build_slowdown_blocks(rules, /*category_has_queued_or_onhand=*/false));
    // Normal/Difficult (build_slowdown on): blocks starting a category the house
    // already has queued/on-hand; allows it when the category is empty.
    rules.build_slowdown = true;
    CHECK(build_slowdown_blocks(rules, /*category_has_queued_or_onhand=*/true));
    CHECK_FALSE(build_slowdown_blocks(rules, /*category_has_queued_or_onhand=*/false));
}

// --- Install-gated real-rulesmd pins (skip clean without an install, exactly
//     like tests/test_ai_base.cpp's install-gated case) ---
#include "ra2assets/rules.hpp"
#include "ra2assets/ini.hpp"
#include "ra2assets/mix.hpp"
#include "asset_fixture.hpp"
#include <cstdlib>
#include <fstream>
#include <string>

TEST_CASE("ai_team: real rulesmd [AI] team knobs match the recovered constants") {
    using namespace ra2assets;
    // Borrow the shared, load-once ra2md.mix + rulesmd parse (asset_fixture)
    // instead of re-slurping the ~196 MB archive and re-parsing rulesmd.
    if (!ra2fixture::assets_available()) { MESSAGE("skipped: ra2md.mix not found"); return; }
    if (ra2fixture::rulesmd_bytes().empty()) {
        MESSAGE("skipped: rulesmd.ini not found in ra2md.mix top level or in "
                "localmd.mix/expandmd*.mix/cachemd.mix/conqmd.mix");
        return;
    }
    const Rules& rules = ra2fixture::rulesmd();

    // [RE-GATE APPLIED: ai-team.md §2/§4 — these pin the committed constants
    // against the REAL file so a rules/constant drift is caught.]
    // [DEVIATION — empirical calibration]: in the real rulesmd these team-clock
    // keys live in the [General] INI section, NOT [AI] (verified against the
    // shipped rulesmd.ini: TeamDelays/TotalAITeamCap/DissolveUnfilledTeamDelay
    // are all under [General]; the "[AI]" tag in the header comments denotes the
    // AI subsystem / Rules+offset, not the INI section). Querying [General] is
    // what makes the pin actually validate the constants against reality.
    CHECK(rules.section_double("General", "DissolveUnfilledTeamDelay", -1.0) ==
          doctest::Approx((double)ra2engine::kAiDissolveUnfilledDelayFrames));
    // TeamDelays= is a CSV; section_double parses the leading number — the
    // FILE-ORDER head. The constant array stores the recovered file order
    // {2000,2500,3500} at indices 0/1/2 as loaded [RE-GATE APPLIED: ai-team.md
    // §4], so the head equals index 0:
    CHECK(rules.section_double("General", "TeamDelays", -1.0) ==
          doctest::Approx((double)ra2engine::kAiTeamDelayFrames[0]));
    CHECK(rules.section_double("General", "AISafeDistance", -1.0) ==
          doctest::Approx((double)ra2engine::kAiSafeDistance));
}

// ---------------------------------------------------------------------------
// G-61: the real 0..64 executor numbering (script_actions.hpp, c14.log).
TEST_CASE("ai_team: classify_op follows the 0x6E94E4 executor table") {
    CHECK(classify_op(0) == OpClass::AttackQuarry);
    CHECK(classify_op(3) == OpClass::MoveWaypoint);
    CHECK(classify_op(4) == OpClass::MoveCell);
    CHECK(classify_op(5) == OpClass::TimedGuard);
    CHECK(classify_op(6) == OpClass::Jump);
    CHECK(classify_op(11) == OpClass::DoMission);
    CHECK(classify_op(16) == OpClass::MoveWaypoint);
    CHECK(classify_op(21) == OpClass::Scatter);
    CHECK(classify_op(46) == OpClass::AttackBuilding);
    CHECK(classify_op(47) == OpClass::MoveEnemyBuilding);
    CHECK(classify_op(49) == OpClass::Success);
    CHECK(classify_op(53) == OpClass::GatherEnemy);
    CHECK(classify_op(54) == OpClass::RegroupBase);
    CHECK(classify_op(58) == OpClass::MoveFriendlyBuilding);
    // W2-SK-09/10: transport load / wait / unload and deploy are live.
    CHECK(classify_op(14) == OpClass::Load);
    CHECK(classify_op(43) == OpClass::WaitLoaded);
    CHECK(classify_op(8) == OpClass::Unload);
    CHECK(classify_op(9) == OpClass::Deploy);
    // W6-03-05: 55 IronCurtainMe, 56/57 ChronoPrep are live.
    CHECK(classify_op(55) == OpClass::IronCurtainMe);
    CHECK(classify_op(56) == OpClass::ChronoPrep);
    CHECK(classify_op(57) == OpClass::ChronoPrep);
    // Berserk / removal / out-of-table -> Inert.
    for (int op : {2, 12, 60, 63, 64, 65, 0x90})
        CHECK(classify_op(op) == OpClass::Inert);
    // Quarry enum is YRpp QuarryType (1-based "Anything").
    CHECK(kQuarryAnything == 1);
    CHECK(kQuarryBuildings == 2);
    CHECK(kQuarryHarvesters == 3);
    CHECK(kQuarryFactories == 6);
    CHECK(kQuarryDefenses == 7);
}

TEST_CASE("ai_team: stock attack script 54/53/0/49/0 runs regroup, gather, attack, success") {
    AimdData d = make_aimd();
    d.scripts[0].actions = {{kSaGatherBase, 0}, {kSaGatherEnemy, 0}, {kSaAttack, kQuarryFactories},
                            {kSaSuccess, 0}, {kSaAttack, kQuarryAnything}};
    TeamInstance t = make_team();
    std::uint32_t rng = 7u;
    claim_full(t);
    t.state = TeamState::Executing;
    TeamView v = full_view(true);
    // 54: move to own base centre.
    TeamCommand c = step_team(t, v, d, rng, kDt);
    CHECK(c.kind == TeamCommand::Kind::MoveTeam);
    CHECK(c.x == v.base_x);
    CHECK(c.y == v.base_y);
    t.gather_x = 12; t.gather_y = 10;               // caller's realized focus (members there)
    c = step_team(t, v, d, rng, kDt);
    CHECK(t.script_pc == 1);
    // 53: move to a ring cell kAiSafeDistance from the enemy base.
    c = step_team(t, v, d, rng, kDt);
    CHECK(c.kind == TeamCommand::Kind::MoveTeam);
    const double r = std::hypot(c.x - v.enemy_x, c.y - v.enemy_y);
    CHECK(r == doctest::Approx((double)kAiSafeDistance).epsilon(0.1));
    t.gather_x = 12; t.gather_y = 10;
    (void)step_team(t, v, d, rng, kDt);
    CHECK(t.script_pc == 2);
    // 0,6: factories exist -> attack command with quarry 6.
    v.quarry_exists = true;
    c = step_team(t, v, d, rng, kDt);
    CHECK(c.kind == TeamCommand::Kind::AttackQuarry);
    CHECK(c.quarry == kQuarryFactories);
    // Factories gone -> advance; 49 flags success and continues to 0,1.
    v.quarry_exists = false;
    (void)step_team(t, v, d, rng, kDt);
    (void)step_team(t, v, d, rng, kDt);
    CHECK(t.completed_success);
    CHECK(t.state == TeamState::Executing);
    CHECK(t.script_pc == 4);
}

TEST_CASE("ai_team: building-typed 46/47/58 and move-to-cell 4") {
    AimdData d = make_aimd();
    d.scripts[0].actions = {{kSaMoveFriendlyBuilding, 0x20001}, {kSaAttackEnemyBuilding, 9},
                            {kSaMoveCell, 5 * 128 + 7}, {kSaDoMission, kMissionHunt}};
    TeamInstance t = make_team();
    std::uint32_t rng = 7u;
    claim_full(t);
    t.state = TeamState::Executing;
    TeamView v = full_view(true);
    // 58 with no such building -> advance at once.
    v.building_found = false;
    (void)step_team(t, v, d, rng, kDt);
    CHECK(t.script_pc == 1);
    // 46 with a target -> attack with the raw building argument.
    t.script_pc = 0;
    v.building_found = true; v.building_x = 3; v.building_y = 4;
    TeamCommand c = step_team(t, v, d, rng, kDt);
    CHECK(c.kind == TeamCommand::Kind::MoveTeam);
    CHECK(c.x == 3);
    CHECK(c.y == 4);
    t.script_pc = 1;
    t.move_issued = false;
    v.quarry_exists = true;
    c = step_team(t, v, d, rng, kDt);
    CHECK(c.kind == TeamCommand::Kind::AttackQuarry);
    CHECK(c.building_arg == 9);
    // 4: x = arg % 128, y = arg / 128.
    t.script_pc = 2;
    c = step_team(t, v, d, rng, kDt);
    CHECK(c.kind == TeamCommand::Kind::MoveTeam);
    CHECK(c.x == 7);
    CHECK(c.y == 5);
    // 11,15 (hunt) -> attack anything, line complete.
    t.script_pc = 3;
    t.move_issued = false;
    c = step_team(t, v, d, rng, kDt);
    CHECK(c.kind == TeamCommand::Kind::AttackQuarry);
    CHECK(c.quarry == kQuarryAnything);
    CHECK(t.script_pc == 4);
}
