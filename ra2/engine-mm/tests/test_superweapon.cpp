#include "doctest.h"

#include <array>
#include <string>

#include "asset_fixture.hpp"
#include "superweapon.hpp"
#include "world.hpp"
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

TEST_CASE("sw building anim: charge holds SuperAnim, fire is a one-shot") {
    // Weather Control (and the other SuperAnim buildings) while recharging.
    SwAnimInput in;
    in.have = true;
    in.remain_secs = 540;          // 9 min left of a 10 min recharge
    in.charged_secs = 60;          // ChargedAnimTime=1 minute
    CHECK(sw_building_anim_phase(in) == 0);
    in.phase = 0;
    in.remain_secs = 61;
    CHECK(sw_building_anim_phase(in) == 0);
    in.remain_secs = 60;           // the sting starts
    CHECK(sw_building_anim_phase(in) == 1);
    in.phase = 1;
    CHECK(sw_building_anim_phase(in) == 1);
    in.oneshot_done = true;        // then the ready hold, still inside the window
    CHECK(sw_building_anim_phase(in) == 2);
    in.phase = 2;
    in.ready = true;
    in.remain_secs = 0;
    CHECK(sw_building_anim_phase(in) == 2);
    in.was_ready = true;           // fired
    in.ready = false;
    in.remain_secs = 600;
    CHECK(sw_building_anim_phase(in) == 3);
    in.phase = 3;
    in.was_ready = false;
    in.oneshot_done = false;
    CHECK(sw_building_anim_phase(in) == 3);
    in.oneshot_done = true;        // back to the charge hold, not another fire loop
    CHECK(sw_building_anim_phase(in) == 0);
}

TEST_CASE("sw building anim: already-ready and no-weapon") {
    SwAnimInput ready;
    ready.have = true;
    ready.ready = true;
    CHECK(sw_building_anim_phase(ready) == 2);
    SwAnimInput none;
    none.phase = 2;
    CHECK(sw_building_anim_phase(none) == -1);
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
    CHECK(std::string(superweapon_type_name(SuperweaponType::SpyPlane)) == "SpyPlane");
}

TEST_CASE("superweapon: Spy Plane fire reveals shroud (camera sweep fallback)") {
    // [RE: FUN_006CD66F]: without a registered SPYP type the fire path still
    // clears a Camera Damage= disc at the target so the SW is not a no-op.
    if (!ra2fixture::mapsmd03_available()) { MESSAGE("skipped: real map assets not found"); return; }
    ra2assets::MapFile map = ra2fixture::mapsmd03_map();
    World w(map);
    REQUIRE(w.grid_w() > 30);
    REQUIRE(w.grid_h() > 30);
    w.enable_shroud(true);
    REQUIRE(w.shroud_enabled());
    const int tx = w.grid_w() / 2, ty = w.grid_h() / 2;
    REQUIRE_FALSE(w.explored_at(0, tx, ty));

    std::array<int, 11> verses{};
    verses.fill(100);
    w.grant_superweapon(0, SuperweaponType::SpyPlane, /*recharge_secs=*/1.0, 0, 0.0, 1.0, verses);
    for (int f = 0; f < 60 && !w.sw_ready(0, SuperweaponType::SpyPlane); ++f) w.step(1.0 / 15.0);
    REQUIRE(w.sw_ready(0, SuperweaponType::SpyPlane));
    REQUIRE(w.fire_superweapon_at(0, SuperweaponType::SpyPlane, tx, ty));
    CHECK(w.explored_at(0, tx, ty));
    CHECK(w.explored_at(0, tx + 5, ty));
    CHECK_FALSE(w.explored_at(0, tx + 8, ty));  // outside Damage=6
}
