// Rallypoint: IsGattling stage timer and weapon-slot pick (synthetic rules).
#include "doctest.h"
#include "gattling.hpp"

using ra2engine::GattlingRules;
using ra2engine::WeaponSlot;
using ra2engine::gattling_pick;
using ra2engine::gattling_stage;
using ra2engine::gattling_tick;
using ra2engine::gattling_weapon_index;

static GattlingRules ytnk_rules() {
    GattlingRules r;
    r.enabled = true;
    r.rate_up = 1;
    r.rate_down = 50;
    r.weapon_stages = 3;
    r.stage_end[0] = 200;
    r.stage_end[1] = 400;
    r.stage_end[2] = 600;
    r.elite_stage_end[0] = 100;
    r.elite_stage_end[1] = 200;
    r.elite_stage_end[2] = 300;
    r.has_elite_stages = true;
    return r;
}

TEST_CASE("gattling: RateUp climbs to StageMax; RateDown decays; 0 resets") {
    const GattlingRules r = ytnk_rules();
    int t = 0;
    for (int i = 0; i < 250; ++i) t = gattling_tick(t, true, r);
    CHECK(t == 250);
    CHECK(gattling_stage(t, r, false) == 1);   // past Stage1=200

    for (int i = 0; i < 400; ++i) t = gattling_tick(t, true, r);
    CHECK(t == 600);                           // capped at Stage3
    CHECK(gattling_stage(t, r, false) == 2);

    t = gattling_tick(t, false, r);            // RateDown=50
    CHECK(t == 550);
    GattlingRules instant = r;
    instant.rate_down = 0;
    CHECK(gattling_tick(550, false, instant) == 0);
}

TEST_CASE("gattling: stage endpoints and elite spool faster") {
    const GattlingRules r = ytnk_rules();
    CHECK(gattling_stage(0, r, false) == 0);
    CHECK(gattling_stage(199, r, false) == 0);
    CHECK(gattling_stage(200, r, false) == 1);
    CHECK(gattling_stage(400, r, false) == 2);
    CHECK(gattling_stage(600, r, false) == 2);

    CHECK(gattling_stage(100, r, true) == 1);  // EliteStage1=100
    CHECK(gattling_stage(200, r, true) == 2);
    CHECK(gattling_stage(50, r, true) == 0);
}

TEST_CASE("gattling: weapon index is AG odd / AA even per stage") {
    CHECK(gattling_weapon_index(0, false) == 0);  // Weapon1
    CHECK(gattling_weapon_index(0, true) == 1);   // Weapon2
    CHECK(gattling_weapon_index(1, false) == 2);  // Weapon3
    CHECK(gattling_weapon_index(1, true) == 3);   // Weapon4
    CHECK(gattling_weapon_index(2, false) == 4);  // Weapon5
    CHECK(gattling_weapon_index(2, true) == 5);   // Weapon6
}

TEST_CASE("gattling: pick prefers elite slot when present") {
    std::vector<WeaponSlot> base(6), elite(6);
    base[0].valid = true;
    base[0].damage = 25;
    base[4].valid = true;
    base[4].damage = 40;
    elite[4].valid = true;
    elite[4].damage = 50;

    CHECK(gattling_pick(base, elite, 0, false).damage == 25);
    CHECK(gattling_pick(base, elite, 4, false).damage == 40);
    CHECK(gattling_pick(base, elite, 4, true).damage == 50);
    CHECK_FALSE(gattling_pick(base, elite, 1, false).valid);  // empty AA slot
}
