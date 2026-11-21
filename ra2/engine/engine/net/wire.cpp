// engine/net/wire.cpp -- datagram checksum + tunnel headers. See wire.hpp.
#include "net/wire.hpp"

#include "net/net_bytes.hpp"

#include <cstring>

namespace ra2::net {

uint32_t packet_crc(const uint8_t* payload, size_t len) {
    uint32_t crc = 0;
    const size_t words = len / 4;
    for (size_t i = 0; i < words; ++i) crc = add_crc(crc, get_u32le(payload + 4 * i));
    const size_t tail = len & 3;
    if (tail != 0) {
        // 0x7B0E1C: MOV EAX,[ESI]; EDX = 0xFFFFFFFF >> (32 - 8*tail); AND.
        // The original reads 4 bytes past the end of the data (inside its
        // 0x2A0-byte buffer); only the low `tail` bytes survive the mask, so
        // reading just those bytes is equivalent.
        uint32_t w = 0;
        for (size_t k = 0; k < tail; ++k) w |= static_cast<uint32_t>(payload[4 * words + k]) << (8 * k);
        crc = add_crc(crc, w);
    }
    return crc;
}

bool build_datagram(const uint8_t* payload, size_t len, std::vector<uint8_t>& out) {
    if (len > kMaxDatagramPayload) return false;
    out.resize(4 + len);
    put_u32le(out.data(), packet_crc(payload, len));
    if (len) std::memcpy(out.data() + 4, payload, len);
    return true;
}

bool check_datagram(const uint8_t* data, size_t len, const uint8_t** payload, size_t* plen) {
    if (len < 4) return false;
    const size_t n = len - 4;
    if (n >= 0x280) return false;  // 0x7B0E58: CMP EAX,0x280; JGE reject
    if (packet_crc(data + 4, n) != get_u32le(data)) return false;
    *payload = data + 4;
    *plen = n;
    return true;
}

void tunnel_wrap(TunnelMode mode, uint32_t from_id, uint32_t to_id, const uint8_t* payload,
                 size_t len, std::vector<uint8_t>& out) {
    const size_t h = tunnel_header_size(mode);
    out.resize(h + len);
    if (mode == TunnelMode::V2) {
        put_u16be(out.data(), static_cast<uint16_t>(from_id));
        put_u16be(out.data() + 2, static_cast<uint16_t>(to_id));
    } else if (mode == TunnelMode::V3) {
        put_u32le(out.data(), from_id);
        put_u32le(out.data() + 4, to_id);
    }
    if (len) std::memcpy(out.data() + h, payload, len);
}

bool tunnel_unwrap(TunnelMode mode, const uint8_t* data, size_t len, uint32_t* from_id,
                   uint32_t* to_id, const uint8_t** payload, size_t* plen, int64_t expect_to) {
    uint32_t f = 0, t = 0;
    size_t h = 0;
    if (mode == TunnelMode::V2) {
        if (len < 5) return false;  // spawner NetHack: drop anything shorter than 5
        f = get_u16be(data);
        t = get_u16be(data + 2);
        h = 4;
    } else if (mode == TunnelMode::V3) {
        if (len < 8) return false;  // cncnet-server TunnelV3: min 8 bytes
        f = get_u32le(data);
        t = get_u32le(data + 4);
        h = 8;
    }
    if (expect_to >= 0 && t != static_cast<uint32_t>(expect_to)) return false;
    if (from_id) *from_id = f;
    if (to_id) *to_id = t;
    *payload = data + h;
    *plen = len - h;
    return true;
}

std::vector<uint8_t> make_tunnel_ping(uint32_t cookie) {
    std::vector<uint8_t> p(kTunnelPingSize, 0);
    // bytes 0..7 must stay zero (sender = receiver = 0); the cookie rides in
    // bytes 8..11, which the server echoes back in its 12-byte reply.
    put_u32le(p.data() + 8, cookie);
    return p;
}

bool is_tunnel_ping_reply(const uint8_t* data, size_t len, uint32_t cookie) {
    if (len != kTunnelPingReplySize) return false;
    for (int i = 0; i < 8; ++i)
        if (data[i] != 0) return false;
    return get_u32le(data + 8) == cookie;
}

}  // namespace ra2::net
