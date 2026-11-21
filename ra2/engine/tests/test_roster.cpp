#include "doctest.h"
#include "ra2assets/ini.hpp"
#include "ra2assets/rules.hpp"

// The 4 new buildings' recovered stat blocks [RE: roster.md §2], authored as a
// minimal rulesmd fragment. Asserting through ra2assets::Rules proves our
// recovered constants round-trip through the exact parser the building resolve
// loop (main.cpp:1510-1591) feeds from. (The full-install byte-identity of the
// resolved kRoster is covered by --demo-roster + the 17-gate re-verify.)
static const char* kRosterBuildingsIni = R"INI(
[NANRCT]
Prerequisite=NATECH,NACNST
TechLevel=9
Cost=1000
Strength=1000
Armor=concrete
Sight=5
Power=2000

[ATESLA]
Image=GAPRIS
Prerequisite=POWER,RADAR,GACNST
TechLevel=6
Cost=1500
Strength=600
Armor=steel
Sight=8
Power=-75
Primary=PrismShot

[NASAM]
Prerequisite=BARRACKS,GACNST
TechLevel=4
Cost=1000
Strength=900
Armor=steel
Sight=10
Power=-50
Primary=RedEye2

[GAAIRC]
Prerequisite=GAREFN,GACNST
TechLevel=3
Cost=1000
Strength=600
Armor=steel
Sight=5
Power=-50

[PrismShot]
Damage=120
ROF=45
Range=8
Warhead=PrismWarhead

[RedEye2]
Damage=75
ROF=55
Range=12
Warhead=SAMWH
)INI";

TEST_CASE("roster: 4 new buildings resolve recovered stats [RE: roster.md §2]") {
    ra2assets::Ini ini = ra2assets::Ini::parse(kRosterBuildingsIni);
    ra2assets::Rules rules = ra2assets::Rules::load(ini);

    // NANRCT — Soviet Nuclear Reactor (huge power output; generic power path).
    CHECK(rules.cost("NANRCT") == 1000);
    CHECK(rules.tech_level("NANRCT") == 9);
    CHECK(rules.strength("NANRCT") == 1000);
    CHECK(rules.power("NANRCT") == 2000);
    CHECK(rules.prerequisite("NANRCT") == std::vector<std::string>{"NATECH", "NACNST"});

    // ATESLA — Allied Prism Tower (Image=GAPRIS; PrismShot defensive weapon).
    CHECK(rules.image("ATESLA") == "GAPRIS");
    CHECK(rules.cost("ATESLA") == 1500);
    CHECK(rules.tech_level("ATESLA") == 6);
    CHECK(rules.strength("ATESLA") == 600);
    CHECK(rules.power("ATESLA") == -75);
    CHECK(rules.primary("ATESLA") == "PrismShot");
    CHECK(rules.weapon_damage("PrismShot") == 120);
    CHECK(rules.weapon_range("PrismShot") == doctest::Approx(8.0));
    CHECK(rules.weapon_warhead("PrismShot") == "PrismWarhead");

    // NASAM — Allied Patriot Missile (RedEye2 AA weapon).
    CHECK(rules.cost("NASAM") == 1000);
    CHECK(rules.tech_level("NASAM") == 4);
    CHECK(rules.strength("NASAM") == 900);
    CHECK(rules.power("NASAM") == -50);
    CHECK(rules.primary("NASAM") == "RedEye2");
    CHECK(rules.weapon_damage("RedEye2") == 75);
    CHECK(rules.weapon_warhead("RedEye2") == "SAMWH");

    // GAAIRC — Allied Airforce Command HQ (radar; no weapon).
    CHECK(rules.cost("GAAIRC") == 1000);
    CHECK(rules.tech_level("GAAIRC") == 3);
    CHECK(rules.strength("GAAIRC") == 600);
    CHECK(rules.power("GAAIRC") == -50);
    CHECK(rules.prerequisite("GAAIRC") == std::vector<std::string>{"GAREFN", "GACNST"});
}

// The 12 new units' recovered stat blocks [RE: roster.md §2]. Asserted through
// ra2assets::Rules (the substrate resolve_combat_unit reads). Weapon rows carry
// the primary Damage/ROF/Range/Warhead. Image= overrides are the render/probe art.
static const char* kRosterUnitsIni = R"INI(
[DRON]
Prerequisite=NAWEAP
TechLevel=4
Cost=500
Strength=100
Armor=special_1
Primary=DroneJump
[DroneJump]
Damage=50
ROF=60
Range=1.83
Warhead=Parasite

[V3]
Prerequisite=NAWEAP,NARADR
TechLevel=3
Cost=800
Strength=150
Armor=light
Primary=V3Launcher
[V3Launcher]
Damage=1
ROF=150
Range=18
Warhead=Special

[ZEP]
Prerequisite=NAWEAP,NATECH
TechLevel=10
Cost=2000
Strength=2000
Armor=medium
Primary=BlimpBomb
[BlimpBomb]
Damage=250
ROF=50
Range=1.5
Warhead=BlimpHE

[SHK]
Prerequisite=NAHAND
TechLevel=5
Cost=500
Strength=130
Armor=Plate
Primary=ElectricBolt
[ElectricBolt]
Damage=50
ROF=60
Range=3
Warhead=Shock

[DESO]
Prerequisite=NAHAND,RADAR
TechLevel=8
Cost=600
Strength=150
Armor=plate
Primary=RadBeamWeapon
[RadBeamWeapon]
Damage=125
ROF=50
Range=6
Warhead=RadBeamWarhead

[APOC]
Image=MTNK
Prerequisite=NAWEAP,NATECH
TechLevel=7
Cost=1750
Strength=800
Armor=heavy
Primary=120mmx
[120mmx]
Damage=100
ROF=80
Range=5.75
Warhead=ApocAP

[TERROR]
Image=TRST
Prerequisite=NAHAND,RADAR
TechLevel=5
Cost=200
Strength=75
Armor=flak
Primary=TerrorBomb
[TerrorBomb]
Damage=225
ROF=10
Range=1.5
Warhead=TerrorBombWH

[GHOST]
Image=SEAL
Prerequisite=GAPILE,RADAR
TechLevel=9
Cost=1000
Strength=125
Armor=flak
Primary=MP5
[MP5]
Damage=125
ROF=10
Range=6
Warhead=HollowPoint

[CLEG]
Prerequisite=GAPILE,TECH
TechLevel=10
Cost=1500
Strength=125
Armor=none
Primary=NeutronRifle
[NeutronRifle]
Damage=8
ROF=120
Range=5
Warhead=ChronoBeam

[JUMPJET]
Image=ROCK
Prerequisite=GAPILE,RADAR
TechLevel=3
Cost=600
Strength=125
Armor=none
Primary=20mm
[20mm]
Damage=25
ROF=30
Range=5
Warhead=SSA

[MGTK]
Image=RTNK
Prerequisite=GAWEAP,GATECH
TechLevel=9
Cost=1000
Strength=200
Armor=light
Primary=MirageGun
[MirageGun]
Damage=100
ROF=70
Range=7
Warhead=MirageWH

[ORCA]
Image=FALC
Prerequisite=RADAR
TechLevel=3
Cost=1200
Strength=150
Armor=light
Primary=Maverick
[Maverick]
Damage=150
ROF=10
Range=6
Warhead=ORCAAP
)INI";

TEST_CASE("roster: 12 new units resolve recovered stats + Image overrides [RE: roster.md §2]") {
    ra2assets::Ini ini = ra2assets::Ini::parse(kRosterUnitsIni);
    ra2assets::Rules rules = ra2assets::Rules::load(ini);

    struct Row { const char* id; const char* image; int tech; int cost; int str;
                 const char* primary; int dmg; const char* warhead; };
    const Row rows[] = {
        {"DRON",   "DRON",  4,  500,  100, "DroneJump",     50,  "Parasite"},
        {"V3",     "V3",    3,  800,  150, "V3Launcher",    1,   "Special"},
        {"ZEP",    "ZEP",   10, 2000, 2000,"BlimpBomb",     250, "BlimpHE"},
        {"SHK",    "SHK",   5,  500,  130, "ElectricBolt",  50,  "Shock"},
        {"DESO",   "DESO",  8,  600,  150, "RadBeamWeapon", 125, "RadBeamWarhead"},
        {"APOC",   "MTNK",  7,  1750, 800, "120mmx",        100, "ApocAP"},
        {"TERROR", "TRST",  5,  200,  75,  "TerrorBomb",    225, "TerrorBombWH"},
        {"GHOST",  "SEAL",  9,  1000, 125, "MP5",           125, "HollowPoint"},
        {"CLEG",   "CLEG",  10, 1500, 125, "NeutronRifle",  8,   "ChronoBeam"},
        {"JUMPJET","ROCK",  3,  600,  125, "20mm",          25,  "SSA"},
        {"MGTK",   "RTNK",  9,  1000, 200, "MirageGun",     100, "MirageWH"},
        {"ORCA",   "FALC",  3,  1200, 150, "Maverick",      150, "ORCAAP"},
    };
    for (const Row& r : rows) {
        CAPTURE(r.id);
        CHECK(rules.image(r.id) == r.image);          // Image= override honored
        CHECK(rules.tech_level(r.id) == r.tech);
        CHECK(rules.cost(r.id) == r.cost);
        CHECK(rules.strength(r.id) == r.str);
        CHECK(rules.primary(r.id) == r.primary);
        CHECK(rules.weapon_damage(r.primary) == r.dmg);
        CHECK(rules.weapon_warhead(r.primary) == r.warhead);
    }
    // APOC promotion: real dual prereq recovered (kSoviet proxy stays NAWEAP-only).
    CHECK(rules.prerequisite("APOC") == std::vector<std::string>{"NAWEAP", "NATECH"});
}
