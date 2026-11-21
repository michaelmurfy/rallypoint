#include "factory.hpp"
#include "orig_math.hpp"   // Net L0: om::ftol (_ftol 0x7C4D00) for double->int
#include <algorithm>

namespace ra2engine {

Cellxy exit_cell(const Building& factory, const BuildingTypeDef& def,
                 const std::function<bool(int, int)>& free) {
    // [RE-GATE APPLIED: production.md §1.3 — KickOutUnit FUN_00443BC0: a
    // nearest-free-cell walk outward from the building's anchor cell to the
    // first free cell; foundation cells are skipped because they are never
    // free. For each Chebyshev ring radius r we probe EVERY cell of the full
    // ring perimeter (all (dx,dy) with max(|dx|,|dy|)==r), not just the 8
    // axis/diagonal rays — a factory free only on a non-ray cell (e.g.
    // (cx+fw, cy+fh) on a wide foundation) must still find its door, matching
    // the original occupant-walk. The radius expands so wide foundations still
    // find a door. ExitCoord's sub-cell lean is dropped at cell resolution
    // (Task 9 divergence).]
    // [RE-FALLBACK adjudicated: the original seeds the walk facing from the
    // lockstep RNG; we use a deterministic order — within each ring, cells are
    // ordered SOUTH-first (dy descending), then column-centered (|dx|
    // ascending), then west-before-east (dx ascending). At r=1 this makes the
    // SOUTH door cell (cx+fw/2, cy+fh) the very first probe, preserving the
    // door-free result; the same reachable set as the original, deterministic.]
    const int ax = factory.cx + def.fw / 2;   // anchor: foundation center cell
    const int ay = factory.cy + def.fh / 2;
    const int max_r = std::max(def.fw, def.fh) + 1;
    auto iabs = [](int v) { return v < 0 ? -v : v; };
    for (int r = 1; r <= max_r; ++r) {
        // Full ring perimeter at Chebyshev distance r, ordered deterministically
        // (south-first, then column-centered, then west-before-east).
        std::vector<Cellxy> ring;
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx)
                if (std::max(iabs(dx), iabs(dy)) == r)
                    ring.push_back(Cellxy{dx, dy});
        std::sort(ring.begin(), ring.end(), [&](const Cellxy& a, const Cellxy& b) {
            if (a.y != b.y) return a.y > b.y;            // south (+y) first
            if (iabs(a.x) != iabs(b.x)) return iabs(a.x) < iabs(b.x);  // center column first
            return a.x < b.x;                            // west before east
        });
        for (const Cellxy& d : ring) {
            const int x = ax + d.x, y = ay + d.y;
            if (free(x, y)) return Cellxy{x, y};
        }
    }
    return Cellxy{-1, -1};                     // fully boxed in -> caller HOLDs
}

namespace {
bool is_cat(const std::vector<int>& factory_types, int type) {
    return std::find(factory_types.begin(), factory_types.end(), type) != factory_types.end();
}
}  // namespace

int primary_factory(const std::vector<Building>& buildings,
                    const std::vector<int>& factory_types, int house) {
    int fallback = -1;
    for (int i = 0; i < (int)buildings.size(); ++i) {
        const Building& b = buildings[i];
        if (!b.alive || b.state != Building::State::Idle || b.house != house) continue;
        if (!is_cat(factory_types, b.type)) continue;
        if (b.primary) return i;                 // flagged primary wins outright
        if (fallback < 0) fallback = i;          // else the first-built eligible one
    }
    return fallback;
}

void set_primary(std::vector<Building>& buildings, const std::vector<int>& factory_types,
                 int b_idx) {
    if (b_idx < 0 || b_idx >= (int)buildings.size()) return;
    if (!is_cat(factory_types, buildings[b_idx].type)) return;
    const int house = buildings[b_idx].house;
    for (int i = 0; i < (int)buildings.size(); ++i) {
        Building& b = buildings[i];
        if (b.house != house || !is_cat(factory_types, b.type)) continue;
        b.primary = (i == b_idx);
    }
}

}  // namespace ra2engine
