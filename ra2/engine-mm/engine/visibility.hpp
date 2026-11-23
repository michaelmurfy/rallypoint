#pragma once
// Shroud/Fog phase [RE: shroud.md §3]: pure near-circular reveal math, shared by
// World::reveal_source and host-doctestable without a World. No SDL/World/RNG.
#include <cstdint>
#include <vector>

namespace ra2engine {

// Near-circular membership [RE-GATE APPLIED: shroud.md §3 -- dx2+dy2<=sight2,
// integer cell centers, inclusive boundary]. sight is in whole cells (clamped
// to 10 at data-load). sight <= 0 -> only the source cell (dx=dy=0) qualifies.
inline bool cell_in_sight(int dx, int dy, int sight) {
    return dx * dx + dy * dy <= sight * sight;
}

// Stamp a near-circular disc of radius `sight` cells around (cx,cy) into two
// grids sized gw*gh (index y*gw+x): `visible` is set for every in-range cell,
// and `explored` is ALSO set (sticky -- fog never regrows, so callers never
// clear explored). Out-of-bounds cells are skipped. Both grids must already be
// sized gw*gh (the caller allocates). Returns the count of in-bounds cells set.
int reveal_disc(std::vector<std::uint8_t>& explored,
                std::vector<std::uint8_t>& visible,
                int gw, int gh, int cx, int cy, int sight);

}  // namespace ra2engine
