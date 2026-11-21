#include "ai_team.hpp"
#include "orig_math.hpp"   // Net L0: the original's FastMath, never libm in the sim
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>

namespace ra2engine {

OpClass classify_op(int action) {
    // [G-61; RE: c13.log:29-33 / c14.log -- the 65-entry executor table at
    // 0x6E94E4, ModEnc numbering; script_actions.hpp]. The single table
    // location for AI-2.
    switch (action) {
        case kSaAttack:               return OpClass::AttackQuarry;
        case kSaAttackEnemyBuilding:  return OpClass::AttackBuilding;
        case kSaMoveWaypoint:         return OpClass::MoveWaypoint;
        case kSaPatrol:               return OpClass::MoveWaypoint;   // [RE-FALLBACK: patrol = move]
        case kSaAttackWaypoint:       return OpClass::MoveWaypoint;   // [RE-FALLBACK: walk there; auto-acquire fires]
        case kSaMoveCell:             return OpClass::MoveCell;
        case kSaMoveEnemyBuilding:    return OpClass::MoveEnemyBuilding;
        case kSaMoveFriendlyBuilding: return OpClass::MoveFriendlyBuilding;
        case kSaGatherEnemy:          return OpClass::GatherEnemy;
        case kSaGatherBase:           return OpClass::RegroupBase;
        case kSaGuard:                return OpClass::TimedGuard;
        case kSaDoMission:            return OpClass::DoMission;
        case kSaScatter:              return OpClass::Scatter;
        case kSaJump:                 return OpClass::Jump;
        case kSaSuccess:              return OpClass::Success;
        case kSaLoad:                 return OpClass::Load;         // W2-SK-09
        case kSaWaitLoaded:           return OpClass::WaitLoaded;   // W2-SK-09
        case kSaUnload:               return OpClass::Unload;       // W2-SK-09
        case kSaDeploy:               return OpClass::Deploy;       // W2-SK-10
        case kSaIronCurtainMe:        return OpClass::IronCurtainMe;   // W6-03-05
        case kSaChronoPrepAB:
        case kSaChronoPrepAQ:         return OpClass::ChronoPrep;      // W6-03-05
        default:                      return OpClass::Inert;
    }
}

Cellxy base_gather_cell(int bx, int by, std::uint32_t& rng) {
    const int ox = (int)(ai_rand(rng) % 7u) - 3, oy = (int)(ai_rand(rng) % 7u) - 3;
    return Cellxy{bx + ox, by + oy};
}

Cellxy gather_cell(int ex, int ey, std::uint32_t& rng) {
    // [RE-GATE APPLIED: ai-team.md §2] — a point on a circle of radius
    // kAiSafeDistance (AISafeDistance = 20 cells) around the ENEMY base centre
    // (ex,ey), at a SEEDED-xorshift32 angle from `rng` (ai_base's ai_rand
    // substituting the lockstep RNG [DEVIATION]). The caller snaps this ideal
    // ring cell to the nearest free cell via the existing move/path machinery.
    const double theta =
        (double)ai_rand(rng) / 4294967296.0 * 6.283185307179586;  // [0, 2*pi)
    const double rad = (double)kAiSafeDistance;
    return Cellxy{ex + (int)om::lround(rad * om::cos(theta)),
                  ey + (int)om::lround(rad * om::sin(theta))};
}

namespace {
// Alive-member helpers over the PARALLEL view.
int alive_count(const TeamView& v) {
    int n = 0;
    for (const TeamMemberView& m : v.members)
        if (m.alive) ++n;
    return n;
}
// All live members within `cells` of (cx,cy) — the recovered Stray/CloseEnough
// radius predicate [RE-GATE APPLIED: ai-team.md §2]. Squared-distance in cells.
bool all_within(const TeamView& v, int cx, int cy, double cells) {
    const double r2 = cells * cells;
    for (const TeamMemberView& m : v.members) {
        if (!m.alive) continue;
        const double dx = (double)(m.cx - cx), dy = (double)(m.cy - cy);
        if (dx * dx + dy * dy > r2) return false;
    }
    return true;
}
Cellxy centroid(const TeamView& v) {
    long long sx = 0, sy = 0, n = 0;
    for (const TeamMemberView& m : v.members)
        if (m.alive) { sx += m.cx; sy += m.cy; ++n; }
    if (n == 0) return Cellxy{v.base_x, v.base_y};
    return Cellxy{(int)(sx / n), (int)(sy / n)};
}
}  // namespace

TeamCommand step_team(TeamInstance& team, const TeamView& view,
                      const AimdData& aimd, std::uint32_t& rng, double dt,
                      bool multiplay) {
    TeamCommand c;
    if (team.teamtype < 0 || team.teamtype >= (int)aimd.teams.size()) {
        team.state = TeamState::Disbanding;
        return c;
    }
    const TeamType& tt = aimd.teams[team.teamtype];
    const ScriptType& sc = aimd.scripts[tt.script];
    const int alive = alive_count(view);

    // Team wiped after launch -> Disbanding, any state [RE: ai-team.md §2].
    if (team.state != TeamState::Recruiting && alive == 0) {
        team.state = TeamState::Disbanding;
        return c;
    }

    switch (team.state) {
        case TeamState::Recruiting: {
            team.recruit_timer += dt;
            const bool complete = team_shortfall_type(team, aimd) < 0;
            // Full=yes: WAIT for full TaskForce strength before leaving
            // Recruiting. Full=no (all stock offensive teams): ACT UNDER
            // STRENGTH — launch as soon as it has any member, never waiting for
            // the dissolve timer [RE-GATE APPLIED: ai-team.md §2].
            if (complete || (!tt.full && alive > 0)) {
                team.state = TeamState::Gathering;
                team.recruit_timer = 0.0;   // W2-SK-06: the muster clock
                // W2-SK-06: the pre-script muster is at the OWN base (the stock
                // scripts open with 54 regroup-at-base then 53 gather-at-enemy;
                // mustering on the enemy ring first marched every wave across
                // the map and straight back home) [RE-FALLBACK].
                const Cellxy g = base_gather_cell(view.base_x, view.base_y, rng);
                team.gather_x = g.x;
                team.gather_y = g.y;
                team.move_issued = false;
                return c;
            }
            // DissolveUnfilledTeamDelay is MULTIPLAY ONLY: only an MP unfilled
            // team dissolves on the timer. The skirmish/campaign default
            // (multiplay=false) NEVER dissolves — it keeps recruiting/producing
            // toward strength [RE-GATE APPLIED: ai-team.md §2].
            // dissolve_frames (AiTeamRules) mirrors this constant for a modded
            // [General] DissolveUnfilledTeamDelay= override, but step_team is a
            // pure lifecycle that only takes the scalars it needs (multiplay
            // here) — it has no AiTeamRules/config reach, and no other call
            // site consumes dissolve_frames either, so it stays intentionally
            // constant-backed rather than widening this MP-only signature.
            if (multiplay &&
                team.recruit_timer * 15.0 >= (double)kAiDissolveUnfilledDelayFrames)
                team.state = TeamState::Disbanding;
            return c;  // Recruiting emits no field command (caller recruits/produces)
        }

        case TeamState::Gathering: {
            // A death that reopens a slot drops a full=yes team back to
            // Recruiting [RE: ai-team.md §2].
            if (tt.full && team_shortfall_type(team, aimd) >= 0) {
                team.state = TeamState::Recruiting;
                team.recruit_timer = 0.0;
                team.move_issued = false;
                return c;
            }
            if (!team.move_issued) {
                c.kind = TeamCommand::Kind::MoveTeam;
                c.x = team.gather_x;
                c.y = team.gather_y;
                team.move_issued = true;
                return c;
            }
            // GATHERED = all live members within Rules.Stray (2.0 cells) of the
            // gather focus [RE-GATE APPLIED: ai-team.md §2].
            // W2-SK-06 [RE-FALLBACK]: an under-strength Full=no team keeps
            // recruiting while it musters (its new units join it) and leaves
            // when complete or after kAiMusterSecs -- a lone first recruit no
            // longer marches off as a 1-unit "wave".
            team.recruit_timer += dt;
            const bool filled = team_shortfall_type(team, aimd) < 0;
            if ((filled || team.recruit_timer >= kAiMusterSecs) &&
                all_within(view, team.gather_x, team.gather_y, team_reach_cells(kAiStrayCells, alive))) {
                team.state = TeamState::Executing;
                team.script_pc = 0;
                team.script_timer = 0.0;
                team.move_issued = false;
            }
            return c;
        }

        case TeamState::Executing: {
            if (team.script_pc < 0 || team.script_pc >= (int)sc.actions.size()) {
                team.state = TeamState::Disbanding;  // script ran off the end
                return c;
            }
            const ScriptAction a = sc.actions[team.script_pc];
            const OpClass oc = classify_op(a.action);
            auto advance = [&]() {
                ++team.script_pc;
                team.script_timer = 0.0;
                team.move_issued = false;
            };
            // A move line: issue once, then advance on arrival at the REALIZED
            // focus the caller stored (team.gather_x/y) [CloseEnough, ai-team.md §2].
            auto move_line = [&](int x, int y) {
                if (!team.move_issued) {
                    c.kind = TeamCommand::Kind::MoveTeam;
                    c.x = x;
                    c.y = y;
                    team.move_issued = true;
                } else if (all_within(view, team.gather_x, team.gather_y,
                                      team_reach_cells(kAiCloseEnoughCells, alive))) {
                    advance();
                }
            };

            switch (oc) {
                case OpClass::AttackQuarry:
                case OpClass::AttackBuilding: {
                    // Emit while a matching target exists; advance when the class
                    // (or the named building type) is extinct [RE: ai-team.md
                    // §3/§5]. The caller computes view.quarry_exists for THIS line.
                    if (!view.quarry_exists) { advance(); return c; }
                    c.kind = TeamCommand::Kind::AttackQuarry;
                    c.quarry = oc == OpClass::AttackQuarry ? a.argument : kQuarryBuildings;
                    if (oc == OpClass::AttackBuilding) c.building_arg = a.argument;
                    return c;
                }
                case OpClass::MoveWaypoint: {
                    const Cellxy w =
                        (a.argument >= 0 && a.argument < (int)view.waypoints.size())
                            ? view.waypoints[a.argument]
                            : Cellxy{view.enemy_x, view.enemy_y};  // [RE-FALLBACK:
                              // unmapped waypoint -> enemy base centre]
                    move_line(w.x, w.y);
                    return c;
                }
                case OpClass::MoveCell:
                    move_line(script_cell_x(a.argument), script_cell_y(a.argument));
                    return c;
                case OpClass::MoveEnemyBuilding:
                case OpClass::MoveFriendlyBuilding:
                    if (!view.building_found) { advance(); return c; }  // none of that type
                    move_line(view.building_x, view.building_y);
                    return c;
                case OpClass::GatherEnemy: {
                    // Gather on the AISafeDistance ring around the enemy base
                    // [RE: ai-team.md §2 gather rule; FUN_006eec60]. The ring cell
                    // is drawn once, when the line's move is issued.
                    if (!team.move_issued) {
                        const Cellxy g = gather_cell(view.enemy_x, view.enemy_y, rng);
                        move_line(g.x, g.y);
                    } else {
                        move_line(team.gather_x, team.gather_y);
                    }
                    return c;
                }
                case OpClass::RegroupBase:
                    // Regroup at the own base centre [FUN_006eef70; RE-FALLBACK:
                    // the ConYard-first base centre, snapped by the caller].
                    move_line(view.base_x, view.base_y);
                    return c;
                case OpClass::TimedGuard: {
                    // Guard for arg x 15 frames [RE: c14.log idx 5].
                    team.script_timer += dt * 15.0;  // accumulate FRAMES
                    if (team.script_timer >=
                        (double)(a.argument * kAiGuardTicksToFrames)) {
                        advance();
                        return c;
                    }
                    const Cellxy m = centroid(view);
                    c.kind = TeamCommand::Kind::GuardArea;
                    c.x = m.x;
                    c.y = m.y;
                    c.radius = kAiGatherRadius;
                    return c;
                }
                case OpClass::DoMission: {
                    // Assign mission <arg> and complete the line [FUN_006ecd40].
                    // Hunt (15) -> attack anything; every other mission (Area
                    // Guard 11, Guard 5, ...) -> guard where the team stands
                    // [RE-FALLBACK: engine mission set].
                    if (a.argument == kMissionHunt) {
                        c.kind = TeamCommand::Kind::AttackQuarry;
                        c.quarry = kQuarryAnything;
                    } else {
                        const Cellxy m = centroid(view);
                        c.kind = TeamCommand::Kind::GuardArea;
                        c.x = m.x;
                        c.y = m.y;
                        c.radius = kAiGatherRadius;
                    }
                    advance();
                    return c;
                }
                case OpClass::Scatter: {
                    const Cellxy m = centroid(view);
                    c.kind = TeamCommand::Kind::MoveTeam;
                    c.x = m.x;
                    c.y = m.y;
                    c.scatter = true;  // caller spreads members [c14 idx 21]
                    advance();         // one-shot
                    return c;
                }
                case OpClass::Jump: {
                    // Jump consumes the tick (a jump cycle can never spin within
                    // one tick); the argument is 1-BASED [RE: c14.log idx 6 --
                    // cursor = arg-2, then advanced => line arg-1].
                    team.script_pc = std::max(
                        0, std::min(script_jump_line(a.argument), (int)sc.actions.size()));
                    team.script_timer = 0.0;
                    team.move_issued = false;
                    return c;
                }
                case OpClass::Success: {
                    // 49 sets team+0x80 (line complete) AND team+0x84 (success)
                    // and the script CONTINUES [RE: c14.log idx 49] -- the stock
                    // attack scripts run 49 mid-way ("0,6 / 49,0 / 0,2 / 0,1").
                    // completed_success feeds AI-3's RegisterSuccess at disband.
                    team.completed_success = true;
                    advance();
                    return c;
                }
                // W2-SK-09/10 [RE: script_actions.hpp 14 FUN_006EC760 load,
                // 43 wait-until-loaded, 8 FUN_006EE670 unload, 9 FUN_006ECA30
                // deploy]: the caller fans the order out; 43 holds the line
                // until everyone is aboard [RE-FALLBACK: 60 s cap].
                case OpClass::Load:
                    // fv9a [RE: FUN_006EC760]: the handler orders ONE member per
                    // pass (QueueMission(Enter) + SetDestination(transport), then
                    // returns) and sets team+0x80 only on a pass with nobody left
                    // to order; a one-shot Load (advance at once) boarded a single
                    // member since w9fix-speed's one-per-pass fan-out, and 43
                    // WaitLoaded then sat out its 900-frame cap.
                    if (!view.load_pending) { advance(); return c; }
                    c.kind = TeamCommand::Kind::Load;
                    return c;
                case OpClass::Unload:
                    c.kind = TeamCommand::Kind::Unload;
                    advance();
                    return c;
                case OpClass::Deploy:
                    c.kind = TeamCommand::Kind::Deploy;
                    advance();
                    return c;
                // W6-03-05 [RE: FUN_006ef1d0 / FUN_006ef3c0 / FUN_006ef690]:
                // fire-and-complete, wait, or complete without firing.
                case OpClass::IronCurtainMe:
                case OpClass::ChronoPrep:
                    if (view.sw_state == 2) { c.kind = TeamCommand::Kind::Wait; return c; }
                    if (view.sw_state == 1) {
                        c.kind = oc == OpClass::IronCurtainMe ? TeamCommand::Kind::IronCurtain
                                                              : TeamCommand::Kind::ChronoShift;
                        c.quarry = a.argument;
                        c.building_arg = a.action == kSaChronoPrepAB ? a.argument : -1;
                    }
                    advance();
                    return c;
                case OpClass::WaitLoaded:
                    team.script_timer += dt * 15.0;
                    if (view.all_loaded || team.script_timer >= 900.0) advance();
                    else c.kind = TeamCommand::Kind::Wait;
                    return c;
                case OpClass::Inert:
                default: {
                    // Valid-but-unimplemented action: logged skip, pc++.
                    c.kind = TeamCommand::Kind::Inert;
                    c.opcode = a.action;
                    advance();
                    return c;
                }
            }
        }

        case TeamState::Disbanding:
        default:
            return c;  // caller releases survivors + erases the instance
    }
}

int team_shortfall_type(const TeamInstance& team, const AimdData& aimd) {
    // First unfilled TaskForce slot in FILE ORDER [RE: ai-team.md §2] —
    // count claimed members of each required type against tf.members.
    if (team.teamtype < 0 || team.teamtype >= (int)aimd.teams.size()) return -1;
    const TaskForce& tf = aimd.taskforces[aimd.teams[team.teamtype].taskforce];
    for (const TaskForceMember& m : tf.members) {
        int have = 0;
        for (int t : team.member_types)
            if (t == m.unit) ++have;
        if (have < m.count) return m.unit;
    }
    return -1;
}

namespace {
// Optional country restriction ONLY [RE-GATE APPLIED: ai-team.md §1/§4]: stock
// data is universally House=<none>, so this returns true for "<none>"/"" (any
// house) — it is NOT the eligibility gate (fielding the TaskForce is, guaranteed
// upstream by parse_aimd's art-guard-drop). A rare House=<name> team restricts
// to that country (case-insensitive).
bool house_matches(const std::string& team_house, const std::string& house_name) {
    if (team_house.empty()) return true;
    std::string a = team_house, b = house_name;
    for (char& c : a) c = (char)std::toupper((unsigned char)c);
    for (char& c : b) c = (char)std::toupper((unsigned char)c);
    return a == "<NONE>" || a == b;
}
}  // namespace

int select_autocreate_team(const AimdData& aimd, const std::string& house_name,
                           const std::vector<TeamInstance>& live, double credits,
                           const AiTeamRules& rules, std::uint32_t& rng,
                           const std::vector<char>* team_buildable) {
    auto buildable = [&](int i) {
        return !team_buildable || i >= (int)team_buildable->size() || (*team_buildable)[i];
    };
    // [RE-GATE APPLIED: ai-team.md §4] autocreate ONLY (AITriggerTypes creation
    // is AI-3); NOT is_base_defense (defensive teams out of offensive scope);
    // the TotalAITeamCap hard per-house cap; the Max live-instance cap with
    // Max=0 = UNCAPPED; the CreditReserve spend floor; highest Priority is
    // strictly preferred; the equal-highest-priority tie is broken by a SEEDED
    // ai_rand draw among the tied eligible candidates [RE-FALLBACK: ai-team.md §4
    // — all stock offensive teams are Priority=5, so ties are the common case and
    // the enemy must vary its attack-wave composition; deterministic, NEVER
    // wall-clock]. Eligibility is NOT a per-side house-token compare —
    // house_matches only applies a rare House=<name> country restriction;
    // fielding the TaskForce is guaranteed by parse_aimd's art-guard-drop (a team
    // in `aimd` has a TaskForce this house can field).
    if (credits < rules.credit_reserve) return -1;
    if ((int)live.size() >= rules.total_team_cap) return -1;  // hard per-house cap
    // Pass 1: find the highest Priority among all eligible teams.
    int best_priority = 0;
    bool any = false;
    for (int i = 0; i < (int)aimd.teams.size(); ++i) {
        const TeamType& tt = aimd.teams[i];
        if (!tt.autocreate || tt.is_base_defense ||
            !house_matches(tt.house, house_name) || !buildable(i))
            continue;
        int count = 0;
        for (const TeamInstance& t : live)
            if (t.teamtype == i) ++count;
        if (tt.max > 0 && count >= tt.max) continue;  // Max=0 -> uncapped
        if (!any || tt.priority > best_priority) {
            best_priority = tt.priority;
            any = true;
        }
    }
    if (!any) return -1;
    // Pass 2: collect every eligible team at the highest Priority (file order).
    std::vector<int> candidates;
    for (int i = 0; i < (int)aimd.teams.size(); ++i) {
        const TeamType& tt = aimd.teams[i];
        if (!tt.autocreate || tt.is_base_defense ||
            !house_matches(tt.house, house_name) || !buildable(i))
            continue;
        int count = 0;
        for (const TeamInstance& t : live)
            if (t.teamtype == i) ++count;
        if (tt.max > 0 && count >= tt.max) continue;  // Max=0 -> uncapped
        if (tt.priority == best_priority) candidates.push_back(i);
    }
    // Seeded tie-break among the equal-highest-priority eligible candidates
    // [RE-FALLBACK: ai-team.md §4]. A single candidate still draws once so the
    // RNG stream advances identically whether or not a tie exists.
    return candidates[ai_rand(rng) % (std::uint32_t)candidates.size()];
}

int recruit_idle(TeamInstance& team, const AimdData& aimd,
                 const std::vector<IdleUnitView>& idle) {
    // [RE: ai-team.md §2] recruit-idle-first: TaskForce slots in FILE ORDER,
    // ascending idle-unit walk per slot (the caller supplies `idle` in
    // ascending unit-key order) — mirrors AI-1's deterministic
    // standing-building sync. Each idle unit claimed at most once per call.
    if (team.teamtype < 0 || team.teamtype >= (int)aimd.teams.size()) return 0;
    const TaskForce& tf = aimd.taskforces[aimd.teams[team.teamtype].taskforce];
    std::vector<char> taken(idle.size(), 0);
    int claimed = 0;
    for (const TaskForceMember& m : tf.members) {
        int have = 0;
        for (int t : team.member_types)
            if (t == m.unit) ++have;
        for (std::size_t i = 0; i < idle.size() && have < m.count; ++i) {
            if (taken[i] || idle[i].unit_type != m.unit) continue;
            taken[i] = 1;
            team.members.push_back(idle[i].unit);
            team.member_types.push_back(m.unit);
            ++have;
            ++claimed;
        }
    }
    return claimed;
}

int next_produce_type(const std::vector<TeamInstance>& teams, const AimdData& aimd,
                      double credits, double cost_mul,
                      const std::vector<UnitTypeDef>& unit_roster,
                      const AiTeamRules& rules) {
    // [RE: ai-team.md §6] produce-the-shortfall: the FIRST (oldest)
    // Recruiting team's first unfilled slot drives production — one intent
    // per house tick; the caller routes infantry->tab2, vehicle->tab3 and
    // holds when that queue is busy (the single-slot queue IS the recovered
    // one-unit-at-a-time factory gate). Affordability gates the START:
    // credits >= cost x cost_mul + CreditReserve — bank, never substitute
    // (the AI-1 rule). INFANTRY shortfall is produced FREELY above
    // InfantryReserve (50000); otherwise it falls to the normal check. VEHICLE
    // shortfall always uses the normal check [RE-GATE APPLIED: ai-team.md §6].
    for (const TeamInstance& team : teams) {
        if (team.state != TeamState::Recruiting) continue;
        const int t = team_shortfall_type(team, aimd);
        if (t < 0 || t >= (int)unit_roster.size()) continue;
        const bool infantry = !unit_roster[t].is_vehicle;
        if (infantry && credits > rules.infantry_reserve) return t;
        if (credits < unit_roster[t].cost * cost_mul + rules.credit_reserve)
            return -1;  // bank income; NEVER a cheaper substitute
        return t;
    }
    return -1;
}

bool build_slowdown_blocks(const AiTeamRules& rules, bool category_has_queued_or_onhand) {
    // BuildSlowdown is a difficulty BYTE (Easy off / Normal+Difficult on), an
    // anti-spam OVER-QUEUE damp, NOT a build-time multiplier [RE-GATE APPLIED:
    // ai-team.md §6]. When on, decline to START a category the house already has
    // queued or on-hand.
    return rules.build_slowdown && category_has_queued_or_onhand;
}

}  // namespace ra2engine
