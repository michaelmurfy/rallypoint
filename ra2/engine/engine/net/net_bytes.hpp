// engine/net/net_bytes.hpp -- little/big-endian byte helpers for the wire codecs.
//
// Everything the original game puts on the wire is little-endian (x86 struct
// copies), except the CnCNet tunnel V2 header, which is big-endian (htons).
// These helpers never type-pun, so the codecs are portable to arm64 / x86.
#pragma once

#include <cstddef>
#include <cstdint>

namespace ra2::net {

inline void put_u8(uint8_t* p, uint8_t v) { p[0] = v; }
inline void put_u16le(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}
inline void put_u32le(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}
inline void put_u16be(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v);
}
inline uint16_t get_u16le(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
inline uint32_t get_u32le(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
inline uint16_t get_u16be(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}
inline int32_t get_i32le(const uint8_t* p) { return static_cast<int32_t>(get_u32le(p)); }
inline void put_i32le(uint8_t* p, int32_t v) { put_u32le(p, static_cast<uint32_t>(v)); }
inline int16_t get_i16le(const uint8_t* p) { return static_cast<int16_t>(get_u16le(p)); }
inline void put_i16le(uint8_t* p, int16_t v) { put_u16le(p, static_cast<uint16_t>(v)); }

}  // namespace ra2::net
