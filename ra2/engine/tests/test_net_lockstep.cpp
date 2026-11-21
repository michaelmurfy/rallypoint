// tests/test_net_lockstep.cpp -- the scheduler with in-process peers over a
// loopback hub and the deterministic StubSim (engine/net/stub_sim.hpp).
#include "doctest.h"

#include "net/event_codec.hpp"
#include "net/lockstep.hpp"
#include "net/net_bytes.hpp"
#include "net/net_manager.hpp"
#include "net/stub_sim.hpp"

#include <memory>
#include <vector>

using namespace ra2::net;

namespace {

struct Peer {
    int house;
    std::unique_ptr<NetManager> net;
    std::unique_ptr<LockstepSession> s;
    StubSim sim;
    StubInput in;
    bool in_frame = false;
    FrameStatus last = FrameStatus::Proceed;
    std::vector<uint32_t> crcs;  // crc per frame
    std::vector<int> removed;
    uint32_t crc_bias = 0;       // to force a desync
    uint32_t bias_from = 0xFFFFFFFFu;
    Peer(int h) : house(h), in(0x9E3779B9u * static_cast<uint32_t>(h + 1)) {}
};

struct World {
    uint32_t now = 10000000;
    LoopbackHub hub{[this] { return now; }, 1234};
    std::vector<std::unique_ptr<Peer>> peers;

    void make(int n, LockstepConfig base) {
        std::vector<int> houses;
        for (int i = 0; i < n; ++i) houses.push_back(i);
        for (int i = 0; i < n; ++i) {
            auto p = std::make_unique<Peer>(i);
            PeerAddr me{NetAddr::loopback(static_cast<uint16_t>(2000 + i)), 0};
            p->net = std::make_unique<NetManager>(*hub.create(me), [this] { return now; });
            for (int j = 0; j < n; ++j)
                if (j != i) p->net->create_connection(j, PeerAddr{NetAddr::loopback(static_cast<uint16_t>(2000 + j)), 0});
            LockstepConfig c = base;
            c.local_house = i;
            c.houses = houses;
            c.master = (i == 0);
            c.master_house = 0;   // W9-05-12: TIMING from house 0 only
            p->s = std::make_unique<LockstepSession>(*p->net, c);
            Peer* raw = p.get();
            p->s->on_player_removed = [raw](int h, int) { raw->removed.push_back(h); };
            peers.push_back(std::move(p));
        }
    }

    // One scheduler pass for every peer; the clock advances 16 ms per pass.
    void step(bool commands = true) {
        for (auto& pp : peers) {
            Peer& p = *pp;
            if (p.last != FrameStatus::Proceed && p.last != FrameStatus::Stall) continue;
            if (!p.in_frame) {
                p.sim.logic_step(p.s->frame());
                Event e;
                if (commands && p.in.maybe(p.house, e)) p.s->queue_local(e);
                uint32_t crc = p.sim.crc();
                if (p.s->frame() >= p.bias_from) crc += p.crc_bias;
                p.crcs.push_back(crc);
                p.s->begin_frame(crc);
                p.in_frame = true;
            }
            p.last = p.s->poll();
            if (p.last == FrameStatus::Proceed) {
                for (const auto& x : p.s->executed()) p.sim.apply(x);
                p.s->end_frame();
                p.in_frame = false;
            }
        }
        now += 16;
    }
    bool run_until_frame(uint32_t f, int max_steps, bool commands = true) {
        for (int i = 0; i < max_steps; ++i) {
            bool all = true;
            for (auto& p : peers)
                if (p->s->frame() < f && (p->last == FrameStatus::Proceed || p->last == FrameStatus::Stall)) all = false;
            if (all) return true;
            step(commands);
        }
        return false;
    }
};

LockstepConfig lan() {
    LockstepConfig c;
    c.game_mode = GameMode::LAN;
    c.protocol = 2;
    c.max_ahead = 10;
    c.frame_send_rate = 5;
    return c;
}

}  // namespace

TEST_CASE("net lockstep: timeouts table 0x836338") {
    CHECK(wait_timeouts(GameMode::LAN).resend_init == 60);
    CHECK(wait_timeouts(GameMode::LAN).total == 2400);
    CHECK(wait_timeouts(GameMode::Internet).dialog_init == 1800);
    CHECK(wait_timeouts(GameMode::Internet).dialog_frame == 720);
    CHECK(wait_timeouts(GameMode::Serial).total == 3600);
}

TEST_CASE("net lockstep: two peers stay in sync for 3000 frames (clean link)") {
    // W9-05-14: was 700 frames, too short to reach the megamission cache-slot
    // artefact the StubSim hashed at ~2,550 (ra2netpeer --selftest).
    World w;
    w.make(2, lan());
    REQUIRE(w.run_until_frame(3000, 90000));
    auto& a = *w.peers[0];
    auto& b = *w.peers[1];
    CHECK(a.last == FrameStatus::Proceed);
    CHECK(b.last == FrameStatus::Proceed);
    const size_t n = std::min(a.crcs.size(), b.crcs.size());
    REQUIRE(n >= 3000);
    for (size_t i = 0; i < n; ++i) REQUIRE(a.crcs[i] == b.crcs[i]);
    CHECK(a.sim.executed > 50);
    CHECK(a.s->commands_sent() > 4);
    // Master TIMING after PROCESS_TIME: both adopt the same MaxAhead / FSR.
    CHECK(a.s->max_ahead() == b.s->max_ahead());
    CHECK(a.s->frame_send_rate() == b.s->frame_send_rate());
    CHECK(a.s->max_ahead() == 15);  // 3*FSR floor on a fast link
}

TEST_CASE("net lockstep: three peers over a lossy, jittery link stay in sync") {
    World w;
    w.hub.set_loss(0.15);
    w.hub.set_duplicate(0.05);
    w.hub.set_latency(5, 60);
    w.make(3, lan());
    REQUIRE(w.run_until_frame(600, 60000));
    for (int k = 1; k < 3; ++k) {
        const size_t n = std::min(w.peers[0]->crcs.size(), w.peers[k]->crcs.size());
        for (size_t i = 0; i < n; ++i) {
            if (w.peers[0]->crcs[i] != w.peers[k]->crcs[i])
                MESSAGE("diverge i=" << i << " k=" << k << " late0=" << w.peers[0]->s->stats().late_events
                        << " latek=" << w.peers[k]->s->stats().late_events << " moved0="
                        << w.peers[0]->s->stats().moved_events << " movedk=" << w.peers[k]->s->stats().moved_events
                        << " ma=" << w.peers[0]->s->max_ahead() << "/" << w.peers[k]->s->max_ahead());
            REQUIRE(w.peers[0]->crcs[i] == w.peers[k]->crcs[i]);
        }
        CHECK(w.peers[k]->last != FrameStatus::OutOfSync);
    }
    CHECK(w.peers[0]->s->stats().late_events == 0);
}

TEST_CASE("net lockstep: a diverging CRC is detected (after the 256-frame grace)") {
    World w;
    w.make(2, lan());
    w.peers[1]->crc_bias = 1;
    w.peers[1]->bias_from = 300;
    w.run_until_frame(400, 20000);
    const bool a_oos = w.peers[0]->last == FrameStatus::OutOfSync;
    const bool b_oos = w.peers[1]->last == FrameStatus::OutOfSync;
    CHECK((a_oos || b_oos));
    const auto& info = a_oos ? w.peers[0]->s->out_of_sync() : w.peers[1]->s->out_of_sync();
    CHECK(info.frame >= 300);
    CHECK(info.local_crc != info.remote_crc);
}

TEST_CASE("net lockstep: different frame-0 CRC -> scenarios do not match") {
    World w;
    w.make(2, lan());
    w.peers[1]->crc_bias = 5;
    w.peers[1]->bias_from = 0;
    for (int i = 0; i < 200; ++i) w.step();
    CHECK(w.peers[0]->last == FrameStatus::ScenarioMismatch);
    CHECK(w.peers[1]->last == FrameStatus::ScenarioMismatch);
}

TEST_CASE("net lockstep: a dropped peer is kicked after the total timeout -> REMOVEPLAYER") {
    World w;
    w.make(3, lan());
    REQUIRE(w.run_until_frame(100, 20000));
    PeerAddr c{NetAddr::loopback(2002), 0};
    w.hub.set_partitioned(c, true);
    w.peers[2]->last = FrameStatus::Timeout;  // stop stepping the cut-off peer
    // LAN total = 2400 ticks (38.4 s) of stall, then house 2 is kicked.
    REQUIRE(w.run_until_frame(400, 20000));
    auto& a = *w.peers[0];
    auto& b = *w.peers[1];
    CHECK(a.removed == std::vector<int>{2});
    CHECK(b.removed == std::vector<int>{2});
    CHECK(a.s->players() == 2);
    const size_t n = std::min(a.crcs.size(), b.crcs.size());
    for (size_t i = 0; i < n; ++i) REQUIRE(a.crcs[i] == b.crcs[i]);
}

TEST_CASE("net lockstep: initial sync times out when a peer never answers") {
    World w;
    w.make(2, lan());
    w.hub.set_partitioned(PeerAddr{NetAddr::loopback(2001), 0}, true);
    w.peers[1]->last = FrameStatus::Timeout;
    for (int i = 0; i < 2500; ++i) w.step();
    CHECK(w.peers[0]->last == FrameStatus::Timeout);
}

TEST_CASE("net lockstep: EXIT is executed at a common frame and closes the connection") {
    World w;
    w.make(2, lan());
    REQUIRE(w.run_until_frame(50, 5000));
    w.peers[1]->s->request_exit();
    REQUIRE(w.run_until_frame(80, 5000));
    CHECK(w.peers[0]->removed == std::vector<int>{1});
    CHECK(w.peers[0]->s->players() == 1);
}

TEST_CASE("net lockstep: CommProtocol 0 (raw 111-byte events) also works") {
    World w;
    LockstepConfig c = lan();
    c.protocol = 0;
    c.max_ahead = 8;
    w.make(2, c);
    REQUIRE(w.run_until_frame(320, 20000));
    const size_t n = std::min(w.peers[0]->crcs.size(), w.peers[1]->crcs.size());
    for (size_t i = 0; i < n; ++i) REQUIRE(w.peers[0]->crcs[i] == w.peers[1]->crcs[i]);
}

TEST_CASE("net lockstep: spawner Protocol 0 latency levels converge on all peers") {
    World w;
    w.hub.set_latency(30, 40);
    LockstepConfig c = lan();
    c.game_mode = GameMode::Internet;
    c.spawner_protocol_zero = true;
    c.frame_send_rate = 2;
    c.max_ahead = 12;
    w.make(2, c);
    REQUIRE(w.run_until_frame(800, 40000));
    auto& a = *w.peers[0];
    auto& b = *w.peers[1];
    CHECK(a.s->latency_level() > 0);
    CHECK(a.s->latency_level() == b.s->latency_level());
    CHECK(a.s->frame_send_rate() == b.s->frame_send_rate());
    CHECK(a.s->max_ahead() == b.s->max_ahead());
    const size_t n = std::min(a.crcs.size(), b.crcs.size());
    for (size_t i = 0; i < n; ++i) REQUIRE(a.crcs[i] == b.crcs[i]);
}

// W9-01-03 [RE: FUN_00647090 TIMING = min(1000 / worst PROCESS_TIME, speed
// cap); PROCESS_TIME = the processing time WITHOUT waits]: every peer adopts
// the slowest machine's rate; DesiredFrameRate starts at 30 (0x52D7FD).
TEST_CASE("W9-01-03: the TIMING frame rate follows the slowest peer's process time") {
    World w;
    LockstepConfig c = lan();
    c.game_speed = 1;   // lobby speed 1: cap 45
    w.make(2, c);
    CHECK(w.peers[0]->s->desired_fps() == 30u);
    // Peer 0 is fast (2 ms), peer 1 needs 80 ms per frame (a phone).
    for (int i = 0; i < 20000 && w.peers[0]->s->frame() < 700; ++i) {
        w.peers[0]->s->note_process_time(2.0);
        w.peers[1]->s->note_process_time(80.0);
        w.step();
    }
    REQUIRE(w.peers[0]->s->frame() >= 700);
    CHECK(w.peers[0]->s->desired_fps() == 12u);   // 1000 / 80
    CHECK(w.peers[1]->s->desired_fps() == 12u);
}

TEST_CASE("W9-01-01: a fast pair runs at the lobby speed cap (speed 1 = 45 fps, GAMESPEED changes it)") {
    World w;
    LockstepConfig c = lan();
    c.game_speed = 1;
    w.make(2, c);
    for (int i = 0; i < 20000 && w.peers[0]->s->frame() < 600; ++i) {
        for (auto& p : w.peers) p->s->note_process_time(3.0);
        w.step();
    }
    CHECK(w.peers[0]->s->desired_fps() == 45u);
    CHECK(w.peers[1]->s->desired_fps() == 45u);
    // GAMESPEED 4 executed on both peers: the next TIMING caps at 15.
    for (auto& p : w.peers) p->s->set_game_speed(4);
    for (int i = 0; i < 20000 && w.peers[0]->s->frame() < 1100; ++i) {
        for (auto& p : w.peers) p->s->note_process_time(3.0);
        w.step();
    }
    CHECK(w.peers[0]->s->desired_fps() == 15u);
    CHECK(w.peers[1]->s->desired_fps() == 15u);
}

// ---------------------------------------------------------------------------
// W9-05-12: malformed / forged packets are dropped, never executed, and the
// honest peers stay in sync.

TEST_CASE("net lockstep: forged sender house / far-future frame / non-master TIMING are dropped (W9-05-12)") {
    World w;
    w.make(3, lan());
    REQUIRE(w.run_until_frame(300, 20000));
    // Peer 1 (not the master) sends every kind of forged packet a few times
    // (the engine's RA2_NET_FUZZ hook): impersonating another house, a frame
    // 50000 ahead, a TIMING with absurd values, a megamission naming slot 200.
    int sent = 0;
    for (int pass = 0; pass < 40000 && sent < 12; ++pass) {
        w.step();
        if (w.peers[1]->s->frame() % 5 == 0 && w.peers[1]->s->frame() > 300 + static_cast<uint32_t>(sent) * 10) {
            w.peers[1]->s->send_test_garbage(sent);
            ++sent;
        }
    }
    REQUIRE(sent == 12);
    REQUIRE(w.run_until_frame(900, 40000));
    auto& a = *w.peers[0];
    for (int k : {0, 2}) {
        const LockstepStats& st = w.peers[k]->s->stats();
        INFO("peer " << k);
        CHECK(st.rejected_packets >= 6);   // kinds 0 and 1, three times each
        CHECK(st.rejected_events >= 3);    // the non-master TIMING
        CHECK(w.peers[k]->s->max_ahead() <= kMaxMaxAhead);
    }
    CHECK(a.s->max_ahead() == w.peers[2]->s->max_ahead());
    CHECK(a.s->frame_send_rate() == w.peers[1]->s->frame_send_rate());
    for (int k = 1; k < 3; ++k) {
        CHECK(w.peers[k]->last != FrameStatus::OutOfSync);
        const size_t n = std::min(a.crcs.size(), w.peers[k]->crcs.size());
        for (size_t i = 0; i < n; ++i) REQUIRE(a.crcs[i] == w.peers[k]->crcs[i]);
    }
}

TEST_CASE("net lockstep: a megamission cache slot beyond 0-7 is refused by the decoder (W9-05-12)") {
    // [04][count 0][23-byte payload with slot byte 9]: replaying "slot 9" of
    // house 0 would read house 1's cache (0*8 + 9).
    std::vector<uint8_t> pkt(kFrameHeaderSize, 0);
    FrameHeader h;
    h.house = 0;
    h.frame = 100;
    h.write(pkt.data());
    EventDecoder dec;
    // Seed house 1 slot 1 with a 3-unit list through a legal packet.
    {
        std::deque<Event> q;
        for (uint32_t u = 1; u <= 3; ++u) {
            ev::MegaMission m;
            m.whom = TargetRef{u, 0x0F};
            m.mission = 5;
            q.push_back(ev::mega_mission(1, m));
        }
        std::vector<uint8_t> b(kMaxGamePacket + 8, 0);
        FrameHeader h1;
        h1.house = 1;
        h1.frame = 100;
        h1.write(b.data());
        EventEncoder enc;
        std::vector<Event> echo;
        int n = 0;
        const size_t len = enc.add_compressed(q, b.data(), kMaxGamePacket, kFrameHeaderSize, 16, 100, 1, 100, echo, 64, n);
        b.resize(len);
        std::vector<Event> out;
        dec.extract(b.data(), b.size(), out);
    }
    size_t seeded = 0;
    for (int s = 0; s < 8; ++s) seeded += dec.cached_count(1, s);
    REQUIRE(seeded >= 2);   // house 1 has a cached list in some slot
    for (int slot = 8; slot < 256; ++slot) {
        std::vector<uint8_t> bad = pkt;
        bad.push_back(static_cast<uint8_t>(EventType::MEGAMISSION));
        bad.push_back(0);   // count 0: replay the cached list
        std::vector<uint8_t> payload(23, 0);
        payload[6] = static_cast<uint8_t>(slot);
        bad.insert(bad.end(), payload.begin(), payload.end());
        std::vector<Event> out;
        dec.extract(bad.data(), bad.size(), out);
        size_t megas = 0;
        for (const Event& e : out)
            if (e.type == static_cast<uint8_t>(EventType::MEGAMISSION)) ++megas;
        INFO("slot " << slot);
        CHECK(megas == 0);   // nothing replayed from another house's cache
    }
}
