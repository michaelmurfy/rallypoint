// G-05 / G-06 / G-10 / G-04: the ORIGINAL mouse scheme (Input default) and the
// hotkey commands, fed through Input::handle with real InputEvents on the real
// mapsmd03 map [RE: gamemd fixB hunt -- see engine/input_original.cpp].
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

void unit_px(const ra2engine::Unit& u, int& px, int& py) {
    const ra2engine::ScreenPt p = ra2engine::cell_to_screen(u.cell_x, u.cell_y, u.level, 60, 30, 15);
    px = p.x + 30;
    py = p.y + 15;
}
void cell_px(int cx, int cy, int& px, int& py) {
    const ra2engine::ScreenPt p = ra2engine::cell_to_screen(cx, cy, 0, 60, 30, 15);
    px = p.x + 30;
    py = p.y + 15;
}

InputEvent ev(InputEvent::Kind k, int x, int y, bool right = false, uint32_t t = 1000) {
    InputEvent e;
    e.kind = k;
    e.x = x;
    e.y = y;
    e.button = right ? InputEvent::Right : InputEvent::Left;
    e.t_ms = t;
    return e;
}
void lclick(Input& in, int x, int y, uint32_t t, bool shift = false, bool ctrl = false,
            bool alt = false) {
    InputEvent d = ev(InputEvent::PointerDown, x, y, false, t);
    d.shift = shift; d.ctrl = ctrl; d.alt = alt;
    InputEvent u = d;
    u.kind = InputEvent::PointerUp;
    in.handle(d);
    in.handle(u);
}
void rclick(Input& in, int x, int y, uint32_t t = 1000) {
    in.handle(ev(InputEvent::PointerDown, x, y, true, t));
    in.handle(ev(InputEvent::PointerUp, x, y, true, t));
}

const std::vector<int> kSA = {100, 80, 80, 50, 25, 25, 75, 50, 25, 100, 100};
ra2engine::UnitCombat gi() { return ra2engine::UnitCombat{125, "none", "GI", 15, 20, 4.0, kSA}; }
ra2engine::UnitCombat cons() { return ra2engine::UnitCombat{125, "flak", "CONS", 15, 25, 4.0, kSA}; }

// Two own GIs + one house-1 Conscript side by side near the start cell.
struct Fx { bool ok = false; int g0 = -1, g1 = -1, c = -1; };
Fx spawn3(World& w) {
    Fx f;
    const MapCell* s = ra2engine::pick_start_cell(w.map());
    if (!s) return f;
    std::vector<std::pair<int, int>> cells;
    for (int dx = 0; dx < 10 && cells.size() < 3; ++dx)
        if (w.walkable(s->x + dx, s->y) && w.unit_at_cell(s->x + dx, s->y) < 0)
            cells.push_back({s->x + dx, s->y});
    if (cells.size() < 3) return f;
    w.add_combat_unit(cells[0].first, cells[0].second, 0, gi());
    f.g0 = (int)w.units().size() - 1;
    w.add_combat_unit(cells[1].first, cells[1].second, 0, gi());
    f.g1 = (int)w.units().size() - 1;
    w.add_combat_unit(cells[2].first, cells[2].second, 1, cons());
    f.c = (int)w.units().size() - 1;
    f.ok = true;
    return f;
}
// A walkable, unit-free level-0 cell between d and d+3 cells from (ax, ay).
std::pair<int, int> far_cell(const World& w, int ax, int ay, int d) {
    for (const auto& mc : w.map().cells())
        if (mc.level == 0 && w.walkable(mc.x, mc.y) && w.unit_at_cell(mc.x, mc.y) < 0 &&
            std::max(std::abs(mc.x - ax), std::abs(mc.y - ay)) >= d &&
            std::max(std::abs(mc.x - ax), std::abs(mc.y - ay)) < d + 4)
            return {mc.x, mc.y};
    return {-1, -1};
}

}  // namespace

TEST_CASE("Input original: Original is the default scheme") {
    MapFile map;
    if (!real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    Camera cam;
    Input in(world, cam, 1280, 720);
    CHECK(in.scheme() == Input::Scheme::Original);
}

TEST_CASE("Input original: left selects, left on ground moves, right deselects") {
    MapFile map;
    if (!real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    world.set_move_speed(4.0);
    Camera cam;
    Input in(world, cam, 1280, 720);
    Fx f = spawn3(world);
    if (!f.ok) { MESSAGE("skipped"); return; }
    const auto goal = far_cell(world, world.units()[f.g0].cell_x, world.units()[f.g0].cell_y, 5);
    if (goal.first < 0) { MESSAGE("skipped: no goal cell"); return; }
    int gx, gy, ux, uy;
    cell_px(goal.first, goal.second, gx, gy);

    // Nothing selected: a ground click does nothing [RE: FUN_004AB8C0 action 0].
    CHECK(in.action_at(gx, gy) == Input::Action::None);
    lclick(in, gx, gy, 1000);
    CHECK_FALSE(world.units()[f.g0].moving());

    // Left-click own unit selects it (cursor says Select first).
    unit_px(world.units()[f.g0], ux, uy);
    CHECK(in.action_at(ux, uy) == Input::Action::Select);
    lclick(in, ux, uy, 2000);
    CHECK(in.selection().has(f.g0));
    CHECK(in.take_actions().selected);

    // Left-click ground with a selection = MOVE (orders on the LEFT button).
    CHECK(in.action_at(gx, gy) == Input::Action::Move);
    lclick(in, gx, gy, 3000);
    const ra2engine::InputActions a = in.take_actions();
    CHECK(a.move_ordered);
    CHECK(world.units()[f.g0].moving());
    CHECK(in.selection().has(f.g0));   // the selection survives the order

    // Right-click (no drag) with no mode armed = deselect all [FUN_004AAC40 tail].
    rclick(in, gx, gy);
    CHECK(in.selection().empty());
}

TEST_CASE("Input original: left on a hostile attacks; Alt forces a move") {
    MapFile map;
    if (!real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    world.set_move_speed(4.0);
    Camera cam;
    Input in(world, cam, 1280, 720);
    Fx f = spawn3(world);
    if (!f.ok) { MESSAGE("skipped"); return; }
    in.selection().set(f.g0);
    int cx, cy;
    unit_px(world.units()[f.c], cx, cy);
    CHECK(in.action_at(cx, cy) == Input::Action::Attack);
    lclick(in, cx, cy, 1000);
    CHECK(in.take_actions().attack_ordered);
    CHECK(world.units()[f.g0].target == f.c);

    lclick(in, cx, cy, 2000, false, false, /*alt=*/true);   // force-move onto it
    CHECK(in.take_actions().move_ordered);
    CHECK(world.units()[f.g0].target == -1);
}

TEST_CASE("Input original: G-06 an ALLIED unit is not an attack target") {
    MapFile map;
    if (!real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    World::HouseRec h0, h1;
    h0.alliance_team = 0;
    h1.alliance_team = 0;   // house 1 is on the player's team
    world.set_house(0, h0);
    world.set_house(1, h1);
    Camera cam;
    Input in(world, cam, 1280, 720);
    Fx f = spawn3(world);
    if (!f.ok) { MESSAGE("skipped"); return; }
    in.selection().set(f.g0);
    int cx, cy;
    unit_px(world.units()[f.c], cx, cy);
    CHECK(in.action_at(cx, cy) != Input::Action::Attack);
    lclick(in, cx, cy, 1000);
    CHECK_FALSE(in.take_actions().attack_ordered);
    CHECK(world.units()[f.g0].target == -1);
}

TEST_CASE("Input original: right-drag scrolls and does NOT deselect") {
    MapFile map;
    if (!real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    Camera cam;
    Input in(world, cam, 1280, 720);
    Fx f = spawn3(world);
    if (!f.ok) { MESSAGE("skipped"); return; }
    in.center_on_cell(world.units()[f.g0].cell_x, world.units()[f.g0].cell_y);
    in.selection().set(f.g0);
    const int x0 = cam.x;
    in.handle(ev(InputEvent::PointerDown, 600, 300, true));
    in.handle(ev(InputEvent::PointerMove, 620, 300));
    in.handle(ev(InputEvent::PointerMove, 660, 300));
    in.handle(ev(InputEvent::PointerUp, 660, 300, true));
    CHECK(cam.x != x0);
    CHECK(in.selection().has(f.g0));
}

TEST_CASE("Input original: right-click cancels ONE mode before it deselects") {
    MapFile map;
    if (!real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    Camera cam;
    Input in(world, cam, 1280, 720);
    Fx f = spawn3(world);
    if (!f.ok) { MESSAGE("skipped"); return; }
    in.selection().set(f.g0);
    in.begin_placement(1, 2, 2);
    CHECK(in.action_at(100, 100) == Input::Action::Place);
    rclick(in, 100, 100);
    CHECK_FALSE(in.placement_active());
    CHECK(in.take_actions().place_cancelled);
    CHECK(in.selection().has(f.g0));   // the first right-click only cancelled
    rclick(in, 100, 100);
    CHECK_FALSE(in.selection().has(f.g0));
}

TEST_CASE("Input original: G-10 any own building is selectable; right-click deselects it") {
    MapFile map;
    if (!real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    Camera cam;
    Input in(world, cam, 1280, 720);
    Fx f = spawn3(world);
    if (!f.ok) { MESSAGE("skipped"); return; }
    const auto o = far_cell(world, world.units()[f.g0].cell_x, world.units()[f.g0].cell_y, 8);
    if (o.first < 0) { MESSAGE("skipped"); return; }
    const int b = world.add_building(1, o.first, o.second, 2, 2, 0, 1000, 0, 0.0, true);
    in.selection().set(f.g0);
    int px, py;
    cell_px(o.first, o.second, px, py);
    CHECK(in.action_at(px, py) == Input::Action::SelectBuilding);
    lclick(in, px, py, 1000);
    CHECK(in.selected_building() == b);
    CHECK(in.take_actions().building_selected);
    CHECK_FALSE(in.selection().has(f.g0));   // selecting a building drops units
    rclick(in, px, py);
    CHECK(in.selected_building() == -1);
}

TEST_CASE("Input original: G-04 control groups create / select / add / centre") {
    MapFile map;
    if (!real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    Camera cam;
    Input in(world, cam, 1280, 720);
    Fx f = spawn3(world);
    if (!f.ok) { MESSAGE("skipped"); return; }
    in.selection().set(f.g0);
    in.group_create(1);
    CHECK(in.group_of(f.g0) == 1);
    in.selection().clear();
    in.selection().set(f.g1);
    in.group_create(2);
    in.selection().clear();

    in.group_select(1, 5000);
    CHECK(in.selection().has(f.g0));
    CHECK_FALSE(in.selection().has(f.g1));
    in.group_add_select(2);
    CHECK(in.selection().has(f.g1));

    // Re-creating group 1 from g1 moves g1 (one group per unit) and empties g0.
    in.selection().clear();
    in.selection().set(f.g1);
    in.group_create(1);
    CHECK(in.group_of(f.g1) == 1);
    CHECK(in.group_of(f.g0) == 0);

    // TeamSelect twice within 800 ms centres the view on the group.
    in.group_select(1, 10000);
    in.group_select(1, 10500);
    const auto& u = world.units()[f.g1];
    const int sx = static_cast<int>(std::lround((u.fx - u.fy) * 30.0)) + 30 + cam.x;
    const int sy = static_cast<int>(std::lround((u.fx + u.fy) * 15.0)) + 15 + cam.y;
    CHECK(std::abs(sx - 640) <= 2);
    CHECK(std::abs(sy - 360) <= 2);
}

TEST_CASE("Input original: G-04 Stop halts at the next cell and drops the target") {
    MapFile map;
    if (!real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    world.set_move_speed(4.0);
    Camera cam;
    Input in(world, cam, 1280, 720);
    Fx f = spawn3(world);
    if (!f.ok) { MESSAGE("skipped"); return; }
    in.selection().set(f.g0);
    const auto goal = far_cell(world, world.units()[f.g0].cell_x, world.units()[f.g0].cell_y, 8);
    if (goal.first < 0) { MESSAGE("skipped"); return; }
    world.group_move(ra2engine::kAnyHouse, in.selection().ids(), goal.first, goal.second);
    REQUIRE(world.units()[f.g0].moving());
    for (int i = 0; i < 5; ++i) world.step(1.0 / 15.0);
    in.stop_selected();
    CHECK(world.units()[f.g0].path.size() <= 2);
    CHECK(world.units()[f.g0].target == -1);
    for (int i = 0; i < 30; ++i) world.step(1.0 / 15.0);
    CHECK_FALSE(world.units()[f.g0].moving());
    CHECK(std::max(std::abs(world.units()[f.g0].cell_x - goal.first),
                   std::abs(world.units()[f.g0].cell_y - goal.second)) >= 4);
}

TEST_CASE("Input original: G-04 TypeSelect extends the selection by image") {
    MapFile map;
    if (!real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    Camera cam;
    Input in(world, cam, 1280, 720);
    Fx f = spawn3(world);
    if (!f.ok) { MESSAGE("skipped"); return; }
    in.center_on_cell(world.units()[f.g0].cell_x, world.units()[f.g0].cell_y);
    in.selection().set(f.g0);
    CHECK(in.type_select());
    CHECK(in.selection().has(f.g1));       // same image (GI)
    CHECK_FALSE(in.selection().has(f.c));  // different image + hostile
}

TEST_CASE("Input original: ACTION_SELF -- clicking the selected deployable unit deploys it") {
    MapFile map;
    if (!real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    Camera cam;
    Input in(world, cam, 1280, 720);
    Fx f = spawn3(world);
    if (!f.ok) { MESSAGE("skipped"); return; }
    in.selection().set(f.g0);
    int ux, uy;
    unit_px(world.units()[f.g0], ux, uy);
    CHECK(in.action_at(ux, uy) == Input::Action::Select);   // a plain GI: re-select
    in.set_deploy_check([](const ra2engine::Unit& u) { return u.image == "GI"; });
    CHECK(in.action_at(ux, uy) == Input::Action::Deploy);
    lclick(in, ux, uy, 1000);
    CHECK(in.take_actions().deploy_clicked);
    CHECK(in.selection().has(f.g0));
}
