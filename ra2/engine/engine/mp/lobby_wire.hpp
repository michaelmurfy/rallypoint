#pragma once
// engine/mp/lobby_wire -- the in-room lobby's bytes on the wire, in the
// ORIGINAL game's global-channel format (docs/gap/net/01-original-protocol.md
// §2.1, §2.3, §6, §7.1), so the clone's lobby is format-compatible with
// gamemd's out-of-band channel:
//
//   datagram  = [u32 CRC][payload]                    (UDPInterfaceClass, §2.1)
//               CRC: Add_CRC = per LE u32 word crc = rotl1(crc) + w, tail
//               bytes masked into a final word
//   payload   = 16-byte IPXGlobalConnClass header     (§2.3)
//               u16 magic 0x1236, u8 code, u8 0, u32 PacketID, u32 0, u16 0,
//               u16 ProductID 0xAA04 (YR)
//               code 0 = DATA_ACK (reliable), 1 = DATA_NOACK, 2 = ACK
//             + 455-byte GlobalPacketType body (ACKs carry no body)
//   body      = u32 cmd at +0, then per command
//
// Commands use the original NET_* numbering where the original has one
// (0x06 QUERY_JOIN, 0x07 CONFIRM_JOIN, 0x08 REJECT_JOIN, 0x09 GAME_OPTIONS,
// 0x0A SIGN_OFF, 0x0B GO, 0x0C MESSAGE, 0x0D PING, 0x13 READY_TO_GO); the
// chat packet (0x0C) uses the exact original layout (sender name at +4,
// chat mask +0x2F, UTF-16 text +0x30, colour +0x110, game-name hash +0x114).
// The clone-only slot request is 0x40 and the lobby AWAY / BACK 0x41 (outside
// the original 0x00-0x2F range).
// The options string inside GAME_OPTIONS / GO is the original comma layout
// starting with the seed (mp_options.hpp).
//
// NET-PLAY: the datagram CRC, the 0x1236 header, ACK / resend / dedupe and
// the chat packet layout are engine/net's (wire.hpp, channel.hpp,
// global_msg.hpp; mp_net.hpp wires the lobby onto them). This file keeps the
// lobby's command bodies. SDL-free.
#include <cstdint>
#include <string>
#include <vector>

namespace ra2mp {

constexpr std::size_t kGlobalBody = 0x1C7;   // 455: GlobalPacketType (engine/net kGlobalMaxPayload)

enum LobbyCmd : std::uint32_t {
    kCmdQueryJoin = 0x06,
    kCmdConfirmJoin = 0x07,
    kCmdRejectJoin = 0x08,
    kCmdGameOptions = 0x09,
    kCmdSignOff = 0x0A,
    kCmdGo = 0x0B,
    kCmdMessage = 0x0C,
    kCmdPing = 0x0D,
    kCmdReadyToGo = 0x13,
    kCmdSlotRequest = 0x40,   // clone extension
    kCmdAway = 0x41,          // clone extension (W9-04-07): u8 back, u16 seconds
};

// --- body helpers (little-endian, bounds-checked) ---------------------------
struct BodyWriter {
    std::vector<std::uint8_t> b = std::vector<std::uint8_t>(kGlobalBody, 0);
    std::size_t pos = 0;
    void u8(std::uint8_t v) { if (pos < b.size()) b[pos] = v; ++pos; }
    void u16(std::uint16_t v) { u8(v & 0xFF); u8(v >> 8); }
    void u32(std::uint32_t v) { u16(v & 0xFFFF); u16(v >> 16); }
    void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
    void u64(std::uint64_t v) { u32(static_cast<std::uint32_t>(v)); u32(static_cast<std::uint32_t>(v >> 32)); }
    void at(std::size_t p) { pos = p; }
    // Fixed-width NUL-padded string field.
    void fixed(const std::string& s, std::size_t width);
    bool overflow() const { return pos > b.size(); }
};

struct BodyReader {
    const std::vector<std::uint8_t>& b;
    std::size_t pos = 0;
    bool bad = false;
    explicit BodyReader(const std::vector<std::uint8_t>& body) : b(body) {}
    std::uint8_t u8() { if (pos >= b.size()) { bad = true; return 0; } return b[pos++]; }
    std::uint16_t u16() { const std::uint16_t lo = u8(); return static_cast<std::uint16_t>(lo | (u8() << 8)); }
    std::uint32_t u32() { const std::uint32_t lo = u16(); return lo | (static_cast<std::uint32_t>(u16()) << 16); }
    std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
    std::uint64_t u64() { const std::uint64_t lo = u32(); return lo | (static_cast<std::uint64_t>(u32()) << 32); }
    void at(std::size_t p) { pos = p; }
    std::string fixed(std::size_t width);
};

// --- the original chat packet (cmd 0x0C, FUN_0055E050) ----------------------
struct ChatPacket {
    std::string sender;         // ANSI, +0x004 .. +0x02E (43 bytes)
    std::uint8_t chat_mask = 0; // +0x02F (router mask; 0 here)
    std::string text;           // UTF-8 here, UTF-16LE on the wire at +0x030 (112 units)
    std::int32_t color = 0;     // +0x110 sender colour scheme
    std::uint32_t game_hash = 0;// +0x114 Add_CRC over the session name
};
std::vector<std::uint8_t> encode_chat(const ChatPacket& c);
bool decode_chat(const std::vector<std::uint8_t>& body, ChatPacket& out);
// Add_CRC (engine/net packet_crc) over a session (room) name: the chat
// packet's game-name hash.
std::uint32_t game_name_hash(const std::string& name);

}  // namespace ra2mp
