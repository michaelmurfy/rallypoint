#include "doctest.h"

#include "occupancy.hpp"

using ra2engine::Occupancy;

TEST_CASE("Occupancy: default grid is all empty and off-grid reads are empty") {
    Occupancy occ(4, 3);
    CHECK(occ.width() == 4);
    CHECK(occ.height() == 3);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 4; ++x) {
            CHECK(occ.at(x, y) == Occupancy::kEmpty);
            CHECK(occ.is_free(x, y));
        }
    // Off-grid is never owned and never enterable.
    CHECK(occ.at(-1, 0) == Occupancy::kEmpty);
    CHECK(occ.at(4, 0) == Occupancy::kEmpty);
    CHECK_FALSE(occ.is_free(-1, 0));   // off-grid is not "free" to enter
    CHECK_FALSE(occ.can_enter(4, 0, 7));
}

TEST_CASE("Occupancy: set / at / is_free / can_enter") {
    Occupancy occ(4, 3);
    occ.set(1, 1, 5);
    CHECK(occ.at(1, 1) == 5);
    CHECK_FALSE(occ.is_free(1, 1));
    CHECK(occ.is_free(2, 1));
    // Owner can re-enter its own cell; a different unit cannot.
    CHECK(occ.can_enter(1, 1, 5));
    CHECK_FALSE(occ.can_enter(1, 1, 6));
    CHECK(occ.can_enter(2, 1, 6));       // free cell
    CHECK_FALSE(occ.can_enter(-1, 1, 5));  // off-grid
}

TEST_CASE("Occupancy: release only clears the matching owner; clear is unconditional") {
    Occupancy occ(4, 3);
    occ.set(2, 2, 8);
    occ.release(2, 2, 9);            // wrong owner -> no-op
    CHECK(occ.at(2, 2) == 8);
    occ.release(2, 2, 8);            // right owner -> cleared
    CHECK(occ.at(2, 2) == Occupancy::kEmpty);
    occ.set(0, 0, 3);
    occ.clear(0, 0);                // unconditional
    CHECK(occ.at(0, 0) == Occupancy::kEmpty);
    // Off-grid set/clear/release are silent no-ops (no crash).
    occ.set(99, 99, 1);
    occ.clear(99, 99);
    occ.release(99, 99, 1);
}

TEST_CASE("Occupancy: release_all_of clears every cell of a unit except the kept one") {
    Occupancy occ(4, 3);
    occ.set(1, 1, 5);   // current cell
    occ.set(2, 1, 5);   // stale next-cell reservation
    occ.set(3, 2, 6);   // another unit
    occ.release_all_of(5, 1, 1);
    CHECK(occ.at(1, 1) == 5);                  // kept
    CHECK(occ.at(2, 1) == Occupancy::kEmpty);  // stale reservation cleared
    CHECK(occ.at(3, 2) == 6);                  // other units untouched
}
