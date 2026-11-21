#include "doctest.h"
#include "ra2assets/theater.hpp"
#include "ra2assets/mix.hpp"
#include "asset_fixture.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;
using namespace ra2assets;

// ---------------------------------------------------------------------------
// Synthetic .tmp builder: one 1x1 template, cx x cy, single present tile whose
// diamond is filled with a constant palette index. Layout mirrors the on-disk
// TMP TS format exactly:
//   t_tmp_ts_header (16B): cblocks_x, cblocks_y, cx, cy
//   int32 index[count]     : byte offset to each tile's image header (0=absent)
//   t_tmp_image_header (52B): x,y,extra_ofs,z_ofs,extra_z_ofs,x_extra,y_extra,
//                             cx_extra,cy_extra (9*i32), 4B flag word, then
//                             height, terrain_type, ramp_type, 6 radar bytes,
//                             3 pad bytes.
//   diamond pixels (cx*cy/2 bytes)
// ---------------------------------------------------------------------------
static std::vector<uint8_t> make_tmp(int cx, int cy, uint8_t fill) {
    std::vector<uint8_t> b;
    auto p32 = [&](uint32_t v){ for (int i=0;i<4;i++) b.push_back((v>>(8*i))&0xff); };
    // header
    p32(1); p32(1); p32((uint32_t)cx); p32((uint32_t)cy);
    // index table (count=1): image header starts right after this 4-byte entry
    uint32_t hdr_off = 20;                 // 16 (header) + 4 (one index entry)
    p32(hdr_off);
    // image header (52 bytes)
    p32(0); p32(0);                        // x, y
    p32(0);                                // extra_ofs
    p32((uint32_t)(52 + cx*cy/2));         // z_ofs (informational)
    p32(0);                                // extra_z_ofs
    p32(0); p32(0); p32(0); p32(0);        // x_extra, y_extra, cx_extra, cy_extra
    p32(0);                                // flag word (has_extra_data=0)
    b.push_back(0);                        // height
    b.push_back(0);                        // terrain_type
    b.push_back(0);                        // ramp_type
    for (int i=0;i<6;i++) b.push_back(0);  // radar colors
    for (int i=0;i<3;i++) b.push_back(0);  // pad
    // diamond pixels
    for (int i=0;i<cx*cy/2;i++) b.push_back(fill);
    return b;
}

// Synthetic .tmp with an EXTRA image (cliff/slope face). Same 1x1 layout as
// make_tmp, but the image header sets has_extra_data=1, the extra placement
// (x_extra, y_extra) and size (cx_extra, cy_extra), and extra_ofs pointing at
// a plain cx_extra*cy_extra byte block placed RIGHT AFTER the diamond. Note
// extra_ofs is relative to the image header position (offset 20 here), matching
// XCC get_extra_data = data + index[i] + extra_ofs.
static std::vector<uint8_t> make_tmp_extra(int cx, int cy, uint8_t diamond_fill,
                                           int ex, int ey, int cxe, int cye,
                                           uint8_t extra_fill) {
    std::vector<uint8_t> b;
    auto p32 = [&](uint32_t v){ for (int i=0;i<4;i++) b.push_back((v>>(8*i))&0xff); };
    p32(1); p32(1); p32((uint32_t)cx); p32((uint32_t)cy);   // header
    const uint32_t hdr_off = 20;                            // 16 + one index entry
    p32(hdr_off);
    // image header (52 bytes)
    const uint32_t extra_ofs = (uint32_t)(52 + cx*cy/2);    // right after diamond
    p32(0); p32(0);                                         // x, y
    p32(extra_ofs);                                         // extra_ofs
    p32((uint32_t)(52 + cx*cy/2));                          // z_ofs (informational)
    p32(0);                                                 // extra_z_ofs
    p32((uint32_t)ex); p32((uint32_t)ey);                   // x_extra, y_extra
    p32((uint32_t)cxe); p32((uint32_t)cye);                 // cx_extra, cy_extra
    p32(1u);                                                // flag word: has_extra_data=1
    for (int i=0;i<12;i++) b.push_back(0);                  // height..radar..pad
    for (int i=0;i<cx*cy/2;i++) b.push_back(diamond_fill);  // diamond pixels
    for (int i=0;i<cxe*cye;i++) b.push_back(extra_fill);    // extra image (plain rect)
    return b;
}

TEST_CASE("tmp synthetic: single diamond tile, corners transparent") {
    const int cx = 48, cy = 24;
    auto bytes = make_tmp(cx, cy, 5);
    TmpFile t = TmpFile::load(std::move(bytes));
    REQUIRE(t.count() == 1);
    CHECK(t.tile_w() == cx);
    CHECK(t.tile_h() == cy);

    TileImage img = t.decode(0);
    REQUIRE(img.w == cx);
    REQUIRE(img.h == cy);
    REQUIRE(img.indices.size() == (size_t)cx*cy);

    // Reconstruct the expected diamond geometry independently: row y has
    // width = 4*(y+1) for the top half (y<cy/2) then narrows by 4 per row;
    // it is horizontally centered, remaining columns are transparent (0).
    int diamond_pixels = 0, corner_pixels = 0;
    int width = 0;
    for (int y=0; y<cy; y++) {
        if (y < cy/2) width += 4; else width -= 4;
        int left = (cx - width) / 2;
        for (int x=0; x<cx; x++) {
            uint8_t v = img.indices[(size_t)y*cx + x];
            if (x >= left && x < left + width) { CHECK(v == 5); diamond_pixels++; }
            else                                { CHECK(v == 0); corner_pixels++; }
        }
    }
    CHECK(diamond_pixels == cx*cy/2);   // exactly half the rectangle is diamond
    CHECK(corner_pixels == cx*cy/2);    // the other half is transparent corners

    // A plain (no has_extra_data) tile reports no extra image.
    CHECK(t.has_extra(0) == false);
    CHECK(t.tile_x(0) == 0);
    CHECK(t.tile_y(0) == 0);
    TileImage none = t.decode_extra(0);
    CHECK(none.w == 0);
    CHECK(none.h == 0);
    CHECK(none.indices.empty());
}

TEST_CASE("tmp synthetic: extra image (cliff face) decodes + positions") {
    const int cx = 60, cy = 30;
    const int ex = -6, ey = -40;        // cliff face rises up-and-left of the diamond
    const int cxe = 24, cye = 40;       // plain rectangle
    auto bytes = make_tmp_extra(cx, cy, 3, ex, ey, cxe, cye, 17);
    TmpFile t = TmpFile::load(std::move(bytes));
    REQUIRE(t.count() == 1);

    // The base diamond still decodes exactly as before (unaffected by extra).
    TileImage base = t.decode(0);
    REQUIRE(base.w == cx);
    REQUIRE(base.h == cy);
    CHECK(std::count(base.indices.begin(), base.indices.end(), (uint8_t)3) == cx*cy/2);

    // Extra image is present, correctly sized, and constant-filled.
    CHECK(t.has_extra(0) == true);
    CHECK(t.extra_x(0) == ex);          // signed offsets read back exactly
    CHECK(t.extra_y(0) == ey);
    CHECK(t.tile_x(0) == 0);            // base diamond offset (reference origin)
    CHECK(t.tile_y(0) == 0);

    TileImage extra = t.decode_extra(0);
    REQUIRE(extra.w == cxe);
    REQUIRE(extra.h == cye);
    REQUIRE(extra.indices.size() == (size_t)cxe * cye);
    CHECK(std::all_of(extra.indices.begin(), extra.indices.end(),
                      [](uint8_t v){ return v == 17; }));
}

TEST_CASE("tmp extra image bounds checks") {
    const int cx = 48, cy = 24;
    // extra_ofs + cx_extra*cy_extra runs past EOF -> decode_extra throws, but
    // the cheap accessors (has_extra / extra_x) still succeed (header is intact).
    {
        auto b = make_tmp_extra(cx, cy, 2, 0, -20, 16, 20, 8);
        // Corrupt cy_extra (image header offset 20+32 = 52) to a huge value so
        // the extra block claims far more bytes than the file holds.
        const size_t cye_ofs = 20 + 32;
        b[cye_ofs+0]=0x00; b[cye_ofs+1]=0x00; b[cye_ofs+2]=0x00; b[cye_ofs+3]=0x40; // 0x40000000
        TmpFile t = TmpFile::load(std::move(b));
        CHECK(t.has_extra(0) == true);
        CHECK_THROWS_AS(t.decode_extra(0), std::runtime_error);
    }
    // Non-positive extra dimensions are rejected.
    {
        auto b = make_tmp_extra(cx, cy, 2, 0, -20, 16, 20, 8);
        const size_t cxe_ofs = 20 + 28;        // cx_extra field
        b[cxe_ofs+0]=0; b[cxe_ofs+1]=0; b[cxe_ofs+2]=0; b[cxe_ofs+3]=0;   // cx_extra = 0
        TmpFile t = TmpFile::load(std::move(b));
        CHECK_THROWS_AS(t.decode_extra(0), std::runtime_error);
    }
    // Out-of-range tile index throws on every extra accessor.
    {
        auto t = TmpFile::load(make_tmp_extra(cx, cy, 2, 0, -20, 16, 20, 8));
        CHECK_THROWS_AS(t.has_extra(1), std::runtime_error);
        CHECK_THROWS_AS(t.decode_extra(2), std::runtime_error);
        CHECK_THROWS_AS(t.extra_x(-1), std::runtime_error);
    }
}

TEST_CASE("tmp bounds checks reject malformed input") {
    // Too small for even the header.
    CHECK_THROWS_AS(TmpFile::load(std::vector<uint8_t>{1,2,3}), std::runtime_error);

    // Valid header claiming an index entry that points past EOF -> decode throws.
    {
        auto b = make_tmp(48, 24, 7);
        // Corrupt the single index entry (at byte 16) to a huge offset.
        b[16] = 0xF0; b[17] = 0xFF; b[18] = 0xFF; b[19] = 0x7F;
        TmpFile t = TmpFile::load(std::move(b));
        REQUIRE(t.count() == 1);
        CHECK_THROWS_AS(t.decode(0), std::runtime_error);
    }

    // Out-of-range tile index throws.
    {
        auto t = TmpFile::load(make_tmp(48, 24, 3));
        CHECK_THROWS_AS(t.decode(1), std::runtime_error);
        CHECK_THROWS_AS(t.decode(-1), std::runtime_error);
    }
}

TEST_CASE("tmp absent index entry decodes to all-transparent tile") {
    // Build a 1x2 template where the second tile's index entry is 0 (absent).
    std::vector<uint8_t> b;
    auto p32 = [&](uint32_t v){ for (int i=0;i<4;i++) b.push_back((v>>(8*i))&0xff); };
    const int cx=48, cy=24, count=2;
    p32(1); p32(2); p32(cx); p32(cy);      // cblocks_x=1, cblocks_y=2
    uint32_t hdr_off = 16 + 4*count;       // first image header after index table
    p32(hdr_off);                          // tile 0 present
    p32(0);                                // tile 1 absent
    // image header for tile 0 (52 bytes), diamond filled with 9
    p32(0); p32(0); p32(0); p32(52+cx*cy/2); p32(0);
    p32(0); p32(0); p32(0); p32(0);
    p32(0);
    for (int i=0;i<12;i++) b.push_back(0);
    for (int i=0;i<cx*cy/2;i++) b.push_back(9);
    TmpFile t = TmpFile::load(std::move(b));
    REQUIRE(t.count() == 2);
    TileImage a = t.decode(0);
    TileImage z = t.decode(1);
    CHECK(std::count(a.indices.begin(), a.indices.end(), (uint8_t)9) == cx*cy/2);
    // Absent tile -> full cx*cy buffer of zeros.
    REQUIRE(z.indices.size() == (size_t)cx*cy);
    CHECK(std::all_of(z.indices.begin(), z.indices.end(), [](uint8_t v){ return v==0; }));
    // An absent tile reports no extra image and its decode_extra is empty.
    CHECK(t.has_extra(1) == false);
    CHECK(t.decode_extra(1).indices.empty());
}

// ---------------------------------------------------------------------------
// Guarded real-tile oracle test: compares TmpFile::decode(i) against ground
// truth produced by XCC's own decode_tile (built verbatim, OUTSIDE this repo,
// into xcc_tmp_dump). Skips gracefully if the oracle set is absent.
//
// oracle/tmp_manifest.txt: one template per line, whitespace-separated:
//   <mix-name-relative-to-install> <tmp-path-within-mix> <tile-dir-under-oracle>
// e.g.  ra2.mix isotemp.mix/clear01.tem tmp/clear01
// oracle/<tile-dir>/tileNN.raw : cx*cy indexed bytes per tile (XCC decode_tile
// output; absent tiles are cx*cy zeros).
// ---------------------------------------------------------------------------
static std::vector<uint8_t> slurp_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

static std::vector<uint8_t> read_path(const MixFile& m, const std::string& path) {
    size_t s = path.find('/');
    if (s == std::string::npos) {
        const MixEntry* e = m.find(path);
        REQUIRE(e != nullptr);
        return m.read(*e);
    }
    MixFile inner = m.open_nested(path.substr(0, s));
    return read_path(inner, path.substr(s+1));
}

TEST_CASE("tmp tile output matches XCC oracle byte-for-byte") {
    const char* od = std::getenv("RA2_ORACLE_DIR");
    fs::path base = od ? od : "D:\\ra2-android\\oracle";
    fs::path manifest = base / "tmp_manifest.txt";
    if (!fs::exists(manifest)) { MESSAGE("no tmp oracle manifest; skipped"); return; }
    const char* id = std::getenv("RA2_INSTALL_DIR");
    fs::path install = id ? id : "D:\\Red Alert 2";
    if (!fs::exists(install / "ra2.mix")) {
        MESSAGE("install absent; skipped (oracle files present but no game mixes)");
        return;
    }

    std::ifstream mf(manifest);
    std::string mixname, tmppath, tiledir;
    int tiles_checked = 0;
    while (mf >> mixname >> tmppath >> tiledir) {
        // Borrow the shared, load-once mix (asset_fixture): the manifest names
        // ra2.mix on every line, so this reads/opens it once instead of once
        // per line. REQUIRE_FALSE kept (now on the shared bytes) so the count
        // is unchanged.
        REQUIRE_FALSE(ra2fixture::mix_bytes_by_name(mixname).empty());
        const MixFile& m = ra2fixture::mix_by_name(mixname);
        TmpFile t = TmpFile::load(read_path(m, tmppath));
        for (int i = 0; i < t.count(); i++) {
            char fn[32]; std::snprintf(fn, sizeof fn, "tile%02d.raw", i);
            fs::path fp = base / tiledir / fn;
            if (!fs::exists(fp)) continue;
            auto want = slurp_file(fp);
            TileImage got = t.decode(i);
            REQUIRE(got.indices.size() == want.size());
            CHECK(std::equal(got.indices.begin(), got.indices.end(), want.begin()));
            tiles_checked++;
        }
    }
    CHECK(tiles_checked > 0);   // a manifest exercising zero tiles is a failure
}
