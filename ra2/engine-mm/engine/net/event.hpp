// engine/net/event.hpp -- EventClass, the original's lockstep command record.
//
// In memory (and on the wire under CommProtocol 0) an event is 111 packed
// bytes (YRpp EventClass.h, confirmed by the 0x1B-dword + 3-byte copy loops):
//   +0 u8 Type   +1 u8 flags (bit0 IsExecuted, local only)   +2 i8 HouseIndex
//   +3 u32 Frame (execution frame)   +7 104-byte payload union
// Under CommProtocol 1/2 only `kEventPayloadSize[type]` payload bytes travel
// (table at 0x81F8FC, names at 0x81F92C, executor EventClass::Execute
// FUN_004C6BD0). A TargetClass is 5 bytes: u32 id (low 24 bits significant)
// + u8 RTTI (AbstractType).
//
// Quirks reproduced:
//  * RESPONSE_TIME (0x1B) keeps its 1 payload byte at data+6 (event +0x0D).
//  * ADDPLAYER (0x1F) keeps a heap pointer at data+0 and the blob length at
//    data+4; on the wire it is [0x1F][u32 len][len bytes]. Here the blob is
//    held in Event::blob and data+0..3 stays 0.
//  * 0x30 ResponseTime2 is the CnCNet spawner's extension (2 bytes: i8
//    MaxAhead, u8 LatencyLevel); a vanilla gamemd reads size[0x30] past the
//    end of its table (0xF8) and cannot decode it.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ra2::net {

enum class EventType : uint8_t {
    EMPTY = 0x00,
    POWERON = 0x01,
    POWEROFF = 0x02,
    ALLY = 0x03,
    MEGAMISSION = 0x04,
    MEGAMISSION_F = 0x05,
    IDLE = 0x06,
    SCATTER = 0x07,
    DESTRUCT = 0x08,
    DEPLOY = 0x09,
    DETONATE = 0x0A,
    PLACE = 0x0B,
    OPTIONS = 0x0C,
    GAMESPEED = 0x0D,
    PRODUCE = 0x0E,
    SUSPEND = 0x0F,
    ABANDON = 0x10,
    PRIMARY = 0x11,
    SPECIAL_PLACE = 0x12,
    EXIT = 0x13,
    ANIMATION = 0x14,
    REPAIR = 0x15,
    SELL = 0x16,
    SELLCELL = 0x17,
    SPECIAL = 0x18,
    FRAMESYNC = 0x19,
    MESSAGE = 0x1A,
    RESPONSE_TIME = 0x1B,
    FRAMEINFO = 0x1C,
    SAVEGAME = 0x1D,
    ARCHIVE = 0x1E,
    ADDPLAYER = 0x1F,
    TIMING = 0x20,
    PROCESS_TIME = 0x21,
    PAGEUSER = 0x22,
    REMOVEPLAYER = 0x23,
    LATENCYFUDGE = 0x24,
    MEGAFRAMEINFO = 0x25,
    PACKETTIMING = 0x26,
    ABOUTTOEXIT = 0x27,
    FALLBACKHOST = 0x28,
    ADDRESSCHANGE = 0x29,
    PLANCONNECT = 0x2A,
    PLANCOMMIT = 0x2B,
    PLANNODEDELETE = 0x2C,
    ALLCHEER = 0x2D,
    ABANDON_ALL = 0x2E,
    RESPONSE_TIME2 = 0x30,  // CnCNet spawner only
};

constexpr int kVanillaEventTypes = 47;  // 0x00..0x2E (YR); RA2 game.exe has 46
constexpr size_t kEventClassSize = 111;
constexpr size_t kEventDataSize = 104;

// Verbatim dump of the size table at 0x81F8FC (48 bytes; [0x2F] is padding).
extern const uint8_t kEventPayloadSize[48];

// "EMPTY", "POWERON", ... from 0x81F92C; "RESPONSETIME2" for 0x30; "?" else.
const char* event_type_name(uint8_t type);

// Payload bytes on the wire for `type`, or -1 when this decoder treats the
// type as undecodable (> 0x2E, or 0x30 without the spawner extension).
int event_wire_size(uint8_t type, bool spawner_ext);

struct TargetRef {
    uint32_t id = 0;   // low 24 bits significant (resolved through UniqueID)
    uint8_t rtti = 0;  // AbstractType; 0 = none

    static TargetRef read(const uint8_t* p);
    void write(uint8_t* p) const;
    // The original's debug-print packing: rtti<<24 | id&0xFFFFFF, -1 if none.
    uint32_t packed() const { return rtti == 0 ? 0xFFFFFFFFu : (uint32_t(rtti) << 24) | (id & 0xFFFFFFu); }
    bool operator==(const TargetRef& o) const { return id == o.id && rtti == o.rtti; }
    bool operator!=(const TargetRef& o) const { return !(*this == o); }
};

struct CellXY {
    int16_t x = 0, y = 0;
    bool operator==(const CellXY& o) const { return x == o.x && y == o.y; }
};

struct Event {
    uint8_t type = 0;
    uint8_t flags = 0;  // bit0 IsExecuted (never meaningful on the wire)
    int8_t house = 0;
    uint32_t frame = 0;
    std::array<uint8_t, kEventDataSize> data{};
    std::vector<uint8_t> blob;  // ADDPLAYER payload (length mirrored at data+4)

    EventType kind() const { return static_cast<EventType>(type); }
    bool executed() const { return (flags & 1) != 0; }

    // CommProtocol 0 layout: 111 bytes. ADDPLAYER's pointer field is written 0.
    void to_raw(uint8_t out[kEventClassSize]) const;
    static Event from_raw(const uint8_t* in);

    // Field helpers on the payload union.
    int32_t i32(size_t off) const;
    void set_i32(size_t off, int32_t v);
    uint16_t u16(size_t off) const;
    void set_u16(size_t off, uint16_t v);
    TargetRef target(size_t off) const { return TargetRef::read(data.data() + off); }
    void set_target(size_t off, const TargetRef& t) { t.write(data.data() + off); }
    CellXY cell(size_t off) const;
    void set_cell(size_t off, const CellXY& c);

    // Equality over the bytes that travel on the wire for this type (type,
    // house, frame, payload[0..size), blob). flags are ignored.
    bool wire_equal(const Event& o, bool spawner_ext = true) const;
};

// ------------------------------------------------------- typed payloads
// Offsets are into Event::data (event offset - 7). See docs/re/net-protocol.md.
namespace ev {

Event make(EventType t, int house, uint32_t frame = 0);

// POWERON/POWEROFF/IDLE/SCATTER/DEPLOY/DETONATE/PRIMARY/REPAIR/SELL/PLANNODEDELETE: Target @0
Event whom(EventType t, int house, const TargetRef& who);

struct MegaMission {  // 0x04, 23 bytes
    TargetRef whom;     // @0
    uint8_t mission = 0;  // @5
    uint8_t slot = 0;     // @6  (megamission unit-list cache slot, set by the encoder)
    TargetRef target;   // @7
    TargetRef dest;     // @12
    TargetRef follow;   // @17
    bool planning = false;  // @22
};
Event mega_mission(int house, const MegaMission& m);
MegaMission get_mega_mission(const Event& e);

struct MegaMissionF {  // 0x05, 24 bytes
    TargetRef whom;       // @0
    uint8_t mission = 0;  // @5
    TargetRef target;     // @6
    TargetRef dest;       // @11
    int32_t speed = 0;    // @16
    int32_t max_speed = 0;  // @20
};
Event mega_mission_f(int house, const MegaMissionF& m);
MegaMissionF get_mega_mission_f(const Event& e);

struct Production {  // PRODUCE/SUSPEND/ABANDON/ABANDON_ALL, 12 bytes
    int32_t rtti = 0, heap_id = 0, is_naval = 0;
};
Event production(EventType t, int house, const Production& p);
Production get_production(const Event& e);

struct Place {  // 0x0B, 16 bytes
    int32_t rtti = 0, heap_id = 0, is_naval = 0;
    CellXY cell;  // @12
};
Event place(int house, const Place& p);
Place get_place(const Event& e);

struct SpecialPlace {  // 0x12, 8 bytes
    int32_t sw_index = 0;
    CellXY cell;  // @4
};
Event special_place(int house, const SpecialPlace& p);
SpecialPlace get_special_place(const Event& e);

struct Animation {  // 0x14, 16 bytes
    int32_t anim_id = 0, house_id = 0, x = 0, y = 0;
};
Event animation(int house, const Animation& a);
Animation get_animation(const Event& e);

// ALLY / GAMESPEED / SPECIAL / REMOVEPLAYER / LATENCYFUDGE / FALLBACKHOST / ALLCHEER: i32 @0
Event int_event(EventType t, int house, int32_t v);
// SELLCELL: CellXY @0
Event sell_cell(int house, const CellXY& c);
// ARCHIVE / PLANCONNECT: Target @0, Target @5
Event two_targets(EventType t, int house, const TargetRef& a, const TargetRef& b);

struct FrameInfo {  // 0x1C, 7 bytes
    uint32_t crc = 0;
    uint16_t command_count = 0;
    uint8_t delay = 0;
};
Event frame_info(int house, uint32_t frame, const FrameInfo& f);
FrameInfo get_frame_info(const Event& e);

struct Timing {  // 0x20, 5 bytes
    uint16_t desired_fps = 0, max_ahead = 0;
    uint8_t frame_send_rate = 0;
};
Event timing(int house, const Timing& t);
Timing get_timing(const Event& e);

Event process_time(int house, uint16_t avg_ms);      // 0x21: u16 @0
Event response_time(int house, uint8_t max_ahead);   // 0x1B: u8 @6
uint8_t get_response_time(const Event& e);

struct AddressChange {  // 0x29, 5 bytes
    int8_t player = 0;
    uint32_t addr = 0;
};
Event address_change(int house, const AddressChange& a);
AddressChange get_address_change(const Event& e);

Event add_player(int house, const std::vector<uint8_t>& blob);  // 0x1F

struct ResponseTime2 {  // 0x30 (spawner), 2 bytes
    int8_t max_ahead = 0;
    uint8_t latency_level = 0;
};
Event response_time2(int house, const ResponseTime2& r);
ResponseTime2 get_response_time2(const Event& e);

}  // namespace ev
}  // namespace ra2::net
