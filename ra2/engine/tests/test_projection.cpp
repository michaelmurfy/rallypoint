#include "doctest.h"
#include "iso_project.hpp"
#include <algorithm>

using ra2engine::ScreenPt;
using ra2engine::cell_to_screen;
using ra2engine::screen_to_cell;

TEST_CASE("proj: diamond stagger sanity (tile 60x30)") {
    const int tw = 60, th = 30, lp = 10;

    ScreenPt p = cell_to_screen(0, 0, 0, tw, th, lp);
    CHECK(p.x == 0);
    CHECK(p.y == 0);

    p = cell_to_screen(1, 0, 0, tw, th, lp);
    CHECK(p.x == tw / 2);
    CHECK(p.y == th / 2);

    p = cell_to_screen(0, 1, 0, tw, th, lp);
    CHECK(p.x == -(tw / 2));
    CHECK(p.y == th / 2);

    p = cell_to_screen(1, 1, 0, tw, th, lp);
    CHECK(p.x == 0);
    CHECK(p.y == th);

    // level raises the tile on screen: py decreases by level * level_px,
    // px is unaffected.
    ScreenPt base = cell_to_screen(3, 4, 0, tw, th, lp);
    ScreenPt raised = cell_to_screen(3, 4, 2, tw, th, lp);
    CHECK(raised.x == base.x);
    CHECK(raised.y == base.y - 2 * lp);
}

TEST_CASE("proj: diamond stagger sanity (tile 48x24)") {
    const int tw = 48, th = 24, lp = 8;

    ScreenPt p = cell_to_screen(0, 0, 0, tw, th, lp);
    CHECK(p.x == 0);
    CHECK(p.y == 0);

    p = cell_to_screen(1, 0, 0, tw, th, lp);
    CHECK(p.x == tw / 2);
    CHECK(p.y == th / 2);

    p = cell_to_screen(0, 1, 0, tw, th, lp);
    CHECK(p.x == -(tw / 2));
    CHECK(p.y == th / 2);

    p = cell_to_screen(1, 1, 0, tw, th, lp);
    CHECK(p.x == 0);
    CHECK(p.y == th);
}

TEST_CASE("proj: round-trip grid 0..20, tile 60x30 (temperate)") {
    const int tw = 60, th = 30, lp = 10;
    for (int x = 0; x <= 20; ++x) {
        for (int y = 0; y <= 20; ++y) {
            ScreenPt p = cell_to_screen(x, y, 0, tw, th, lp);
            int rx = -1, ry = -1;
            screen_to_cell(p.x, p.y, tw, th, rx, ry);
            CHECK(rx == x);
            CHECK(ry == y);
        }
    }
}

TEST_CASE("proj: round-trip grid 0..20, tile 48x24") {
    const int tw = 48, th = 24, lp = 8;
    for (int x = 0; x <= 20; ++x) {
        for (int y = 0; y <= 20; ++y) {
            ScreenPt p = cell_to_screen(x, y, 0, tw, th, lp);
            int rx = -1, ry = -1;
            screen_to_cell(p.x, p.y, tw, th, rx, ry);
            CHECK(rx == x);
            CHECK(ry == y);
        }
    }
}

TEST_CASE("proj: round-trip with negative cells, tile 60x30") {
    const int tw = 60, th = 30, lp = 10;
    for (int x = -10; x <= 10; ++x) {
        for (int y = -10; y <= 10; ++y) {
            ScreenPt p = cell_to_screen(x, y, 0, tw, th, lp);
            int rx = -999, ry = -999;
            screen_to_cell(p.x, p.y, tw, th, rx, ry);
            CHECK(rx == x);
            CHECK(ry == y);
        }
    }
}

TEST_CASE("proj: round-trip ignores level (level offset stripped by caller)") {
    // screen_to_cell only inverts the level-0 plane; verify that inverting
    // a level-0 projection for the same (x,y) still matches regardless of
    // what level was used elsewhere (i.e. level never leaks into px).
    const int tw = 60, th = 30, lp = 10;
    ScreenPt p_lvl0 = cell_to_screen(5, 7, 0, tw, th, lp);
    ScreenPt p_lvl3 = cell_to_screen(5, 7, 3, tw, th, lp);
    CHECK(p_lvl0.x == p_lvl3.x);
    int rx = -1, ry = -1;
    screen_to_cell(p_lvl0.x, p_lvl0.y, tw, th, rx, ry);
    CHECK(rx == 5);
    CHECK(ry == 7);
}

// W4-FIX-POS [RE: FUN_00459ee0 / game.exe FUN_00453c70]: the building canvas
// centre is the origin cell's TOP vertex for every foundation size. The old
// clone anchor (foundation bbox centre) sat ((fw-fh)*15, (fw+fh)*7.5) px
// away -- the audit's per-size error table (docs/gap/wave4/01-positioning.md §2).
TEST_CASE("proj: building draw point = origin cell top vertex, any foundation") {
    struct F { int fw, fh, ex, ey; };
    const F sizes[] = {{1, 1, 0, 15}, {2, 2, 0, 30}, {3, 3, 0, 45}, {4, 4, 0, 60}, {3, 4, -15, 52}, {2, 6, -60, 60}};
    for (const F& f : sizes) {
        const int cx = 61, cy = 69, level = 6;
        const ra2engine::ScreenPt d = ra2engine::building_draw_point(cx, cy, level);
        const ra2engine::ScreenPt o = ra2engine::cell_to_screen(cx, cy, level, 60, 30, 15);
        CHECK(d.x == o.x + 30);
        CHECK(d.y == o.y);
        int minx = 1 << 30, miny = 1 << 30, maxx = -(1 << 30), maxy = -(1 << 30);
        for (int j = 0; j < f.fh; ++j)
            for (int i = 0; i < f.fw; ++i) {
                const ra2engine::ScreenPt p = ra2engine::cell_to_screen(cx + i, cy + j, level, 60, 30, 15);
                minx = std::min(minx, p.x); miny = std::min(miny, p.y);
                maxx = std::max(maxx, p.x + 60); maxy = std::max(maxy, p.y + 30);
            }
        CHECK((minx + maxx) / 2 - d.x == f.ex);
        CHECK((miny + maxy) / 2 - d.y == f.ey);
    }
}

TEST_CASE("proj: overlay y offset table (FUN_005fd4b0 / FUN_00480020)") {
    CHECK(ra2engine::overlay_y_offset(false, false, 5, false, 0) == 0);     // rock / low bridge
    CHECK(ra2engine::overlay_y_offset(true, false, 5, false, 0) == -12);    // wall / ore / crate
    CHECK(ra2engine::overlay_y_offset(false, true, 5, false, 0) == -1);     // railroad
    CHECK(ra2engine::overlay_y_offset(false, false, 0x7E, false, 0) == -13);
    CHECK(ra2engine::overlay_y_offset(false, false, 25, true, 0) == -16);   // high bridge
    CHECK(ra2engine::overlay_y_offset(false, false, 25, true, 9) == -31);
    CHECK(ra2engine::overlay_y_offset(false, false, 25, true, 17) == -31);
    CHECK(ra2engine::overlay_y_offset(false, false, 25, true, 18) == -16);
    CHECK(ra2engine::overlay_y_offset(false, false, 0xEF, false, 0) == -15);
    const ra2engine::ScreenPt c = ra2engine::overlay_draw_center(3, 4, 2, -12);
    const ra2engine::ScreenPt p = ra2engine::cell_to_screen(3, 4, 2, 60, 30, 15);
    CHECK(c.x == p.x + 30);
    CHECK(c.y == p.y + 3);
    const ra2engine::ScreenPt v = ra2engine::cell_top_vertex(3, 4, 2);
    CHECK(v.x == p.x + 30);
    CHECK(v.y == p.y);
}
