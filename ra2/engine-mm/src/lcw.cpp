#include "ra2assets/lcw.hpp"
#include <stdexcept>

// Original clean-room implementation of Westwood's LCW ("Format80")
// back-reference/RLE compression scheme. This is a public, well-documented
// algorithm (used throughout the C&C/RA2 file formats for map packs and
// some assets) -- reimplemented fresh from the well-known command layout,
// not derived from or copied out of any third-party decoder source.
namespace ra2assets {

size_t lcw_decompress(const uint8_t* s, size_t sl, uint8_t* d, size_t dl) {
    const uint8_t* se = s + sl;
    uint8_t* d0 = d;
    uint8_t* de = d + dl;

    // Bounds-check helpers: `need` guards upcoming source reads, `put`
    // guards each destination write. Both throw with context on violation
    // per the Phase-1 hardening standard (never read past src_len, never
    // write past out_len).
    auto need = [&](const uint8_t* p, size_t n) {
        if (p + n > se) throw std::runtime_error("lcw: src overrun");
    };
    auto put = [&](uint8_t b) {
        if (d >= de) throw std::runtime_error("lcw: dst overrun");
        *d++ = b;
    };

    while (s < se) {
        uint8_t cmd = *s++;
        if (!(cmd & 0x80)) {
            // 0x00-0x7F: short relative back-copy.
            // count = bits 6:4 of cmd (0-7) + 3; a 12-bit offset is formed
            // from cmd's low nibble (high 4 bits of the offset) and the
            // next source byte (low 8 bits).
            need(s, 1);
            size_t count = ((cmd & 0x70) >> 4) + 3;
            size_t off = ((size_t)(cmd & 0x0F) << 8) | *s++;
            if (off == 0 || (size_t)(d - d0) < off)
                throw std::runtime_error("lcw: bad rel off");
            const uint8_t* r = d - off;
            // Byte-by-byte (not memcpy/memmove): when off < count the
            // source and destination ranges overlap and later reads must
            // observe bytes written earlier in this same loop, exactly
            // like the classic LZ77 "copy run" behavior.
            for (size_t i = 0; i < count; i++) put(*r++);
        } else if (!(cmd & 0x40)) {
            // 0x80-0xBF: literal copy of (cmd & 0x3F) bytes; 0x80 itself
            // (count==0) is the end-of-stream marker.
            size_t count = cmd & 0x3F;
            if (count == 0) break;
            need(s, count);
            for (size_t i = 0; i < count; i++) put(*s++);
        } else if (cmd != 0xFE && cmd != 0xFF) {
            // 0xC0-0xFD: medium absolute copy. count = (cmd & 0x3F) + 3,
            // pos is an absolute little-endian u16 offset into the output
            // written so far.
            size_t count = (cmd & 0x3F) + 3;
            need(s, 2);
            size_t pos = s[0] | (s[1] << 8);
            s += 2;
            for (size_t i = 0; i < count; i++) {
                if (pos >= (size_t)(d - d0)) throw std::runtime_error("lcw: bad abs pos");
                put(d0[pos++]);
            }
        } else if (cmd == 0xFE) {
            // 0xFE: run fill. count is a little-endian u16, followed by a
            // single fill byte value.
            need(s, 3);
            size_t count = s[0] | (s[1] << 8);
            uint8_t v = s[2];
            s += 3;
            for (size_t i = 0; i < count; i++) put(v);
        } else {
            // 0xFF: large absolute copy. Same as the medium form but with
            // a 16-bit little-endian count instead of the packed 6-bit one.
            need(s, 4);
            size_t count = s[0] | (s[1] << 8);
            size_t pos = s[2] | (s[3] << 8);
            s += 4;
            for (size_t i = 0; i < count; i++) {
                if (pos >= (size_t)(d - d0)) throw std::runtime_error("lcw: bad abs pos");
                put(d0[pos++]);
            }
        }
    }
    return (size_t)(d - d0);
}

}  // namespace ra2assets
