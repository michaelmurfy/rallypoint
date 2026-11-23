#include "slope_tilt.hpp"
#include "voxel_raster.hpp"
#include <cmath>

namespace ra2engine {
namespace {

constexpr double kPi = 3.14159265358979323846;
// FUN_00754A20: atan(LevelHeight / 256), LevelHeight = 104 leptons/level.
constexpr double kRampPitch = 0.38588266939807375;  // atan(104/256)
// Steeper set (FUN_00754A50 / register ebx in FUN_00754CB0): ~atan(2*LH/cell).
constexpr double kRampPitchSteep = 0.6857295104989298;  // atan(208/256)

// Tilt-axis yaw (rad) per SlopeIndex for the FUN_007559B0 table at 0xB45188.
// Values are the first arg to FUN_005AE6F0 in FUN_00754CB0 (0x4096cac1 = 270°,
// 0x40490e56 = 180°, 0x3fc90e56 = 90°, 0, then 225°/135°/45°/315° repeats).
constexpr double kSlopeYaw[17] = {
    0.0,
    4.712250232696533,   // 1: 270°
    3.1414999961853027,  // 2: 180°
    1.5707499980926514,  // 3:  90°
    0.0,                 // 4:   0°
    3.926875114440918,   // 5: 225°
    2.3561251163482666,  // 6: 135°
    0.7853749990463257,  // 7:  45°
    5.49762487411499,    // 8: 315°
    3.926875114440918,   // 9
    2.3561251163482666,  // 10
    0.7853749990463257,  // 11
    5.49762487411499,    // 12
    3.926875114440918,   // 13
    2.3561251163482666,  // 14
    0.7853749990463257,  // 15
    5.49762487411499,    // 16
};

// Which pitch register FUN_00754CB0 pushed: ebx (steep) vs ebp (single).
constexpr bool kSlopeSteep[17] = {
    false,
    true,  true,  true,  true,   // 1-4 ebx
    false, false, false, false,  // 5-8 ebp
    false, false, false, false,  // 9-12 ebp
    true,  true,  true,  true,   // 13-16 ebx
};

}  // namespace

double slope_tilt_pitch(int slope_index, double body_facing_u16) {
    if (slope_index <= 0 || slope_index > 16) return 0.0;
    const double p = kSlopeSteep[slope_index] ? kRampPitchSteep : kRampPitch;
    const double axis = kSlopeYaw[slope_index];
    // Model-space nose pitch before yaw: full tip when the hull faces along
    // the tilt axis (FUN_0055A7D0 applies M_slope then body RotateZ; the
    // nose component is p * cos(body - axis)).
    constexpr int kFacings = 32;  // kVehicleFacings [RE: voxel-render.md §2]
    const int bi = facing_to_index(body_facing_u16, kFacings);
    const double body = index_to_yaw(bi, kFacings);
    return p * std::cos(body - axis);
}

}  // namespace ra2engine
