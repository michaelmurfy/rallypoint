#include "ra2assets/land_type.hpp"

namespace ra2assets {

// [RE-GATE APPLIED: naval.md §2] classify a tile's owning tile-set index against
// the theater [General] indices. -1 general fields (absent keys) never match a
// valid set index (>= 0); an unknown/-1 set index is Clear -- land, NEVER water.
LandType land_type_from_tile_set(int set_index, const TileSetClass& g) {
    if (set_index < 0) return LandType::Clear;              // unknown tile -> land
    if (set_index == g.water) return LandType::Water;       // WaterSet -> the ship's domain
    if (set_index == g.shore) return LandType::Beach;       // ShorePieces (0% for Float)
    if (set_index == g.cliff || set_index == g.water_cliffs)
        return LandType::Rock;                               // CliffSet / WaterCliffs
    if (set_index == g.rough) return LandType::Rough;       // RoughTile
    if (set_index == g.clear) return LandType::Clear;       // ClearTile
    return LandType::Clear;                                  // any other set -> land, never water
}

}  // namespace ra2assets
