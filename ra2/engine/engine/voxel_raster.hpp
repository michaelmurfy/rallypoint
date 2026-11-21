#pragma once
#include <cstdint>
#include <vector>
#include "ra2assets/hva.hpp"
#include "ra2assets/pal.hpp"
#include "ra2assets/vpl.hpp"
#include "ra2assets/vxl.hpp"

namespace ra2engine {

// One pre-rendered voxel-model sprite. `anchor_x/anchor_y` is the pixel the
// MODEL ORIGIN projects to -- the renderer places that pixel on the unit's
// ground point (tile center), exactly like the SHP canvas-center convention
// in draw_unit (iso_renderer.hpp).
struct VoxelSprite {
    int w = 0, h = 0;
    int anchor_x = 0, anchor_y = 0;
    std::vector<uint8_t> rgba;  // w*h*4, alpha 0 = empty
};

// Projection constants (docs/re/voxel-render.md §1, recovered basis
// Rx(-60 deg)*Rz(-45 deg)): screen px per rotated-model unit. 1 voxel
// unit = 1 px -- no cell-metric scaling inside the rasterizer.
constexpr double kIsoX = 0.70711;   // sx = (wx + wy) * kIsoX
constexpr double kIsoY = 0.35355;   // sy = (wx - wy) * kIsoY - wz * kIsoZ
constexpr double kIsoZ = 0.86603;
// World-fixed light direction (voxel-render.md §3): rotated into MODEL
// space per rasterize call (L_model = Rz(-yaw) * kLightWorld), never dotted
// with normals directly as a constant.
constexpr double kLightWorld[3] = {-0.7071, -0.7071, 0.0};

// Facing quantization (docs/re/voxel-render.md §2): `facing_u16` is a
// facing in the original's 16-bit space [0, 65536); n is the cached facing
// count. Nearest-index with wraparound.
int facing_to_index(double facing_u16, int n);
// The yaw angle (radians about model +Z) to rasterize facing index i at.
double index_to_yaw(int i, int n);

// Rasterize every section of `m` (using HVA frame 0's per-section matrices,
// translation scaled by each section's `scale`) rotated by yaw_rad about +Z,
// lit via the normal tables + `vpl`, colored through `pal` (pass a
// pal_for_house-remapped palette for house colors). Deterministic; pure.
VoxelSprite rasterize_model(const ra2assets::VxlModel& m, const ra2assets::HvaModel& hva,
                            double yaw_rad, const ra2assets::Vpl& vpl,
                            const ra2assets::Palette& pal, int hva_frame = 0,   // W3-B HVA frame
                            double pitch_rad = 0.0);   // W4: nose-up pitch (rockets / voxel bullets)

// W2-GA-12 [RE: voxel-render.md fn table, FUN_00755680 -- the VoxLib shadow
// renderer plots every projected column in colour 1 (shadow) at GROUND
// level]: the model's silhouette with each voxel's height flattened to z=0,
// as a black alpha-`alpha` mask in the same anchor space as the hull sprite
// of the same yaw. [RE-FALLBACK: RE-Q9 open -- no light-direction offset;
// 50 % Darken like the SHP shadows.]
VoxelSprite rasterize_shadow(const ra2assets::VxlModel& m, const ra2assets::HvaModel& hva,
                             double yaw_rad, int alpha);

}  // namespace ra2engine
