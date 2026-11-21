#pragma once
// W9-01-04 / W9-02-04: the per-frame lockstep sync CRC (the FRAMEINFO CRC).
//
// [RE: ComputeFrameCRC FUN_0064D520, dump D:\ra2-re\net1-out\dec2.c] The
// original folds, EVERY frame, `CRC = rotl1(CRC) + term` (the decompile's
// `CRC*2 - (CRC>>31)` with an arithmetic shift) over
//   - every infantry   (+0x9C X / 10) + (+0xA0 Y / 10) << 16 + facing8,
//   - every unit       the same with BOTH facings (body + turret),
//   - every building   the same,
//   - the five map layers and the Logic list (X/10, Y/10, WhatAmI),
//   - one Scenario->Random draw (FUN_0065C1C0),
// where facing8 = ((DirStruct >> 7) + 1) >> 1 & 0xFF (FUN_004C92F0). It costs
// microseconds and never allocates.
//
// The clone keeps that shape (rotl1 + term, objects in array order, then the
// RNG) and is deliberately STRONGER, because the clone's sim is not the
// original's and real divergences must be caught quickly (brief W9 step 1):
//   * positions are folded as the exact cell AND the bit pattern of the
//     continuous position (fx/fy), so a sub-lepton drift is caught at once;
//   * facing (8-way, body, turret), health, target / mission handles, cargo,
//     harvester load;
//   * buildings: type, owner, origin, health, state;
//   * diplomacy: the one-way alliance matrix, alliance teams, the lobby
//     diplomacy rules, game over / observer state (w9fix-match);
//   * every house's economy: wallet, the legacy credit pool, every
//     production slot (type / stage / progress / spent), defeat / outcome;
//   * projectiles in flight (position, target), crates;
//   * EVERY sim RNG stream's state (crate R250 table position + words,
//     projectile / combat / ore / bridge / air-naval xorshifts, each AI
//     house's base / team / trigger stream) -- PEEKED, never drawn: the
//     original's draw advances the shared Randomizer on every peer alike, the
//     clone's streams are per subsystem and must not move outside the sim;
//   * the sim frame clocks.
// DEVIATION (documented): the original draws the RNG inside the CRC; the clone
// folds the RNG states instead (identical detection power, no sim side effect).
//
// The W9-05 SYNC-HASH POLICY (savegame.hpp, w9fix-robust) governs this CRC
// too; the input set is listed in ONE place (net_frame_crc.cpp):
//   * canonical doubles: add_d folds om::canonical_bits, the exact rule the
//     save / full sync hash io_scalar uses (every NaN = 0x7FF8..., other
//     values bit-exact);
//   * no derived caches: the members save_fields.hpp visits with
//     io_unhashed (amphib_passable_, crush_grid_/crush_leak_, deck_*,
//     w5_hide_/w5_own_) and the occupancy / claim index are NEVER read here
//     (tests/test_net_play.cpp pins it by a source scan);
//   * hash-only state IS folded: the per-house production-rules install
//     (econ_rules_set_ + econ_rules_digest_, the io_hash_only members) --
//     a peer that built different rules desyncs at frame 0;
//   * the campaign / co-op map-trigger runtime (`triggers`, when given): RNG,
//     frame, globals / locals, mission timer, outcome, house flags, every
//     tag / trigger instance (enabled, destroyed, bits, timers) -- not the
//     text log.
//
// The full-World hash stays available for debugging: RA2.INI [Net]
// FullHash=yes (or RA2_NET_FULL_HASH=1) -- GameScene::net_state_hash streams
// save::world_sync_hash (SyncHasher, the same policy, + the trigger runtime)
// -- and the desync dump writes the full save plus per-member hashes.

#include <cstdint>
#include <vector>

#include "orig_math.hpp"   // om::canonical_bits (the sync-hash policy)

namespace ra2engine {

class World;
class MapTriggerSystem;

// The rolling fold. rotl1 + term exactly like FUN_0064D520.
struct FrameCrc {
    std::uint32_t v = 0;
    void add(std::uint32_t t) { v = ((v << 1) | (v >> 31)) + t; }
    void add_i(int x) { add(static_cast<std::uint32_t>(x)); }
    void add_u64(std::uint64_t x) {
        add(static_cast<std::uint32_t>(x));
        add(static_cast<std::uint32_t>(x >> 32));
    }
    // Canonical double under the W9-05 sync-hash policy (om::canonical_bits,
    // the same rule the save / full sync hash io_scalar applies): every NaN
    // folds as the one quiet NaN, everything else its exact IEEE bits.
    void add_d(double d) { add_u64(om::canonical_bits(d)); }
};

// The per-frame CRC of the World's sim state (see above). `extra` is folded
// next (GameScene passes its sim-side FreeUnit tracker), then the map-trigger
// runtime when `triggers` is set (campaign / co-op).
std::uint32_t net_frame_crc(const World& w, const std::vector<int>* extra = nullptr,
                            const MapTriggerSystem* triggers = nullptr);

}  // namespace ra2engine
