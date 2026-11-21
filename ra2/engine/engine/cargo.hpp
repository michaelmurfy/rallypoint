#pragma once
// Pure CB-1 transport/cargo logic [RE: docs/re/transport-cargo.md]: the
// COUNT capacity gate + per-passenger size cap (can_load), the pip-only
// total-size metric (cargo_total_size -- the original GetTotalSize), the
// LIFO eject rule (next_eject), and the facing+/-90-degree scatter
// eject-cell picker (eject_cell) recovered from the original unload
// (FUN_00415C60) [RE-GATE APPLIED: transport-cargo.md §4 -- the KickOutUnit
// DAT_0089E5F8 8-ring is NOT reused by unload; factory.cpp stays untouched].
// Layered on Unit exactly like harvest.hpp (world.hpp defines Unit; this
// module includes world.hpp; world.hpp does NOT include this back). No
// SDL/OS.
#include <functional>
#include <vector>
#include "pathfind.hpp"  // Cellxy
#include "world.hpp"     // Unit, facing_u16_from_delta

namespace ra2engine {

// DISPLAY-ONLY sum of units[i].size over the transport's cargo list -- the
// original's GetTotalSize (FUN_00473390), whose only callers are pip draws
// [RE-GATE APPLIED: transport-cargo.md §1 -- pip-only; NEVER consulted by
// the capacity gate]. DERIVED every call -- no stored counter to drift out
// of sync [spec §2]. Out-of-range entries are ignored defensively (cargo
// holds unit INDICES, stable for a World's lifetime because units are
// tombstoned, never erased -- the same invariant `target` relies on).
int cargo_total_size(const std::vector<Unit>& units, const Unit& transport);

// The "can X enter Y" predicate over plain Unit fields [RE-GATE APPLIED:
// transport-cargo.md §2 -- recovered chain (What_Action -> ReceiveMessage
// 0xF): transport flag, ownership, capacity COUNT, per-passenger size]:
//   1. both indices in range and distinct (not self),
//   2. transport is a transport (passengers_max > 0) [§2 CONFIRMED],
//   3. transport alive and non-dying, passenger alive and non-dying,
//   4. same house [RE-GATE APPLIED: transport-cargo.md §2 -- documented
//      deviation: the original gate is IsAlliedWith (FUN_004F98B0: same
//      house OR same side OR ally bitmask); allies are unmodeled in CB-1
//      (one house per side) -- this clause is where allied loading would
//      attach],
//   5. passenger not already carried (cargo_of < 0),
//   6. passenger's own cargo empty (no nested loaded transports -- spec
//      Out-of-scope; data-unreachable with HTK SizeLimit=2 < vehicle Size=3),
//   7. transport not itself carried (cargo_of < 0),
//   8. passenger.size <= transport.size_limit [RE-GATE APPLIED:
//      transport-cargo.md §1 -- a PER-PASSENGER cap (Size <= SizeLimit),
//      not a pool],
//   9. COUNT capacity: (int)transport.cargo.size() < transport.passengers_max
//      [RE-GATE APPLIED: transport-cargo.md §1 -- NumPassengers < Passengers
//      (FUN_004190B0 case 0xF/0xE); each body is ONE slot regardless of
//      Size -- the pre-hunt size-sum is DELETED].
// Wave-5 (R-052): `allied` (optional) widens clause 4 to the original
// IsAlliedWith (FUN_004F98B0) -- an ally's infantry may board the transport.
bool can_load(const std::vector<Unit>& units, int transport_idx, int passenger_idx,
              const std::function<bool(int, int)>& allied = {});

// The next passenger to eject: the LAST cargo entry (LIFO -- the original
// container is a head-pushed singly-linked stack and unload pops the head,
// i.e. the NEWEST passenger) [RE-GATE APPLIED: transport-cargo.md §6 --
// CONFIRMED]. Our cargo vector push_backs on board, so the stack head is
// the BACK. -1 when the cargo list is empty.
int next_eject(const Unit& transport);

// The eject-cell picker for one unload step [RE-GATE APPLIED:
// transport-cargo.md §4 -- CONFIRMED shape: the original eject
// (FUN_00415C60) does NOT walk the KickOutUnit ring; it rotates the
// transport's facing +/- a quarter turn (0x3FFF BAM ~ 90 deg;
// cell-equivalent 0x4000), ALTERNATING the side each eject via a counter's
// low bit, and places the passenger one step out]. transport_facing_u16 is
// the vehicle body_facing in the engine's ONE 16-bit facing space
// (voxel-render.md §2); the 8-way step is chosen by REUSING
// facing_u16_from_delta -- no second convention. If the scatter cell fails
// `free`, falls back to the first free cell of the immediate neighbourhood
// (Chebyshev r=1 then r=2, row-major within each ring -- documented
// deviation: FUN_00481090's exact fine placement is unpinned; behaviourally
// equivalent = first free cell near the transport). Returns {-1,-1} when
// fully boxed in (caller HOLDs: put-back/retry [§4 CONFIRMED]).
Cellxy eject_cell(int tx, int ty, double transport_facing_u16, int eject_counter,
                  const std::function<bool(int, int)>& free);

}  // namespace ra2engine
