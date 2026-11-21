#include "doctest.h"
#include "voxel_raster.hpp"
#include "ra2assets/vxl.hpp"
#include "ra2assets/hva.hpp"
#include "ra2assets/vpl.hpp"
#include "ra2assets/mix.hpp"
#include "asset_fixture.hpp"
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

using ra2assets::VxlModel;
using ra2assets::VxlSection;
using ra2assets::HvaModel;
using ra2assets::HvaSectionXform;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {
// One 1x1x1-section model holding a single voxel with the given color/normal,
// identity transform, bounds centered on the origin (so the voxel center IS
// the model origin), normals_mode 2.
VxlModel single_voxel_model(uint8_t color, uint8_t normal) {
    VxlModel m;
    VxlSection s;
    s.name = "T";
    s.size_x = s.size_y = s.size_z = 1;
    s.scale = 1.0f;
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 4; ++c) s.transform[r][c] = (r == c) ? 1.f : 0.f;
    for (int a = 0; a < 3; ++a) { s.min_bounds[a] = -0.5f; s.max_bounds[a] = 0.5f; }
    s.normals_mode = 2;
    s.voxels = { {color, normal, true} };
    m.sections.push_back(s);
    return m;
}
HvaModel identity_hva() {
    HvaModel h;
    h.frame_count = 1;
    h.section_names = {"T"};
    HvaSectionXform x{};
    for (int r = 0; r < 3; ++r) for (int c = 0; c < 4; ++c) x.transform[r][c] = (r == c) ? 1.f : 0.f;
    h.frames = {{x}};
    return h;
}
// Identity-ish vpl: every table row is the identity map, palette gray ramp.
ra2assets::Vpl identity_vpl() {
    std::vector<uint8_t> raw(16 + 768 + 32 * 256, 0);
    raw[8] = 32;  // section_count
    for (int i = 0; i < 256; ++i) {
        raw[16 + i * 3 + 0] = raw[16 + i * 3 + 1] = raw[16 + i * 3 + 2] =
            static_cast<uint8_t>(i / 4);  // 6-bit gray ramp
        for (int t = 0; t < 32; ++t) raw[16 + 768 + t * 256 + i] = static_cast<uint8_t>(i);
    }
    return ra2assets::Vpl::load(raw.data(), raw.size());
}
ra2assets::Palette gray_pal() {
    // Palette::load expands <<2; feed the same gray ramp for consistency.
    uint8_t raw[768];
    for (int i = 0; i < 256; ++i) raw[i*3] = raw[i*3+1] = raw[i*3+2] = static_cast<uint8_t>(i / 4);
    return ra2assets::Palette::load(raw, 768);
}
int opaque_count(const ra2engine::VoxelSprite& s) {
    int n = 0;
    for (size_t i = 3; i < s.rgba.size(); i += 4) if (s.rgba[i] != 0) ++n;
    return n;
}
}  // namespace

TEST_CASE("voxel_raster: single voxel lands on the anchor") {
    auto spr = ra2engine::rasterize_model(single_voxel_model(100, 0), identity_hva(),
                                          0.0, identity_vpl(), gray_pal());
    REQUIRE(spr.w > 0);
    REQUIRE(spr.h > 0);
    CHECK(opaque_count(spr) >= 1);
    // The voxel center is the model origin, so its pixels must cover the
    // anchor point (within the 1-voxel footprint).
    bool anchor_covered = false;
    for (int dy = -2; dy <= 2 && !anchor_covered; ++dy)
        for (int dx = -2; dx <= 2 && !anchor_covered; ++dx) {
            const int x = spr.anchor_x + dx, y = spr.anchor_y + dy;
            if (x < 0 || y < 0 || x >= spr.w || y >= spr.h) continue;
            if (spr.rgba[(static_cast<size_t>(y) * spr.w + x) * 4 + 3] != 0) anchor_covered = true;
        }
    CHECK(anchor_covered);
}

TEST_CASE("voxel_raster: yaw rotation moves an off-axis voxel as hand-computed") {
    // Two-voxel column along model +X: bounds [0,2] x [0,1] x [0,1], 2x1x1.
    // Corner-anchored positions (voxel-render.md §1): voxel (1,0,0) sits at
    // local (1, 0, 0) exactly -- pure +X, so its projected offset from the
    // anchor is sign-definite under the recovered basis (no y/z bleed).
    VxlModel m = single_voxel_model(100, 0);
    m.sections[0].size_x = 4;
    m.sections[0].min_bounds[0] = 0.0f; m.sections[0].max_bounds[0] = 4.0f;
    m.sections[0].min_bounds[1] = 0.0f; m.sections[0].max_bounds[1] = 1.0f;
    m.sections[0].min_bounds[2] = 0.0f; m.sections[0].max_bounds[2] = 1.0f;
    m.sections[0].voxels = { {0, 0, false}, {0, 0, false}, {0, 0, false},
                             {100, 0, true} };  // only x=3 present: local (3, 0, 0)
    auto at0 = ra2engine::rasterize_model(m, identity_hva(), 0.0, identity_vpl(), gray_pal());
    auto at180 = ra2engine::rasterize_model(m, identity_hva(), M_PI, identity_vpl(), gray_pal());
    // At yaw 0 the voxel is at world (3,0,0) -> sx = +2.12, sy = +1.06 px
    // (down-right, screen East -- whole-pixel offsets survive rounding); at
    // yaw pi it is (-3,0,0) -> sx = -2.12, sy = -1.06. Find each sprite's
    // opaque centroid relative to its anchor and check the sign flip.
    auto centroid_rel = [](const ra2engine::VoxelSprite& s, double& cx, double& cy) {
        double sx = 0, sy = 0; int n = 0;
        for (int y = 0; y < s.h; ++y)
            for (int x = 0; x < s.w; ++x)
                if (s.rgba[(static_cast<size_t>(y) * s.w + x) * 4 + 3] != 0) { sx += x; sy += y; ++n; }
        REQUIRE(n > 0);
        cx = sx / n - s.anchor_x;
        cy = sy / n - s.anchor_y;
    };
    double x0, y0, x1, y1;
    centroid_rel(at0, x0, y0);
    centroid_rel(at180, x1, y1);
    CHECK(x0 > 0.0);       // +X model axis projects right-and-down (diamond shear)
    CHECK(y0 > 0.0);
    CHECK(x1 < 0.0);       // opposite yaw mirrors both
    CHECK(y1 < 0.0);
}

TEST_CASE("voxel_raster: normal index selects the vpl row") {
    // A vpl whose table t maps EVERY color to t (so the output pixel value
    // reveals which lighting level was chosen). Combined with gray_pal (which
    // maps palette index j -> gray value j, because (j/4)<<2 == j for j%4==0),
    // the first opaque pixel's R channel equals level*8.
    std::vector<uint8_t> raw(16 + 768 + 32 * 256, 0);
    raw[8] = 32;
    for (int i = 0; i < 256; ++i) {
        raw[16 + i * 3] = raw[16 + i * 3 + 1] = raw[16 + i * 3 + 2] = static_cast<uint8_t>(i / 4);
        for (int t = 0; t < 32; ++t) raw[16 + 768 + t * 256 + i] = static_cast<uint8_t>(t * 8);
    }
    auto vpl = ra2assets::Vpl::load(raw.data(), raw.size());
    // Hand-computed lighting at yaw 0 (voxel-render.md §3). The world light is
    // rotated into model space: L_model = Rz(-0)*(-0.7071,-0.7071,0) =
    // (-0.7071,-0.7071,0); H_model = normalize(L+(0,0,1)) = (-0.5,-0.5,0.7071).
    // level = trunc((max(0,n.L) + s)*16), s = h/(3-2h), h = max(0,n.H), clamped
    // to [0, section_count-1]. Chosen indices 4 and 22 of kNormals2 (brief's
    // 0/18 both give level 0 -- both normals point away from the light):
    //
    //   n[4]  = (-0.171400994, -0.572709978, -0.80163902):
    //     d = 0.7071*(0.171400994+0.572709978) = 0.52616; h < 0 -> s = 0
    //     level = trunc(0.52616*16) = trunc(8.4186) = 8  -> gray 8*8 = 64
    //   n[22] = (-0.705051005, -0.687775016,  0.172828004):
    //     d = 0.7071*(0.705051005+0.687775016) = 0.98487
    //     h = 0.5*(1.392826) + 0.172828*0.70711 = 0.81863; s = h/(3-2h) = 0.60070
    //     level = trunc((0.98487+0.60070)*16) = trunc(25.3691) = 25 -> gray 200
    //
    // Both are comfortably clear of an integer truncation boundary (8.42, 25.37).
    auto a = ra2engine::rasterize_model(single_voxel_model(1, 4), identity_hva(), 0.0, vpl, gray_pal());
    auto b = ra2engine::rasterize_model(single_voxel_model(1, 22), identity_hva(), 0.0, vpl, gray_pal());
    auto first_opaque_r = [](const ra2engine::VoxelSprite& s) -> int {
        for (size_t i = 3; i < s.rgba.size(); i += 4) if (s.rgba[i] != 0) return s.rgba[i - 3];
        return -1;
    };
    const int ra = first_opaque_r(a), rb = first_opaque_r(b);
    REQUIRE(ra >= 0);
    REQUIRE(rb >= 0);
    CHECK(ra != rb);  // different normals -> different lighting rows
    CHECK(ra == 64);  // level 8
    CHECK(rb == 200); // level 25
}

TEST_CASE("voxel_raster: facing quantization round-trips") {
    const int n = 32;  // [RE: voxel-render.md §2]
    CHECK(ra2engine::facing_to_index(0.0, n) == 0);
    CHECK(ra2engine::facing_to_index(65536.0 / n * 5, n) == 5);
    CHECK(ra2engine::facing_to_index(65536.0 - 1.0, n) == 0);  // wraps to nearest
    for (int i = 0; i < n; ++i)
        CHECK(ra2engine::facing_to_index(65536.0 * i / n, n) == i);
}

// ---------------------------------------------------------------------------
// FNV-1a 32-bit over the sprite bytes (regression fingerprint).
// ---------------------------------------------------------------------------
static uint32_t fnv1a(const std::vector<uint8_t>& v) {
    uint32_t h = 2166136261u;
    for (uint8_t b : v) { h ^= b; h *= 16777619u; }
    return h;
}

TEST_CASE("voxel_raster: real htnk hull renders deterministically") {
    // Guarded (same mix-chain/guard pattern as test_vxl.cpp / test_vpl.cpp).
    // Skips cleanly when the install is absent.
    const char* id = std::getenv("RA2_INSTALL_DIR");
    fs::path install = id ? id : "D:\\Red Alert 2";
    if (!fs::exists(install / "ra2.mix")) {
        MESSAGE("RA2 install absent; skipped");
        return;
    }
    // Borrow the shared, load-once ra2.mix (asset_fixture) instead of
    // re-slurping the ~268 MB archive; open_nested still exercises the real
    // path. REQUIRE_FALSE kept (now on the shared bytes) so the count is unchanged.
    REQUIRE_FALSE(ra2fixture::ra2_bytes().empty());
    const ra2assets::MixFile& outer = ra2fixture::ra2_mix();
    // htnk.vxl + htnk.hva + voxels.vpl live in ra2.mix -> local.mix.
    ra2assets::MixFile local = outer.open_nested("local.mix");
    const ra2assets::MixEntry* ve = local.find("htnk.vxl");
    const ra2assets::MixEntry* he = local.find("htnk.hva");
    const ra2assets::MixEntry* pe = local.find("voxels.vpl");
    REQUIRE(ve != nullptr);
    REQUIRE(he != nullptr);
    REQUIRE(pe != nullptr);
    auto vxl_raw = local.read(*ve);
    auto hva_raw = local.read(*he);
    auto vpl_raw = local.read(*pe);
    VxlModel m = ra2assets::load_vxl(vxl_raw.data(), vxl_raw.size());
    HvaModel hva = ra2assets::load_hva(hva_raw.data(), hva_raw.size());
    ra2assets::Vpl vpl = ra2assets::Vpl::load(vpl_raw.data(), vpl_raw.size());
    // unittem.pal comes from ra2.mix -> cache.mix.
    ra2assets::MixFile cache = outer.open_nested("cache.mix");
    const ra2assets::MixEntry* pale = cache.find(std::string("unittem.pal"));
    REQUIRE(pale != nullptr);
    auto pal_raw = cache.read(*pale);
    ra2assets::Palette pal = ra2assets::Palette::load(pal_raw.data(), pal_raw.size());

    auto s1 = ra2engine::rasterize_model(m, hva, 0.0, vpl, pal);
    auto s2 = ra2engine::rasterize_model(m, hva, 0.0, vpl, pal);
    CHECK(s1.rgba == s2.rgba);              // byte-deterministic
    CHECK(opaque_count(s1) > 200);          // a 46x34x12 tank is not a speck
    CHECK(s1.w < 200);
    CHECK(s1.h < 200);                      // and not exploded
    // FNV-1a fingerprint (regression lock) -- pinned after visual calibration
    // vs the original (Task 6, docs/re/voxel-render.md §1-4: the recovered
    // projection/lighting/palette produce a coherent Rhino hull, verified on
    // screen). Re-pin ONLY with a new visual check. Observed sprite: w=61 h=43
    // opaque=1038 anchor=(27,24).
    CHECK(fnv1a(s1.rgba) == 1209756769u);  // 0x481b6c61
}
