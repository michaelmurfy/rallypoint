// Net L0: the FastMath / _ftol routines (engine/orig_math.*).
//
// 1. The lookup tables are generated at configure time by
//    tools/gen_math_tables.py (Rallypoint: our own correctly rounded values,
//    not the original game's). The routines are checked against libm within
//    the table step, and a golden hash pins every result bit, so two builds
//    that would simulate differently fail here.
// 2. A source scan bans libm transcendentals, std::sqrt, std::hypot and
//    std::pow in the simulation sources (presentation code may keep libm):
//    cross-platform lockstep needs bit-identical results on every platform.
#include "doctest.h"

#include <cmath>
#include <limits>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "orig_math.hpp"

namespace om = ra2engine::om;

namespace {
std::uint64_t bits(double d) { std::uint64_t b; std::memcpy(&b, &d, 8); return b; }

// FNV-1a over the result bits of every routine on a fixed corpus.
std::uint64_t math_hash() {
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&h](double v) {
        const std::uint64_t b = bits(v);
        for (int i = 0; i < 8; ++i) { h ^= (b >> (8 * i)) & 0xFF; h *= 1099511628211ull; }
    };
    for (int i = -20000; i <= 20000; ++i) {
        const double x = i * 0.000731;
        mix(om::sin(x)); mix(om::cos(x)); mix(om::tan(x)); mix(om::atan(x * 7.0));
        mix(om::asin(x / 15.0)); mix(om::acos(x / 15.0)); mix(om::atan2(x, 3.0 - x));
        mix(om::sqrt(x * x * 1000.0 + i));
    }
    return h;
}
}  // namespace

TEST_CASE("orig_math: _ftol truncates and wraps mod 2^32") {
    CHECK(om::ftol(0.5) == 0);
    CHECK(om::ftol(-1.99) == -1);
    CHECK(om::ftol(2147483647.0) == 2147483647);
    CHECK(static_cast<std::uint32_t>(om::ftol(2147483648.0)) == 0x80000000u);
    CHECK(om::ftol(4294967303.0) == 7);            // wraps, does not saturate
    CHECK(om::ftol(-2.9) == -2);                   // truncation toward zero
    CHECK(om::ftol(std::nan("")) == 0);            // integer indefinite -> low half 0
    CHECK(om::ftol(std::numeric_limits<double>::infinity()) == 0);
    CHECK(om::ftol(1e30) == 0);
}

TEST_CASE("orig_math: sqrt is within the table's precision") {
    for (int i = 1; i < 200000; i += 7) {
        const double x = i * 0.37;
        CHECK(std::fabs(om::sqrt(x) - std::sqrt(x)) <= std::sqrt(x) * 7e-5);
    }
    CHECK(om::sqrt(0.0) == 0.0);
    CHECK(om::sqrt(-4.0) == om::sqrt(4.0));
    CHECK(om::sqrt(4.0) == 2.0);
}

TEST_CASE("orig_math: sin/cos/tan follow their tables") {
    const double step = 2 * 3.14159265358979323846 / 8192;
    for (int i = -30000; i <= 30000; i += 3) {
        const double x = i * 0.00041;
        // up to two steps: a negative index rounds its half step away from x
        CHECK(std::fabs(om::sin(x) - std::sin(x)) <= 2 * step);
        CHECK(std::fabs(om::cos(x) - std::cos(x)) <= 2 * step);
        if (std::fabs(std::cos(x)) > 0.2) CHECK(std::fabs(om::tan(x) - std::tan(x)) <= 0.05);
    }
    CHECK(om::sin(0.0) == 0.0);
    CHECK(om::cos(0.0) == 1.0);
    CHECK(om::sin_table(2048) == 1.0f);   // exact at the quadrant points
    CHECK(om::sin_table(4096) == 0.0f);
}

TEST_CASE("orig_math: asin/acos/atan/atan2 follow their tables") {
    for (int i = -2048; i <= 2048; ++i) {
        const double x = i / 2048.0;
        CHECK(std::fabs(om::asin(x) - std::asin(x)) <= 0.05);
        CHECK(std::fabs(om::acos(x) - std::acos(x)) <= 0.05);
    }
    for (int i = -4000; i <= 4000; ++i) {
        const double x = i * 0.025;
        CHECK(std::fabs(om::atan(x) - std::atan(x)) <= 0.025);
        CHECK(std::fabs(om::atan2(x, 1.5) - std::atan2(x, 1.5)) <= 0.025);
        CHECK(std::fabs(om::atan2(x, -1.5) - std::atan2(x, -1.5)) <= 0.025);
    }
    CHECK(om::atan2(1.0, 0.0) == static_cast<double>(1.57079632679489661923f));
    CHECK(om::atan2(0.0, 0.0) == 0.0);
}

TEST_CASE("orig_math: every result bit is the same on every build") {
    // Golden value of the generated tables; a change here changes the game.
    CHECK(math_hash() == 0xc9b1842ad103ed04ull);
}

TEST_CASE("orig_math: ipow is plain repeated multiplication") {
    CHECK(om::ipow(1.25, 0) == 1.0);
    CHECK(om::ipow(1.25, 2) == 1.25 * 1.25);
    CHECK(om::ipow(2.0, -2) == 0.25);
}

#ifdef RA2_SOURCE_DIR
// The simulation sources: everything World::step / the logic tick runs. Each
// must use om:: (or IEEE-exact floor/ceil/round/fmod/trunc) -- never libm
// transcendentals, std::sqrt/hypot/pow, whose last bit differs between the
// MinGW UCRT and Android bionic (measured, docs/gap/net/04-engine-lockstep.md).
namespace {
const char* const kSim[] = {
        "world.cpp", "world_veterancy.cpp", "world_an.cpp", "world_depth.cpp", "world_abilities.cpp",
        "world_mcv.cpp", "world_sw.cpp", "world_w5.cpp", "world_w6.cpp", "world_econ.cpp", "world.hpp",
        "combat.cpp", "combat.hpp", "building_combat.cpp", "projectile.cpp", "projectile.hpp",
        "rocket.cpp", "rocket.hpp", "ai_base.cpp", "ai_team.cpp", "ai_team.hpp", "ai_trigger.cpp",
        "harvest.cpp", "flight.cpp", "cargo.cpp", "capture.cpp", "abilities.cpp", "abilities.hpp",
        "abil_w5_rules.cpp", "superweapon.cpp", "ore.cpp", "crate.cpp", "veterancy.cpp",
        "repair_sell.cpp", "production.cpp", "factory.cpp", "building.cpp", "map_trigger.cpp",
        "campaign.cpp", "unit_orders.cpp", "weapon.cpp", "weapon_unit.cpp", "weapon_resolve.cpp",
        "inf_anim.cpp", "bridges.cpp", "occupancy.cpp", "visibility.cpp", "group_move.cpp",
        "pathfind.cpp", "roster2.cpp", "skirmish.cpp", "campaign_runtime.inc", "realplay_scene.inc",
    };
}  // namespace

TEST_CASE("orig_math: no libm transcendental / sqrt / hypot / pow in simulation sources") {
    const std::regex banned(
        R"((^|[^A-Za-z0-9_.:>])(std::)?(sqrt|sqrtf|hypot|hypotf|sin|sinf|cos|cosf|tan|tanf|asin|acos|atan|atan2|atan2f|atanf|pow|powf|exp|expf|exp2|log|log2|log10|cbrt)\s*\()");
    int hits = 0;
    for (const char* f : kSim) {
        std::ifstream in(std::string(RA2_SOURCE_DIR) + "/engine/" + f);
        REQUIRE_MESSAGE(in.good(), std::string(f));
        std::string line;
        int ln = 0;
        bool block = false;
        while (std::getline(in, line)) {
            ++ln;
            // strip comments (good enough for this code base: no "//" in sim string literals that matter)
            std::string code;
            for (std::size_t i = 0; i < line.size(); ++i) {
                if (block) {
                    if (line.compare(i, 2, "*/") == 0) { block = false; ++i; }
                    continue;
                }
                if (line.compare(i, 2, "/*") == 0) { block = true; ++i; continue; }
                if (line.compare(i, 2, "//") == 0) break;
                if (line[i] == '"') {   // skip string literals
                    std::size_t j = i + 1;
                    while (j < line.size() && line[j] != '"') j += (line[j] == '\\') ? 2 : 1;
                    i = j;
                    continue;
                }
                code += line[i];
            }
            if (std::regex_search(code, banned)) {
                MESSAGE("banned libm call in sim source " << std::string(f) << ":" << ln << ": " << line);
                ++hits;
            }
        }
    }
    CHECK(hits == 0);
}

// W9-05-13: std::lround / lrint return `long` (32-bit Windows, 64-bit
// Android): the sim rounds through om::lround.
TEST_CASE("orig_math: no long-returning lround / lrint in simulation sources") {
    const std::regex banned(R"((^|[^A-Za-z0-9_.:>])(std::)?(lround|lroundf|lrint|lrintf)\s*\()");
    int hits = 0;
    for (const char* f : kSim) {
        std::ifstream in(std::string(RA2_SOURCE_DIR) + "/engine/" + f);
        REQUIRE_MESSAGE(in.good(), std::string(f));
        std::string line;
        int ln = 0;
        while (std::getline(in, line)) {
            ++ln;
            const std::size_t c = line.find("//");
            const std::string code = c == std::string::npos ? line : line.substr(0, c);
            if (std::regex_search(code, banned)) {
                MESSAGE("long-returning rounding call in sim source " << std::string(f) << ":" << ln << ": " << line);
                ++hits;
            }
        }
    }
    CHECK(hits == 0);
}
#endif

TEST_CASE("orig_math: lround / itrunc are defined everywhere and match the Windows build (W9-05-13)") {
    // In range: std::lround's half-away-from-zero.
    CHECK(om::lround(2.5) == 3);
    CHECK(om::lround(-2.5) == -3);
    CHECK(om::lround(2.4999999999999996) == 2);
    CHECK(om::lround(0.49999999999999994) == 0);
    CHECK(om::lround(-0.5) == -1);
    CHECK(om::lround(2147483647.4) == 2147483647);
    CHECK(om::lround(-2147483648.4) == -2147483647 - 1);
    // Out of int32 / NaN: the MinGW UCRT (32-bit long) result, 0.
    CHECK(om::lround(1e30) == 0);
    CHECK(om::lround(-1e30) == 0);
    CHECK(om::lround(3e9) == 0);
    CHECK(om::lround(2147483647.5) == 0);
    CHECK(om::lround(-2147483648.5) == 0);
    CHECK(om::lround(std::numeric_limits<double>::quiet_NaN()) == 0);
    CHECK(om::lround(std::numeric_limits<double>::infinity()) == 0);
    // itrunc: the x86 CVTTSD2SI result.
    CHECK(om::itrunc(2.9) == 2);
    CHECK(om::itrunc(-2.9) == -2);
    CHECK(om::itrunc(2147483647.9) == 2147483647);
    CHECK(om::itrunc(2147483648.0) == -2147483647 - 1);
    CHECK(om::itrunc(-2147483649.0) == -2147483647 - 1);
    CHECK(om::itrunc(1e30) == -2147483647 - 1);
    CHECK(om::itrunc(std::numeric_limits<double>::quiet_NaN()) == -2147483647 - 1);
#if defined(_WIN32) && defined(__x86_64__) && (defined(_UCRT) || defined(_MSC_VER))
    // On the Windows build the helpers ARE the platform's results (the UCRT's; Rallypoint's MinGW
    // cross build links the older msvcrt, whose lround differs out of range: the sim never calls it).
    volatile double big = 3e9, nan = std::numeric_limits<double>::quiet_NaN(), half = 2.5;
    CHECK(om::lround(big) == static_cast<int>(std::lround(big)));
    CHECK(om::lround(nan) == static_cast<int>(std::lround(nan)));
    CHECK(om::lround(half) == static_cast<int>(std::lround(half)));
    CHECK(om::itrunc(big) == static_cast<int>(big));
    CHECK(om::itrunc(nan) == static_cast<int>(nan));
#endif
}
