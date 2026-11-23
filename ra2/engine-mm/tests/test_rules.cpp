#include "doctest.h"
#include "ra2assets/rules.hpp"
#include "ra2assets/ini.hpp"
#include "ra2assets/mix.hpp"
#include "asset_fixture.hpp"
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
using namespace ra2assets;

TEST_CASE("rules: synthetic - explicit Image/Speed, unknown id falls back") {
    std::string text =
        "[GI]\n"
        "Image=GI\n"
        "Speed=4\n";
    Ini ini = Ini::parse(text);
    Rules rules = Rules::load(ini);

    CHECK(rules.image("GI") == "GI");
    CHECK(rules.speed("GI") == 4);

    // Unknown unit id: image falls back to the id itself; speed defaults to 0.
    CHECK(rules.image("NOSUCHUNIT") == "NOSUCHUNIT");
    CHECK(rules.speed("NOSUCHUNIT") == 0);

    // Voice getters: absent section/key -> "".
    CHECK(rules.voice_select("E9") == "");
}

TEST_CASE("rules: image() returns the explicit Image= value, not just the id") {
    // Real rulesmd.ini's actual GI unit id is "E1" (see the real-asset test
    // below) with Image=GI -- distinguish "image looks up the key" from
    // "image always equals the unit id" by using a section whose id differs
    // from its Image= value.
    std::string text =
        "[E1]\n"
        "Image=GI\n"
        "Speed=4\n";
    Ini ini = Ini::parse(text);
    Rules rules = Rules::load(ini);

    CHECK(rules.image("E1") == "GI");
    CHECK(rules.speed("E1") == 4);
}

TEST_CASE("rules: locates real rulesmd.ini from the install and reads GI (unit id E1)") {
    // Borrow the shared, load-once ra2md.mix + rulesmd.ini parse (asset_fixture)
    // instead of re-slurping the ~196 MB archive and re-parsing rulesmd per case.
    if (!ra2fixture::assets_available()) { MESSAGE("skipped: ra2md.mix not found"); return; }
    if (ra2fixture::rulesmd_bytes().empty()) {
        MESSAGE("skipped: rulesmd.ini not found in ra2md.mix top level or in "
                "localmd.mix/expandmd*.mix/cachemd.mix/conqmd.mix");
        return;
    }
    const Ini& ini = ra2fixture::rulesmd_ini();
    const Rules& rules = ra2fixture::rulesmd();

    // The real rulesmd.ini's rules-section id for the GI is "E1" -- "GI" is
    // only the unit's flavor Name= / Image= (SHP base name) value, not the
    // section id itself (confirmed by inspecting the extracted rulesmd.ini
    // directly: "[E1]" / "Name=GI" / "Image=GI" / "Speed=4"). So the
    // real-data assertion below is phrased against the real section id
    // rather than a literal "[GI]" section, which does not exist in stock
    // rulesmd.ini.
    REQUIRE(ini.has("E1"));
    CHECK(rules.image("E1") == "GI");
    CHECK(rules.speed("E1") > 0);
    MESSAGE("rulesmd.ini [E1] (GI) speed=" << rules.speed("E1"));
}

TEST_CASE("rules: combat getters read unit/weapon/warhead fields") {
    ra2assets::Ini ini = ra2assets::Ini::parse(
        "[E9]\nStrength=125\nArmor=flak\nPrimary=Zap\n"
        "[Zap]\nDamage=15\nROF=25\nRange=4\nWarhead=SA\nReport=ZapSound\n"
        "[SA]\nVerses=100%,80%,80%,50%,25%,25%,75%,50%,25%,100%,100%\n");
    ra2assets::Rules r = ra2assets::Rules::load(ini);
    CHECK(r.strength("E9") == 125);
    CHECK(r.armor("E9") == "flak");
    CHECK(r.primary("E9") == "Zap");
    CHECK(r.weapon_damage("Zap") == 15);
    CHECK(r.weapon_rof("Zap") == 25);
    CHECK(r.weapon_range("Zap") == doctest::Approx(4.0));
    CHECK(r.weapon_warhead("Zap") == "SA");
    CHECK(r.weapon_report("Zap") == "ZapSound");
    auto v = r.verses("SA");
    REQUIRE(v.size() == 11);
    CHECK(v[0] == 100); CHECK(v[1] == 80); CHECK(v[4] == 25); CHECK(v[6] == 75);
    // Absent sections/keys fall back safely.
    CHECK(r.strength("NOPE") == 0);
    CHECK(r.armor("NOPE") == "none");
    CHECK(r.primary("NOPE") == "");
    CHECK(r.verses("NOPE").empty());
}

TEST_CASE("rules: verses parsing tolerates spaces and missing % signs") {
    ra2assets::Ini ini = ra2assets::Ini::parse("[W]\nVerses= 100% , 80 ,70%\n");
    ra2assets::Rules r = ra2assets::Rules::load(ini);
    auto v = r.verses("W");
    REQUIRE(v.size() == 3);
    CHECK(v[0] == 100); CHECK(v[1] == 80); CHECK(v[2] == 70);
}

TEST_CASE("rules: vehicle getters") {
    ra2assets::Ini ini = ra2assets::Ini::parse(
        "[HTNK]\nROT=5\nTurret=yes\nExplosion=TWLT070,S_BANG48\n"
        "[UTNK]\nTurret=no\n"
        "[120mm]\nRange=5.75\n");
    ra2assets::Rules r = ra2assets::Rules::load(ini);
    CHECK(r.rot("HTNK") == 5);
    CHECK(r.rot("NOPE") == 0);
    CHECK(r.has_turret("HTNK"));
    CHECK_FALSE(r.has_turret("UTNK"));
    CHECK_FALSE(r.has_turret("NOPE"));
    CHECK(r.explosion_first("HTNK") == "TWLT070");
    CHECK(r.explosion_first("NOPE") == "");
    CHECK(r.weapon_range("120mm") == doctest::Approx(5.75));
}

TEST_CASE("rules: real rulesmd combat values pin (E1/E2/M60/M1Carbine/SA)") {
    if (!ra2fixture::assets_available()) { MESSAGE("skipped: ra2md.mix not found"); return; }
    if (ra2fixture::rulesmd_bytes().empty()) {
        MESSAGE("skipped: rulesmd.ini not found in ra2md.mix top level or in "
                "localmd.mix/expandmd*.mix/cachemd.mix/conqmd.mix");
        return;
    }
    const Rules& rules = ra2fixture::rulesmd();

    // E1 (GI) combat values
    CHECK(rules.strength("E1") == 125);
    CHECK(rules.armor("E1") == "none");
    CHECK(rules.primary("E1") == "M60");
    CHECK(rules.image("E1") == "GI");
    CHECK(rules.voice_select("E1") == "GISelect");
    CHECK(rules.voice_move("E1") == "GIMove");
    CHECK(rules.voice_attack("E1") == "GIAttackCommand");
    CHECK(rules.die_sound("E1") == "GIDie");

    // E2 (CONS) combat values
    CHECK(rules.strength("E2") == 125);
    CHECK(rules.armor("E2") == "flak");
    CHECK(rules.primary("E2") == "M1Carbine");
    CHECK(rules.image("E2") == "CONS");
    CHECK(rules.voice_select("E2") == "ConscriptSelect");
    CHECK(rules.die_sound("E2") == "ConscriptDie");

    // M60 weapon values
    CHECK(rules.weapon_damage("M60") == 15);
    CHECK(rules.weapon_rof("M60") == 20);
    CHECK(rules.weapon_range("M60") == doctest::Approx(4.0));
    CHECK(rules.weapon_warhead("M60") == "SA");

    // M1Carbine weapon values
    CHECK(rules.weapon_damage("M1Carbine") == 15);
    CHECK(rules.weapon_rof("M1Carbine") == 25);
    CHECK(rules.weapon_range("M1Carbine") == doctest::Approx(4.0));
    CHECK(rules.weapon_warhead("M1Carbine") == "SA");

    // Vehicle getters (HTNK) + 120mm weapon range pin
    CHECK(rules.rot("HTNK") == 5);
    CHECK(rules.has_turret("HTNK"));
    CHECK(rules.explosion_first("HTNK") == "TWLT070");
    CHECK(rules.weapon_range("120mm") == doctest::Approx(5.75));

    // SA warhead verses (armor effectiveness multipliers)
    auto sa_verses = rules.verses("SA");
    REQUIRE(sa_verses.size() == 11);
    std::vector<int> expected_verses{100, 80, 80, 50, 25, 25, 75, 50, 25, 100, 100};
    CHECK(sa_verses == expected_verses);
}

TEST_CASE("rules: building accessors (synthetic)") {
    ra2assets::Ini ini = ra2assets::Ini::parse(
        "[General]\nBuildSpeed=.7\nRefundPercent=50%\nPrerequisitePower=GAPOWR,NAPOWR\n"
        "[MultiplayerDialogSettings]\nMoney=10000\n"
        "[GAPOWR]\nCost=800\nPower=200\nAdjacent=2\nTechLevel=1\nPrerequisite=GACNST\n"
        "[GACNST]\nConstructionYard=yes\nCost=3000\n"
        "[GAPILE]\nPrerequisite= power , GACNST\n");
    ra2assets::Rules r = ra2assets::Rules::load(ini);
    CHECK(r.cost("GAPOWR") == 800);
    CHECK(r.power("GAPOWR") == 200);
    CHECK(r.power("MISSING") == 0);
    CHECK(r.adjacent("GAPOWR") == 2);
    CHECK(r.tech_level("GAPOWR") == 1);
    CHECK(r.is_construction_yard("GACNST"));
    CHECK_FALSE(r.is_construction_yard("GAPOWR"));
    auto pre = r.prerequisite("GAPILE");
    REQUIRE(pre.size() == 2);
    CHECK(pre[0] == "POWER");   // trimmed + uppercased
    CHECK(pre[1] == "GACNST");
    auto grp = r.prerequisite_group("Power");
    REQUIRE(grp.size() == 2);
    CHECK(grp[0] == "GAPOWR");
    CHECK(r.general_double("BuildSpeed", 0.0) == doctest::Approx(0.7));
    CHECK(r.general_double("RefundPercent", 0.0) == doctest::Approx(50.0));
    CHECK(r.general_double("Absent", 3.5) == doctest::Approx(3.5));
    CHECK(r.mp_money() == 10000);
}

TEST_CASE("rules: economy accessors") {
    ra2assets::Ini ini = ra2assets::Ini::parse(
        "[General]\nGrowthRate=5\n"
        "[Riparius]\nValue=25\nGrowthPercentage=.06\nSpreadPercentage=.06\n"
        "[HARV]\nStorage=40\nHarvester=yes\nDock=NAREFN,GAREFN\nUnloadingClass=HORV\n"
        "[NAREFN]\nStorage=200\nFreeUnit=HARV\nNumberOfDocks=1\n");
    auto r = ra2assets::Rules::load(ini);
    CHECK(r.value("Riparius") == 25);
    CHECK(r.growth_percentage("Riparius") == doctest::Approx(0.06));
    CHECK(r.spread_percentage("Riparius") == doctest::Approx(0.06));
    CHECK(r.storage("HARV") == 40);
    CHECK(r.harvester("HARV"));
    CHECK_FALSE(r.harvester("NAREFN"));
    CHECK(r.dock("HARV") == std::vector<std::string>{"NAREFN", "GAREFN"});
    CHECK(r.free_unit("NAREFN") == "HARV");
    CHECK(r.free_unit("HARV") == "");
    CHECK(r.number_of_docks("NAREFN") == 1);
    CHECK(r.growth_rate_minutes() == doctest::Approx(5.0));
    CHECK(r.unloading_class("HARV") == "HORV");
}

TEST_CASE("rules: real rulesmd building pins") {
    if (!ra2fixture::assets_available()) { MESSAGE("skipped: ra2md.mix not found"); return; }
    if (ra2fixture::rulesmd_bytes().empty()) {
        MESSAGE("skipped: rulesmd.ini not found in ra2md.mix top level or in "
                "localmd.mix/expandmd*.mix/cachemd.mix/conqmd.mix");
        return;
    }
    const Rules& rules = ra2fixture::rulesmd();

    CHECK(rules.cost("GAPOWR") == 800);
    CHECK(rules.cost("NAPOWR") == 600);
    CHECK(rules.cost("GAPILE") == 500);
    CHECK(rules.cost("GAWEAP") == 2000);
    CHECK(rules.cost("GACNST") == 3000);

    CHECK(rules.power("GAPOWR") == 200);
    CHECK(rules.power("NAPOWR") == 150);
    CHECK(rules.power("GAWEAP") == -25);
    CHECK(rules.power("NAHAND") == -10);
    CHECK(rules.power("GACNST") == 0);

    CHECK(rules.adjacent("GACNST") == 2);
    CHECK(rules.tech_level("GACNST") == -1);

    CHECK(rules.is_construction_yard("GACNST"));
    CHECK(rules.is_construction_yard("NACNST"));
    CHECK_FALSE(rules.is_construction_yard("GAPOWR"));

    auto gaweap_pre = rules.prerequisite("GAWEAP");
    std::vector<std::string> expected_pre{"PROC", "GAPILE", "GACNST"};
    CHECK(gaweap_pre == expected_pre);

    auto proc_group = rules.prerequisite_group("Proc");
    REQUIRE_FALSE(proc_group.empty());
    CHECK(proc_group[0] == "GAREFN");

    CHECK(rules.general_double("BuildSpeed", 0.0) == doctest::Approx(0.7));
    CHECK(rules.general_double("RefundPercent", 0.0) == doctest::Approx(50.0));
    CHECK(rules.general_double("LowPowerPenaltyModifier", 0.0) == doctest::Approx(1.0));
    CHECK(rules.general_double("ConditionYellow", 0.0) == doctest::Approx(50.0));
    CHECK(rules.general_double("ConditionRed", 0.0) == doctest::Approx(25.0));

    CHECK(rules.mp_money() == 10000);
}

TEST_CASE("rules: build_category / is_infantry / is_vehicle classification") {
    ra2assets::Ini ini = ra2assets::Ini::parse(
        "[InfantryTypes]\n0=E1\n1=E2\n"
        "[VehicleTypes]\n0=MTNK\n1=HARV\n2=DEST\n"
        "[BuildingTypes]\n0=GAPOWR\n1=GAWEAP\n2=GAPILL\n3=GATOW\n"
        "[E1]\nName=GI\n"
        "[MTNK]\nName=Grizzly\n"
        "[DEST]\nName=Destroyer\nNaval=yes\n"
        "[GAPOWR]\nName=Power Plant\nBuildCat=Power\n"
        "[GAWEAP]\nName=War Factory\n"
        "[GAPILL]\nName=Pillbox\nBuildCat=Combat\n"
        "[GATOW]\nName=Patriot\nBuildCat=combat\n");
    auto r = ra2assets::Rules::load(ini);
    using BC = ra2assets::Rules::BuildCategory;
    CHECK(r.build_category("E1") == BC::Infantry);
    CHECK(r.build_category("MTNK") == BC::Vehicle);
    CHECK(r.build_category("HARV") == BC::Vehicle);
    CHECK(r.build_category("DEST") == BC::None);   // Naval=yes -> Ship queue, out of scope [§1.1]
    CHECK(r.build_category("GAPOWR") == BC::Structure);  // BuildCat=Power -> Structure
    CHECK(r.build_category("GAWEAP") == BC::Structure);  // no BuildCat -> Structure
    CHECK(r.build_category("GAPILL") == BC::Defense);    // BuildCat=Combat -> Defense
    CHECK(r.build_category("GATOW") == BC::Defense);     // case-insensitive value
    CHECK(r.build_category("AMCV") == BC::None);   // in no list section
    CHECK(r.is_infantry("E1"));
    CHECK_FALSE(r.is_infantry("MTNK"));
    CHECK(r.is_vehicle("MTNK"));
    CHECK_FALSE(r.is_vehicle("E1"));
    CHECK(r.is_buildcat_combat("GAPILL"));
    CHECK_FALSE(r.is_buildcat_combat("GAWEAP"));
    CHECK_FALSE(r.is_buildcat_combat("GAPOWR"));
}

TEST_CASE("rules: real rulesmd classification pins") {
    if (!ra2fixture::assets_available()) { MESSAGE("skipped: ra2md.mix not found"); return; }
    if (ra2fixture::rulesmd_bytes().empty()) {
        MESSAGE("skipped: rulesmd.ini not found in ra2md.mix top level or in "
                "localmd.mix/expandmd*.mix/cachemd.mix/conqmd.mix");
        return;
    }
    const Rules& rules = ra2fixture::rulesmd();

    using BC = ra2assets::Rules::BuildCategory;
    CHECK(rules.build_category("E1") == BC::Infantry);     // GI
    CHECK(rules.build_category("MTNK") == BC::Vehicle);    // Grizzly
    // [RE-GATE APPLIED: production.md §1.1] every defense has BuildCat=Combat:
    for (const char* d : {"GAPILL", "GTGCAN", "NALASR", "ATESLA", "NASAM", "GAWALL"})
        CHECK(rules.build_category(d) == BC::Defense);
    // ...and the non-Combat structures do NOT (BuildCat Power/Resource/Tech/None):
    for (const char* s : {"GAPOWR", "GAREFN", "GATECH", "GACNST", "GAWEAP"})
        CHECK(rules.build_category(s) == BC::Structure);
    // Real naval vehicles store Naval=yes with an inline comment ("Naval=yes ;GS"):
    // they must route to the Ships queue (out of scope this phase) -> None, not
    // Vehicle [RE-GATE APPLIED: production.md §1.1].
    for (const char* n : {"DEST", "DLPH"})
        CHECK(rules.build_category(n) == BC::None);
}

TEST_CASE("rules: section_double reads the [AI] and difficulty sections") {
    ra2assets::Ini ini = ra2assets::Ini::parse(
        "[AI]\nPowerSurplus=50\nBaseSizeAdd=3\nPowerEmergency=75%\nRefineryRatio=.16\n"
        "[Easy]\nCost=1.2\nBuildTime=1.2\n"
        "[Difficult]\nCost=0.8\n");
    auto r = ra2assets::Rules::load(ini);
    CHECK(r.section_double("AI", "PowerSurplus", 0.0) == doctest::Approx(50.0));
    CHECK(r.section_double("AI", "BaseSizeAdd", 0.0) == doctest::Approx(3.0));
    // Tolerant parse: '%' stops strtod (75, normalized by the consumer) and
    // a bare leading '.' parses ([RE: ai-base.md §2 — real key spellings]).
    CHECK(r.section_double("AI", "PowerEmergency", 0.0) == doctest::Approx(75.0));
    CHECK(r.section_double("AI", "RefineryRatio", 0.0) == doctest::Approx(0.16));
    CHECK(r.section_double("Easy", "Cost", 1.0) == doctest::Approx(1.2));
    CHECK(r.section_double("Difficult", "Cost", 1.0) == doctest::Approx(0.8));
    CHECK(r.section_double("Easy", "Missing", 7.0) == doctest::Approx(7.0));   // fallback
    CHECK(r.section_double("NoSection", "Cost", 3.0) == doctest::Approx(3.0)); // fallback
}

TEST_CASE("rules: Sight= read in cells, hard-clamped to 10 [RE: shroud.md §2]") {
    // Gated behind the real-rulesmd fixture (same guard the install-backed cases
    // above use) so it skips cleanly without the install.
    if (!ra2fixture::assets_available()) { MESSAGE("skipped: ra2md.mix not found"); return; }
    if (ra2fixture::rulesmd_bytes().empty()) { MESSAGE("skipped: rulesmd.ini not found"); return; }
    const ra2assets::Rules& r = ra2fixture::rulesmd();
    CHECK(r.sight("E1") == 5);       // GI
    CHECK(r.sight("MTNK") == 8);     // Grizzly
    CHECK(r.sight("DOG") == 9);      // Attack Dog
    CHECK(r.sight("NARADR") == 10);  // Soviet Radar Tower (exactly the cap)
    CHECK(r.sight("NAPSIS") == 10);  // Psychic Sensor (Sight=10)
    CHECK(r.sight("NONEXISTENT_ID_XYZ") == 0);  // absent -> 0
}


TEST_CASE("rules: section_flag honours per-key fallbacks [RE: repair-sell.md §A4]") {
    // Synthetic: the point of section_flag is that the ABSENT-KEY default is the
    // CALLER's, because the three Repair & Sell flags disagree about it
    // (Unsellable -> false, Crewed -> false, ClickRepairable -> TRUE).
    const std::string text =
        "[GACNST]\n"
        "Cost=3000\n"
        "Strength=1000\n"
        "Crewed=yes\n"
        "[GAPOWR]\n"
        "Cost=800\n"
        "Strength=750\n"
        "Crewed=no\n"
        "ClickRepairable=no\n"
        "[GAREFN]\n"
        "Crewed=TRUE\n"
        "Unsellable=Yes\n";
    const ra2assets::Ini ini = ra2assets::Ini::parse(text);
    const ra2assets::Rules r = ra2assets::Rules::load(ini);
    CHECK(r.section_flag("GACNST", "Crewed", false));
    CHECK_FALSE(r.section_flag("GAPOWR", "Crewed", false));
    // Case-insensitive yes/true, same tolerant parse the dedicated flag readers use.
    CHECK(r.section_flag("GAREFN", "Crewed", false));
    CHECK(r.section_flag("GAREFN", "Unsellable", false));
    // absent key -> the CALLER's fallback, which differs per key
    CHECK(r.section_flag("GACNST", "ClickRepairable", true));  // absent -> default YES
    CHECK_FALSE(r.section_flag("GAPOWR", "ClickRepairable", true));  // explicit no wins
    CHECK_FALSE(r.section_flag("GACNST", "Unsellable", false));
    // absent SECTION -> the caller's fallback too (GATOW has no rulesmd section)
    CHECK_FALSE(r.section_flag("NOSUCH", "Crewed", false));
    CHECK(r.section_flag("NOSUCH", "ClickRepairable", true));
    CHECK_FALSE(r.section_flag("NOSUCH", "Unsellable", false));
}

TEST_CASE("rules: general_string strips the inline comment [RE: repair-sell.md §D1]") {
    // The real rulesmd line shape: an id, padding, then a ';' comment.
    const std::string text =
        "[General]\n"
        "AlliedCrew=E1           ; soldier that emerges from destroyed unit or building\n"
        "SovietCrew=E2\n"
        "ThirdCrew=\n";
    const ra2assets::Ini ini = ra2assets::Ini::parse(text);
    const ra2assets::Rules r = ra2assets::Rules::load(ini);
    CHECK(r.general_string("AlliedCrew", "X") == "E1");
    CHECK(r.general_string("SovietCrew", "X") == "E2");
    CHECK(r.general_string("ThirdCrew", "INIT") == "INIT");   // empty -> fallback
    CHECK(r.general_string("NoSuchKey", "E1") == "E1");       // absent -> fallback
}

TEST_CASE("rules: the real rulesmd repair/sell scalars [RE: repair-sell.md §Ground truth]") {
    if (!ra2fixture::assets_available()) { MESSAGE("skipped: ra2md.mix not found"); return; }
    if (ra2fixture::rulesmd_bytes().empty()) { MESSAGE("skipped: rulesmd.ini not found"); return; }
    const ra2assets::Rules& rules = ra2fixture::rulesmd();

    // [General] scalars -- the whole phase's arithmetic is downstream of these.
    // strtod stops at '%', so "15%"/"50%" read back as 15 / 50.
    CHECK(rules.general_double("RepairStep", -1.0) == doctest::Approx(8.0));
    CHECK(rules.general_double("RepairPercent", -1.0) == doctest::Approx(15.0));
    CHECK(rules.general_double("RepairRate", -1.0) == doctest::Approx(0.016));
    CHECK(rules.general_double("RefundPercent", -1.0) == doctest::Approx(50.0));
    CHECK(rules.general_double("AlliedSurvivorDivisor", -1.0) == doctest::Approx(500.0));
    CHECK(rules.general_double("SovietSurvivorDivisor", -1.0) == doctest::Approx(250.0));
    CHECK(rules.general_double("ThirdSurvivorDivisor", -1.0) == doctest::Approx(750.0));
    // URepairStep / URepairPercent DO NOT EXIST in this build (§Ground truth) --
    // there is exactly ONE RepairStep/RepairPercent shared by buildings and
    // vehicles. (URepairRate DOES exist; it is a rate, not a step, and is out of
    // scope for buildings.) The fallback must survive untouched.
    CHECK(rules.general_double("URepairStep", -1.0) == doctest::Approx(-1.0));
    CHECK(rules.general_double("URepairPercent", -1.0) == doctest::Approx(-1.0));

    // Per-type flags [RE-GATE: repair-sell.md §D item 9 -- use the MEASURED row].
    // GAPOWR is Crewed=YES / Strength=750; the hunt's "Crewed=no, Strength=1000"
    // was overturned by §D0.1, and this case is the pin that keeps it overturned.
    CHECK(rules.section_flag("GACNST", "Crewed", false));
    CHECK(rules.section_flag("GAPOWR", "Crewed", false));
    CHECK(rules.strength("GAPOWR") == 750);
    CHECK_FALSE(rules.section_flag("GAPILL", "Crewed", false));  // defenses: Crewed=no
    CHECK_FALSE(rules.section_flag("NAWALL", "Crewed", false));  // absent -> false
    CHECK_FALSE(rules.section_flag("GATOW", "Crewed", false));   // no section at all

    // Soylent [§B1, RE-GATE §D0.2]: the refineries DO set it, so the absolute
    // refund override is load-bearing -- GAREFN refunds 300, NOT 50% x 2000.
    CHECK(rules.section_double("GAREFN", "Soylent", 0.0) == doctest::Approx(300.0));
    CHECK(rules.section_double("NAREFN", "Soylent", 0.0) == doctest::Approx(300.0));
    CHECK(rules.section_double("GACNST", "Soylent", 0.0) == doctest::Approx(0.0));
    CHECK(rules.section_double("GATOW", "Soylent", 0.0) == doctest::Approx(0.0));

    // Unsellable [§B5, RE-GATE §D0.3]: CAOILD DOES set it -- the sell refusal is
    // reachable on the roster, not dead code.
    CHECK(rules.section_flag("CAOILD", "Unsellable", false));
    CHECK_FALSE(rules.section_flag("GACNST", "Unsellable", false));
    CHECK_FALSE(rules.section_flag("GAREFN", "Unsellable", false));

    // ClickRepairable [§A4]: absent from every rostered building EXCEPT NABNKR,
    // which sets it explicitly to yes. No rostered building sets it to `no`, so
    // the recovered default of YES is what makes the wrench work at all.
    CHECK(rules.section_flag("GACNST", "ClickRepairable", true));
    CHECK(rules.section_flag("NABNKR", "ClickRepairable", true));  // explicit yes
    CHECK(rules.section_flag("GATOW", "ClickRepairable", true));   // no section -> yes
}

TEST_CASE("rules: section_flag follows the game's first-char Get_Bool (RC-5)") {
    const ra2assets::Ini ini = ra2assets::Ini::parse(
        "[X]\nA=Yes;\nB=true\nC=1\nD=No ;c\nE=false\nF=maybe\nG=Y\n");
    const ra2assets::Rules r = ra2assets::Rules::load(ini);
    CHECK(r.section_flag("X", "A", false));
    CHECK(r.section_flag("X", "B", false));
    CHECK(r.section_flag("X", "C", false));
    CHECK_FALSE(r.section_flag("X", "D", true));
    CHECK_FALSE(r.section_flag("X", "E", true));
    CHECK(r.section_flag("X", "F", true));          // unparseable -> default
    CHECK_FALSE(r.section_flag("X", "F", false));
    CHECK(r.section_flag("X", "G", false));
    CHECK(r.section_flag("X", "Missing", true));
}
