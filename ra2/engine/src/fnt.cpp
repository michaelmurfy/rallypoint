#include "ra2assets/fnt.hpp"
#include <stdexcept>

namespace ra2assets {
namespace {
uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
}  // namespace

// Westwood classic variable-width bitmap font (C&C/TS/RA2 ".fnt").
//
// CALIBRATE-FIRST (menufix.md §1, CONFIRMED against real cache.mix/12metfnt.fnt
// bytes + XCC fnt_decode). Header (20 = 0x14 bytes, little-endian):
//
//   0x00 u16  fileSize            == exact file length (magic/format guard)
//   0x06 u16  glyphOffsetTable    -> u16 * count, glyph bitmap offset RELATIVE to
//                                    image_ofs (NOT an absolute file offset)
//   0x08 u16  widthTable          -> u8  * count, per-glyph pixel width / advance
//   0x0A u16  image_ofs           base of glyph-bitmap region; add to table values
//   0x0C u16  heightTable         -> u16 * count: low byte = y-offset (top pad),
//                                    high byte = rows (lit height)
//   0x0E u8   firstChar           (0 on 12metfnt)
//   0x12 u8   height              max cell height (16)
//   0x13 u8   maxWidth            (16)
//
// count is not a stored field; it is the table stride between the offset table
// (u16) and the width table (u8): count = (widthTable - glyphOffsetTable)/2,
// cross-validated against (heightTable - widthTable). On 12metfnt all three
// spacings agree at 219 (first=0..last=218).
//
// Glyphs are 8bpp: one byte per pixel, `w` bytes = `w` pixels per row, `rows`
// rows placed at vertical y-offset `yoff` inside the `height`-tall cell. The
// pixel value is a small shading/bevel ramp index in the LOW nibble (12metfnt
// uses 0..4; the high nibble is unused/zero) -- 0 = transparent, 1..N = "lit".
// Total glyph bytes = w * rows.
//
// THE FIX (menufix.md §1 was RIGHT that the offsets are image_ofs-relative, but
// WRONG that the data is 4bpp/2-px-per-byte): the real bug was the MISSING
// image_ofs base -- the old code read glyph bytes at the ABSOLUTE table offset
// (d + gdo), pulling garbage. The bytes prove 8bpp: every high nibble is 0
// across all glyphs, and an 8-px-wide 'H' (`02 01 02 02 02 02 02 01` crossbar)
// decodes cleanly, whereas a 2-px-per-byte read inserts a blank column between
// every pixel (a comb/striped look). Adding image_ofs + keeping 8bpp width=w
// yields legible authentic glyphs (verified: "SKIRMISH CAMPAIGN QUIT" render).
Font Font::load(std::vector<uint8_t> b) {
    if (b.size() < 0x14) throw std::runtime_error("fnt: shorter than header");
    const uint8_t* d = b.data();

    // Format guard: the classic face stores its own byte length at 0x00. This
    // rejects the "FoNt"/"fonT"-magic faces (metal12.fnt/game.fnt), whose first
    // u16 is the ASCII "Fo"/"fo" -- never the file size.
    const uint16_t file_size = rd16(d + 0x00);
    if (file_size != b.size())
        throw std::runtime_error("fnt: not a classic Westwood .fnt (size field mismatch)");

    const uint16_t off_glyphs = rd16(d + 0x06);   // u16 per glyph: bitmap offset
    const uint16_t off_width  = rd16(d + 0x08);   // u8  per glyph: byte-width/row
    const uint16_t image_ofs  = rd16(d + 0x0A);   // base of glyph bitmap data (ADD to table)
    const uint16_t off_height = rd16(d + 0x0C);   // u16 per glyph: {yoff, rows}

    if (off_glyphs >= off_width || off_width >= off_height)
        throw std::runtime_error("fnt: header offset tables out of order");
    if ((off_width - off_glyphs) % 2 != 0)
        throw std::runtime_error("fnt: glyph offset table not u16-aligned");

    const int count = (off_width - off_glyphs) / 2;         // u16 offset table
    if (count <= 0) throw std::runtime_error("fnt: empty font");
    if (off_height - off_width != count)                    // u8 width table
        throw std::runtime_error("fnt: width/height table size mismatch");

    Font f;
    f.first_     = d[0x0E];
    f.height_    = d[0x12];
    f.max_width_ = d[0x13];
    f.last_      = f.first_ + count - 1;
    if (f.height_ <= 0) throw std::runtime_error("fnt: bad height");

    // Bounds: every referenced table must lie inside the file.
    const size_t need = static_cast<size_t>(off_height) + static_cast<size_t>(count) * 2;
    if (need > b.size()) throw std::runtime_error("fnt: tables exceed file");
    // Glyph bitmap data starts at image_ofs; it must lie inside the file and at
    // or past the height table end (the tables end exactly at image_ofs on 12metfnt).
    if (image_ofs > b.size()) throw std::runtime_error("fnt: image_ofs past end of file");

    f.glyphs_.resize(count);
    f.widths_.resize(count);
    for (int i = 0; i < count; ++i) {
        const int w = d[off_width + i];                     // pixels per row (1 byte each)
        const uint8_t* hh = d + off_height + i * 2;
        const int yoff = hh[0];
        int rows = hh[1];
        const uint16_t gdo = rd16(d + off_glyphs + i * 2);  // this glyph's bitmap (rel. image_ofs)

        f.widths_[i] = w;                                   // advance = pixel width
        FntGlyph& g = f.glyphs_[i];
        g.w = w;
        g.h = f.height_;                                    // uniform cell height
        g.pixels.assign(static_cast<size_t>(g.w) * g.h, 0);

        // Clamp the lit block into the cell and to the file, defensively.
        if (w <= 0 || rows <= 0) continue;
        if (yoff >= g.h) continue;
        if (yoff + rows > g.h) rows = g.h - yoff;
        const size_t glyph_bytes = static_cast<size_t>(w) * rows;
        if (static_cast<size_t>(image_ofs) + gdo + glyph_bytes > b.size())
            continue;                                       // corrupt entry
        // Bitmap base is RELATIVE to image_ofs (the missing header field that made
        // the old absolute-offset read pull garbage). 8bpp: one byte per pixel,
        // value = ramp index in the low nibble (0 = transparent, 1..N = bevel).
        const uint8_t* src = d + image_ofs + gdo;
        for (int ry = 0; ry < rows; ++ry) {
            const uint8_t* srow = src + static_cast<size_t>(ry) * w;
            uint8_t* drow = &g.pixels[static_cast<size_t>(yoff + ry) * g.w];
            for (int rx = 0; rx < w; ++rx) drow[rx] = srow[rx] & 0x0F;
        }
    }
    return f;
}

int Font::width(int ch) const {
    if (ch < first_ || ch > last_) return 0;
    return widths_[ch - first_];
}

const FntGlyph& Font::glyph(int ch) const {
    if (ch < first_ || ch > last_) throw std::runtime_error("fnt: glyph out of range");
    return glyphs_[ch - first_];
}

}  // namespace ra2assets
