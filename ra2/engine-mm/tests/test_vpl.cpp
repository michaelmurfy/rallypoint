#include "doctest.h"
#include "ra2assets/vpl.hpp"
#include "ra2assets/mix.hpp"
#include "asset_fixture.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace ra2assets;

namespace {
// 16-byte header (remap_start, remap_end, section_count, unused as u32 LE),
// 768 palette bytes, then n*256 table bytes.
std::vector<uint8_t> synth_vpl(uint32_t sections) {
    std::vector<uint8_t> v(16 + 768 + sections * 256, 0);
    auto put32 = [&](size_t at, uint32_t x) {
        v[at] = x & 0xff; v[at+1] = (x >> 8) & 0xff;
        v[at+2] = (x >> 16) & 0xff; v[at+3] = (x >> 24) & 0xff;
    };
    put32(0, 16);   // remap_start
    put32(4, 31);   // remap_end
    put32(8, sections);
    // palette: entry 1 = raw (10, 20, 30) -> <<2 = (40, 80, 120)
    v[16 + 3] = 10; v[16 + 4] = 20; v[16 + 5] = 30;
    // table 0 entry 5 = 42; last table's entry 255 = 7. Guard sections == 0:
    // there is no table 0 (or "last table") to write in that case -- it's
    // exercised solely to confirm section_count == 0 is rejected, and
    // writing anyway (or computing (sections - 1) with sections a uint32_t)
    // would read/write out of the vector's bounds.
    if (sections > 0) {
        v[16 + 768 + 5] = 42;
        v[16 + 768 + (sections - 1) * 256 + 255] = 7;
    }
    return v;
}
}  // namespace

TEST_CASE("vpl: synthetic parse") {
    auto v = synth_vpl(32);
    ra2assets::Vpl p = ra2assets::Vpl::load(v.data(), v.size());
    CHECK(p.remap_start == 16);
    CHECK(p.remap_end == 31);
    CHECK(p.section_count == 32);
    CHECK(p.tables.size() == 32);
    CHECK(p.palette[1].r == 40);   // 6-bit <<2 expansion, like .pal
    CHECK(p.palette[1].g == 80);
    CHECK(p.palette[1].b == 120);
    CHECK(p.tables[0][5] == 42);
    CHECK(p.tables[31][255] == 7);
}

TEST_CASE("vpl: truncation and bad counts throw") {
    auto v = synth_vpl(32);
    CHECK_THROWS_AS(ra2assets::Vpl::load(v.data(), 15), std::runtime_error);       // no header
    CHECK_THROWS_AS(ra2assets::Vpl::load(v.data(), v.size() - 1), std::runtime_error);  // short table
    auto z = synth_vpl(0);   // section_count == 0
    CHECK_THROWS_AS(ra2assets::Vpl::load(z.data(), z.size()), std::runtime_error);
    auto big = synth_vpl(2);  // header claims 2 but hand it a huge count
    big[8] = 0xff; big[9] = 0xff; big[10] = 0xff; big[11] = 0xff;
    CHECK_THROWS_AS(ra2assets::Vpl::load(big.data(), big.size()), std::runtime_error);
}

TEST_CASE("vpl: real voxels.vpl pins") {
    // Same guarded pattern as test_vxl.cpp's real-asset test: resolve the
    // install dir from RA2_INSTALL_DIR (default "D:\Red Alert 2"), skip
    // cleanly if ra2.mix is absent. voxels.vpl lives in ra2.mix -> local.mix.
    const char* id = std::getenv("RA2_INSTALL_DIR");
    fs::path install = id ? id : "D:\\Red Alert 2";
    if (!fs::exists(install / "ra2.mix")) {
        MESSAGE("RA2 install absent; skipping");
        return;
    }

    // Borrow the shared, load-once ra2.mix (asset_fixture) instead of
    // re-slurping the ~268 MB archive; open_nested still exercises the real
    // path. The REQUIRE_FALSE is kept (now on the shared bytes) so this case's
    // assertion count is unchanged.
    REQUIRE_FALSE(ra2fixture::ra2_bytes().empty());
    const MixFile& outer = ra2fixture::ra2_mix();
    MixFile local = outer.open_nested("local.mix");

    const MixEntry* e = local.find("voxels.vpl");
    REQUIRE(e != nullptr);
    std::vector<uint8_t> bytes = local.read(*e);
    REQUIRE(bytes.size() == 8976);   // byte-verified size in this install

    ra2assets::Vpl p = ra2assets::Vpl::load(bytes.data(), bytes.size());
    CHECK(bytes.size() == 16 + 768 + p.section_count * 256);
    CHECK(p.section_count == 32);
    CHECK(p.tables.size() == 32);

    // Identity spot-check [RE: voxel-render.md §3/§4 names table row 16 as
    // "neutral/full diffuse", inferred from the lighting formula
    // level = ftol(d*16.0) reaching its d=1 maximum at 16]. Checked directly
    // against the real 8,976-byte file: row 16 has only 13 fixed points
    // (table[16][i] == i) out of 256 -- NOT an identity table. Scanning all
    // 32 rows shows a single sharp, isolated peak at row 8 (199 fixed
    // points; neighbors are 96 and 104, everything else <50), which is the
    // one that actually behaves like "most colors map to themselves". This
    // contradicts the doc's row-16 claim -- see the concern filed in
    // task-2-report.md recommending docs/re/voxel-render.md's §3/§4 be
    // revisited (either the level formula's divisor, or the row/level
    // correspondence, appears off by a factor of 2 relative to the real
    // file). Pinning against the empirically-verified row so this stays a
    // real regression guard rather than an assertion that can never pass.
    REQUIRE(p.tables.size() > 8);
    int fixed_points = 0;
    for (int i = 0; i < 256; i++)
        if (p.tables[8][i] == i) fixed_points++;
    CHECK(fixed_points > 150);
}
