#include "iso_project.hpp"

#include <cmath>

namespace ra2engine {

ScreenPt cell_to_screen(int cell_x, int cell_y, int level, int tile_w, int tile_h, int level_px) {
    const int hw = tile_w / 2;
    const int hh = tile_h / 2;
    ScreenPt p;
    p.x = (cell_x - cell_y) * hw;
    p.y = (cell_x + cell_y) * hh - level * level_px;
    return p;
}

void screen_to_cell(int px, int py, int tile_w, int tile_h, int& cell_x, int& cell_y) {
    const double hw = tile_w / 2.0;
    const double hh = tile_h / 2.0;
    const double fx = (px / hw + py / hh) / 2.0;
    const double fy = (py / hh - px / hw) / 2.0;
    cell_x = static_cast<int>(std::lround(fx));
    cell_y = static_cast<int>(std::lround(fy));
}

ScreenPt cell_top_vertex(int cell_x, int cell_y, int level) {
    ScreenPt p = cell_to_screen(cell_x, cell_y, level, 60, 30, 15);
    p.x += 30;
    return p;
}

int overlay_y_offset(bool wall_tib_crate, bool railroad, int overlay_index, bool bridge_cell, int overlay_data) {
    int y = (wall_tib_crate || overlay_index == 0x7E) ? -12 : 0;
    if (railroad) y -= 1;
    if (overlay_index == 0x7E) y -= 1;
    if (bridge_cell) {
        y -= 16;
        if (overlay_data >= 9 && overlay_data <= 17) y -= 15;
    } else if (overlay_index == 0xEF) {
        y -= 15;
    }
    return y;
}

ScreenPt overlay_draw_center(int cell_x, int cell_y, int level, int yoff) {
    ScreenPt p = cell_to_screen(cell_x, cell_y, level, 60, 30, 15);
    p.x += 30;
    p.y += 15 + yoff;
    return p;
}

}  // namespace ra2engine
