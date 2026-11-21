// engine/net/wire.hpp -- the outermost layers of an original-format datagram.
//
//   [tunnel header (optional): V2 = u16 BE from, u16 BE to | V3 = u32 LE from, u32 LE to]
//   [u32 LE rotate-add checksum over the payload]                 UDPInterfaceClass
//   [payload: a channel packet (0xAA04 private / 0x1236 global)]  ConnectionClass
//
// Byte-exact with gamemd.exe (YR 1.001, this install's build):
//   * Add_CRC                 FUN_0064D8C0  crc = rotl1(crc) + w
//   * Build packet CRC        FUN_007B0DD0  (UDPInterfaceClass vtable +0x54)
//   * Check packet CRC        FUN_007B0E50  (vtable +0x58; payload length must be < 0x280)
//   * recvfrom cap            0x280 in FUN_007B28A0 (so a received payload is <= 636 bytes)
// Tunnel headers: CnCNet spawner NetHack (V2, emitted by gamemd itself) and the
// CnCNet tunnel server V3 header (docs/gap/net/02-cncnet.md section 3.2).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ra2::net {

// recvfrom() buffer size in UDPInterfaceClass::Message_Handler (0x7B28A0).
constexpr size_t kMaxDatagram = 0x280;
// Largest payload the CRC check accepts (len - 4 must be < 0x280; with the
// 0x280 receive cap the real limit is 0x280 - 4 = 636).
constexpr size_t kMaxDatagramPayload = kMaxDatagram - 4;

// FUN_0064D8C0: one rotate-left-by-1 then add step.
inline uint32_t add_crc(uint32_t crc, uint32_t value) {
    return ((crc << 1) | (crc >> 31)) + value;
}

// FUN_007B0DD0 / FUN_007B0E50: checksum of a datagram payload. Full LE dwords
// first; a 1..3 byte tail is read as a dword and masked to its low len&3 bytes.
uint32_t packet_crc(const uint8_t* payload, size_t len);

// Prepend the u32 checksum: out = [crc][payload]. Returns false when the
// payload would not fit a 0x280-byte receive buffer on the far side.
bool build_datagram(const uint8_t* payload, size_t len, std::vector<uint8_t>& out);

// Validate a received [crc][payload] datagram. On success *payload/*plen
// point inside `data`. Mirrors FUN_007B0E50: rejects payloads >= 0x280 bytes
// and checksum mismatches ("Error in Winsock packet CRC").
bool check_datagram(const uint8_t* data, size_t len, const uint8_t** payload, size_t* plen);

// ---------------------------------------------------------------- tunnels
enum class TunnelMode : uint8_t {
    None = 0,  // plain P2P UDP (LAN / direct)
    V2 = 2,    // CnCNet tunnel v2: [u16 BE from][u16 BE to]  (what the spawner emits)
    V3 = 3,    // CnCNet tunnel v3: [u32 LE from][u32 LE to]
};

constexpr size_t tunnel_header_size(TunnelMode m) {
    return m == TunnelMode::V2 ? 4 : m == TunnelMode::V3 ? 8 : 0;
}

// out = [header][payload]. V2 ids are truncated to 16 bits.
void tunnel_wrap(TunnelMode mode, uint32_t from_id, uint32_t to_id, const uint8_t* payload,
                 size_t len, std::vector<uint8_t>& out);

// Strip a tunnel header. Returns false when the datagram is too short
// (V2: the spawner requires at least 5 bytes; V3: at least 8), or when
// `expect_to` is non-negative and does not match the receiver id.
bool tunnel_unwrap(TunnelMode mode, const uint8_t* data, size_t len, uint32_t* from_id,
                   uint32_t* to_id, const uint8_t** payload, size_t* plen,
                   int64_t expect_to = -1);

// Tunnel server ping: 50 bytes, sender == receiver == 0; the server answers
// with the first 12 bytes (cncnet-server TunnelV2/TunnelV3).
constexpr size_t kTunnelPingSize = 50;
constexpr size_t kTunnelPingReplySize = 12;
std::vector<uint8_t> make_tunnel_ping(uint32_t cookie);
bool is_tunnel_ping_reply(const uint8_t* data, size_t len, uint32_t cookie);

}  // namespace ra2::net
