#pragma once
// Internal shared IMA-ADPCM nibble-decode core (docs/re/audio.md). Used by
// both the BAG chunked decoder (src/audio_bag.cpp) and the RIFF fmt-0x0011
// WAV decoder (src/wav.cpp) -- the two containers share the exact same
// per-nibble arithmetic, confirmed bit-for-bit against the game's decoder
// (FUN_0040acd0) and, independently, against its second self-contained copy
// (FUN_007c50ac) used for the RIFF WAV path. Not a public interface (no
// include/ra2assets/ entry): header-only (all `inline`) so it can be
// included by multiple translation units without an ODR violation or a
// dedicated .cpp/cmake target.
#include <cstdint>

namespace ra2assets {
namespace ima {

// 89-entry IMA step table, 7..32767 (docs/re/audio.md: 0x00815558 in
// gamemd.exe, entry[88]==32767 confirms the anchor).
inline const int kStep[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,
    17,    19,    21,    23,    25,    28,    31,    34,    37,
    41,    45,    50,    55,    60,    66,    73,    80,    88,
    97,    107,   118,   130,   143,   157,   173,   190,   209,
    230,   253,   279,   307,   337,   371,   408,   449,   494,
    544,   598,   658,   724,   796,   876,   963,   1060,  1166,
    1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,
    3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,
    7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899, 15289,
    16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};

// Classic 16-entry IMA IndexTable (docs/re/audio.md: the real game table at
// 0x00815518 is 16 entries wide -- the 8-entry pattern {-1,-1,-1,-1,2,4,6,8}
// repeated twice, at 0x00815518 and 0x00815518+0x20). decode_nibble indexes
// this with the FULL 0-15 nibble code (not code & 7); since the pattern
// repeats every 8 entries this is behaviorally identical to an 8-entry table
// indexed by code & 7, but matches the game's actual table width.
inline const int kIndexAdjust[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8,
};

// Decode one 4-bit ADPCM code (0-15), updating `sample` and `index` in
// place, and returning the new (clamped) sample. Exact cascade from
// docs/re/audio.md / FUN_0040acd0:
//   step  = kStep[index]
//   delta = step>>3, += step>>2 if bit0, += step>>1 if bit1, += step if bit2
//   bit3 set  -> sample -= delta, clamp >= -32768
//   bit3 clear -> sample += delta, clamp <= 32767
//   index += kIndexAdjust[code]; clamp to [0, 88]
inline int decode_nibble(int code, int& sample, int& index) {
    const int step = kStep[index];
    int delta = step >> 3;
    if (code & 1) delta += step >> 2;
    if (code & 2) delta += step >> 1;
    if (code & 4) delta += step;
    if (code & 8) {
        sample -= delta;
        if (sample < -32768) sample = -32768;
    } else {
        sample += delta;
        if (sample > 32767) sample = 32767;
    }
    index += kIndexAdjust[code];
    if (index < 0) index = 0;
    else if (index > 88) index = 88;
    return sample;
}

}  // namespace ima
}  // namespace ra2assets
