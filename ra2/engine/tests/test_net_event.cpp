// tests/test_net_event.cpp -- EventClass layouts, the compressed packet codec
// (Add_Compressed_Events 0x64B0D0 / Extract_Compressed_Events 0x64B840 with the
// megamission unit-list caches), CommProtocol 0, and the command mapping.
#include "doctest.h"

#include "net/event.hpp"
#include "net/event_codec.hpp"
#include "net/event_map.hpp"
#include "net/net_bytes.hpp"

#include <deque>
#include <string>
#include <vector>

using namespace ra2::net;

namespace {
std::vector<uint8_t> V(std::initializer_list<int> l) {
    std::vector<uint8_t> v;
    for (int x : l) v.push_back(static_cast<uint8_t>(x));
    return v;
}

struct Packet {
    std::array<uint8_t, kMaxGamePacket + 8> buf{};
    size_t len = 0;
};

// One Send_Packets-style packet: header + compressed records.
Packet build(EventEncoder& enc, std::deque<Event>& out, int8_t house, uint32_t now, uint32_t ma,
             uint32_t fsr, uint32_t crc, uint16_t cmds, std::vector<Event>& echo, int& n) {
    Packet p;
    FrameHeader h;
    h.house = house;
    h.frame = stamp_frame(now, ma, fsr, 2);
    h.crc = crc;
    h.command_count = cmds;
    h.delay = static_cast<uint8_t>(ma);
    h.write(p.buf.data());
    p.len = enc.add_compressed(out, p.buf.data(), kMaxGamePacket, kFrameHeaderSize, 1000, h.frame,
                               house, now, echo, 0x4000, n);
    return p;
}

ev::MegaMission mm(uint32_t whom) {
    ev::MegaMission m;
    m.whom = TargetRef{whom, 0x0F};
    m.mission = 5;
    m.dest = TargetRef{0x123456, 0x2A};
    return m;
}
}  // namespace

TEST_CASE("net event: size table 0x81F8FC and names 0x81F92C") {
    CHECK(kEventPayloadSize[0x04] == 23);
    CHECK(kEventPayloadSize[0x05] == 24);
    CHECK(kEventPayloadSize[0x0B] == 16);
    CHECK(kEventPayloadSize[0x1B] == 1);
    CHECK(kEventPayloadSize[0x1C] == 7);
    CHECK(kEventPayloadSize[0x25] == 0x68);
    CHECK(kEventPayloadSize[0x26] == 0x40);
    CHECK(kEventPayloadSize[0x2E] == 12);
    CHECK(std::string(event_type_name(0x00)) == "EMPTY");
    CHECK(std::string(event_type_name(0x12)) == "SPECIAL_PLACE");
    CHECK(std::string(event_type_name(0x2E)) == "ABANDON_ALL");
    CHECK(event_wire_size(0x30, false) == -1);
    CHECK(event_wire_size(0x30, true) == 2);
    CHECK(event_wire_size(0x2F, true) == -1);
}

TEST_CASE("net event: 111-byte raw layout and typed payload offsets") {
    ev::Place pl;
    pl.rtti = 6;
    pl.heap_id = 0x1234;
    pl.is_naval = 1;
    pl.cell = CellXY{-3, 77};
    Event e = ev::place(2, pl);
    e.frame = 0xAABBCCDD;
    uint8_t raw[kEventClassSize];
    e.to_raw(raw);
    CHECK(raw[0] == 0x0B);
    CHECK(raw[2] == 2);
    CHECK(get_u32le(raw + 3) == 0xAABBCCDDu);
    CHECK(get_u32le(raw + 7) == 6);
    CHECK(get_u32le(raw + 11) == 0x1234);
    CHECK(get_u32le(raw + 15) == 1);
    CHECK(get_i16le(raw + 19) == -3);
    CHECK(get_i16le(raw + 21) == 77);
    Event back = Event::from_raw(raw);
    CHECK(back.wire_equal(e));
    // RESPONSE_TIME's byte lives at event +0x0D (data+6)
    Event rt = ev::response_time(0, 42);
    rt.to_raw(raw);
    CHECK(raw[0x0D] == 42);
    // MEGAMISSION layout
    Event m = ev::mega_mission(1, mm(7));
    CHECK(m.data[5] == 5);
    CHECK(get_u32le(&m.data[12]) == 0x123456);
    CHECK(m.data[16] == 0x2A);
}

TEST_CASE("net codec: golden FRAMEINFO + PRODUCE packet") {
    EventEncoder enc;
    std::deque<Event> out;
    out.push_back(ev::production(EventType::PRODUCE, 9, ev::Production{0x0F, 3, 0}));
    std::vector<Event> echo;
    int n = 0;
    Packet p = build(enc, out, 1, 100, 10, 5, 0x11223344, 7, echo, n);
    CHECK(n == 1);
    CHECK(out.empty());
    std::vector<uint8_t> got(p.buf.begin(), p.buf.begin() + static_cast<long>(p.len));
    CHECK(got == V({0x1C, 0x00, 0x01, 0x6E, 0, 0, 0, 0x44, 0x33, 0x22, 0x11, 0x07, 0x00, 0x0A,
                    0x0E, 0x0F, 0, 0, 0, 0x03, 0, 0, 0, 0, 0, 0, 0}));
    REQUIRE(echo.size() == 1);
    CHECK(echo[0].frame == 110);  // ((5+100-1+10)/5)*5
    CHECK(echo[0].house == 1);    // stamped with the sender's house

    EventDecoder dec;
    std::vector<Event> evs;
    CHECK(dec.extract(p.buf.data(), p.len, evs) == 2);
    REQUIRE(evs.size() == 2);
    CHECK(evs[0].type == 0x1C);
    CHECK(ev::get_frame_info(evs[0]).crc == 0x11223344u);
    CHECK(ev::get_frame_info(evs[0]).command_count == 7);
    CHECK(ev::get_frame_info(evs[0]).delay == 10);
    CHECK(evs[1].wire_equal(echo[0]));
}

TEST_CASE("net codec: stamp_frame rounding (CommProtocol 2) vs plain") {
    CHECK(stamp_frame(100, 6, 2, 2) == 106);
    CHECK(stamp_frame(101, 10, 5, 2) == 115);
    CHECK(stamp_frame(101, 10, 5, 1) == 111);
    CHECK(stamp_frame(0, 10, 5, 2) == 10);
}

TEST_CASE("net codec: MEGAMISSION run-length + unit-list cache (golden, both directions)") {
    EventEncoder enc;
    EventDecoder dec;
    std::deque<Event> out;
    for (uint32_t w : {0x10u, 0x11u, 0x12u}) out.push_back(ev::mega_mission(0, mm(w)));
    std::vector<Event> echo;
    int n = 0;
    Packet p1 = build(enc, out, 0, 100, 10, 5, 0, 0, echo, n);
    CHECK(n == 3);
    std::vector<uint8_t> body(p1.buf.begin() + 14, p1.buf.begin() + static_cast<long>(p1.len));
    // [04][count 3][23-byte payload, slot 0][whom2][whom3]
    CHECK(body == V({0x04, 0x03, 0x10, 0, 0, 0, 0x0F, 0x05, 0x00, 0, 0, 0, 0, 0, 0x56, 0x34, 0x12,
                     0x00, 0x2A, 0, 0, 0, 0, 0, 0x00, 0x11, 0, 0, 0, 0x0F, 0x12, 0, 0, 0, 0x0F}));
    CHECK(enc.slot(0).count == 3);
    CHECK(enc.slot(0).saved_frame == 100);

    // Same group again: the cache hit sends count 0 + slot, no list.
    for (uint32_t w : {0x10u, 0x11u, 0x12u}) out.push_back(ev::mega_mission(0, mm(w)));
    Packet p2 = build(enc, out, 0, 105, 10, 5, 0, 3, echo, n);
    std::vector<uint8_t> body2(p2.buf.begin() + 14, p2.buf.begin() + static_cast<long>(p2.len));
    CHECK(body2.size() == 25);
    CHECK(body2[0] == 0x04);
    CHECK(body2[1] == 0x00);
    CHECK(body2[2 + 6] == 0);  // slot

    std::vector<Event> evs;
    CHECK(dec.extract(p1.buf.data(), p1.len, evs) == 4);
    CHECK(dec.cached_count(0, 0) == 3);
    CHECK(dec.extract(p2.buf.data(), p2.len, evs) == 4);
    REQUIRE(evs.size() == 8);
    const uint32_t want[] = {0x10, 0x11, 0x12};
    for (int k = 0; k < 3; ++k) {
        CHECK(ev::get_mega_mission(evs[1 + k]).whom.id == want[k]);
        CHECK(ev::get_mega_mission(evs[5 + k]).whom.id == want[k]);
        CHECK(evs[5 + k].frame == stamp_frame(105, 10, 5, 2));
    }
    // The receiver cache is per sender house: replaying slot 0 for house 2 is
    // an empty-cache replay (malformed) and stops the walk.
    Packet p3 = p2;
    p3.buf[2] = 2;
    std::vector<Event> e3;
    CHECK(dec.extract(p3.buf.data(), p3.len, e3) == 1);
}

TEST_CASE("net codec: early-repeat search splits a run that matches a cached list") {
    EventEncoder enc;
    std::deque<Event> out;
    std::vector<Event> echo;
    int n;
    for (uint32_t w : {0x10u, 0x11u}) out.push_back(ev::mega_mission(0, mm(w)));
    build(enc, out, 0, 100, 10, 5, 0, 0, echo, n);  // caches [0x10,0x11] in slot 0
    for (uint32_t w : {0x10u, 0x11u, 0x12u}) out.push_back(ev::mega_mission(0, mm(w)));
    Packet p = build(enc, out, 0, 105, 10, 5, 0, 0, echo, n);
    std::vector<uint8_t> b(p.buf.begin() + 14, p.buf.begin() + static_cast<long>(p.len));
    REQUIRE(b.size() == 50);
    CHECK(b[0] == 0x04);
    CHECK(b[1] == 0x00);  // replay of slot 0 = [0x10, 0x11]
    CHECK(b[25] == 0x04);
    CHECK(b[26] == 0x01);  // 0x12 alone
    CHECK(b[27] == 0x12);
    EventDecoder dec;
    std::vector<Event> evs;
    // decoder needs the cache from the first packet: rebuild it
    std::deque<Event> o2;
    EventEncoder enc2;
    for (uint32_t w : {0x10u, 0x11u}) o2.push_back(ev::mega_mission(0, mm(w)));
    Packet pp = build(enc2, o2, 0, 100, 10, 5, 0, 0, echo, n);
    dec.extract(pp.buf.data(), pp.len, evs);
    evs.clear();
    CHECK(dec.extract(p.buf.data(), p.len, evs) == 4);
}

TEST_CASE("net codec: run membership ignores Follow / IsPlanning (quirk)") {
    EventEncoder enc;
    std::deque<Event> out;
    auto a = mm(1), b = mm(2);
    b.follow = TargetRef{99, 0x0F};
    out.push_back(ev::mega_mission(0, a));
    out.push_back(ev::mega_mission(0, b));
    std::vector<Event> echo;
    int n;
    Packet p = build(enc, out, 0, 100, 10, 5, 0, 0, echo, n);
    CHECK(p.buf[15] == 2);  // one run of 2
    EventDecoder dec;
    std::vector<Event> evs;
    dec.extract(p.buf.data(), p.len, evs);
    REQUIRE(evs.size() == 3);
    CHECK(ev::get_mega_mission(evs[2]).follow.id == 0);  // b's Follow is not transmitted
}

TEST_CASE("net codec: RESPONSE_TIME, ADDPLAYER blob, FRAMESYNC skip, ResponseTime2") {
    EventEncoder enc(true);
    std::deque<Event> out;
    out.push_back(ev::response_time(0, 33));
    out.push_back(ev::add_player(0, V({1, 2, 3, 4, 5})));
    out.push_back(ev::response_time2(0, ev::ResponseTime2{7, 3}));
    std::vector<Event> echo;
    int n;
    Packet p = build(enc, out, 3, 50, 10, 5, 0, 0, echo, n);
    std::vector<uint8_t> b(p.buf.begin() + 14, p.buf.begin() + static_cast<long>(p.len));
    CHECK(b == V({0x1B, 33, 0x1F, 5, 0, 0, 0, 1, 2, 3, 4, 5, 0x30, 7, 3}));
    EventDecoder dec(true);
    std::vector<Event> evs;
    CHECK(dec.extract(p.buf.data(), p.len, evs) == 4);
    CHECK(ev::get_response_time(evs[1]) == 33);
    CHECK(evs[2].blob == V({1, 2, 3, 4, 5}));
    CHECK(ev::get_response_time2(evs[3]).latency_level == 3);
    // a vanilla decoder cannot decode 0x30 and stops there
    EventDecoder van(false);
    std::vector<Event> e2;
    CHECK(van.extract(p.buf.data(), p.len, e2) == 3);
    // FRAMESYNC-only packet: 14 bytes, nothing decoded
    FrameHeader fs;
    fs.type = 0x19;
    uint8_t f[14];
    fs.write(f);
    std::vector<Event> e3;
    CHECK(dec.extract(f, 14, e3) == 0);
}

TEST_CASE("net codec: every event type round-trips (compressed and CommProtocol 0)") {
    for (int t = 0; t <= 0x30; ++t) {
        if (t == 0x19 || t == 0x1C || t == 0x2F) continue;  // packet-level / no type
        const int ws = event_wire_size(static_cast<uint8_t>(t), true);
        if (ws < 0) continue;
        CAPTURE(t);
        Event e = ev::make(static_cast<EventType>(t), 1);
        if (t == 0x1F) {
            e = ev::add_player(1, V({9, 8, 7}));
        } else {
            const size_t off = (t == 0x1B) ? 6 : 0;
            for (int k = 0; k < ws; ++k) e.data[off + static_cast<size_t>(k)] = static_cast<uint8_t>(t * 31 + k * 7 + 1);
            if (t == 0x04) e.data[6] = 0;  // count==1 path keeps whatever slot; keep it valid
        }
        // compressed
        EventEncoder enc(true);
        EventDecoder dec(true);
        std::deque<Event> out{e};
        std::vector<Event> echo;
        int n;
        Packet p = build(enc, out, 1, 200, 10, 5, 0, 0, echo, n);
        REQUIRE(n == 1);
        std::vector<Event> evs;
        REQUIRE(dec.extract(p.buf.data(), p.len, evs) == 2);
        CHECK(evs[1].wire_equal(echo[0]));
        // raw
        std::deque<Event> out2{e};
        std::vector<Event> echo2;
        std::array<uint8_t, 600> raw{};
        size_t len = encode_raw_events(out2, raw.data(), kMaxGamePacket, kEventClassSize, 10, 210, 1, echo2, 100, n);
        REQUIRE(n == 1);
        Event fi = ev::frame_info(1, 210, ev::FrameInfo{});
        fi.to_raw(raw.data());
        std::vector<Event> evs2;
        REQUIRE(decode_raw_events(raw.data(), len, evs2) == 2);
        CHECK(evs2[1].wire_equal(echo2[0]));
    }
}

TEST_CASE("net codec: a full buffer stops the encoder; the rest goes next packet") {
    EventEncoder enc;
    std::deque<Event> out;
    for (int i = 0; i < 60; ++i) out.push_back(ev::production(EventType::PRODUCE, 0, ev::Production{0x0F, i, 0}));
    std::vector<Event> echo;
    int n;
    Packet p = build(enc, out, 0, 10, 10, 5, 0, 0, echo, n);
    CHECK(n == (558 - 14) / 13);
    CHECK(p.len <= kMaxGamePacket);
    CHECK(out.size() == 60u - static_cast<size_t>(n));
    CHECK(echo.size() == static_cast<size_t>(n));
}

TEST_CASE("net event_map: command <-> event field mapping") {
    NetCommand c;
    c.kind = EventType::MEGAMISSION;
    c.house = 2;
    c.frame = 55;
    c.whom = TargetRef{5, 0x0F};
    c.mission = 3;
    c.dest = TargetRef{77, 0x2A};
    c.follow = TargetRef{6, 0x0F};
    Event e;
    REQUIRE(command_to_event(c, e));
    NetCommand back;
    REQUIRE(event_to_command(e, back));
    CHECK(back.whom == c.whom);
    CHECK(back.dest == c.dest);
    CHECK(back.follow == c.follow);
    CHECK(back.mission == 3);
    CHECK(back.frame == 55);
    c = NetCommand{};
    c.kind = EventType::PLACE;
    c.house = 0;
    c.rtti = 6;
    c.heap_id = 12;
    c.cell = CellXY{10, 20};
    REQUIRE(command_to_event(c, e));
    CHECK(ev::get_place(e).cell == CellXY{10, 20});
    c.kind = EventType::TIMING;
    CHECK_FALSE(command_to_event(c, e));
    CHECK_FALSE(is_sim_event(EventType::FRAMEINFO));
    CHECK(is_sim_event(EventType::REMOVEPLAYER));
}
