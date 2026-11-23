// Rallypoint: the third side (Yuri). Synthetic values only (no game data).
#include "doctest.h"
#include "yuri_side.hpp"

using ra2engine::World;
namespace y = ra2engine::yuri;

TEST_CASE("yuri: rules Side= maps to the three sides") {
    CHECK(y::side_of_rules_side("GDI") == World::Side::Allied);
    CHECK(y::side_of_rules_side("Nod") == World::Side::Soviet);
    CHECK(y::side_of_rules_side("ThirdSide") == World::Side::Yuri);
    CHECK(y::side_of_rules_side("thirdside") == World::Side::Yuri);
    CHECK(y::side_of_rules_side("") == World::Side::Neutral);
    CHECK(y::side_of_rules_side("Civilian") == World::Side::Neutral);
}

TEST_CASE("yuri: per-side keys, planning side, trigger side, default country") {
    CHECK(std::string(y::key_prefix(World::Side::Yuri)) == "Third");
    CHECK(std::string(y::key_prefix(World::Side::Soviet)) == "Soviet");
    CHECK(std::string(y::key_prefix(World::Side::Allied)) == "Allied");
    CHECK(y::planning_side(World::Side::Allied) == 0);
    CHECK(y::planning_side(World::Side::Soviet) == 1);
    CHECK(y::planning_side(World::Side::Yuri) == 2);
    CHECK(y::trigger_side(World::Side::Allied) == 1);
    CHECK(y::trigger_side(World::Side::Soviet) == 2);
    CHECK(y::trigger_side(World::Side::Yuri) == 3);
    CHECK(std::string(y::default_country(World::Side::Yuri)) == "YuriCountry");
    CHECK(std::string(y::sides_key(World::Side::Yuri)) == "ThirdSide");
    CHECK(y::side_index(World::Side::Yuri) == static_cast<int>(World::Side::Yuri));
}

TEST_CASE("yuri: which types belong to the third side") {
    const std::string all = "British,French,Germans,Americans,Alliance,Russians,Confederation,Africans,Arabs,YuriCountry";
    const std::vector<std::string> none;
    // Owner names only YuriCountry: Yuri's (a conyard, an MCV, a unit).
    CHECK(y::is_yuri_type("YuriCountry", "", "", none));
    CHECK(y::is_yuri_type(" yuricountry ; comment", "", "", none));
    // Everyone may own it, but the AI plans it for side 2.
    CHECK(y::is_yuri_type(all, "2", "", none));
    CHECK_FALSE(y::is_yuri_type(all, "1", "", none));
    CHECK_FALSE(y::is_yuri_type(all, "", "", none));   // a shared Allied building
    // Shared owner list, but built from a Yuri structure.
    const std::vector<std::string> yb = {"XBRCK", "XRAD"};
    CHECK(y::is_yuri_type(all, "", "XBRCK,XRAD", yb));
    CHECK_FALSE(y::is_yuri_type(all, "", "XPILE", yb));
    // Not owned by YuriCountry at all.
    CHECK_FALSE(y::is_yuri_type("Russians", "2", "XBRCK", yb));
}

TEST_CASE("yuri: tokens trims, upper-cases and cuts the comment") {
    const std::vector<std::string> t = y::tokens(" a, bB ,,c ;d,e");
    REQUIRE(t.size() == 3);
    CHECK(t[0] == "A");
    CHECK(t[1] == "BB");
    CHECK(t[2] == "C");
}

TEST_CASE("yuri: launcher Country=9 is YuriCountry (index after the nine RA2 countries)") {
    // ra2launch::SIDES writes Country=0..9; 9 must be YuriCountry so Side=ThirdSide.
    CHECK(std::string(y::default_country(World::Side::Yuri)) == "YuriCountry");
    CHECK(y::side_of_rules_side("ThirdSide") == World::Side::Yuri);
    CHECK(y::planning_side(World::Side::Yuri) == 2);
}
