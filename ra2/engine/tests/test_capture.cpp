#include "doctest.h"
#include "capture.hpp"
using namespace ra2engine;

TEST_CASE("capture: cash_steal_amount truncates 0.5 x victim credits [RE: capture.md §B3]") {
    CHECK(cash_steal_amount(1000.0, 0.5) == 500);
    CHECK(cash_steal_amount(1001.0, 0.5) == 500);   // ftol trunc, not 500.5
    CHECK(cash_steal_amount(0.0, 0.5) == 0);
    CHECK(cash_steal_amount(-50.0, 0.5) == 0);       // never negative
    CHECK(cash_steal_amount(100.0, 0.0) == 0);
}

TEST_CASE("capture: apply_blackout forces output to 0, leaves drain [RE: capture.md §B2]") {
    PowerTotals pt; pt.output = 300; pt.drain = 120;
    PowerTotals on = apply_blackout(pt, true);
    CHECK(on.output == 0);
    CHECK(on.drain == 120);                          // drain unaffected
    PowerTotals off = apply_blackout(pt, false);     // identity when not blacked
    CHECK(off.output == 300);
    CHECK(off.drain == 120);
}

TEST_CASE("capture: startup granted only from the neutral house [RE: capture.md §A3]") {
    CHECK(grants_startup_from_neutral(kNeutralHouse));   // 2
    CHECK_FALSE(grants_startup_from_neutral(0));
    CHECK_FALSE(grants_startup_from_neutral(1));
}

TEST_CASE("capture: veteran multipliers apply only at rank>0 [RE: capture.md §B5]") {
    CHECK(veteran_firepower(100, 0) == 100);         // rank 0 = byte-identical
    CHECK(veteran_hp(100, 0) == 100);
    CHECK(veteran_firepower(100, 1) == 110);         // ×1.1 VeteranCombat, rounded
    CHECK(veteran_hp(100, 1) == 150);                // ×1.5 VeteranArmor, rounded
    CHECK(kNeutralHouse == 2);
}
