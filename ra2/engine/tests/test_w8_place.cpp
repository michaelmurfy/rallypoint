// w8fix-place: wave-8 root causes 7 and 8 -- building placement legality
// (W8-03-01, CanThisExistHere FUN_0047C530), rock/fence overlay land
// (W8-03-02, FUN_0047D1C0), garrison exits (W8-06-07 / W8-02-04,
// UnloadOccupants FUN_00457DD0) and the height gate on exits from a dead
// host (W8-03-10). Pure checks plus World checks on the real mapsmd03 map
// (install-gated, the test_mcv_deploy precedent).
#include "doctest.h"

#include "asset_fixture.hpp"
#include "building.hpp"
#include "ore.hpp"
#include "overlay_land.hpp"
#include "world.hpp"

using namespace ra2engine;

namespace {

Building mkb(int cx, int cy, int fw, int fh) {
    Building b;
    b.cx = cx; b.cy = cy; b.fw = fw; b.fh = fh; b.house = 0;
    b.state = Building::State::Idle; b.hp = b.max_hp = 100;
    return b;
}

const bool kBuildable[12] = {true, true, false, false, false, false, false, true, false, false, false, false};

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

UnitCombat gi_combat() {
    UnitCombat c;
    c.image = "E1";
    c.strength = 125;
    c.armor = "none";
    c.occupier = true;
    return c;
}

bool open_rect(const World& w, int W, int H, int& x0, int& y0) {
    for (int y = 6; y + H < w.grid_h() - 6; ++y)
        for (int x = 6; x + W < w.grid_w() - 6; ++x) {
            bool ok = true;
            const int l0 = w.cell_level(x, y);
            for (int dy = 0; dy < H && ok; ++dy)
                for (int dx = 0; dx < W && ok; ++dx)
                    if (!w.walkable(x + dx, y + dy) || w.unit_at_cell(x + dx, y + dy) >= 0 ||
                        w.ore().has_ore(x + dx, y + dy) ||
                        w.cell_level(x + dx, y + dy) != l0)
                        ok = false;
            if (ok) { x0 = x; y0 = y; return true; }
        }
    return false;
}

}  // namespace

#define W8_WORLD(world, x0, y0)                                              \
    if (!ra2fixture::mapsmd03_available()) {                                 \
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR"); \
        return;                                                              \
    }                                                                        \
    World world(ra2fixture::mapsmd03_map());                                 \
    int x0 = 0, y0 = 0;                                                      \
    REQUIRE(open_rect(world, 14, 14, x0, y0));

// ------------------------------------------------------------ W8-03-01 pure

TEST_CASE("w8place W8-03-01: buildable_static refuses overlay, slope, bridge and unbuildable land") {
    // 6 cells: clear / ore-free overlay / ramp / bridge / Water / Rough.
    const std::vector<std::uint8_t> land = {0, 0, 0, 0, 2, 7};
    const std::vector<std::uint8_t> ovl = {0, 1, 0, 0, 0, 0};
    const std::vector<std::uint8_t> ramp = {0, 0, 3, 0, 0, 0};
    const std::vector<std::uint8_t> bridge = {0, 0, 0, 1, 0, 0};
    const auto g = buildable_static(land, ovl, ramp, bridge, kBuildable);
    REQUIRE(g.size() == 6);
    CHECK(g[0] == 1);
    CHECK(g[1] == 0);
    CHECK(g[2] == 0);
    CHECK(g[3] == 0);
    CHECK(g[4] == 0);
    CHECK(g[5] == 1);   // [Rough] Buildable=yes
}

TEST_CASE("w8place W8-03-01: placement_check with the mask -- ore refuses a building, not a wall") {
    const int W = 10, H = 10;
    std::vector<std::uint8_t> pass((size_t)W * H, 1), stat((size_t)W * H, 1);
    Occupancy occ(W, H);
    std::vector<Building> bs = {mkb(0, 0, 2, 2)};
    OreField ore(W, H);
    ore.set(5, 5, OreKind::Ore, 3, 0);
    stat[(size_t)7 * W + 2] = 0;   // a slope / overlay cell
    BuildMask bm{&stat, &ore, false};
    BuildMask wall{&stat, &ore, true};
    // Legacy test (no mask): ore and the static hole pass.
    CHECK(placement_check(4, 4, 2, 2, 0, 0, pass, W, H, occ, bs, true).ok);
    CHECK(placement_check(2, 7, 1, 1, 0, 0, pass, W, H, occ, bs, true).ok);
    // Masked: a 2x2 with one ore cell fails, and flags that cell.
    const PlacementCheck c = placement_check(4, 4, 2, 2, 0, 0, pass, W, H, occ, bs, true, nullptr, &bm);
    CHECK_FALSE(c.ok);
    REQUIRE(c.cell_ok.size() == 4);
    CHECK(c.cell_ok[0]);
    CHECK_FALSE(c.cell_ok[3]);   // (5,5) the ore
    CHECK(placement_check(6, 6, 2, 2, 0, 0, pass, W, H, occ, bs, true, nullptr, &bm).ok);
    CHECK_FALSE(placement_check(2, 7, 1, 1, 0, 0, pass, W, H, occ, bs, true, nullptr, &bm).ok);
    // A wall may stand on ore (FUN_0047C530: Wall=yes over a Tiberium overlay),
    // never on the static hole.
    CHECK(placement_check(5, 5, 1, 1, 0, 0, pass, W, H, occ, bs, true, nullptr, &wall).ok);
    CHECK_FALSE(placement_check(2, 7, 1, 1, 0, 0, pass, W, H, occ, bs, true, nullptr, &wall).ok);
}

// ------------------------------------------------------------ W8-03-02 pure

TEST_CASE("w8place W8-03-02: overlay land table -- rocks and fences are Rock, crates keep the tile") {
    const std::vector<std::string> names = {"SROCK01", "FENCE01", "CRAT01", "TRACKS01", "TIB01",
                                            "BRIDGE1", "RUBBLE_OVERLAY", "LOBRDG01", "GAWALL"};
    auto get = [](const std::string& n, const std::string& k) -> std::string {
        if (n == "SROCK01" || n == "FENCE01") {
            if (k == "Land") return "Rock";
            if (k == "NoUseTileLandType") return "true";
        }
        if (n == "CRAT01" && k == "Land") return "Rock";
        if (n == "TRACKS01" && k == "Land") return "Railroad";
        if (n == "TIB01" && k == "Tiberium") return "yes";
        if (n == "BRIDGE1" && k == "NoUseTileLandType") return "false";
        if (n == "RUBBLE_OVERLAY" && k == "NoUseTileLandType") return "true";
        if (n == "LOBRDG01") {
            if (k == "Land") return "Road";
            if (k == "NoUseTileLandType") return "true";
        }
        if (n == "GAWALL" && k == "Wall") return "yes";
        return "";
    };
    const auto tab = overlay_land_table(names, get);
    REQUIRE(tab.size() == names.size());
    CHECK(tab[0].land == 3);    // SROCK01 -> Rock
    CHECK(tab[1].land == 3);    // FENCE01 -> Rock
    CHECK(tab[2].land == -1);   // CRAT01: Land=Rock without NoUseTileLandType -> the tile wins
    CHECK(tab[3].land == 9);    // TRACKS01 -> Railroad
    CHECK(tab[4].land == -1);   // ore: the tile wins (Tiberium land is live)
    CHECK(tab[4].tiberium);
    CHECK(tab[5].land == -1);   // the high-bridge deck keeps the tile (Water)
    CHECK(tab[6].land == 0);    // RUBBLE_OVERLAY -> Clear
    CHECK(tab[7].land == 1);    // LOBRDG01 -> Road
    CHECK(tab[8].land == -1);   // walls: the clone's wall model

    // Apply on a 4x2 grid (land Clear, level 0; cell (3,1) has a 4-level-higher
    // back neighbour (3,0)).
    std::vector<int> lt(8, 0), ovl(8, -1), lvl(8, 0);
    ovl[0] = 0;   // SROCK01
    ovl[1] = 2;   // CRAT01
    ovl[2] = 7;   // LOBRDG01 (skipped: depth_set_terrain's override)
    ovl[7] = 3;   // TRACKS01 under the cliff -> Rock
    lvl[3] = 4;
    apply_overlay_land(lt, ovl, lvl, 4, 2, tab, 2, [](int id) { return id == 7; });
    CHECK(lt[0] == 3);
    CHECK(lt[1] == 0);
    CHECK(lt[2] == 0);
    CHECK(lt[7] == 3);
    ovl[7] = 3;
    lvl[3] = 0;
    lt.assign(8, 0);
    apply_overlay_land(lt, ovl, lvl, 4, 2, tab, 2, nullptr);
    CHECK(lt[7] == 9);   // no cliff: Railroad
    CHECK(lt[2] == 1);   // without the skip a low bridge reads Road
}

// --------------------------------------------------------- W8-03-01 World

TEST_CASE("w8place W8-03-01: an MCV refuses ore and an unbuildable cell, and relocates by the ring") {
    W8_WORLD(w, x0, y0);
    w.set_building_types({conyard_def()});
    std::vector<std::uint8_t> stat((size_t)w.grid_w() * w.grid_h(), 1);
    w.set_build_static(stat);
    w.add_combat_unit(x0 + 6, y0 + 6, 0, mcv_combat());
    // Legal on clean ground.
    CHECK(w.can_deploy_at(0, x0 + 6, y0 + 6));
    // One ore cell under the 4x4 foundation (origin (x0+5,y0+5)) refuses it.
    w.ore().set(x0 + 7, y0 + 7, OreKind::Ore, 3, 0);
    CHECK_FALSE(w.can_deploy_at(0, x0 + 6, y0 + 6));
    w.ore().set(x0 + 7, y0 + 7, OreKind::None, 0, 0);
    CHECK(w.can_deploy_at(0, x0 + 6, y0 + 6));
    // A slope / overlay cell (static 0) refuses it too.
    stat[(size_t)(y0 + 8) * w.grid_w() + (x0 + 8)] = 0;
    w.set_build_static(stat);
    CHECK_FALSE(w.can_deploy_at(0, x0 + 6, y0 + 6));
    // The order walks the recovered 36-offset ring to the first legal spot.
    REQUIRE(w.order_deploy_mcv(0));
    CHECK(w.units()[0].deploy_mission == 2);
}

TEST_CASE("w8place W8-03-01: an MCV with no buildable spot in the ring cannot deploy") {
    W8_WORLD(w, x0, y0);
    w.set_building_types({conyard_def()});
    std::vector<std::uint8_t> stat((size_t)w.grid_w() * w.grid_h(), 0);   // nothing buildable
    w.set_build_static(stat);
    w.add_combat_unit(x0 + 6, y0 + 6, 0, mcv_combat());
    CHECK_FALSE(w.order_deploy_mcv(0));
    const auto ev = w.take_deploy_events();
    REQUIRE(ev.size() == 1);
    CHECK(ev[0].kind == World::DeployEvent::Kind::CannotDeploy);
    CHECK(w.buildings().empty());
}

// ------------------------------------------------- W8-06-07 / W8-02-04

namespace {
BuildingTypeDef garrison_def() {
    BuildingTypeDef d;
    d.id = "CABHUT";
    d.fw = 2;
    d.fh = 2;
    d.strength = 400;
    d.can_be_occupied = true;
    d.can_occupy_fire = true;
    d.max_occupants = 5;
    return d;
}
}  // namespace

TEST_CASE("w8place W8-06-07: the exit is the first enterable border cell (bottom-right first)") {
    W8_WORLD(w, x0, y0);
    w.set_building_types({garrison_def()});
    const int bx = x0 + 6, by = y0 + 6;
    const int b = w.add_building(0, bx, by, 2, 2, 0, 400, 0, 0.0, true);
    w.add_combat_unit(bx - 1, by, 0, gi_combat());
    w.add_combat_unit(bx - 1, by + 1, 0, gi_combat());
    REQUIRE(w.campaign_occupy_now(0, b));
    REQUIRE(w.campaign_occupy_now(1, b));
    REQUIRE(w.buildings()[(std::size_t)b].occupants.size() == 2);
    w.unload_occupants(b);
    // Ring walk start: (x0+w, y0+h) = the cell diagonally past the
    // foundation's bottom-right corner; both GIs share it (infantry spots)
    // or the first spills next to it.
    for (int k = 0; k < 2; ++k) {
        const Unit& u = w.units()[(std::size_t)k];
        CHECK(u.alive);
        CHECK(u.cargo_of == -1);
        CHECK(std::max(std::abs(u.cell_x - (bx + 2)), std::abs(u.cell_y - (by + 2))) <= 1);
    }
}

TEST_CASE("w8place W8-06-07: a closed border kills the occupants; one open cell takes them all") {
    W8_WORLD(w, x0, y0);
    w.set_building_types({garrison_def()});
    const int bx = x0 + 6, by = y0 + 6;
    const int b = w.add_building(0, bx, by, 2, 2, 0, 400, 0, 0.0, true);
    w.add_combat_unit(bx - 1, by, 0, gi_combat());
    w.add_combat_unit(bx - 1, by + 1, 0, gi_combat());
    REQUIRE(w.campaign_occupy_now(0, b));
    REQUIRE(w.campaign_occupy_now(1, b));
    // Close every ring cell except the left column's (bx-1, by).
    for (int y = by - 1; y <= by + 2; ++y)
        for (int x = bx - 1; x <= bx + 2; ++x) {
            const bool ring = x == bx - 1 || x == bx + 2 || y == by - 1 || y == by + 2;
            if (ring && !(x == bx - 1 && y == by)) w.block_cell(x, y);
        }
    w.unload_occupants(b);
    for (int k = 0; k < 2; ++k) {
        const Unit& u = w.units()[(std::size_t)k];
        CHECK(u.alive);
        CHECK(std::max(std::abs(u.cell_x - (bx - 1)), std::abs(u.cell_y - by)) <= 1);
    }
}

TEST_CASE("w8place W8-06-07: a closed border kills the occupants (KillOccupants, no relocation)") {
    W8_WORLD(w, x0, y0);
    w.set_building_types({garrison_def()});
    const int bx = x0 + 6, by = y0 + 6;
    const int b = w.add_building(0, bx, by, 2, 2, 0, 400, 0, 0.0, true);
    w.add_combat_unit(bx - 1, by, 0, gi_combat());
    w.add_combat_unit(bx - 1, by + 1, 0, gi_combat());
    REQUIRE(w.campaign_occupy_now(0, b));
    REQUIRE(w.campaign_occupy_now(1, b));
    for (int y = by - 1; y <= by + 2; ++y)
        for (int x = bx - 1; x <= bx + 2; ++x)
            if (x == bx - 1 || x == bx + 2 || y == by - 1 || y == by + 2) w.block_cell(x, y);
    w.unload_occupants(b);
    CHECK(w.buildings()[(std::size_t)b].occupants.empty());
    CHECK_FALSE(w.units()[0].alive);
    CHECK_FALSE(w.units()[1].alive);
}

TEST_CASE("w8place W8-06-07: a sold garrison with a closed border puts them on its last cell") {
    W8_WORLD(w, x0, y0);
    w.set_building_types({garrison_def()});
    const int bx = x0 + 6, by = y0 + 6;
    const int b = w.add_building(0, bx, by, 2, 2, 0, 400, 0, 0.0, true);
    w.add_combat_unit(bx - 1, by, 0, gi_combat());
    REQUIRE(w.campaign_occupy_now(0, b));
    for (int y = by - 1; y <= by + 2; ++y)
        for (int x = bx - 1; x <= bx + 2; ++x)
            if (x == bx - 1 || x == bx + 2 || y == by - 1 || y == by + 2) w.block_cell(x, y);
    w.unload_occupants(b, /*place_inside=*/true);
    CHECK(w.units()[0].alive);
    CHECK(w.units()[0].cell_x == bx + 1);
    CHECK(w.units()[0].cell_y == by + 1);
}

// ------------------------------------------------------------ W8-03-10

TEST_CASE("w8place W8-03-10: an exit from a dead host never drops down a cliff") {
    if (!ra2fixture::mapsmd03_available()) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    World w(ra2fixture::mapsmd03_map());
    const int gw = w.grid_w(), gh = w.grid_h();
    std::vector<std::uint8_t> zero((size_t)gw * gh, 0);
    w.arm_land_speed(zero, zero, World::LandSpeedRules{});   // arms the level grid (height gate)
    // A walkable host cell H with a walkable 8-neighbour L at least 2 levels lower.
    int hx = -1, hy = -1, lx = -1, ly = -1;
    for (int y = 4; y < gh - 4 && hx < 0; ++y)
        for (int x = 4; x < gw - 4 && hx < 0; ++x) {
            if (!w.walkable(x, y)) continue;
            for (int dy = -1; dy <= 1 && hx < 0; ++dy)
                for (int dx = -1; dx <= 1 && hx < 0; ++dx) {
                    if (!dx && !dy) continue;
                    if (w.walkable(x + dx, y + dy) && w.cell_level(x + dx, y + dy) <= w.cell_level(x, y) - 2) {
                        hx = x; hy = y; lx = x + dx; ly = y + dy;
                    }
                }
        }
    if (hx < 0) {
        MESSAGE("skipped: no cliff pair on mapsmd03");
        return;
    }
    UnitCombat gi = gi_combat();
    // Fill the host cell and every ring-1 cell except L with blockers.
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            const int x = hx + dx, y = hy + dy;
            if (x == lx && y == ly) continue;
            if (!w.walkable(x, y)) continue;
            UnitCombat v = gi;
            v.is_vehicle = true;
            w.add_combat_unit(x, y, 0, v);
        }
    // The passenger (only its domain matters) stands far away.
    int px = -1, py = -1;
    for (int y = 4; y < gh - 4 && px < 0; ++y)
        for (int x = 4; x < gw - 4 && px < 0; ++x)
            if (w.walkable(x, y) && w.unit_at_cell(x, y) < 0 &&
                std::max(std::abs(x - hx), std::abs(y - hy)) > 6) { px = x; py = y; }
    REQUIRE(px >= 0);
    const Unit& pass = w.add_combat_unit(px, py, 0, gi);
    REQUIRE(w.occupancy().is_free(lx, ly));
    const Cellxy e = w.nearest_exit_from_host(pass, hx, hy, 0);
    // The ungated nearest-free search would have answered (lx,ly) or the
    // passenger's own cell; the gated one never a cell 2+ levels below H
    // reached in one step.
    CHECK_FALSE((e.x == lx && e.y == ly));
    if (e.x >= 0) {
        const int dl = w.cell_level(hx, hy) - w.cell_level(e.x, e.y);
        CHECK(dl <= 1);
    }
}
