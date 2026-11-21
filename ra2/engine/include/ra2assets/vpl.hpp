#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <vector>
#include "ra2assets/pal.hpp"

namespace ra2assets {

// Decoded voxels.vpl: the shared voxel lighting remap the game applies at
// draw time (docs/re/voxel-render.md §4). Layout (byte-verified against the
// real 8,976-byte file): u32 remap_start, u32 remap_end, u32 section_count,
// u32 unused; 768 bytes of 6-bit palette (expanded <<2 like .pal); then
// section_count 256-byte lookup tables (table[level][color_index] ->
// final palette index).
struct Vpl {
    uint8_t remap_start = 0;
    uint8_t remap_end = 0;
    uint32_t section_count = 0;
    Palette palette;
    std::vector<std::array<uint8_t, 256>> tables;

    // Throws std::runtime_error (with context) on: len < 16, section_count
    // of 0 or > 256, or len != 16 + 768 + section_count*256 exactly.
    static Vpl load(const uint8_t* data, size_t len);
};

}  // namespace ra2assets
