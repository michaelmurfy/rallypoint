// tests/test_w9_speed.cpp -- wave-9 multiplayer speed fixes:
// W9-02-01 per-unit occupancy claims, W9-01-04 the per-frame sync CRC.
#include "doctest.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

#include "net_frame_crc.hpp"
#include "occupancy.hpp"

using ra2engine::Occupancy;

namespace {
// The pre-W9 O(w*h) release_all_of, as the reference.
void release_all_ref(Occupancy& o, int unit, int kx, int ky) {
    for (int y = 0; y < o.height(); ++y)
        for (int x = 0; x < o.width(); ++x)
            if (!(x == kx && y == ky)) o.release(x, y, unit);
}
bool same(const Occupancy& a, const Occupancy& b) {
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x) {
            for (int l = 0; l <= 1; ++l) {
                if (a.at(x, y, l) != b.at(x, y, l)) return false;
                for (int s = 2; s <= 4; ++s)
                    if (a.spot_unit(x, y, s, l) != b.spot_unit(x, y, s, l)) return false;
            }
        }
    return true;
}
std::uint32_t lcg(std::uint32_t& s) { return s = s * 1664525u + 1013904223u; }
}  // namespace

TEST_CASE("W9-02-01: release_all_of over the claim index equals the old full-map scan (both planes, sub-cells)") {
    for (int subcells = 0; subcells <= 1; ++subcells) {
        Occupancy o(40, 30);
        if (subcells) o.enable_subcells();
        std::uint32_t r = 12345u + static_cast<std::uint32_t>(subcells);
        for (int u = 0; u < 60; ++u) {
            o.mark_infantry(u, subcells && (u % 3 == 0));
            if (u % 7 == 0) o.mark_layer(u, 1);   // some deck units (the second plane)
        }
        for (int step = 0; step < 6000; ++step) {
            const int u = static_cast<int>(lcg(r) % 60);
            const int x = static_cast<int>(lcg(r) % 40), y = static_cast<int>(lcg(r) % 30);
            switch (lcg(r) % 6) {
                case 0: case 1: o.set(x, y, u); break;
                case 2: o.release(x, y, u); break;
                case 3: if (subcells) o.set_spot(x, y, u, 2 + static_cast<int>(lcg(r) % 3)); break;
                case 4: if (lcg(r) % 8 == 0) o.clear(x, y); break;
                default: {
                    Occupancy ref = o;
                    const int kx = (lcg(r) % 2) ? x : -1, ky = kx < 0 ? -1 : y;
                    release_all_ref(ref, u, kx, ky);
                    o.release_all_of(u, kx, ky);
                    REQUIRE(same(o, ref));
                    CHECK(o.claim_count(u) <= 1);
                }
            }
        }
    }
}

TEST_CASE("W9-02-01: release_all_of is O(claims) on a 263x263 map") {
    Occupancy o(263, 263);
    o.enable_subcells();
    for (int u = 0; u < 1000; ++u) o.set(u % 263, u / 263, u);
    const auto t0 = std::chrono::steady_clock::now();
    for (int k = 0; k < 10000; ++k) {
        const int u = k % 1000;
        o.release_all_of(u, u % 263, u / 263);
        o.set(u % 263, u / 263, u);
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    MESSAGE("10000 release_all_of on 263x263: " << ms << " ms");
    CHECK(ms < 50.0);   // the old scan: ~69k cells per call (seconds for 10000)
    for (int u = 0; u < 1000; ++u) CHECK(o.at(u % 263, u / 263) == u);
}

TEST_CASE("W9-01-04: the frame CRC fold is FUN_0064D520's rotl1 + term, NaN canonical (W9-05 policy)") {
    ra2engine::FrameCrc c;
    c.add(5);
    CHECK(c.v == 5u);
    c.add(0x80000000u);   // rotl1(5) = 10
    CHECK(c.v == 0x8000000Au);
    c.add(0);             // rotl1(0x8000000A) = 0x15
    CHECK(c.v == 0x15u);
    ra2engine::FrameCrc a, b;
    a.add_d(std::numeric_limits<double>::quiet_NaN());
    b.add_d(-std::numeric_limits<double>::quiet_NaN());
    CHECK(a.v == b.v);
    // W9-05 sync-hash policy (w9int): the CRC folds om::canonical_bits, the
    // save / full-hash rule -- NaN canonical, everything else bit-exact.
    ra2engine::FrameCrc n1, n2;
    std::uint64_t pay = 0x7FF0000000000123ull;   // a signalling NaN with a payload
    double sn = 0.0;
    std::memcpy(&sn, &pay, 8);
    n1.add_d(sn);
    n2.add_u64(0x7FF8000000000000ull);
    CHECK(n1.v == n2.v);
    ra2engine::FrameCrc z1, z2;
    z1.add_d(0.0);
    z2.add_d(-0.0);
    CHECK(z1.v != z2.v);
    ra2engine::FrameCrc p1, p2;
    p1.add_d(1.0);
    p2.add_d(std::nextafter(1.0, 2.0));
    CHECK(p1.v != p2.v);   // a one-ulp position drift is caught
}
