#pragma once
// P6 "Roster breadth + remaining producers" -- the PURE mechanics recovered in
// docs/re/roster2.md: walls (connection mask/frame, wall-line fill, 3x build
// time), the water-bound Naval Yard (placement + water exit), and the
// Airforce Command dock pads + fixed-wing sortie/rearm state machine.
// No SDL, no World: callers (main.cpp's --demo-roster2, tests) supply grids
// and predicates. Nothing here is reachable from an existing gate.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "building.hpp"
#include "pathfind.hpp"   // Cellxy

namespace ra2engine {

// ---------------------------------------------------------------- walls ---

// [RE: roster2.md TL;DR 2/3] CellClass::Wall_Update (FUN_00480540) walks the
// facings {0,2,4,6} = N,E,S,W through AdjacentCell[] @0x89E5F8 (N=(0,-1),
// E=(+1,0), S=(0,+1), W=(-1,0)) and sets bit i when that neighbour carries the
// SAME wall overlay.
constexpr int kWallN = 1;
constexpr int kWallE = 2;
constexpr int kWallS = 4;
constexpr int kWallW = 8;
// rulesmd [General] WallBuildSpeedCoefficient=3.0 ("multiplier to time").
constexpr double kWallBuildSpeedCoefficient = 3.0;

// The 4-bit connection mask of the wall at (x,y). `same_wall(nx,ny)` answers
// "does that cell hold the SAME wall type" (the caller owns bounds checks).
int wall_mask(int x, int y, const std::function<bool(int, int)>& same_wall);

// OverlayData = stage<<4 | mask -> the wall SHP frame (gtwall/ntwall: 96 frames
// = 3 stages x 16 masks, image half). mask is clamped to 4 bits, stage >= 0.
int wall_frame(int mask, int stage);

// [RE-FALLBACK roster2.md §D] the engine keeps walls as 1x1 Buildings, so the
// damage stage is derived from HP: 0 while hp/max_hp > condition_yellow, else 1.
int wall_stage(int hp, int max_hp, double condition_yellow);

// GuardRange (rules cells) -> the line-fill reach: (int)(GuardRange*256) >> 8,
// exactly FUN_00588180's `TechnoType+0x5B8 >> 8` on the lepton value.
int wall_fill_range(double guard_range_cells);

// [RE: roster2.md TL;DR 4] FUN_00588180, run when a wall is placed at (x,y):
// for each facing N,E,S,W walk k = 1..range. If cell k is `own_wall` (same
// wall type, same house) -> the cells 1..k-1 are filled (returned, in facing
// then distance order) and that direction stops. Else if `can_place(cell k)`
// the walk continues, else that direction stops. A direction that hits `range`
// without meeting an own wall fills nothing. Filled segments are FREE.
std::vector<Cellxy> wall_line_fill(int x, int y, int range,
                                   const std::function<bool(int, int)>& own_wall,
                                   const std::function<bool(int, int)>& can_place);

// [RE: roster2.md TL;DR 5] Time_To_Build for a wall: ftol((double)frames *
// coefficient), frames = build_frames(cost, build_speed) (integer first).
int wall_build_frames(int cost, double build_speed,
                      double coefficient = kWallBuildSpeedCoefficient);

// ------------------------------------------------------------ naval yard ---

// [RE: roster2.md TL;DR 6] WaterBound -> SpeedType Float: EVERY foundation cell
// must be in-grid and water-passable (water grid value 1).
bool water_bound_ok(int cx, int cy, int fw, int fh, const std::vector<std::uint8_t>& water,
                    int grid_w, int grid_h);

// [RE: roster2.md TL;DR 8] The naval exit: the existing exit_cell ring walk
// from the yard anchor, with "free" = water-passable AND `free_extra` (caller:
// unoccupied and not under a building). {-1,-1} when boxed in.
Cellxy naval_exit_cell(const Building& yard, const BuildingTypeDef& def,
                       const std::vector<std::uint8_t>& water, int grid_w, int grid_h,
                       const std::function<bool(int, int)>& free_extra);

// [RE: roster2.md TL;DR 7] the factory category split: of the roster entries
// whose `Factory=UnitType`, keep those whose Naval flag equals `naval`.
// `unit_factory[i]` / `naval[i]` are per-roster-index flags.
std::vector<int> unit_factory_types(const std::vector<bool>& unit_factory,
                                    const std::vector<bool>& naval, bool want_naval);

// -------------------------------------------------------------- airfield ---

struct LeptonXY { int x = 0, y = 0; };

// [ANCHORED roster2.md §C] a building's GetCoords = its foundation centre.
LeptonXY building_center_leptons(const Building& b);
// [RE: roster2.md TL;DR 9] dock pad k = GetCoords + DockingOffset[k].
LeptonXY dock_pad_leptons(const Building& b, int pad, const std::vector<LeptonXY>& offsets);
Cellxy lepton_cell(LeptonXY p);   // floor(lepton / 256) per axis

// [RE: roster2.md TL;DR 10] ftol(ReloadRate * 900): .3 -> 270. Never < 1.
int reload_cycle_frames(double reload_rate_minutes);

// First free pad (owner -1) of `pad_owner`, or -1 when all are taken.
int first_free_pad(const std::vector<int>& pad_owner);

// [RE: roster2.md §C] one fixed-wing aircraft's dock -> strike -> RTB -> rearm
// cycle, stepped one 15 Hz frame at a time (all integers/doubles, no RNG).
struct Sortie {
    enum class State { Docked, TakeOff, Cruise, Return, Landing };
    State state = State::Docked;
    double x = 0.0, y = 0.0;   // leptons
    int z = 0;                 // altitude, leptons
    int ammo = 0, max_ammo = 0;
    int pad = -1;
    LeptonXY home;             // dock pad coordinate
    bool has_target = false;
    LeptonXY target;
    double speed_lpf = 0.0;    // cruise leptons per frame
    int flight_level = 1500;   // rulesmd [General] FlightLevel
    int climb_lpf = 100;       // [RE-FALLBACK] Fly loco climb/descent rate
    int range_leptons = 0;     // weapon Range * 256
    int reload_timer = 0;      // frames accumulated toward the next reload step
    int shots = 0;             // passes fired (ammo spent)
    bool rtb_started = false;  // the RTB leg has been announced
};

enum class SortieEvent { None, TookOff, ReachedCruise, Fired, StartedReturn, Landed, Reloaded };

// Advance one frame. Docked: refill +1 ammo every `reload_cycle` frames while
// below max; launch (TakeOff) when a target is set and ammo > 0. TakeOff/
// Landing: climb/descend climb_lpf. Cruise: straight line toward the target;
// within range -> fire (ammo--), and at ammo 0 -> Return. Return: straight
// line home; on arrival -> Landing. Landing reaches z=0 -> Docked (target
// cleared, reload timer restarted).
SortieEvent step_sortie(Sortie& s, int reload_cycle);

const char* sortie_state_name(Sortie::State st);

}  // namespace ra2engine
