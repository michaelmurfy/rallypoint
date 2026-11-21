#include "doctest.h"
#include "../engine/map_trigger.hpp"
#include "asset_fixture.hpp"
#include "ra2assets/ini.hpp"
#include <map>
#include <string>
#include <vector>

using namespace ra2engine;
using ra2assets::Ini;

namespace {
const std::vector<std::string> kRulesCountries = {"Americans", "Alliance", "Russians"};
const std::vector<std::string> kRulesBuildings = {"GAPOWR", "NAPOWR", "GACNST"};

MapTriggerData parse(const std::string& text) {
    return parse_map_triggers(Ini::parse(text), kRulesCountries, kRulesBuildings);
}
}  // namespace

// ===========================================================================
// Task 1 -- parser [RE: map-triggers.md §1]
// ===========================================================================

TEST_CASE("map_trigger: waypoint letter decode (FUN_007624b0)") {
    CHECK(map_waypoint_letters("A") == 0);
    CHECK(map_waypoint_letters("Z") == 25);
    CHECK(map_waypoint_letters("AA") == 26);
    CHECK(map_waypoint_letters("AB") == 27);
    CHECK(map_waypoint_letters("BA") == 52);
    CHECK(map_waypoint_letters("b") == 1);       // case-insensitive
    CHECK(map_waypoint_letters("A1") == 0);      // non-alpha 2nd char -> one letter
    CHECK(map_waypoint_letters("1") == -1);
    CHECK(map_waypoint_letters("") == -1);
}

TEST_CASE("map_trigger: attach flags per event/action kind [RE §3.1]") {
    CHECK(map_event_flags(kEvElapsedTime) == kAttachLogic);
    CHECK(map_event_flags(kEvLocalSet) == kAttachLogic);
    CHECK(map_event_flags(kEvBuildingExists) == kAttachHouse);
    CHECK(map_event_flags(kEvDestroyedByAnything) == kAttachObject);
    CHECK(map_event_flags(kEvEnteredBy) == (kAttachCell | kAttachObject));
    CHECK(map_event_flags(kEvAnyEvent) ==
          (kAttachCell | kAttachObject | kAttachMap | kAttachHouse | kAttachLogic));
    CHECK(map_event_flags(kEvZoneEntryBy) == (kAttachCell | kAttachMap));
    CHECK(map_action_flags(14) == kAttachObject);
    CHECK(map_action_flags(53) == 0);
}

TEST_CASE("map_trigger: [Triggers] fields, house space, linked forward ref") {
    const std::string text =
        "[Countries]\n0=Player\n1=Russians\n"
        "[Triggers]\n"
        "AAAA0001=Player,AAAA0002,First,0,1,0,1,0\n"
        "AAAA0002=Russians,<none>,Second,1,1,1,1,1\n"
        "AAAA0003=<none>,none,Third\n";
    MapTriggerData d = parse(text);
    // house space: rules list then map-new appended (Russians already present)
    REQUIRE(d.house_types.size() == 4);
    CHECK(d.house_types[3] == "Player");
    CHECK(d.find_house_type("player") == 3);
    REQUIRE(d.triggers.size() == 3);
    const MapTriggerType& t1 = d.triggers[d.find_trigger("AAAA0001")];
    CHECK(t1.house_type == 3);
    CHECK(t1.name == "First");
    CHECK(t1.enabled);
    CHECK(t1.difficulty[0]); CHECK_FALSE(t1.difficulty[1]); CHECK(t1.difficulty[2]);
    CHECK(t1.linked == d.find_trigger("AAAA0002"));
    // the forward reference created AAAA0002 BEFORE its own line: array order
    CHECK(d.find_trigger("AAAA0002") == 1);
    const MapTriggerType& t2 = d.triggers[1];
    CHECK(t2.house_type == 2);   // Russians, rules index
    CHECK_FALSE(t2.enabled);     // Disabled=1
    CHECK(t2.must_transfer);
    CHECK(t2.linked == -1);
    const MapTriggerType& t3 = d.triggers[2];
    CHECK(t3.house_type == 0);   // "<none>" -> HouseType[0]
    CHECK(t3.linked == -1);      // "none"
    CHECK_FALSE(t3.enabled);     // missing Disabled field -> disabled
    CHECK_FALSE(t3.difficulty[1]);
}

TEST_CASE("map_trigger: [Events] count, Param==2 string, reversed storage") {
    const std::string text =
        "[Triggers]\nT1=Americans,<none>,x,0,1,1,1,0\n"
        "[Events]\nT1=3,13,0,10,61,2,0,GAPOWR,36,0,5\n";
    MapTriggerData d = parse(text);
    const MapTriggerType& t = d.triggers[0];
    REQUIRE(t.events.size() == 3);
    // memory order = INI order reversed (ReadINI prepends) [RE §1.2]
    CHECK(t.events[0].kind == 36); CHECK(t.events[0].value == 5);
    CHECK(t.events[1].kind == 61); CHECK(t.events[1].param == 2);
    CHECK(t.events[1].str == "GAPOWR");
    CHECK(t.events[2].kind == 13); CHECK(t.events[2].value == 10);
}

TEST_CASE("map_trigger: [Actions] ParamKind interpretation + last token") {
    const std::string text =
        "[TeamTypes]\n0=TEAMA\n1=TEAMB\n"
        "[TEAMA]\nName=a\n[TEAMB]\nName=b\nTag=TAGT\n"
        "[Triggers]\nT1=Americans,<none>,x,0,1,1,1,0\nTRIG0002=Americans,<none>,y,0,1,1,1,0\n"
        "[Actions]\nT1=9,"
        "53,2,TRIG0002,0,0,0,0,A,"    // trigger ref, waypoint A
        "4,1,1,0,0,0,0,AB,"           // team by 1-char index -> TEAMB
        "7,1,TEAMA,0,0,0,0,B,"        // team by ID
        "80,5,TEAMB,0,0,0,0,7,"       // ParamKind 5: last token -> Value
        "11,4,  Txt:Label ,0,0,0,0,A,"  // text, trimmed
        "125,10,NAMISL,0,0,0,0,C,"     // TechnoID
        "56,0,12,1,2,3,4,A,"           // Value + p[]
        "46,3,TAGZ,0,0,0,0,A,"         // tag ref (forward-created)
        "99,11,3,0,0,0,0,9\n";         // ParamKind 11 -> Value2
    MapTriggerData d = parse(text);
    REQUIRE(d.team_ids.size() == 2);
    const MapTriggerType& t = d.triggers[0];
    REQUIRE(t.actions.size() == 9);
    CHECK(t.actions[0].kind == 53); CHECK(t.actions[0].trigger == 1);
    // a <3-char P1 is an ARRAY INDEX, not an ID [RE §1.3]
    CHECK(t.actions[0].waypoint == 0);
    CHECK(t.actions[1].team == 1); CHECK(t.actions[1].waypoint == 27);
    CHECK(t.actions[2].team == 0); CHECK(t.actions[2].waypoint == 1);
    CHECK(t.actions[3].team == 1); CHECK(t.actions[3].value == 7);
    CHECK(t.actions[3].waypoint == -1);
    CHECK(t.actions[4].text == "Txt:Label");
    CHECK(t.actions[5].text == "NAMISL"); CHECK(t.actions[5].waypoint == 2);
    CHECK(t.actions[6].value == 12);
    CHECK(t.actions[6].p[0] == 1); CHECK(t.actions[6].p[3] == 4);
    CHECK(t.actions[7].tag == d.find_tag("TAGZ"));
    CHECK(t.actions[8].value == 3); CHECK(t.actions[8].value2 == 9);
    CHECK(t.actions[8].waypoint == -1);
    // TagType creation order: TeamType Tag= first, then action refs [RE §1.8]
    CHECK(d.find_tag("TAGT") == 0);
    CHECK(d.find_tag("TAGZ") == 1);
}

TEST_CASE("map_trigger: [Tags], [VariableNames], [CellTags], unknown refs") {
    const std::string text =
        "[Triggers]\nT1=Americans,<none>,x,0,1,1,1,0\n"
        "[Actions]\nT1=1,53,2,NOPE,0,0,0,0,A\n"
        "[Tags]\nG1=2,Tag One,T1\nG2=0,Tag Two,MISSING\n"
        "[VariableNames]\n0=First,1\n3=Fourth,0\n150=Out,1\n"
        "[CellTags]\n30105=G1\n107018=G2\n";
    MapTriggerData d = parse(text);
    REQUIRE(d.tags.size() == 2);
    CHECK(d.tags[0].persistence == 2);
    CHECK(d.tags[0].name == "Tag One");
    CHECK(d.tags[0].first_trigger == 0);
    // a dangling trigger id is created (find-or-create) but never defined
    CHECK(d.tags[1].first_trigger == d.find_trigger("MISSING"));
    CHECK_FALSE(d.triggers[d.find_trigger("MISSING")].defined);
    CHECK(d.triggers[0].actions[0].trigger == d.find_trigger("NOPE"));
    REQUIRE(d.local_init.size() == 100);
    CHECK(d.local_init[0] == 1); CHECK(d.local_names[0] == "First");
    CHECK(d.local_init[3] == 0); CHECK(d.local_names[3] == "Fourth");
    REQUIRE(d.cell_tags.size() == 2);
    CHECK(d.cell_tags[0].x == 105); CHECK(d.cell_tags[0].y == 30);
    CHECK(d.cell_tags[0].tag == 0);
    CHECK(d.cell_tags[1].x == 18); CHECK(d.cell_tags[1].y == 107);
}

TEST_CASE("map_trigger: real sov07tmd.map parses [install-gated]") {
    if (!ra2fixture::assets_available()) { MESSAGE("install absent; skipped"); return; }
    if (ra2fixture::mix_bytes_by_name("mapsmd03.mix").empty()) {
        MESSAGE("mapsmd03.mix absent; skipped"); return;
    }
    const ra2assets::MixFile& maps = ra2fixture::mix_by_name("mapsmd03.mix");
    const ra2assets::MixEntry* e = maps.find(0x133a0cb2u);
    REQUIRE(e != nullptr);
    auto raw = maps.read(*e);
    Ini map = Ini::parse(std::string(raw.begin(), raw.end()));
    const Ini& rules = ra2fixture::rulesmd_ini();
    MapTriggerData d = parse_map_triggers(map, ini_list_values(rules, "Countries"),
                                          ini_list_values(rules, "BuildingTypes"));
    CHECK(d.find_house_type("Americans") == 0);
    CHECK(d.find_house_type("Einstein") == 14);
    CHECK(d.find_house_type("Player") == 15);   // the _Lose DestroyedAll value
    const int lose = d.find_trigger("05FD863C");
    REQUIRE(lose >= 0);
    CHECK(d.triggers[lose].name == "_Lose");
    REQUIRE(d.triggers[lose].events.size() == 1);
    CHECK(d.triggers[lose].events[0].kind == kEvDestroyedAll);
    CHECK(d.triggers[lose].events[0].value == 15);
    int defined = 0;
    for (const MapTriggerType& t : d.triggers) defined += t.defined ? 1 : 0;
    CHECK(defined == (int)map.section("Triggers").size());
    CHECK(d.cell_tags.size() == map.section("CellTags").size());
    CHECK(d.local_names[5] == "Soviets Freed");
    // BuildingType index space: rules list FindOrAllocate'd (NAPSYA is listed
    // twice) -- the map's event values 24/54/300 name NAIRON/NAMISL/YACNST.
    CHECK(d.building_types[24] == "NAIRON");
    CHECK(d.building_types[54] == "NAMISL");
    CHECK(d.building_types[300] == "YACNST");
}

// ===========================================================================
// Task 2 -- runtime core [RE: map-triggers.md §3/§5]
// ===========================================================================
namespace {

// House types in these tests: 0 Americans, 1 Alliance, 2 Russians (rules),
// 3 Player (map [Countries]). Instances: Player (3) and Russians (2).
struct FakeHost : TriggerHost {
    std::vector<int> houses = {3, 2};
    int player = 3;
    std::map<int, int> buildings, vehicles, infantry;
    std::map<std::pair<int, int>, int> btype;
    std::map<std::string, int> technos;    // id -> count; absent id -> -1
    std::map<int, long> credits;
    std::map<int, double> power;
    std::vector<TriggerEffect> effects;
    bool house_exists(int ht) const override {
        for (int hh : houses) if (hh == ht) return true;
        return false;
    }
    std::vector<int> house_instances() const override { return houses; }
    int player_house_type() const override { return player; }
    static int get(const std::map<int, int>& m, int k) {
        auto it = m.find(k);
        return it == m.end() ? 0 : it->second;
    }
    int house_buildings(int ht) const override { return get(buildings, ht); }
    int house_vehicles(int ht) const override { return get(vehicles, ht); }
    int house_infantry(int ht) const override { return get(infantry, ht); }
    std::map<int, int> naval;
    int house_naval_units(int ht) const override { return get(naval, ht); }
    int house_building_type_count(int ht, int bt) const override {
        auto it = btype.find({ht, bt});
        return it == btype.end() ? 0 : it->second;
    }
    long house_credits(int ht) const override {
        auto it = credits.find(ht);
        return it == credits.end() ? 0 : it->second;
    }
    double house_power_ratio(int ht) const override {
        auto it = power.find(ht);
        return it == power.end() ? 1.0 : it->second;
    }
    int techno_count(const std::string& id) const override {
        if (id.empty()) {
            int n = 0;
            for (const auto& kv : technos) n += kv.second;
            return n;
        }
        auto it = technos.find(id);
        return it == technos.end() ? -1 : it->second;
    }
    bool apply(const TriggerEffect& e) override {
        effects.push_back(e);
        return true;
    }
    std::vector<VisualRequest> visuals;   // W3-FX
    void present(const VisualRequest& v) override { visuals.push_back(v); }
};

const char* kHead = "[Countries]\n0=Player\n";

// One-trigger map: tag TAG00001 (persistence p) -> trigger TRG00001 owned by
// Player with the given events/actions INI values.
std::string one(int persistence, const std::string& events, const std::string& actions,
                const std::string& flags = "0,1,1,1,0") {
    return std::string(kHead) +
           "[Triggers]\nTRG00001=Player,<none>,One," + flags + "\n"
           "[Events]\nTRG00001=" + events + "\n"
           "[Actions]\nTRG00001=" + actions + "\n"
           "[Tags]\nTAG00001=" + std::to_string(persistence) + ",T,TRG00001\n";
}

// Run `n` ticks; return the frame of the first fire of trigger `id` (-1 none).
long first_fire(MapTriggerSystem& s, FakeHost& h, int n, const std::string& id) {
    for (int i = 0; i < n; ++i) {
        const std::size_t before = s.log().size();
        s.tick(h);
        for (std::size_t k = before; k < s.log().size(); ++k)
            if (s.log()[k].find("fire " + id) != std::string::npos) return s.frame() - 1;
    }
    return -1;
}

int count_fires(const MapTriggerSystem& s, const std::string& id) {
    int n = 0;
    for (const std::string& l : s.log())
        if (l.find("fire " + id) != std::string::npos) ++n;
    return n;
}

}  // namespace

TEST_CASE("map_trigger: ElapsedTime fires at exactly Value*15 frames") {
    MapTriggerData d = parse(one(0, "1,13,0,2", "1,56,0,0,0,0,0,0,A"));
    MapTriggerSystem s(d, 1, 1);
    FakeHost h;
    s.start(h);
    CHECK(first_fire(s, h, 100, "TRG00001") == 30);
    CHECK(s.local(0));
    // volatile: never again, trigger destroyed
    for (int i = 0; i < 60; ++i) s.tick(h);
    CHECK(count_fires(s, "TRG00001") == 1);
    CHECK(s.trigger_destroyed(0));
}

TEST_CASE("map_trigger: ElapsedScenarioTime is Value <= frame/15") {
    MapTriggerData d = parse(one(0, "1,47,0,2", "1,56,0,0,0,0,0,0,A"));
    MapTriggerSystem s(d, 1, 1);
    FakeHost h;
    s.start(h);
    CHECK(first_fire(s, h, 100, "TRG00001") == 30);
}

TEST_CASE("map_trigger: AND of events; volatile fire kills the sibling chain trigger") {
    const std::string text = std::string(kHead) +
        "[Triggers]\n"
        "TRGAAAAA=Player,TRGBBBBB,A,0,1,1,1,0\n"
        "TRGBBBBB=Player,<none>,B,0,1,1,1,0\n"
        "TRGCCCCC=Player,<none>,C,0,1,1,1,0\n"
        "[Events]\nTRGAAAAA=1,13,0,1\nTRGBBBBB=1,13,0,3\n"
        "TRGCCCCC=2,13,0,1,36,0,4\n"              // ElapsedTime 1 AND local 4
        "[Actions]\nTRGAAAAA=1,56,0,1,0,0,0,0,A\nTRGBBBBB=1,56,0,2,0,0,0,0,A\n"
        "TRGCCCCC=1,56,0,3,0,0,0,0,A\n"
        "[Tags]\nTAGAAAAA=0,chain,TRGAAAAA\nTAGCCCCC=0,and,TRGCCCCC\n";
    MapTriggerData d = parse(text);
    MapTriggerSystem s(d, 1, 1);
    FakeHost h;
    s.start(h);
    for (int i = 0; i < 100; ++i) s.tick(h);
    CHECK(s.local(1));            // A fired at 15
    CHECK_FALSE(s.local(2));      // B (same tag) died with the tag
    CHECK_FALSE(s.local(3));      // C needs local 4 too
    s.set_local(4, true);         // ...and the change re-arms C's timer
    const long f = first_fire(s, h, 100, "TRGCCCCC");
    CHECK(f == 100 + 15);
    CHECK(s.local(3));
}

TEST_CASE("map_trigger: persistent tag re-arms its timer every fire") {
    MapTriggerData d = parse(one(2, "1,13,0,1", "1,19,7,Snd,0,0,0,0,A"));
    MapTriggerSystem s(d, 1, 1);
    FakeHost h;
    s.start(h);
    for (int i = 0; i <= 60; ++i) s.tick(h);   // frames 0..60
    CHECK(count_fires(s, "TRG00001") == 4);     // 15, 30, 45, 60
    CHECK_FALSE(s.trigger_destroyed(0));
}

TEST_CASE("map_trigger: logic-list order decides same-frame variable visibility") {
    // SETTER (ElapsedTime 0 -> LocalSet 1) and WATCHER (LocalSet 1 -> LocalSet 2).
    auto build = [](bool watcher_first) {
        std::string tags = watcher_first
            ? "[Tags]\nTAGWATCH=0,w,TRGWATCH\nTAGSETTR=0,s,TRGSETTR\n"
            : "[Tags]\nTAGSETTR=0,s,TRGSETTR\nTAGWATCH=0,w,TRGWATCH\n";
        return std::string(kHead) +
            "[Triggers]\nTRGSETTR=Player,<none>,S,0,1,1,1,0\nTRGWATCH=Player,<none>,W,0,1,1,1,0\n"
            "[Events]\nTRGSETTR=1,13,0,0\nTRGWATCH=1,36,0,1\n"
            "[Actions]\nTRGSETTR=1,56,0,1,0,0,0,0,A\nTRGWATCH=1,56,0,2,0,0,0,0,A\n" + tags;
    };
    {
        MapTriggerData d = parse(build(false));
        MapTriggerSystem s(d, 1, 1);
        FakeHost h;
        s.start(h);
        CHECK(first_fire(s, h, 5, "TRGWATCH") == 0);   // later in the list: same frame
    }
    {
        MapTriggerData d = parse(build(true));
        MapTriggerSystem s(d, 1, 1);
        FakeHost h;
        s.start(h);
        CHECK(first_fire(s, h, 5, "TRGWATCH") == 1);   // earlier in the list: next frame
    }
}

TEST_CASE("map_trigger: Enable resets timers and honours difficulty") {
    const std::string text = std::string(kHead) +
        "[Triggers]\n"
        "TRGOFF01=Player,<none>,off,1,1,1,1,0\n"      // disabled, all difficulties
        "TRGOFF02=Player,<none>,offN,1,1,0,1,0\n"     // disabled, NOT Normal
        "TRGON001=Player,<none>,enabler,0,1,1,1,0\n"
        "[Events]\nTRGOFF01=1,13,0,1\nTRGOFF02=1,13,0,1\nTRGON001=1,13,0,2\n"
        "[Actions]\nTRGOFF01=1,56,0,1,0,0,0,0,A\nTRGOFF02=1,56,0,2,0,0,0,0,A\n"
        "TRGON001=2,53,2,TRGOFF01,0,0,0,0,A,53,2,TRGOFF02,0,0,0,0,A\n"
        "[Tags]\nTAG00001=0,a,TRGOFF01\nTAG00002=0,b,TRGOFF02\nTAG00003=0,c,TRGON001\n";
    MapTriggerData d = parse(text);
    MapTriggerSystem s(d, /*Normal*/ 1, 1);
    FakeHost h;
    s.start(h);
    CHECK_FALSE(s.trigger_enabled(d.find_trigger("TRGOFF01")));
    // enabled at frame 30 -> timer restarts -> fires at 45 (not 15)
    CHECK(first_fire(s, h, 100, "TRGOFF01") == 45);
    CHECK_FALSE(s.local(2));     // Normal flag clear -> enable refused
}

TEST_CASE("map_trigger: Disable, DestroyTrigger and ForceTrigger") {
    const std::string text = std::string(kHead) +
        "[Triggers]\n"
        "TRGTARG1=Player,<none>,t1,0,1,1,1,0\n"
        "TRGTARG2=Player,<none>,t2,0,1,1,1,0\n"
        "TRGTARG3=Player,<none>,t3,0,1,1,1,0\n"
        "TRGCTRL1=Player,<none>,ctl,0,1,1,1,0\n"
        "[Events]\nTRGTARG1=1,13,0,2\nTRGTARG2=1,13,0,2\nTRGTARG3=1,36,0,9\nTRGCTRL1=1,13,0,1\n"
        "[Actions]\nTRGTARG1=1,56,0,1,0,0,0,0,A\nTRGTARG2=1,56,0,2,0,0,0,0,A\n"
        "TRGTARG3=1,56,0,3,0,0,0,0,A\n"
        "TRGCTRL1=3,54,2,TRGTARG1,0,0,0,0,A,12,2,TRGTARG2,0,0,0,0,A,22,2,TRGTARG3,0,0,0,0,A\n"
        "[Tags]\nTAG00001=0,a,TRGTARG1\nTAG00002=0,b,TRGTARG2\nTAG00003=0,c,TRGTARG3\n"
        "TAG00004=0,d,TRGCTRL1\n";
    MapTriggerData d = parse(text);
    MapTriggerSystem s(d, 1, 1);
    FakeHost h;
    s.start(h);
    for (int i = 0; i < 100; ++i) s.tick(h);
    CHECK_FALSE(s.local(1));   // disabled before its 30-frame timer
    CHECK_FALSE(s.local(2));   // destroyed
    CHECK(s.local(3));         // forced: actions ran although local 9 is clear
    CHECK_FALSE(s.trigger_destroyed(d.find_trigger("TRGTARG3")));   // force != fire
}

TEST_CASE("map_trigger: mission timer actions + MissionTimerExpired is a one-frame event") {
    const std::string text = std::string(kHead) +
        "[Triggers]\nTRGSET01=Player,<none>,set,0,1,1,1,0\nTRGEXP01=Player,<none>,exp,0,1,1,1,0\n"
        "[Events]\nTRGSET01=1,13,0,0\nTRGEXP01=1,14,0,0\n"
        "[Actions]\nTRGSET01=1,27,0,2,0,0,0,0,A\nTRGEXP01=1,56,0,1,0,0,0,0,A\n"
        "[Tags]\nTAG00001=0,a,TRGSET01\nTAG00002=0,b,TRGEXP01\n";
    MapTriggerData d = parse(text);
    MapTriggerSystem s(d, 1, 1);
    FakeHost h;
    s.start(h);
    s.tick(h);   // frame 0: TimerSet 2 -> {0, 30}
    CHECK(s.mission_timer_start() == 0);
    CHECK(s.mission_timer_left() == 30);
    CHECK(first_fire(s, h, 100, "TRGEXP01") == 30);
    CHECK(s.mission_timer_start() == -1);   // stopped after the logic loop
}

TEST_CASE("map_trigger: mission timer Stop/Start/Extend/Shorten arithmetic") {
    const std::string text = std::string(kHead) +
        "[Triggers]\nTRGA0001=Player,<none>,a,0,1,1,1,0\nTRGB0001=Player,<none>,b,0,1,1,1,0\n"
        "TRGC0001=Player,<none>,c,0,1,1,1,0\n"
        "[Events]\nTRGA0001=1,13,0,0\nTRGB0001=1,13,0,1\nTRGC0001=1,13,0,2\n"
        // f0: Set 10 (150); f15: Stop (left 135) ; f30: Extend 2 (135+30=165 from 30) + Shorten 1 (150)
        "[Actions]\nTRGA0001=1,27,0,10,0,0,0,0,A\nTRGB0001=1,24,0,0,0,0,0,0,A\n"
        "TRGC0001=3,25,0,2,0,0,0,0,A,26,0,1,0,0,0,0,A,23,0,0,0,0,0,0,A\n"
        "[Tags]\nTAG0000A=0,a,TRGA0001\nTAG0000B=0,b,TRGB0001\nTAG0000C=0,c,TRGC0001\n";
    MapTriggerData d = parse(text);
    MapTriggerSystem s(d, 1, 1);
    FakeHost h;
    s.start(h);
    for (int i = 0; i <= 15; ++i) s.tick(h);
    CHECK(s.mission_timer_start() == -1);
    CHECK(s.mission_timer_left() == 135);
    for (int i = 16; i <= 30; ++i) s.tick(h);
    CHECK(s.mission_timer_start() == 30);
    CHECK(s.mission_timer_left() == 150);
}

TEST_CASE("map_trigger: house loop - every 8th frame, last tag first, AnyEvent") {
    const std::string text = std::string(kHead) +
        "[Triggers]\n"
        "TRGHOUS1=Player,<none>,h1,0,1,1,1,0\nTRGHOUS2=Player,<none>,h2,0,1,1,1,0\n"
        "TRGNOHSE=Alliance,<none>,nohouse,0,1,1,1,0\n"
        "TRGBLDG1=Player,<none>,bldg,0,1,1,1,0\n"
        "[Events]\nTRGHOUS1=1,8,0,0\nTRGHOUS2=1,8,0,0\nTRGNOHSE=1,32,0,0\nTRGBLDG1=1,32,0,1\n"
        "[Actions]\nTRGHOUS1=1,0,0,0,0,0,0,0,A\nTRGHOUS2=1,0,0,0,0,0,0,0,A\n"
        "TRGNOHSE=1,56,0,1,0,0,0,0,A\nTRGBLDG1=1,56,0,2,0,0,0,0,A\n"
        "[Tags]\nTAG00001=0,a,TRGHOUS1\nTAG00002=0,b,TRGHOUS2\nTAG00003=0,c,TRGNOHSE\n"
        "TAG00004=0,d,TRGBLDG1\n";
    MapTriggerData d = parse(text);
    MapTriggerSystem s(d, 1, 1);
    FakeHost h;
    s.start(h);
    s.tick(h);   // frame 0
    int i1 = -1, i2 = -1;
    for (int i = 0; i < (int)s.log().size(); ++i) {
        if (s.log()[i].find("fire TRGHOUS1") != std::string::npos) i1 = i;
        if (s.log()[i].find("fire TRGHOUS2") != std::string::npos) i2 = i;
    }
    // AnyEvent (8) is ALSO a logic event: both fire in the frame-0 logic loop,
    // in list order, before the house loop ever runs.
    REQUIRE(i1 >= 0);
    REQUIRE(i2 >= 0);
    CHECK(i1 < i2);
    CHECK_FALSE(s.local(1));    // Alliance has no house instance: never listed
    // BuildingExists: Player gains building type 1 at frame 3 -> fires at frame 8
    for (int i = 1; i < 3; ++i) s.tick(h);
    h.btype[{3, 1}] = 1;
    CHECK(first_fire(s, h, 20, "TRGBLDG1") == 8);
    CHECK(s.house_fired() == 1);
}

TEST_CASE("map_trigger: house-loop order is last tag first") {
    const std::string text = std::string(kHead) +
        "[Triggers]\nTRGHB001=Player,<none>,b1,0,1,1,1,0\nTRGHB002=Player,<none>,b2,0,1,1,1,0\n"
        "[Events]\nTRGHB001=1,32,0,0\nTRGHB002=1,32,0,0\n"
        "[Actions]\nTRGHB001=1,0,0,0,0,0,0,0,A\nTRGHB002=1,0,0,0,0,0,0,0,A\n"
        "[Tags]\nTAG00001=0,a,TRGHB001\nTAG00002=0,b,TRGHB002\n";
    MapTriggerData d = parse(text);
    MapTriggerSystem s(d, 1, 1);
    FakeHost h;
    h.btype[{3, 0}] = 1;
    s.start(h);
    s.tick(h);
    int i1 = -1, i2 = -1;
    for (int i = 0; i < (int)s.log().size(); ++i) {
        if (s.log()[i].find("fire TRGHB001") != std::string::npos) i1 = i;
        if (s.log()[i].find("fire TRGHB002") != std::string::npos) i2 = i;
    }
    REQUIRE(i1 >= 0);
    REQUIRE(i2 >= 0);
    CHECK(i2 < i1);
}

TEST_CASE("map_trigger: Win/Lose are player-relative; first decision sticks") {
    {
        MapTriggerData d = parse(one(0, "1,13,0,0", "1,1,0,3,0,0,0,0,A"));
        MapTriggerSystem s(d, 1, 1);
        FakeHost h;
        s.start(h);
        s.tick(h);
        CHECK(s.outcome() == 1);   // Win(Player) -> player wins
    }
    {
        MapTriggerData d = parse(one(0, "1,13,0,0", "2,2,0,3,0,0,0,0,A,1,0,3,0,0,0,0,A"));
        MapTriggerSystem s(d, 1, 1);
        FakeHost h;
        s.start(h);
        s.tick(h);
        CHECK(s.outcome() == 2);   // Lose(Player) first -> lost, Win ignored
    }
    {
        MapTriggerData d = parse(one(0, "1,13,0,0", "1,1,0,2,0,0,0,0,A"));
        MapTriggerSystem s(d, 1, 1);
        FakeHost h;
        s.start(h);
        s.tick(h);
        CHECK(s.outcome() == 2);   // Win(Russians) -> player loses
    }
}

TEST_CASE("map_trigger: RandomDelay draws (Value/2 + [0,Value]) * 15") {
    for (std::uint32_t seed = 1; seed < 40; ++seed) {
        MapTriggerData d = parse(one(0, "1,51,0,10", "1,56,0,0,0,0,0,0,A"));
        MapTriggerSystem s(d, 1, seed);
        FakeHost h;
        s.start(h);
        const long f = first_fire(s, h, 300, "TRG00001");
        CHECK(f >= 75);
        CHECK(f <= 225);
        CHECK(f % 15 == 0);
    }
}

TEST_CASE("map_trigger: a disabled trigger never fires; state_hash is deterministic") {
    MapTriggerData d = parse(one(0, "1,13,0,0", "1,56,0,0,0,0,0,0,A", "1,1,1,1,0"));
    MapTriggerSystem a(d, 1, 7), b(d, 1, 7);
    FakeHost h1, h2;
    a.start(h1);
    b.start(h2);
    for (int i = 0; i < 50; ++i) { a.tick(h1); b.tick(h2); }
    CHECK_FALSE(a.local(0));
    CHECK(a.state_hash() == b.state_hash());
}

// ===========================================================================
// Task 3 -- host-backed events/actions, object raises [RE: map-triggers.md §4/§5]
// ===========================================================================

TEST_CASE("map_trigger: DestroyedAll / BuildingsAll / UnitsAll + absent-house rule") {
    // house-loop triggers owned by Player; Value = the house checked.
    const std::string text = std::string(kHead) +
        "[Triggers]\n"
        "TRGALL02=Player,<none>,all2,0,1,1,1,0\n"     // DestroyedAll Russians (exists)
        "TRGALL01=Player,<none>,all1,0,1,1,1,0\n"     // DestroyedAll Alliance (absent)
        "TRGBLD02=Player,<none>,bld2,0,1,1,1,0\n"     // DestroyedBuildingsAll Russians
        "TRGUNT02=Player,<none>,unt2,0,1,1,1,0\n"     // DestroyedUnitsAll Russians
        "[Events]\nTRGALL02=1,11,0,2\nTRGALL01=1,11,0,1\nTRGBLD02=1,10,0,2\nTRGUNT02=1,9,0,2\n"
        "[Actions]\nTRGALL02=1,56,0,1,0,0,0,0,A\nTRGALL01=1,56,0,2,0,0,0,0,A\n"
        "TRGBLD02=1,56,0,3,0,0,0,0,A\nTRGUNT02=1,56,0,4,0,0,0,0,A\n"
        "[Tags]\nTAG00001=0,a,TRGALL02\nTAG00002=0,b,TRGALL01\nTAG00003=0,c,TRGBLD02\n"
        "TAG00004=0,d,TRGUNT02\n";
    MapTriggerData d = parse(text);
    MapTriggerSystem s(d, 1, 1);
    FakeHost h;
    h.buildings[2] = 2;
    h.infantry[2] = 1;
    s.start(h);
    s.tick(h);
    CHECK_FALSE(s.local(1));   // Russians still own things
    CHECK(s.local(2));         // Alliance has no house: the value-house rule -> TRUE
    CHECK_FALSE(s.local(3));   // buildings remain
    CHECK_FALSE(s.local(4));   // infantry remains
    h.buildings[2] = 0;
    for (int i = 1; i <= 8; ++i) s.tick(h);
    CHECK(s.local(3));
    CHECK_FALSE(s.local(1));
    h.infantry[2] = 0;
    for (int i = 9; i <= 16; ++i) s.tick(h);
    CHECK(s.local(1));
    CHECK(s.local(4));
}

TEST_CASE("map_trigger: W2-CA-06 DestroyedUnitsNaval (55) / DestroyedUnitsLand (56)") {
    // [RE tev0_0071dbe0.c case 0x37/0x38] value-house switch.
    const std::string text = std::string(kHead) +
        "[Triggers]\n"
        "TRGNAV02=Player,<none>,nav2,0,1,1,1,0\n"
        "TRGLND02=Player,<none>,lnd2,0,1,1,1,0\n"
        "TRGNAV01=Player,<none>,nav1,0,1,1,1,0\n"
        "[Events]\nTRGNAV02=1,55,0,2\nTRGLND02=1,56,0,2\nTRGNAV01=1,55,0,1\n"
        "[Actions]\nTRGNAV02=1,56,0,1,0,0,0,0,A\nTRGLND02=1,56,0,2,0,0,0,0,A\n"
        "TRGNAV01=1,56,0,3,0,0,0,0,A\n"
        "[Tags]\nTAG00001=0,a,TRGNAV02\nTAG00002=0,b,TRGLND02\nTAG00003=0,c,TRGNAV01\n";
    MapTriggerData d = parse(text);
    MapTriggerSystem s(d, 1, 1);
    FakeHost h;
    h.vehicles[2] = 3;   // 2 ships + 1 tank
    h.naval[2] = 2;
    h.infantry[2] = 1;
    s.start(h);
    s.tick(h);
    CHECK_FALSE(s.local(1));   // ships remain
    CHECK_FALSE(s.local(2));   // tank + infantry remain
    CHECK(s.local(3));         // Alliance absent -> TRUE
    h.naval[2] = 0;
    h.vehicles[2] = 1;
    for (int i = 1; i <= 8; ++i) s.tick(h);
    CHECK(s.local(1));
    CHECK_FALSE(s.local(2));
    h.vehicles[2] = 0;
    for (int i = 9; i <= 16; ++i) s.tick(h);
    CHECK_FALSE(s.local(2));   // infantry still alive
    h.infantry[2] = 0;
    for (int i = 17; i <= 24; ++i) s.tick(h);
    CHECK(s.local(2));
}

TEST_CASE("map_trigger: BuildingDoesNotExist, credits, power events") {
    const std::string text = std::string(kHead) +
        "[Triggers]\n"
        "TRGNEX01=Player,<none>,nex,0,1,1,1,0\nTRGCRD01=Player,<none>,crd,0,1,1,1,0\n"
        "TRGCRB01=Player,<none>,crb,0,1,1,1,0\nTRGLOW01=Player,<none>,low,0,1,1,1,0\n"
        "TRGFUL01=Player,<none>,ful,0,1,1,1,0\n"
        "[Events]\nTRGNEX01=1,57,0,2\nTRGCRD01=1,12,0,500\nTRGCRB01=1,52,0,100\n"
        "TRGLOW01=1,30,0,3\nTRGFUL01=1,58,0,3\n"
        "[Actions]\nTRGNEX01=1,56,0,1,0,0,0,0,A\nTRGCRD01=1,56,0,2,0,0,0,0,A\n"
        "TRGCRB01=1,56,0,3,0,0,0,0,A\nTRGLOW01=1,56,0,4,0,0,0,0,A\nTRGFUL01=1,56,0,5,0,0,0,0,A\n"
        "[Tags]\nTAG00001=0,a,TRGNEX01\nTAG00002=0,b,TRGCRD01\nTAG00003=0,c,TRGCRB01\n"
        "TAG00004=0,d,TRGLOW01\nTAG00005=0,e,TRGFUL01\n";
    MapTriggerData d = parse(text);
    MapTriggerSystem s(d, 1, 1);
    FakeHost h;
    h.btype[{3, 2}] = 1;
    h.credits[3] = 200;
    h.power[3] = 0.5;
    s.start(h);
    s.tick(h);
    CHECK_FALSE(s.local(1));   // building type 2 exists
    CHECK_FALSE(s.local(2));   // 200 < 500
    CHECK_FALSE(s.local(3));   // 200 > 100
    CHECK(s.local(4));         // ratio 0.5 < 1.0
    CHECK_FALSE(s.local(5));
    h.btype[{3, 2}] = 0;
    h.credits[3] = 600;
    h.power[3] = 1.0;
    for (int i = 1; i <= 8; ++i) s.tick(h);
    CHECK(s.local(1));
    CHECK(s.local(2));
    CHECK(s.local(5));
}

TEST_CASE("map_trigger: TechTypeExists/DoesntExist; unknown type -> false") {
    const std::string text = std::string(kHead) +
        "[Triggers]\n"
        "TRGTEX01=Player,<none>,tex,0,1,1,1,0\nTRGTNX01=Player,<none>,tnx,0,1,1,1,0\n"
        "TRGUNK01=Player,<none>,unk,0,1,1,1,0\n"
        "[Events]\nTRGTEX01=1,60,2,2,HTNK\nTRGTNX01=1,61,2,0,APOC\nTRGUNK01=1,61,2,0,NOSUCH\n"
        "[Actions]\nTRGTEX01=1,56,0,1,0,0,0,0,A\nTRGTNX01=1,56,0,2,0,0,0,0,A\n"
        "TRGUNK01=1,56,0,3,0,0,0,0,A\n"
        "[Tags]\nTAG00001=2,a,TRGTEX01\nTAG00002=0,b,TRGTNX01\nTAG00003=0,c,TRGUNK01\n";
    MapTriggerData d = parse(text);
    MapTriggerSystem s(d, 1, 1);
    FakeHost h;
    h.technos["HTNK"] = 1;
    h.technos["APOC"] = 1;
    s.start(h);
    s.tick(h);
    CHECK_FALSE(s.local(1));   // 1 < 2
    CHECK_FALSE(s.local(2));   // an APOC exists
    CHECK_FALSE(s.local(3));   // unknown type
    h.technos["HTNK"] = 2;
    h.technos["APOC"] = 0;
    s.tick(h);
    CHECK(s.local(1));
    CHECK(s.local(2));
}

TEST_CASE("map_trigger: object kinds fire only when raised with their kind") {
    const std::string text = std::string(kHead) +
        "[Triggers]\n"
        "TRGDIE01=Player,<none>,die,0,1,1,1,0\nTRGENT01=Player,<none>,enter,0,1,1,1,0\n"
        "TRGATK01=Player,<none>,atk,0,1,1,1,0\n"
        "[Events]\nTRGDIE01=1,48,0,0\nTRGENT01=1,1,0,2\nTRGATK01=1,44,0,2\n"
        "[Actions]\nTRGDIE01=1,56,0,1,0,0,0,0,A\nTRGENT01=1,56,0,2,0,0,0,0,A\n"
        "TRGATK01=1,56,0,3,0,0,0,0,A\n"
        "[Tags]\nTAGDIE01=0,a,TRGDIE01\nTAGENT01=0,b,TRGENT01\nTAGATK01=0,c,TRGATK01\n"
        "[CellTags]\n30105=TAGENT01\n";
    MapTriggerData d = parse(text);
    MapTriggerSystem s(d, 1, 1);
    FakeHost h;
    s.start(h);
    for (int i = 0; i < 20; ++i) s.tick(h);
    CHECK_FALSE(s.local(1));   // never raised by the loops
    RaiseContext obj;
    obj.has_object = true;
    obj.object_house_type = 3;
    CHECK_FALSE(s.raise_tag(d.find_tag("TAGDIE01"), kEvAttackedByAnybody, obj, h));
    CHECK(s.raise_tag(d.find_tag("TAGDIE01"), kEvDestroyedByAnything, obj, h));
    CHECK(s.local(1));
    // EnteredBy Value 2 (Russians): a Player object does not count
    CHECK_FALSE(s.raise_cell(105, 30, kEvEnteredBy, obj, h));
    obj.object_house_type = 2;
    CHECK(s.raise_cell(105, 30, kEvEnteredBy, obj, h));
    CHECK(s.local(2));
    // AttackedByHouse needs the attacker's house == Value
    RaiseContext atk = obj;
    atk.source_house_type = 3;
    CHECK_FALSE(s.raise_tag(d.find_tag("TAGATK01"), kEvAttackedByHouse, atk, h));
    atk.source_house_type = 2;
    CHECK(s.raise_tag(d.find_tag("TAGATK01"), kEvAttackedByHouse, atk, h));
}

TEST_CASE("map_trigger: host effects carry the resolved team/waypoint/house") {
    const std::string text = std::string(kHead) +
        "[TeamTypes]\n0=TEAMAAAA\n[TEAMAAAA]\nName=x\n"
        "[Triggers]\nTRG00001=Player,<none>,fx,0,1,1,1,0\n"
        "[Events]\nTRG00001=1,13,0,0\n"
        "[Actions]\nTRG00001=8,"
        "4,1,TEAMAAAA,0,0,0,0,A,"
        "7,1,0,0,0,0,0,A,"
        "80,1,TEAMAAAA,0,0,0,0,C,"
        "80,1,TEAMAAAA,0,0,0,0,1,"     // waypoint -1 -> refused
        "17,0,12,0,0,0,0,A,"           // reveal around waypoint 12 (P1)
        "125,10,NAMISL,0,0,0,0,B,"
        "21,6,EVA_Speech,0,0,0,0,A,"   // presentation
        "88,0,3,0,0,0,0,A\n"           // unimplemented
        "[Tags]\nTAG00001=0,T,TRG00001\n";
    MapTriggerData d = parse(text);
    MapTriggerSystem s(d, 1, 1);
    FakeHost h;
    s.start(h);
    s.tick(h);
    REQUIRE(h.effects.size() == 5);
    CHECK(h.effects[0].action == 4);
    CHECK(h.effects[0].team == "TEAMAAAA");
    CHECK(h.effects[1].action == 7);
    CHECK(h.effects[1].team == "TEAMAAAA");   // "0" -> team array index 0
    CHECK(h.effects[2].action == 80);
    CHECK(h.effects[2].waypoint == 2);
    CHECK(h.effects[3].action == 17);
    CHECK(h.effects[3].waypoint == 12);
    CHECK(h.effects[3].house_type == 3);      // the player
    CHECK(h.effects[4].action == 125);
    CHECK(h.effects[4].text == "NAMISL");
    CHECK(h.effects[4].house_type == 3);      // the trigger's house
    CHECK(h.effects[4].waypoint == 1);
    bool recorded = false, unimpl = false;
    for (const std::string& l : s.log()) {
        if (l.find("a21 PlaySpeech") != std::string::npos && l.find("(recorded)") != std::string::npos)
            recorded = true;
        if (l.find("a88 unimplemented") != std::string::npos) unimpl = true;
    }
    CHECK(recorded);
    CHECK(unimpl);
}

TEST_CASE("map_trigger: house flags from ProductionBegins / AITriggers") {
    MapTriggerData d = parse(one(0, "1,13,0,0",
                                 "3,3,0,2,0,0,0,0,A,74,0,2,0,0,0,0,A,3,0,1,0,0,0,0,A"));
    MapTriggerSystem s(d, 1, 1);
    FakeHost h;
    s.start(h);
    s.tick(h);
    CHECK(s.house_flags(2) == (1u | 4u));
    CHECK(s.house_flags(1) == 0u);   // Alliance has no house instance
}

TEST_CASE("map_trigger: W3-FX presentation actions queue typed requests in campaign mode only") {
    const std::string text = std::string(kHead) +
        "[TeamTypes]\n0=TEAMAAAA\n[TEAMAAAA]\nName=x\n"
        "[Triggers]\nTRG00001=Player,<none>,fx,0,1,1,1,0\n"
        "[Events]\nTRG00001=1,13,0,0\n"
        "[Actions]\nTRG00001=7,"
        "41,0,253,0,0,0,0,U,"          // PlayAnimAt AnimTypes[253] at U
        "55,0,3,0,0,0,0,B,"            // CreateRadarEvent type 3 at B
        "88,0,12,0,0,0,0,AM,"          // ParticleAnim system 12 at AM
        "48,0,4,0,0,0,0,C,"            // MoveCamera speed 4 to C
        "113,0,3,0,0,0,0,A,"           // MakeHouseCheer house 3
        "104,1,TEAMAAAA,0,0,0,0,30,"   // FlashTeam
        "90,0,32,0,0,0,0,AG\n"         // LightningStrikeAt -> host
        "[Tags]\nTAG00001=0,T,TRG00001\n";
    MapTriggerData d = parse(text);
    {   // P9 mode: nothing queued, 90 stays unimplemented, log as before
        MapTriggerSystem s(d, 1, 1);
        FakeHost h;
        s.start(h);
        s.tick(h);
        CHECK(h.visuals.empty());
        CHECK(h.effects.empty());
    }
    MapTriggerSystem s(d, 1, 1);
    s.set_campaign_mode(true);
    FakeHost h;
    s.start(h);
    s.tick(h);
    const std::vector<VisualRequest>& v = h.visuals;
    REQUIRE(v.size() == 6);
    CHECK(v[0].action == 41);
    CHECK(v[0].value == 253);
    CHECK(v[0].waypoint == map_waypoint_letters("U"));
    CHECK(v[1].action == 55);
    CHECK(v[1].value == 3);
    CHECK(v[2].action == 88);
    CHECK(v[2].value == 12);
    CHECK(v[2].waypoint == map_waypoint_letters("AM"));
    CHECK(v[3].action == 48);
    CHECK(v[3].value == 4);
    CHECK(v[4].action == 113);
    CHECK(v[4].house_type == 3);
    CHECK(v[5].action == 104);
    CHECK(v[5].team == "TEAMAAAA");
    REQUIRE(h.effects.size() == 1);    // 90 goes to the host (it damages)
    CHECK(h.effects[0].action == 90);
    CHECK(h.effects[0].waypoint == map_waypoint_letters("AG"));
}

TEST_CASE("map_trigger: W5-A events 23/34/53/54 and actions 35/40/61/70") {
    struct Host2 : FakeHost {
        std::vector<std::string> left;
        bool team_left_map(const std::string& id) const override {
            for (const std::string& t : left) if (t == id) return true;
            return false;
        }
        bool waypoint_cell(int wp, int& x, int& y) const override {
            if (wp != 4) return false;
            x = 50; y = 60;
            return true;
        }
        std::string infantry_type_id(int idx) const override { return idx == 7 ? "E1" : "E2"; }
    };
    const std::string text = std::string(kHead) +
        "[TeamTypes]\n0=TEAMAAAA\n[TEAMAAAA]\nName=x\n"
        "[Triggers]\n"
        "TRGLEF01=Player,<none>,left,0,1,1,1,0\nTRGNEA01=Player,<none>,near,0,1,1,1,0\n"
        "TRGSPY01=Player,<none>,spyh,0,1,1,1,0\nTRGSPY02=Player,<none>,spyi,0,1,1,1,0\n"
        "TRGACT01=Player,<none>,acts,0,1,1,1,0\n"
        "[Events]\nTRGLEF01=1,23,1,TEAMAAAA\nTRGNEA01=1,34,0,4\nTRGSPY01=1,53,0,2\nTRGSPY02=1,54,0,7\n"
        "TRGACT01=1,13,0,0\n"
        "[Actions]\nTRGLEF01=1,56,0,1,0,0,0,0,A\nTRGNEA01=1,56,0,2,0,0,0,0,A\n"
        "TRGSPY01=1,56,0,3,0,0,0,0,A\nTRGSPY02=1,56,0,4,0,0,0,0,A\n"
        "TRGACT01=4,35,0,7,0,0,0,0,A,40,0,0,2,4,45,120,A,61,0,0,0,0,0,0,A,70,3,TAGNEA01,0,0,0,0,A\n"
        "[Tags]\nTAGLEF01=2,a,TRGLEF01\nTAGNEA01=2,b,TRGNEA01\nTAGSPY01=2,c,TRGSPY01\n"
        "TAGSPY02=2,d,TRGSPY02\nTAGACT01=0,e,TRGACT01\n"
        "[CellTags]\n30105=TAGSPY01\n30106=TAGSPY02\n";
    MapTriggerData d = parse(text);
    MapTriggerSystem s(d, 1, 1);
    s.set_campaign_mode(true);
    Host2 h;
    s.start(h);
    // 23 TeamLeavesMap: raised on the logic list, true once the TeamType left.
    RaiseContext none;
    CHECK_FALSE(s.raise_logic(23, none, h));
    h.left.push_back("TEAMAAAA");
    CHECK(s.raise_logic(23, none, h));
    CHECK(s.local(1));
    // 34 ComesNearWaypoint: within 5 cells (0x500 leptons) of waypoint 4 (50,60).
    RaiseContext near;
    near.has_object = true;
    near.object_house_type = 3;
    near.obj_x = 56; near.obj_y = 60;
    CHECK_FALSE(s.raise_tag(d.find_tag("TAGNEA01"), 34, near, h));
    near.obj_x = 54; near.obj_y = 63;
    CHECK(s.raise_tag(d.find_tag("TAGNEA01"), 34, near, h));
    CHECK(s.local(2));
    // 53 SpyAsHouse (Russians = 2) / 54 SpyAsInfantry (InfantryTypes[7] = E1).
    RaiseContext spy = near;
    spy.disguise_house_type = 3;
    CHECK_FALSE(s.raise_cell(105, 30, 53, spy, h));
    spy.disguise_house_type = 2;
    CHECK(s.raise_cell(105, 30, 53, spy, h));
    spy.disguise_infantry = "E2";
    CHECK_FALSE(s.raise_cell(106, 30, 54, spy, h));
    spy.disguise_infantry = "E1";
    CHECK(s.raise_cell(106, 30, 54, spy, h));
    CHECK(s.local(3));
    CHECK(s.local(4));
    // 35 / 40 / 61 go to the host with their parameters; 70 destroys a tag.
    s.tick(h);
    REQUIRE(h.effects.size() == 3);
    CHECK(h.effects[0].action == 35);
    CHECK(h.effects[0].value == 7);
    CHECK(h.effects[0].house_type == 3);
    CHECK(h.effects[1].action == 40);
    CHECK(h.effects[1].p[0] == 2);
    CHECK(h.effects[1].p[3] == 120);
    CHECK(h.effects[2].action == 61);
    CHECK_FALSE(s.tag_alive(d.find_tag("TAGNEA01")));   // DestroyTag 70
}
