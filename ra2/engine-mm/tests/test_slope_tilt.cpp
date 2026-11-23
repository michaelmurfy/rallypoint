#include "doctest.h"
#include "slope_tilt.hpp"
#include <cmath>

TEST_CASE("slope_tilt: flat SlopeIndex is zero pitch") {
    CHECK(ra2engine::slope_tilt_pitch(0, 0) == 0.0);
    CHECK(ra2engine::slope_tilt_pitch(-1, 0x4000) == 0.0);
    CHECK(ra2engine::slope_tilt_pitch(17, 0) == 0.0);
}

TEST_CASE("slope_tilt: single-height corner tips along its axis") {
    // SlopeIndex 7: yaw 45°, ebp/single pitch atan(104/256). Facing NE
    // (0x2000 → index 4, yaw = pi/4) aligns with the axis.
    const double along = ra2engine::slope_tilt_pitch(7, 0x2000);
    CHECK(along == doctest::Approx(0.38588266939807375).epsilon(1e-6));
    // Facing SE (0x6000 → index 12, yaw = -pi/4): cos(pi/2) = 0.
    const double across = ra2engine::slope_tilt_pitch(7, 0x6000);
    CHECK(across == doctest::Approx(0.0).epsilon(1e-4));
}

TEST_CASE("slope_tilt: steep cardinal uses larger pitch magnitude") {
    // Index 4 (ebx/steep): yaw 0°. Facing East 0x4000 → full steep pitch.
    const double p = ra2engine::slope_tilt_pitch(4, 0x4000);
    CHECK(p == doctest::Approx(0.6857295104989298).epsilon(1e-5));
}
