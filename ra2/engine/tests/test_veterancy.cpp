#include "doctest.h"
#include "veterancy.hpp"
#include "crate.hpp"
#include "asset_fixture.hpp"
#include "ra2assets/ini.hpp"
#include "ra2assets/rules.hpp"
using namespace ra2engine;

// Every pin below is recovered from YR-1.001 gamemd.exe / this install's
// rulesmd.ini -- docs/re/veterancy.md (§ refs inline).

TEST_CASE("veterancy: rank thresholds are float compares at 1.0f / 2.0f [V1]") {
    CHECK(vet_rank(0.0f) == 0);
    CHECK(vet_rank(0.99999994f) == 0);
    CHECK(vet_rank(1.0f) == 1);
    CHECK(vet_rank(1.9999999f) == 1);
    CHECK(vet_rank(2.0f) == 2);
    CHECK(vet_rank(-0.25f) == 0);       // negative "rookie" never ranks up
    CHECK(is_veteran(1.0f));
    CHECK_FALSE(is_veteran(2.0f));      // elite is NOT veteran (exclusive bands)
    CHECK(is_elite(2.0f));
    CHECK_FALSE(is_elite(1.9999999f));
}

TEST_CASE("veterancy: Add = victim/(owner*Ratio), capped on the double sum [V2]") {
    VeterancyRules r;   // install defaults: Ratio 3.0, Cap 2.0
    // E1 (Cost 200) kills an E2 (Cost 100): 100/600 per kill.
    float v = 0.0f;
    v = veterancy_add(v, 200, 100, r);
    CHECK(v == doctest::Approx(1.0 / 6.0).epsilon(1e-6));
    // Accumulate exactly the way the binary does (double add, float store).
    float w = 0.0f;
    for (int i = 0; i < 6; ++i) w = veterancy_add(w, 200, 100, r);
    // six float-stored sixths: the result is whatever the float chain gives --
    // the test pins the chain is reproducible, not a pretty number.
    float ref = 0.0f;
    for (int i = 0; i < 6; ++i) ref = (float)((double)100 / ((double)200 * 3.0) + (double)ref);
    CHECK(w == ref);
    // Cap: a huge victim value never exceeds VeteranCap.
    CHECK(veterancy_add(1.5f, 100, 100000, r) == 2.0f);
    // ownerCost 0 -> +inf -> capped (a free unit that kills goes elite).
    CHECK(veterancy_add(0.0f, 0, 100, r) == 2.0f);
    // A non-default rules value is honored (wiring proof).
    VeterancyRules r2; r2.ratio = 1.0;
    CHECK(veterancy_add(0.0f, 100, 100, r2) == 1.0f);
}

TEST_CASE("veterancy: kill value x2 veteran, x3 elite, 0 allied [V3]") {
    CHECK(kill_value(100, 0.0f, false) == 100);
    CHECK(kill_value(100, 1.0f, false) == 200);
    CHECK(kill_value(100, 2.0f, false) == 300);
    CHECK(kill_value(100, 2.0f, true) == 0);
}

TEST_CASE("veterancy: abilities parse + HasAbility inherits the veteran list at elite [V4]") {
    const AbilityMask v = parse_abilities("STRONGER,FIREPOWER,ROF,SIGHT,FASTER");
    const AbilityMask e = parse_abilities(" self_heal , STRONGER,FIREPOWER,ROF ;comment");
    CHECK(ability_bit(v, Ability::Stronger));
    CHECK(ability_bit(v, Ability::Faster));
    CHECK_FALSE(ability_bit(v, Ability::SelfHeal));
    CHECK(ability_bit(e, Ability::SelfHeal));
    CHECK(parse_abilities("BOGUS,,") == 0u);
    // rookie: nothing
    CHECK_FALSE(has_ability(0.5f, v, e, Ability::Stronger));
    // veteran: vet list only
    CHECK(has_ability(1.0f, v, e, Ability::Faster));
    CHECK_FALSE(has_ability(1.0f, v, e, Ability::SelfHeal));
    // elite: vet OR elite
    CHECK(has_ability(2.0f, v, e, Ability::Faster));
    CHECK(has_ability(2.0f, v, e, Ability::SelfHeal));
    // MTNK-like: ROF only at elite
    const AbilityMask mv = parse_abilities("STRONGER,FIREPOWER,SIGHT,FASTER");
    CHECK_FALSE(has_ability(1.0f, mv, e, Ability::Rof));
    CHECK(has_ability(2.0f, mv, e, Ability::Rof));
}

TEST_CASE("veterancy: fire / receive / ROF / speed integer chains [V5-V8]") {
    // V5: E1 M60 15 -> ftol(16.5) = 16; M60E 25 -> ftol(27.5000..04) = 27.
    CHECK(fire_damage(15, 1.0, true, 1.1) == 16);
    CHECK(fire_damage(25, 1.0, true, 1.1) == 27);
    CHECK(fire_damage(15, 1.0, false, 1.1) == 15);
    CHECK(fire_damage(15, 2.0, false, 1.1) == 30);   // Firepower crate x2
    CHECK(fire_damage(15, 2.0, true, 1.1) == 33);
    // V6: /ArmorMult, /VeteranArmor (STRONGER), then max(1).
    CHECK(receive_damage(15, 1.0, false, 1.5) == 15);
    CHECK(receive_damage(15, 1.0, true, 1.5) == 10);
    CHECK(receive_damage(16, 1.0, true, 1.5) == 10);   // ftol(10.67)
    CHECK(receive_damage(15, 1.5, true, 1.5) == 6);    // ftol(ftol(10)/1.5)=6
    CHECK(receive_damage(0, 1.0, false, 1.5) == 1);    // min-1 clamp
    CHECK(receive_damage(1, 1.0, true, 1.5) == 1);
    // V7: ROF delay x0.6, truncated. 5*0.6 in IEEE double == 3.0 (PC_53), so 3.
    CHECK(rof_frames(20, true, 0.6) == 12);
    CHECK(rof_frames(5, true, 0.6) == 3);
    CHECK(rof_frames(20, false, 0.6) == 20);
    CHECK(rof_frames(60, true, 0.6) == 36);
    // V8: E1 Speed 4 -> ftol(4.8) = 4; MTNK 7 -> 8; Speed crate 1.2 first.
    CHECK(speed_value(4, 1.0, true, 1.2) == 4);
    CHECK(speed_value(7, 1.0, true, 1.2) == 8);
    CHECK(speed_value(7, 1.2, false, 1.2) == 8);
    CHECK(speed_value(7, 1.2, true, 1.2) == 9);       // ftol(ftol(8.4)*1.2)=ftol(9.6)
    // V9: VeteranSight 0.0 -> dead branch.
    CHECK(sight_value(6, true, 0.0) == 6);
    CHECK(sight_value(6, true, 1.5) == 9);
}

TEST_CASE("veterancy: self-heal on the global ftol(RepairRate*900) phase [V11]") {
    CHECK(self_heal_interval(0.016) == 14);
    CHECK(should_self_heal(0, 14, true, 50, 100));
    CHECK(should_self_heal(28, 14, true, 50, 100));
    CHECK_FALSE(should_self_heal(27, 14, true, 50, 100));
    CHECK_FALSE(should_self_heal(14, 14, false, 50, 100));   // not eligible
    CHECK_FALSE(should_self_heal(14, 14, true, 100, 100));   // full
    CHECK_FALSE(should_self_heal(14, 14, true, 0, 100));     // dead
    CHECK_FALSE(should_self_heal(14, 0, true, 50, 100));     // interval 0 guarded
}

TEST_CASE("veterancy: the Veteran-crate level loop [C6]") {
    CHECK(promote_levels(0.3f, 1) == 1.0f);
    CHECK(promote_levels(1.4f, 1) == 2.0f);
    CHECK(promote_levels(2.0f, 1) == 2.0f);
    CHECK(promote_levels(-0.25f, 1) == 0.0f);
    CHECK(promote_levels(0.0f, 2) == 2.0f);
    CHECK(promote_levels(0.0f, 0) == 0.0f);
}

TEST_CASE("rules: section_string strips the inline comment and trims [P5]") {
    const ra2assets::Ini ini = ra2assets::Ini::parse(
        "[E1]\nVeteranAbilities=STRONGER,FIREPOWER ; gs note\nElitePrimary=  M60E  \n");
    const ra2assets::Rules r = ra2assets::Rules::load(ini);
    CHECK(r.section_string("E1", "VeteranAbilities", "") == "STRONGER,FIREPOWER");
    CHECK(r.section_string("E1", "ElitePrimary", "") == "M60E");
    CHECK(r.section_string("E1", "EliteAbilities", "none") == "none");
    CHECK(r.section_string("NOSUCH", "X", "fb") == "fb");
}

TEST_CASE("veterancy: the real rulesmd [General]/[CrateRules]/[Powerups] values [RE: §0]") {
    if (!ra2fixture::assets_available()) { MESSAGE("skipped: ra2md.mix not found"); return; }
    if (ra2fixture::rulesmd_bytes().empty()) { MESSAGE("skipped: rulesmd.ini not found"); return; }
    const ra2assets::Rules& rules = ra2fixture::rulesmd();
    CHECK(rules.general_double("VeteranRatio", -1) == doctest::Approx(3.0));
    CHECK(rules.general_double("VeteranCombat", -1) == doctest::Approx(1.1));
    CHECK(rules.general_double("VeteranSpeed", -1) == doctest::Approx(1.2));
    CHECK(rules.general_double("VeteranSight", -1) == doctest::Approx(0.0));
    CHECK(rules.general_double("VeteranArmor", -1) == doctest::Approx(1.5));
    CHECK(rules.general_double("VeteranROF", -1) == doctest::Approx(0.6));
    CHECK(rules.general_double("VeteranCap", -1) == doctest::Approx(2.0));
    CHECK(rules.general_double("EliteFlashTimer", -1) == doctest::Approx(150.0));  // [AudioVisual]
    CHECK(rules.section_double("CrateRules", "CrateRegen", -1) == doctest::Approx(3.0));
    CHECK(rules.section_double("CrateRules", "CrateRadius", -1) == doctest::Approx(3.0));
    CHECK(rules.section_double("CrateRules", "CrateMinimum", -1) == doctest::Approx(1.0));
    const auto m = ra2engine::parse_powerup(rules.section_string("Powerups", "Money", ""));
    CHECK(m.weight == 20);
    CHECK(m.arg == doctest::Approx(2000.0));
    const auto e1v = ra2engine::parse_abilities(rules.section_string("E1", "VeteranAbilities", ""));
    const auto e1e = ra2engine::parse_abilities(rules.section_string("E1", "EliteAbilities", ""));
    CHECK(ra2engine::ability_bit(e1v, ra2engine::Ability::Rof));
    CHECK(ra2engine::ability_bit(e1e, ra2engine::Ability::SelfHeal));
    CHECK(rules.section_string("E1", "ElitePrimary", "") == "M60E");
    CHECK_FALSE(rules.section_flag("DOG", "Trainable", true));
    CHECK(rules.section_flag("HARV", "SelfHealing", false));
    CHECK_FALSE(rules.section_flag("MTNK", "CrateGoodie", false));
}

TEST_CASE("veterancy: G-63 AssignHandicap FirePower/ROF scale the house's weapons") {
    // [Easy] ROF=.8 (the class a Brutal AI runs on): 30-frame weapon -> 24.
    CHECK(ra2engine::rof_frames(30, false, 0.75, 0.8) == 24);
    CHECK(ra2engine::rof_frames(30, false, 0.75) == 30);         // humans: 1.0
    CHECK(ra2engine::rof_frames(30, false, 0.75, 1.2) == 36);    // [Difficult]
    CHECK(ra2engine::fire_damage(100, 1.0, false, 1.1, 1.0) == 100);
    CHECK(ra2engine::fire_damage(100, 1.0, false, 1.1, 1.5) == 150);
}
