// W6-01 touch fixes in Input (engine/input.cpp, engine/input_original.cpp),
// fed with real InputEvents marked `touch` on the real mapsmd03 map:
//   W6-01-01  building pick: mouse = the original (foundation + the origin-cell
//             hotspot), touch = the drawn art first, a unit only by its body
//   W6-01-05  a touch double tap on a deployer selects the type, never deploys;
//             a single tap on the lone selected deployer deploys after the window
//   W6-01-06  with an army selected, a tap on one of it is a move order
#include "doctest.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>
#include <vector>

#include "input.hpp"
#include "iso_project.hpp"
#include "iso_renderer.hpp"
#include "world.hpp"

#include "ra2assets/map.hpp"
#include "asset_fixture.hpp"

using namespace ra2assets;
using ra2engine::Camera;
using ra2engine::Input;
using ra2engine::InputEvent;
using ra2engine::World;

namespace {

bool real_map(MapFile& out) {
    if (!ra2fixture::mapsmd03_available()) return false;
    out = ra2fixture::mapsmd03_map();
    return true;
}

Camera g_cam;   // the tests' camera (Input keeps a reference)

// The unit's ground point (its feet), as Input::unit_ground_px.
void unit_px(const ra2engine::Unit& u, int& px, int& py) {
    const ra2engine::ScreenPt p = ra2engine::cell_to_screen(u.cell_x, u.cell_y, u.level, 60, 30, 15);
    px = p.x + 30 + g_cam.x;
    py = p.y + 15 + g_cam.y;
}
// Centre the camera on cell (cx, cy) so commit_double's on-screen test holds.
void look_at(int cx, int cy) {
    const ra2engine::ScreenPt p = ra2engine::cell_to_screen(cx, cy, 0, 60, 30, 15);
    g_cam.x = 640 - (p.x + 30);
    g_cam.y = 360 - (p.y + 15);
}

void tap(Input& in, int x, int y, uint32_t t) {
    InputEvent d;
    d.kind = InputEvent::PointerDown;
    d.x = x;
    d.y = y;
    d.button = InputEvent::Left;
    d.t_ms = t;
    d.touch = true;
    InputEvent u = d;
    u.kind = InputEvent::PointerUp;
    in.handle(d);
    in.handle(u);
}

const std::vector<int> kSA = {100, 80, 80, 50, 25, 25, 75, 50, 25, 100, 100};
ra2engine::UnitCombat gi() { return ra2engine::UnitCombat{125, "none", "GI", 15, 20, 4.0, kSA}; }

// `n` own GIs on level-0 walkable cells in a row from the start cell.
std::vector<int> spawn_gis(World& w, int n, int gap = 1) {
    std::vector<int> out;
    const MapCell* s = ra2engine::pick_start_cell(w.map());
    if (!s) return out;
    for (int dx = 0; dx < 40 && static_cast<int>(out.size()) < n; dx += gap) {
        const MapCell* c = w.map().at(s->x + dx, s->y);
        if (!c || c->level != 0 || !w.walkable(s->x + dx, s->y) || w.unit_at_cell(s->x + dx, s->y) >= 0) continue;
        w.add_combat_unit(s->x + dx, s->y, 0, gi());
        out.push_back(static_cast<int>(w.units().size()) - 1);
    }
    return out;
}

}  // namespace

TEST_CASE("W6-01-06: a touch tap on a unit of the selected army is a move order") {
    MapFile map;
    if (!real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    world.set_move_speed(4.0);
    g_cam = Camera{};
    Input in(world, g_cam, 1280, 720);
    const std::vector<int> g = spawn_gis(world, 3);
    if (g.size() < 3) { MESSAGE("skipped: no cells"); return; }
    look_at(world.units()[g[0]].cell_x, world.units()[g[0]].cell_y);
    in.selection().set(g[0]);
    in.selection().set(g[1]);
    int x, y;
    unit_px(world.units()[g[1]], x, y);
    tap(in, x, y - 8, 5000);   // on the body of a SELECTED unit
    const ra2engine::InputActions a = in.take_actions();
    CHECK(a.move_ordered);
    CHECK(in.selection().has(g[0]));   // the army stays selected
    CHECK(in.selection().has(g[1]));
    // A tap on the body of an UNSELECTED own unit is a deliberate select.
    unit_px(world.units()[g[2]], x, y);
    tap(in, x, y - 8, 9000);
    CHECK(in.selection().has(g[2]));
    CHECK_FALSE(in.selection().has(g[0]));
}

TEST_CASE("W6-01-06: with a selection, a touch tap beside an own unit (box, not body) is ground") {
    MapFile map;
    if (!real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    world.set_move_speed(4.0);
    g_cam = Camera{};
    Input in(world, g_cam, 1280, 720);
    const std::vector<int> g = spawn_gis(world, 3, 3);
    if (g.size() < 3) { MESSAGE("skipped: no cells"); return; }
    look_at(world.units()[g[0]].cell_x, world.units()[g[0]].cell_y);
    in.selection().set(g[0]);
    int x, y;
    unit_px(world.units()[g[2]], x, y);
    // 28 px right of the feet: inside the 65 px selection box, outside the body
    CHECK(in.pick_unit_at(x + 28, y) == g[2]);
    tap(in, x + 28, y, 5000);
    CHECK(in.take_actions().move_ordered);
    CHECK(in.selection().has(g[0]));
    CHECK_FALSE(in.selection().has(g[2]));
}

TEST_CASE("W6-01-05: a touch double tap selects the type and never deploys") {
    MapFile map;
    if (!real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    g_cam = Camera{};
    Input in(world, g_cam, 1280, 720);
    in.set_deploy_check([](const ra2engine::Unit&) { return true; });   // every GI deploys
    const std::vector<int> g = spawn_gis(world, 3);
    if (g.size() < 3) { MESSAGE("skipped: no cells"); return; }
    look_at(world.units()[g[0]].cell_x, world.units()[g[0]].cell_y);
    int x, y;
    unit_px(world.units()[g[0]], x, y);
    tap(in, x, y - 8, 5000);   // select
    CHECK(in.selection().has(g[0]));
    in.take_actions();
    tap(in, x + 3, y - 6, 5600);   // later: a single tap on the lone selected deployer
    CHECK_FALSE(in.take_actions().deploy_clicked);   // waits out the double-tap window
    in.touch_tick(0.3);
    CHECK_FALSE(in.take_actions().deploy_clicked);
    in.touch_tick(0.3);
    CHECK(in.take_actions().deploy_clicked);         // ... then deploys

    // A double tap: the first tap would deploy (ACTION_SELF), the second
    // makes it a type select and the pending deploy is dropped.
    tap(in, x, y - 8, 9000);
    tap(in, x + 3, y - 6, 9200);
    in.touch_tick(1.0);
    const ra2engine::InputActions a = in.take_actions();
    CHECK_FALSE(a.deploy_clicked);
    for (int i : g) CHECK(in.selection().has(i));
}

TEST_CASE("W6-01-01: building pick -- mouse by foundation + hotspot, touch by the drawn art") {
    MapFile map;
    if (!real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    g_cam = Camera{};
    Input in(world, g_cam, 1280, 720);
    const MapCell* s = ra2engine::pick_start_cell(world.map());
    if (!s) { MESSAGE("skipped"); return; }
    int bx = -1, by = -1;
    for (int dx = 2; dx < 30 && bx < 0; ++dx) {
        const MapCell* c = world.map().at(s->x + dx, s->y);
        const MapCell* c2 = world.map().at(s->x + dx + 1, s->y);
        if (c && c->level == 0 && world.walkable(s->x + dx, s->y) && c2 && c2->level == 0 &&
            world.walkable(s->x + dx + 1, s->y)) {
            bx = s->x + dx;
            by = s->y;
        }
    }
    if (bx < 0) { MESSAGE("skipped: no cell"); return; }
    const int b = world.add_building(0, bx, by, 1, 1, 0, 400, 0, 0.0, true);
    look_at(bx, by);
    // The renderer's art test: a tall 20 x 70 px "tower" above the cell centre.
    const ra2engine::ScreenPt cp = ra2engine::cell_to_screen(bx, by, 0, 60, 30, 15);
    const int ccx = cp.x + 30 + g_cam.x, ccy = cp.y + 15 + g_cam.y;
    in.set_building_art_test([&](int bi, int px, int py) {
        return bi == b && px >= ccx - 10 && px <= ccx + 10 && py >= ccy - 70 && py <= ccy;
    });
    // Mouse: the foundation and the original hotspot pick it, the tower head does not.
    CHECK(in.pick_building(ccx, ccy, false) == b);
    CHECK(in.pick_building(ccx, ccy - 18, false) == b);   // dy 18: inside the hotspot ellipse
    CHECK(in.pick_building(ccx, ccy - 55, false) == -1);
    // Touch: the head picks it.
    CHECK(in.pick_building(ccx, ccy - 55, true) == b);

    // A GI at the tower's foot (the next cell): its 65 px box covers the head,
    // but a finger on the head (outside the GI's body) picks the tower.
    world.add_combat_unit(bx + 1, by, 0, gi());
    const int gi_idx = static_cast<int>(world.units().size()) - 1;
    CHECK(in.pick_unit_at(ccx + 6, ccy - 30) == gi_idx);
    tap(in, ccx + 6, ccy - 30, 5000);
    CHECK(in.take_actions().building_selected);
    CHECK(in.selected_building() == b);
    CHECK_FALSE(in.selection().has(gi_idx));
    // ... and the GI's body still selects the GI.
    int ux, uy;
    unit_px(world.units()[gi_idx], ux, uy);
    tap(in, ux, uy - 10, 9000);
    CHECK(in.selection().has(gi_idx));
    CHECK(in.selected_building() == -1);
}
