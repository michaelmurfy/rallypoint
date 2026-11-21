#pragma once
// Pure ore-field state: per-cell tiberium kind/density/variant from the map's
// overlay, harvester consumption, and the growth/spread timer.
// Calibration: docs/re/economy.md (§ refs inline). No SDL/OS.
#include <cstdint>
#include <vector>
#include "occupancy.hpp"
#include "ra2assets/map.hpp"

namespace ra2engine {

enum class OreKind : std::uint8_t { None = 0, Ore = 1, Gems = 2 };

// [RE-GATE APPLIED x2: economy.md §5, RESOLVED] the binary classifies
// range-based (FUN_005FD510): overlay index i belongs to tiberium t iff
// i is in [base_t, base_t + NumImages=12) flat images or the following
// NumSlopes=8 slope frames. The runtime overlay index is the 0-based
// ORDER-OF-APPEARANCE position in [OverlayTypes] (NOT key-1 — the INI
// keys have gaps): positions GEM01=27, TIB01=102, TIB2_01=127,
// TIB3_01=147 — exact match to the recovered binary bases, and Task 2's
// real-map dump observed exactly indices {27,102,127} with data bytes
// 0..11 (variant-indexed model CONFIRMED). The 20-entry TIB blocks
// decompose as 12 flat images + 8 slope variants (TIB13..TIB20).
// FINAL table: gems [27,39); ore [102,122) + [127,147) + [147,167)
// (each = 12 flat + 8 slope; slope cells classify as ore too).
OreKind overlay_ore_kind(int overlay_index);
// 0-based art variant within its set (TIB07 -> 6); -1 for non-ore indices.
int overlay_ore_variant(int overlay_index);

// [RE-GATE APPLIED: economy.md §4 CONFIRMED] density byte is 0-based,
// cap 11 (each tiberium overlay has NumImages = 12 density frames).
constexpr int kMaxOreDensity = 11;

// Deterministic engine-local RNG (xorshift32) for growth/spread selection.
// DOCUMENTED DEVIATION: the original uses its global Randomizer; matching its
// stream is unreachable, so selection RULES are calibrated, the stream is ours.
struct OreRng {
    std::uint32_t s = 0x0EC0417Au;   // fixed seed: demo determinism
    std::uint32_t next();            // xorshift32; never returns with s==0
    int below(int n);                // uniform [0, n), n >= 1
};

// W2-SK-01 [RE: gamemd TerrainClass::AI 0x71B9D0 + CellClass forced spread
// FUN_00483690 / place FUN_004870A0, pinned by the wave-2 skirmish fix]:
// an ore mine (TerrainType SpawnsTiberium=yes, TIBTRE01-03) is an animated
// terrain object. Every logic frame a mine that is NOT animating starts its
// animation with probability AnimationProbability (abs(Random)%1000000 *
// 1e-6 < p); an animating mine advances one stage every AnimationRate frames;
// when the stage reaches half the SHP's frame count (the second half is the
// shadow set) the animation stops and the mine spawns ore ONCE: a random start
// direction 0..7, then the first of the 8 neighbours (DAT_0089E5F8 order) that
// can hold tiberium (FUN_004837F0: land allows tiberium, no overlay, not
// another mine, no building) receives the mine cell's tiberium type (none ->
// type 0 = Riparius ore) at OverlayData 3. A mine never raises existing ore;
// once its ring is full it only feeds the field through normal growth/spread.
struct OreMine {
    int x = 0, y = 0;            // the mine's own (impassable) cell
    int half_frames = 1;         // SHP frame count / 2 (spawn stage)
    int anim_rate = 3;           // AnimationRate= (frames per stage)
    double anim_prob = 0.003;    // AnimationProbability= (per frame)
    int stage = 0;               // current animation stage
    int rate = 0;                // 0 = not animating (TerrainClass+0xC0)
    int timer = 0;               // frames left in the current stage
    int spawned = 0;             // cells spawned so far (diagnostics)
};
constexpr int kMineSpawnDensity = 3;   // FUN_00483690 -> FUN_004870A0(tib, 3)

constexpr int kOreScanRadius = 32;       // harvester scan, Chebyshev cells: the binary DEFAULT only
// W2-SK-02 [RE: rulesmd [General] TiberiumLongScan=48 / TiberiumShortScan=6,
// Rules+0x177C/+0x1778 loaded by FUN_0066CF70; 0x2000 leptons = 32 cells is
// only FUN_00665090's default]. The World hands the rules values in.
struct HarvestScan {
    int long_scan = kOreScanRadius;
    int short_scan = 6;
};

class OreField {
public:
    friend struct SaveAccess;  // P7 Save/Load (engine/save_fields.hpp)
    OreField() = default;
    OreField(int w, int h);                       // empty field (tests)
    // Build from the map: for every terrain cell, classify overlay_at via
    // overlay_ore_kind; density = clamp(overlay_data_at, 0, kMaxOreDensity).
    static OreField from_map(const ra2assets::MapFile& map, int grid_w, int grid_h);

    int w() const;
    int h() const;
    bool in(int x, int y) const;
    OreKind kind_at(int x, int y) const;          // None when out of range
    int density_at(int x, int y) const;           // 0 when out of range/None
    int variant_at(int x, int y) const;           // art variant, 0 default
    bool has_ore(int x, int y) const;             // kind != None (density >= 0 means >=1 bale: see take_bale)
    void set(int x, int y, OreKind k, int density, int variant);

    // One gulp [RE-GATE: economy.md §1]: pre-hunt, a gulp removes one
    // density step; the cell empties (kind -> None) when density hits 0.
    // Returns the kind consumed, or None if the cell had nothing.
    OreKind take_bale(int x, int y);

    // Growth/spread timer [RE-GATE APPLIED: economy.md §4 — per-type
    // fields, GrowthRate cadence, cap 11, and spread-from-fully-grown are
    // recovered; the exact per-firing candidate-drain slice is
    // [RE-FALLBACK], adjudicated to the shape below; RNG = engine-local
    // seeded xorshift32 (documented deviation, README)]. Call once per sim
    // tick; fires every `interval_secs`. Per firing:
    //   growth: candidates = ore cells with density < kMaxOreDensity;
    //     n = max(1, (int)(candidates * growth_pct)) if candidates else 0;
    //     pick n via rng without replacement; density += 1.
    //   spread: sources = ore cells with density == kMaxOreDensity;
    //     n = max(1, (int)(sources * spread_pct)) if sources else 0; each
    //     picks one rng 8-neighbor that is in-grid, passable, empty of ore
    //     -> new Ore cell, density 1 (variant copied from the source).
    //   Gems (GrowthPercentage=0 in rulesmd) never grow or spread.
    // `passable` is the World passability grid (same w/h).
    void step(double dt, double interval_secs, double growth_pct, double spread_pct,
              const std::vector<std::uint8_t>& passable, OreRng& rng,
              const Occupancy* occ = nullptr);

    // W2-SK-01 regrowth [RE: gamemd TiberiumClass growth FUN_00721EE0 ->
    // FUN_007221A0 and spread FUN_00721450 -> FUN_007216E0, pinned by the
    // wave-2 skirmish fix]. When set (frames > 0) step() runs the original
    // per-type model instead of the GrowthRate fallback above:
    //   growth, every Growth= frames (Riparius 2200; x0.3 under a Rules flag
    //     that is off in skirmish): n = abs(Random) % clamp(ftol(H x
    //     GrowthPercentage), 5, 50) + 1 cells of the growth heap (H = cells
    //     below the top density) gain one density step (FUN_00483620 ->
    //     FUN_004870A0(tib, 1));
    //   spread, every Spread= frames: n = abs(Random) % clamp(ftol(H x
    //     SpreadPercentage), 5, 25) + 1 cells of the spread heap (H = cells
    //     whose density > tiberium index / 2 -- FUN_004835A0 -- with no
    //     object on them and an empty neighbour) each seed ONE neighbour at
    //     OverlayData 3 (FUN_00483690 non-forced -> FUN_004870A0(tib, 3)).
    //   The heaps are keyed by Random%50 + frame; [RE-FALLBACK] the pick is a
    //   uniform random draw without replacement from the candidate set.
    void set_original_timing(int growth_frames, int spread_frames) {
        growth_frames_ = growth_frames;
        spread_frames_ = spread_frames;
    }

    double timer() const;                          // accumulated seconds (tests)

    // W2-SK-01 ore mines (see OreMine). `frames` logic frames elapse (the
    // World steps one 1/15 s frame at a time). Returns the cells spawned.
    void add_mine(int x, int y, int half_frames, int anim_rate, double anim_prob);
    const std::vector<OreMine>& mines() const { return mines_; }
    bool is_mine(int x, int y) const;
    int step_mines(int frames, const std::vector<std::uint8_t>& passable, OreRng& rng);
private:
    int w_ = 0, h_ = 0;
    std::vector<std::uint8_t> kind_, density_, variant_;
    double timer_ = 0.0;
    std::vector<OreMine> mines_;   // W2-SK-01 (save v13)
    int growth_frames_ = 0, spread_frames_ = 0;   // W2-SK-01 original timing (0 = fallback)
    double spread_timer_ = 0.0;                    // W2-SK-01 spread clock (frames since Net L0)
    void step_original(double dt, double growth_pct, double spread_pct,
                       const std::vector<std::uint8_t>& passable, OreRng& rng, const Occupancy* occ);
};

}  // namespace ra2engine
