#pragma once
#include <cstdint>
#include <vector>

#include "occupancy.hpp"
#include "pathfind.hpp"  // Cellxy

namespace ra2engine {

// Up to `count` distinct cells nearest-first (by expanding square rings) around
// `center`, each walkable (passable[y*w+x] != 0) and free (occ.is_free). The
// center itself is the first candidate. Fewer than `count` are returned if the
// nearby area is too crowded/blocked (bounded ring search). Pure.
std::vector<Cellxy> nearest_free_cells(Cellxy center, int count,
                                       const std::vector<uint8_t>& passable, int w, int h,
                                       const Occupancy& occ);

// Assign each of `mover_cells` a distinct destination cell for a group move to
// `goal`. Collects free walkable cells spiralling out from `goal` -- treating
// each mover's OWN current cell as free (they are about to vacate) -- then
// greedily gives each collected cell (nearest-to-goal first) to the nearest
// not-yet-assigned mover. Any mover left unassigned (crowded) keeps its own
// current cell. Returns exactly mover_cells.size() targets. Pure.
// Precondition: mover_cells are pairwise distinct (callers derive them from an Occupancy, which holds at most one unit per cell); duplicate mover cells can produce duplicate targets.
std::vector<Cellxy> assign_formation(Cellxy goal, const std::vector<Cellxy>& mover_cells,
                                     const std::vector<uint8_t>& passable, int w, int h,
                                     const Occupancy& occ);

// W5-B R-061: the infantry form of assign_formation under sub-cells -- a
// destination cell takes as many movers as it has free infantry spots (three
// in an empty cell), so a squad packs three to a cell around the goal the way
// the original's FindInfantrySubposition fills a cell before NearByLocation
// spills to the next. `mover_ids` (same order as mover_cells) are released
// from a scratch copy of `occ` first; `blocked_cells` (e.g. the vehicles' own
// formation targets) are treated as taken. Returns mover_cells.size() targets.
std::vector<Cellxy> assign_infantry_formation(Cellxy goal, const std::vector<Cellxy>& mover_cells,
                                              const std::vector<int>& mover_ids,
                                              const std::vector<uint8_t>& passable, int w, int h,
                                              const Occupancy& occ,
                                              const std::vector<Cellxy>& blocked_cells);

}  // namespace ra2engine
