#pragma once
// Pure CB-3 LandType foundation [RE-GATE APPLIED: docs/re/naval.md §2]: the
// map's per-cell land type, decoded from the theater tile-set classification --
// each map tile's owning [TileSetNNNN] set is matched against the theater
// control INI's [General] set indices (WaterSet/ShorePieces/CliffSet/...). The
// .tmp per-tile TerrainType byte was once judged FALSIFIED for LandType (a direct
// decode gave 9-15, zero Water) -- WRONG (fv7c, 13a #1): the byte is an index
// into the exe table DAT_008278F4 {0,8,8,8,8,10,9,3,3,2,6,1,1,0,7,3}
// (FUN_00544910), where 9 is Water. engine/main.cpp resolve_land_types now
// reads it per sub-tile; this TILE-SET classification is only its fall-back.
// This is the net-new water-cell identification the engine lacked before CB-3
// (spec "Current-engine reality"). No SDL/OS -- compiled into both
// ra2assets_tests and ra2engine.
#include <cstdint>

namespace ra2assets {

// The land-type categories the passability matrix (spec §Recovered exemplar
// ground truth) is indexed by, in the RECOVERED binary enum order (name table
// 0x00838D60). Water(2) is the only one CB-3 acts on (a Float naval unit is
// passable on Water, blocked everywhere else); the rest are classified so the
// map's water cells are distinguished from land and the enum matches the
// engine's Ground[LandType][SpeedType] row order for the speed-weighted
// carry-forward. Clear = 0 so a value-initialized / unclassified LandType is
// Clear (land, never water).
enum class LandType {
    Clear    = 0,   // open ground / any unclassified tile-set (land, never water)
    Road     = 1,   // paved
    Water    = 2,   // open water -- the ONLY passable land type for a Float ship
    Rock     = 3,   // cliffs / impassable
    Wall     = 4,
    Tiberium = 5,
    Beach    = 6,   // shore (0% for plain Float -- a pure ship cannot beach)
    Rough    = 7,
    Ice      = 8,
    Railroad = 9,
    Tunnel   = 10,
    Weeds    = 11,
};

// The theater [General] tile-set indices that classify a tile's owning
// [TileSetNNNN] set [RE-GATE APPLIED: naval.md §2]. Each field is the SET index
// (the NNNN in [TileSetNNNN]); -1 = the key was absent (never matches a valid
// set index). Only `water` is load-bearing for CB-3; the rest are classified
// for completeness. Populated by ra2engine::Theater from the control INI's
// [General] section.
struct TileSetClass {
    int water        = -1;   // WaterSet=21    -> Water
    int shore        = -1;   // ShorePieces=12 -> Beach
    int cliff        = -1;   // CliffSet=10    -> Rock
    int water_cliffs = -1;   // WaterCliffs=15 -> Rock
    int rough        = -1;   // RoughTile=13   -> Rough
    int clear        = -1;   // ClearTile=0    -> Clear
};

// Classify a tile's owning tile-set index against the [General] indices ->
// LandType [RE-GATE APPLIED: naval.md §2]. A set matching WaterSet is Water; the
// named shore/cliff/rough/clear sets map as above; anything else (or an unknown
// / -1 set index) is Clear -- land, NEVER water. Pure; host-testable with a
// hand-built TileSetClass (no SDL / no install). Only the Water case is
// load-bearing for CB-3.
LandType land_type_from_tile_set(int set_index, const TileSetClass& g);

}  // namespace ra2assets
