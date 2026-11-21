#include "ra2assets/lzo1x.hpp"
#include <stdexcept>
#include <string>

// Original clean-room implementation of the LZO1X decompressor (Markus
// Oberhumer's LZO1X-1 / miniLZO-compatible bitstream). RA2/YR store map
// terrain ([IsoMapPack5]) as "Format5"-wrapped chunks whose bodies are
// LZO1X-compressed. LZO's reference implementations (miniLZO, liblzo2,
// kernel lzo, OpenRA's MiniLzo) are GPL; NONE of them were opened, read, or
// transcribed. This decoder is written solely from the public, documented
// LZO1X bitstream grammar (token classes, literal/back-copy encodings,
// little-endian distances, the 2-bit trailing-literal "state", the
// zero-byte length extension, and the 0x11 0x00 0x00 end marker) -- a format
// description that is not protected. Correctness is pinned by decoding
// real [IsoMapPack5] chunks to their exact declared sizes (see
// tests/test_lzo1x.cpp), the same real-data arbiter used elsewhere in this
// library.
//
// Token grammar (command byte t, `state` = #literals copied by the previous
// op, carried between ops so a following op can pick its distance encoding):
//   first byte t>17 : copy (t-17) literals, state=min(t-17,4); else state=0
//   t 0x00-0x0F :
//        state==0 : LITERAL RUN, len = 3 + (t?t : 15+ext); state:=4
//        state 1-3: 2-byte copy, dist = (H<<2)+(t>>2)+1     (<=1KB)
//        state>=4 : 3-byte copy, dist = (H<<2)+(t>>2)+2049  (2..3KB)
//   t 0x10-0x1F : copy, len = 2 + ((t&7)?(t&7):7+ext); read LE16 D;
//                 dist = 16384 + (((t>>3)&1)<<14) + (D>>2); state = D&3;
//                 dist==16384  =>  END OF STREAM
//   t 0x20-0x3F : copy, len = 2 + ((t&31)?(t&31):31+ext); read LE16 D;
//                 dist = (D>>2)+1 (<=16KB); state = D&3
//   t 0x40-0x7F : copy, len = 3 + ((t>>5)&1); read H;
//                 dist = (H<<3)+((t>>2)&7)+1 (<=2KB); state = t&3
//   t 0x80-0xFF : copy, len = 5 + ((t>>5)&3); read H;
//                 dist = (H<<3)+((t>>2)&7)+1 (<=2KB); state = t&3
//   after every copy: emit `state` trailing literals; length extension =
//   sum of 0x00 bytes * 255 plus the next non-zero byte.
namespace ra2assets {

size_t lzo1x_decompress(const uint8_t* src, size_t src_len,
                        uint8_t* out, size_t out_len) {
    const uint8_t* ip = src;
    const uint8_t* const ip_end = src + src_len;
    uint8_t* op = out;
    uint8_t* const op_end = out + out_len;

    // Bounds helpers, matching the Phase-1 hardening standard: never read past
    // src_len, never write past out_len, never back-reference before out[0].
    auto rd = [&]() -> uint8_t {
        if (ip >= ip_end) throw std::runtime_error("lzo1x: source exhausted mid-token");
        return *ip++;
    };
    auto need = [&](size_t n) {
        if ((size_t)(ip_end - ip) < n)
            throw std::runtime_error("lzo1x: source overrun (need " + std::to_string(n) + ")");
    };
    auto put = [&](uint8_t b) {
        if (op >= op_end) throw std::runtime_error("lzo1x: destination overrun");
        *op++ = b;
    };
    auto copy_literals = [&](size_t n) {
        need(n);
        for (size_t i = 0; i < n; ++i) put(*ip++);
    };
    auto copy_match = [&](size_t dist, size_t len) {
        // distance must reference bytes already written (never at/ before out[0])
        if (dist == 0 || (size_t)(op - out) < dist)
            throw std::runtime_error("lzo1x: back-reference distance " + std::to_string(dist) +
                                     " exceeds " + std::to_string((size_t)(op - out)) + " bytes written");
        const uint8_t* m = op - dist;
        // byte-by-byte: overlapping copies (dist < len) must observe bytes
        // written earlier in this same run (classic LZ77 semantics).
        for (size_t i = 0; i < len; ++i) put(*m++);
    };
    // Length extension: the token's length field was all-zero (an escape);
    // add 255 per following 0x00 byte, then the next non-zero byte.
    auto read_ext = [&](size_t base) -> size_t {
        size_t v = base;
        uint8_t b;
        while ((b = rd()) == 0) {
            if (v > (size_t)-1 - 255) throw std::runtime_error("lzo1x: length extension overflow");
            v += 255;
        }
        v += b;
        return v;
    };

    if (src_len == 0) {
        if (out_len != 0) throw std::runtime_error("lzo1x: empty input but nonzero output expected");
        return 0;
    }

    size_t state; // #literals copied by the previous op (selects next op's form)

    // --- first byte special case ---
    // A leading byte > 17 is a bare literal run of (t-17) bytes; otherwise the
    // first byte is a normal instruction decoded with state==0.
    if (*ip > 17) {
        size_t lit = (size_t)(*ip) - 17;
        ++ip;
        copy_literals(lit);
        state = (lit < 4) ? lit : 4;
    } else {
        state = 0;
    }

    for (;;) {
        uint8_t t = rd();

        if (t < 16) {
            if (state == 0) {
                // Long literal run (only valid right after a match that left
                // no trailing literals, or at stream start).
                size_t len = 3 + (t ? (size_t)t : read_ext(15));
                copy_literals(len);
                state = 4;
                continue; // literal runs carry no trailing literals
            } else if (state < 4) {
                // 2-byte back-copy, short (<=1KB) distance.
                uint8_t h = rd();
                copy_match(((size_t)h << 2) + (t >> 2) + 1, 2);
                state = t & 3;
            } else {
                // 3-byte back-copy, 2..3KB distance (only after a literal run).
                uint8_t h = rd();
                copy_match(((size_t)h << 2) + (t >> 2) + 2049, 3);
                state = t & 3;
            }
        } else if (t < 32) {
            // 0x10-0x1F: distance 16..48KB. dist==16384 is the end marker.
            size_t len = 2 + ((t & 7) ? (size_t)(t & 7) : read_ext(7));
            need(2);
            size_t d = (size_t)ip[0] | ((size_t)ip[1] << 8);
            ip += 2;
            size_t dist = 16384 + (((size_t)(t >> 3) & 1) << 14) + (d >> 2);
            if (dist == 16384) break; // 0x11 0x00 0x00 -> end of stream
            copy_match(dist, len);
            state = d & 3;
        } else if (t < 64) {
            // 0x20-0x3F: distance <=16KB.
            size_t len = 2 + ((t & 31) ? (size_t)(t & 31) : read_ext(31));
            need(2);
            size_t d = (size_t)ip[0] | ((size_t)ip[1] << 8);
            ip += 2;
            copy_match((d >> 2) + 1, len);
            state = d & 3;
        } else if (t < 128) {
            // 0x40-0x7F: length 3-4, distance <=2KB.
            uint8_t h = rd();
            copy_match(((size_t)h << 3) + ((t >> 2) & 7) + 1, 3 + ((t >> 5) & 1));
            state = t & 3;
        } else {
            // 0x80-0xFF: length 5-8, distance <=2KB.
            uint8_t h = rd();
            copy_match(((size_t)h << 3) + ((t >> 2) & 7) + 1, 5 + ((t >> 5) & 3));
            state = t & 3;
        }

        // Trailing literals carried by the match token (0..3), copied verbatim
        // from the input immediately after the token.
        if (state) copy_literals(state);
    }

    size_t produced = (size_t)(op - out);
    if (produced != out_len)
        throw std::runtime_error("lzo1x: produced " + std::to_string(produced) +
                                 " bytes, declared/expected " + std::to_string(out_len));
    return produced;
}

} // namespace ra2assets
