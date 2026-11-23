#pragma once
// Pure harvester mission logic [RE: docs/re/economy.md]. Steps every
// harvester Unit through SeekOre -> ... -> Unloading, mutating the OreField
// and crediting `house_credits`. Movement itself stays in step_units: this
// module only emits HarvestMove requests (the step_combat MoveRequest
// pattern) that World paths afterwards. No SDL/OS.
#include <vector>
#include "occupancy.hpp"  // Occupancy (chrono-warp occupancy reconcile, Task 6 review fix)
#include "ore.hpp"
#include "world.hpp"

namespace ra2engine {

// [RE-GATE APPLIED: economy.md §1-§3] pacing constants. The hunt CONFIRMED
// the structures (8-ring dig, per-type store, chunked unload, warp-home)
// but their timer constants are [RE-FALLBACK adjudicated] to these values:
constexpr double kGulpSecs = 1.0;        // one bale per second while parked
constexpr double kUnloadSecsPerBale = 2.0 / 15.0;  // 40 bales ~ 5.3 s
// kOreScanRadius / HarvestScan (W2-SK-02) live in ore.hpp (World needs them).
constexpr double kRescanSecs = 3.0;      // idle rescan cadence
constexpr double kChronoWarpSecs = 1.0;  // CMIN in-transit time per warp
// Tiberium bale values, resolved by the caller from Rules (Riparius 25,
// Cruentus 50) [rulesmd]. PurifierBonus does NOT apply (no purifier built).
struct OreValues { int ore = 25; int gems = 50; };

struct HarvestMove { int unit = -1; int gx = 0, gy = 0; };     // path request (W9-05: initialised)
struct HarvestEvent {                              // audio/render hooks
    enum class Kind { ChronoOut, ChronoIn, UnloadStart, UnloadEnd };
    Kind kind = Kind::ChronoOut; int unit = -1;
};

// The refinery dock cell [RE-GATE APPLIED: economy.md §2 — the binary
// docks at the building's DockingOffsets coord (BuildingType+0x1788), but
// GAREFN/NAREFN define NO DockingOffset in artmd (verified: only
// airfields/shipyards/depots do), so the refinery uses a binary default
// the hunt did not isolate. [RE-FALLBACK adjudicated]: one row south of
// the foundation's center column; visually calibrated against the
// refinery pad in Task 7's screenshots.]
inline int dock_cell_x(const Building& b) { return b.cx + b.fw / 2; }
inline int dock_cell_y(const Building& b) { return b.cy + b.fh; }

// Index of the nearest alive Idle refinery of `house` (types[t].id found in
// `refinery_types`), or -1. Distance = Chebyshev from (x, y) to the dock cell.
int nearest_refinery(const std::vector<Building>& buildings,
                     const std::vector<BuildingTypeDef>& types,
                     const std::vector<int>& refinery_types, int house, int x, int y);

// Advance every harvester by dt [RE: economy.md §1-§3]. Deterministic:
// ascending unit index. Appends path requests to `moves` and events to
// `events` (either may be nullptr-free — pass by reference, always).
// `refinery_types` = roster indices whose id is a refinery (GAREFN/NAREFN).
// `house_credits` accumulates unload income per house (index 0/1).
// `occ` is the World cell-ownership grid (Task 6 review fix): a CMIN
// chrono-warp releases EVERY claim the warping unit holds and claims exactly
// its landing cell, and a warp never lands on a cell owned by another unit
// (it deflects deterministically -- see teleport_unit in harvest.cpp). Only
// the teleport path touches `occ`; driving/parked harvesters' occupancy is
// managed by step_units exactly as before.
void step_harvest(std::vector<Unit>& units, std::vector<Building>& buildings,
                  const std::vector<BuildingTypeDef>& types,
                  const std::vector<int>& refinery_types, OreField& ore,
                  const std::vector<std::uint8_t>& passable, int gw, int gh, Occupancy& occ,
                  double dt, const OreValues& values, double house_credits[2],
                  std::vector<HarvestMove>& moves, std::vector<HarvestEvent>& events,
                  int n_houses = 2, const int* purifiers = nullptr,
                  double purifier_bonus = 0.0, const HarvestScan& scan = HarvestScan{});

// W2-SK-04 [RE-FALLBACK: RA1 FootClass::Goto_Tiberium walks the scan rings and
// takes only cells in the miner's movement ZONE with no other techno on them;
// the RA2 reader of Rules+0x177C was not isolated]. Nearest (Chebyshev, ring
// scan order y asc, x asc) ore cell within `radius` that is 4-connected-
// reachable from (cx, cy) on `passable`, not owned in `occ` by another unit,
// and not (skip_x, skip_y). Exposed for tests.
bool find_reachable_ore(const OreField& ore, const std::vector<std::uint8_t>& passable, int gw,
                        int gh, const Occupancy& occ, int self, int cx, int cy, int radius,
                        int skip_x, int skip_y, int& out_x, int& out_y);
// Gap owner A: `house_credits` has `n_houses` entries (every skirmish house
// is paid, not just 0/1 -- house 2+ income used to land in house 0's pool).
// [RE: economy.md §2 CONFIRMED] each unloaded bale also pays
// purifiers[house] x PurifierBonus x Value as a separate add (nullptr = no
// purifiers anywhere).

// FreeUnit grand-opening edge detector [RE: economy.md §6, main.cpp's
// spawn_free_units_on_open] -- Review Important-2: the Buildup->Idle edge
// detection that drives a refinery's grand-opening miner spawn had zero
// automated coverage (its only caller is the interactive loop). Extracted
// here as a pure function -- no World/Rules/asset dependency -- so it's
// directly unit-testable.
//
// Returns the indices (ascending, each at most once) of every building that
// flipped Buildup->Idle since the LAST call AND whose roster type ships a
// FreeUnit. `prev_states` is grown/tracked exactly like main.cpp's own
// prev_states: index i holds buildings[i]'s last-observed Building::State
// (as an int), -1 meaning "never seen" (so a building already Idle the
// first time it's observed does not fire -- matches the original semantics).
// `free_unit_of_type[t]` is true when roster type t ships a FreeUnit (the
// caller resolves this from Rules::free_unit(id).empty()); a building whose
// type is out of range is treated as "no FreeUnit" (skipped), matching
// spawn_free_units_on_open's own bounds check.
std::vector<std::size_t> free_unit_grand_openings(
    const std::vector<Building>& buildings, const std::vector<bool>& free_unit_of_type,
    std::vector<int>& prev_states);

}  // namespace ra2engine
