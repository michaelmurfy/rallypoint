#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include "ra2assets/pal.hpp"

namespace ra2assets {

// One voxel cell. `color` indexes VxlModel::palette; `normal` indexes the
// engine's fixed normal table selected by VxlSection::normals_mode. Cells
// default to present == false (empty space).
struct VxlVoxel { uint8_t color; uint8_t normal; bool present; };

// One named section (limb) of a voxel model. `voxels` is a dense
// size_x*size_y*size_z grid laid out as
//   voxels[x + size_x * (y + (size_t)size_y * z)]
// with present == false for empty cells.
struct VxlSection {
    std::string name;
    int size_x, size_y, size_z;
    float scale;
    float transform[3][4];          // section transform (tailer)
    float min_bounds[3], max_bounds[3];
    uint8_t normals_mode;           // 2 (TS) | 4 (RA2)
    std::vector<VxlVoxel> voxels;   // size_x*size_y*size_z, present=false if empty
};

// Decoded .vxl voxel model: the 256-entry palette (6-bit source channels
// expanded game-accurately via Palette::load's <<2) plus one VxlSection per
// section header/tailer pair.
struct VxlModel {
    Rgba palette[256];
    std::vector<VxlSection> sections;
};

// Parse a .vxl byte buffer (decode only -- no rendering). Throws
// std::runtime_error (with context) on: truncation, bad magic,
// palette_count != 1, header/tailer count mismatch, an implausible section
// count (> 1,000,000), total-size mismatch (len must equal
// 802 + N*28 + body_size + N*92 exactly), span tables or span data reaching
// outside the body region, or a voxel run exceeding size_z. See
// docs/re/vxl.md for the layout evidence.
VxlModel load_vxl(const uint8_t* data, size_t len);

} // namespace ra2assets
