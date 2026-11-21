#pragma once
// Pure factory-exit / primary-factory / rally logic [RE: docs/re/production.md
// §1.1–§1.4], layered on Building like harvest.cpp is on Unit. No SDL/OS.
// `factory_types` is a category resolved to roster indices by the caller (the
// set of BuildingType indices that are factories for ONE queue category, e.g.
// the Vehicle queue's {GAWEAP}) — the same "resolve the category to a roster-
// index list" pattern harvest.cpp's refinery_types uses.
#include <functional>
#include <vector>
#include "building.hpp"
#include "pathfind.hpp"  // Cellxy

namespace ra2engine {

// The cell a finished unit first appears in [RE-GATE APPLIED: production.md
// §1.3 — BuildingClass::KickOutUnit FUN_00443BC0]: from the building's anchor
// cell (cx + fw/2, cy + fh/2), walk the 8-neighbour (dx,dy) ring — the SAME
// DAT_0089E5F8 table as the harvester dig ring (economy.md §1) — expanding
// the radius outward, to the FIRST cell `free(x,y)` accepts (the building's
// own foundation cells are never free, so the walk naturally lands just
// outside the door). It is NOT a fixed constant offset and NOT a per-unit
// exit list (a unit's Factory= key is the producer's AbstractType, not a
// coordinate). The BuildingType ExitCoord (+0xEC8) sub-cell lean rides on top
// of the chosen door cell; our cell-resolution grid drops the sub-cell
// fraction (divergence, noted in Task 9). Returns {-1, -1} when no ring cell
// is free (caller HOLDs the ready slot).
// [RE-FALLBACK adjudicated: the game seeds the walk from a lockstep-RNG exit
// facing (production.md §1.3, PROBABLE); our single-machine engine uses a
// deterministic door facing — SOUTH first, then the fixed ring order — same
// reachable set, deterministic.] With the south door cell free this returns
// (cx + fw/2, cy + fh) for even-height foundations — the cell one row south
// of the foundation's center column.
Cellxy exit_cell(const Building& factory, const BuildingTypeDef& def,
                 const std::function<bool(int, int)>& free);

// Index of the factory a completed unit exits from: the alive, Idle, owned
// building whose type is in `factory_types` that is flagged `primary`; if none
// is flagged, the recovered default [RE-GATE APPLIED: production.md §1.2 —
// default primary = the FIRST-built (lowest index) same-category same-house
// factory; a newly placed factory becomes primary only if the house has none
// yet, and building a second never steals primary until right-clicked].
// Returns -1 if the house owns no alive Idle factory of this category.
int primary_factory(const std::vector<Building>& buildings,
                    const std::vector<int>& factory_types, int house);

// Set building `b_idx` as its house's primary factory of its category: sets
// primary=true on b_idx and clears primary on EVERY OTHER building of the same
// house whose type is in `factory_types` (exactly one primary per category per
// house). No-op if b_idx is out of range or its type is not in factory_types.
void set_primary(std::vector<Building>& buildings, const std::vector<int>& factory_types,
                 int b_idx);

}  // namespace ra2engine
