// Rallypoint: HouseType country bonuses (FUN_00511850). Synthetic INI only.
#include "doctest.h"
#include "country_bonuses.hpp"
#include "ra2assets/ini.hpp"
#include "ra2assets/rules.hpp"
#include "target_w6.hpp"

using ra2engine::UnitCombat;
namespace c = ra2engine::country;

TEST_CASE("country: CostUnitsMult and Cost= scale vehicle cost") {
    // Stock rulesmd comments CostUnitsMult=.85 under [Americans]; the loader
    // still reads it when present (gamemd 0x511b92).
    ra2assets::Ini ini = ra2assets::Ini::parse(
        "[Americans]\nCost=1.0\nCostUnitsMult=.85\n"
        "[French]\nCost=1.1\nCostInfantryMult=.8\n");
    auto rules = ra2assets::Rules::load(ini);
    const c::Bonuses us = c::parse(rules, "Americans");
    CHECK(us.cost_units == doctest::Approx(0.85));
    CHECK(c::cost_mul(us, c::Kind::Unit) == doctest::Approx(0.85));
    CHECK(c::scale_cost(1000, us, c::Kind::Unit) == 850);
    CHECK(c::scale_cost(1000, us, c::Kind::Infantry) == 1000);  // no infantry mult

    const c::Bonuses fr = c::parse(rules, "French");
    CHECK(c::cost_mul(fr, c::Kind::Infantry) == doctest::Approx(0.88));  // 1.1 * 0.8
    CHECK(c::scale_cost(500, fr, c::Kind::Infantry) == 440);
}

TEST_CASE("country: VeteranInfantry arms combat at spawn") {
    ra2assets::Ini ini = ra2assets::Ini::parse(
        "[British]\nVeteranInfantry=SNIPE,GHOST\nVeteranUnits=MTNK\n"
        "[Germans]\nVeteranUnits=TNKD\n");
    auto rules = ra2assets::Rules::load(ini);
    const c::Bonuses uk = c::parse(rules, "British");
    REQUIRE(uk.veteran_infantry.size() == 2);
    CHECK(uk.veteran_infantry[0] == "SNIPE");
    CHECK(c::starts_veteran(uk, "SNIPE", c::Kind::Infantry));
    CHECK(c::starts_veteran(uk, "MTNK", c::Kind::Unit));
    CHECK_FALSE(c::starts_veteran(uk, "E1", c::Kind::Infantry));

    UnitCombat sniper;
    sniper.is_infantry = true;
    sniper.trainable = true;
    sniper.tg.rules_id = "SNIPE";
    sniper.strength = 125;
    sniper.speed_cps = 4.0;
    c::apply_to_combat(sniper, uk);
    CHECK(sniper.veteran_rank == 1);

    UnitCombat gi;
    gi.is_infantry = true;
    gi.trainable = true;
    gi.tg.rules_id = "E1";
    c::apply_to_combat(gi, uk);
    CHECK(gi.veteran_rank == 0);

    const c::Bonuses de = c::parse(rules, "Germans");
    UnitCombat tnkd;
    tnkd.is_vehicle = true;
    tnkd.trainable = true;
    tnkd.tg.rules_id = "TNKD";
    c::apply_to_combat(tnkd, de);
    CHECK(tnkd.veteran_rank == 1);
}

TEST_CASE("country: Owner/RequiredHouses/ForbiddenHouses for country uniques") {
    ra2assets::Ini ini = ra2assets::Ini::parse(
        "[TNKD]\nOwner=British,French,Germans,Americans,Alliance\nRequiredHouses=Germans\n"
        "[SNIPE]\nOwner=British,French,Germans,Americans,Alliance\nRequiredHouses=British\n"
        "[GAAIRC]\nOwner=British,French,Germans,Americans,Alliance\nForbiddenHouses=Americans\n"
        "[AMRADR]\nOwner=British,French,Germans,Americans,Alliance\nRequiredHouses=Americans\n");
    auto rules = ra2assets::Rules::load(ini);
    CHECK(c::country_allows(rules, "TNKD", "Germans"));
    CHECK_FALSE(c::country_allows(rules, "TNKD", "British"));
    CHECK(c::country_allows(rules, "SNIPE", "British"));
    CHECK_FALSE(c::country_allows(rules, "SNIPE", "Americans"));
    CHECK_FALSE(c::country_allows(rules, "GAAIRC", "Americans"));
    CHECK(c::country_allows(rules, "GAAIRC", "French"));
    CHECK(c::country_allows(rules, "AMRADR", "Americans"));
    CHECK_FALSE(c::country_allows(rules, "AMRADR", "French"));
}

// T2.2: stock rulesmd RequiredHouses / ForbiddenHouses for all 10 countries
// (keys copied from a live YR rulesmd.ini; Owner lists truncated to the side).
TEST_CASE("country: stock rulesmd uniques for all 10 countries") {
    const char* kAllied = "British,French,Germans,Americans,Alliance";
    const char* kSoviet = "Russians,Confederation,Africans,Arabs";
    const char* kDogs =
        "Russians,Confederation,Africans,Arabs,YuriCountry,British,French,Germans,Americans,Alliance";
    ra2assets::Ini ini = ra2assets::Ini::parse(std::string() +
        "[AMRADR]\nOwner=" + kAllied + "\nRequiredHouses=Americans\n"
        "[GAAIRC]\nOwner=" + kAllied + "\nForbiddenHouses=Americans\n"
        "[ORCA]\nOwner=British,French,Germans,Americans\nForbiddenHouses=Alliance\n"
        "[BEAG]\nOwner=" + kAllied + "\nRequiredHouses=Alliance\n"
        "[GTGCAN]\nOwner=" + kAllied + "\nRequiredHouses=French\n"
        "[SNIPE]\nOwner=" + kAllied + "\nRequiredHouses=British\n"
        "[TNKD]\nOwner=" + kAllied + "\nRequiredHouses=Germans\n"
        "[DESO]\nOwner=" + kSoviet + "\nRequiredHouses=Arabs\n"
        "[DTRUCK]\nOwner=" + kSoviet + "\nRequiredHouses=Africans\n"
        "[TERROR]\nOwner=" + kSoviet + "\nRequiredHouses=Confederation\n"
        "[TTNK]\nOwner=" + kSoviet + "\nRequiredHouses=Russians\n"
        "[YADOG]\nOwner=" + kDogs + "\nRequiredHouses=YuriCountry\n"
        "[YDOG]\nOwner=" + kDogs + "\nRequiredHouses=YuriCountry\n"
        "[ADOG]\nOwner=" + kDogs + "\nForbiddenHouses=Russians,Confederation,Africans,Arabs,YuriCountry\n"
        "[DOG]\nOwner=" + kDogs +
        "\nForbiddenHouses=British,French,Germans,Americans,Alliance,YuriCountry\n");
    auto rules = ra2assets::Rules::load(ini);

    struct Row {
        const char* id;
        const char* only;
    };
    const Row kMust[] = {
        {"AMRADR", "Americans"}, {"BEAG", "Alliance"}, {"GTGCAN", "French"},
        {"SNIPE", "British"},    {"TNKD", "Germans"},  {"DESO", "Arabs"},
        {"DTRUCK", "Africans"},  {"TERROR", "Confederation"},
        {"TTNK", "Russians"},    {"YADOG", "YuriCountry"}, {"YDOG", "YuriCountry"},
    };
    const char* kAll10[] = {"Americans", "Alliance", "French", "Germans", "British",
                            "Africans", "Arabs", "Confederation", "Russians", "YuriCountry"};
    for (const Row& r : kMust) {
        for (const char* c : kAll10) {
            const bool want = std::string(c) == r.only;
            CHECK(c::country_allows(rules, r.id, c) == want);
        }
    }
    CHECK(c::country_allows(rules, "GAAIRC", "French"));
    CHECK_FALSE(c::country_allows(rules, "GAAIRC", "Americans"));
    CHECK(c::country_allows(rules, "ORCA", "Americans"));
    CHECK_FALSE(c::country_allows(rules, "ORCA", "Alliance"));
    CHECK(c::country_allows(rules, "ADOG", "Americans"));
    CHECK_FALSE(c::country_allows(rules, "ADOG", "Russians"));
    CHECK(c::country_allows(rules, "DOG", "Russians"));
    CHECK_FALSE(c::country_allows(rules, "DOG", "Americans"));
    CHECK_FALSE(c::country_allows(rules, "DOG", "YuriCountry"));
}
