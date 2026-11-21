#include "doctest.h"

#include <string>

#include "superweapon.hpp"
using namespace ra2engine;

TEST_CASE("superweapon: charges to READY at exactly recharge_secs, not before") {
    Superweapon sw;
    sw.type = SuperweaponType::Nuke;
    sw.recharge_secs = 10.0;
    const double dt = 1.0;
    for (int i = 0; i < 9; ++i) { sw.tick(dt); CHECK_FALSE(sw.is_ready()); }
    CHECK(sw.timer == doctest::Approx(9.0));
    sw.tick(dt);                     // reaches 10.0 -> READY
    CHECK(sw.is_ready());
    CHECK(sw.timer == doctest::Approx(10.0));
}

TEST_CASE("superweapon: a mid-charge tick is monotonic and never overshoots") {
    Superweapon sw;
    sw.type = SuperweaponType::Nuke;
    sw.recharge_secs = 5.0;
    double last = 0.0;
    for (int i = 0; i < 3; ++i) { sw.tick(2.0); CHECK(sw.timer >= last); last = sw.timer; }
    CHECK(sw.timer == doctest::Approx(5.0));   // clamped, not 6.0
    CHECK(sw.is_ready());
    sw.tick(100.0);                            // no-op once ready
    CHECK(sw.timer == doctest::Approx(5.0));   // never overshoots the cap
}

TEST_CASE("superweapon: on_fired resets timer to 0 and clears ready") {
    Superweapon sw;
    sw.type = SuperweaponType::Nuke;
    sw.recharge_secs = 4.0;
    sw.tick(4.0);
    REQUIRE(sw.is_ready());
    sw.on_fired();
    CHECK(sw.timer == doctest::Approx(0.0));
    CHECK_FALSE(sw.is_ready());
    // Re-charges from 0 after firing.
    sw.tick(4.0);
    CHECK(sw.is_ready());
}

TEST_CASE("superweapon: charge_fraction tracks 0..1 and an uninitialized SW is inert") {
    Superweapon sw;                            // type None, recharge 0
    CHECK(sw.charge_fraction() == doctest::Approx(0.0));
    sw.tick(100.0);                            // type None -> no-op
    CHECK_FALSE(sw.is_ready());
    sw.type = SuperweaponType::Nuke;
    sw.recharge_secs = 8.0;
    sw.tick(4.0);
    CHECK(sw.charge_fraction() == doctest::Approx(0.5));
    sw.tick(4.0);
    CHECK(sw.charge_fraction() == doctest::Approx(1.0));
}

TEST_CASE("superweapon: the SuperweaponType enum + [SuperWeaponTypes] id mapping") {
    // [RE-GATE APPLIED: superweapon.md §4] the recovered roster (only Nuke active).
    // Enum follows the binary Type order (+1 for None=0): AmerParaDrop is DISTINCT
    // (7), SpyPlane (9) comes after PsychicDominator (8).
    CHECK(static_cast<int>(SuperweaponType::None) == 0);
    CHECK(static_cast<int>(SuperweaponType::Nuke) == 1);
    CHECK(static_cast<int>(SuperweaponType::ParaDrop) == 6);
    CHECK(static_cast<int>(SuperweaponType::AmerParaDrop) == 7);       // distinct entry (§4)
    CHECK(static_cast<int>(SuperweaponType::PsychicDominator) == 8);
    CHECK(static_cast<int>(SuperweaponType::SpyPlane) == 9);           // idx 8 in binary, 9 here
    CHECK(superweapon_type_from_id(1) == SuperweaponType::Nuke);
    CHECK(superweapon_type_from_id(2) == SuperweaponType::IronCurtain);
    CHECK(superweapon_type_from_id(6) == SuperweaponType::ParaDrop);        // ParaDropSpecial
    CHECK(superweapon_type_from_id(7) == SuperweaponType::AmerParaDrop);    // American is DISTINCT (§4)
    CHECK(superweapon_type_from_id(9) == SuperweaponType::SpyPlane);        // SpyPlaneSpecial
    CHECK(superweapon_type_from_id(0) == SuperweaponType::None);       // unknown -> None
    CHECK(superweapon_type_from_id(99) == SuperweaponType::None);
    CHECK(std::string(superweapon_type_name(SuperweaponType::Nuke)) == "Nuke");
    CHECK(std::string(superweapon_type_name(SuperweaponType::AmerParaDrop)) == "AmerParaDrop");
}
