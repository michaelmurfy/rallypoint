// Rallypoint: Floating Disc drain — pure math and targeting (no game data).
#include "doctest.h"
#include "disc_drain.hpp"
#include "ra2assets/map.hpp"
#include "world.hpp"

namespace d = ra2engine::disc_drain;

TEST_CASE("disc_drain: stock CombatDamage defaults match YR") {
    CHECK(d::kDefaultMoneyDelay == 30);
    CHECK(d::kDefaultMoneyAmount == 30);
}

TEST_CASE("disc_drain: targeting needs a Drainable building") {
    CHECK(d::can_target(true, true));
    CHECK_FALSE(d::can_target(false, true));
    CHECK_FALSE(d::can_target(true, false));
    CHECK_FALSE(d::can_target(false, false));
}

TEST_CASE("disc_drain: firer must sit on the foundation") {
    // 2x2 at (10,20): cells (10,20)(11,20)(10,21)(11,21)
    CHECK(d::above_foundation(10, 20, 10, 20, 2, 2));
    CHECK(d::above_foundation(11, 21, 10, 20, 2, 2));
    CHECK_FALSE(d::above_foundation(9, 20, 10, 20, 2, 2));
    CHECK_FALSE(d::above_foundation(10, 19, 10, 20, 2, 2));
    CHECK_FALSE(d::above_foundation(12, 20, 10, 20, 2, 2));
    // 1x1
    CHECK(d::above_foundation(5, 5, 5, 5, 1, 1));
    CHECK_FALSE(d::above_foundation(6, 5, 5, 5, 1, 1));
    // degenerate sizes still treat as 1x1
    CHECK(d::above_foundation(0, 0, 0, 0, 0, 0));
}

TEST_CASE("disc_drain: classify power / money / disable") {
    CHECK(d::classify(false, true, 200) == d::Effect::None);
    CHECK(d::classify(true, false, 0) == d::Effect::Disable);
    CHECK(d::classify(true, false, -50) == d::Effect::Disable);   // Power drain only
    CHECK(d::classify(true, false, 200) == d::Effect::Power);
    CHECK(d::classify(true, true, 0) == d::Effect::Money);
    CHECK(d::classify(true, true, -30) == d::Effect::Money);
    CHECK(d::classify(true, true, 150) == d::Effect::MoneyAndPower);
    CHECK(d::causes_blackout(d::Effect::Power));
    CHECK(d::causes_blackout(d::Effect::MoneyAndPower));
    CHECK_FALSE(d::causes_blackout(d::Effect::Money));
    CHECK(d::causes_money(d::Effect::Money));
    CHECK_FALSE(d::causes_money(d::Effect::Power));
}

TEST_CASE("disc_drain: money transfer clamps to the victim wallet") {
    CHECK(d::money_transfer(1000, 30) == 30);
    CHECK(d::money_transfer(20, 30) == 20);
    CHECK(d::money_transfer(0, 30) == 0);
    CHECK(d::money_transfer(100, 0) == 0);
    CHECK(d::money_transfer(-5, 30) == 0);
}

TEST_CASE("disc_drain: money is due on the global frame [RE: 0x6FA167]") {
    CHECK(d::money_due(0, 30));
    CHECK_FALSE(d::money_due(29, 30));
    CHECK(d::money_due(30, 30));
    CHECK(d::money_due(90, 30));
    CHECK(d::money_due(7, 0));   // delay 0 treated as 1
    CHECK(d::money_due(7, 1));
}

TEST_CASE("disc_drain: live check runs on Frame % 16 == 2 [RE: 0x7361B3]") {
    CHECK(d::live_check_due(2));
    CHECK(d::live_check_due(18));
    CHECK(d::live_check_due(16 * 1000 + 2));
    CHECK_FALSE(d::live_check_due(0));
    CHECK_FALSE(d::live_check_due(3));
    CHECK_FALSE(d::live_check_due(17));
}

TEST_CASE("disc_drain: DrainStart / DrainEnd keep gamemd's single DrainingMe slot") {
    int t0 = -1, t1 = -1, slot = -1;
    d::start(t0, slot, 4, 0);
    CHECK(t0 == 4);
    CHECK(slot == 0);
    d::start(t1, slot, 4, 1);   // a second Disc takes the slot
    CHECK(slot == 1);
    d::end(t0, slot);           // the first Disc's DrainEnd clears it anyway
    CHECK(t0 == -1);
    CHECK(slot == -1);
    CHECK(t1 == 4);
}

namespace {
using namespace ra2engine;

struct DiscRig {
    ra2assets::MapFile map = ra2assets::MapFile::parse("[Map]\nSize=0,0,32,32\nTheater=TEMPERATE\n");
    World w{map};
    explicit DiscRig(bool refinery) {
        BuildingTypeDef t;
        t.id = refinery ? "NAREFN" : "NAPOWR";
        t.fw = 3;
        t.fh = 3;
        t.drainable = true;
        t.resource_destination = refinery;
        t.power = refinery ? -50 : 200;
        w.set_building_types({t});
        w.set_house(0, World::HouseRec{});
        w.set_house(1, World::HouseRec{});
        w.add_building(0, 10, 10, 3, 3, 1, 900, 0, 0.0, /*instant=*/true);
        UnitCombat disc;
        disc.strength = 600;
        w.add_combat_unit(11, 11, 0, disc);
        w.set_wallet(0, 0.0);
        w.set_wallet(1, 1000.0);
    }
    void drain(int unit) {
        disc_drain::start(w.units()[(size_t)unit].drain_target, w.buildings()[0].drained_by, 0, unit);
    }
    // Step until the frame the next World::step will run at is `f`.
    void step_to(std::uint64_t f) {
        while (w.vet_frame() < f) w.step(1.0 / 15.0);
    }
};
}  // namespace

TEST_CASE("disc_drain: world money: one payout per building every 30 frames") {
    DiscRig r(true);
    UnitCombat disc2;
    disc2.strength = 600;
    r.w.add_combat_unit(12, 11, 0, disc2);
    r.drain(0);
    r.drain(1);   // two Discs on one refinery still pay once a tick
    r.step_to(61);   // frames 0, 30 and 60 ran
    CHECK(r.w.credits(1) == doctest::Approx(1000.0 - 90.0));
    CHECK(r.w.credits(0) == doctest::Approx(90.0));
    CHECK(r.w.buildings()[0].drained_by == 1);
}

TEST_CASE("disc_drain: world money stops at an empty wallet") {
    DiscRig r(true);
    r.w.set_wallet(1, 40.0);
    r.drain(0);
    r.step_to(61);
    CHECK(r.w.credits(1) == doctest::Approx(0.0));
    CHECK(r.w.credits(0) == doctest::Approx(40.0));
}

TEST_CASE("disc_drain: world live check ends the drain only on Frame % 16 == 2") {
    DiscRig r(true);
    r.drain(0);
    r.step_to(3);   // frame 2 checked: still over the refinery
    CHECK(r.w.units()[0].drain_target == 0);
    Unit& u = r.w.units()[0];
    u.cell_x = 20; u.cell_y = 20; u.fx = 20.0; u.fy = 20.0;   // flown off the base
    r.step_to(18);  // frames 3..17: no check yet, the drain lives on
    CHECK(r.w.units()[0].drain_target == 0);
    CHECK(r.w.buildings()[0].drained_by == 0);
    r.step_to(19);  // frame 18: the live check ends it
    CHECK(r.w.units()[0].drain_target == -1);
    CHECK(r.w.buildings()[0].drained_by == -1);
}

TEST_CASE("disc_drain: world drain ends at once when the owners become allies") {
    DiscRig r(true);
    r.drain(0);
    r.step_to(5);
    CHECK(r.w.units()[0].drain_target == 0);
    r.w.set_pair_alliance(0, 1, true);
    r.step_to(6);
    CHECK(r.w.units()[0].drain_target == -1);
    CHECK(r.w.buildings()[0].drained_by == -1);
}

TEST_CASE("disc_drain: world drained power plant blacks its owner out") {
    DiscRig r(false);
    r.step_to(1);
    CHECK_FALSE(r.w.house_blacked_out(1));
    r.drain(0);
    r.step_to(2);
    CHECK(r.w.house_blacked_out(1));
    CHECK(r.w.credits(1) == doctest::Approx(1000.0));   // no ResourceDestination: no money
}
