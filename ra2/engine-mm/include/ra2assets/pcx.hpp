#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include "ra2assets/pal.hpp"

namespace ra2assets {

// A decoded 8-bit palettized PCX image. `indices` is w*h palette indices
// (row-major, top-down); `palette` is the FULL 8-bit trailer palette (a=255).
// RA2's logo.pcx/title.pcx/loadscreens are all 8-bit RLE PCX with the palette
// in the 769-byte trailer (0x0C marker + 256*3 RGB). [RE: mainmenu.md §2]
struct PcxImage {
    int w = 0, h = 0;
    std::vector<uint8_t> indices;   // size w*h
    Palette palette;                // 256 entries, a=255
    std::vector<uint8_t> rgba() const;  // w*h*4, R,G,B,A order, fully opaque
};

// Decode a standard 8-bit RLE PCX (1 plane, 8 bpp) with a trailer palette.
// Throws std::runtime_error on: bad manufacturer/version, non-RLE encoding,
// not-8bpp/not-1-plane, a missing/short trailer palette, or a truncated body.
PcxImage decode_pcx(const uint8_t* data, std::size_t len);
inline PcxImage decode_pcx(const std::vector<uint8_t>& b) {
    return decode_pcx(b.data(), b.size());
}

}  // namespace ra2assets
