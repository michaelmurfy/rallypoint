#pragma once

#include <cstdint>
#include <vector>

namespace ra2engine {

// Grid cell coordinate (x, y) in passability-grid space.
struct Cellxy { int x, y; };

// A* pathfinding over an 8-connected width*height passability grid.
//
// PURE math/algorithm -- no SDL/OS deps, so it is unit-testable without a
// window (see tests/test_pathfind.cpp) and is compiled into both
// `ra2assets_tests` and `ra2engine` from a single source of truth, same
// pattern as engine/iso_project.cpp (see CMakeLists.txt).
//
// `passable[y*width+x] != 0` means the cell is walkable. Movement is
// 8-directional (4 cardinal + 4 diagonal); diagonal moves are blocked if
// either of the two orthogonal "corner" cells they would cut across is
// impassable (prevents cutting through a wall's corner) -- unless that corner
// cell is flagged in the optional `leak` mask (same size; W5-A: wall overlays,
// whose corner-only contacts leave a gap). W7-11-08 [RE: FUN_00429A10 /
// FUN_004297B0]: every step costs the same (diagonals included) plus a small
// per-direction tie-break (N .001 .. NW .008, orthogonals cheapest), and the
// heuristic is the Euclidean distance -- scaled x1000 to integers so the
// search stays deterministic.
//
// Returns the sequence of cells from `start` to `goal` inclusive. Returns an
// empty vector if: start or goal is out of [0,width)x[0,height) range,
// either start or goal cell is impassable, or no path exists. If
// `start == goal` (and it is in range and passable), returns a single-cell
// path containing just that cell.
std::vector<Cellxy> find_path(const std::vector<uint8_t>& passable, int width, int height,
                              Cellxy start, Cellxy goal,
                              const std::vector<uint8_t>* leak = nullptr);

// R-210 (bridges and cliffs) [RE: FootClass height gate FUN_004D9B70, reached
// from InfantryClass / UnitClass IsCellOccupied (vtable +0x1AC -> +0x1B0) for
// every A* neighbour in FUN_00429A10 and for every step a locomotor takes;
// CellClass::IsClearToMove FUN_004833B0 (Level +0x11B, bridge flag 0x100,
// deck at Level+4)]. The optional terrain makes the search height-aware:
//   * a cell is entered at its ground (layer 0, height Level) or, where
//     `deck` says so, on an intact high-bridge deck (layer 1, Level + 4);
//   * a step keeps its height, or climbs / descends ONE level when the LOWER
//     of the two cells is a slope (SlopeIndex != 0, the TMP ramp type);
//     every other height change (a cliff: 2, 3, 4+ levels) is refused. The
//     original's +-4 bridge-end cases are the "same height" steps between a
//     deck and the high ground at its ends.
// `deck` per cell: 0 none, 1 deck AND ground both enterable, 2 deck only (the
// ground under it -- water -- is not this mover's). `version` changes with
// the deck state (World::bridge_version) and keys the result memo: the
// level / slope grids are taken as fixed for a given pointer, so a caller that
// edits them (or the deck grid) must move `version`.
// With a null `terrain` (or null `level`) every function below behaves
// exactly as before (byte-identical paths).
struct PathTerrain {
    const std::vector<std::int8_t>* level = nullptr;    // ground Level per cell
    const std::vector<std::uint8_t>* slope = nullptr;   // TMP ramp type (0 = flat)
    const std::vector<std::uint8_t>* deck = nullptr;    // 0 / 1 / 2 (above); null = no decks
    std::uint32_t version = 0;
    int start_layer = 0;                                // the mover's layer at `start`
    // Gate=yes buildings that open for this mover (sorted cell indices): passable
    // although the grid holds 0 there [RE: InfantryClass/UnitClass IsCellOccupied
    // -> Move::ClosedGate (3) when the gate's owner IsAlliedWith the mover].
    const std::vector<int>* open_cells = nullptr;
};

// The layer a mover at (sx, sy, layer `sl`) stands on after stepping to the
// neighbour (dx, dy): 0 ground, 1 deck, -1 when the height gate refuses the
// step (a cliff). Shared by the A* and the step-by-step movement. Needs
// `t.level`; without it every step keeps layer 0.
int path_step_layer(const PathTerrain& t, int width, int sx, int sy, int sl, int dx, int dy);

// The layer-aware searches (same contracts as the functions below; a null
// terrain reproduces them exactly).
std::vector<Cellxy> find_path_t(const std::vector<uint8_t>& passable, int width, int height,
                                Cellxy start, Cellxy goal, const std::vector<uint8_t>* leak,
                                const PathTerrain* terrain);
// W7-11-08: `cost` (optional, one entry per cell) is the original's per-cell
// Can_Enter cost the step into that cell pays -- 1 clear, 8 a parked friendly
// (Temp), 1 / 4 / 1000 a moving block, 20 / 60 a destroyable blocker, 1000 a
// cloaked cell [RE: DAT_0081770C] -- null = every cell costs 1.
std::vector<Cellxy> find_path_bounded_t(const std::vector<uint8_t>& passable, int width, int height,
                                        Cellxy start, Cellxy goal, int max_expand,
                                        const std::vector<uint8_t>* leak, const PathTerrain* terrain,
                                        const std::vector<std::uint16_t>* cost = nullptr);
std::vector<Cellxy> find_path_nearest_t(const std::vector<uint8_t>& passable, int width, int height,
                                        Cellxy start, Cellxy goal, int max_expand,
                                        const std::vector<uint8_t>* leak, const PathTerrain* terrain,
                                        const std::vector<std::uint16_t>* cost = nullptr);

// W2-SK-16: find_path with an expansion budget (> 0): empty when the goal is
// not reached within `max_expand` closed nodes (same path otherwise).
std::vector<Cellxy> find_path_bounded(const std::vector<uint8_t>& passable, int width, int height,
                                      Cellxy start, Cellxy goal, int max_expand,
                                      const std::vector<uint8_t>* leak = nullptr);

// W2-SK-05/16 [RE-FALLBACK: the original walks a unit ordered onto an
// impassable or unreachable cell to the nearest place it can reach]: an
// impassable goal is first relaxed to the nearest passable cell (<= 16 rings);
// if that is still not reached (unreachable, or `max_expand` > 0 runs out) the
// path ends at the expanded cell nearest the goal. Empty only for bad input or
// an impassable start; a single-cell path means "already as close as it gets".
std::vector<Cellxy> find_path_nearest(const std::vector<uint8_t>& passable, int width, int height,
                                      Cellxy start, Cellxy goal, int max_expand,
                                      const std::vector<uint8_t>* leak = nullptr);

}  // namespace ra2engine
