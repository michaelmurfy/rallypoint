// W3-B infantry sequence machine (engine/inf_anim.cpp) -- pure checks of the
// recovered rules (docs/re/infantry-anim.md §8-§12).
#include "doctest.h"
#include "inf_anim.hpp"

using namespace ra2engine;

namespace {
InfTable gi_table() {
    // artmd [GISequence] (infantry-anim.md §2/§8).
    InfTable t;
    auto set = [&](int q, int f, int c, int m, int face = -1) { t.e[q] = InfSeqEntry{f, c, m, face, true}; };
    set(kSqReady, 0, 1, 1);
    set(kSqGuard, 0, 1, 1);
    set(kSqProne, 86, 1, 6);
    set(kSqWalk, 8, 6, 6);
    set(kSqFireUp, 164, 6, 6);
    set(kSqDown, 260, 2, 2);
    set(kSqCrawl, 86, 6, 6);
    set(kSqUp, 276, 2, 2);
    set(kSqFireProne, 212, 6, 6);
    set(kSqIdle1, 56, 15, 0, 4);
    set(kSqIdle2, 71, 15, 0, 2);
    set(kSqDie1, 134, 15, 0);
    set(kSqDie2, 149, 15, 0);
    set(kSqDeploy, 300, 15, 0);
    set(kSqDeployed, 292, 1, 1);
    set(kSqUndeploy, 276, 2, 2);
    set(kSqCheer, 364, 8, 0, 2);
    set(kSqPanic, 8, 6, 6);
    return t;
}
}  // namespace

TEST_CASE("inf_anim: DoControls rates from gamemd 0x7E9F7C") {
    CHECK(inf_do_control(kSqReady).rate == 0);
    CHECK(inf_do_control(kSqWalk).rate == 3);
    CHECK(inf_do_control(kSqWalk).mobile);
    CHECK(inf_do_control(kSqFireUp).rate == 1);
    CHECK(inf_do_control(kSqIdle1).rate == 3);
    CHECK(inf_do_control(kSqPanic).rate == 4);
    CHECK(!inf_do_control(kSqDie1).interrupt);
    CHECK(inf_do_control(kSqCheer).rate == 3);
}

TEST_CASE("inf_anim: walk cycles one stage per 3 ticks with the facing block") {
    InfTable t = gi_table();
    InfAnimState s;
    InfTickCtx c;
    c.moving = true;
    int facing = 2;
    int fr = inf_anim_tick(s, t, facing, c);   // Ready -> Walk on the first tick
    CHECK(s.seq == kSqWalk);
    CHECK(fr == 8 + 2 * 6);
    for (int i = 0; i < 3; ++i) fr = inf_anim_tick(s, t, facing, c);
    CHECK(fr == 8 + 2 * 6 + 1);
}

TEST_CASE("inf_anim: InfDeath picks Die1 / Die2 / a death anim") {
    InfTable t = gi_table();
    for (int d : {1, 2, 3, 5}) {
        InfAnimState s;
        InfTickCtx c;
        int facing = 4;
        inf_anim_tick(s, t, facing, c);
        s.death = d;
        c.dying = true;
        const int fr = inf_anim_tick(s, t, facing, c);
        if (d == 1) CHECK(fr == 134);
        if (d == 2) CHECK(fr == 149);
        if (d >= 3) CHECK(s.vanish);
    }
}

TEST_CASE("inf_anim: fear -> Down -> Prone, ProneDamage halves, Up after decay") {
    InfTable t = gi_table();
    InfAnimState s;
    s.armed = true;
    InfTickCtx c;
    int facing = 0;
    inf_anim_tick(s, t, facing, c);
    inf_on_hit(s, /*has_attacker=*/true, 0.9);
    CHECK(s.fear == 100);
    inf_anim_tick(s, t, facing, c);
    CHECK(s.seq == kSqDown);
    CHECK(s.prone);
    CHECK(inf_prone_damage(s, 40, 0.5) == 20);
    for (int i = 0; i < 4; ++i) inf_anim_tick(s, t, facing, c);
    CHECK(s.seq == kSqProne);
    for (int i = 0; i < 60; ++i) inf_anim_tick(s, t, facing, c);
    CHECK(!s.prone);   // fear < 50 -> Up
}

TEST_CASE("inf_anim: a moving human soldier does not drop; Fearless never fears") {
    InfTable t = gi_table();
    InfAnimState s;
    s.armed = true;
    InfTickCtx c;
    c.human = true;
    c.moving = true;
    int facing = 0;
    inf_on_hit(s, true, 0.9);
    inf_anim_tick(s, t, facing, c);
    CHECK(!s.prone);
    InfAnimState f;
    f.armed = true;
    f.fearless = true;
    inf_on_hit(f, true, 0.1);
    CHECK(f.fear == 0);
}

TEST_CASE("inf_anim: idle timer lies in [freq*450, freq*1800] ticks") {
    InfTable t = gi_table();
    for (std::uint32_t seed = 1; seed < 40; ++seed) {
        InfAnimState s;
        s.rng = seed;
        InfTickCtx c;
        int facing = 0;
        inf_anim_tick(s, t, facing, c);
        CHECK(s.idle_timer >= static_cast<int>(0.15 * 450.0) - 1);
        CHECK(s.idle_timer <= static_cast<int>(0.15 * 1800.0));
    }
}
