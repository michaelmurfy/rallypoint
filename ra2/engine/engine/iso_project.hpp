#pragma once

namespace ra2engine {

// Screen-space pixel coordinate (window/render-target pixels, camera-local:
// callers add a camera pan/scroll offset on top of this).
struct ScreenPt { int x, y; };

// RA2/TS diamond isometric projection.
//
// PURE math -- no SDL/OS deps. Compiled into both `ra2assets_tests` (so it
// is unit-testable without SDL/a window) and `ra2engine` (the real
// renderer, Task 9, and hit-testing, Task 12, both call these functions
// directly). See CMakeLists.txt: engine/iso_project.cpp is added as a
// source to both targets rather than living inside the ra2assets library,
// since it is engine-layer (screen space), not asset-parsing.
//
// Orientation / convention (confirmed against OpenRA's TS `Map` diamond
// projection, D:\ra2-re\reference\OpenRA -- OpenRA's MPos/CPos -> screen
// shear also moves +X right+down and +Y left+down along the two diamond
// edges, i.e. `(x - y)` scales the horizontal half-width and `(x + y)`
// scales the vertical half-height): given a cell (cell_x, cell_y) in the
// map's raw IsoMapPack5 grid space (see MapFile / map.hpp) and a height
// `level` (RA2 terrain height step, e.g. cliffs/ramps):
//
//   px = (cell_x - cell_y) * (tile_w / 2)
//   py = (cell_x + cell_y) * (tile_h / 2) - level * level_px
//
// Increasing `level` raises the tile on screen, so its contribution is
// SUBTRACTED from py (screen +y is down). Cell (0,0) is the screen origin
// (0,0); this is the "top-left of the tile's bounding rect" convention
// used consistently by both functions below (i.e. it is defined purely by
// the half-extent shear above -- there is no additional offset applied).
// The renderer (Task 9) adds its own camera/scroll translation on top of
// this pixel.
//
// `screen_to_cell` is the exact algebraic inverse of the level-0 case
// (level/height is ignored on the way back, since a screen pixel alone is
// height-ambiguous): solving the 2x2 linear system
//   px = (x - y) * hw,  py = (x + y) * hh   (hw = tile_w/2, hh = tile_h/2)
// for x, y gives
//   x = (px/hw + py/hh) / 2
//   y = (py/hh - px/hw) / 2
// rounded to the nearest integer cell. Because cell_to_screen and
// screen_to_cell are each other's exact inverse by construction, round-trip
// (cell -> screen -> cell) is exact for every integer cell whenever
// tile_w/tile_h are even (both stock RA2 tile sizes, 60x30 and 48x24, are).
ScreenPt cell_to_screen(int cell_x, int cell_y, int level, int tile_w, int tile_h, int level_px);

// Inverse of cell_to_screen (ignoring level/height, see above): given a
// screen pixel using the same top-left-of-bounding-rect convention,
// returns the nearest (cell_x, cell_y).
void screen_to_cell(int px, int py, int tile_w, int tile_h, int& cell_x, int& cell_y);

// ---- W4-FIX-POS: the original's draw anchors (60x30 tiles, 15 px/level) -----
// Every SHP below is blitted CENTRED (CC_Draw_Shape flag 0x200): the canvas
// centre lands on the returned point. Frame offsets stay canvas-relative.
//
// Building draw point [RE: docs/re/building-render.md §2; gamemd.exe
// BuildingClass vtbl+0xAC FUN_00459ee0 = Location - (128,128,0), RA2 game.exe
// FUN_00453c70]: the TOP (north) vertex of the foundation's ORIGIN cell (cx,cy)
// at the building's level -- independent of the foundation size. Bib, slot
// anims, doors, damage fires, rubble, DestroyAnim and voxel turrets
// (TurretAnimX/Y) all hang off this same point. The depth-sort key is NOT this
// point (it stays the foundation centre, building-render.md §3).
// The same point is the smudge anchor [RE: FUN_00480260 -> SmudgeType::DrawIt
// FUN_006b4c70]: a smudge centres on its cell's top vertex.
ScreenPt cell_top_vertex(int cell_x, int cell_y, int level);
inline ScreenPt building_draw_point(int cell_x, int cell_y, int level) { return cell_top_vertex(cell_x, cell_y, level); }

// Overlay per-type Y offset [RE: FUN_005fd4b0 + FUN_00480020]:
//   -12 if the type is Wall=, Tiberium= or Crate= (or runtime overlay #126)
//   -1  if its Land= is Railroad;  -1 more for overlay #126
//   -16 if the cell is a high-bridge cell (CellFlags 0x80), and -15 more when
//       OverlayData is 9..17 (or, without the flag, for overlay #239).
int overlay_y_offset(bool wall_tib_crate, bool railroad, int overlay_index, bool bridge_cell, int overlay_data);
// Overlay / ore / gem / crate canvas centre [RE: FUN_006d6280 -> CellClass::
// DrawOverlay FUN_0047f5b0]: the cell centre + (0, yoff).
ScreenPt overlay_draw_center(int cell_x, int cell_y, int level, int yoff);

}  // namespace ra2engine
