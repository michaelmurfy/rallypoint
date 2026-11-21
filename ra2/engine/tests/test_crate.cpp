#include "doctest.h"
#include "crate.hpp"
using namespace ra2engine;

// Pins from docs/re/veterancy.md §3 (C1-C8). The RNG reference values below
// were produced by an independent Python transcription of the disassembly of
// FUN_0065C110 / 0x65C1C0 / 0x65C220 (scratch rng.py), so the C++ port is
// cross-checked against a second implementation of the same listing.

TEST_CASE("crate: GameRandom psdes seed table + R250 sequence [C1]") {
    GameRandom r0(0);
    CHECK(r0.table_word(0) == 0xad2eaa18u);
    CHECK(r0.table_word(1) == 0x667051bbu);
    CHECK(r0.table_word(249) == 0xd72e1536u);
    GameRandom r(12345);
    CHECK(r.next() == 2979637345u);
    CHECK(r.next() == 186607930u);
    CHECK(r.next() == 1812860918u);
    GameRandom q(12345);
    const int want[8] = {98, 59, 2, 40, 102, 36, 90, 79};
    for (int i = 0; i < 8; ++i) CHECK(q.ranged(1, 110) == want[i]);
}

TEST_CASE("crate: GameRandom::ranged bounds, swapped args, degenerate range [C1]") {
    GameRandom r(7);
    CHECK(r.ranged(5, 5) == 5);
    for (int i = 0; i < 10000; ++i) {
        const int a = r.ranged(3, 9);
        CHECK(a >= 3);
        CHECK(a <= 9);
        const int b = r.ranged(9, 3);      // either order
        CHECK(b >= 3);
        CHECK(b <= 9);
        const int c = r.ranged(0, 0x7ffffffe);
        CHECK(c >= 0);
    }
    // determinism
    GameRandom x(99), y(99);
    for (int i = 0; i < 1000; ++i) CHECK(x.ranged(0, 1000) == y.ranged(0, 1000));
}

TEST_CASE("crate: [Powerups] parse incl. comment and % argument [C6]") {
    const PowerupDef a = parse_powerup("10,ARMOR,yes,1.5              ; armor of nearby objects");
    CHECK(a.weight == 10);
    CHECK(a.anim == "ARMOR");
    CHECK(a.naval);
    CHECK(a.arg == doctest::Approx(1.5));
    const PowerupDef u = parse_powerup("20,<none>,no");
    CHECK(u.weight == 20);
    CHECK_FALSE(u.naval);
    CHECK(u.arg == 0.0);
    const PowerupDef p = parse_powerup("5,X,yes,50%");
    CHECK(p.arg == doctest::Approx(0.5));
    const auto d = default_powerups();
    int total = 0;
    for (const PowerupDef& e : d) total += e.weight;
    CHECK(total == 110);   // 10+10+10+20+10+10+20+20 (stock weights, §0)
    CHECK(d[(int)Powerup::Money].arg == 2000.0);
    CHECK(d[(int)Powerup::Veteran].arg == 1.0);
}

TEST_CASE("crate: cumulative weighted pick never selects a 0-weight powerup [C5]") {
    const auto d = default_powerups();
    GameRandom r(4242);
    int hits[kPowerupCount] = {};
    for (int i = 0; i < 20000; ++i) {
        const int p = pick_powerup(r, d);
        REQUIRE(p >= 0);
        REQUIRE(p < kPowerupCount);
        ++hits[p];
    }
    for (int i = 0; i < kPowerupCount; ++i)
        if (d[i].weight == 0) CHECK(hits[i] == 0);
    CHECK(hits[(int)Powerup::Money] > hits[(int)Powerup::Armor]);   // 20 vs 10 shares
    // An all-zero table returns -1.
    std::array<PowerupDef, kPowerupCount> z{};
    CHECK(pick_powerup(r, z) == -1);
    // A single-weight table always returns that powerup (draw 1..1).
    z[(int)Powerup::Reveal].weight = 3;
    CHECK(pick_powerup(r, z) == (int)Powerup::Reveal);
}

TEST_CASE("crate: lifetime within [Regen*450, Regen*1800] frames; initial count clamp [C2/C4]") {
    GameRandom r(1);
    for (int i = 0; i < 2000; ++i) {
        const int f = crate_lifetime_frames(r, 3.0);
        CHECK(f >= 1350);
        CHECK(f <= 5400);
    }
    CrateRules cr;
    CHECK(initial_crate_count(0, cr) == 1);    // CrateMinimum
    CHECK(initial_crate_count(1, cr) == 1);
    CHECK(initial_crate_count(4, cr) == 4);
    cr.maximum = 2;
    CHECK(initial_crate_count(4, cr) == 2);
    cr.minimum = 3;                              // Max applied last
    CHECK(initial_crate_count(0, cr) == 2);
}
