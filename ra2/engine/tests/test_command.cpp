// NET L1: the command layer (engine/command.hpp) -- queue order, scheduling,
// the text form, and every command type's apply against a real World.
#include "doctest.h"

#include <algorithm>
#include <string>

#include "asset_fixture.hpp"
#include "command.hpp"
#include "selection.hpp"
#include "world.hpp"

using namespace ra2engine;

namespace {

Command cmd(std::uint32_t frame, int house, std::uint32_t seq) {
    Command c;
    c.type = EventType::Idle;
    c.frame = frame;
    c.house = static_cast<std::int8_t>(house);
    c.seq = seq;
    return c;
}

UnitCombat gi() {
    UnitCombat c;
    c.strength = 125;
    c.armor = "none";
    c.image = "E1";
    c.damage = 15;
    c.rof = 20;
    c.range = 4.0;
    c.verses = {100, 80, 80, 50, 25, 25, 75, 50, 25, 100, 100};
    return c;
}

// A free walkable need x need block away from the map edge.
bool open_area(const World& w, int need, int& x, int& y) {
    for (int yy = 20; yy < w.grid_h() - 20; ++yy)
        for (int xx = 20; xx < w.grid_w() - 20; ++xx) {
            if (xx + yy <= w.grid_w() + 4) continue;
            bool ok = true;
            for (int dy = 0; dy < need && ok; ++dy)
                for (int dx = 0; dx < need && ok; ++dx)
                    ok = w.walkable(xx + dx, yy + dy) && w.unit_at_cell(xx + dx, yy + dy) < 0 &&
                         w.building_at_cell(xx + dx, yy + dy) < 0;
            if (ok) { x = xx; y = yy; return true; }
        }
    return false;
}

int add_unit(World& w, int x, int y, int house, UnitCombat c = gi()) {
    w.add_combat_unit(x, y, house, c);
    return static_cast<int>(w.units().size()) - 1;
}

// Records every scene-side call the executor makes.
struct MockHost : CommandHost {
    OrderRules rules;
    std::vector<std::string> calls;
    const OrderRules* order_rules() const override { return &rules; }
    void produce(int h, int r, int t, bool n) override { log("produce", h, r, t, n); }
    void suspend(int h, int r, int t, bool n) override { log("suspend", h, r, t, n); }
    void abandon(int h, int r, int t, bool n) override { log("abandon", h, r, t, n); }
    void abandon_all(int h, int r, int t, bool n) override { log("abandon_all", h, r, t, n); }
    void place(int h, int r, int t, int cx, int cy) override {
        calls.push_back("place " + std::to_string(h) + " " + std::to_string(r) + " " + std::to_string(t) + " " +
                        std::to_string(cx) + "," + std::to_string(cy));
    }
    void selection_notify(int h, const std::vector<int>& ids) override {
        std::string s = "select " + std::to_string(h);
        for (int i : ids) s += " " + std::to_string(i);
        calls.push_back(s);
    }
    void log(const char* what, int h, int r, int t, bool n) {
        calls.push_back(std::string(what) + " " + std::to_string(h) + " " + std::to_string(r) + " " +
                        std::to_string(t) + " " + (n ? "1" : "0"));
    }
};

#define NEED_MAP()                                                           \
    if (!ra2fixture::mapsmd03_available()) {                                 \
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR"); \
        return;                                                              \
    }

}  // namespace

// ---------------------------------------------------------------------------
// numbering / payload model
// ---------------------------------------------------------------------------

TEST_CASE("command: event numbering is the original EventType table (0x81F8FC)") {
    CHECK(static_cast<int>(EventType::PowerOn) == 0x01);
    CHECK(static_cast<int>(EventType::Ally) == 0x03);
    CHECK(static_cast<int>(EventType::MegaMission) == 0x04);
    CHECK(static_cast<int>(EventType::Idle) == 0x06);
    CHECK(static_cast<int>(EventType::Scatter) == 0x07);
    CHECK(static_cast<int>(EventType::Deploy) == 0x09);
    CHECK(static_cast<int>(EventType::Place) == 0x0B);
    CHECK(static_cast<int>(EventType::Produce) == 0x0E);
    CHECK(static_cast<int>(EventType::Suspend) == 0x0F);
    CHECK(static_cast<int>(EventType::Abandon) == 0x10);
    CHECK(static_cast<int>(EventType::Primary) == 0x11);
    CHECK(static_cast<int>(EventType::SpecialPlace) == 0x12);
    CHECK(static_cast<int>(EventType::Repair) == 0x15);
    CHECK(static_cast<int>(EventType::Sell) == 0x16);
    CHECK(static_cast<int>(EventType::SellCell) == 0x17);
    CHECK(static_cast<int>(EventType::Archive) == 0x1E);
    CHECK(static_cast<int>(EventType::AllCheer) == 0x2D);
    CHECK(static_cast<int>(EventType::AbandonAll) == 0x2E);
    CHECK(static_cast<int>(Mission::Move) == 2);
    CHECK(static_cast<int>(Mission::AreaGuard) == 11);
    CHECK(std::string(event_name(EventType::MegaMission)) == "MEGAMISSION");
}

TEST_CASE("command: a cell Target packs CellStruct (x | y << 16), negatives too") {
    const Target t = Target::cell(123, 45);
    CHECK(t.is_cell());
    CHECK(t.cell_x() == 123);
    CHECK(t.cell_y() == 45);
    const Target n = Target::cell(-1, -2);
    CHECK(n.cell_x() == -1);
    CHECK(n.cell_y() == -2);
    CHECK(Target::building(7).is_building());
    CHECK_FALSE(Target{}.is_unit());
}

// ---------------------------------------------------------------------------
// queue + scheduler
// ---------------------------------------------------------------------------

TEST_CASE("command queue: apply order is frame, then house, then sequence") {
    CommandQueue q;
    q.push(cmd(5, 1, 0));
    q.push(cmd(3, 2, 7));
    q.push(cmd(3, 0, 9));
    q.push(cmd(3, 0, 2));
    q.push(cmd(4, 0, 0));
    q.push(cmd(3, 2, 1));
    const std::vector<Command> due = q.take_due(3);
    REQUIRE(due.size() == 4);
    CHECK((due[0].house == 0 && due[0].seq == 2));
    CHECK((due[1].house == 0 && due[1].seq == 9));
    CHECK((due[2].house == 2 && due[2].seq == 1));
    CHECK((due[3].house == 2 && due[3].seq == 7));
    CHECK(q.size() == 2);   // frames 4 and 5 stay queued
    CHECK(q.take_due(3).empty());
    const std::vector<Command> rest = q.take_due(100);
    REQUIRE(rest.size() == 2);
    CHECK(rest[0].frame == 4);
    CHECK(rest[1].frame == 5);
    CHECK(q.empty());
}

TEST_CASE("command queue: equal keys keep their push order (stable)") {
    CommandQueue q;
    for (int k = 0; k < 5; ++k) {
        Command c = cmd(1, 0, 0);
        c.value = k;
        q.push(c);
    }
    const std::vector<Command> due = q.take_due(1);
    REQUIRE(due.size() == 5);
    for (int k = 0; k < 5; ++k) CHECK(due[static_cast<std::size_t>(k)].value == k);
}

TEST_CASE("command queue: a late command for a past frame applies at the next take") {
    CommandQueue q;
    q.push(cmd(2, 0, 0));
    CHECK(q.take_due(1).empty());
    CHECK(q.take_due(7).size() == 1);   // frame <= now: due
}

TEST_CASE("command scheduler: stamps house, frame = now + delay, increasing sequence") {
    CommandQueue q;
    CommandScheduler s(q, 3, 0);
    s.set_now(10);
    CHECK(s.schedule(Command{EventType::Idle}) == 10);   // delay 0: the very next tick
    CHECK(s.schedule(Command{EventType::Scatter}) == 10);
    s.set_delay(4);
    s.set_now(11);
    CHECK(s.schedule(Command{EventType::Deploy}) == 15);
    const std::vector<Command> due = q.take_due(15);
    REQUIRE(due.size() == 3);
    CHECK(due[0].type == EventType::Idle);
    CHECK(due[1].type == EventType::Scatter);
    CHECK(due[2].type == EventType::Deploy);
    for (const Command& c : due) CHECK(c.house == 3);
    CHECK(due[0].seq < due[1].seq);
    CHECK(due[1].seq < due[2].seq);
}

TEST_CASE("command: the replay text form round-trips every field") {
    Command c;
    c.type = EventType::MegaMission;
    c.house = 2;
    c.frame = 123456;
    c.seq = 77;
    c.whom = {{Rtti::Infantry, 4}, {Rtti::Unit, 9}, {Rtti::Building, 3}};
    c.mission = Mission::Attack;
    c.target = Target::cell(-3, 250);
    c.dest = Target::building(11);
    c.rtti_type = static_cast<int>(Rtti::UnitType);
    c.heap_id = 42;
    c.is_naval = 1;
    c.cx = 5; c.cy = -6; c.cx2 = 7; c.cy2 = 8;
    c.value = -99;
    c.ext = {1, 2, 3};
    Command back;
    REQUIRE(command_from_line(command_to_line(c), back));
    CHECK(back == c);
    Command bad;
    CHECK_FALSE(command_from_line("X 1 2 3", bad));
}

TEST_CASE("selection: ids ascending, toggle, count, lowest") {
    Selection s;
    s.set(5);
    s.set(1);
    s.set(3);
    s.toggle(3);
    CHECK(s.ids() == std::vector<int>{1, 5});
    CHECK(s.lowest() == 1);
    CHECK(s.count() == 2);
    CHECK_FALSE(s.has(-1));
    CHECK_FALSE(s.has(100));
    s.clear();
    CHECK(s.empty());
}

// ---------------------------------------------------------------------------
// apply: every command type
// ---------------------------------------------------------------------------

TEST_CASE("command apply: MEGAMISSION Move paths the issuer's units only, marks W4-SQ") {
    NEED_MAP();
    World w(ra2fixture::mapsmd03_map());
    int x = 0, y = 0;
    REQUIRE(open_area(w, 6, x, y));
    const int a = add_unit(w, x, y, 0), b = add_unit(w, x + 1, y, 0), e = add_unit(w, x + 2, y, 1);
    (void)w.take_player_orders();
    Command c = make_mega(w, {a, b, e}, Mission::Move, Target{}, Target::cell(x + 4, y + 4));
    c.house = 0;
    apply_command(w, c);
    CHECK_FALSE(w.units()[a].path.empty());
    CHECK_FALSE(w.units()[b].path.empty());
    CHECK(w.units()[e].path.empty());   // not the issuer's: ignored
    const std::vector<int> marked = w.take_player_orders();
    CHECK(std::count(marked.begin(), marked.end(), a) == 1);
    // The same command from house 1 moves only its own unit.
    c.house = 1;
    c.dest = Target::cell(x + 5, y);
    apply_command(w, c);
    CHECK_FALSE(w.units()[e].path.empty());
    // The selection prune drops a boarded / consumed unit, keeps the rest.
    Selection s;
    s.set(a);
    s.set(b);
    w.units()[b].cargo_of = a;
    s.prune(w);
    CHECK(s.ids() == std::vector<int>{a});
}

TEST_CASE("command apply: MEGAMISSION Attack on a unit, a building and a cell") {
    NEED_MAP();
    World w(ra2fixture::mapsmd03_map());
    int x = 0, y = 0;
    REQUIRE(open_area(w, 8, x, y));
    const int a = add_unit(w, x, y, 0);
    const int foe = add_unit(w, x + 3, y, 1);
    const int bi = w.add_building(-1, x + 5, y + 5, 2, 2, 1, 500, 0, 0.0, true);
    apply_command(w, make_mega(w, {a}, Mission::Attack, Target::unit(w, foe), Target{}));
    CHECK(w.units()[a].target == foe);
    CHECK(w.units()[a].attack_order);
    // A friendly target is refused.
    const int pal = add_unit(w, x + 1, y + 1, 0);
    apply_command(w, make_mega(w, {a}, Mission::Attack, Target::unit(w, pal), Target{}));
    CHECK(w.units()[a].target == foe);
    apply_command(w, make_mega(w, {a}, Mission::Attack, Target::building(bi), Target{}));
    CHECK(w.units()[a].target_building == bi);
    apply_command(w, make_mega(w, {a}, Mission::Attack, Target::cell(x + 2, y + 2), Target{}));
    CHECK(w.units()[a].attack_cell_x == x + 2);
    CHECK(w.units()[a].attack_cell_y == y + 2);
}

TEST_CASE("command apply: IDLE stops, SCATTER steps aside, MEGAMISSION AreaGuard guards") {
    NEED_MAP();
    World w(ra2fixture::mapsmd03_map());
    int x = 0, y = 0;
    REQUIRE(open_area(w, 8, x, y));
    const int a = add_unit(w, x + 3, y + 3, 0);
    const int foe = add_unit(w, x + 6, y + 6, 1);
    apply_command(w, make_mega(w, {a}, Mission::Attack, Target::unit(w, foe), Target{}));
    REQUIRE(w.units()[a].target == foe);
    apply_command(w, make_whom(w, EventType::Idle, {a}));
    CHECK(w.units()[a].target == -1);
    CHECK_FALSE(w.units()[a].attack_order);
    apply_command(w, make_mega(w, {a}, Mission::AreaGuard, Target{}, Target{}));
    CHECK(w.units()[a].tg.area_guard);
    apply_command(w, make_whom(w, EventType::Scatter, {a}));
    CHECK_FALSE(w.units()[a].path.empty());   // stepping to a free neighbour
    // Another house's IDLE does not touch house 0's unit.
    apply_command(w, make_mega(w, {a}, Mission::Attack, Target::unit(w, foe), Target{}));
    Command other = make_whom(w, EventType::Idle, {a});
    other.house = 1;
    apply_command(w, other);
    CHECK(w.units()[a].target == foe);
}

TEST_CASE("command apply: MEGAMISSION Harvest / Enter (dock) drive the issuer's harvesters") {
    NEED_MAP();
    World w(ra2fixture::mapsmd03_map());
    int x = 0, y = 0;
    REQUIRE(open_area(w, 8, x, y));
    w.ore().set(x + 4, y + 4, OreKind::Ore, kMaxOreDensity, 0);
    w.add_harvester(x, y, 0, gi(), false, 40);
    const int h = static_cast<int>(w.units().size()) - 1;
    apply_command(w, make_mega(w, {h}, Mission::Harvest, Target::cell(x + 4, y + 4), Target{}));
    CHECK(w.units()[h].harv == HarvState::MoveToOre);
    CHECK(w.units()[h].harv_cell_x == x + 4);
    // Dock: Mission_Enter on an own refinery -> ReturnToRef.
    const int ref = w.add_building(9, x + 6, y, 2, 2, 0, 500, 0, 0.0, true);
    MockHost host;
    host.rules.refinery_types = {9};
    apply_command(w, make_mega(w, {h}, Mission::Enter, Target::building(ref), Target{}), &host);
    CHECK(w.units()[h].harv == HarvState::ReturnToRef);
}

TEST_CASE("command apply: MEGAMISSION Enter on a transport boards the issuer's infantry") {
    NEED_MAP();
    World w(ra2fixture::mapsmd03_map());
    int x = 0, y = 0;
    REQUIRE(open_area(w, 8, x, y));
    UnitCombat t = gi();
    t.image = "MTNK";
    t.is_vehicle = true;
    const int tr = add_unit(w, x + 3, y + 3, 0, t);
    w.units()[tr].passengers_max = 5;
    w.units()[tr].size_limit = 5;
    const int p = add_unit(w, x, y, 0);
    apply_command(w, make_mega(w, {p}, Mission::Enter, Target::unit(w, tr), Target{}));
    CHECK(w.units()[p].enter_target == tr);
}

TEST_CASE("command apply: MEGAMISSION Capture (engineer) and Sabotage (spy)") {
    NEED_MAP();
    World w(ra2fixture::mapsmd03_map());
    int x = 0, y = 0;
    REQUIRE(open_area(w, 10, x, y));
    UnitCombat eng = gi();
    eng.image = "ENGINEER";
    const int e = add_unit(w, x, y, 0, eng);
    const int bi = w.add_building(-1, x + 5, y + 5, 2, 2, 1, 500, 0, 0.0, true);   // untyped: capturable
    apply_command(w, make_mega(w, {e}, Mission::Capture, Target::building(bi), Target{}));
    CHECK(w.units()[e].capture_target == bi);
    // W7-01-04: every engineer of a group acts (not only a lone one).
    const int e2 = add_unit(w, x + 1, y, 0, eng);
    apply_command(w, make_mega(w, {e, e2}, Mission::Capture, Target::building(bi), Target{}));
    CHECK(w.units()[e2].capture_target == bi);
    UnitCombat spyc = gi();
    spyc.image = "SPY";
    const int s = add_unit(w, x, y + 2, 0, spyc);
    const int bt = w.add_building(4, x + 5, y + 1, 2, 2, 1, 500, 0, 0.0, true);
    MockHost host;
    host.rules.infiltrate_purposes.assign(10, World::InfiltratePurpose::None);
    host.rules.infiltrate_purposes[4] = World::InfiltratePurpose::Refinery;
    apply_command(w, make_mega(w, {s}, Mission::Sabotage, Target::building(bt), Target{}), &host);
    CHECK(w.units()[s].infiltrate_target == bt);
    CHECK(w.units()[s].infiltrate_purpose == static_cast<int>(World::InfiltratePurpose::Refinery));
}

TEST_CASE("command apply: DEPLOY sets unload on a loaded transport; deploy-fire infantry digs in") {
    NEED_MAP();
    World w(ra2fixture::mapsmd03_map());
    int x = 0, y = 0;
    REQUIRE(open_area(w, 8, x, y));
    UnitCombat g = gi();
    g.deploy_fire = true;
    const int d = add_unit(w, x, y, 0, g);
    Command c;
    c.type = EventType::Deploy;
    c.whom = {Target::unit(w, d)};
    apply_command(w, c);
    CHECK(w.units()[d].deployed);
    UnitCombat t = gi();
    t.image = "MTNK";
    t.is_vehicle = true;
    const int tr = add_unit(w, x + 3, y + 3, 0, t);
    w.units()[tr].passengers_max = 5;
    const int rider = w.add_unit_in_transport(tr, 0, gi());
    REQUIRE(rider >= 0);
    Command u;
    u.type = EventType::Deploy;
    u.whom = {Target::unit(w, tr)};
    apply_command(w, u);
    CHECK(w.units()[tr].unload_pending);
}

TEST_CASE("command apply: building events -- POWERON/OFF, REPAIR, SELL, SELLCELL, PRIMARY, ARCHIVE") {
    NEED_MAP();
    World w(ra2fixture::mapsmd03_map());
    int x = 0, y = 0;
    REQUIRE(open_area(w, 10, x, y));
    std::vector<BuildingTypeDef> types(3);
    types[0].power = -50;     // a consumer
    types[0].cost = 1000;
    types[0].strength = 500;
    types[1].cost = 300;      // a factory type
    types[1].strength = 500;
    types[2].cost = 100;
    types[2].strength = 100;
    w.set_building_types(types);
    const int b = w.add_building(0, x, y, 2, 2, 0, 500, 0, /*buildup=*/10.0, true);
    apply_command(w, make_building_event(EventType::PowerOff, b));
    CHECK(w.buildings()[b].power_off);
    Command foreign = make_building_event(EventType::PowerOn, b);
    foreign.house = 1;   // not the owner: refused
    apply_command(w, foreign);
    CHECK(w.buildings()[b].power_off);
    apply_command(w, make_building_event(EventType::PowerOn, b));
    CHECK_FALSE(w.buildings()[b].power_off);
    w.buildings()[b].hp = 200;
    apply_command(w, make_building_event(EventType::Repair, b));
    CHECK(w.buildings()[b].being_repaired);
    apply_command(w, make_building_event(EventType::Repair, b));   // toggles off
    CHECK_FALSE(w.buildings()[b].being_repaired);
    apply_command(w, make_building_event(EventType::Sell, b));
    CHECK(w.buildings()[b].selling);
    const int wall = w.add_building(2, x + 6, y, 1, 1, 0, 100, 0, 0.0, true);
    Command sc;
    sc.type = EventType::SellCell;
    sc.cx = static_cast<std::int16_t>(x + 6);
    sc.cy = static_cast<std::int16_t>(y);
    apply_command(w, sc);
    // No buildup art: the instant-removal branch (the wall case).
    CHECK((!w.buildings()[wall].alive || w.buildings()[wall].state == Building::State::Dead));
    // PRIMARY within the factory group, ARCHIVE = rally point.
    const int f1 = w.add_building(1, x, y + 4, 2, 2, 0, 500, 0, 0.0, true);
    const int f2 = w.add_building(1, x + 3, y + 4, 2, 2, 0, 500, 0, 0.0, true);
    MockHost host;
    host.rules.veh_factory_types = {1};
    apply_command(w, make_building_event(EventType::Primary, f2), &host);
    CHECK(w.buildings()[f2].primary);
    CHECK_FALSE(w.buildings()[f1].primary);
    Command ar = make_building_event(EventType::Archive, f1);
    ar.target = Target::cell(x + 1, y + 8);
    apply_command(w, ar);
    CHECK(w.buildings()[f1].has_rally);
    CHECK(w.buildings()[f1].rally_x == x + 1);
    CHECK(w.buildings()[f1].rally_y == y + 8);
}

TEST_CASE("command apply: production / placement / select reach the scene host") {
    NEED_MAP();
    World w(ra2fixture::mapsmd03_map());
    MockHost host;
    const Command p = make_type_event(EventType::Produce, Rtti::InfantryType, 3, false);
    apply_command(w, p, &host);
    apply_command(w, make_type_event(EventType::Suspend, Rtti::UnitType, 4, true), &host);
    apply_command(w, make_type_event(EventType::Abandon, Rtti::BuildingType, 5, false), &host);
    apply_command(w, make_type_event(EventType::AbandonAll, Rtti::UnitType, 6, false), &host);
    Command pl = make_type_event(EventType::Place, Rtti::BuildingType, 7, false);
    pl.cx = 10;
    pl.cy = 11;
    apply_command(w, pl, &host);
    Command sn;
    sn.type = EventType::SelectNotify;
    sn.ext = {2, 5};
    apply_command(w, sn, &host);
    REQUIRE(host.calls.size() == 6);
    CHECK(host.calls[0] == "produce 0 16 3 0");
    CHECK(host.calls[1] == "suspend 0 40 4 1");
    CHECK(host.calls[2] == "abandon 0 7 5 0");
    CHECK(host.calls[3] == "abandon_all 0 40 6 0");
    CHECK(host.calls[4] == "place 0 7 7 10,11");
    CHECK(host.calls[5] == "select 0 2 5");
    // No host: the scene-side events are inert, never a crash.
    apply_command(w, p, nullptr);
}

TEST_CASE("command apply: ALLY toggles the sender's view, ALLCHEER cheers, SPECIAL_PLACE without a SW is refused") {
    NEED_MAP();
    World w(ra2fixture::mapsmd03_map());
    int x = 0, y = 0;
    REQUIRE(open_area(w, 4, x, y));
    // W9-05-03: three combatants (MakeAlly never allies the last enemy) and
    // EVENT_ALLY is ONE-WAY (house 0's own view of house 1).
    for (int h = 0; h < 3; ++h) {
        World::HouseRec rec;
        rec.alliance_team = h;
        w.set_house(h, rec);
    }
    const bool before = w.are_allied(0, 1);
    Command al;
    al.type = EventType::Ally;
    al.value = 1;
    apply_command(w, al);
    CHECK(w.are_allied(0, 1) != before);
    CHECK_FALSE(w.are_allied(1, 0));
    apply_command(w, al);
    CHECK(w.are_allied(0, 1) == before);
    const int a = add_unit(w, x, y, 0);
    apply_command(w, Command{EventType::AllCheer});
    CHECK(w.units()[a].ia.cheer);
    Command sw;
    sw.type = EventType::SpecialPlace;
    sw.value = 1;
    sw.cx = static_cast<std::int16_t>(x);
    sw.cy = static_cast<std::int16_t>(y);
    apply_command(w, sw);   // no superweapon granted: nothing fires, no crash
    CHECK(w.units()[a].alive);
}

TEST_CASE("command apply: stale ids (out of range, dead) are skipped") {
    NEED_MAP();
    World w(ra2fixture::mapsmd03_map());
    int x = 0, y = 0;
    REQUIRE(open_area(w, 4, x, y));
    const int a = add_unit(w, x, y, 0);
    w.units()[a].alive = false;
    apply_command(w, make_mega(w, {a, 9999, -5}, Mission::Move, Target{}, Target::cell(x + 2, y)));
    CHECK(w.units()[a].path.empty());
    apply_command(w, make_building_event(EventType::Sell, 12345));
    apply_command(w, make_building_event(EventType::Primary, -1));
}

// ---------------------------------------------------------------------------
// the wire bridge (engine/net/command_bridge.hpp)
// ---------------------------------------------------------------------------
#include "net/command_bridge.hpp"

TEST_CASE("command bridge: a Whom run goes out as one record per object and comes back as one Command") {
    Command c;
    c.type = EventType::MegaMission;
    c.house = 1;
    c.frame = 40;
    c.mission = Mission::Move;
    c.whom = {{Rtti::Infantry, 3}, {Rtti::Unit, 8}, {Rtti::Infantry, 12}};
    c.dest = Target::cell(77, 101);
    const std::vector<ra2::net::NetCommand> recs = ra2::net::to_net(c);
    REQUIRE(recs.size() == 3);
    CHECK(recs[1].whom.id == 8);
    CHECK(recs[1].mission == 2);
    Command stop;
    stop.type = EventType::Idle;
    stop.house = 1;
    stop.frame = 40;
    stop.whom = {{Rtti::Infantry, 3}};
    std::vector<ra2::net::NetCommand> all = recs;
    for (const auto& r : ra2::net::to_net(stop)) all.push_back(r);
    const std::vector<Command> back = ra2::net::from_net(all);
    REQUIRE(back.size() == 2);
    CHECK(back[0].type == EventType::MegaMission);
    CHECK(back[0].whom == c.whom);
    CHECK(back[0].dest == c.dest);
    CHECK(back[0].house == 1);
    CHECK(back[0].frame == 40);
    CHECK(back[1].type == EventType::Idle);
    CHECK(back[1].whom.size() == 1);
}

TEST_CASE("command bridge: the Chronosphere's two picks pair up; SW index is the binary index") {
    Command c;
    c.type = EventType::SpecialPlace;
    c.value = static_cast<int>(SuperweaponType::ChronoSphere);
    c.cx = 10; c.cy = 11; c.cx2 = 40; c.cy2 = 41;
    const auto recs = ra2::net::to_net(c);
    REQUIRE(recs.size() == 2);
    CHECK(recs[0].value == 3);   // binary ChronoSphere
    CHECK(recs[1].value == 4);   // binary ChronoWarp
    const auto back = ra2::net::from_net(recs);
    REQUIRE(back.size() == 1);
    CHECK(back[0].value == c.value);
    CHECK((back[0].cx2 == 40 && back[0].cy2 == 41));
    Command sel;
    sel.type = EventType::SelectNotify;
    CHECK(ra2::net::to_net(sel).empty());   // clone-local, never on the wire
}

TEST_CASE("command bridge: to_event / from_event round-trip through the EventClass codec record") {
    Command c;
    c.type = EventType::Sell;
    c.house = 2;
    c.frame = 99;
    c.whom = {Target::building(17)};
    ra2::net::Event e;
    REQUIRE(ra2::net::to_event(c, e));
    CHECK(e.type == static_cast<std::uint8_t>(EventType::Sell));
    Command back;
    REQUIRE(ra2::net::from_event(e, back));
    CHECK(back.type == EventType::Sell);
    CHECK(back.house == 2);
    CHECK(back.frame == 99);
    REQUIRE(back.whom.size() == 1);
    CHECK(back.whom[0] == Target::building(17));
    Command p = make_type_event(EventType::Produce, Rtti::InfantryType, 5, false);
    p.house = 1;
    REQUIRE(ra2::net::to_event(p, e));
    REQUIRE(ra2::net::from_event(e, back));
    CHECK(back.heap_id == 5);
    CHECK(back.rtti_type == static_cast<int>(Rtti::InfantryType));
    Command run = c;
    run.type = EventType::Idle;
    run.whom = {{Rtti::Infantry, 1}, {Rtti::Infantry, 2}};
    CHECK_FALSE(ra2::net::to_event(run, e));       // two records
    CHECK(ra2::net::to_events(run).size() == 2);
}

// W9-05-12: a command that arrived from the network is bounds-checked before
// it reaches the World (forged / corrupt events are dropped, never applied).
TEST_CASE("command: valid_net_command bounds-checks houses, ids and cells (W9-05-12)") {
    NEED_MAP();
    World w(ra2fixture::mapsmd03_map());
    int x = 0, y = 0;
    REQUIRE(open_area(w, 2, x, y));
    add_unit(w, x, y, 0);
    Command m = make_mega(w, {0}, Mission::Move, Target{}, Target::cell(x + 1, y));
    m.house = 0;
    CHECK(valid_net_command(w, m));
    Command bad_house = m;
    bad_house.house = 40;
    CHECK_FALSE(valid_net_command(w, bad_house));
    Command bad_unit = m;
    bad_unit.whom.push_back(Target{Rtti::Unit, 999});
    CHECK_FALSE(valid_net_command(w, bad_unit));
    Command bad_cell = m;
    bad_cell.dest = Target::cell(w.grid_w() + 10, 3);
    CHECK_FALSE(valid_net_command(w, bad_cell));
    Command bad_bld = make_building_event(EventType::Sell, 77);
    bad_bld.house = 0;
    CHECK_FALSE(valid_net_command(w, bad_bld));
    Command place = make_type_event(EventType::Place, Rtti::BuildingType, 0, false);
    place.house = 0;
    place.cx = -3;
    place.cy = 4;
    CHECK_FALSE(valid_net_command(w, place));
    place.cx = x;
    place.cy = y;
    CHECK(valid_net_command(w, place));
}
