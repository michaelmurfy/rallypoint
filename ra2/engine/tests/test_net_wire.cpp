// tests/test_net_wire.cpp -- datagram checksum, tunnel headers, the 0xAA04 /
// 0x1236 channel layer, NetManager over loopback and real UDP sockets.
// Golden bytes follow gamemd.exe (docs/re/net-protocol.md).
#include "doctest.h"

#include "net/channel.hpp"
#include "net/net_bytes.hpp"
#include "net/net_manager.hpp"
#include "net/udp_socket.hpp"
#include "net/wire.hpp"

#include <map>
#include <string>
#include <vector>

using namespace ra2::net;

namespace {
std::vector<uint8_t> V(std::initializer_list<int> l) {
    std::vector<uint8_t> v;
    for (int x : l) v.push_back(static_cast<uint8_t>(x));
    return v;
}
struct Capture {
    std::vector<std::pair<PeerAddr, std::vector<uint8_t>>> out;
    ChannelEmit fn() {
        return [this](const PeerAddr& to, const uint8_t* p, size_t n) { out.push_back({to, std::vector<uint8_t>(p, p + n)}); };
    }
};
}  // namespace

TEST_CASE("net wire: Add_CRC 0x64D8C0 and packet CRC 0x7B0DD0") {
    CHECK(add_crc(0, 1) == 1);
    CHECK(add_crc(0x80000000u, 0) == 1);           // rotate-left wraps the top bit
    CHECK(add_crc(0x40000000u, 5) == 0x80000005u);
    auto a = V({1, 0, 0, 0, 2, 0, 0, 0});
    CHECK(packet_crc(a.data(), a.size()) == 4);    // rotl(1)+2
    auto b = V({1, 0, 0, 0, 0xFF});
    CHECK(packet_crc(b.data(), b.size()) == 0x101);  // tail masked to 1 byte
    auto c = V({0xAB, 0xCD, 0xEF});
    CHECK(packet_crc(c.data(), c.size()) == 0x00EFCDABu);
    CHECK(packet_crc(nullptr, 0) == 0);
}

TEST_CASE("net wire: datagram build/check (0x7B0E50 rules)") {
    auto p = V({4, 0xAA, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'x'});
    std::vector<uint8_t> d;
    REQUIRE(build_datagram(p.data(), p.size(), d));
    REQUIRE(d.size() == p.size() + 4);
    CHECK(get_u32le(d.data()) == packet_crc(p.data(), p.size()));
    const uint8_t* q = nullptr;
    size_t n = 0;
    REQUIRE(check_datagram(d.data(), d.size(), &q, &n));
    CHECK(n == p.size());
    CHECK(std::vector<uint8_t>(q, q + n) == p);
    d[6] ^= 1;
    CHECK_FALSE(check_datagram(d.data(), d.size(), &q, &n));
    std::vector<uint8_t> big(4 + 0x280, 0);  // payload length 0x280 rejected
    CHECK_FALSE(check_datagram(big.data(), big.size(), &q, &n));
    std::vector<uint8_t> ok(4 + 0x27F, 0);   // 0x27F accepted (crc of zeros = 0)
    CHECK(check_datagram(ok.data(), ok.size(), &q, &n));
}

TEST_CASE("net wire: tunnel V2 (BE u16) and V3 (LE u32) headers") {
    auto pl = V({0xDE, 0xAD});
    std::vector<uint8_t> o;
    tunnel_wrap(TunnelMode::V2, 0x1234, 0xABCD, pl.data(), pl.size(), o);
    CHECK(o == V({0x12, 0x34, 0xAB, 0xCD, 0xDE, 0xAD}));
    uint32_t f = 0, t = 0;
    const uint8_t* q;
    size_t n;
    REQUIRE(tunnel_unwrap(TunnelMode::V2, o.data(), o.size(), &f, &t, &q, &n, 0xABCD));
    CHECK(f == 0x1234);
    CHECK(n == 2);
    CHECK_FALSE(tunnel_unwrap(TunnelMode::V2, o.data(), o.size(), &f, &t, &q, &n, 0x1111));
    CHECK_FALSE(tunnel_unwrap(TunnelMode::V2, o.data(), 4, &f, &t, &q, &n));  // spawner: >= 5 bytes
    tunnel_wrap(TunnelMode::V3, 0x11223344, 0x55667788, pl.data(), pl.size(), o);
    CHECK(o == V({0x44, 0x33, 0x22, 0x11, 0x88, 0x77, 0x66, 0x55, 0xDE, 0xAD}));
    REQUIRE(tunnel_unwrap(TunnelMode::V3, o.data(), o.size(), &f, &t, &q, &n));
    CHECK(f == 0x11223344u);
    CHECK(t == 0x55667788u);
    auto ping = make_tunnel_ping(0xCAFEu);
    CHECK(ping.size() == 50);
    CHECK(is_tunnel_ping_reply(ping.data(), 12, 0xCAFEu));
}

TEST_CASE("net channel: private header golden bytes and PacketID counters") {
    PeerAddr peer{NetAddr::loopback(1234), 0};
    PrivateChannel ch(1, peer);
    ch.init();
    auto hi = V({'h', 'i'});
    REQUIRE(ch.send(hi.data(), 2, true));
    REQUIRE(ch.send(hi.data(), 2, true));
    REQUIRE(ch.send(hi.data(), 2, false));
    Capture cap;
    REQUIRE(ch.service(1000, cap.fn()));
    REQUIRE(cap.out.size() == 3);
    CHECK(cap.out[0].second == V({0x04, 0xAA, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'h', 'i'}));
    CHECK(cap.out[1].second == V({0x04, 0xAA, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'h', 'i'}));
    CHECK(cap.out[2].second == V({0x04, 0xAA, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 'h', 'i'}));  // NOACK id 0
    CHECK(cap.out[0].first == peer);
}

TEST_CASE("net channel: retransmit after RetryDelta, NOACK sent once, Timeout marks bad") {
    PrivateChannel ch(1, PeerAddr{NetAddr::loopback(1), 0});
    ch.init();
    ch.set_timing({2, 0xFFFFFFFFu, 60});
    auto x = V({1});
    ch.send(x.data(), 1, true);
    ch.send(x.data(), 1, false);
    Capture c;
    ch.service(100, c.fn());
    CHECK(c.out.size() == 2);
    ch.service(102, c.fn());  // 102-100 = 2, not > 2
    CHECK(c.out.size() == 2);
    ch.service(103, c.fn());  // resend the DATA_ACK only
    CHECK(c.out.size() == 3);
    CHECK(c.out[2].second[2] == kCodeDataAck);
    CHECK(ch.stats().resends == 1);
    CHECK(ch.send_pending() == 1);  // NOACK retired
    bool ok = true;
    for (uint32_t t = 104; t < 200 && ok; ++t) ok = ch.service(t, c.fn());
    CHECK_FALSE(ok);  // last - first > 60
}

TEST_CASE("net channel: DATA_ACK receive always ACKs, dedupes, advances LastSeqID") {
    PeerAddr peer{NetAddr::loopback(9), 0};
    PrivateChannel ch(2, peer);
    ch.init();
    Capture c;
    auto mk = [](uint8_t code, uint32_t id, uint8_t b) {
        std::vector<uint8_t> p(15, 0);
        put_u16le(p.data(), 0xAA04);
        p[2] = code;
        put_u32le(p.data() + 4, id);
        p[14] = b;
        return p;
    };
    auto p1 = mk(0, 1, 11), p0 = mk(0, 0, 10);
    CHECK(ch.receive(p1.data(), p1.size(), c.fn()));
    CHECK(ch.last_seq_id() == 0xFFFFFFFFu);  // out of order: not advanced (hw stays -1? id>hw)
    CHECK(ch.receive(p1.data(), p1.size(), c.fn()));  // duplicate: ACKed again, not queued
    CHECK(ch.receive(p0.data(), p0.size(), c.fn()));
    CHECK(ch.last_seq_id() == 1);  // 0 arrived, 1 still queued -> hw 1
    REQUIRE(c.out.size() == 3);
    CHECK(c.out[0].second == V({0x04, 0xAA, 2, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0}));
    CHECK(ch.received_pending() == 2);
    // Get_Packet 0x48C230 delivers DATA_ACK packets in PacketID order.
    ReceivedPacket r;
    REQUIRE(ch.pop_received(r));
    CHECK(r.payload == V({10}));
    REQUIRE(ch.pop_received(r));
    CHECK(r.payload == V({11}));
    CHECK_FALSE(ch.pop_received(r));
    // read packets stay queued until Service_Receive_Queue retires them
    // (NOACK, or id < LastSeqID): id 0 goes, id 1 (== LastSeqID) stays.
    ch.service(1, c.fn());
    CHECK(ch.receive_queue_size() == 1);
    // a retransmitted id 1 is still recognised: ACKed, not re-delivered
    CHECK(ch.receive(p1.data(), p1.size(), c.fn()));
    CHECK_FALSE(ch.pop_received(r));
    // bad magic
    auto bad = p0;
    bad[0] = 3;
    CHECK_FALSE(ch.receive(bad.data(), bad.size(), c.fn()));
    CHECK(ch.stats().bad_magic == 1);
    // ACK for our own packet retires it
    auto x = V({5});
    ch.send(x.data(), 1, true);
    ch.service(10, c.fn());
    auto ack = mk(2, 0, 0);
    ack.resize(14);
    ch.receive(ack.data(), ack.size(), c.fn());
    ch.service(11, c.fn());
    CHECK(ch.send_pending() == 0);
    CHECK(ch.stats().rtt_count == 1);
}

TEST_CASE("net channel: NOACK keeps two queue slots free; DATA_ACK refused without ACK") {
    PrivateChannel ch(2, PeerAddr{NetAddr::loopback(9), 0});
    ch.init();
    Capture c;
    std::vector<uint8_t> p(14, 0);
    put_u16le(p.data(), 0xAA04);
    p[2] = 1;
    int accepted = 0;
    for (uint32_t i = 0; i < 40; ++i) {
        put_u32le(p.data() + 4, i);
        if (ch.receive(p.data(), p.size(), c.fn())) ++accepted;
    }
    CHECK(accepted == kPrivateQueueSize - 1);  // stops when free slots < 2
    p[2] = 0;
    put_u32le(p.data() + 4, 5);  // not hw+1 (hw = -1 -> next is 0)
    CHECK_FALSE(ch.receive(p.data(), p.size(), c.fn()));
    CHECK(c.out.empty());
    put_u32le(p.data() + 4, 0);  // the in-order one still gets in
    CHECK(ch.receive(p.data(), p.size(), c.fn()));
    CHECK(c.out.size() == 1);
}

TEST_CASE("net channel: global 0x1236 header, broadcast NOACK, (addr,id) dedupe") {
    GlobalChannel g;
    g.init();
    PeerAddr a{NetAddr::loopback(1), 0}, bc{NetAddr::broadcast(1234), 0};
    auto m = V({0x0C, 0, 0, 0});
    g.send(m.data(), 4, true, &a, bc);
    g.send(m.data(), 4, true, nullptr, bc);
    Capture c;
    g.service(50, c.fn());
    REQUIRE(c.out.size() == 2);
    CHECK(c.out[0].second == V({0x36, 0x12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x04, 0xAA, 0x0C, 0, 0, 0}));
    CHECK(c.out[1].second[2] == kCodeDataNoAck);
    CHECK(get_u32le(c.out[1].second.data() + 4) == 1);
    CHECK(c.out[1].first == bc);

    GlobalChannel r;
    r.init();
    Capture rc;
    auto pkt = c.out[0].second;
    CHECK(r.receive(pkt.data(), pkt.size(), a, rc.fn()));
    CHECK(r.receive(pkt.data(), pkt.size(), a, rc.fn()));   // dup from same addr
    PeerAddr b2{NetAddr::loopback(2), 0};
    CHECK(r.receive(pkt.data(), pkt.size(), b2, rc.fn()));  // same id, other addr: new
    CHECK(r.received_pending() == 2);
    REQUIRE(rc.out.size() == 3);
    CHECK(rc.out[0].second == V({0x36, 0x12, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x04, 0xAA}));
    ReceivedPacket rp;
    REQUIRE(r.pop_received(rp));
    CHECK(rp.product == 0xAA04);
    CHECK(rp.payload == m);
}

TEST_CASE("net manager: reliable delivery over a lossy loopback hub") {
    uint32_t now = 1000000;
    LoopbackHub hub([&] { return now; }, 7);
    hub.set_loss(0.3);
    hub.set_duplicate(0.1);
    hub.set_latency(0, 40);
    PeerAddr pa{NetAddr::loopback(1001), 0}, pb{NetAddr::loopback(1002), 0};
    NetManager A(*hub.create(pa), [&] { return now; });
    NetManager B(*hub.create(pb), [&] { return now; });
    A.create_connection(1, pb);
    B.create_connection(0, pa);
    A.set_timing({2, 0xFFFFFFFFu, 100000});
    std::vector<uint8_t> got;
    int sent = 0;
    for (int step = 0; step < 4000; ++step) {
        if (sent < 200 && A.connection(1)->send_pending() < 20) {
            uint8_t v = static_cast<uint8_t>(sent);
            if (A.send_private(&v, 1, true, 1)) ++sent;
        }
        A.service();
        B.service();
        std::vector<uint8_t> p;
        int id;
        while (B.get_private(p, id)) got.push_back(p[0]);
        now += 16;
    }
    CHECK(sent == 200);
    std::map<int, int> seen;
    for (auto v : got) seen[v]++;
    CHECK(seen.size() == 200);
    REQUIRE(got.size() == 200);  // exactly once and in order (Get_Packet 0x48C230)
    for (int i = 0; i < 200; ++i) CHECK(got[static_cast<size_t>(i)] == i);
    CHECK(A.crc_errors() == 0);
}

TEST_CASE("net manager: corrupt datagrams counted as CRC errors") {
    uint32_t now = 5000;
    struct Raw : DatagramIO {
        std::vector<std::vector<uint8_t>> in;
        bool send(const PeerAddr&, const uint8_t*, size_t) override { return true; }
        bool recv(PeerAddr& f, std::vector<uint8_t>& d) override {
            if (in.empty()) return false;
            f = PeerAddr{};
            d = in.back();
            in.pop_back();
            return true;
        }
    } raw;
    raw.in.push_back(V({1, 2, 3, 4, 5, 6}));
    NetManager m(raw, [&] { return now; });
    m.service();
    CHECK(m.crc_errors() == 1);
}

TEST_CASE("net udp: real sockets on 127.0.0.1, direct and through a V2 tunnel relay") {
    UdpDatagramIO ioA, ioB;
    REQUIRE(ioA.open(0, 0x7F000001u));
    REQUIRE(ioB.open(0, 0x7F000001u));
    const uint16_t portA = ioA.socket().local_port(), portB = ioB.socket().local_port();
    REQUIRE(portA != 0);
    PeerAddr pa{NetAddr::loopback(portA), 0}, pb{NetAddr::loopback(portB), 0};
    uint32_t now = 100000;
    NetManager A(ioA, [&] { return now; });
    NetManager B(ioB, [&] { return now; });
    A.create_connection(1, pb);
    B.create_connection(0, pa);
    auto msg = V({'h', 'e', 'l', 'l', 'o'});
    A.send_private(msg.data(), msg.size(), true, 1);
    std::vector<uint8_t> got;
    int id = -1;
    for (int i = 0; i < 400 && got.empty(); ++i) {
        A.service();
        B.service();
        B.get_private(got, id);
        now += 16;
    }
    CHECK(got == msg);
    CHECK(id == 0);

    // V2 tunnel: a tiny relay keyed by the 16-bit ids (cncnet-server TunnelV2).
    UdpSocket relay;
    REQUIRE(relay.open(0, 0x7F000001u));
    const NetAddr raddr = NetAddr::loopback(relay.local_port());
    UdpDatagramIO tA, tB;
    REQUIRE(tA.open(0, 0x7F000001u));
    REQUIRE(tB.open(0, 0x7F000001u));
    tA.set_tunnel(TunnelMode::V2, raddr, 0x0101);
    tB.set_tunnel(TunnelMode::V2, raddr, 0x0202);
    std::map<uint16_t, NetAddr> bind;
    NetManager TA(tA, [&] { return now; });
    NetManager TB(tB, [&] { return now; });
    TA.create_connection(1, PeerAddr{raddr, 0x0202});
    TB.create_connection(0, PeerAddr{raddr, 0x0101});
    TA.send_private(msg.data(), msg.size(), true, 1);
    TB.send_private(msg.data(), 1, true, 0);  // binds B at the relay
    got.clear();
    for (int i = 0; i < 400 && got.size() != msg.size(); ++i) {
        TA.service();
        uint8_t buf[2048];
        NetAddr src;
        int n;
        while ((n = relay.recv_from(src, buf, sizeof buf)) >= 5) {
            const uint16_t from = get_u16be(buf), to = get_u16be(buf + 2);
            bind[from] = src;
            auto it = bind.find(to);
            if (it != bind.end()) relay.send_to(it->second, buf, static_cast<size_t>(n));
        }
        TB.service();
        std::vector<uint8_t> p;
        if (TB.get_private(p, id)) got = p;
        now += 16;
    }
    CHECK(got == msg);
}
