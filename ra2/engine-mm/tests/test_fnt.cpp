#include "doctest.h"
#include "ra2assets/fnt.hpp"
#include "ra2assets/mix.hpp"
#include "asset_fixture.hpp"
#include <cstdlib>

using namespace ra2assets;

// Structural self-consistency of the real Westwood variable-width bitmap font.
//
// CALIBRATE-FIRST note (T3 Step 1): the brief nominated `local.mix/metal12.fnt`,
// but a hex probe showed that file (and `local.mix/game.fnt`) use the newer
// "FoNt"/"fonT"-magic face, NOT the classic Westwood layout the decoder targets.
// The classic-format shell/menu font in this install is `cache.mix/12metfnt.fnt`
// (RE mainmenu.md §3 treats "12metfnt.fnt / metal12.fnt" as the same bevelled
// menu face). Its header (u16 fileSize == file length) was confirmed against the
// real bytes, so this case exercises that font. Observed on this install:
// first=0, last=218, count=219, height=16, max_width=16 (recorded, not asserted).
TEST_CASE("fnt: real 12metfnt.fnt decodes to a self-consistent variable-width font") {
    if (!ra2fixture::ra2_available()) { MESSAGE("ra2.mix absent; skipped"); return; }
    const MixFile& top = ra2fixture::mix_by_name("ra2.mix");
    MixFile cache = top.open_nested("cache.mix");
    const MixEntry* e = cache.find("12metfnt.fnt");
    REQUIRE(e != nullptr);
    Font f = load_fnt(cache.read(*e));

    CHECK(f.count() == f.last() - f.first() + 1);   // header self-consistency
    CHECK(f.count() > 0);
    CHECK(f.height() > 0);
    CHECK(f.height() <= 64);
    CHECK(f.max_width() > 0);
    // Every glyph's advance width is within the font max, and its bitmap is
    // exactly width*height indices (0 = transparent).
    for (int c = f.first(); c <= f.last(); ++c) {
        CAPTURE(c);
        CHECK(f.width(c) <= f.max_width());
        const FntGlyph& g = f.glyph(c);
        CHECK(g.pixels.size() == static_cast<size_t>(g.w) * g.h);
    }
    // A visible glyph ('A') has a positive width and at least one lit pixel.
    if ('A' >= f.first() && 'A' <= f.last()) {
        const FntGlyph& a = f.glyph('A');
        CHECK(a.w > 0);
        bool any = false;
        for (uint8_t p : a.pixels) if (p != 0) { any = true; break; }
        CHECK(any);
    }
}

// DECODE-CORRECTNESS regression guard [RE: menufix.md §1 + byte-level RE of the
// real 12metfnt.fnt]. The original decoder read glyph bytes at the ABSOLUTE
// offset-table value (d + gdo) instead of RELATIVE to the image_ofs header field
// (d + image_ofs + gdo), so it pulled garbage -> the menu fell back to the rect-
// font. The data is 8bpp: one byte per pixel, `w` bytes = `w` pixels per row, the
// value being a bevel-ramp index in the low nibble (0 = transparent). (menufix.md
// §1.4 proposed a 4bpp / 2-px-per-byte unpack; the real bytes disprove it -- every
// high nibble is 0, and an 8bpp read yields clean legible glyphs while a 2-px-per-
// byte read inserts a blank column between every pixel.) These EXACT-BITMAP asserts
// (independently decoded from the raw bytes, NOT by this decoder) fail if the
// image_ofs base is dropped (garbage) or the depth/width is wrong. '.' = 0; a
// digit is that ramp value (12metfnt uses 0..4).
namespace {
// Assert glyph `ch` decodes to `rows` (each string one pixel row: '.'=0 else the
// exact ramp digit). Width = number of chars per row = pixel width = advance.
void check_glyph(const Font& f, int ch, const std::vector<std::string>& rows) {
    CAPTURE(ch);
    REQUIRE(ch >= f.first());
    REQUIRE(ch <= f.last());
    const FntGlyph& g = f.glyph(ch);
    REQUIRE(rows.size() == static_cast<size_t>(g.h));
    REQUIRE(g.w == static_cast<int>(rows[0].size()));   // pixel width
    CHECK(f.width(ch) == g.w);                           // advance == pixel width
    for (int y = 0; y < g.h; ++y) {
        for (int x = 0; x < g.w; ++x) {
            const char c = rows[y][x];
            const uint8_t expect = (c == '.') ? 0 : static_cast<uint8_t>(c - '0');
            CAPTURE(y); CAPTURE(x);
            CHECK(static_cast<int>(g.pixels[static_cast<size_t>(y) * g.w + x]) ==
                  static_cast<int>(expect));
        }
    }
}
}  // namespace

TEST_CASE("fnt: 12metfnt 'I' and 'H' decode to the exact (legible) 8bpp bitmap") {
    if (!ra2fixture::ra2_available()) { MESSAGE("ra2.mix absent; skipped"); return; }
    const MixFile& top = ra2fixture::mix_by_name("ra2.mix");
    MixFile cache = top.open_nested("cache.mix");
    const MixEntry* e = cache.find("12metfnt.fnt");
    REQUIRE(e != nullptr);
    Font f = load_fnt(cache.read(*e));

    // 'I' (2 px wide), a clean bevelled vertical bar: highlight col 0 (ramp 2),
    // shade col 1 (ramp 1), lit rows 2..11 within the 16-tall cell.
    check_glyph(f, 'I', {
        "..",
        "..",
        "22",
        "21",
        "21",
        "21",
        "21",
        "21",
        "21",
        "21",
        "21",
        "21",
        "..",
        "..",
        "..",
        "..",
    });

    // 'H' (8 px wide): two straight vertical bars (cols 0-1 and 6-7) joined by a
    // horizontal crossbar (rows 6-7). A dropped image_ofs base or a 2-px-per-byte
    // read would destroy this shape.
    check_glyph(f, 'H', {
        "........",
        "........",
        "22....22",
        "21....21",
        "21....21",
        "21....21",
        "21222221",
        "21111111",
        "21....21",
        "21....21",
        "21....21",
        "21....21",
        "........",
        "........",
        "........",
        "........",
    });
}
