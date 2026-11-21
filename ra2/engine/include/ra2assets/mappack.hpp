#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

namespace ra2assets {

// Decode a base64 string (standard alphabet: A-Z a-z 0-9 + /, '=' padding),
// skipping any embedded whitespace. RA2/YR map INIs wrap long base64 blobs
// (e.g. [IsoMapPack5]) across many numbered keys whose values get
// concatenated by the caller before being handed to this function.
std::vector<uint8_t> base64_decode(const std::string& s);

// Body codec used by the individual chunks of a "Format5" pack. The chunk
// WRAPPER ([u16 compressedSize][u16 uncompressedSize] headers) is identical
// for both; only the per-chunk body compression differs. RA2/YR use:
//   - LZO  for [IsoMapPack5] (terrain) chunk bodies (XCC: decode5(...,5))
//   - LCW  ("Format80") for [OverlayPack]/[OverlayDataPack] (XCC: decode5(...,80))
enum class MapPackCodec { LCW, LZO };

// Decompress a concatenated "Format5" pack: a back-to-back sequence of chunks,
// each laid out as [u16 le compressedSize][u16 le uncompressedSize][body of
// compressedSize bytes], continuing until `len` bytes of `data` are consumed.
// Each chunk's body is decompressed by the selected `codec`
// (ra2assets::lcw_decompress for LCW, ra2assets::lzo1x_decompress for LZO) and
// must yield EXACTLY the declared uncompressedSize; every chunk header and body
// is bounds-checked against `len`. Throws std::runtime_error (with context) on
// any malformed input.
std::vector<uint8_t> decompress_mappack(const uint8_t* data, size_t len, MapPackCodec codec);

// Thin wrapper: LCW-bodied Format5 pack (the original Task-2 entry point,
// correct for [OverlayPack]/[OverlayDataPack]). Equivalent to
// decompress_mappack(data, len, MapPackCodec::LCW).
std::vector<uint8_t> decompress_format5(const uint8_t* data, size_t len);

} // namespace ra2assets
