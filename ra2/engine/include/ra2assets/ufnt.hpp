#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ra2assets {

// RA2's Unicode bitmap font ("fonT" magic) -- local.mix/game.fnt, the face
// game.exe loads at FUN_00432290 (GAME.FNT -> global 0x84E9E8) and prints the
// in-game HUD text with (sidebar Ready/On Hold/count, tooltips, messages).
//
// Format (CONFIRMED against the real 1,584,195-byte game.fnt, W2-UI-12):
//   0x00 char[4] "fonT"
//   0x04 i32  20        (unused here)
//   0x08 i32  bytes per glyph row     (3 -> glyphs up to 24 px wide)
//   0x0C i32  glyph rows              (16)
//   0x10 i32  line height             (17)
//   0x14 i32  symbol count            (29655)
//   0x18 i32  bytes per symbol        (49 = 1 width byte + 16 rows * 3)
//   0x1C u16[65536] codepoint -> 1-based symbol index (0 = no glyph)
//   then symbol_count * bytes_per_symbol: u8 width, rows of 1bpp MSB-first bits.
// header + table + symbols == file size exactly.
class UFont {
public:
    static UFont load(std::vector<uint8_t> bytes);
    int rows() const { return rows_; }            // glyph cell height
    int line_height() const { return line_h_; }
    int symbol_count() const { return count_; }
    bool has(uint32_t cp) const;
    int width(uint32_t cp) const;                 // advance; 0 when absent
    bool pixel(uint32_t cp, int x, int y) const;  // lit bit
    // Advance width of a UTF-8 string (newlines restart the line; returns the widest).
    int text_width(const std::string& utf8) const;
    static std::vector<uint32_t> decode_utf8(const std::string& s);

private:
    const uint8_t* sym(uint32_t cp) const;
    std::vector<uint8_t> data_;
    int row_bytes_ = 0, rows_ = 0, line_h_ = 0, count_ = 0, sym_bytes_ = 0;
};

}  // namespace ra2assets
