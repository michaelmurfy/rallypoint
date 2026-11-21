#include "doctest.h"
#include "abilities.hpp"
using namespace ra2engine;

// Every pin below is a literal recovered from YR-1.001 gamemd.exe and this
// install's rulesmd.ini -- docs/re/abilities.md (§ refs inline).

TEST_CASE("abilities: RadSite spread in leptons is n*256+128 [RE: §A1 0x65AF10]") {
    CHECK(rad_spread_leptons(10) == 2688);   // RadEruptionWarhead CellSpread=10
    CHECK(rad_spread_leptons(5) == 1408);    // CRNUKEWH CellSpread=5
    CHECK(rad_spread_leptons(0) == 128);
}

TEST_CASE("abilities: per-cell radiation falls off linearly to the spread edge [RE: §A2]") {
    CHECK(rad_cell_amount(0, 2688, 500) == doctest::Approx(500.0));
    CHECK(rad_cell_amount(1280, 2688, 500) == doctest::Approx(1408.0 / 2688.0 * 500.0));
    CHECK(rad_cell_amount(2688, 2688, 500) == doctest::Approx(0.0));
    CHECK(rad_cell_amount(2689, 2688, 500) == 0.0);   // outside -> exactly 0
}

TEST_CASE("abilities: RadSite duration bookkeeping [RE: §A1/§A3]") {
    CHECK(rad_level_steps(500, 90) == 5);            // 500 frames / 90 (int div)
    CHECK(rad_level_steps(940, 90) == 10);
    CHECK(rad_level_steps(500, 0) == 0);             // defensive
    CHECK(rad_add_current(500, 440, 500) == 440);    // ability_ftol(L*left/duration)
    CHECK(rad_add_current(500, 0, 0) == 0);
    // Deactivate owes (left/delay + 1) decrements of amt/steps.
    CHECK(rad_deactivate_amount(100.0, 5, 440, 90) == doctest::Approx(100.0));
    CHECK(rad_deactivate_amount(100.0, 5, 80, 90) == doctest::Approx(20.0));
}

TEST_CASE("abilities: radiation damage = ability_ftol(ability_ftol(min(lvl,500))*0.2) [RE: §A4 0x4DA502]") {
    const AbilityRules r;
    CHECK(rad_damage(500.0, r) == 100);
    CHECK(rad_damage(940.0, r) == 100);     // clamped to RadLevelMax at READ time
    CHECK(rad_damage(261.9, r) == 52);      // ability_ftol(261)=261 -> 52.2 -> 52
    CHECK(rad_damage(4.9, r) == 0);         // 4 * 0.2 = 0.8 -> 0
    CHECK(rad_damage(0.5, r) == 0);         // level ftol's to 0 -> no damage call
    CHECK(rad_apply_due(0, r));
    CHECK(rad_apply_due(32, r));
    CHECK_FALSE(rad_apply_due(15, r));      // absolute 16-frame phase
}

TEST_CASE("abilities: RadField create / decay / delete [RE: §A1-§A3]") {
    const AbilityRules r;
    RadField f;
    f.init(30, 30);
    f.detonate(15, 15, 10, 500, /*frame=*/0, r);
    CHECK(f.live_sites() == 1);
    CHECK(f.level_at(15, 15) == doctest::Approx(500.0));
    CHECK(f.level_at(20, 15) == doctest::Approx(rad_cell_amount(1280, 2688, 500)));
    // TimeLeft-- every frame; the first decay is when Frame - start >= 90.
    for (std::uint64_t fr = 1; fr < 90; ++fr) f.step(fr, r);
    CHECK(f.level_at(15, 15) == doctest::Approx(500.0));
    f.step(90, r);
    CHECK(f.level_at(15, 15) == doctest::Approx(400.0));   // -500/5
    for (std::uint64_t fr = 91; fr <= 500; ++fr) f.step(fr, r);
    CHECK(f.live_sites() == 0);                            // RadTimeLeft hit 0
    CHECK(f.level_at(15, 15) == doctest::Approx(0.0).epsilon(1e-9));
}

TEST_CASE("abilities: RadField Add accumulates a re-fired eruption [RE: §A1 0x65AF70]") {
    const AbilityRules r;
    RadField f;
    f.init(30, 30);
    f.detonate(15, 15, 10, 500, 0, r);
    for (std::uint64_t fr = 1; fr <= 60; ++fr) f.step(fr, r);   // left = 440
    f.detonate(15, 15, 10, 500, 60, r);                          // cur 440 + 500
    CHECK(f.live_sites() == 1);
    CHECK(f.sites()[0].rad_level == 940);
    CHECK(f.sites()[0].duration == 940);
    CHECK(f.sites()[0].level_steps == 10);
    CHECK(f.level_at(15, 15) == doctest::Approx(940.0));
}

TEST_CASE("abilities: drone suppression = 2*damage - threshold, only above it [RE: §B3]") {
    CHECK(suppression_frames(25, 5) == 45);
    CHECK(suppression_frames(5, 5) == 0);
    CHECK(suppression_frames(3, 5) == 0);
}

TEST_CASE("abilities: teleport delay = max(dist/48, 16) [RE: §C1 0x7187F3]") {
    AbilityRules r;
    CHECK(teleport_delay_frames(2560, r) == 53);   // 10 cells
    CHECK(teleport_delay_frames(500, r) == 16);    // floored by ChronoMinimumDelay
    CHECK(teleport_delay_frames(0, r) == 16);
    r.chrono_trigger = false;
    CHECK(teleport_delay_frames(2560, r) == 16);   // constant when the trigger is off
    r.chrono_trigger = true;
    r.chrono_range_minimum = 3000;
    CHECK(teleport_delay_frames(2560, r) == 16);   // inside the constant range
}

TEST_CASE("abilities: chrono erase budget is Strength*10 [RE: §C2 0x71A1A0]") {
    CHECK(warp_remaining_init(125) == 1250);
    // NeutronRifle Damage=8 per frame: a GI is erased on frame 157.
    int left = warp_remaining_init(125), frames = 0;
    while (left > 0) { left -= 8; ++frames; }
    CHECK(frames == 157);
}

TEST_CASE("abilities: C4 timer is ability_ftol(C4Delay*900) [RE: §D1 0x51A39E]") {
    CHECK(c4_delay_frames(0.03) == 27);
    CHECK(c4_delay_frames(0.0) == 0);
}

TEST_CASE("abilities: Ivan bomb detonates strictly after DetonationFrame [RE: §E 0x4389F0]") {
    CHECK_FALSE(bomb_due(550, 550));
    CHECK(bomb_due(550, 551));
    CHECK_FALSE(bomb_due(-1, 1000));   // no bomb
}

TEST_CASE("abilities: IFV weapon index [RE: §G 0x70D0A0]") {
    CHECK(ifv_weapon_index(2, 17) == 2);    // GI -> Weapon3=CRM60
    CHECK(ifv_weapon_index(9, 17) == 9);    // Desolator -> CRRadBeamWeapon
    CHECK(ifv_weapon_index(16, 17) == 16);
    CHECK(ifv_weapon_index(17, 17) == 0);   // no Weapon18 on FV
    CHECK(ifv_weapon_index(18, 40) == 0);   // >= 18 rejected outright
    CHECK(ifv_weapon_index(-1, 17) == 0);
}

TEST_CASE("abilities: area falloff 100% -> PercentAtMax [RE: combat.md §7]") {
    CHECK(area_scale(0.0, 512.0, 0.5) == doctest::Approx(1.0));
    CHECK(area_scale(256.0, 512.0, 0.5) == doctest::Approx(0.75));
    CHECK(area_scale(512.0, 512.0, 0.5) == doctest::Approx(0.5));
    CHECK(area_scale(513.0, 512.0, 0.5) < 0.0);
}
