// W9-05 (w9fix-match): the in-match multiplayer layer -- one-way ALLY
// (MakeAlly / MakeEnemy / Is_Allowed_To_Ally), alliance victory, DESTRUCT,
// BuildOffAlly, the beacon packets, and the "no UI path mutates the World"
// source tripwire (W9-05-01).
#include "doctest.h"

#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "../engine/building.hpp"
#include "../engine/command.hpp"
#include "../engine/net/global_msg.hpp"
#include "../engine/world.hpp"

using ra2engine::Command;
using ra2engine::EventType;
using ra2engine::World;
using AC = World::AllyChange;

namespace {

// Three FFA combatant houses (0 human, 1 and 2 not), one building each.
void w9_ffa(World& w, int n = 3) {
    for (int h = 0; h < n; ++h) w.set_house(h, World::HouseRec{World::Side::Allied, h, "", -1, true});
    w.enable_victory(true, /*short_game=*/true);
    for (int h = 0; h < n; ++h) w.add_building(0, 2 + 10 * h, 2, 1, 1, h, 100, 0, 0.0, true);
}
void w9_kill_house(World& w, int h) {
    for (auto& b : w.buildings())
        if (b.house == h) ra2engine::kill_building(b);
}
Command w9_ally(int from, int to) {
    Command c;
    c.type = EventType::Ally;
    c.house = static_cast<std::int8_t>(from);
    c.value = to;
    return c;
}

}  // namespace

TEST_CASE("W9-05-03: ALLY is one-way (MakeAlly FUN_004F9990), mutual on the reply, war breaks both") {
    ra2assets::MapFile map;
    World w(map);
    w9_ffa(w);
    CHECK(w.toggle_alliance(0, 1) == AC::Requested);
    CHECK(w.allied_with(0, 1));
    CHECK_FALSE(w.allied_with(1, 0));   // one way only
    CHECK(w.allied_with(0, 0));
    CHECK(w.toggle_alliance(1, 0) == AC::Formed);
    CHECK(w.allied_with(1, 0));
    CHECK(w.are_allied(0, 1));
    CHECK(w.are_allied(1, 0));
    // House 2 is not dragged along.
    CHECK_FALSE(w.allied_with(0, 2));
    CHECK_FALSE(w.allied_with(2, 0));
    // Is_Allowed_To_Ally FUN_00501360: never ally the last enemy.
    CHECK_FALSE(w.can_ally(0, 2));
    CHECK(w.toggle_alliance(0, 2) == AC::None);
    CHECK_FALSE(w.allied_with(0, 2));
    // MakeEnemy FUN_004F9DB0: the war is mutual.
    CHECK(w.toggle_alliance(1, 0) == AC::BrokenMutual);
    CHECK_FALSE(w.allied_with(1, 0));
    CHECK_FALSE(w.allied_with(0, 1));
    // One-way break: 0 allies 1, then 0 declares war -> only 0's bit existed.
    CHECK(w.toggle_alliance(0, 1) == AC::Requested);
    CHECK(w.toggle_alliance(0, 1) == AC::Broken);
    CHECK_FALSE(w.allied_with(0, 1));
    // A neutral / non-combatant house cannot be allied.
    World w2(map);
    w9_ffa(w2);
    w2.set_house(3, World::HouseRec{World::Side::Neutral, 3, "", -1, false});
    CHECK_FALSE(w2.can_ally(0, 3));
}

TEST_CASE("W9-05-03: the ALLY command applies MakeAlly / MakeEnemy and respects AllyChangeAllowed") {
    ra2assets::MapFile map;
    World w(map);
    w9_ffa(w);
    struct Host : ra2engine::CommandHost {
        std::vector<AC> seen;
        void diplomacy(int, int, AC ch) override { seen.push_back(ch); }
    } host;
    ra2engine::apply_command(w, w9_ally(0, 1), &host);
    ra2engine::apply_command(w, w9_ally(1, 0), &host);
    REQUIRE(host.seen.size() == 2u);
    CHECK(host.seen[0] == AC::Requested);
    CHECK(host.seen[1] == AC::Formed);
    // Team Game (MPTeamMD.ini AllyChangeAllowed=no): locked.
    w.set_diplomacy_rules(true, false);
    ra2engine::apply_command(w, w9_ally(0, 1), &host);
    CHECK(host.seen.size() == 2u);
    CHECK(w.allied_with(0, 1));
    CHECK_FALSE(w.alliance_toggle_allowed(0, 1));
    // Free For All (AlliesAllowed=no): no alliances at all.
    w.set_diplomacy_rules(false, true);
    CHECK_FALSE(w.alliance_toggle_allowed(0, 2));
    // ToggleAlliance FUN_007309C0: the target must be a human house.
    w.set_diplomacy_rules(true, true);
    w.set_human(1, false);
    CHECK_FALSE(w.alliance_toggle_allowed(0, 1));
    w.set_human(1, true);
    CHECK(w.alliance_toggle_allowed(0, 1));
}

TEST_CASE("W9-05-04: the game ends when every living house is MUTUALLY allied (FUN_004FBED0 tail)") {
    ra2assets::MapFile map;
    World w(map);
    w9_ffa(w);
    w.set_human(1, true);
    w.toggle_alliance(0, 1);
    w.toggle_alliance(1, 0);
    w.step_victory();
    CHECK_FALSE(w.game_over());   // 2 is alive and an enemy
    w9_kill_house(w, 2);
    w.step_victory();
    CHECK(w.house_defeated(2));
    CHECK(w.game_over());
    for (int f = 0; f < 3; ++f) w.step(1.0 / 15.0);
    CHECK(w.house_outcome(0) == 1);   // both allies win, different lobby teams
    CHECK(w.house_outcome(1) == 1);
    CHECK(w.house_outcome(2) == -1);
}

TEST_CASE("W9-05-04: a one-way alliance is no shared victory; the check runs on a defeat only") {
    ra2assets::MapFile map;
    World w(map);
    w9_ffa(w, 4);
    w.set_human(1, true);
    w.toggle_alliance(0, 1);          // 1 never answers
    w9_kill_house(w, 2);
    w.step_victory();
    CHECK(w.house_defeated(2));
    CHECK_FALSE(w.game_over());       // 0, 1, 3 still enemies
    w.toggle_alliance(1, 0);          // now 0+1 mutual, but 3 lives
    w9_kill_house(w, 3);
    w.step_victory();
    CHECK(w.game_over());
    // With the third party gone, 1 can no longer answer (Is_Allowed_To_Ally:
    // never ally the last enemy) -- the match goes on to a winner.
    World v(map);
    w9_ffa(v);
    v.set_human(1, true);
    v.toggle_alliance(0, 1);
    w9_kill_house(v, 2);
    v.step_victory();
    CHECK_FALSE(v.game_over());
    CHECK(v.toggle_alliance(1, 0) == AC::None);
    v.step_victory();
    CHECK_FALSE(v.game_over());
}

TEST_CASE("W9-05-04: no human left alive ends the game; the last-team rule still holds without a matrix") {
    ra2assets::MapFile map;
    World w(map);
    w9_ffa(w);                         // house 0 is the only human
    w9_kill_house(w, 0);
    w.step_victory();
    CHECK(w.game_over());
    // Teams (no pair matrix): 0+2 vs 1 -> killing 1 ends it, team 0 wins.
    World t(map);
    t.set_house(0, World::HouseRec{World::Side::Allied, 0, "", 0, true});
    t.set_house(1, World::HouseRec{World::Side::Soviet, 1, "", 1, true});
    t.set_house(2, World::HouseRec{World::Side::Allied, 2, "", 0, true});
    t.enable_victory(true, true);
    for (int h = 0; h < 3; ++h) t.add_building(0, 2 + 10 * h, 2, 1, 1, h, 100, 0, 0.0, true);
    t.step_victory();
    CHECK(t.winning_team() == -1);
    w9_kill_house(t, 1);
    t.step_victory();
    CHECK(t.game_over());
    CHECK(t.winning_team() == 0);
}

TEST_CASE("W9-05-06: DESTRUCT resigns the issuing house (Leave / Observe)") {
    ra2assets::MapFile map;
    World w(map);
    w9_ffa(w);
    Command d;
    d.type = EventType::Destruct;
    d.house = 1;
    ra2engine::apply_command(w, d);
    CHECK(w.house_defeated(1));
    CHECK_FALSE(w.house_defeated(0));
    w.step_victory();
    CHECK_FALSE(w.game_over());       // 0 and 2 fight on
}

TEST_CASE("W9-05-05: observer_reveal shows a defeated human the whole map") {
    ra2assets::MapFile map;
    World w(map);
    CHECK_FALSE(w.observer_reveal());
    w.enable_observer_reveal(true);
    CHECK(w.observer_reveal());
}

TEST_CASE("W9-05-09: BuildOffAlly -- an allied EligibileForAllyBuilding building extends the base") {
    const int W = 40, H = 40;
    std::vector<std::uint8_t> pass(static_cast<std::size_t>(W * H), 1);
    ra2engine::Occupancy occ(W, H);
    std::vector<ra2engine::Building> blds(1);
    blds[0].alive = true;
    blds[0].house = 1;   // the ally's ConYard
    blds[0].type = 0;
    blds[0].cx = 10;
    blds[0].cy = 10;
    blds[0].fw = 2;
    blds[0].fh = 2;
    blds[0].state = ra2engine::Building::State::Idle;
    const std::vector<char> allied = {0, 1, 0}, not_allied = {0, 0, 0};
    const std::vector<char> eligible = {1}, ineligible = {0};
    auto check = [&](const std::vector<char>* a, const std::vector<char>* e) {
        ra2engine::AllyBuild ab{a, e};
        return ra2engine::placement_check(13, 10, 1, 1, /*house=*/0, /*adjacent=*/2, pass, W, H, occ, blds, false,
                                          nullptr, nullptr, a ? &ab : nullptr)
            .ok;
    };
    CHECK_FALSE(check(nullptr, nullptr));        // option off: own buildings only
    CHECK(check(&allied, &eligible));            // allied ConYard
    CHECK_FALSE(check(&not_allied, &eligible));  // not an ally
    CHECK_FALSE(check(&allied, &ineligible));    // EligibileForAllyBuilding=no
}

TEST_CASE("W9-05-08: beacon packets round-trip in the original layout (global 0x20 / 0x21 / 0x22)") {
    using namespace ra2::net;
    BeaconMsg p;
    p.cmd = kGlobalBeaconPlace;
    p.sender = "Hosty";
    p.x = 20 * 256 + 128;
    p.y = 31 * 256 + 128;
    p.slot = 2;
    p.house = 1;
    const std::vector<uint8_t> b = encode_beacon_msg(p);
    CHECK(b.size() == kBeaconPacketSize);
    CHECK(b[0] == 0x20);
    CHECK(b[0x3B] == 2);
    CHECK(b[0x3C] == 1);
    BeaconMsg q;
    REQUIRE(decode_beacon_msg(b, q));
    CHECK(q.sender == "Hosty");
    CHECK(q.x == p.x);
    CHECK(q.y == p.y);
    CHECK(q.slot == 2);
    BeaconMsg t;
    t.cmd = kGlobalBeaconMessage;
    t.slot = 1;
    t.house = 0;
    t.text = utf8_to_u16(std::string(200, 'x'));
    const std::vector<uint8_t> tb = encode_beacon_msg(t);
    CHECK(tb.size() == kGlobalPacketSize);
    REQUIRE(decode_beacon_msg(tb, q));
    CHECK(q.cmd == kGlobalBeaconMessage);
    CHECK(q.text.size() == kBeaconTextMax);   // 127 + NUL
    CHECK(q.slot == 1);
    BeaconMsg d;
    d.cmd = kGlobalBeaconDelete;
    d.slot = 0;
    std::vector<uint8_t> db = encode_beacon_msg(d);
    REQUIRE(decode_beacon_msg(db, q));
    CHECK(q.cmd == kGlobalBeaconDelete);
    db[0x3B] = 7;   // W9-05-12: a slot outside 0..2 is refused
    CHECK_FALSE(decode_beacon_msg(db, q));
}

#ifdef RA2_SOURCE_DIR
// W9-05-01 tripwire: the UI (mouse, keyboard, touch, hotkeys, pages) builds
// commands; it never changes the simulation itself. A direct World mutator
// call in these files is a desync in a network match (the touch Cancel was
// one: touch_ui.inc called world_.econ_abandon on the local peer only).
TEST_CASE("W9-05-01: no UI source calls a World mutator (every action goes through a command)") {
    static const char* const kUi[] = {
        "touch_ui.inc", "touch_bar.inc", "game_scene_controls.inc", "input.cpp", "input_original.cpp",
        "touch_gestures.cpp", "hotkeys.cpp", "ingame_pages.inc", "game_scene_mp.inc", "hud_tooltip.inc",
    };
    const std::regex banned(
        R"(world_?\.(econ_(produce|suspend|abandon|place|place_building)|group_(?!occupy_count|can_enter)[a-z_]+|order_(?!issuer)[a-z_]+|set_(?!order_issuer|diplomacy_rules)[a-z_]+|)"
        R"(add_[a-z_]+|begin_sell|toggle_repair|set_building_power|set_primary|set_rally|resign_house|)"
        R"(deposit_credits|grant_credits_for_test|fire_superweapon_at|make_ally|make_enemy|toggle_alliance|)"
        R"(set_pair_alliance|set_ally_view|reveal_[a-z_]+|unload_occupants|kill_building|campaign_[a-z_]+|)"
        R"(make_house_cheer|garrison_attack|mark_player_orders|enable_(?!observer_reveal)[a-z_]+|step[a-z_]*)\s*\()");
    int hits = 0;
    for (const char* f : kUi) {
        std::ifstream in(std::string(RA2_SOURCE_DIR) + "/engine/" + f);
        REQUIRE_MESSAGE(in.good(), std::string(f));
        std::string line;
        int ln = 0;
        while (std::getline(in, line)) {
            ++ln;
            const std::size_t c = line.find("//");
            const std::string code = c == std::string::npos ? line : line.substr(0, c);
            // Allowed: world_.set_order_issuer (the issuer bookkeeping of a
            // query) and the scene's game-start setup in game_scene_mp.inc
            // (set_diplomacy_rules / enable_observer_reveal, before frame 0).
            if (std::regex_search(code, banned)) {
                ++hits;
                const std::string msg = std::string(f) + ":" + std::to_string(ln) + " mutates the World from the UI: " + line;
                FAIL_CHECK(msg);
            }
        }
    }
    CHECK(hits == 0);
}
#endif
