#pragma once
// Pure construction economy: the single BuildingType queue, credits, power.
// Calibration: docs/re/construction.md (§ refs inline). No SDL/OS.
#include <algorithm>
#include <string>
#include <vector>
#include "building.hpp"
#include "unit_type.hpp"

namespace ra2engine {

// [RE: construction.md §7] gamemd computes ftol(Cost x BuildSpeed x 0.9) in
// EXACTLY that operation order (FUN_00711280). Do NOT algebraically
// rearrange to /1000*900: IEEE rounding differs (503/1889 instead of the
// recovered 504/1890 pins for GAPOWR/GACNST).
int build_frames(int cost, double build_speed);   // = (int)(cost * build_speed * 0.9)

// Power totals over ALIVE, non-Dead buildings of `house`. Buildings still in
// Buildup do NOT contribute output or drain [RE: construction.md §8,
// pre-hunt: power applies when operational].
struct PowerTotals { int output = 0; int drain = 0; };   // drain stored positive
PowerTotals power_totals(const std::vector<Building>& buildings,
                         const std::vector<BuildingTypeDef>& types, int house);

// [RE-GATE APPLIED: construction.md §8] low power = drain > output. Recovered
// rule (FUN_004FCC50 + the production-speed chain): with f = output/drain,
//   speed = 1 - low_power_penalty_modifier * (1 - f)        (0 output -> 0)
//   speed = max(speed, kMinLowPowerSpeed)                    (= .5, rulesmd)
//   speed = min(speed, kMaxLowPowerSpeed)   [low power only] (= .8, rulesmd)
// Full power -> exactly 1.0. With modifier=1 and any shortage, the result is
// therefore always in [.5, .8].
constexpr double kMinLowPowerSpeed = 0.5;   // rulesmd MinLowPowerProductionSpeed
constexpr double kMaxLowPowerSpeed = 0.8;   // rulesmd MaxLowPowerProductionSpeed
double build_speed_multiplier(const PowerTotals& p, double low_power_penalty_modifier);

// One-slot BuildingType queue [RE-GATE APPLIED: construction.md §10 —
// FactoryClass's 54-step incremental economy]. Production advances in
// exactly kProductionSteps stages; each stage lasts `step_ticks` 15 Hz
// frames (= clamp(trunc(total_frames/54), 1, 255)); entering stage s+1
// charges `(cost - spent) / (54 - s)` credits — equal cost/54 chunks under
// uninterrupted funding. When a stage's charge is unaffordable the build
// STALLS (no progress, no charge, retried every tick).
constexpr int kProductionSteps = 54;
struct QueueSlot {
    int type = -1;              // roster index; -1 = idle
    int stage = 0;              // 0..kProductionSteps
    int step_ticks = 1;         // 15 Hz frames per stage
    double frames_accum = 0.0;  // fractional frames toward the next stage
    int total_frames = 1;       // Time_To_Build (the 504-frame pin) — display/witness
    double spent = 0.0;         // credits deducted so far
    bool ready = false;         // finished, waiting for placement
    // Effective per-build charge total [RE-GATE APPLIED: economy.md §6a].
    // Resolved ONCE by start(): types[type].charge when the caller set one
    // (> 0 — e.g. GAREFN/NAREFN's FreeUnit-subtracted 600), else
    // types[type].cost (every pre-economy-phase caller/test that never
    // touches BuildingTypeDef::charge, which defaults to 0). tick()'s
    // per-stage charge math and its final settle-to-exact-total line both
    // read THIS field, never types[...].cost/.charge directly — `types` is a
    // const ref shared across every build of that type, so resolving the
    // "charge unset -> use cost" fallback into the per-slot state here
    // (instead of mutating the roster) keeps concurrent/repeat builds of the
    // same type independent. build_frames (wall-clock build TIME) is
    // unaffected — it stays on raw cost, computed once in start() below.
    int charge_basis = 0;
};

struct ProductionState {
    double credits = 0.0;
    QueueSlot slot;
};

// Prerequisite check [RE: construction.md §10]: every token must be satisfied.
// A plain token = an ALIVE, IDLE (fully built) building of that id owned by
// `house` exists. Group tokens (POWER/BARRACKS/PROC/FACTORY/RADAR/TECH) =
// any id in `groups[token]` satisfied the same way. Unknown tokens fail.
bool prerequisites_met(const std::vector<std::string>& tokens,
                       const std::vector<Building>& buildings,
                       const std::vector<BuildingTypeDef>& types, int house,
                       const std::vector<std::pair<std::string, std::vector<std::string>>>& groups);

// Can `type` start building? conyard_alive = an own alive Idle ConYard exists
// (the BuildingType factory); slot must be idle; prereqs met; TechLevel > 0.
// Affordability is NOT gated here — RA2 starts a build with any credits and
// stalls at 0 [RE: construction.md §10].
bool can_start(const ProductionState& ps, int type,
               const std::vector<BuildingTypeDef>& types,
               const std::vector<Building>& buildings, int house,
               const std::vector<std::pair<std::string, std::vector<std::string>>>& groups,
               bool conyard_alive);

// Start building `type` (caller checked can_start): total_frames =
// build_frames(cost, build_speed); step_ticks = clamp(total_frames /
// kProductionSteps, 1, 255) (integer division = the binary's trunc)
// [RE-GATE: construction.md §10 FUN_004C9DC0]. GAPOWR: 504 -> step_ticks 9
// (wall duration 54*9 = 486 frames = 32.4 s). Also resolves
// QueueSlot::charge_basis = types[type].charge > 0 ? types[type].charge :
// types[type].cost [RE-GATE: economy.md §6a] — the "charge unset" fallback;
// see QueueSlot's comment.
void start(ProductionState& ps, int type, const std::vector<BuildingTypeDef>& types,
           double build_speed);

// Advance by dt seconds [RE: construction.md §10; charge basis vs. cost —
// economy.md §6a]:
//   frames_accum += dt * 15 * speed_mult;
//   while (frames_accum >= step_ticks && stage < kProductionSteps):
//     charge = (charge_basis - spent) / (kProductionSteps - stage);
//     if (credits < charge) { frames_accum = 0.0; break; }  // stall
//     credits -= charge; spent += charge; ++stage; frames_accum -= step_ticks;
//   stage == kProductionSteps -> spent settles to exactly charge_basis, ready = true.
// charge_basis (see QueueSlot) is cost for every type with no FreeUnit, and
// the caller's FreeUnit-subtracted amount otherwise — see start() below.
// The stall reset (frames_accum = 0.0) models the original's free-running
// stage timer, which restarts after a broke expiry (rolled-back stage,
// retried on the NEXT timer expiry) — a funding gap adds 0..step_ticks
// latency and never produces a catch-up burst. No-op when idle/ready.
void tick(ProductionState& ps, double dt, double speed_mult,
          const std::vector<BuildingTypeDef>& types);

// Cancel: refund everything spent so far, clear the slot
// [RE-GATE: construction.md §10 — Abandon refunds Cost_Of minus balance,
// i.e. ALL spent].
void cancel(ProductionState& ps);

// Placement succeeded: clear the slot (credits already fully spent).
void placed(ProductionState& ps);

// Scalar production entry point [RE-GATE APPLIED: production.md §1.5 — the
// FactoryClass 54-step economy is shared by ALL categories (FactoryClass::Set
// FUN_004C9B90 is the single entry for Structure/Defense/Infantry/Vehicle);
// build_frames from raw Cost, immediate exit on completion for units,
// READY-hold for structures/defenses].
// Decouples the queue math from the BuildingType roster: total_frames =
// build_frames(cost, build_speed); step_ticks = clamp(total_frames/54, 1, 255);
// charge_basis = charge (caller passes cost when there is no FreeUnit — units
// always do). tick()/cancel()/placed() are reused verbatim. The existing
// start(ps, type, types, build_speed) becomes a one-line wrapper over this
// (types[type].cost, and charge = types[type].charge>0 ? types[type].charge :
// types[type].cost) so the BuildingType path stays BIT-IDENTICAL.
void start(ProductionState& ps, int cost, int charge, double build_speed, double time_mult = 1.0);

// Can unit `unit_type` start in queue `ps`? factory_alive = an own alive Idle
// factory of this queue's category exists; slot idle; TechLevel > 0; prereqs
// met by scanning BUILDINGS (a unit's prereq is a BARRACKS/FACTORY building),
// reusing prerequisites_met. Affordability is NOT gated (RA2 starts at any
// credits and stalls at 0). `unit_roster`/`unit_type` name the UnitTypeDef.
bool can_start_unit(const ProductionState& ps, int unit_type,
                    const std::vector<UnitTypeDef>& unit_roster,
                    const std::vector<Building>& buildings,
                    const std::vector<BuildingTypeDef>& btypes, int house,
                    const std::vector<std::pair<std::string, std::vector<std::string>>>& groups,
                    bool factory_alive);

// The FOUR concurrent production queues [RE-GATE APPLIED: production.md §1.1 —
// per-house per-category FactoryClass queues; Structures (+0x53BC) and
// Defenses (+0x53CC, BuildCat==Combat) are SEPARATE CONCURRENT queues, so this
// phase's four sidebar tabs map 1:1 to four queues: tab0=Structure,
// tab1=Defense, tab2=Infantry, tab3=Vehicle. Clicking a structure and a
// defense builds BOTH at once from the same ConYard (two different
// FactoryClass objects); both building queues gate on the ConYard-alive
// factory group and share ONE ConYard factory-count slot (+0x5384) for the
// MultipleFactory speedup — no struct/def split for the multiplier
// (§1.1/§1.2)]. Additive: the Structure queue's ProductionState is
// stepped/started EXACTLY as before (demo-base/demo-econ untouched — those
// never construct a ProductionQueues; demo-base builds GAPOWR, a Structure,
// so it stays byte-identical). Infantry/Vehicle queues draw from a
// UnitTypeDef roster and, on completion, raise a ready-to-exit flag consumed
// by World (Task 5); Structure/Defense queues READY-hold for placement.
// DIVERGENCE (deliberate simplification — see Task 9 README, do not "fix" in
// review): RA2 queues up to MaximumQueuedObjects=29 items per factory
// (production.md §1.5); this phase keeps a SINGLE slot per queue (one item
// building + ready).
// Gap owner A (G-24) [RE: roster2.md TL;DR 7]: Naval=yes UnitType factories
// (GAYARD/NAYARD) and AircraftType factories (GAAIRC/AMRADR) are their OWN
// FactoryClass queues, concurrent with the War Factory's. Appended AFTER the
// original four so every existing index (and the AI's 4-queue tick) is
// unchanged; the sidebar shows all three unit kinds on tab 3.
enum class QueueCat { Structure, Defense, Infantry, Vehicle, Naval, Aircraft };
constexpr int kQueueCats = 6;

class ProductionQueues {
public:
    friend struct SaveAccess;  // P7 Save/Load (engine/save_fields.hpp)
    ProductionQueues() = default;

    // The existing single-queue economy IS the Structure queue (demo-base
    // builds GAPOWR, a Structure — byte-identical path).
    ProductionState& structure() { return q_[0]; }
    const ProductionState& structure() const { return q_[0]; }
    ProductionState& state(QueueCat c) { return q_[(int)c]; }
    const ProductionState& state(QueueCat c) const { return q_[(int)c]; }

    // Wire the credit pool: all four queues SHARE the player's credits, so
    // set the same value on every ProductionState each frame before tick()
    // (mirrors how ps.credits is the single money pool). Call once at setup +
    // whenever credits change outside tick (income, placement).
    void set_credits(double credits) { for (auto& q : q_) q.credits = credits; }
    double credits() const { return q_[0].credits; }

    // Advance every non-idle queue by dt. Each queue's speed = power_mult x its
    // MultipleFactory speedup (factory_speedup[cat]; Structure and Defense
    // receive the SAME ConYard-count speedup — buildings collapse to one
    // factory-count slot, §1.1). The four queues share ONE money pool, so tick
    // SEQUENTIALLY (Structure -> Defense -> Infantry -> Vehicle) carrying the
    // running balance across queues — total spend can never exceed the money,
    // and the order is deterministic. Caller sets all four credits equal via
    // set_credits() first; credits() reads the final pool balance after.
    // [RE-GATE APPLIED: production.md §1.5 — shared FactoryClass timing]
    void tick(double dt, double power_mult, const double factory_speedup[4],
              const std::vector<BuildingTypeDef>& btypes);
    // All six queues (gap owner A): factory_speedup has kQueueCats entries.
    // The 4-entry overload above ticks Naval/Aircraft at speedup 1.0.
    void tick_all(double dt, double power_mult, const double factory_speedup[kQueueCats],
                  const std::vector<BuildingTypeDef>& btypes);

    // Reconcile the pool after cancel(): all four queues already share the same
    // balance (cancel() broadcasts each refund into the pool as it happens — see
    // below), so this is just a defensive re-broadcast of the max. Caller reads
    // credits() after. Kept for API symmetry with the start/tick flow.
    void reconcile_refund() {
        double m = q_[0].credits;
        for (int i = 1; i < kQueueCats; ++i) m = std::max(m, q_[i].credits);
        for (auto& q : q_) q.credits = m;
    }

    // Start a unit build in the Infantry/Vehicle queue (caller checked
    // can_start_unit). charge == cost (§1.5, no FreeUnit).
    void start_unit(QueueCat c, int unit_type, const std::vector<UnitTypeDef>& unit_roster,
                    double build_speed);

    // Cancel queue `c`: refund everything it spent, then BROADCAST the resulting
    // balance to all four queues so the refund lands in the shared pool
    // immediately. This makes multiple cancels in one frame ACCUMULATE — e.g. a
    // lost ConYard cancels BOTH building queues (prod_tick), and each refund
    // must survive. A prior version only raised the cancelled queue's own
    // credits and relied on reconcile_refund's max(), which silently destroyed
    // all but the largest refund when >1 queue was cancelled. Because tick()
    // leaves all four queues at the same balance, the cancelled queue's
    // post-refund credits IS the new shared total; broadcasting it lets the next
    // cancel build on top of it.
    void cancel(QueueCat c) {
        ra2engine::cancel(q_[(int)c]);
        const double pool = q_[(int)c].credits;
        for (auto& q : q_) q.credits = pool;
    }
    // Queue slot readiness (Task 5 consumes this for the unit queues).
    bool ready(QueueCat c) const { return q_[(int)c].slot.ready; }
    int producing(QueueCat c) const { return q_[(int)c].slot.type; }
    void placed(QueueCat c) { ra2engine::placed(q_[(int)c]); }

private:
    ProductionState q_[kQueueCats];   // Structure, Defense, Infantry, Vehicle, Naval, Aircraft
};

}  // namespace ra2engine
