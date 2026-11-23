// gF MCV deploy / undeploy -- World-level behaviour on the real mapsmd03 map
// (install-gated, the test_world precedent) [RE: docs/re/mcv-deploy.md].
#include "doctest.h"

#include "asset_fixture.hpp"
#include "building.hpp"
#include "world.hpp"

using namespace ra2engine;

namespace {
using Ev = World::DeployEvent;

BuildingTypeDef conyard_def() {
    BuildingTypeDef d;
    d.id = "GACNST";
    d.fw = 4;
    d.fh = 4;
    d.strength = 1000;
    d.conyard = true;
    d.deploy_facing = 0x80;
    return d;
}

UnitCombat mcv_combat() {
    UnitCombat c;
    c.image = "MCV";
    c.strength = 1000;
    c.armor = "heavy";
    c.is_vehicle = true;
    c.rot = 5;
    c.deploys_into = "GACNST";
    return c;
}

bool open_rect(const World& w, int W, int H, int& x0, int& y0) {
    for (int y = 6; y + H < w.grid_h() - 6; ++y)
        for (int x = 6; x + W < w.grid_w() - 6; ++x) {
            bool ok = true;
            for (int dy = 0; dy < H && ok; ++dy)
                for (int dx = 0; dx < W && ok; ++dx)
                    if (!w.walkable(x + dx, y + dy) || w.unit_at_cell(x + dx, y + dy) >= 0) ok = false;
            if (ok) { x0 = x; y0 = y; return true; }
        }
    return false;
}

int run_until_deployed(World& w, int frames) {
    for (int f = 0; f < frames; ++f) {
        w.step(1.0 / 15.0);
        for (const Ev& e : w.take_deploy_events())
            if (e.kind == Ev::Kind::Deployed) return e.building;
    }
    return -1;
}
}  // namespace

#define MCV_WORLD(world, x0, y0)                                            \
    if (!ra2fixture::mapsmd03_available()) {                                 \
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR"); \
        return;                                                              \
    }                                                                        \
    World world(ra2fixture::mapsmd03_map());                                 \
    world.set_building_types({conyard_def()});                               \
    int x0 = 0, y0 = 0;                                                      \
    REQUIRE(open_rect(world, 12, 12, x0, y0));

TEST_CASE("mcv: DeploysInto resolves to the roster index; plain units stay -1") {
    MCV_WORLD(w, x0, y0);
    w.add_combat_unit(x0 + 5, y0 + 5, 0, mcv_combat());
    UnitCombat gi = mcv_combat();
    gi.deploys_into.clear();
    w.add_combat_unit(x0 + 1, y0 + 1, 0, gi);
    CHECK(w.units()[0].deploys_into == 0);
    CHECK(w.is_mcv(0));
    CHECK(w.units()[1].deploys_into == -1);
    CHECK_FALSE(w.is_mcv(1));
    CHECK_FALSE(w.order_deploy_mcv(1));
}

TEST_CASE("mcv: deploy turns to DeployFacing then unfolds at cell+(-1,-1) with buildup") {
    MCV_WORLD(w, x0, y0);
    w.set_buildup_totals({2.0});
    w.add_combat_unit(x0 + 5, y0 + 5, 0, mcv_combat());
    w.units()[0].body_facing = 0.0;   // facing north: must turn to 0x80 first
    w.units()[0].hp = 500;
    REQUIRE(w.order_deploy_mcv(0));
    w.step(1.0 / 15.0);
    CHECK(w.buildings().empty());     // still turning (ROT 5 cannot do 180 deg in a frame)
    const int b = run_until_deployed(w, 300);
    REQUIRE(b >= 0);
    const Building& cy = w.buildings()[(std::size_t)b];
    CHECK(cy.cx == x0 + 4);
    CHECK(cy.cy == y0 + 4);
    CHECK(cy.state == Building::State::Buildup);
    CHECK(cy.hp == 500);              // health ratio carried (500/1000 of 1000)
    CHECK_FALSE(w.units()[0].alive);
    CHECK(w.units()[0].deployed_away);
    CHECK(w.unit_at_cell(x0 + 5, y0 + 5) < 0);
}

TEST_CASE("mcv: blocked spot -> drives to the first legal ring offset, then deploys") {
    MCV_WORLD(w, x0, y0);
    w.add_combat_unit(x0 + 5, y0 + 5, 0, mcv_combat());
    UnitCombat blocker = mcv_combat();
    blocker.deploys_into.clear();
    w.add_combat_unit(x0 + 6, y0 + 6, 0, blocker);   // inside the foundation
    REQUIRE(w.order_deploy_mcv(0));
    CHECK(w.units()[0].deploy_mission == 2);
    const int b = run_until_deployed(w, 600);
    REQUIRE(b >= 0);
    // The ring's first legal entry for a blocker at (+1,+1): (0,-1) moves the
    // foundation to rows y0+3..y0+6 which still contains (x0+6,y0+6); (0,-2)
    // -> rows y0+2..y0+5, clear.
    CHECK(w.buildings()[(std::size_t)b].cy == y0 + 2);
    CHECK(w.buildings()[(std::size_t)b].cx == x0 + 4);
}

TEST_CASE("mcv: no legal spot anywhere -> CannotDeploy event, nothing placed") {
    MCV_WORLD(w, x0, y0);
    BuildingTypeDef huge = conyard_def();
    huge.fw = 60;
    huge.fh = 60;
    w.set_building_types({huge});
    w.add_combat_unit(x0 + 5, y0 + 5, 0, mcv_combat());
    CHECK_FALSE(w.order_deploy_mcv(0));
    const auto ev = w.take_deploy_events();
    REQUIRE(ev.size() == 1);
    CHECK(ev[0].kind == Ev::Kind::CannotDeploy);
    CHECK(w.buildings().empty());
}

TEST_CASE("mcv: firewall -- a world that never deploys is untouched") {
    MCV_WORLD(w, x0, y0);
    w.add_combat_unit(x0 + 5, y0 + 5, 0, mcv_combat());
    for (int f = 0; f < 30; ++f) w.step(1.0 / 15.0);
    CHECK(w.units()[0].alive);
    CHECK(w.buildings().empty());
    CHECK(w.take_deploy_events().empty());
}

TEST_CASE("mcv: ConYard sold by a human in a skirmish with MCVRedeploys packs into the MCV") {
    MCV_WORLD(w, x0, y0);
    w.set_buildup_totals({1.0});
    w.set_undeploy_unit(0, mcv_combat());
    w.set_skirmish_session(true);
    w.set_mcv_redeploys(true);
    const int b = w.add_building(0, x0 + 2, y0 + 2, 4, 4, 0, 1000, 0, 1.0, /*instant=*/true);
    w.buildings()[(std::size_t)b].hp = 250;
    const double before = w.house_credits(0);
    REQUIRE(w.begin_sell(b, 0));
    std::vector<Ev> got;
    for (int f = 0; f < 40; ++f) {
        w.step(1.0 / 15.0);
        for (const Ev& e : w.take_deploy_events()) got.push_back(e);
    }
    REQUIRE(got.size() == 1);
    CHECK(got[0].kind == Ev::Kind::Undeployed);
    const Unit& u = w.units()[(std::size_t)got[0].unit];
    CHECK(u.cell_x == x0 + 3);
    CHECK(u.cell_y == y0 + 3);
    CHECK(u.hp == 250);
    CHECK(u.deploys_into == 0);
    CHECK(w.house_credits(0) == before);   // no refund
}

TEST_CASE("mcv: without the skirmish session / option the ConYard sale is a plain refund") {
    MCV_WORLD(w, x0, y0);
    w.set_undeploy_unit(0, mcv_combat());
    const int b = w.add_building(0, x0 + 2, y0 + 2, 4, 4, 0, 1000, 0, 0.0, /*instant=*/true);
    REQUIRE(w.begin_sell(b, 0));
    for (int f = 0; f < 5; ++f) w.step(1.0 / 15.0);
    CHECK(w.take_deploy_events().empty());
}
