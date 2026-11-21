#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

namespace ra2assets {

// One decoded glyph: w*h palette indices (row-major, index 0 = transparent).
struct FntGlyph {
    int w = 0, h = 0;
    std::vector<uint8_t> pixels;    // size w*h; 0 = transparent
};

// A Westwood .fnt variable-width bitmap font (RA2 shell/menu font, e.g.
// 12metfnt.fnt -- the bevelled metallic "metal12" menu face). Glyphs are
// indexed by byte value in [first(), last()]. Glyphs are 8bpp (1 byte/pixel,
// value = bevel-ramp index in the low nibble, 0 = transparent); width() /
// FntGlyph::w are the pixel width, which is also the text advance. Bitmap
// offsets are RELATIVE to the image_ofs header field. [RE: menufix.md §1, but
// the data is 8bpp not 4bpp -- see fnt.cpp for the byte-level proof.]
//
// Format (CONFIRMED against real cache.mix/12metfnt.fnt bytes; classic Westwood/
// TS/RA2 variable-width bitmap font, NOT the newer "FoNt"-magic face used by
// local.mix/metal12.fnt & game.fnt -- see fnt.cpp):
//   0x00 u16 fileSize   0x06 u16 glyphOffsetTable (rel. image_ofs)  0x08 u16 widthTable
//   0x0A u16 image_ofs  0x0C u16 heightTable  0x0E u8 firstChar  0x12 u8 height  0x13 u8 maxWidth
class Font {
public:
    static Font load(std::vector<uint8_t> bytes);
    int first() const { return first_; }
    int last()  const { return last_; }
    int count() const { return last_ - first_ + 1; }
    int height() const { return height_; }        // max glyph height (cell height)
    int max_width() const { return max_width_; }
    int width(int ch) const;                       // advance width; 0 if out of range
    const FntGlyph& glyph(int ch) const;           // throws if out of range

private:
    int first_ = 0, last_ = 0, height_ = 0, max_width_ = 0;
    std::vector<FntGlyph> glyphs_;                 // index ch-first_
    std::vector<int> widths_;                      // index ch-first_
};

inline Font load_fnt(const std::vector<uint8_t>& b) { return Font::load(b); }

}  // namespace ra2assets
