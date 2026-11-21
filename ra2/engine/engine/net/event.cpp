// engine/net/event.cpp -- see event.hpp.
#include "net/event.hpp"

#include "net/net_bytes.hpp"

#include <cstring>

namespace ra2::net {

// 0x81F8FC (dis1.txt in D:\ra2-re\net1-out, re-dumped for this module).
const uint8_t kEventPayloadSize[48] = {
    0x00, 0x05, 0x05, 0x04, 0x17, 0x18, 0x05, 0x05, 0x00, 0x05, 0x05, 0x10,
    0x00, 0x04, 0x0C, 0x0C, 0x0C, 0x05, 0x08, 0x00, 0x10, 0x05, 0x05, 0x04,
    0x04, 0x00, 0x00, 0x01, 0x07, 0x00, 0x0A, 0x04, 0x05, 0x02, 0x00, 0x04,
    0x04, 0x68, 0x40, 0x00, 0x04, 0x05, 0x0A, 0x00, 0x05, 0x04, 0x0C, 0x00,
};

namespace {
const char* const kNames[kVanillaEventTypes] = {
    "EMPTY", "POWERON", "POWEROFF", "ALLY", "MEGAMISSION", "MEGAMISSION_F", "IDLE", "SCATTER",
    "DESTRUCT", "DEPLOY", "DETONATE", "PLACE", "OPTIONS", "GAMESPEED", "PRODUCE", "SUSPEND",
    "ABANDON", "PRIMARY", "SPECIAL_PLACE", "EXIT", "ANIMATION", "REPAIR", "SELL", "SELLCELL",
    "SPECIAL", "FRAMESYNC", "MESSAGE", "RESPONSE_TIME", "FRAMEINFO", "SAVEGAME", "ARCHIVE",
    "ADDPLAYER", "TIMING", "PROCESS_TIME", "PAGEUSER", "REMOVEPLAYER", "LATENCYFUDGE",
    "MEGAFRAMEINFO", "PACKETTIMING", "ABOUTTOEXIT", "FALLBACKHOST", "ADDRESSCHANGE",
    "PLANCONNECT", "PLANCOMMIT", "PLANNODEDELETE", "ALLCHEER", "ABANDON_ALL",
};

// Where a type's wire payload lives inside Event::data.
size_t wire_data_offset(uint8_t type) {
    if (type == static_cast<uint8_t>(EventType::RESPONSE_TIME)) return 6;
    if (type == static_cast<uint8_t>(EventType::ADDPLAYER)) return 4;
    return 0;
}
}  // namespace

const char* event_type_name(uint8_t type) {
    if (type < kVanillaEventTypes) return kNames[type];
    if (type == static_cast<uint8_t>(EventType::RESPONSE_TIME2)) return "RESPONSETIME2";
    return "?";
}

int event_wire_size(uint8_t type, bool spawner_ext) {
    if (type < kVanillaEventTypes) return kEventPayloadSize[type];
    if (spawner_ext && type == static_cast<uint8_t>(EventType::RESPONSE_TIME2)) return 2;
    return -1;
}

TargetRef TargetRef::read(const uint8_t* p) {
    TargetRef t;
    t.id = get_u32le(p);
    t.rtti = p[4];
    return t;
}

void TargetRef::write(uint8_t* p) const {
    put_u32le(p, id);
    p[4] = rtti;
}

void Event::to_raw(uint8_t out[kEventClassSize]) const {
    out[0] = type;
    out[1] = flags;
    out[2] = static_cast<uint8_t>(house);
    put_u32le(out + 3, frame);
    std::memcpy(out + 7, data.data(), kEventDataSize);
    if (type == static_cast<uint8_t>(EventType::ADDPLAYER)) {
        put_u32le(out + 7, 0);  // heap pointer: meaningless off-process
        put_u32le(out + 11, static_cast<uint32_t>(blob.size()));
    }
}

Event Event::from_raw(const uint8_t* in) {
    Event e;
    e.type = in[0];
    e.flags = in[1];
    e.house = static_cast<int8_t>(in[2]);
    e.frame = get_u32le(in + 3);
    std::memcpy(e.data.data(), in + 7, kEventDataSize);
    return e;
}

int32_t Event::i32(size_t off) const { return get_i32le(data.data() + off); }
void Event::set_i32(size_t off, int32_t v) { put_i32le(data.data() + off, v); }
uint16_t Event::u16(size_t off) const { return get_u16le(data.data() + off); }
void Event::set_u16(size_t off, uint16_t v) { put_u16le(data.data() + off, v); }
CellXY Event::cell(size_t off) const {
    return CellXY{get_i16le(data.data() + off), get_i16le(data.data() + off + 2)};
}
void Event::set_cell(size_t off, const CellXY& c) {
    put_i16le(data.data() + off, c.x);
    put_i16le(data.data() + off + 2, c.y);
}

bool Event::wire_equal(const Event& o, bool spawner_ext) const {
    if (type != o.type || house != o.house || frame != o.frame) return false;
    const int n = event_wire_size(type, spawner_ext);
    if (n < 0) return data == o.data;
    const size_t off = wire_data_offset(type);
    if (std::memcmp(data.data() + off, o.data.data() + off, static_cast<size_t>(n)) != 0) return false;
    if (type == static_cast<uint8_t>(EventType::ADDPLAYER)) return blob == o.blob;
    return true;
}

// ------------------------------------------------------------- typed
namespace ev {

Event make(EventType t, int house, uint32_t frame) {
    Event e;
    e.type = static_cast<uint8_t>(t);
    e.house = static_cast<int8_t>(house);
    e.frame = frame;
    return e;
}

Event whom(EventType t, int house, const TargetRef& who) {
    Event e = make(t, house);
    e.set_target(0, who);
    return e;
}

Event mega_mission(int house, const MegaMission& m) {
    Event e = make(EventType::MEGAMISSION, house);
    e.set_target(0, m.whom);
    e.data[5] = m.mission;
    e.data[6] = m.slot;
    e.set_target(7, m.target);
    e.set_target(12, m.dest);
    e.set_target(17, m.follow);
    e.data[22] = m.planning ? 1 : 0;
    return e;
}

MegaMission get_mega_mission(const Event& e) {
    MegaMission m;
    m.whom = e.target(0);
    m.mission = e.data[5];
    m.slot = e.data[6];
    m.target = e.target(7);
    m.dest = e.target(12);
    m.follow = e.target(17);
    m.planning = e.data[22] != 0;
    return m;
}

Event mega_mission_f(int house, const MegaMissionF& m) {
    Event e = make(EventType::MEGAMISSION_F, house);
    e.set_target(0, m.whom);
    e.data[5] = m.mission;
    e.set_target(6, m.target);
    e.set_target(11, m.dest);
    e.set_i32(16, m.speed);
    e.set_i32(20, m.max_speed);
    return e;
}

MegaMissionF get_mega_mission_f(const Event& e) {
    MegaMissionF m;
    m.whom = e.target(0);
    m.mission = e.data[5];
    m.target = e.target(6);
    m.dest = e.target(11);
    m.speed = e.i32(16);
    m.max_speed = e.i32(20);
    return m;
}

Event production(EventType t, int house, const Production& p) {
    Event e = make(t, house);
    e.set_i32(0, p.rtti);
    e.set_i32(4, p.heap_id);
    e.set_i32(8, p.is_naval);
    return e;
}

Production get_production(const Event& e) { return Production{e.i32(0), e.i32(4), e.i32(8)}; }

Event place(int house, const Place& p) {
    Event e = make(EventType::PLACE, house);
    e.set_i32(0, p.rtti);
    e.set_i32(4, p.heap_id);
    e.set_i32(8, p.is_naval);
    e.set_cell(12, p.cell);
    return e;
}

Place get_place(const Event& e) {
    Place p;
    p.rtti = e.i32(0);
    p.heap_id = e.i32(4);
    p.is_naval = e.i32(8);
    p.cell = e.cell(12);
    return p;
}

Event special_place(int house, const SpecialPlace& p) {
    Event e = make(EventType::SPECIAL_PLACE, house);
    e.set_i32(0, p.sw_index);
    e.set_cell(4, p.cell);
    return e;
}

SpecialPlace get_special_place(const Event& e) {
    SpecialPlace p;
    p.sw_index = e.i32(0);
    p.cell = e.cell(4);
    return p;
}

Event animation(int house, const Animation& a) {
    Event e = make(EventType::ANIMATION, house);
    e.set_i32(0, a.anim_id);
    e.set_i32(4, a.house_id);
    e.set_i32(8, a.x);
    e.set_i32(12, a.y);
    return e;
}

Animation get_animation(const Event& e) {
    return Animation{e.i32(0), e.i32(4), e.i32(8), e.i32(12)};
}

Event int_event(EventType t, int house, int32_t v) {
    Event e = make(t, house);
    e.set_i32(0, v);
    return e;
}

Event sell_cell(int house, const CellXY& c) {
    Event e = make(EventType::SELLCELL, house);
    e.set_cell(0, c);
    return e;
}

Event two_targets(EventType t, int house, const TargetRef& a, const TargetRef& b) {
    Event e = make(t, house);
    e.set_target(0, a);
    e.set_target(5, b);
    return e;
}

Event frame_info(int house, uint32_t frame, const FrameInfo& f) {
    Event e = make(EventType::FRAMEINFO, house, frame);
    put_u32le(e.data.data(), f.crc);
    e.set_u16(4, f.command_count);
    e.data[6] = f.delay;
    return e;
}

FrameInfo get_frame_info(const Event& e) {
    FrameInfo f;
    f.crc = get_u32le(e.data.data());
    f.command_count = e.u16(4);
    f.delay = e.data[6];
    return f;
}

Event timing(int house, const Timing& t) {
    Event e = make(EventType::TIMING, house);
    e.set_u16(0, t.desired_fps);
    e.set_u16(2, t.max_ahead);
    e.data[4] = t.frame_send_rate;
    return e;
}

Timing get_timing(const Event& e) {
    Timing t;
    t.desired_fps = e.u16(0);
    t.max_ahead = e.u16(2);
    t.frame_send_rate = e.data[4];
    return t;
}

Event process_time(int house, uint16_t avg_ms) {
    Event e = make(EventType::PROCESS_TIME, house);
    e.set_u16(0, avg_ms);
    return e;
}

Event response_time(int house, uint8_t max_ahead) {
    Event e = make(EventType::RESPONSE_TIME, house);
    e.data[6] = max_ahead;
    return e;
}

uint8_t get_response_time(const Event& e) { return e.data[6]; }

Event address_change(int house, const AddressChange& a) {
    Event e = make(EventType::ADDRESSCHANGE, house);
    e.data[0] = static_cast<uint8_t>(a.player);
    put_u32le(e.data.data() + 1, a.addr);
    return e;
}

AddressChange get_address_change(const Event& e) {
    AddressChange a;
    a.player = static_cast<int8_t>(e.data[0]);
    a.addr = get_u32le(e.data.data() + 1);
    return a;
}

Event add_player(int house, const std::vector<uint8_t>& blob) {
    Event e = make(EventType::ADDPLAYER, house);
    e.blob = blob;
    put_u32le(e.data.data() + 4, static_cast<uint32_t>(blob.size()));
    return e;
}

Event response_time2(int house, const ResponseTime2& r) {
    Event e = make(EventType::RESPONSE_TIME2, house);
    e.data[0] = static_cast<uint8_t>(r.max_ahead);
    e.data[1] = r.latency_level;
    return e;
}

ResponseTime2 get_response_time2(const Event& e) {
    return ResponseTime2{static_cast<int8_t>(e.data[0]), e.data[1]};
}

}  // namespace ev
}  // namespace ra2::net
