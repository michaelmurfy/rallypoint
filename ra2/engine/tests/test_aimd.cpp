#include "doctest.h"
#include "../engine/aimd_parse.hpp"
#include "ra2assets/ini.hpp"
#include "ra2assets/mix.hpp"
#include "asset_fixture.hpp"
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
using namespace ra2engine;

namespace {
// Real aimd.ini shapes [RE-GATE APPLIED: ai-team.md §1 — index sections
// (index=id) + numbered entity entries; ids/values here are fixture stand-ins
// in the recovered FORMAT].
const char* kAimdFixture =
    "[TaskForces]\n0=01000050-G\n1=01000051-G\n2=09999999-G\n"
    "[ScriptTypes]\n0=03000015-G\n"
    "[TeamTypes]\n0=02000010-G\n1=02000011-G\n2=02000012-G\n"
    "[01000050-G]\nName=Rhino Pair\nGroup=-1\n0=2,HTNK\n1=1,E2\n"
    "[01000051-G]\nName=Ghost Force\n0=1,ZZNOART\n"   // unknown UNITID -> dropped
    "[03000015-G]\nName=March And Attack\n0=3,2\n1=0,1\n2=49,0\n"
    "[02000010-G]\nName=Sov Rhino Rush\nHouse=<none>\nPriority=7\nMax=2\n"
    "TaskForce=01000050-G\nScript=03000015-G\nAutocreate=yes\nFull=no\nWaypoint=C\n"
    "Aggressive=yes\nReinforce=no\nSuicide=no\nTechLevel=5\nIsBaseDefense=no\n"
    "[02000011-G]\nName=Ghost Team\nHouse=Russians\nTaskForce=01000051-G\n"
    "Script=03000015-G\nAutocreate=yes\n"              // rides the dropped TF
    "[02000012-G]\nName=No Script\nHouse=Russians\nTaskForce=01000050-G\n"
    "Script=09999999-G\nAutocreate=yes\n";             // missing script -> dropped

// Test resolver: HTNK->0, E2->1, everything else unknown (-1).
int test_unit_index(const std::string& id) {
    if (id == "HTNK") return 0;
    if (id == "E2") return 1;
    return -1;
}
}  // namespace

TEST_CASE("aimd: waypoint_from_letter decodes the letter encoding") {
    CHECK(waypoint_from_letter("A") == 0);
    CHECK(waypoint_from_letter("C") == 2);
    CHECK(waypoint_from_letter("Z") == 25);
    CHECK(waypoint_from_letter("AA") == 26);   // [RE-GATE APPLIED: ai-team.md §1]
    CHECK(waypoint_from_letter("") == -1);
    CHECK(waypoint_from_letter("7") == -1);
}

TEST_CASE("aimd: parse_aimd walks the index sections and numbered entries") {
    ra2assets::Ini ini = ra2assets::Ini::parse(kAimdFixture);
    const AimdData d = parse_aimd(ini, test_unit_index);
    // TaskForces: Rhino Pair kept, Ghost Force art-guard-dropped, the
    // dangling index entry 2=09999999-G (no section) skipped.
    REQUIRE(d.taskforces.size() == 1);
    CHECK(d.taskforces[0].id == "01000050-G");
    CHECK(d.taskforces[0].name == "Rhino Pair");
    CHECK(d.taskforces[0].group == -1);
    REQUIRE(d.taskforces[0].members.size() == 2);
    CHECK(d.taskforces[0].members[0].count == 2);   // 0=2,HTNK
    CHECK(d.taskforces[0].members[0].unit == 0);
    CHECK(d.taskforces[0].members[1].count == 1);   // 1=1,E2
    CHECK(d.taskforces[0].members[1].unit == 1);
    // ScriptTypes: all actions kept verbatim (inert split is the interpreter's).
    REQUIRE(d.scripts.size() == 1);
    REQUIRE(d.scripts[0].actions.size() == 3);
    CHECK(d.scripts[0].actions[0].action == 3);     // 0=3,2
    CHECK(d.scripts[0].actions[0].argument == 2);
    CHECK(d.scripts[0].actions[1].action == 0);     // 1=0,1
    CHECK(d.scripts[0].actions[1].argument == 1);
    CHECK(d.scripts[0].actions[2].action == 49);    // 2=49,0
    // TeamTypes: only the team whose TaskForce AND Script survived.
    REQUIRE(d.teams.size() == 1);
    const TeamType& t = d.teams[0];
    CHECK(t.id == "02000010-G");
    CHECK(t.house == "<none>");   // universal in stock data [RE: ai-team.md §1]
    CHECK(t.priority == 7);
    CHECK(t.max == 2);
    CHECK(t.taskforce == 0);
    CHECK(t.script == 0);
    CHECK(t.autocreate);
    CHECK_FALSE(t.full);
    CHECK(t.waypoint == 2);   // Waypoint=C
    // New load-bearing / informational TeamType fields [RE: ai-team.md §1]:
    CHECK(t.aggressive);
    CHECK_FALSE(t.reinforce);
    CHECK_FALSE(t.suicide);
    CHECK(t.tech_level == 5);
    CHECK_FALSE(t.is_base_defense);
}

TEST_CASE("aimd: parse_aimd is deterministic") {
    ra2assets::Ini ini = ra2assets::Ini::parse(kAimdFixture);
    const AimdData a = parse_aimd(ini, test_unit_index);
    const AimdData b = parse_aimd(ini, test_unit_index);
    REQUIRE(a.teams.size() == b.teams.size());
    REQUIRE(a.taskforces.size() == b.taskforces.size());
    for (std::size_t i = 0; i < a.teams.size(); ++i) CHECK(a.teams[i].id == b.teams[i].id);
}

namespace {
// Real [AITriggerTypes] flat-line shapes [RE-GATE APPLIED: ai-trigger.md §1 —
// the CONFIRMED 18-field comma order (Name,Team1,OwnerHouse,TechLevel,
// ConditionType,ConditionObject,Conditions,StartWeight,MinWeight,MaxWeight,
// IsForSkirmish,0,SideIndex,IsForBaseDefense,Team2,eE,eN,eH); ids/values are
// fixture stand-ins in the recovered FORMAT; comparator blobs are the 64-hex
// Conditions[4]: VALUE dword first, then the operator dword, LE, slots 1..3
// zero [RE: ai-trigger.md §2]].
const char* kTriggerFixture =
    "[TaskForces]\n0=01000050-G\n"
    "[ScriptTypes]\n0=03000015-G\n"
    "[TeamTypes]\n0=02000010-G\n"
    "[01000050-G]\nName=Rhino Pair\n0=2,HTNK\n"
    "[03000015-G]\nName=March\n0=3,2\n1=49,0\n"
    "[02000010-G]\nName=Sov Response\nHouse=<none>\nTaskForce=01000050-G\n"
    "Script=03000015-G\nAutocreate=no\n"
    "[AITriggerTypes]\n"
    // Kept: enemy owns >= 3 GAWEAP -> create 02000010-G (building object).
    "SOVT01=Sov Rhino Response,02000010-G,Russians,5,0,GAWEAP,"
    "0300000003000000000000000000000000000000000000000000000000000000,"
    "50.000000,10.000000,90.000000,1,0,2,0,02000010-G,1,1,1\n"
    // Dropped: condition object ZZNOART resolves in NEITHER roster.
    "SOVT02=Ghost Object,02000010-G,Russians,5,0,ZZNOART,"
    "0100000003000000000000000000000000000000000000000000000000000000,"
    "50.000000,10.000000,90.000000,1,0,2,0,<none>,1,1,1\n"
    // Dropped: team1 09999999-G does not exist in AimdData::teams.
    "SOVT03=Ghost Team,09999999-G,Russians,5,1,HTNK,"
    "0100000003000000000000000000000000000000000000000000000000000000,"
    "50.000000,10.000000,90.000000,1,0,2,0,<none>,1,1,1\n"
    // Kept: owner owns < 2 HTNK (unit object); team2 <none> -> -1;
    // Normal-only enable fields; not-for-skirmish; side any.
    "SOVT04=Owner Owns Tanks,02000010-G,<all>,5,1,HTNK,"
    "0200000000000000000000000000000000000000000000000000000000000000,"
    "30.000000,0.000000,60.000000,0,0,0,0,<none>,0,1,0\n"
    // [AITriggerTypesEnable]: SOVT04 disabled per-trigger; SOVT03 names a
    // DROPPED trigger -> ignored, no crash; SOVT01 unlisted -> stays enabled
    // [RE-GATE APPLIED: ai-trigger.md §5].
    "[AITriggerTypesEnable]\nSOVT04=no\nSOVT03=yes\n";

// Building resolver: GAWEAP -> 7, everything else unknown (-1).
int test_building_index(const std::string& id) {
    return id == "GAWEAP" ? 7 : -1;
}
}  // namespace

TEST_CASE("aimd: decode_comparator unpacks the value-first Conditions[4] blob") {
    ra2engine::TriggerComparator c;
    // VALUE-FIRST [RE-GATE APPLIED: ai-trigger.md §2 — CONFIRMED]: dword0 =
    // count/value N, dword1 = operator; 3 (>=) is the stock default op.
    REQUIRE(decode_comparator(
        "0300000003000000000000000000000000000000000000000000000000000000", c));
    CHECK(c.value == 3);
    CHECK(c.op == 3);                                    // 3 = >=
    REQUIRE(decode_comparator(
        "0a00000005000000000000000000000000000000000000000000000000000000", c));
    CHECK(c.value == 10);
    CHECK(c.op == 5);                                    // 5 = !=
    // Reserved dwords [2..7] are hex-validated but IGNORED for meaning.
    REQUIRE(decode_comparator(
        "0100000000000000ffffffff000000000000000000000000000000000000ffff", c));
    CHECK(c.value == 1);
    CHECK(c.op == 0);                                    // 0 = <
    CHECK_FALSE(decode_comparator("0a00000005000000", c));  // 16 chars: NOT a
                                                     // full 32-byte blob
    CHECK_FALSE(decode_comparator("03000000", c));       // too short
    CHECK_FALSE(decode_comparator(                       // non-hex
        "zz00000003000000000000000000000000000000000000000000000000000000", c));
}

TEST_CASE("aimd: parse_ai_triggers walks the flat list with art-guard-drop") {
    ra2assets::Ini ini = ra2assets::Ini::parse(kTriggerFixture);
    ra2engine::AimdData d = parse_aimd(ini, test_unit_index);
    REQUIRE(d.teams.size() == 1);                        // 02000010-G survived
    parse_ai_triggers(ini, d, test_unit_index, test_building_index);
    // SOVT02 (unresolvable object) and SOVT03 (missing team1) dropped WHOLE.
    REQUIRE(d.triggers.size() == 2);
    const ra2engine::AITriggerType& a = d.triggers[0];
    CHECK(a.id == "SOVT01");
    CHECK(a.name == "Sov Rhino Response");
    CHECK(a.team1 == 0);
    CHECK(a.team2 == 0);                                 // second team, ALSO
                                                         // created [RE: §3]
    CHECK(a.owner_house == "Russians");
    CHECK(a.tech_level == 5);
    CHECK(a.condition_type == 0);                        // enemy-owns
    CHECK(a.condition_object.kind == ra2engine::TechnoKind::Building);
    CHECK(a.condition_object.index == 7);                // GAWEAP
    CHECK(a.comparator.value == 3);
    CHECK(a.comparator.op == 3);
    CHECK(a.starting_weight == 50.0);
    CHECK(a.min_weight == 10.0);
    CHECK(a.max_weight == 90.0);
    CHECK(a.is_for_skirmish);
    CHECK(a.side == 2);
    CHECK_FALSE(a.base_defense);
    CHECK(a.enabled[0]); CHECK(a.enabled[1]); CHECK(a.enabled[2]);
    CHECK(a.is_enabled);                                 // unlisted in the
                                                         // Enable section ->
                                                         // default enabled
    const ra2engine::AITriggerType& b = d.triggers[1];
    CHECK(b.id == "SOVT04");
    CHECK(b.condition_type == 1);                        // owner-owns
    CHECK(b.condition_object.kind == ra2engine::TechnoKind::Unit);
    CHECK(b.condition_object.index == 0);                // HTNK
    CHECK(b.comparator.value == 2);
    CHECK(b.comparator.op == 0);                         // <
    CHECK(b.team2 == -1);                                // <none> tolerated
    CHECK_FALSE(b.is_for_skirmish);
    CHECK(b.side == 0);
    CHECK_FALSE(b.enabled[0]); CHECK(b.enabled[1]); CHECK_FALSE(b.enabled[2]);
    CHECK_FALSE(b.is_enabled);                           // [AITriggerTypesEnable]
                                                         // SOVT04=no applied
                                                         // [RE: ai-trigger.md §5]
}

TEST_CASE("aimd: parse_ai_triggers is deterministic") {
    ra2assets::Ini ini = ra2assets::Ini::parse(kTriggerFixture);
    ra2engine::AimdData a = parse_aimd(ini, test_unit_index);
    ra2engine::AimdData b = parse_aimd(ini, test_unit_index);
    parse_ai_triggers(ini, a, test_unit_index, test_building_index);
    parse_ai_triggers(ini, b, test_unit_index, test_building_index);
    REQUIRE(a.triggers.size() == b.triggers.size());
    for (std::size_t i = 0; i < a.triggers.size(); ++i)
        CHECK(a.triggers[i].id == b.triggers[i].id);
}

TEST_CASE("aimd: real aimd.ini parses with resolved cross-references") {
    using namespace ra2assets;
    // Borrow the shared, load-once ra2md.mix (asset_fixture) instead of
    // re-slurping the ~196 MB archive; aimd.ini is resolved via the SAME
    // nested-candidate walk. This case keeps its own parse_aimd/parse_ai_triggers
    // (its actual subject) under the permissive resolvers after the shared read.
    if (!ra2fixture::assets_available()) { MESSAGE("skipped: ra2md.mix not found"); return; }
    auto bytes = ra2fixture::resolve_nested_bytes("aimd.ini");
    if (bytes.empty()) {
        MESSAGE("skipped: aimd.ini not found in ra2md.mix top level or in "
                "localmd.mix/expandmd*.mix/cachemd.mix/conqmd.mix");
        return;
    }

    ra2assets::Ini ini = ra2assets::Ini::parse(std::string(bytes.begin(), bytes.end()));
    REQUIRE(ini.has("TeamTypes"));
    REQUIRE(ini.has("TaskForces"));
    REQUIRE(ini.has("ScriptTypes"));
    // Permissive resolver (every UNITID "known") -> structure-only smoke:
    const AimdData d = parse_aimd(ini, [](const std::string&) { return 0; });
    CHECK(d.taskforces.size() > 50);   // the real file carries hundreds
    CHECK(d.scripts.size() > 50);
    CHECK(d.teams.size() > 50);
    int autocreate = 0;
    for (const TeamType& t : d.teams) {
        REQUIRE(t.taskforce >= 0);
        REQUIRE(t.taskforce < (int)d.taskforces.size());
        REQUIRE(t.script >= 0);
        REQUIRE(t.script < (int)d.scripts.size());
        if (t.autocreate) ++autocreate;
    }
    CHECK(autocreate > 0);   // the autocreate pool AI-2 draws from is nonempty
    MESSAGE("real aimd: tf=" << d.taskforces.size() << " sc=" << d.scripts.size()
            << " teams=" << d.teams.size() << " autocreate=" << autocreate);

    // AI-3: the real [AITriggerTypes] parses under permissive resolvers.
    ra2engine::AimdData dt = d;
    parse_ai_triggers(ini, dt, [](const std::string&) { return 0; },
                      [](const std::string&) { return 0; });
    CHECK(dt.triggers.size() > 50);   // the real file carries hundreds
    for (const ra2engine::AITriggerType& t : dt.triggers) {
        REQUIRE(t.team1 >= 0);
        REQUIRE(t.team1 < (int)dt.teams.size());
        REQUIRE(t.max_weight >= t.min_weight);
    }
    MESSAGE("real aimd: triggers=" << dt.triggers.size());
}
