// tools/ra2netpeer.cpp -- two-process LAN test driver for engine/net.
//
// Runs one lockstep peer over real UDP with the deterministic StubSim and
// prints a hash trail; two instances (one per house) must print identical
// trails. Example (PowerShell, two windows or Start-Process):
//   ra2netpeer --house 0 --port 41234 --peer 127.0.0.1:41235 --frames 600
//   ra2netpeer --house 1 --port 41235 --peer 127.0.0.1:41234 --frames 600
// Through a CnCNet-style V2 tunnel instead of P2P:
//   ra2netpeer --house 0 --port 0 --tunnel 1.2.3.4:50000 --id 11 --peer-id 22 ...
// --selftest runs both peers in this process over two real sockets.
#include "net/lockstep.hpp"
#include "net/net_manager.hpp"
#include "net/stub_sim.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace ra2::net;

namespace {
uint32_t now_ms() {
    using namespace std::chrono;
    return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

struct Runner {
    int house = 0;
    UdpDatagramIO io;
    std::unique_ptr<NetManager> net;
    std::unique_ptr<LockstepSession> s;
    StubSim sim;
    StubInput in{1};
    bool in_frame = false;
    FrameStatus last = FrameStatus::Proceed;
    std::string trail;

    bool setup(int h, uint16_t port, const std::vector<std::pair<int, PeerAddr>>& peers, bool p0) {
        house = h;
        in = StubInput(0x9E3779B9u * static_cast<uint32_t>(h + 1));
        if (!io.open(port)) {
            std::fprintf(stderr, "open port %u failed: %s\n", port, io.socket().last_error().c_str());
            return false;
        }
        net = std::make_unique<NetManager>(io, now_ms);
        std::vector<int> houses{h};
        for (auto& p : peers) {
            net->create_connection(p.first, p.second);
            houses.push_back(p.first);
        }
        LockstepConfig c;
        c.game_mode = GameMode::LAN;
        c.local_house = h;
        c.houses = houses;
        c.master = (h == 0);
        c.spawner_protocol_zero = p0;
        if (p0) {
            c.frame_send_rate = 2;
            c.max_ahead = 12;
        }
        s = std::make_unique<LockstepSession>(*net, c);
        return true;
    }

    // returns false when finished / failed
    bool step(uint32_t frames) {
        if (s->frame() >= frames) return false;
        if (last != FrameStatus::Proceed && last != FrameStatus::Stall) return false;
        if (!in_frame) {
            sim.logic_step(s->frame());
            Event e;
            if (in.maybe(house, e)) s->queue_local(e);
            s->begin_frame(sim.crc());
            in_frame = true;
        }
        last = s->poll();
        if (last == FrameStatus::Proceed) {
            for (const auto& x : s->executed()) sim.apply(x);
            if (s->frame() % 100 == 0) {
                char b[64];
                std::snprintf(b, sizeof b, "frame %u crc %08x\n", s->frame(), sim.crc());
                trail += b;
            }
            s->end_frame();
            in_frame = false;
        }
        return true;
    }
    const char* status() const {
        switch (last) {
            case FrameStatus::Proceed: return "ok";
            case FrameStatus::Stall: return "stalled";
            case FrameStatus::ScenarioMismatch: return "scenario-mismatch";
            case FrameStatus::DoListFull: return "dolist-full";
            case FrameStatus::Timeout: return "timeout";
            case FrameStatus::OutOfSync: return "out-of-sync";
        }
        return "?";
    }
};

int run_selftest(uint32_t frames, bool p0) {
    Runner a, b;
    UdpSocket probe;
    // ephemeral ports: open, read back, then wire each other up
    if (!a.io.open(0, 0x7F000001u) || !b.io.open(0, 0x7F000001u)) return 2;
    const uint16_t pa = a.io.socket().local_port(), pb = b.io.socket().local_port();
    a.io.socket().close();
    b.io.socket().close();
    if (!a.setup(0, pa, {{1, PeerAddr{NetAddr::loopback(pb), 0}}}, p0)) return 2;
    if (!b.setup(1, pb, {{0, PeerAddr{NetAddr::loopback(pa), 0}}}, p0)) return 2;
    const uint32_t t0 = now_ms();
    while (now_ms() - t0 < 120000) {
        const bool ra = a.step(frames), rb = b.step(frames);
        if (!ra && !rb) break;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    std::printf("A: %s frame %u crc %08x executed %u\nB: %s frame %u crc %08x executed %u\n", a.status(),
                a.s->frame(), a.sim.crc(), a.sim.executed, b.status(), b.s->frame(), b.sim.crc(), b.sim.executed);
    const bool ok = a.trail == b.trail && a.sim.crc() == b.sim.crc() && a.s->frame() >= frames &&
                    b.s->frame() >= frames;
    std::printf("SELFTEST %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
}  // namespace

int main(int argc, char** argv) {
    int house = 0;
    uint16_t port = 41234;
    uint32_t frames = 600;
    std::string peer, tunnel;
    uint32_t id = 0, peer_id = 0;
    bool selftest = false, p0 = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--house") house = std::atoi(next().c_str());
        else if (a == "--port") port = static_cast<uint16_t>(std::atoi(next().c_str()));
        else if (a == "--peer") peer = next();
        else if (a == "--frames") frames = static_cast<uint32_t>(std::atoi(next().c_str()));
        else if (a == "--tunnel") tunnel = next();
        else if (a == "--id") id = static_cast<uint32_t>(std::atoi(next().c_str()));
        else if (a == "--peer-id") peer_id = static_cast<uint32_t>(std::atoi(next().c_str()));
        else if (a == "--protocol-zero") p0 = true;
        else if (a == "--selftest") selftest = true;
        else {
            std::fprintf(stderr, "unknown arg %s\n", a.c_str());
            return 2;
        }
    }
    if (selftest) return run_selftest(frames, p0);

    Runner r;
    PeerAddr pa;
    if (!tunnel.empty()) {
        NetAddr srv;
        if (!NetAddr::parse(tunnel, srv)) return 2;
        pa = PeerAddr{srv, peer_id};
        r.io.set_tunnel(TunnelMode::V2, srv, id);
    } else if (!NetAddr::parse(peer, pa.addr)) {
        std::fprintf(stderr, "--peer a.b.c.d:port required\n");
        return 2;
    }
    if (!r.setup(house, port, {{1 - house, pa}}, p0)) return 2;
    const uint32_t t0 = now_ms();
    while (r.step(frames) && now_ms() - t0 < 180000) std::this_thread::sleep_for(std::chrono::microseconds(500));
    // keep servicing a little so the peer gets our last ACKs
    const uint32_t t1 = now_ms();
    while (now_ms() - t1 < 500) {
        r.net->service();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::fputs(r.trail.c_str(), stdout);
    std::printf("FINAL %s house %d frame %u crc %08x executed %u\n", r.status(), house, r.s->frame(), r.sim.crc(),
                r.sim.executed);
    return r.last == FrameStatus::Proceed || r.s->frame() >= frames ? 0 : 1;
}
