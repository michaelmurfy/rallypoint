#include "doctest.h"
#include "../engine/logical_canvas.hpp"

static void canvas(int ww, int wh, int& w, int& h) {
    ra2engine::desktop_canvas(ww, wh, w, h);
}

TEST_CASE("desktop logical canvas follows the window, 720..1440") {
    int w = 0, h = 0;
    canvas(800, 600, w, h);
    CHECK(w == 1280);
    CHECK(h == 720);
    canvas(1280, 720, w, h);
    CHECK(w == 1280);
    CHECK(h == 720);
    canvas(1600, 900, w, h);
    CHECK(w == 1600);
    CHECK(h == 900);
    canvas(1920, 1080, w, h);
    CHECK(w == 1920);
    CHECK(h == 1080);
    canvas(2560, 1440, w, h);   // 1440p: 2x, so the sidebar is 336 screen pixels
    CHECK(w == 1280);
    CHECK(h == 720);
    canvas(3440, 1440, w, h);   // ultrawide: 2x, 1720x720, the sidebar 336 of 3440
    CHECK(w == 1720);
    CHECK(h == 720);
    canvas(5120, 1440, w, h);   // wider than ~21:9 stops gaining map
    CHECK(w == 720 * 239 / 100);
    CHECK(h == 720);
    // RA2_LOGICAL_HEIGHT path
    ra2engine::logical_size_for_height(900, 1920, 1080, w, h);
    CHECK((w == 1600 && h == 900));
}

using namespace ra2engine;

TEST_CASE("HiDPI canvas is the window over a whole number") {
    int w = 0, h = 0;
    desktop_canvas(1920, 1080, w, h);
    CHECK((w == 1920 && h == 1080));
    desktop_canvas(3840, 2160, w, h);   // 4K: 2x, a 1080-line canvas
    CHECK((w == 1920 && h == 1080));
    desktop_canvas(2880, 1864, w, h);   // 14" Retina: 2x
    CHECK((w == 932 * 16 / 9 && h == 932));   // 16:10 panel: 16:9 floor, letterboxed
    desktop_canvas(5120, 2880, w, h);   // 5K: 3x, 960 lines
    CHECK((w == 1706 && h == 960));
    desktop_canvas(1024, 600, w, h);    // small: the 720 floor
    CHECK(h == 720);
    CHECK(desktop_pixel_scale(1080) == 1);
    CHECK(desktop_pixel_scale(1299) == 1);
    CHECK(desktop_pixel_scale(1300) == 2);
    CHECK(desktop_pixel_scale(1440) == 2);
    CHECK(desktop_pixel_scale(2160) == 2);
    CHECK(desktop_pixel_scale(2880) == 3);
    // the sidebar is never smaller than 2x on a 1440-line screen
    CHECK(168 * (1440 / desktop_pixel_scale(1440) > 0 ? desktop_pixel_scale(1440) : 1) >= 2 * 168);
}

TEST_CASE("present mode: whole scale is exact, fractional pre-scales") {
    PresentMode m = present_mode(1920, 1080, 1920, 1080);
    CHECK((m.exact && m.scale == 1));
    m = present_mode(3840, 2160, 1920, 1080);
    CHECK((m.exact && m.scale == 2));
    m = present_mode(2880, 1864, 1440, 932);   // 1864 = 2 * 932
    CHECK((m.exact && m.scale == 2));
    m = present_mode(1920, 1080, 1280, 720);   // menus on 1080p: 1.5x
    CHECK(!m.exact);
    CHECK(m.prescale == 2);
    m = present_mode(1000, 600, 1280, 720);    // window smaller than the canvas
    CHECK(!m.exact);
    CHECK(m.prescale == 1);
}

TEST_CASE("tactical zoom limits and steps") {
    CHECK(zoom_min(1.0) == doctest::Approx(0.75));
    CHECK(zoom_min(2.0) == doctest::Approx(0.5));
    CHECK(zoom_min(1.5) == doctest::Approx(0.6666).epsilon(0.01));
    CHECK(zoom_min(4.0) == doctest::Approx(0.5));
    CHECK(zoom_min(0.5) == doctest::Approx(0.75));
    CHECK(zoom_step(1.0, -1, 0.75) == doctest::Approx(0.875));
    CHECK(zoom_step(0.875, -1, 0.75) == doctest::Approx(0.75));
    CHECK(zoom_step(0.75, -1, 0.75) == doctest::Approx(0.75));
    CHECK(zoom_step(0.75, 1, 0.75) == doctest::Approx(0.875));
    CHECK(zoom_step(1.0, 1, 0.5) == doctest::Approx(1.0));
    CHECK(zoom_step(0.5, -1, 0.5) == doctest::Approx(0.5));
}

TEST_CASE("tactical zoom coordinates") {
    CHECK(zoom_view(1920, 1.0) == 1920);
    CHECK(zoom_view(1920, 0.5) == 3840);
    CHECK(zoom_view(1000, 0.75) == 1334);
    CHECK(zoom_to_view(960, 0.5) == 1920);
    CHECK(zoom_to_view(100, 1.0) == 100);
    // The world point under the anchor stays put: screen = world + cam in view px.
    const int cam = -500, ax = 400;
    const double z0 = 1.0, z1 = 0.5;
    const int cam2 = zoom_keep_point(cam, ax, z0, z1);
    const double world_before = ax / z0 - cam, world_after = ax / z1 - cam2;
    CHECK(world_before == doctest::Approx(world_after).epsilon(0.001));
    // zoom in again returns to the same camera
    CHECK(zoom_keep_point(cam2, ax, z1, z0) == cam);
}

TEST_CASE("pointer is kept on the canvas, edge band is 24 screen pixels") {
    CHECK(clamp_pointer(-5, 1280) == 0);
    CHECK(clamp_pointer(1280, 1280) == 1279);
    CHECK(clamp_pointer(640, 1280) == 640);
    CHECK(clamp_pointer(7, 0) == 7);
    CHECK(edge_scroll_band(1.0) == 24);
    CHECK(edge_scroll_band(2.0) == 12);
    CHECK(edge_scroll_band(3.0) == 8);
    CHECK(edge_scroll_band(1.5) == 16);
    CHECK(edge_scroll_band(0.5) == 24);   // window smaller than the canvas
    CHECK(edge_scroll_band(0.0) == 24);
}

TEST_CASE("first window size follows the display scale, inside the display") {
    int w = 0, h = 0;
    scaled_window_size(1280, 720, 96.0, 1920, 1080, w, h);   // 100%
    CHECK((w == 1280 && h == 720));
    scaled_window_size(1280, 720, 144.0, 3840, 2160, w, h);  // 150% on 4K
    CHECK((w == 1920 && h == 1080));
    scaled_window_size(1280, 720, 192.0, 3840, 2160, w, h);  // 200%
    CHECK((w == 2560 && h == 1440));
    scaled_window_size(1280, 720, 192.0, 2560, 1440, w, h);  // 200% on a 1440p panel: 90% of it
    CHECK((w == 2304 && h == 1296));
    scaled_window_size(1280, 720, 0.0, 1920, 1080, w, h);    // unknown DPI
    CHECK((w == 1280 && h == 720));
    scaled_window_size(1280, 720, 96.0, 1024, 600, w, h);    // never below the request
    CHECK((w == 1280 && h == 720));
}
