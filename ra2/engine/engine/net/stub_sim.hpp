// engine/net/stub_sim.hpp -- a tiny deterministic "simulation" used to test
// the lockstep scheduler without the real World (loopback unit tests and the
// two-process tools/ra2netpeer driver). Header-only, test support only.
//
// Each house has an accumulator; every frame's logic step mixes the frame
// number in; every executed event mixes its wire bytes in. Two peers that
// execute the same events at the same frames end with identical hashes.
#pragma once

#include "net/event.hpp"
#include "net/lockstep.hpp"
#include "net/wire.hpp"

#include <array>
#include <cstdint>

namespace ra2::net {

struct StubSim {
    std::array<uint32_t, 8> acc{};
    uint32_t frames = 0;
    uint32_t executed = 0;

    void logic_step(uint32_t frame) {
        for (size_t i = 0; i < acc.size(); ++i) acc[i] = acc[i] * 1103515245u + 12345u + frame + static_cast<uint32_t>(i);
        ++frames;
    }
    void apply(const ExecItem& x) {
        const Event& e = x.ev;
        uint32_t h = static_cast<uint32_t>(e.type) * 2654435761u ^ e.frame;
        uint8_t raw[kEventClassSize];
        e.to_raw(raw);
        raw[1] = 0;  // IsExecuted is local
        // W9-05-14: a MEGAMISSION's byte data[6] is the codec's cache slot,
        // transport bookkeeping: the encoder writes it into the copy it sends
        // but the sender's own echo keeps 0 (the engine's event->Command map
        // ignores it). Hashing it made --selftest report a false OOS after
        // ~2,550 frames.
        if (e.type == static_cast<uint8_t>(EventType::MEGAMISSION)) raw[7 + 6] = 0;
        for (size_t k = 0; k < kEventClassSize; ++k) h = (h ^ raw[k]) * 16777619u;
        h ^= static_cast<uint32_t>(x.group_size) << 16;
        acc[static_cast<size_t>(e.house) & 7] = add_crc(acc[static_cast<size_t>(e.house) & 7], h);
        ++executed;
    }
    // The frame CRC handed to LockstepSession::begin_frame.
    uint32_t crc() const {
        uint32_t c = 0;
        for (uint32_t a : acc) c = add_crc(c, a);
        return c;
    }
};

// Deterministic per-house command generator (xorshift).
struct StubInput {
    uint32_t s;
    explicit StubInput(uint32_t seed) : s(seed ? seed : 1) {}
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    // Sometimes returns a command for this frame (true), else false.
    bool maybe(int house, Event& out) {
        const uint32_t r = next();
        if (r % 7 != 0) return false;
        switch ((r >> 8) % 4) {
            case 0: {
                ev::MegaMission m;
                m.whom = TargetRef{(r >> 4) & 0xFF, 0x0F};
                m.mission = 5;
                m.dest = TargetRef{(r >> 12) & 3, 0x2A};
                out = ev::mega_mission(house, m);
                break;
            }
            case 1: out = ev::production(EventType::PRODUCE, house, ev::Production{0x0F, static_cast<int32_t>(r & 31), 0}); break;
            case 2: out = ev::whom(EventType::DEPLOY, house, TargetRef{r & 0xFFF, 0x0F}); break;
            default: out = ev::place(house, ev::Place{6, static_cast<int32_t>(r & 15), 0, CellXY{int16_t(r & 127), int16_t((r >> 7) & 127)}}); break;
        }
        return true;
    }
};

}  // namespace ra2::net
