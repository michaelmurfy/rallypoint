#include "doctest.h"
#include "repair_sell.hpp"
using namespace ra2engine;

// Every pin below is a literal recovered from YR-1.001 gamemd.exe / this
// install's rulesmd.ini -- docs/re/repair-sell.md (§ refs inline). Every
// numeric expectation is copied VERBATIM from the §D item 9 MEASURED 24-row
// table, which is authoritative over the §A2/§B3 worked examples printed
// earlier in that document (see §D0: GAPOWR is Strength=750/Crewed=yes,
// GAREFN/NAREFN set Soylent=300, CAOILD sets Unsellable=yes, GATOW has no
// rulesmd section at all).

namespace {
// A minimal type/instance pair for the predicate cases.
BuildingTypeDef mk_type(int cost, int strength) {
    BuildingTypeDef t;
    t.cost = cost;
    t.strength = strength;
    return t;
}
Building mk_bldg(int hp, int max_hp) {
    Building b;
    b.hp = hp;
    b.max_hp = max_hp;
    b.state = Building::State::Idle;
    b.alive = true;
    return b;
}
}  // namespace

TEST_CASE("repair: interval is trunc(RepairRate*900) = 14, NOT 15 [RE: §A2]") {
    // 0.016 * 900 = 14.4 and __ftol TRUNCATES. Getting 15 here is a 7% error.
    CHECK(repair_interval_frames(0.016) == 14);
    CHECK(repair_interval_frames(0.02) == 18);
    CHECK(repair_interval_frames(1.0 / 900.0) == 1);
    CHECK(repair_interval_frames(0.0) == 1);      // floored, never divides by zero
    CHECK(repair_interval_frames(-1.0) == 1);
}

TEST_CASE("repair: the tick is a GLOBAL frame phase [RE: §A2]") {
    const int iv = repair_interval_frames(0.016);   // 14
    CHECK(repair_due(0, iv));
    CHECK_FALSE(repair_due(1, iv));
    CHECK_FALSE(repair_due(13, iv));
    CHECK(repair_due(14, iv));
    CHECK(repair_due(28, iv));
    CHECK(repair_due(140, iv));
    CHECK_FALSE(repair_due(15, iv));                // the 15-frame trap
    CHECK_FALSE(repair_due(10, 0));                 // guard: interval 0
}

TEST_CASE("repair: stepCost uses TWO integer divisions [RE: §D item 9 table]") {
    // GACNST 3000/1000 [§D9 row 0]: 1000/8 = 125; 3000/125 = 24; 24*0.15 = 3.60 -> 3
    CHECK(repair_step_cost(3000, 1000, 8, 0.15) == 3);
    // GAPOWR 800/750 [§D9 row 1, §D0.1 -- Strength is 750, NOT the 1000 the
    // §A2 worked example prints]: 750/8 = 93; 800/93 = 8; 8*0.15 = 1.20 -> 1
    CHECK(repair_step_cost(800, 750, 8, 0.15) == 1);
    // GAWEAP 2000/1000 [§D9 row 3]: 125; 2000/125 = 16; 16*0.15 = 2.40 -> 2
    CHECK(repair_step_cost(2000, 1000, 8, 0.15) == 2);
    // GATECH 2000/500 [§D9 row 18] -- the roster's only 4: 62; 2000/62 = 32;
    // 32*0.15 = 4.80 -> 4
    CHECK(repair_step_cost(2000, 500, 8, 0.15) == 4);
    // NAWALL 100/300 [§D9 row 16]: 37; 100/37 = 2; 2*0.15 = 0.30 -> clamped to 1
    CHECK(repair_step_cost(100, 300, 8, 0.15) == 1);
    // GATOW [§D9 row 11, §D0.4]: NO rulesmd section at all -> Cost 0, Strength 0.
    // The per == 0 guard is OURS and load-bearing; the original would #DE here.
    CHECK(repair_step_cost(0, 0, 8, 0.15) == 1);
    // The naive float form is ~17% wrong on a ConYard: 3000 * 0.15 = 450 credits
    // to full, but the real total is (1000/8) steps * 3 = 375.
    const int steps_to_full = 1000 / 8;                       // 125 steps of 8 HP
    CHECK(steps_to_full * repair_step_cost(3000, 1000, 8, 0.15) == 375);
    CHECK(steps_to_full * repair_step_cost(2000, 1000, 8, 0.15) == 250);
    // GAPOWR's real total-to-full is 94 steps (ceil(750/8)), not 125 [§D0.1].
    CHECK(94 * repair_step_cost(800, 750, 8, 0.15) == 94);
    // max(1, .) clamp and the divide-by-zero guards.
    CHECK(repair_step_cost(1, 1000, 8, 0.15) == 1);
    CHECK(repair_step_cost(500, 4, 8, 0.15) == 75);           // Strength < RepairStep -> per == 0
    CHECK(repair_step_cost(500, 1000, 0, 0.15) == 75);        // RepairStep == 0 -> per == 0
}

TEST_CASE("sell: the refund is FLAT and Soylent bypasses RefundPercent [RE: §B1]") {
    // Human owner, no Soylent: trunc(trunc(3000*1*1) * 0.5) = 1500 [§D9 row 0].
    CHECK(sell_refund(3000, 0, 0.50, true) == 1500);
    CHECK(sell_refund(800, 0, 0.50, true) == 400);            // GAPOWR [§D9 row 1]
    CHECK(sell_refund(2000, 0, 0.50, true) == 1000);          // GAWEAP [§D9 row 3]
    CHECK(sell_refund(100, 0, 0.50, true) == 50);             // NAWALL [§D9 row 16]
    CHECK(sell_refund(0, 0, 0.50, true) == 0);                // CAOILD/GATOW [§D9 11/19]
    CHECK(sell_refund(999, 0, 0.50, true) == 499);            // trunc(499.5)
    // GAREFN/NAREFN [§D9 rows 8/9, §D0.2]: Soylent=300 overrides the Cost path
    // ENTIRELY -- 300, not trunc(2000 * 0.50) = 1000. This is load-bearing.
    CHECK(sell_refund(2000, 300, 0.50, true) == 300);
    // AI owner: RefundPercent is NOT applied (a real, recovered asymmetry).
    CHECK(sell_refund(3000, 0, 0.50, false) == 3000);
    // Soylent overrides AND bypasses RefundPercent, for human and AI alike.
    CHECK(sell_refund(3000, 700, 0.50, true) == 700);
    CHECK(sell_refund(3000, 700, 0.50, false) == 700);
    // The cost multipliers are applied INSIDE the inner trunc.
    CHECK(sell_refund(3000, 0, 0.50, true, /*country*/1.0, /*house*/0.5) == 750);
}

TEST_CASE("sell: survivor divisors and the clamp [RE: §D item 9 table]") {
    RepairSellRules r;
    CHECK(survivor_divisor(r, 0) == 500);      // Allied
    CHECK(survivor_divisor(r, 1) == 250);      // Soviet
    CHECK(survivor_divisor(r, 2) == 750);      // Yuri / Third
    CHECK(survivor_divisor(r, 3) == 0);        // Neutral -> no survivors
    CHECK(survivor_divisor(r, -1) == 0);
    // GACNST: refund 1500, Crewed=yes [§D9 row 0] -> survA 3, survS 5
    CHECK(survivor_count(1500, 500, true, false) == 3);
    CHECK(survivor_count(1500, 250, true, false) == 5);   // clamped from 6
    CHECK(survivor_count(1500, 750, true, false) == 2);
    // GAWEAP: refund 1000, Crewed=yes [§D9 row 3] -> survA 2, survS 4
    CHECK(survivor_count(1000, 500, true, false) == 2);
    CHECK(survivor_count(1000, 250, true, false) == 4);
    CHECK(survivor_count(1000, 750, true, false) == 1);
    // GAPOWR [§D9 row 1, §D0.1]: refund 400 and Crewed=YES -> 1 Allied / 1
    // Soviet, NOT the 0/0 the §B3 worked table prints.
    CHECK(survivor_count(400, 500, true, false) == 1);
    CHECK(survivor_count(400, 250, true, false) == 1);
    // GAREFN/NAREFN [§D9 rows 8/9]: the Soylent refund of 300 is what gets
    // divided, so 1/1 -- not the 2/4 the raw Cost would give.
    CHECK(survivor_count(300, 500, true, false) == 1);
    CHECK(survivor_count(300, 250, true, false) == 1);
    // GAPILL [§D9 row 10]: Crewed=no -> 0 regardless of the 250 refund.
    CHECK(survivor_count(250, 500, false, false) == 0);
    CHECK(survivor_count(250, 250, false, false) == 0);
    // floor 1: a Crewed building always yields at least one
    CHECK(survivor_count(0, 500, true, false) == 1);
    // a captured building DOUBLES the divisor
    CHECK(survivor_count(1500, 500, true, true) == 1);    // 1500 / 1000
    CHECK(survivor_count(0, 0, true, false) == 0);        // divisor 0 guard
}

TEST_CASE("repair: Can_Be_Click_Repaired [RE: §A4]") {
    BuildingTypeDef t = mk_type(3000, 1000);
    Building b = mk_bldg(500, 1000);
    CHECK(can_be_click_repaired(b, t));
    SUBCASE("full health is refused") { b.hp = 1000; CHECK_FALSE(can_be_click_repaired(b, t)); }
    SUBCASE("zero health is refused")  { b.hp = 0;    CHECK_FALSE(can_be_click_repaired(b, t)); }
    SUBCASE("under construction is refused") {
        b.state = Building::State::Buildup;
        CHECK_FALSE(can_be_click_repaired(b, t));
    }
    SUBCASE("dead is refused") { b.state = Building::State::Dead; CHECK_FALSE(can_be_click_repaired(b, t)); }
    SUBCASE("tombstoned is refused") { b.alive = false; CHECK_FALSE(can_be_click_repaired(b, t)); }
    SUBCASE("ClickRepairable=no is refused") {
        t.click_repairable = false;
        CHECK_FALSE(can_be_click_repaired(b, t));
    }
    SUBCASE("ClickRepairable defaults to YES") { CHECK(mk_type(1, 1).click_repairable); }
}

TEST_CASE("sell: Can_Be_Sold -- and the two rules that DO NOT EXIST [RE: §B5]") {
    BuildingTypeDef t = mk_type(3000, 1000);
    Building b = mk_bldg(1000, 1000);
    CHECK(can_be_sold(b, t, false));
    SUBCASE("Unsellable=yes is refused") { t.unsellable = true; CHECK_FALSE(can_be_sold(b, t, false)); }
    SUBCASE("CAOILD is the one rostered Unsellable type [§D9 row 19, §D0.3]") {
        BuildingTypeDef oil = mk_type(0, 1000);
        oil.unsellable = true;                       // CAOILD sets Unsellable=yes
        CHECK_FALSE(can_be_sold(mk_bldg(1000, 1000), oil, false));
    }
    SUBCASE("under construction is refused") {
        b.state = Building::State::Buildup;
        CHECK_FALSE(can_be_sold(b, t, false));
    }
    SUBCASE("already selling is refused") { b.selling = true; CHECK_FALSE(can_be_sold(b, t, false)); }
    SUBCASE("C4 planted is refused") { CHECK_FALSE(can_be_sold(b, t, true)); }
    SUBCASE("dead is refused") { b.state = Building::State::Dead; CHECK_FALSE(can_be_sold(b, t, false)); }
    SUBCASE("tombstoned is refused") { b.alive = false; CHECK_FALSE(can_be_sold(b, t, false)); }
    // CONFIRMED BY ABSENCE (§B5): neither of these is a refusal in the binary.
    SUBCASE("a FULL-HEALTH CONSTRUCTION YARD IS sellable (no last-ConYard rule)") {
        BuildingTypeDef cy = mk_type(3000, 1000);
        cy.conyard = true;
        Building only = mk_bldg(1000, 1000);
        CHECK(can_be_sold(only, cy, false));
    }
    SUBCASE("a DAMAGED building is sellable, and refunds the same as a full one") {
        Building hurt = mk_bldg(1, 1000);
        CHECK(can_be_sold(hurt, t, false));
        CHECK(sell_refund(t.cost, t.soylent, 0.50, true) == 1500);   // no HP term
    }
}

TEST_CASE("sell: the buildup plays MIRRORED [RE: §B2]") {
    Building b = mk_bldg(1000, 1000);
    b.buildup_total = 3.6;                       // BuildupTime .06 -> 54 ticks @15Hz
    const int shp_frames = 20;                   // 10 image frames + 10 shadows
    const int image_frames = shp_frames / 2;
    b.sell_time = 0.0;
    CHECK(mirrored_buildup_frame(b, shp_frames) == image_frames - 1);   // starts at the LAST frame
    b.sell_time = 3.6;
    CHECK(mirrored_buildup_frame(b, shp_frames) == 0);                  // ends at the FIRST
    // Equal and opposite to buildup_frame at every stage.
    for (int k = 0; k < image_frames; ++k) {
        const double t = (b.buildup_total / image_frames) * k + 1e-9;
        b.sell_time = t;
        b.buildup_time = t;
        CHECK(mirrored_buildup_frame(b, shp_frames) ==
              image_frames - 1 - buildup_frame(b, shp_frames));
    }
    SUBCASE("no buildup art -> frame 0 (the instant-removal branch)") {
        b.buildup_total = 0.0;
        CHECK(mirrored_buildup_frame(b, shp_frames) == 0);
        CHECK(mirrored_buildup_frame(b, 0) == 0);
    }
}
