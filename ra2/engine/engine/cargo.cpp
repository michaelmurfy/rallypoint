#include "cargo.hpp"
#include "orig_math.hpp"   // Net L0: om::ftol (_ftol 0x7C4D00) for double->int

#include <algorithm>
#include <cmath>

namespace ra2engine {

int cargo_total_size(const std::vector<Unit>& units, const Unit& transport) {
    int total = 0;
    for (int p : transport.cargo)
        if (p >= 0 && p < static_cast<int>(units.size())) total += units[p].size;
    return total;
}

bool can_load(const std::vector<Unit>& units, int transport_idx, int passenger_idx,
              const std::function<bool(int, int)>& allied) {
    // [RE-GATE APPLIED: transport-cargo.md §2 -- recovered boolean chain;
    // see the header comment for the numbered members.]
    const int n = static_cast<int>(units.size());
    if (transport_idx < 0 || transport_idx >= n) return false;
    if (passenger_idx < 0 || passenger_idx >= n) return false;
    if (transport_idx == passenger_idx) return false;              // not self
    const Unit& t = units[transport_idx];
    const Unit& p = units[passenger_idx];
    if (t.passengers_max <= 0) return false;                       // not a transport
    if (!t.alive || t.die_time >= 0.0) return false;               // transport live
    if (!p.alive || p.die_time >= 0.0) return false;               // passenger live
    // Clause 4 [RE-GATE APPLIED: transport-cargo.md §2 -- the original gate is
    // IsAlliedWith (FUN_004F98B0)]. Wave-5 (R-052): with the caller's alliance
    // predicate an allied house's passenger is admitted; without it (pure
    // tests) the gate stays same-house.
    if (t.house != p.house && !(allied && allied(t.house, p.house))) return false;
    if (p.cargo_of >= 0) return false;                             // already carried
    if (!p.cargo.empty()) return false;        // no nested loaded transports
                                               // [spec Out-of-scope]
    if (t.cargo_of >= 0) return false;                             // carried transports
                                               // admit nobody
    // W7-01-28 [RE: UnitClass::ReceiveMessage FUN_00736500 case 0xF]: a
    // passenger that is mind-controlling something may not enter, and a
    // transport being erased (warped out) admits nobody.
    if (!p.w7.mc_victims.empty()) return false;
    if (t.warp_remaining > 0) return false;
    if (p.size > t.size_limit) return false;   // PER-PASSENGER cap: Size <=
                                               // SizeLimit [RE-GATE APPLIED:
                                               // transport-cargo.md §1]
    // W6-04-08 [RE: transport-cargo.md §1 -- UnitClass::ReceiveMessage
    // FUN_00736500 case 0xE/0xF refuses when Passengers (+0x5E0) <
    // GetTotalSize() (FUN_00473390, the sum of the passengers' Size) + the
    // newcomer's Size (+0x380)]: a VEHICLE / ship transport is a Size pool
    // (the Amphibious Transport, Passengers=12, takes 4 Grizzlies of Size 3).
    // AircraftClass::ReceiveMessage FUN_004190B0 (the Night Hawk) keeps the
    // body count NumPassengers < Passengers; garrisons (FUN_0043C250) are
    // counted in world_depth, not here.
    if (t.is_aircraft) return static_cast<int>(t.cargo.size()) < t.passengers_max;
    return cargo_total_size(units, t) + p.size <= t.passengers_max;
}

int next_eject(const Unit& transport) {
    // LIFO: the LAST-boarded passenger leaves first (the original is a
    // head-pushed stack whose unload pops the head, i.e. the newest)
    // [RE-GATE APPLIED: transport-cargo.md §6 -- CONFIRMED]. Our boarding
    // push_backs, so the stack head is the BACK.
    return transport.cargo.empty() ? -1 : transport.cargo.back();
}

Cellxy eject_cell(int tx, int ty, double transport_facing_u16, int eject_counter,
                  const std::function<bool(int, int)>& free) {
    // Scatter facing = the transport's facing rotated a quarter turn, the
    // side alternating with the eject counter's low bit [RE-GATE APPLIED:
    // transport-cargo.md §4 -- facing +/- 0x3FFF (~90 deg BAM), cell-
    // equivalent 0x4000; alternation via (counter & 1). Which side is
    // "even" is an arbitrary-but-fixed choice inside the recovered
    // alternation -- deterministic].
    const double side = (eject_counter & 1) ? -16384.0 : 16384.0;
    const double scatter =
        std::fmod(transport_facing_u16 + side + 65536.0, 65536.0);
    // 8-way quantization REUSES facing_u16_from_delta (the engine's one
    // facing convention -- voxel-render.md §2) instead of a second
    // hand-built mapping: pick the neighbour step whose facing is angularly
    // closest to the scatter facing (ties -> lowest index; deterministic).
    static const int kDx[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    static const int kDy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
    int best = 0;
    double best_d = 65536.0;
    for (int k = 0; k < 8; ++k) {
        double d = std::fabs(facing_u16_from_delta(kDx[k], kDy[k]) - scatter);
        if (d > 32768.0) d = 65536.0 - d;
        if (d < best_d) {
            best_d = d;
            best = k;
        }
    }
    const int px = tx + kDx[best], py = ty + kDy[best];
    if (free(px, py)) return Cellxy{px, py};
    // Primary blocked: nearest-free-cell fallback over the immediate
    // neighbourhood, Chebyshev r = 1 then r = 2, row-major within each ring
    // [RE-GATE APPLIED: transport-cargo.md §4 -- documented deviation:
    // FUN_00481090's exact fine placement is unpinned; behaviourally
    // equivalent = first free cell near the transport].
    for (int r = 1; r <= 2; ++r)
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(dx < 0 ? -dx : dx, dy < 0 ? -dy : dy) != r) continue;
                if (free(tx + dx, ty + dy)) return Cellxy{tx + dx, ty + dy};
            }
    return Cellxy{-1, -1};   // fully boxed in -> caller HOLDs (put-back/retry)
}

}  // namespace ra2engine
