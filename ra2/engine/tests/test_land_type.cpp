#include "doctest.h"

#include <initializer_list>

#include "ra2assets/land_type.hpp"
using namespace ra2assets;

TEST_CASE("land_type: WaterSet classifies as Water; the other named sets map") {
    // [RE-GATE APPLIED: naval.md §2] the recovered temperate [General] indices.
    TileSetClass g;
    g.water = 21; g.shore = 12; g.cliff = 10; g.water_cliffs = 15;
    g.rough = 13; g.clear = 0;
    CHECK(land_type_from_tile_set(21, g) == LandType::Water);   // WaterSet
    CHECK(land_type_from_tile_set(12, g) == LandType::Beach);   // ShorePieces
    CHECK(land_type_from_tile_set(10, g) == LandType::Rock);    // CliffSet
    CHECK(land_type_from_tile_set(15, g) == LandType::Rock);    // WaterCliffs
    CHECK(land_type_from_tile_set(13, g) == LandType::Rough);   // RoughTile
    CHECK(land_type_from_tile_set(0,  g) == LandType::Clear);   // ClearTile
}

TEST_CASE("land_type: an unclassified or unknown set is Clear (land, never Water)") {
    TileSetClass g;
    g.water = 21; g.shore = 12; g.cliff = 10; g.water_cliffs = 15;
    g.rough = 13; g.clear = 0;
    for (int s : {5, 9, 33, 41, 200}) {
        const LandType lt = land_type_from_tile_set(s, g);
        CHECK(lt == LandType::Clear);
        CHECK(lt != LandType::Water);       // an unknown set is NEVER water
    }
    // An out-of-range / absent set index (-1) is Clear and never matches an
    // absent [General] key (also -1).
    CHECK(land_type_from_tile_set(-1, g) == LandType::Clear);
    TileSetClass empty;                     // every key absent (-1)
    CHECK(land_type_from_tile_set(-1, empty) == LandType::Clear);
    CHECK(land_type_from_tile_set(0,  empty) == LandType::Clear);
}

TEST_CASE("land_type: the enum is the recovered binary order (Water == 2)") {
    // Pins the RE-GATE-applied enum order (name table 0x00838D60) that
    // build_water_passability / naval_fold depend on.
    CHECK(static_cast<int>(LandType::Clear) == 0);
    CHECK(static_cast<int>(LandType::Road)  == 1);
    CHECK(static_cast<int>(LandType::Water) == 2);
    CHECK(static_cast<int>(LandType::Rock)  == 3);
    CHECK(static_cast<int>(LandType::Beach) == 6);
}
