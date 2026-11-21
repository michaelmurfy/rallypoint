#include "doctest.h"
#include "facing_cache.hpp"
#include "iso_renderer.hpp"  // pal_for_house (real-Rhino case)
#include "voxel_raster.hpp"
#include "ra2assets/hva.hpp"
#include "ra2assets/mix.hpp"
#include "ra2assets/pal.hpp"
#include "ra2assets/vpl.hpp"
#include "ra2assets/vxl.hpp"
#include "asset_fixture.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

using ra2assets::HvaModel;
using ra2assets::HvaSectionXform;
using ra2assets::VxlModel;
using ra2assets::VxlSection;

namespace {
// Synthetic single-voxel model/vpl/palette helpers copied verbatim from
// tests/test_voxel_raster.cpp's anonymous namespace (they are file-local
// there; the brief's Step 1 note directs copying the three construction
// helpers so this test needs no install to exercise build_facing_set).
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
ra2assets::Vpl identity_vpl() {
    std::vector<uint8_t> raw(16 + 768 + 32 * 256, 0);
    raw[8] = 32;  // section_count
    for (int i = 0; i < 256; ++i) {
        raw[16 + i * 3 + 0] = raw[16 + i * 3 + 1] = raw[16 + i * 3 + 2] =
            static_cast<uint8_t>(i / 4);
        for (int t = 0; t < 32; ++t) raw[16 + 768 + t * 256 + i] = static_cast<uint8_t>(i);
    }
    return ra2assets::Vpl::load(raw.data(), raw.size());
}
ra2assets::Palette gray_pal() {
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

TEST_CASE("facing_cache: N sprites, each equal to a direct rasterize call") {
    VxlModel model = single_voxel_model(100, 0);
    HvaModel hva = identity_hva();
    ra2assets::Vpl vpl = identity_vpl();
    ra2assets::Palette pal = gray_pal();

    auto set = ra2engine::build_facing_set(model, hva, 32, vpl, pal);
    REQUIRE(set.sprites.size() == 32);
    for (int i = 0; i < 32; ++i) {
        CHECK(set.sprites[i].w > 0);
        auto direct = ra2engine::rasterize_model(model, hva, ra2engine::index_to_yaw(i, 32), vpl, pal);
        CHECK(set.sprites[i].rgba == direct.rgba);  // no drift cache vs direct
        CHECK(set.sprites[i].anchor_x == direct.anchor_x);
        CHECK(set.sprites[i].anchor_y == direct.anchor_y);
    }
}

TEST_CASE("facing_cache: real Rhino parts all render non-empty at every facing") {
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
    ra2assets::MixFile local = outer.open_nested("local.mix");

    auto load_model = [&](const char* vxl_name, const char* hva_name,
                          VxlModel& m, HvaModel& hva) {
        const ra2assets::MixEntry* ve = local.find(vxl_name);
        const ra2assets::MixEntry* he = local.find(hva_name);
        REQUIRE(ve != nullptr);
        REQUIRE(he != nullptr);
        auto vxl_raw = local.read(*ve);
        auto hva_raw = local.read(*he);
        m = ra2assets::load_vxl(vxl_raw.data(), vxl_raw.size());
        hva = ra2assets::load_hva(hva_raw.data(), hva_raw.size());
    };

    VxlModel hull, tur, barl;
    HvaModel hull_h, tur_h, barl_h;
    load_model("htnk.vxl", "htnk.hva", hull, hull_h);
    load_model("htnktur.vxl", "htnktur.hva", tur, tur_h);
    load_model("htnkbarl.vxl", "htnkbarl.hva", barl, barl_h);

    const ra2assets::MixEntry* pe = local.find("voxels.vpl");
    REQUIRE(pe != nullptr);
    auto vpl_raw = local.read(*pe);
    ra2assets::Vpl vpl = ra2assets::Vpl::load(vpl_raw.data(), vpl_raw.size());

    ra2assets::MixFile cache = outer.open_nested("cache.mix");
    const ra2assets::MixEntry* pale = cache.find(std::string("unittem.pal"));
    REQUIRE(pale != nullptr);
    auto pal_raw = cache.read(*pale);
    ra2assets::Palette pal = ra2assets::Palette::load(pal_raw.data(), pal_raw.size());

    struct Part { const char* name; VxlModel* m; HvaModel* h; };
    Part parts[] = {{"hull", &hull, &hull_h}, {"turret", &tur, &tur_h}, {"barrel", &barl, &barl_h}};
    for (int house = 0; house <= 1; ++house) {
        const ra2assets::Palette house_pal = ra2engine::pal_for_house(pal, house);
        for (const Part& p : parts) {
            auto set = ra2engine::build_facing_set(*p.m, *p.h, ra2engine::kVehicleFacings,
                                                   vpl, house_pal);
            REQUIRE(set.sprites.size() == static_cast<size_t>(ra2engine::kVehicleFacings));
            for (const auto& s : set.sprites) {
                CHECK(s.w > 0);
                CHECK(opaque_count(s) > 0);
            }
        }
    }
}
