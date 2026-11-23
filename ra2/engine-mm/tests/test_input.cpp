#include "doctest.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "input.hpp"
#include "iso_project.hpp"
#include "iso_renderer.hpp"  // terrain_screen_bounds
#include "pathfind.hpp"      // find_path (reachability probe, Task 6 raised-terrain test)
#include "world.hpp"

#include "ra2assets/map.hpp"
#include "ra2assets/mix.hpp"
#include "asset_fixture.hpp"  // shared load-once real map (mapsmd03.mix)

using namespace ra2assets;
using ra2engine::Camera;
using ra2engine::Input;
using ra2engine::InputEvent;
using ra2engine::World;

namespace {

bool load_real_map(MapFile& out) {
    // Borrow the shared, load-once mapsmd03.mix map (asset_fixture) instead of
    // re-slurping + re-parsing the same archive in every input case. Same
    // ok/false contract as before (archive present AND map entry found); the
    // parsed map is copied into `out` so callers keep their own instance.
    if (!ra2fixture::mapsmd03_available()) return false;
    out = ra2fixture::mapsmd03_map();
    return true;
}

// Screen pixel over the cell (cx, cy) ground point at identity camera, level 0.
// This is the level-0 cell ground point, used for GROUND-target clicks
// (right-click move issues to a cell, not a unit, and is level-blind by
// design). Unit clicks must use ground_px_for_unit below, which is
// level-aware to match Input::unit_under's screen-space pick.
void px_for_cell(int cx, int cy, int& px, int& py) {
    const ra2engine::ScreenPt p = ra2engine::cell_to_screen(cx, cy, 0, 60, 30, 15);
    px = p.x + 30;  // + tile_w/2 (ground point x)
    py = p.y + 15;  // + tile_h/2 (ground point y)
}

// Ground-point pixel of a unit under an identity camera -- LEVEL-AWARE,
// mirroring Input::unit_ground_px / draw_unit's anchor. Required for BOX
// tests: commit_box tests each unit's real (level-raised) ground point, and
// the spawn cluster is NOT level 0 on the real map.
void ground_px_for_unit(const ra2engine::Unit& u, int& px, int& py) {
    const ra2engine::ScreenPt p =
        ra2engine::cell_to_screen(u.cell_x, u.cell_y, u.level, 60, 30, 15);
    px = p.x + 30;
    py = p.y + 15;
}

// Left click helper (down then up, no move) with modifiers + timestamp.
void left_click(Input& in, int px, int py, bool shift, uint32_t t_ms) {
    InputEvent d;
    d.kind = InputEvent::PointerDown; d.button = InputEvent::Left; d.x = px; d.y = py;
    d.shift = shift; d.t_ms = t_ms;
    InputEvent u = d;
    u.kind = InputEvent::PointerUp;
    in.handle(d);
    in.handle(u);
}

// Right click helper (down then up, no move) with a timestamp. The order
// (attack or group-move) is dispatched on PointerDown; the PointerUp is fed
// too only to mirror a real event stream (handle()'s Right PointerUp branch
// is a no-op).
void right_click(Input& in, int px, int py, uint32_t t_ms) {
    InputEvent d;
    d.kind = InputEvent::PointerDown; d.button = InputEvent::Right; d.x = px; d.y = py;
    d.t_ms = t_ms;
    InputEvent u = d;
    u.kind = InputEvent::PointerUp;
    in.handle(d);
    in.handle(u);
}

// GI (house 0, M60) / Conscript (house 1, M1Carbine) combat stats -- same
// ground-truth numbers test_world.cpp's gi_stats()/cons_stats() use (both
// Strength=125, SA warhead verses).
const std::vector<int> kSAVerses = {100, 80, 80, 50, 25, 25, 75, 50, 25, 100, 100};
ra2engine::UnitCombat gi_combat() {
    return ra2engine::UnitCombat{125, "none", "GI", 15, 20, 4.0, kSAVerses};
}
ra2engine::UnitCombat cons_combat() {
    return ra2engine::UnitCombat{125, "flak", "CONS", 15, 25, 4.0, kSAVerses};
}

// Spawn 2 GIs (house 0) + 1 Conscript (house 1) via add_combat_unit at
// adjacent walkable cells near the map's start -- same dx-scan pattern as
// `spawn` above, just combat-capable units and a mixed-house trio. All three
// land within a few cells of each other, comfortably inside both units'
// weapon range (4.0 cells), so the Conscript is always an in-range hostile
// for the group-move/engagement tests below.
struct CombatFixture {
    bool ok = false;
    int gi0 = -1, gi1 = -1, cons = -1;
};
CombatFixture spawn_combat_fixture(World& world) {
    CombatFixture fx;
    const MapCell* c = ra2engine::pick_start_cell(world.map());
    if (!c) return fx;
    std::vector<std::pair<int, int>> cells;
    int placed = 0;
    for (int dx = 0; dx < 8 && placed < 3; ++dx) {
        const int x = c->x + dx, y = c->y;
        if (world.walkable(x, y) && world.unit_at_cell(x, y) < 0) {
            cells.push_back({x, y});
            ++placed;
        }
    }
    if (placed < 3) return fx;
    world.add_combat_unit(cells[0].first, cells[0].second, 0, gi_combat());
    fx.gi0 = static_cast<int>(world.units().size()) - 1;
    world.add_combat_unit(cells[1].first, cells[1].second, 0, gi_combat());
    fx.gi1 = static_cast<int>(world.units().size()) - 1;
    world.add_combat_unit(cells[2].first, cells[2].second, 1, cons_combat());
    fx.cons = static_cast<int>(world.units().size()) - 1;
    fx.ok = true;
    return fx;
}

// True if (p.x, p.y) is within one cell of (gx, gy) -- order_move_selected's
// formation spiral can assign a multi-unit selection's movers to distinct
// cells around the goal rather than the exact goal cell each, so a
// group-move assertion checks "near", not "==" (see World::order_move_selected
// / group_move.hpp's assign_formation).
bool near_cell(const ra2engine::Cellxy& p, int gx, int gy) {
    return std::abs(p.x - gx) <= 1 && std::abs(p.y - gy) <= 1;
}

// Level-aware ground pixel for a raw (cx, cy, level) cell -- same formula as
// ground_px_for_unit but for a bare cell (no Unit); used by the raised-
// terrain right-click test to click the ACTUAL on-screen position of a
// level >= 1 tile (identity camera).
void level_ground_px(int cx, int cy, int level, int& px, int& py) {
    const ra2engine::ScreenPt p = ra2engine::cell_to_screen(cx, cy, level, 60, 30, 15);
    px = p.x + 30;
    py = p.y + 15;
}

// Nearest-first (by squared distance from (bx, by)) cell with level >= 1
// that is both walkable and reachable (find_path -- the same algorithm
// order_move_selected uses -- returns a real >1-cell route from (bx, by)) --
// or nullptr if none exists. The raised-terrain test MESSAGE-skips if this
// comes back empty (not every map has reachable raised terrain near a given
// spawn area).
const MapCell* find_reachable_raised_cell(const World& world, int bx, int by) {
    std::vector<const MapCell*> candidates;
    for (const auto& mc : world.map().cells())
        if (mc.level >= 1 && world.walkable(mc.x, mc.y)) candidates.push_back(&mc);
    std::sort(candidates.begin(), candidates.end(), [bx, by](const MapCell* a, const MapCell* b) {
        const long long da = (long long)(a->x - bx) * (a->x - bx) + (long long)(a->y - by) * (a->y - by);
        const long long db = (long long)(b->x - bx) * (b->x - bx) + (long long)(b->y - by) * (b->y - by);
        return da < db;
    });
    for (const MapCell* mc : candidates) {
        auto p = ra2engine::find_path(world.passable(), world.grid_w(), world.grid_h(), {bx, by},
                                      {mc->x, mc->y});
        if (p.size() > 1) return mc;
    }
    return nullptr;
}

// Spawn `n` GI units at consecutive walkable cells near the map's start.
struct Fixture {
    MapFile map;
    bool ok = false;
    std::vector<std::pair<int, int>> cells;
};
Fixture spawn(World& world, int n) {
    Fixture fx;
    const MapCell* c = ra2engine::pick_start_cell(world.map());
    int placed = 0;
    for (int dx = 0; dx < 8 && placed < n; ++dx) {
        const int x = c->x + dx, y = c->y;
        if (world.walkable(x, y) && world.unit_at_cell(x, y) < 0) {
            world.add_unit(x, y, "GI", 0);
            fx.cells.push_back({x, y});
            ++placed;
        }
    }
    fx.ok = placed == n;
    return fx;
}

}  // namespace

TEST_CASE("Input: left-click a unit selects only it; empty click clears") {
    MapFile map;
    if (!load_real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    Camera cam;
    Input input(world, cam, 1280, 720);
    input.set_scheme(Input::Scheme::Legacy);   // pre-G-05 model under test
    Fixture fx = spawn(world, 3);
    if (!fx.ok) { MESSAGE("skipped: could not place 3 units"); return; }

    int px, py;
    ground_px_for_unit(world.units()[1], px, py);
    left_click(input, px, py, /*shift=*/false, /*t=*/1000);
    CHECK(input.selection().has(1));
    CHECK_FALSE(input.selection().has(0));
    CHECK_FALSE(input.selection().has(2));

    // Click far-away empty ground clears selection.
    left_click(input, px + 900, py + 400, false, 2000);
    CHECK(input.selection().empty());
}

TEST_CASE("Input: Shift+left-click toggles units in and out of the selection") {
    MapFile map;
    if (!load_real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    Camera cam;
    Input input(world, cam, 1280, 720);
    input.set_scheme(Input::Scheme::Legacy);   // pre-G-05 model under test
    Fixture fx = spawn(world, 3);
    if (!fx.ok) { MESSAGE("skipped"); return; }

    int px0, py0, px2, py2;
    ground_px_for_unit(world.units()[0], px0, py0);
    ground_px_for_unit(world.units()[2], px2, py2);
    left_click(input, px0, py0, false, 1000);   // select unit 0
    left_click(input, px2, py2, true, 2000);    // shift-add unit 2
    CHECK(input.selection().has(0));
    CHECK(input.selection().has(2));
    left_click(input, px0, py0, true, 3000);    // shift-remove unit 0
    CHECK_FALSE(input.selection().has(0));
    CHECK(input.selection().has(2));
}

TEST_CASE("Input: right-click issues a group move to the selected units") {
    MapFile map;
    if (!load_real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    world.set_move_speed(4.0);
    Camera cam;
    Input input(world, cam, 1280, 720);
    input.set_scheme(Input::Scheme::Legacy);   // pre-G-05 model under test
    Fixture fx = spawn(world, 2);
    if (!fx.ok) { MESSAGE("skipped"); return; }
    for (std::size_t k_ = 0; k_ < world.units().size(); ++k_) input.selection().set(static_cast<int>(k_));

    // Right-click a cell 10 south of the first unit.
    int px, py;
    px_for_cell(fx.cells[0].first, fx.cells[0].second + 10, px, py);
    InputEvent rc;
    rc.kind = InputEvent::PointerDown; rc.button = InputEvent::Right; rc.x = px; rc.y = py;
    rc.t_ms = 5000;
    input.handle(rc);
    InputEvent ru = rc; ru.kind = InputEvent::PointerUp;
    input.handle(ru);

    // At least one selected unit now has an active path (some may already be
    // adjacent to their assigned cell).
    int moving = 0;
    for (const auto& u : world.units()) moving += u.moving() ? 1 : 0;
    CHECK(moving >= 1);
}

TEST_CASE("Input: left-drag exposes a selection box and box-selects covered units") {
    MapFile map;
    if (!load_real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    Camera cam;
    Input input(world, cam, 1280, 720);
    input.set_scheme(Input::Scheme::Legacy);   // pre-G-05 model under test
    Fixture fx = spawn(world, 3);
    if (!fx.ok) { MESSAGE("skipped"); return; }

    // Compute the REAL (level-aware) ground pixels of all three units --
    // commit_box tests unit_ground_px, which uses each unit's terrain level,
    // so the box corners must account for it too -- then drag a box covering
    // them.
    int minx = 1000000000, miny = 1000000000, maxx = -1000000000, maxy = -1000000000;
    for (const auto& un : world.units()) {
        int px, py; ground_px_for_unit(un, px, py);
        minx = std::min(minx, px); maxx = std::max(maxx, px);
        miny = std::min(miny, py); maxy = std::max(maxy, py);
    }
    InputEvent d; d.kind = InputEvent::PointerDown; d.button = InputEvent::Left;
    d.x = minx - 20; d.y = miny - 20; d.t_ms = 1000;
    input.handle(d);
    InputEvent m; m.kind = InputEvent::PointerMove; m.x = maxx + 20; m.y = maxy + 20; m.t_ms = 1050;
    input.handle(m);
    int x0, y0, x1, y1;
    CHECK(input.selection_box(x0, y0, x1, y1));  // box is live during the drag
    InputEvent u; u.kind = InputEvent::PointerUp; u.button = InputEvent::Left;
    u.x = maxx + 20; u.y = maxy + 20; u.t_ms = 1100;
    input.handle(u);
    CHECK_FALSE(input.selection_box(x0, y0, x1, y1));  // box gone after release
    CHECK(input.selection().count() == static_cast<int>(world.units().size()));
}

TEST_CASE("Input: double-click selects all on-screen units of the same type") {
    MapFile map;
    if (!load_real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    Camera cam;
    Input input(world, cam, 1280, 720);
    input.set_scheme(Input::Scheme::Legacy);   // pre-G-05 model under test
    Fixture fx = spawn(world, 3);
    if (!fx.ok) { MESSAGE("skipped"); return; }

    // Center the camera on unit 0 so all three (adjacent cells) are on-screen
    // -- commit_double only selects units whose ground point lies inside the
    // viewport. Input holds cam by reference, so mutating it here is live.
    int gx0, gy0;
    ground_px_for_unit(world.units()[0], gx0, gy0);
    cam.x = 1280 / 2 - gx0;
    cam.y = 720 / 2 - gy0;

    // Two quick clicks on unit 0's cell (window px = ground px + camera).
    const int px = gx0 + cam.x, py = gy0 + cam.y;
    left_click(input, px, py, false, 1000);
    CHECK(input.selection().has(0));           // first click: single select
    CHECK_FALSE(input.selection().has(1));
    left_click(input, px, py, false, 1200);     // 200ms later, same spot: double
    CHECK(input.selection().count() == static_cast<int>(world.units().size()));  // all GI on screen
}

TEST_CASE("Input: pan moves the camera (clamped) and never fires a select") {
    MapFile map;
    if (!load_real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    Camera cam;
    int mnx, mny, mxx, mxy;
    REQUIRE(ra2engine::terrain_screen_bounds(map, mnx, mny, mxx, mxy));
    cam.x = 1280 / 2 - (mnx + mxx) / 2;
    cam.y = 720 / 2 - (mny + mxy) / 2;
    Input input(world, cam, 1280, 720);
    input.set_scheme(Input::Scheme::Legacy);   // pre-G-05 model under test
    const int x0 = cam.x, y0 = cam.y;
    input.pan(-40, -25);
    CHECK(cam.x == x0 - 40);
    CHECK(cam.y == y0 - 25);
}

TEST_CASE("Input: Shift+left-drag ADDS boxed units to the selection") {
    MapFile map;
    if (!load_real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    Camera cam;
    Input input(world, cam, 1280, 720);
    input.set_scheme(Input::Scheme::Legacy);   // pre-G-05 model under test
    Fixture fx = spawn(world, 3);
    if (!fx.ok) { MESSAGE("skipped"); return; }

    // Click-select unit 0 first (plain click).
    int p0x, p0y;
    ground_px_for_unit(world.units()[0], p0x, p0y);
    left_click(input, p0x, p0y, false, 1000);
    REQUIRE(input.selection().has(0));

    // Shift+drag a box over units 1 and 2 ONLY (unit 0 sits 30px west of the
    // box's left edge). Shift box must ADD them, keeping unit 0 selected.
    int p1x, p1y, p2x, p2y;
    ground_px_for_unit(world.units()[1], p1x, p1y);
    ground_px_for_unit(world.units()[2], p2x, p2y);
    const int bx0 = std::min(p1x, p2x) - 10, by0 = std::min(p1y, p2y) - 10;
    const int bx1 = std::max(p1x, p2x) + 10, by1 = std::max(p1y, p2y) + 10;
    InputEvent d;
    d.kind = InputEvent::PointerDown; d.button = InputEvent::Left;
    d.x = bx0; d.y = by0; d.shift = true; d.t_ms = 3000;
    input.handle(d);
    InputEvent m;
    m.kind = InputEvent::PointerMove; m.x = bx1; m.y = by1; m.t_ms = 3050;
    input.handle(m);
    InputEvent u;
    u.kind = InputEvent::PointerUp; u.button = InputEvent::Left;
    u.x = bx1; u.y = by1; u.shift = true; u.t_ms = 3100;
    input.handle(u);

    CHECK(input.selection().has(0));   // retained: shift box adds, not replaces
    CHECK(input.selection().has(1));
    CHECK(input.selection().has(2));
}

TEST_CASE("Input: pan clamps the camera to the map's screen bounds") {
    MapFile map;
    if (!load_real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    const int view_w = 1280, view_h = 720;
    int mnx, mny, mxx, mxy;
    REQUIRE(ra2engine::terrain_screen_bounds(map, mnx, mny, mxx, mxy));
    const int margin_x = view_w / 2, margin_y = view_h / 2;

    Camera cam_pos;
    Input in_pos(world, cam_pos, view_w, view_h);
    in_pos.set_scheme(Input::Scheme::Legacy);   // pre-G-05 model under test
    in_pos.pan(1000000000, 1000000000);
    CHECK(cam_pos.x == view_w + margin_x - mnx);
    CHECK(cam_pos.y == view_h + margin_y - mny);

    Camera cam_neg;
    Input in_neg(world, cam_neg, view_w, view_h);
    in_neg.set_scheme(Input::Scheme::Legacy);   // pre-G-05 model under test
    in_neg.pan(-1000000000, -1000000000);
    CHECK(cam_neg.x == -margin_x - mxx);
    CHECK(cam_neg.y == -margin_y - mxy);
}

// ---- Task 6: right-click attack + level-aware ground target + house-filtered selection ----

TEST_CASE("Input: right-click a hostile issues an attack order to the selection") {
    MapFile map;
    if (!load_real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    Camera cam;
    Input input(world, cam, 1280, 720);
    input.set_scheme(Input::Scheme::Legacy);   // pre-G-05 model under test
    CombatFixture fx = spawn_combat_fixture(world);
    if (!fx.ok) { MESSAGE("skipped: could not place combat fixture"); return; }

    // Select both GIs (direct set -- equivalent to box/click selection, which
    // Input's own selection tests already cover).
    input.selection().set(fx.gi0);
    input.selection().set(fx.gi1);

    int px, py;
    ground_px_for_unit(world.units()[fx.cons], px, py);
    right_click(input, px, py, 1000);

    CHECK(world.units()[fx.gi0].target == fx.cons);
    CHECK(world.units()[fx.gi0].attack_order);
    CHECK(world.units()[fx.gi1].target == fx.cons);
    CHECK(world.units()[fx.gi1].attack_order);
    CHECK_FALSE(input.selection().has(fx.cons));  // the hostile itself was never selected
}

TEST_CASE("Input: right-click ground still group-moves the selection") {
    MapFile map;
    if (!load_real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    world.set_move_speed(4.0);
    Camera cam;
    Input input(world, cam, 1280, 720);
    input.set_scheme(Input::Scheme::Legacy);   // pre-G-05 model under test
    CombatFixture fx = spawn_combat_fixture(world);
    if (!fx.ok) { MESSAGE("skipped: could not place combat fixture"); return; }

    input.selection().set(fx.gi0);
    input.selection().set(fx.gi1);

    // Engagement seam guard: one step lets the idle GIs auto-acquire the
    // in-range Conscript (mirrors test_world.cpp's freeze-seam regression) --
    // the move-cancels-engagement assertion below is only meaningful if a
    // real engagement happened first.
    world.step(1.0 / 60.0);
    REQUIRE((world.units()[fx.gi0].target == fx.cons || world.units()[fx.gi1].target == fx.cons));

    // Empty ground well beyond weapon range (>=5 cells): scan south from
    // GI0's cell for the first walkable, LEVEL-0 cell -- a level-0 target's
    // level-blind first pass is already exact (see px_for_cell), so the
    // clicked cell and the resolved goal must match precisely.
    const int bx = world.units()[fx.gi0].cell_x, by = world.units()[fx.gi0].cell_y;
    int gx = -1, gy = -1;
    for (int r = 5; r <= 60 && gx < 0; ++r) {
        const int cx = bx + r, cy = by;
        if (!world.walkable(cx, cy)) continue;
        const MapCell* mc = world.map().at(cx, cy);
        if (mc && mc->level == 0) { gx = cx; gy = cy; }
    }
    REQUIRE(gx >= 0);

    int px, py;
    px_for_cell(gx, gy, px, py);
    right_click(input, px, py, 2000);

    CHECK(world.units()[fx.gi0].target == -1);        // move cancels engagement
    CHECK(world.units()[fx.gi1].target == -1);
    CHECK(world.units()[fx.gi0].moving());
    CHECK(world.units()[fx.gi1].moving());
    CHECK(near_cell(world.units()[fx.gi0].path.back(), gx, gy));
    CHECK(near_cell(world.units()[fx.gi1].path.back(), gx, gy));
}

TEST_CASE("Input: left-click a hostile selects nothing") {
    MapFile map;
    if (!load_real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    Camera cam;
    Input input(world, cam, 1280, 720);
    input.set_scheme(Input::Scheme::Legacy);   // pre-G-05 model under test
    CombatFixture fx = spawn_combat_fixture(world);
    if (!fx.ok) { MESSAGE("skipped: could not place combat fixture"); return; }

    int px, py;
    ground_px_for_unit(world.units()[fx.cons], px, py);
    left_click(input, px, py, /*shift=*/false, /*t=*/1000);

    CHECK(input.selection().empty());
    CHECK_FALSE(input.selection().has(fx.cons));
}

TEST_CASE("Input: W2-CA-02 a PlayerControl house's unit is selectable like house 0") {
    MapFile map;
    if (!load_real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    Camera cam;
    Input input(world, cam, 1280, 720);
    input.set_scheme(Input::Scheme::Legacy);
    CombatFixture fx = spawn_combat_fixture(world);
    if (!fx.ok) { MESSAGE("skipped: could not place combat fixture"); return; }
    const int h = world.units()[fx.cons].house;
    REQUIRE(h != 0);
    CHECK_FALSE(world.controllable(h));
    World::HouseRec rec;
    rec.side = World::Side::Allied;
    rec.alliance_team = 0;
    rec.player_control = true;   // [RE FUN_0050B510: +0x1ED -> ControlledByHuman]
    world.set_house(h, rec);
    CHECK(world.controllable(h));
    int px, py;
    ground_px_for_unit(world.units()[fx.cons], px, py);
    left_click(input, px, py, /*shift=*/false, /*t=*/1000);
    CHECK(input.selection().has(fx.cons));
}

TEST_CASE("Input: R3 a PlayerControl house's unit takes force-fire and Stop like house 0") {
    // [RE: StopCommandClass FUN_0072ff70 / Scatter FUN_007300b0 walk every
    // selected object the player commands -- no Owner == PlayerPtr test.]
    MapFile map;
    if (!load_real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    CombatFixture fx = spawn_combat_fixture(world);
    if (!fx.ok) { MESSAGE("skipped: could not place combat fixture"); return; }
    const int h = world.units()[fx.cons].house;
    REQUIRE(h != 0);
    const std::vector<int> ids{fx.cons};
    // Not controllable yet: the order is refused.
    CHECK(world.group_attack_cell(0, ids, 10, 10) == 0);
    World::HouseRec rec;
    rec.side = World::Side::Allied;
    rec.alliance_team = 0;
    rec.player_control = true;
    world.set_house(h, rec);
    REQUIRE(world.controllable(h));
    const int cx = world.units()[fx.cons].cell_x + 2, cy = world.units()[fx.cons].cell_y;
    CHECK(world.group_attack_cell(0, ids, cx, cy) >= 1);
    CHECK(world.units()[fx.cons].attack_cell_x == cx);
    CHECK(world.units()[fx.cons].attack_cell_y == cy);
    world.group_stop(0, ids);

    CHECK(world.units()[fx.cons].attack_cell_x == -1);
}

TEST_CASE("Input: box over both squads selects only house-0") {
    MapFile map;
    if (!load_real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    Camera cam;
    Input input(world, cam, 1280, 720);
    input.set_scheme(Input::Scheme::Legacy);   // pre-G-05 model under test
    CombatFixture fx = spawn_combat_fixture(world);
    if (!fx.ok) { MESSAGE("skipped: could not place combat fixture"); return; }

    // Box covering all three units' real (level-aware) ground points.
    int minx = 1000000000, miny = 1000000000, maxx = -1000000000, maxy = -1000000000;
    for (const auto& un : world.units()) {
        int px, py; ground_px_for_unit(un, px, py);
        minx = std::min(minx, px); maxx = std::max(maxx, px);
        miny = std::min(miny, py); maxy = std::max(maxy, py);
    }
    InputEvent d; d.kind = InputEvent::PointerDown; d.button = InputEvent::Left;
    d.x = minx - 20; d.y = miny - 20; d.t_ms = 1000;
    input.handle(d);
    InputEvent m; m.kind = InputEvent::PointerMove; m.x = maxx + 20; m.y = maxy + 20; m.t_ms = 1050;
    input.handle(m);
    InputEvent u; u.kind = InputEvent::PointerUp; u.button = InputEvent::Left;
    u.x = maxx + 20; u.y = maxy + 20; u.t_ms = 1100;
    input.handle(u);

    CHECK(input.selection().has(fx.gi0));
    CHECK(input.selection().has(fx.gi1));
    CHECK_FALSE(input.selection().has(fx.cons));
}

TEST_CASE("Input: dead units are unpickable") {
    MapFile map;
    if (!load_real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    world.set_move_speed(4.0);
    Camera cam;
    Input input(world, cam, 1280, 720);
    input.set_scheme(Input::Scheme::Legacy);   // pre-G-05 model under test
    CombatFixture fx = spawn_combat_fixture(world);
    if (!fx.ok) { MESSAGE("skipped: could not place combat fixture"); return; }

    int px, py;
    ground_px_for_unit(world.units()[fx.cons], px, py);

    // Tombstone the Conscript: a dead corpse (die_time first, then alive --
    // matches the brief's exact construction order; final state is the same
    // either way).
    world.units()[fx.cons].die_time = 0.0;
    world.units()[fx.cons].alive = false;

    left_click(input, px, py, /*shift=*/false, /*t=*/1000);
    CHECK(input.selection().empty());  // no selection change

    input.selection().set(fx.gi0);
    input.selection().set(fx.gi1);

    right_click(input, px, py, 2000);
    CHECK(world.units()[fx.gi0].target == -1);  // no attack order issued on a corpse
    CHECK(world.units()[fx.gi1].target == -1);

    int moving = 0;
    for (const auto& u : world.units()) moving += u.moving() ? 1 : 0;
    CHECK(moving >= 1);  // the right-click fell through to a ground move instead
}

TEST_CASE("Input: dying units (die anim playing) are equally unpickable") {
    MapFile map;
    if (!load_real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    world.set_move_speed(4.0);
    Camera cam;
    Input input(world, cam, 1280, 720);
    input.set_scheme(Input::Scheme::Legacy);   // pre-G-05 model under test
    CombatFixture fx = spawn_combat_fixture(world);
    if (!fx.ok) { MESSAGE("skipped: could not place combat fixture"); return; }

    int px, py;
    ground_px_for_unit(world.units()[fx.cons], px, py);

    // Unit is mid-death-animation: alive but die_time >= 0 (die_time tracks
    // the animation progress). The unit_under pick should skip it regardless.
    world.units()[fx.cons].die_time = 0.0;
    // alive stays true -- the mid-animation window

    left_click(input, px, py, /*shift=*/false, /*t=*/1000);
    CHECK(input.selection().empty());  // no selection change

    input.selection().set(fx.gi0);
    input.selection().set(fx.gi1);

    right_click(input, px, py, 2000);
    CHECK(world.units()[fx.gi0].target == -1);  // no attack order issued on a dying unit
    CHECK(world.units()[fx.gi1].target == -1);

    int moving = 0;
    for (const auto& u : world.units()) moving += u.moving() ? 1 : 0;
    CHECK(moving >= 1);  // the right-click fell through to a ground move instead
}

TEST_CASE("Input: right-click on RAISED ground targets the clicked tile (level-aware)") {
    MapFile map;
    if (!load_real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    world.set_move_speed(4.0);
    Camera cam;
    Input input(world, cam, 1280, 720);
    input.set_scheme(Input::Scheme::Legacy);   // pre-G-05 model under test
    Fixture fx = spawn(world, 1);
    if (!fx.ok) { MESSAGE("skipped: could not place 1 unit"); return; }

    const MapCell* raised =
        find_reachable_raised_cell(world, fx.cells[0].first, fx.cells[0].second);
    if (!raised) { MESSAGE("skipped: no reachable level>=1 cell found near the spawn"); return; }

    input.selection().set(0);

    int px, py;
    level_ground_px(raised->x, raised->y, raised->level, px, py);
    right_click(input, px, py, 3000);

    // Pre-fix, the level-0 inversion would land 1-2 cells off; the two-pass
    // level-aware inversion must resolve to exactly the clicked cell.
    REQUIRE(world.units()[0].moving());
    CHECK(world.units()[0].path.back().x == raised->x);
    CHECK(world.units()[0].path.back().y == raised->y);
}

TEST_CASE("Input: right-click a hostile with nothing selected is a ground move, not an attack") {
    MapFile map;
    if (!load_real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    world.set_move_speed(4.0);
    Camera cam;
    Input input(world, cam, 1280, 720);
    input.set_scheme(Input::Scheme::Legacy);   // pre-G-05 model under test
    CombatFixture fx = spawn_combat_fixture(world);
    if (!fx.ok) { MESSAGE("skipped: could not place combat fixture"); return; }

    // Right-click the Conscript's ground point with NOTHING selected --
    // the attack gate requires a live selection, so this must fall through
    // to a ground move. Since nothing is selected, order_move_selected no-ops,
    // but the important contract is that NO attack happened (all targets remain -1).
    int px, py;
    ground_px_for_unit(world.units()[fx.cons], px, py);
    right_click(input, px, py, 1000);

    // No unit should have an attack order or a target.
    for (const auto& u : world.units()) {
        CHECK(u.target == -1);
        CHECK_FALSE(u.attack_order);
    }
    // The Conscript itself should never be selected (hostile never selectable).
    CHECK_FALSE(input.selection().has(fx.cons));
}

// ---- Task 5: InputActions feedback (voice-ack hooks, audio phase) ----
// Accumulate-then-consume: handle() ORs flags into the accumulator (never
// resets it); take_actions() -- the game loop's once-per-frame consume
// point -- returns the accumulated struct AND clears it, so a same-batch
// Down+Up pair is preserved and an idle frame reads back all-false (no
// re-triggered acks from stale flags).

TEST_CASE("Input: take_actions reports selected + actor on a click-select, then consumes") {
    MapFile map;
    if (!load_real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    Camera cam;
    Input input(world, cam, 1280, 720);
    input.set_scheme(Input::Scheme::Legacy);   // pre-G-05 model under test
    Fixture fx = spawn(world, 3);
    if (!fx.ok) { MESSAGE("skipped: could not place 3 units"); return; }

    int px, py;
    ground_px_for_unit(world.units()[1], px, py);
    left_click(input, px, py, /*shift=*/false, /*t=*/1000);
    const ra2engine::InputActions a = input.take_actions();
    CHECK(a.selected);
    CHECK(a.actor == 1);
    CHECK_FALSE(a.move_ordered);
    CHECK_FALSE(a.attack_ordered);

    // The consume is real: an immediate second take (an idle frame -- no
    // events in between) reads back all-false / actor -1, so an ack can
    // never re-trigger from stale flags.
    const ra2engine::InputActions idle = input.take_actions();
    CHECK_FALSE(idle.selected);
    CHECK_FALSE(idle.move_ordered);
    CHECK_FALSE(idle.attack_ordered);
    CHECK(idle.actor == -1);

    // Click far-away empty ground: clears the selection and accumulates
    // nothing (a commit that ends with an empty selection is not an action).
    left_click(input, px + 900, py + 400, false, 2000);
    const ra2engine::InputActions e = input.take_actions();
    CHECK_FALSE(e.selected);
    CHECK_FALSE(e.move_ordered);
    CHECK_FALSE(e.attack_ordered);
    CHECK(e.actor == -1);
}

TEST_CASE("Input: a mouse-move-only frame after the consume reads back all-false") {
    MapFile map;
    if (!load_real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    Camera cam;
    Input input(world, cam, 1280, 720);
    input.set_scheme(Input::Scheme::Legacy);   // pre-G-05 model under test
    Fixture fx = spawn(world, 3);
    if (!fx.ok) { MESSAGE("skipped: could not place 3 units"); return; }

    int px, py;
    ground_px_for_unit(world.units()[0], px, py);
    left_click(input, px, py, false, 1000);
    REQUIRE(input.take_actions().selected);   // frame 1: select consumed

    // Frame 2: only a plain mouse move -- accumulates nothing.
    InputEvent mv;
    mv.kind = InputEvent::PointerMove;
    mv.x = px + 5; mv.y = py + 5; mv.t_ms = 1050;
    input.handle(mv);

    const ra2engine::InputActions a = input.take_actions();
    CHECK_FALSE(a.selected);
    CHECK_FALSE(a.move_ordered);
    CHECK_FALSE(a.attack_ordered);
    CHECK(a.actor == -1);
    // Selection itself is untouched by the move (pure observation, no behavior change).
    CHECK(input.selection().has(0));
}

TEST_CASE("Input: take_actions reports selected + lowest boxed index on a box-select") {
    MapFile map;
    if (!load_real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    Camera cam;
    Input input(world, cam, 1280, 720);
    input.set_scheme(Input::Scheme::Legacy);   // pre-G-05 model under test
    Fixture fx = spawn(world, 3);
    if (!fx.ok) { MESSAGE("skipped: could not place 3 units"); return; }

    // Box units 1 and 2 only (unit 0 stays outside -- same box geometry as
    // "Input: Shift+left-drag ADDS boxed units..." above), so the lowest
    // selected index after the box is 1, not 0.
    int p1x, p1y, p2x, p2y;
    ground_px_for_unit(world.units()[1], p1x, p1y);
    ground_px_for_unit(world.units()[2], p2x, p2y);
    const int bx0 = std::min(p1x, p2x) - 10, by0 = std::min(p1y, p2y) - 10;
    const int bx1 = std::max(p1x, p2x) + 10, by1 = std::max(p1y, p2y) + 10;
    InputEvent d; d.kind = InputEvent::PointerDown; d.button = InputEvent::Left;
    d.x = bx0; d.y = by0; d.t_ms = 1000;
    input.handle(d);
    InputEvent m; m.kind = InputEvent::PointerMove; m.x = bx1; m.y = by1; m.t_ms = 1050;
    input.handle(m);
    InputEvent u; u.kind = InputEvent::PointerUp; u.button = InputEvent::Left;
    u.x = bx1; u.y = by1; u.t_ms = 1100;
    input.handle(u);

    REQUIRE(input.selection().has(1));
    REQUIRE(input.selection().has(2));
    CHECK_FALSE(input.selection().has(0));
    const ra2engine::InputActions a = input.take_actions();
    CHECK(a.selected);
    CHECK(a.actor == 1);
}

TEST_CASE("Input: take_actions reports move_ordered + lowest actor on a right-click ground move") {
    MapFile map;
    if (!load_real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    world.set_move_speed(4.0);
    Camera cam;
    Input input(world, cam, 1280, 720);
    input.set_scheme(Input::Scheme::Legacy);   // pre-G-05 model under test
    CombatFixture fx = spawn_combat_fixture(world);
    if (!fx.ok) { MESSAGE("skipped: could not place combat fixture"); return; }

    input.selection().set(fx.gi0);
    input.selection().set(fx.gi1);

    // Empty ground well beyond weapon range (same scan as "Input: right-click
    // ground still group-moves the selection" above).
    const int bx = world.units()[fx.gi0].cell_x, by = world.units()[fx.gi0].cell_y;
    int gx = -1, gy = -1;
    for (int r = 5; r <= 60 && gx < 0; ++r) {
        const int cx = bx + r, cy = by;
        if (!world.walkable(cx, cy)) continue;
        const MapCell* mc = world.map().at(cx, cy);
        if (mc && mc->level == 0) { gx = cx; gy = cy; }
    }
    REQUIRE(gx >= 0);

    int px, py;
    px_for_cell(gx, gy, px, py);
    // right_click feeds BOTH the Down (which dispatches the order) and the
    // trailing Up before the take below -- exactly a real frame's event
    // batch. Accumulation must carry the Down-recorded action across the Up.
    right_click(input, px, py, 2000);

    const ra2engine::InputActions a = input.take_actions();
    CHECK(a.move_ordered);
    CHECK_FALSE(a.selected);
    CHECK_FALSE(a.attack_ordered);
    CHECK(a.actor == std::min(fx.gi0, fx.gi1));
}

// ---- Buildings & Construction Task 7a: placement mode ----

TEST_CASE("input: placement mode confirms at hovered origin and suppresses orders") {
    MapFile map;
    if (!load_real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    Camera cam;  // origin
    Input input(world, cam, 800, 600);
    input.set_scheme(Input::Scheme::Legacy);   // pre-G-05 model under test
    Fixture fx = spawn(world, 1);
    if (!fx.ok) { MESSAGE("skipped: could not place 1 unit"); return; }

    // Pick a level-0 map cell so the two-pass level-aware resolution collapses
    // to a single pass and lands exactly on the clicked cell (px_for_cell
    // projects at level 0; resolve_ground_cell is its exact inverse there).
    int tx = -1, ty = -1;
    for (const auto& mc : world.map().cells())
        if (mc.level == 0) { tx = mc.x; ty = mc.y; break; }
    REQUIRE(tx >= 0);

    input.begin_placement(/*type=*/1, /*fw=*/2, /*fh=*/2);
    CHECK(input.placement_active());
    CHECK(input.placement_type() == 1);

    int px, py;
    px_for_cell(tx, ty, px, py);
    left_click(input, px, py, /*shift=*/false, /*t=*/1000);
    const ra2engine::InputActions acts = input.take_actions();
    CHECK(acts.place_confirmed);
    CHECK(acts.place_cx == tx - 1);   // -fw/2
    CHECK(acts.place_cy == ty - 1);   // -fh/2
    CHECK_FALSE(acts.selected);       // the confirming click did NOT select
    CHECK_FALSE(input.selection().has(0));

    // Right-click cancels back to READY and reports place_cancelled.
    input.begin_placement(1, 2, 2);
    CHECK(input.placement_active());
    right_click(input, px, py, /*t=*/2000);
    CHECK(input.take_actions().place_cancelled);
    CHECK_FALSE(input.placement_active());
}

TEST_CASE("Input: take_actions reports attack_ordered + lowest actor on a right-click attack") {
    MapFile map;
    if (!load_real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    Camera cam;
    Input input(world, cam, 1280, 720);
    input.set_scheme(Input::Scheme::Legacy);   // pre-G-05 model under test
    CombatFixture fx = spawn_combat_fixture(world);
    if (!fx.ok) { MESSAGE("skipped: could not place combat fixture"); return; }

    input.selection().set(fx.gi0);
    input.selection().set(fx.gi1);

    int px, py;
    ground_px_for_unit(world.units()[fx.cons], px, py);
    // Down (dispatches the attack) + trailing Up both fed before the take,
    // as in a real frame's event batch (see the move case above).
    right_click(input, px, py, 1000);

    const ra2engine::InputActions a = input.take_actions();
    CHECK(a.attack_ordered);
    CHECK_FALSE(a.selected);
    CHECK_FALSE(a.move_ordered);
    CHECK(a.actor == std::min(fx.gi0, fx.gi1));
}

// ---- Final-review MF1: interactive attack-building right-click ----

TEST_CASE("Input: right-click a hostile building attacks; own building is a move") {
    MapFile map;
    if (!load_real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    world.set_move_speed(4.0);
    Camera cam;
    Input input(world, cam, 1280, 720);
    input.set_scheme(Input::Scheme::Legacy);   // pre-G-05 model under test
    CombatFixture fx = spawn_combat_fixture(world);
    if (!fx.ok) { MESSAGE("skipped: could not place combat fixture"); return; }

    // Two well-separated level-0 cells for the foundations -- level 0 so the
    // level-blind px_for_cell projection is the exact inverse of Input's
    // resolve_ground_cell (as the placement test relies on). Keep both clear
    // of the unit cluster so unit_under can't intercept the pick.
    std::vector<std::pair<int, int>> l0;
    for (const auto& mc : world.map().cells())
        if (mc.level == 0 && world.unit_at_cell(mc.x, mc.y) < 0 &&
            std::abs(mc.x - world.units()[fx.gi0].cell_x) > 6)
            l0.push_back({mc.x, mc.y});
    if (l0.size() < 2) { MESSAGE("skipped: not enough level-0 cells"); return; }
    // Ensure the two 2x2 foundations don't overlap.
    const auto hostile_o = l0.front();
    std::pair<int, int> friendly_o{-1, -1};
    for (const auto& c : l0)
        if (std::abs(c.first - hostile_o.first) > 3 || std::abs(c.second - hostile_o.second) > 3) {
            friendly_o = c;
            break;
        }
    if (friendly_o.first < 0) { MESSAGE("skipped: no disjoint foundation cell"); return; }

    const int hostile_bi = world.add_building(/*type=*/1, hostile_o.first, hostile_o.second,
                                              /*fw=*/2, /*fh=*/2, /*house=*/1, /*strength=*/1000,
                                              /*armor_ix=*/0, /*buildup_total=*/0.0, /*instant=*/true);
    const int friendly_bi = world.add_building(/*type=*/1, friendly_o.first, friendly_o.second,
                                               /*fw=*/2, /*fh=*/2, /*house=*/0, /*strength=*/1000,
                                               /*armor_ix=*/0, /*buildup_total=*/0.0, /*instant=*/true);
    REQUIRE(hostile_bi >= 0);
    REQUIRE(friendly_bi >= 0);

    input.selection().set(fx.gi0);
    input.selection().set(fx.gi1);

    // Right-click the HOSTILE building's foundation origin -> attack order.
    int px, py;
    px_for_cell(hostile_o.first, hostile_o.second, px, py);
    right_click(input, px, py, 1000);
    const ra2engine::InputActions a = input.take_actions();
    CHECK(a.attack_ordered);
    CHECK_FALSE(a.move_ordered);
    CHECK(a.actor == std::min(fx.gi0, fx.gi1));
    CHECK(world.units()[fx.gi0].target_building == hostile_bi);
    CHECK(world.units()[fx.gi1].target_building == hostile_bi);

    // Right-click the player's OWN building's foundation origin -> ground move,
    // NOT an attack (house 0 is never an attack target).
    px_for_cell(friendly_o.first, friendly_o.second, px, py);
    right_click(input, px, py, 2000);
    const ra2engine::InputActions b = input.take_actions();
    CHECK(b.move_ordered);
    CHECK_FALSE(b.attack_ordered);
    // The own-building click issued no building attack: target_building stays
    // pointed at the earlier hostile order for neither unit (move clears it).
    CHECK(world.units()[fx.gi0].target_building == -1);
    CHECK(world.units()[fx.gi1].target_building == -1);
}

// ---- Economy fix round: harvester right-click orders ----

namespace {
// Minimal harvester profile (image "CMIN"; combat stats irrelevant to input).
ra2engine::UnitCombat miner_combat() {
    return ra2engine::UnitCombat{1000, "heavy", "CMIN", 0, 0, 0.0, kSAVerses};
}

// First walkable, unit-free, ore-free LEVEL-0 cell at least `min_d` cells
// (Chebyshev) from (ax, ay) -- level 0 so px_for_cell is the exact inverse of
// Input's resolve_ground_cell (same rule the attack-building test relies on).
std::pair<int, int> far_l0_cell(const World& world, int ax, int ay, int min_d) {
    for (const auto& mc : world.map().cells()) {
        if (mc.level != 0) continue;
        if (!world.walkable(mc.x, mc.y) || world.unit_at_cell(mc.x, mc.y) >= 0) continue;
        if (world.ore().has_ore(mc.x, mc.y)) continue;
        if (std::max(std::abs(mc.x - ax), std::abs(mc.y - ay)) < min_d) continue;
        return {mc.x, mc.y};
    }
    return {-1, -1};
}
}  // namespace

TEST_CASE("Input: mixed selection right-click on ore -> harvester harvests, others move") {
    MapFile map;
    if (!load_real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    world.set_move_speed(4.0);
    Camera cam;
    Input input(world, cam, 1280, 720);
    input.set_scheme(Input::Scheme::Legacy);   // pre-G-05 model under test

    // One GI (non-harvester) + one harvester side by side near the start.
    const MapCell* c = ra2engine::pick_start_cell(world.map());
    REQUIRE(c != nullptr);
    int gi = -1, harv = -1;
    for (int dx = 0; dx < 10 && (gi < 0 || harv < 0); ++dx) {
        const int x = c->x + dx, y = c->y;
        if (!world.walkable(x, y) || world.unit_at_cell(x, y) >= 0) continue;
        if (gi < 0) {
            world.add_combat_unit(x, y, 0, gi_combat());
            gi = static_cast<int>(world.units().size()) - 1;
        } else {
            world.add_harvester(x, y, 0, miner_combat(), /*chrono=*/true, /*storage=*/20);
            harv = static_cast<int>(world.units().size()) - 1;
        }
    }
    if (gi < 0 || harv < 0) { MESSAGE("skipped: could not place fixture"); return; }

    // A level-0 ore cell away from the units (planted ore -- the click target).
    const auto oc = far_l0_cell(world, world.units()[gi].cell_x, world.units()[gi].cell_y, 8);
    if (oc.first < 0) { MESSAGE("skipped: no suitable level-0 cell"); return; }
    world.ore().set(oc.first, oc.second, ra2engine::OreKind::Ore, 5, 0);
    REQUIRE(world.ore().has_ore(oc.first, oc.second));

    input.selection().set(gi);
    input.selection().set(harv);

    int px, py;
    px_for_cell(oc.first, oc.second, px, py);
    right_click(input, px, py, 1000);

    // Harvester: harvest mission pinned to the clicked cell (order_harvest).
    CHECK(world.units()[harv].harv == ra2engine::HarvState::MoveToOre);
    CHECK(world.units()[harv].harv_cell_x == oc.first);
    CHECK(world.units()[harv].harv_cell_y == oc.second);
    CHECK_FALSE(world.units()[harv].manual_hold);  // a move order would set this

    // Non-harvester: normal ground move toward the same cell (formation cell
    // near the goal), NOT stranded.
    CHECK(world.units()[gi].moving());
    REQUIRE_FALSE(world.units()[gi].path.empty());
    CHECK(near_cell(world.units()[gi].path.back(), oc.first, oc.second));

    // Selection restored: both still selected; the move ack was reported.
    CHECK(input.selection().has(gi));
    CHECK(input.selection().has(harv));
    const ra2engine::InputActions a = input.take_actions();
    CHECK(a.move_ordered);
}

TEST_CASE("Input: harvester right-click own building returns only on a refinery") {
    MapFile map;
    if (!load_real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    world.set_move_speed(4.0);
    Camera cam;
    Input input(world, cam, 1280, 720);
    input.set_scheme(Input::Scheme::Legacy);   // pre-G-05 model under test
    input.set_refinery_types({5});  // roster type 5 = the refinery in this fixture

    const MapCell* c = ra2engine::pick_start_cell(world.map());
    REQUIRE(c != nullptr);
    int harv = -1;
    for (int dx = 0; dx < 10 && harv < 0; ++dx) {
        const int x = c->x + dx, y = c->y;
        if (world.walkable(x, y) && world.unit_at_cell(x, y) < 0) {
            world.add_harvester(x, y, 0, miner_combat(), /*chrono=*/true, /*storage=*/20);
            harv = static_cast<int>(world.units().size()) - 1;
        }
    }
    if (harv < 0) { MESSAGE("skipped: could not place harvester"); return; }
    const int hx = world.units()[harv].cell_x, hy = world.units()[harv].cell_y;

    // Two disjoint 2x2 own buildings on far level-0 ground: type 1 (NOT a
    // refinery) and type 5 (the refinery per set_refinery_types above).
    const auto o1 = far_l0_cell(world, hx, hy, 8);
    if (o1.first < 0) { MESSAGE("skipped: no level-0 cell"); return; }
    std::pair<int, int> o2{-1, -1};
    for (const auto& mc : world.map().cells()) {
        if (mc.level != 0 || !world.walkable(mc.x, mc.y)) continue;
        if (std::max(std::abs(mc.x - hx), std::abs(mc.y - hy)) < 8) continue;
        if (std::abs(mc.x - o1.first) <= 3 && std::abs(mc.y - o1.second) <= 3) continue;
        o2 = {mc.x, mc.y};
        break;
    }
    if (o2.first < 0) { MESSAGE("skipped: no disjoint cell"); return; }
    world.add_building(/*type=*/1, o1.first, o1.second, 2, 2, /*house=*/0, 1000, 0, 0.0,
                       /*instant=*/true);
    world.add_building(/*type=*/5, o2.first, o2.second, 2, 2, /*house=*/0, 1000, 0, 0.0,
                       /*instant=*/true);

    input.selection().set(harv);

    // Right-click the own NON-refinery building: falls through to the ground
    // move -- no return order (harv stays off ReturnToRef, manual_hold set by
    // the move path).
    int px, py;
    px_for_cell(o1.first, o1.second, px, py);
    right_click(input, px, py, 1000);
    CHECK(world.units()[harv].harv != ra2engine::HarvState::ReturnToRef);
    CHECK(world.units()[harv].manual_hold);  // proves the MOVE path ran instead

    // Right-click the own REFINERY: return order.
    px_for_cell(o2.first, o2.second, px, py);
    right_click(input, px, py, 2000);
    CHECK(world.units()[harv].harv == ra2engine::HarvState::ReturnToRef);
}

// ---- Production phase: factory select / rally / primary [RE: production.md §1.2/§1.4] ----

TEST_CASE("Input: left-click own unit factory selects it; re-click primes; elsewhere clears") {
    MapFile map;
    if (!load_real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    Camera cam;
    Input input(world, cam, 1280, 720);
    input.set_scheme(Input::Scheme::Legacy);   // pre-G-05 model under test
    input.set_unit_factory_types({3});   // roster type 3 = a unit factory in this fixture
    input.set_vehicle_factory_types({3});

    const MapCell* c = ra2engine::pick_start_cell(world.map());
    REQUIRE(c != nullptr);
    // Two own factories (type 3) on disjoint level-0 cells.
    const auto o0 = far_l0_cell(world, c->x, c->y, 6);
    if (o0.first < 0) { MESSAGE("skipped: no level-0 cell"); return; }
    std::pair<int, int> o1{-1, -1};
    for (const auto& mc : world.map().cells()) {
        if (mc.level != 0 || !world.walkable(mc.x, mc.y)) continue;
        if (std::max(std::abs(mc.x - c->x), std::abs(mc.y - c->y)) < 6) continue;
        if (std::abs(mc.x - o0.first) <= 3 && std::abs(mc.y - o0.second) <= 3) continue;
        o1 = {mc.x, mc.y};
        break;
    }
    if (o1.first < 0) { MESSAGE("skipped: no disjoint cell"); return; }
    const int b0 = world.add_building(3, o0.first, o0.second, 2, 2, 0, 1000, 0, 0.0, true);
    const int b1 = world.add_building(3, o1.first, o1.second, 2, 2, 0, 1000, 0, 0.0, true);
    REQUIRE(b0 >= 0);
    REQUIRE(b1 >= 0);
    world.buildings()[b1].primary = true;  // a pre-existing primary sibling to be cleared

    // Left-click b0's origin selects it (no unit selection touched).
    int px, py;
    px_for_cell(o0.first, o0.second, px, py);
    left_click(input, px, py, /*shift=*/false, /*t=*/1000);
    CHECK(input.selected_building() == b0);

    // Re-click b0 (well past the double-click window) -> primary set on b0,
    // cleared on the sibling b1 (exactly one primary per category per house).
    left_click(input, px, py, /*shift=*/false, /*t=*/5000);
    CHECK(world.buildings()[b0].primary);
    CHECK_FALSE(world.buildings()[b1].primary);
    CHECK(input.selected_building() == b0);

    // Left-click empty ground drops the factory selection.
    const auto empty = far_l0_cell(world, o0.first, o0.second, 12);
    if (empty.first >= 0) {
        int ex, ey;
        px_for_cell(empty.first, empty.second, ex, ey);
        left_click(input, ex, ey, /*shift=*/false, /*t=*/9000);
        CHECK(input.selected_building() == -1);
    }
}

TEST_CASE("Input: right-click with a factory selected sets its rally point") {
    MapFile map;
    if (!load_real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    Camera cam;
    Input input(world, cam, 1280, 720);
    input.set_scheme(Input::Scheme::Legacy);   // pre-G-05 model under test
    input.set_unit_factory_types({3});

    const MapCell* c = ra2engine::pick_start_cell(world.map());
    REQUIRE(c != nullptr);
    const auto o = far_l0_cell(world, c->x, c->y, 6);
    if (o.first < 0) { MESSAGE("skipped: no level-0 cell"); return; }
    const int b = world.add_building(3, o.first, o.second, 2, 2, 0, 1000, 0, 0.0, true);
    REQUIRE(b >= 0);

    int px, py;
    px_for_cell(o.first, o.second, px, py);
    left_click(input, px, py, /*shift=*/false, /*t=*/1000);
    REQUIRE(input.selected_building() == b);

    // Right-click a far level-0 cell -> rally cell set to exactly that cell.
    const auto r = far_l0_cell(world, o.first, o.second, 8);
    if (r.first < 0) { MESSAGE("skipped: no rally cell"); return; }
    int rx, ry;
    px_for_cell(r.first, r.second, rx, ry);
    right_click(input, rx, ry, 2000);
    CHECK(world.buildings()[b].has_rally);
    CHECK(world.buildings()[b].rally_x == r.first);
    CHECK(world.buildings()[b].rally_y == r.second);
}

TEST_CASE("Input: an enemy factory is never selected or rallied (own-house gating)") {
    MapFile map;
    if (!load_real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    Camera cam;
    Input input(world, cam, 1280, 720);
    input.set_scheme(Input::Scheme::Legacy);   // pre-G-05 model under test
    input.set_unit_factory_types({3});

    const MapCell* c = ra2engine::pick_start_cell(world.map());
    REQUIRE(c != nullptr);
    const auto o = far_l0_cell(world, c->x, c->y, 6);
    if (o.first < 0) { MESSAGE("skipped: no level-0 cell"); return; }
    // Enemy (house 1) factory of the same type.
    const int b = world.add_building(3, o.first, o.second, 2, 2, 1, 1000, 0, 0.0, true);
    REQUIRE(b >= 0);

    int px, py;
    px_for_cell(o.first, o.second, px, py);
    left_click(input, px, py, /*shift=*/false, /*t=*/1000);
    CHECK(input.selected_building() == -1);  // enemy factory is never selectable

    // A right-click (nothing selected) must not set a rally on the enemy factory.
    right_click(input, px, py, 2000);
    CHECK_FALSE(world.buildings()[b].has_rally);
}

TEST_CASE("Input: right-click on an own transport issues the enter order; carried units are unpickable") {
    MapFile map;
    if (!load_real_map(map)) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    World world(map);
    const MapCell* c = ra2engine::pick_start_cell(map);
    REQUIRE(c != nullptr);
    // An FV-shaped transport (1 slot, SizeLimit 2) + a GI on nearby cells.
    ra2engine::UnitCombat tc = gi_combat();
    tc.image = "FV";
    tc.is_vehicle = true;
    tc.passengers = 1;
    tc.size_limit = 2;
    tc.size = 3;
    int tx = -1, ty = -1, gx = -1, gy = -1;
    for (int dx = 0; dx < 10 && gx < 0; ++dx) {
        const int x = c->x + dx, y = c->y;
        if (!world.walkable(x, y) || world.unit_at_cell(x, y) >= 0) continue;
        if (tx < 0) { tx = x; ty = y; continue; }
        gx = x; gy = y;
    }
    REQUIRE(gx >= 0);
    world.add_combat_unit(tx, ty, 0, tc);            // unit 0: the transport
    world.add_combat_unit(gx, gy, 0, gi_combat());   // unit 1: the GI
    Camera cam;                                      // identity camera
    Input in(world, cam, 4000, 4000);
    in.set_scheme(Input::Scheme::Legacy);   // pre-G-05 model under test
    in.selection().set(1);
    int px = 0, py = 0;
    ground_px_for_unit(world.units()[0], px, py);
    right_click(in, px, py, 10);
    CHECK(world.units()[1].enter_target == 0);       // enter order, not a move
    CHECK(world.units()[0].enter_target == -1);      // transport itself untouched
    CHECK(world.units()[1].target == -1);            // no attack leaked through
    // A CARRIED unit is unpickable (sweep site 12): stuff the GI aboard by
    // hand and click its (stale) ground point -- nothing selects.
    in.selection().clear();
    world.units()[1].cargo_of = 0;
    ground_px_for_unit(world.units()[1], px, py);
    left_click(in, px, py, false, 500);
    CHECK_FALSE(in.selection().has(1));
}

// ---- Repair & Sell phase, Task 6: the two cursor modes [RE: repair-sell.md §C1] ----

TEST_CASE("input: repair/sell modes are mutually exclusive and TOGGLE [RE: repair-sell.md §C1]") {
    MapFile map;
    if (!load_real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    Camera cam;
    Input in(world, cam, 1280, 720);
    in.set_scheme(Input::Scheme::Legacy);   // pre-G-05 model under test
    Fixture fx = spawn(world, 1);
    REQUIRE(fx.ok);
    in.set_owned_buildings(1);

    CHECK(in.base_mode() == Input::BaseMode::None);
    in.set_repair_mode(-1);
    CHECK(in.base_mode() == Input::BaseMode::Repair);
    in.set_repair_mode(-1);                       // both buttons TOGGLE
    CHECK(in.base_mode() == Input::BaseMode::None);
    in.set_repair_mode(1);
    in.set_sell_mode(1);                          // arming Sell clears Repair
    CHECK(in.base_mode() == Input::BaseMode::Sell);
    in.set_repair_mode(0);                        // turning an OFF mode off: no-op
    CHECK(in.base_mode() == Input::BaseMode::Sell);
    in.set_sell_mode(0);
    CHECK(in.base_mode() == Input::BaseMode::None);
    SUBCASE("arming clears sw_targeting") {
        in.arm_superweapon();
        in.set_repair_mode(1);
        CHECK(in.base_mode() == Input::BaseMode::Repair);
        CHECK_FALSE(in.sw_targeting());
    }
    SUBCASE("PLACEMENT WINS -- arming is refused while placing") {
        in.begin_placement(0, 2, 2);
        in.set_repair_mode(1);
        CHECK(in.base_mode() == Input::BaseMode::None);
        CHECK(in.placement_active());
    }
    SUBCASE("refused with no owned buildings [§D5]") {
        in.set_owned_buildings(0);
        in.set_sell_mode(1);
        CHECK(in.base_mode() == Input::BaseMode::None);
    }
    SUBCASE("arming DESELECTS everything (Reset_Cursor)") {
        in.selection().set(0);
        in.set_repair_mode(1);
        CHECK_FALSE(in.selection().has(0));
    }
}

TEST_CASE("input: cancel_one_mode follows the recovered priority chain [RE: repair-sell.md §C1]") {
    MapFile map;
    if (!load_real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    Camera cam;
    Input in(world, cam, 1280, 720);
    in.set_scheme(Input::Scheme::Legacy);   // pre-G-05 model under test
    in.set_owned_buildings(1);
    in.begin_placement(0, 2, 2);
    in.set_repair_mode(1);                        // refused (placement wins)
    CHECK(in.base_mode() == Input::BaseMode::None);
    CHECK(in.cancel_one_mode());                  // 1st press: the placement
    CHECK_FALSE(in.placement_active());
    in.set_repair_mode(1);
    in.arm_superweapon();                         // (arm order: SW last)
    CHECK(in.cancel_one_mode());                  // 2nd: Repair before SW
    CHECK(in.base_mode() == Input::BaseMode::None);
    CHECK(in.sw_targeting());
    CHECK(in.cancel_one_mode());                  // 3rd: the superweapon
    CHECK_FALSE(in.sw_targeting());
    CHECK_FALSE(in.cancel_one_mode());            // nothing left
}

TEST_CASE("input: a mode click reports OWN buildings only, and stays armed [RE: repair-sell.md §C1]") {
    MapFile map;
    if (!load_real_map(map)) { MESSAGE("skipped: no install"); return; }
    World world(map);
    Camera cam;
    Input in(world, cam, 1280, 720);
    in.set_scheme(Input::Scheme::Legacy);   // pre-G-05 model under test
    Fixture fx = spawn(world, 1);
    REQUIRE(fx.ok);
    const int ux = fx.cells[0].first, uy = fx.cells[0].second;

    // Two disjoint level-0 foundations away from the unit (level 0 so the
    // level-blind px_for_cell is the exact inverse of resolve_ground_cell).
    std::vector<std::pair<int, int>> l0;
    for (const auto& mc : world.map().cells())
        if (mc.level == 0 && world.unit_at_cell(mc.x, mc.y) < 0 && std::abs(mc.x - ux) > 6)
            l0.push_back({mc.x, mc.y});
    if (l0.size() < 2) { MESSAGE("skipped: not enough level-0 cells"); return; }
    const auto own_o = l0.front();
    std::pair<int, int> enemy_o{-1, -1};
    for (const auto& c : l0)
        if (std::abs(c.first - own_o.first) > 3 || std::abs(c.second - own_o.second) > 3) {
            enemy_o = c;
            break;
        }
    if (enemy_o.first < 0) { MESSAGE("skipped: no disjoint foundation cell"); return; }
    const int b_own = world.add_building(1, own_o.first, own_o.second, 2, 2, /*house*/ 0, 1000,
                                         0, 0.0, true);
    const int b_enemy = world.add_building(1, enemy_o.first, enemy_o.second, 2, 2, /*house*/ 1,
                                           1000, 0, 0.0, true);
    (void)b_enemy;
    in.set_owned_buildings(1);
    in.set_sell_mode(1);

    int px, py;
    px_for_cell(own_o.first, own_o.second, px, py);
    left_click(in, px, py, false, 1000);
    const ra2engine::InputActions a = in.take_actions();
    CHECK(a.sell_clicked);
    CHECK(a.sell_building == b_own);
    CHECK_FALSE(a.repair_clicked);
    CHECK(in.base_mode() == Input::BaseMode::Sell);          // NOT one-shot

    SUBCASE("repair mode reports repair_clicked") {
        in.set_repair_mode(1);
        left_click(in, px, py, false, 2000);
        const ra2engine::InputActions r = in.take_actions();
        CHECK(r.repair_clicked);
        CHECK(r.repair_building == b_own);
        CHECK_FALSE(r.sell_clicked);
    }
    SUBCASE("an enemy building reports NOTHING") {
        int ex, ey;
        px_for_cell(enemy_o.first, enemy_o.second, ex, ey);
        left_click(in, ex, ey, false, 2000);
        const ra2engine::InputActions e = in.take_actions();
        CHECK_FALSE(e.sell_clicked);
        CHECK_FALSE(e.repair_clicked);
    }
    SUBCASE("a right-click cancels the mode") {
        right_click(in, px, py, 2000);
        CHECK(in.base_mode() == Input::BaseMode::None);
        CHECK_FALSE(in.take_actions().move_ordered);
    }
    SUBCASE("no selection leaks through while armed") {
        const ra2engine::Unit& u = world.units()[0];
        int gx, gy;
        ground_px_for_unit(u, gx, gy);
        left_click(in, gx, gy, false, 2000);
        CHECK_FALSE(in.selection().has(0));
        CHECK_FALSE(in.take_actions().selected);
    }
}
