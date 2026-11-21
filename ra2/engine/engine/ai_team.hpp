#pragma once
#include <cmath>
// Pure offensive-team brain [RE: docs/re/ai-team.md]. No SDL, no World —
// consumes a read-only TeamView the caller (World) fills, plus per-house
// AiTeamState. doctest-testable. EVERY numeric constant below is a HUNT
// OUTPUT recorded in docs/re/ai-team.md; none is a guess — tests assert
// against the named constants so a RE-GATE amendment updates constant, doc,
// and test together. Reuses ai_base.hpp's ai_rand xorshift32 + the AI-1
// carry-forward constants (kAiSafeDistance/kAiCreditReserve/kAiInfantryReserve).
#include <cstdint>
#include <string>
#include <vector>
#include "ai_base.hpp"     // ai_rand, kAiSafeDistance, kAiCreditReserve, kAiInfantryReserve, AiDifficulty
#include "orig_math.hpp"   // Net L0: the original's FastMath, never libm in the sim
#include "aimd_parse.hpp"  // AimdData / TeamType / TaskForce / ScriptType
#include "pathfind.hpp"    // Cellxy
#include "unit_type.hpp"   // UnitTypeDef (production intent)
#include "script_actions.hpp"  // the shared 0..64 ScriptType table [G-61]

namespace ra2engine {

// --- Recovered team-clock / lifecycle constants [RE-GATE APPLIED: ai-team.md
// §2/§4 — the hunt PINNED the real values and clarified the two-clock model:
// TeamDelays (frames, per-difficulty) gates the team-management CHECK pass;
// AutocreateTime (minutes) gates the actual autocreate SPAWN] ---
// [AI] TeamDelays= (Rules+0x1158) frames between team-management CHECK passes,
// indexed by AiDifficulty (Easy/Normal/Difficult). Recovered file order is
// 2000,2500,3500; the index binding carries the SAME hardest<->easiest caveat
// as ai-base.md's count arrays [RE-FALLBACK: index 0/1/2 as loaded, default the
// middle 2500] [RE-GATE APPLIED: ai-team.md §4].
constexpr int kAiTeamDelayFrames[3] = {2000, 2500, 3500};
// [AI] AutocreateTime= (Rules+0x1510, double MINUTES) — average minutes between
// creating an autocreate team; gates the actual SPAWN (the global scheduler
// latch) [RE-GATE APPLIED: ai-team.md §4]. 1 min = 900 frames at 15 Hz.
constexpr double kAiAutocreateTimeMin = 1.0;
// [General] TotalAITeamCap= (Rules+0x13c8) hard cap on simultaneous AI teams
// per house [RE-GATE APPLIED: ai-team.md §4].
constexpr int kAiTotalTeamCap = 30;
// [AI] DissolveUnfilledTeamDelay= (Rules+0x1190) frames an unfilled team waits
// before disbanding — rulesmd tags it MULTIPLAY ONLY, so the default skirmish/
// campaign path does NOT auto-dissolve on this timer (an unfilled Full=no team
// keeps recruiting/producing) [RE-GATE APPLIED: ai-team.md §2].
constexpr int kAiDissolveUnfilledDelayFrames = 5000;
// GuardArea radius handed to the caller (guard anchor spread), cells
// [RE-FALLBACK: engine bound, not a binary constant].
constexpr int kAiGatherRadius = 3;
// "Gathered" / arrival predicate radii [RE-GATE APPLIED: ai-team.md §2 — the
// recovered rulesmd values]: a team is GATHERED when all live members are
// within Rules.Stray (+0x171c = 2.0 cells) of the gather focus; a move ARRIVES
// when all live members are within Rules.CloseEnough (+0x1718 = 2.25 cells).
constexpr double kAiStrayCells = 2.0;
constexpr double kAiCloseEnoughCells = 2.25;
// W2-SK-06 [RE-FALLBACK]: longest an under-strength team musters (seconds).
constexpr double kAiMusterSecs = 60.0;
// W2-SK-06 [RE-FALLBACK: the original packs up to 5 infantry per cell and
// measures Stray/CloseEnough in leptons; this engine holds ONE unit per cell,
// so a big team could never fit inside a 2-cell disc and its gather / move
// line never completed]. The radius grows with the live member count so the
// disc holds twice that many cells (room for obstacles): max(base, sqrt(2n / pi) + 1).
inline double team_reach_cells(double base, int alive) {
    const double r = alive > 0 ? om::sqrt(2.0 * (double)alive / 3.14159265358979) + 1.0 : 0.0;
    return r > base ? r : base;
}
// Timed-guard argument unit [RE: c14.log idx 5 -- the handler stores arg x 15
// into the team's frame timer; ai-team.md §3 amended (the old x90 "tenths of a
// minute" reading was the RA1 lineage, not this binary)].
constexpr int kAiGuardTicksToFrames = kScriptGuardFramesPerArg;

// --- ScriptType opcode numbers [G-61; RE: script_actions.hpp -- the 65-entry
// executor jump table at 0x6E94E4 (c13/c14 logs), ModEnc numbering, SHARED with
// the P10 campaign interpreter]. The kOp* names are AI-2 aliases of the shared
// kSa* table for the actions this interpreter implements. ---
constexpr int kOpAttackQuarry = kSaAttack;                    // 0
constexpr int kOpAttackWaypoint = kSaAttackWaypoint;          // 1
constexpr int kOpMoveToWaypoint = kSaMoveWaypoint;            // 3
constexpr int kOpMoveToCell = kSaMoveCell;                    // 4
constexpr int kOpTimedGuard = kSaGuard;                       // 5
constexpr int kOpJumpToLine = kSaJump;                        // 6 (1-based argument)
constexpr int kOpDoMission = kSaDoMission;                    // 11
constexpr int kOpPatrol = kSaPatrol;                          // 16
constexpr int kOpScatter = kSaScatter;                        // 21
constexpr int kOpAttackEnemyBuilding = kSaAttackEnemyBuilding;// 46
constexpr int kOpMoveEnemyBuilding = kSaMoveEnemyBuilding;    // 47
constexpr int kOpSuccess = kSaSuccess;                        // 49 (flag + continue)
constexpr int kOpDisband = kSaSuccess;                        // legacy name of 49
constexpr int kOpGatherEnemy = kSaGatherEnemy;                // 53
constexpr int kOpRegroupBase = kSaGatherBase;                 // 54
constexpr int kOpMoveFriendlyBuilding = kSaMoveFriendlyBuilding;  // 58

// Quarry kinds (action 0's argument) are the shared YRpp QuarryType values in
// script_actions.hpp (kQuarryAnything = 1, kQuarryBuildings = 2, ...).
constexpr int kQuarryBaseThreats = kQuarryThreats;

// Opcode classes the interpreter dispatches on. classify_op maps EVERY action
// number 0..64 to one of these; transport (8/9/14/43), superweapon (55-57),
// cosmetic and house-changing actions, berserk (2: the binary's handler IS the
// epilogue), and 60-64 (conditional member removal; no member ever matches in
// this engine) are Inert (logged skip, pc++).
enum class OpClass {
    AttackQuarry, AttackBuilding, MoveWaypoint, MoveCell, MoveEnemyBuilding,
    MoveFriendlyBuilding, GatherEnemy, RegroupBase, TimedGuard, DoMission,
    Scatter, Jump, Success, Inert,
    Load, WaitLoaded, Unload, Deploy,  // W2-SK-09/10: 14 / 43 / 8 / 9
    IronCurtainMe, ChronoPrep          // W6-03-05: 55 / 56-57
};
OpClass classify_op(int action);  // [G-61: the 0..64 executor table]

// Lifecycle [RE: ai-team.md §2]: Recruiting -> Gathering -> Executing ->
// Disbanding.
enum class TeamState { Recruiting = 0, Gathering = 1, Executing = 2, Disbanding = 3 };

// Read-only per-team snapshot, filled by the caller (World) each tick.
struct TeamMemberView {
    int unit = -1;       // caller's unit key (World unit index)
    bool alive = false;  // false = died this tick (caller prunes next tick)
    int cx = 0, cy = 0;  // current cell
    bool idle = false;   // not moving, no live attack pursuit
};
struct TeamView {
    std::vector<TeamMemberView> members;  // PARALLEL to TeamInstance::members
    std::vector<Cellxy> waypoints;        // waypoint index -> cell (caller table)
    int base_x = 0, base_y = 0;           // own base center (gather anchor)
    int enemy_x = 0, enemy_y = 0;         // enemy base centroid
    bool quarry_exists = false;           // a target matching the CURRENT attack
                                          // action's effective quarry class exists
                                          // (caller-computed; consulted on any of
                                          // AttackQuarry/House/Building/Units)
    // Building-typed actions 46/47/58 [G-61]: the caller resolves the line's
    // argument (BuildingTypes index | selector<<16) against the right house's
    // buildings and reports the chosen building's cell here.
    bool building_found = false;
    int building_x = 0, building_y = 0;
    // W2-SK-09: every non-transport live member is aboard the team's
    // transport (true when the team has no transport).
    bool all_loaded = true;
    // fv9a [RE: FUN_006EC760]: a member still has to be told to board (alive,
    // not aboard, loadable, not already entering the transport). The Load line
    // holds while this is true -- the handler orders ONE member per pass and
    // sets the line-complete flag (team+0x80) only on a pass that orders none.
    bool load_pending = false;
    // W6-03-05 [RE: FUN_006ef1d0 (55) / FUN_006ef3c0 (56) / FUN_006ef690
    // (57)], for the CURRENT line: 1 = the house's Curtain/Sphere is charged
    // and its power is full -> fire and complete; 2 = still charging but past
    // AIMinorSuperReadyPercent -> the line waits; 0 = absent / not near ->
    // the line completes without firing.
    int sw_state = 0;
};

// One command per team per tick; the caller fans it out per member
// [RE: ai-team.md §3 — spec §3's opcode-class -> command table].
struct TeamCommand {
    enum class Kind { None, MoveTeam, AttackQuarry, GuardArea, Wait, Inert,
                      Load, Unload, Deploy,     // W2-SK-09/10
                      IronCurtain, ChronoShift };   // W6-03-05 (55 / 56-57)
    Kind kind = Kind::None;
    int x = 0, y = 0;      // MoveTeam goal / GuardArea center
    int radius = 0;        // GuardArea radius (kAiGatherRadius)
    int quarry = 0;        // AttackQuarry kind [RE: ai-team.md §5]
    int opcode = -1;       // Inert: the skipped action number (witness logging)
    bool scatter = false;  // MoveTeam: spread members instead of one goal
    int building_arg = -1; // AttackQuarry from action 46: the raw argument
                           // (type | selector<<16) -- attack THAT building
};

// One live team [RE: ai-team.md §2]. members/member_types are maintained by
// the caller (claim on recruit/launch, erase on death) and stay PARALLEL.
struct TeamInstance {
    int teamtype = -1;               // AimdData::teams index
    TeamState state = TeamState::Recruiting;
    std::vector<int> members;        // caller unit keys (World indices)
    std::vector<int> member_types;   // unit-roster index per member (claim-stamped)
    int script_pc = 0;               // ScriptType action cursor [RE: ai-team.md §3]
    double script_timer = 0.0;       // dt-accumulated timer for timed actions
    double recruit_timer = 0.0;      // full=no timeout clock [RE: ai-team.md §2]
    int gather_x = 0, gather_y = 0;  // REALIZED move focus: the caller snaps the
                                     // ideal gather/move cell to the nearest free
                                     // cell and writes it back here, so the pure
                                     // arrival predicate is satisfiable [RE: §2].
    bool move_issued = false;        // the current move/gather order went out once
    int stall_reissues = 0;          // World-only backstop counter: consecutive
                                     // stalled re-issues of the CURRENT Executing
                                     // move before the pc is force-advanced (the
                                     // wedge-freedom guarantee) [RE: ai-team.md §2].
    // [G-60] move-progress backstop: the smallest summed member distance^2 to
    // the realized focus seen on this move, and the seconds since it last
    // improved (a team whose members keep "moving" while blocked in a crowd
    // never satisfied the old !any_moving stall test).
    long long best_focus_d2 = -1;
    double no_progress_secs = 0.0;
    bool completed_success = false;  // AI-3 [RE: ai-trigger.md §6]: set TRUE only
                                     // when the team runs its script's Disband
                                     // (kOpDisband) success action; stays FALSE
                                     // when it goes Disbanding via wipe (zero
                                     // members). Task 4 reads it at disband to
                                     // pick RegisterSuccess vs RegisterFailure
                                     // (adjust_weight). ADDITIVE — no AI-2
                                     // lifecycle transition changes.
};

// Per-house team state [RE: ai-team.md §2/§4]. `rng` is the engine-local
// seeded xorshift32 (ai_base's ai_rand) reserved for any hunt-recovered
// tie-break [RE-FALLBACK: ai-team.md deviations — deterministic, NEVER
// wall-clock]. teams_created feeds the gate hash + witnesses.
struct AiTeamState {
    std::vector<TeamInstance> teams;
    double check_clock = 0.0;  // FRAMES since the last TeamDelays management pass [§4] (Net L0)
    double spawn_clock = 0.0;  // FRAMES accrued toward the AutocreateTime spawn [§4] (Net L0)
    std::uint32_t rng = 0x2545F491u;  // deterministic fixed seed
    int teams_created = 0;     // lifetime counter (witness + hash)
};

// The team knobs, defaulting to the recovered constants (main.cpp re-reads
// them from the real rulesmd; tests use the defaults) [RE: ai-team.md §2/§4/§6].
struct AiTeamRules {
    int team_delay_frames = kAiTeamDelayFrames[1];          // Normal check pass [RE: §4]
    double autocreate_time_min = kAiAutocreateTimeMin;      // spawn cadence, minutes [RE: §4]
    int total_team_cap = kAiTotalTeamCap;                   // hard per-house cap [RE: §4]
    int dissolve_frames = kAiDissolveUnfilledDelayFrames;   // MP-only unfilled dissolve [RE: §2]
    bool multiplay = false;                                 // false = skirmish: NEVER dissolve on the timer [RE: §2]
    double credit_reserve = kAiCreditReserve;               // general spend floor (100) [RE: ai-base.md §2/§6]
    int infantry_reserve = kAiInfantryReserve;              // build infantry FREELY above this (50000) [RE-GATE: §6]
    bool build_slowdown = false;                            // difficulty BYTE: over-queue damp, NOT a time mul [RE-GATE: §6]
};

// Gather cell [RE-GATE APPLIED: ai-team.md §2 — the recovered rule]: a point on
// a circle of radius kAiSafeDistance (Rules.AISafeDistance = 20 cells) around the
// ENEMY/target base centre (ex,ey), at a SEEDED-xorshift32 angle drawn from
// `rng` (ai_base's ai_rand, substituting the lockstep RNG). The caller snaps the
// returned ideal ring cell to the nearest free cell via the existing move/path
// machinery (the original's FUN_0056d750 snap; [DEVIATION: engine snap]).
// W2-SK-06: the pre-script muster cell near the OWN base centre (+-3 jitter).
Cellxy base_gather_cell(int bx, int by, std::uint32_t& rng);
Cellxy gather_cell(int ex, int ey, std::uint32_t& rng);

// Advance `team` ONE tick [RE: ai-team.md §2/§3]: lifecycle transitions +
// the ScriptType interpreter (one action per tick; Jump consumes a tick, so
// jump cycles can never spin within a tick). Emits the single TeamCommand the
// caller executes this tick. `rng` (ai_base's ai_rand) draws the gather-ring
// angle and any hunt-recovered tie-break [DEVIATION: seeded xorshift32 for the
// lockstep RNG]. `multiplay` gates the DissolveUnfilledTeamDelay timer: false
// (skirmish/campaign default) NEVER dissolves an unfilled team on the timer; a
// Full=no team keeps recruiting/producing and launches as soon as it has any
// member [RE-GATE APPLIED: ai-team.md §2]. The caller prunes dead members BEFORE
// calling (members/member_types shrink); strength is read via
// team_shortfall_type + view.members.
TeamCommand step_team(TeamInstance& team, const TeamView& view,
                      const AimdData& aimd, std::uint32_t& rng, double dt,
                      bool multiplay = false);

// --- Task 4 (recruitment + production intent) — declared here, defined there ---

// First unfilled TaskForce slot's unit-roster type, walking tf.members in
// file order against the claimed member_types; -1 = team complete
// [RE: ai-team.md §2 recruit-vs-produce].
int team_shortfall_type(const TeamInstance& team, const AimdData& aimd);

// An idle own combat unit available for recruitment (caller-filled,
// ascending unit-key order).
struct IdleUnitView { int unit = -1; int unit_type = -1; };

// Recruit-idle-first [RE: ai-team.md §2]: walk the TaskForce members in file
// order; for each unfilled count, claim ascending idle units of that type
// (each idle unit at most once per call). Appends to team.members/
// member_types. Returns the number claimed. Deterministic.
int recruit_idle(TeamInstance& team, const AimdData& aimd,
                 const std::vector<IdleUnitView>& idle);

// Autocreate selection [RE-GATE APPLIED: ai-team.md §4]: among TeamTypes that
// are autocreate=true AND NOT is_base_defense (defensive teams are out of the
// AI-2 offensive scope, §1). Eligibility is NOT a per-side house-token compare:
// stock data is universally House=<none>, so the real gate is whether the house
// can FIELD the TaskForce — which parse_aimd already guarantees (a team survives
// the art-guard-drop only if every UNITID in its TaskForce resolved in THIS
// house's roster). `house_name` applies ONLY the optional country restriction
// of a rare House=<name> team (case-insensitive; "<none>"/"" matches any).
// Caps: Max (+0xb8) live instances — Max=0 = UNCAPPED (not zero!), else a team
// is eligible while cntInstances < Max; plus the hard per-house
// rules.total_team_cap (TotalAITeamCap=30). Also gated on
// credits >= rules.credit_reserve (the general spend floor). Higher Priority is
// strictly preferred; the equal-highest-priority tie is broken by a seeded
// ai_rand draw among the equal-highest-priority eligible teams
// [RE-FALLBACK: ai-team.md §4 — stock offensive teams are all Priority=5, so
// ties are the common case; deterministic seeded xorshift32, NEVER wall-clock].
// `rng` is the caller's seeded ai_rand stream (AiTeamState::rng). Returns the
// AimdData::teams index or -1. ONE team per call — the caller enforces one per
// autocreate-spawn tick. AITriggerTypes-driven creation is AI-3: this function
// is the whole AI-2 creation surface.
// [G-60] `team_buildable` (per TeamType, optional): a team whose TaskForce the
// house cannot currently build is not eligible -- the same buildable test the
// AITrigger ConditionMet applies (FUN_0041ff00, ai-trigger.md §5).
int select_autocreate_team(const AimdData& aimd, const std::string& house_name,
                           const std::vector<TeamInstance>& live, double credits,
                           const AiTeamRules& rules, std::uint32_t& rng,
                           const std::vector<char>* team_buildable = nullptr);

// Production intent [RE: ai-team.md §6]: the unit-roster type the house should
// start producing NEXT — the shortfall type of the FIRST (oldest) Recruiting
// team. Returns -1 when nothing should start. InfantryReserve GATE [RE-GATE
// APPLIED: ai-team.md §6 — the recovered rule]: an INFANTRY shortfall is
// produced FREELY while `credits > rules.infantry_reserve` (50000); otherwise it
// falls to the normal affordability check `credits >= cost x cost_mul +
// credit_reserve` (bank, never substitute — the AI-1 rule). Vehicle shortfall
// always uses the normal affordability check.
int next_produce_type(const std::vector<TeamInstance>& teams, const AimdData& aimd,
                      double credits, double cost_mul,
                      const std::vector<UnitTypeDef>& unit_roster,
                      const AiTeamRules& rules);

// BuildSlowdown over-queue DAMP [RE-GATE APPLIED: ai-team.md §6]: BuildSlowdown
// is a difficulty BYTE (Easy off / Normal+Difficult on), an anti-spam damp —
// "don't queue more of a category the AI already has queued/on-hand" — NOT a
// build-time multiplier. Returns true when the damp should BLOCK starting
// another of a category the house already has queued/on-hand (i.e. when
// rules.build_slowdown && category_has_queued_or_onhand). Easy (build_slowdown
// off) never damps.
bool build_slowdown_blocks(const AiTeamRules& rules, bool category_has_queued_or_onhand);

}  // namespace ra2engine
