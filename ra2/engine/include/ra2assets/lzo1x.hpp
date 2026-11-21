#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

namespace ra2assets {

// Decompress one LZO1X block (the LZO1X-1 / miniLZO-compatible bitstream that
// RA2/YR [IsoMapPack5] chunk bodies use). `out_len` is BOTH the destination
// capacity AND the declared uncompressed size: on success exactly `out_len`
// bytes are produced and returned. Every source read (`src_len`), destination
// write (`out_len`) and back-reference (distance <= bytes written so far) is
// bounds-checked; any violation, a malformed token, or a produced-size
// mismatch throws std::runtime_error with context.
//
// Clean-room: implemented fresh from the PUBLIC LZO1X bitstream-format
// description (the token/copy/literal grammar, which is documented and not
// protected). No miniLZO / liblzo2 / kernel-lzo / OpenRA-MiniLzo source
// was opened, read, or transcribed.
size_t lzo1x_decompress(const uint8_t* src, size_t src_len, uint8_t* out, size_t out_len);

} // namespace ra2assets
