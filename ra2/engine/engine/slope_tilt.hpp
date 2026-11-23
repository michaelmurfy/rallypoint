#pragma once
// Presentation-only vehicle tilt on TMP ramps (gamemd parity backlog T1.2).
//
// [RE: DriveLocomotionClass Draw_Matrix FUN_004B0410 -> FUN_0055A7D0 reads
// CellClass SlopeIndex (+0x11C, TMP hdr +0x2A), copies the slope Matrix3D via
// FUN_007559B0 (table 0xB45188, 48 bytes x SlopeIndex), then RotateZ by body
// facing. Table built in FUN_00754CB0: index 0 = identity (FUN_005AE860);
// 1..16 = FUN_005AE6F0(yaw, pitch) = Z(yaw)*X(pitch)*Z(-yaw). Pitch magnitude
// FUN_00754A20 atan(LevelHeight/256) with Unsorted::LevelHeight 104.]

#include <cstdint>

namespace ra2engine {

// Nose-up pitch (radians) for a Drive/Ship voxel hull on a cell with the given
// TMP SlopeIndex and body facing (16-bit facing space). 0 on flat / unknown.
// Does not touch simulation state.
double slope_tilt_pitch(int slope_index, double body_facing_u16);

}  // namespace ra2engine
