// engine/net/event_codec.hpp -- the original's game-packet body codec.
//
// A game packet (the payload of a 0xAA04 channel packet) is:
//
//   CommProtocol 1/2 (compressed; always 2 in practice):
//     off 0  u8  0x1C FRAMEINFO (or 0x19 FRAMESYNC)
//     off 1  u8  0 (never written by Send_Packets; stale) -> 0
//     off 2  i8  sender HouseIndex
//     off 3  u32 Frame   = stamp_frame(now, MaxAhead, FSR, protocol)
//     off 7  u32 CRC of the sender's current frame (FRAMESYNC: ScenarioMagic)
//     off 11 u16 cumulative CommandCount sent BEFORE this Send_Packets call
//     off 13 u8  Delay = MaxAhead  (receiver: sender's frame = Frame - Delay)
//     off 14 records [u8 type][kEventPayloadSize[type] bytes] (house/frame inherited)
//              0x04: [04][u8 count][23-byte payload]{[5-byte Whom] x (count-1)}
//              0x1B: [1B][1 byte from data+6]
//              0x1F: [1F][u32 len][len bytes]
//   CommProtocol 0 (raw): a 111-byte FRAMEINFO event then 111-byte events
//   (ADDPLAYER's blob follows its event).
//
// Encoder = Add_Compressed_Events FUN_0064B0D0 with the megamission cache
// lookup FUN_0064AF40 (sender cache: 8 slots at 0xAC4198, counts 0xAF92A0,
// LRU frames 0xAF9394). Decoder = Extract_Compressed_Events FUN_0064B840
// with the receiver cache MegaMissionTargets[house*8+slot] (0xAF93D0, counts
// 0xAC4064). Both are ported decision-for-decision, quirks included (see
// docs/re/net-protocol.md section 5).
#pragma once

#include "net/event.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace ra2::net {

constexpr size_t kFrameHeaderSize = 14;
// Send buffer length: Session area 0xA8C77A..0xA8C9A8 (0x22E bytes); equal to
// the private channel's max payload.
constexpr size_t kMaxGamePacket = 0x22E;
constexpr int kMegaMissionCacheSlots = 8;
constexpr int kMegaMissionMaxList = 128;  // MegaMissionTargets[64][128]
constexpr int kMaxHouses = 8;

struct FrameHeader {
    uint8_t type = static_cast<uint8_t>(EventType::FRAMEINFO);
    uint8_t flags = 0;
    int8_t house = 0;
    uint32_t frame = 0;
    uint32_t crc = 0;  // FRAMESYNC: ScenarioMagic (0xA8D710)
    uint16_t command_count = 0;
    uint8_t delay = 0;

    void write(uint8_t* p) const;
    static FrameHeader read(const uint8_t* p);
    // Receiver-side "their frame" (Process_Receive_Packet FUN_00649B30).
    int32_t sender_frame() const { return static_cast<int32_t>(frame) - delay; }
};

// Frame stamp used by Send_Packets 0x649710, Add_Compressed_Events 0x64B0D0
// and Send_FrameSync 0x649A70:
//   protocol 2: ceil((now + max_ahead) / fsr) * fsr      (integer math as in
//               the binary: ((fsr + now - 1 + max_ahead) / fsr) * fsr)
//   otherwise : now + max_ahead
uint32_t stamp_frame(uint32_t now, uint32_t max_ahead, uint32_t fsr, int protocol);

class EventEncoder {
public:
    explicit EventEncoder(bool spawner_ext = false) : spawner_ext_(spawner_ext) { reset(); }

    // Queue_AI_Multiplayer at frame 0 / load: zero the 8 LRU frames and counts.
    void reset();

    // Add_Compressed_Events. Moves events from the front of `outlist` into
    // `buf` starting at `off` (buf must hold `buflen` bytes; the original
    // passes 0x22E). Each consumed event is stamped with (frame, house),
    // IsExecuted cleared, and a copy appended to `echo` (the local DoList).
    // Stops when `max_count` events are consumed, the buffer is full, or
    // `echo_capacity` copies have been made (DoList 16384 limit).
    // Returns the new write offset; `consumed` gets the number of events.
    size_t add_compressed(std::deque<Event>& outlist, uint8_t* buf, size_t buflen, size_t off,
                          int max_count, uint32_t frame, int8_t house, uint32_t now_frame,
                          std::vector<Event>& echo, size_t echo_capacity, int& consumed);

    struct Slot {
        uint32_t count = 0;        // 0xAF92A0[slot]
        uint32_t saved_frame = 0;  // 0xAF9394[slot]
        std::array<uint8_t, kMegaMissionMaxList * 5> whoms{};  // 0xAC4198 + slot*0x280
    };
    const Slot& slot(int i) const { return slots_[i]; }

private:
    // FUN_0064AF40(count&, offset&, payload, list, finalize)
    bool cache_lookup(uint8_t& count, size_t& off, uint8_t* payload, const uint8_t* list,
                      bool finalize, uint32_t now_frame);
    bool spawner_ext_;
    std::array<Slot, kMegaMissionCacheSlots> slots_;
};

class EventDecoder {
public:
    explicit EventDecoder(bool spawner_ext = false) : spawner_ext_(spawner_ext) { reset(); }
    void reset();  // zero MegaMissionTargetNum (0xAC4064)

    // Extract_Compressed_Events. Decodes a whole compressed packet (FRAMEINFO
    // header first; a leading FRAMESYNC header is skipped). Appends every
    // decoded event to `out`, the FRAMEINFO itself first. Returns the number
    // of events appended, or -1 when `capacity` would be exceeded (the
    // original returns -1 on DoList full). Malformed input (unknown type,
    // truncated blob, megamission replay of an empty cache slot) stops the
    // walk early; see deviation D3 in docs/re/net-protocol.md.
    int extract(const uint8_t* pkt, size_t len, std::vector<Event>& out,
                size_t capacity = static_cast<size_t>(-1));

    uint32_t cached_count(int house, int slot) const { return counts_[house * 8 + slot]; }

private:
    bool spawner_ext_;
    std::array<uint32_t, kMaxHouses * kMegaMissionCacheSlots> counts_{};
    std::array<std::array<uint8_t, kMegaMissionMaxList * 5>, kMaxHouses * kMegaMissionCacheSlots> lists_{};
};

// ----------------------------------------------------- CommProtocol 0
// Send_Packets' raw branch: events at a 111-byte stride after a 111-byte
// FRAMEINFO; frame = now + MaxAhead (never FSR-rounded in this branch).
size_t encode_raw_events(std::deque<Event>& outlist, uint8_t* buf, size_t buflen, size_t off,
                         int max_count, uint32_t frame, int8_t house, std::vector<Event>& echo,
                         size_t echo_capacity, int& consumed);
// Process_Receive_Packet's raw branch: returns events appended (FRAMEINFO
// included), -1 on capacity overflow.
int decode_raw_events(const uint8_t* pkt, size_t len, std::vector<Event>& out,
                      size_t capacity = static_cast<size_t>(-1));

}  // namespace ra2::net
