// engine/net/global_msg.hpp -- in-game messages carried on the 0x1236
// global channel (GlobalPacketType, 0x1C7 bytes). They are out-of-band: they
// never enter the lockstep stream and never affect sync.
//
// Recovered layouts (docs/gap/net/01-original-protocol.md section 6):
//   chat  (cmd 0x0C, FUN_0055E050, handler case 0x0C in FUN_0048D0F0)
//     +0x000 u32 cmd  +0x004 ANSI sender name  +0x02F u8 router chat mask
//     +0x030 UTF-16 message  +0x110 i32 colour scheme  +0x114 u32 game-name hash
//   kick proposal (cmd 0x1C, FUN_0064A700)
//     +0x000 u32 cmd  +0x004 name[20]  +0x02F i32 target house  +0x033 i32 proposer
// Chat and kick proposals are sent DATA_ACK, one copy per recipient.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ra2::net {

constexpr size_t kGlobalPacketSize = 0x1C7;

enum GlobalCmd : uint32_t {
    kGlobalSignOff = 0x0A,     // NET_SIGN_OFF -> Destroy_Connection
    kGlobalChat = 0x0C,        // NET_MESSAGE
    kGlobalLoadProgress = 0x0F,
    kGlobalKickVote = 0x1C,
    kGlobalTaunt = 0x1D,
    kGlobalBeaconPlace = 0x20,
    kGlobalBeaconDelete = 0x21,
    kGlobalBeaconMessage = 0x22,
    // Clone extensions (outside the original's 0x00-0x2F range; an original
    // peer ignores unknown commands):
    kGlobalAway = 0x41,        // NET-PLAY: a player paused (Android background)
    kGlobalBack = 0x42,        // NET-PLAY: ... and is back
};

inline uint32_t global_cmd(const std::vector<uint8_t>& body) {
    return body.size() >= 4 ? (uint32_t(body[0]) | uint32_t(body[1]) << 8 | uint32_t(body[2]) << 16 |
                               uint32_t(body[3]) << 24)
                            : 0xFFFFFFFFu;
}

struct ChatMessage {
    std::string sender;       // ANSI, max 42 chars (0x04..0x2E)
    uint8_t chat_mask = 0;
    std::u16string text;      // max 111 UTF-16 units (+0x30..+0x10F)
    int32_t color = 0;
    uint32_t game_hash = 0;   // FUN_005DA620 over the session name (not computed here)
};
std::vector<uint8_t> encode_chat(const ChatMessage& m);  // 0x1C7 bytes
bool decode_chat(const std::vector<uint8_t>& body, ChatMessage& out);

struct KickVote {
    std::string name;         // max 19 chars
    int32_t target_house = -1;
    int32_t proposer_house = -1;
};
std::vector<uint8_t> encode_kick_vote(const KickVote& k);  // 0x1C7 bytes
bool decode_kick_vote(const std::vector<uint8_t>& body, KickVote& out);

// UTF-8 <-> UTF-16 for the chat text (BMP; anything else becomes '?').
std::u16string utf8_to_u16(const std::string& s);
std::string u16_to_utf8(const std::u16string& s);

// W9-05-08 beacons [RE: BeaconManager 0x89B328; place FUN_00430B20, delete
// FUN_00431140, text FUN_004313D0; received in FUN_0048D0F0 cases 0x20-0x22].
// Sent DATA_ACK to every other player:
//   place  (cmd 0x20, 0x3D bytes) +0x04 ANSI sender name[43]  +0x2F i32 x
//          +0x33 i32 y  +0x37 i32 z (leptons)  +0x3B u8 slot  +0x3C u8 house
//   delete (cmd 0x21, 0x3D bytes) +0x04 name  +0x3B u8 slot  +0x3C u8 house
//   text   (cmd 0x22, 0x1C7 bytes) +0x04 name  +0x2F u8 slot  +0x30 u8 house
//          +0x31 UTF-16 text, 127 units + NUL
constexpr size_t kBeaconPacketSize = 0x3D;
constexpr int kBeaconsPerHouse = 3;     // BeaconManager: 8 houses x 3 slots
constexpr size_t kBeaconTextMax = 127;  // FUN_004313D0 copies 0x7F chars
struct BeaconMsg {
    uint32_t cmd = kGlobalBeaconPlace;   // 0x20 place / 0x21 delete / 0x22 text
    std::string sender;
    int32_t x = 0, y = 0, z = 0;        // leptons (place)
    int slot = 0;                        // 0..2
    int house = -1;
    std::u16string text;                 // (text)
};
std::vector<uint8_t> encode_beacon_msg(const BeaconMsg& m);
bool decode_beacon_msg(const std::vector<uint8_t>& body, BeaconMsg& out);

// NET-PLAY away notice (cmd 0x41 / 0x42): +0x04 i32 house, +0x08 u32 seconds
// the others hold the player's slot before the normal stall / kick resumes.
struct AwayNotice {
    bool back = false;          // false = AWAY (0x41), true = BACK (0x42)
    int32_t house = -1;
    uint32_t seconds = 0;
};
std::vector<uint8_t> encode_away(const AwayNotice& a);  // 0x1C7 bytes
bool decode_away(const std::vector<uint8_t>& body, AwayNotice& out);

}  // namespace ra2::net
