#include "doctest.h"
#include "ra2assets/hva.hpp"
#include "ra2assets/vxl.hpp"
#include "ra2assets/mix.hpp"
#include "asset_fixture.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace ra2assets;

// ---------------------------------------------------------------------------
// Synthetic .hva builder. Layout (little-endian, packed; byte-verified against
// the real 88-byte htnk.hva = 16 + 4 + 4 + 16 + 48):
//   char path[16]                          @ 0   (content ignored by the loader)
//   u32  frame_count                       @ 16
//   u32  section_count                     @ 20
//   char section_names[section_count][16]  @ 24
//   f32  matrices[frames][sections][3][4]  frame-major, each matrix row-major
// Matrix value at (frame f, section s, row r, col c) = 100*f + 10*s + (4*r+c),
// so every slot is distinct and exactly representable in f32.
// ---------------------------------------------------------------------------
static std::vector<uint8_t> make_hva(int frames, int sections) {
    std::vector<uint8_t> b;
    auto p8  = [&](uint8_t v){ b.push_back(v); };
    auto p32 = [&](uint32_t v){ for (int i=0;i<4;i++) p8((uint8_t)((v>>(8*i))&0xff)); };
    auto pf32 = [&](float f){ uint32_t v; std::memcpy(&v, &f, 4); p32(v); };
    auto pname = [&](const std::string& s){
        for (int i=0;i<16;i++) p8(i < (int)s.size() ? (uint8_t)s[i] : 0);
    };
    pname("test.vxl");                       // path (ignored)
    p32((uint32_t)frames);
    p32((uint32_t)sections);
    for (int s=0;s<sections;s++) pname("sec" + std::to_string(s));
    for (int f=0; f<frames; f++)
        for (int s=0; s<sections; s++)
            for (int r=0;r<3;r++)
                for (int c=0;c<4;c++)
                    pf32((float)(100*f + 10*s + 4*r + c));
    return b;
}

TEST_CASE("hva synthetic: names, frame count, frame-major matrices") {
    auto bytes = make_hva(2, 3);
    REQUIRE(bytes.size() == 24u + 16u*3 + 48u*2*3);   // 360
    HvaModel m = load_hva(bytes.data(), bytes.size());
    CHECK(m.frame_count == 2);
    REQUIRE(m.section_names.size() == 3);
    CHECK(m.section_names[0] == "sec0");
    CHECK(m.section_names[1] == "sec1");
    CHECK(m.section_names[2] == "sec2");
    REQUIRE(m.frames.size() == 2);
    REQUIRE(m.frames[0].size() == 3);
    REQUIRE(m.frames[1].size() == 3);
    CHECK(m.frames[0][0].transform[0][0] == 0.0f);
    CHECK(m.frames[0][1].transform[1][2] == 16.0f);    // 10*1 + 4*1+2
    CHECK(m.frames[1][2].transform[2][3] == 131.0f);   // 100 + 20 + 11
    CHECK(m.frames[1][0].transform[0][1] == 101.0f);   // 100 + 0 + 1
}

TEST_CASE("hva synthetic: size mismatches and bad counts throw") {
    auto good = make_hva(2, 3);
    // one byte short
    {
        auto b = good; b.pop_back();
        CHECK_THROWS_AS(load_hva(b.data(), b.size()), std::runtime_error);
    }
    // one byte long
    {
        auto b = good; b.push_back(0);
        CHECK_THROWS_AS(load_hva(b.data(), b.size()), std::runtime_error);
    }
    // smaller than the 24-byte header
    {
        std::vector<uint8_t> b(10, 0);
        CHECK_THROWS_AS(load_hva(b.data(), b.size()), std::runtime_error);
    }
    // zero frames (bytes internally consistent: 24 + 16*3 + 0 matrix bytes)
    {
        auto b = make_hva(0, 3);
        CHECK_THROWS_AS(load_hva(b.data(), b.size()), std::runtime_error);
    }
    // zero sections
    {
        auto b = make_hva(2, 0);
        CHECK_THROWS_AS(load_hva(b.data(), b.size()), std::runtime_error);
    }
}

// ---------------------------------------------------------------------------
// Synthetic .vxl builder: 1 section "TESTSEC", size_x=2, size_y=1, size_z=2.
// Column 0 (x=0,y=0) holds one voxel at z=1 (color 7, normal 3); column 1
// (x=1,y=0) is EMPTY (span start/end entries = 0xFFFFFFFF). Palette entry 7
// is 6-bit (63,0,0), which must expand game-accurately (<<2) to r=252.
//
// Body layout (21 bytes): span starts u32[2] @0, span ends u32[2] @8, span
// data @16: skip=1, count=1, color=7, normal=3, trailing-count=1.
// File = 802 (file header) + 28 (section header) + 21 (body) + 92 (tailer)
//      = 943 bytes — same arithmetic that pins the real htnk.vxl at
//        802 + 28 + 33,628 + 92 = 34,550.
// ---------------------------------------------------------------------------
static std::vector<uint8_t> make_vxl() {
    std::vector<uint8_t> b;
    auto p8  = [&](uint8_t v){ b.push_back(v); };
    auto p32 = [&](uint32_t v){ for (int i=0;i<4;i++) p8((uint8_t)((v>>(8*i))&0xff)); };
    auto pf32 = [&](float f){ uint32_t v; std::memcpy(&v, &f, 4); p32(v); };
    auto pname = [&](const std::string& s){
        for (int i=0;i<16;i++) p8(i < (int)s.size() ? (uint8_t)s[i] : 0);
    };
    // --- file header (802 bytes) ---
    pname("Voxel Animation");        // magic: 15 chars + NUL padding
    p32(1);                          // palette_count
    p32(1);                          // section_header_count
    p32(1);                          // section_tailer_count
    p32(21);                         // body_size
    p8(0x10); p8(0x1f);              // palette remap start/end (as real files)
    for (int i=0;i<256;i++) {        // 768-byte 6-bit palette; entry 7 = (63,0,0)
        if (i == 7) { p8(63); p8(0); p8(0); }
        else        { p8(0);  p8(0); p8(0); }
    }
    // --- section header (28 bytes) ---
    pname("TESTSEC");
    p32(0);                          // limb_number
    p32(1);                          // unknown1 (XCC-written files hold 1)
    p32(0);                          // unknown2 (XCC-written files hold 0)
    // --- body (21 bytes) ---
    p32(0);  p32(0xFFFFFFFFu);       // span starts: col0=0, col1=EMPTY
    p32(4);  p32(0xFFFFFFFFu);       // span ends:   col0=4 (last byte index), col1=EMPTY
    p8(1); p8(1); p8(7); p8(3); p8(1); // col0 data: skip 1, count 1, (7,3), trailing 1
    // --- section tailer (92 bytes) ---
    p32(0);                          // span_start_ofs (body-relative)
    p32(8);                          // span_end_ofs
    p32(16);                         // span_data_ofs
    pf32(0.25f);                     // det_scale (exactly representable)
    for (int i=1;i<=12;i++) pf32((float)i);   // transform rows {1..4},{5..8},{9..12}
    pf32(-1.5f); pf32(-0.5f); pf32(0.0f);     // min_bounds
    pf32( 1.5f); pf32( 0.5f); pf32(2.0f);     // max_bounds
    p8(2); p8(1); p8(2);             // size_x, size_y, size_z
    p8(4);                           // normals_mode (RA2)
    return b;
}

TEST_CASE("vxl synthetic: header, palette, section header + tailer parse") {
    auto bytes = make_vxl();
    REQUIRE(bytes.size() == 943);
    VxlModel m = load_vxl(bytes.data(), bytes.size());

    // Palette entry 7 is 6-bit (63,0,0) -> game-accurate <<2 expansion = 252.
    CHECK(m.palette[7].r == 252);
    CHECK(m.palette[7].g == 0);
    CHECK(m.palette[7].b == 0);
    CHECK(m.palette[7].a == 255);
    CHECK(m.palette[0].r == 0);

    REQUIRE(m.sections.size() == 1);
    const VxlSection& s = m.sections[0];
    CHECK(s.name == "TESTSEC");
    CHECK(s.size_x == 2);
    CHECK(s.size_y == 1);
    CHECK(s.size_z == 2);
    CHECK(s.scale == 0.25f);
    CHECK(s.transform[0][0] == 1.0f);
    CHECK(s.transform[0][3] == 4.0f);
    CHECK(s.transform[1][1] == 6.0f);
    CHECK(s.transform[2][3] == 12.0f);
    CHECK(s.min_bounds[0] == -1.5f);
    CHECK(s.min_bounds[1] == -0.5f);
    CHECK(s.min_bounds[2] == 0.0f);
    CHECK(s.max_bounds[0] == 1.5f);
    CHECK(s.max_bounds[1] == 0.5f);
    CHECK(s.max_bounds[2] == 2.0f);
    CHECK(s.normals_mode == 4);
    CHECK(s.voxels.size() == 4u);            // 2*1*2 cells allocated
}

TEST_CASE("vxl bounds checks reject malformed input") {
    // truncated: smaller than the 802-byte file header
    {
        auto b = make_vxl(); b.resize(100);
        CHECK_THROWS_AS(load_vxl(b.data(), b.size()), std::runtime_error);
    }
    // wrong magic
    {
        auto b = make_vxl(); b[0] = 'W';
        CHECK_THROWS_AS(load_vxl(b.data(), b.size()), std::runtime_error);
    }
    // palette_count != 1
    {
        auto b = make_vxl(); b[16] = 2;
        CHECK_THROWS_AS(load_vxl(b.data(), b.size()), std::runtime_error);
    }
    // tailer_count != header_count
    {
        auto b = make_vxl(); b[24] = 2;
        CHECK_THROWS_AS(load_vxl(b.data(), b.size()), std::runtime_error);
    }
    // total size must equal 802 + N*28 + body_size + N*92 exactly
    {
        auto b = make_vxl(); b.push_back(0);
        CHECK_THROWS_AS(load_vxl(b.data(), b.size()), std::runtime_error);
    }
    // span start TABLE beyond the body: tailer span_start_ofs (abs offset 851,
    // = 802 + 28 + 21) corrupted to 0x40000000
    {
        auto b = make_vxl();
        b[851] = 0x00; b[852] = 0x00; b[853] = 0x00; b[854] = 0x40;
        CHECK_THROWS_AS(load_vxl(b.data(), b.size()), std::runtime_error);
    }
    // span_data_ofs beyond the body (abs offset 859 -> 64 > body_size 21)
    {
        auto b = make_vxl();
        b[859] = 64; b[860] = 0; b[861] = 0; b[862] = 0;
        CHECK_THROWS_AS(load_vxl(b.data(), b.size()), std::runtime_error);
    }
}

TEST_CASE("vxl rejects an implausible section count with a clear error") {
    // An attacker-controlled header count must be rejected by an explicit
    // plausibility cap (mirroring load_hva / AudioIdx::load) with a message that
    // NAMES the bad count -- not left to fall through to the generic size check
    // or to a large speculative allocation. section_header_count is at byte 20
    // and section_tailer_count at byte 24; keep them equal so the count-mismatch
    // guard doesn't fire first.
    auto b = make_vxl();
    auto put32 = [&](size_t o, uint32_t v) {
        for (int i = 0; i < 4; i++) b[o + i] = (uint8_t)((v >> (8 * i)) & 0xff);
    };
    put32(20, 2000000u);
    put32(24, 2000000u);
    bool threw = false;
    std::string msg;
    try { load_vxl(b.data(), b.size()); }
    catch (const std::runtime_error& e) { threw = true; msg = e.what(); }
    CHECK(threw);
    CHECK(msg.find("implausible") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Second synthetic .vxl: 1 section, size 1x1x8, ONE column whose data holds
// two voxel spans separated by skips and ends on a BARE skip (no trailing
// count/duplicate bytes after it):
//   skip 1, count 2, (10,1), (11,2), trailing 2,
//   skip 2, count 1, (12,3),         trailing 1,
//   skip 2                                          -> z = 8 = size_z, column ends
// Voxels present: z=1 (10,1), z=2 (11,2), z=5 (12,3). 13 data bytes; body =
// 4 (starts) + 4 (ends) + 13 = 21; file = 802 + 28 + 21 + 92 = 943 bytes.
// ---------------------------------------------------------------------------
static std::vector<uint8_t> make_vxl_multispan() {
    std::vector<uint8_t> b;
    auto p8  = [&](uint8_t v){ b.push_back(v); };
    auto p32 = [&](uint32_t v){ for (int i=0;i<4;i++) p8((uint8_t)((v>>(8*i))&0xff)); };
    auto pf32 = [&](float f){ uint32_t v; std::memcpy(&v, &f, 4); p32(v); };
    auto pname = [&](const std::string& s){
        for (int i=0;i<16;i++) p8(i < (int)s.size() ? (uint8_t)s[i] : 0);
    };
    // file header (802 bytes)
    pname("Voxel Animation");
    p32(1); p32(1); p32(1); p32(21);            // palette_count, headers, tailers, body_size
    p8(0x10); p8(0x1f);
    for (int i=0;i<768;i++) p8(0);              // all-zero palette
    // section header (28 bytes)
    pname("MULTI");
    p32(0); p32(1); p32(0);
    // body (21 bytes)
    p32(0);                                      // span start[0] = 0
    p32(12);                                     // span end[0]   = 12 (last data byte)
    p8(1); p8(2); p8(10); p8(1); p8(11); p8(2); p8(2);   // span 1: skip,count,2 voxels,trailing
    p8(2); p8(1); p8(12); p8(3); p8(1);                  // span 2: skip,count,1 voxel,trailing
    p8(2);                                               // final bare skip -> z = 8
    // section tailer (92 bytes)
    p32(0);                                      // span_start_ofs
    p32(4);                                      // span_end_ofs
    p32(8);                                      // span_data_ofs
    pf32(1.0f);                                  // det_scale
    for (int i=0;i<12;i++) pf32(0.0f);           // transform
    for (int i=0;i<6;i++) pf32(0.0f);            // min/max bounds
    p8(1); p8(1); p8(8);                         // size_x, size_y, size_z
    p8(4);                                        // normals_mode
    return b;
}

TEST_CASE("vxl span decode: skip/count walk fills the voxel grid") {
    auto bytes = make_vxl();
    VxlModel m = load_vxl(bytes.data(), bytes.size());
    REQUIRE(m.sections.size() == 1);
    const VxlSection& s = m.sections[0];
    REQUIRE(s.voxels.size() == 4u);
    // documented layout: voxels[x + size_x*(y + size_y*z)]
    auto at = [&](int x, int y, int z) -> const VxlVoxel& {
        return s.voxels[(size_t)x + (size_t)s.size_x *
                        ((size_t)y + (size_t)s.size_y * (size_t)z)];
    };
    CHECK(at(0,0,0).present == false);
    CHECK(at(0,0,1).present == true);      // the one voxel: skip 1 then (7,3)
    CHECK(at(0,0,1).color == 7);
    CHECK(at(0,0,1).normal == 3);
    CHECK(at(1,0,0).present == false);     // empty column (0xFFFFFFFF start)
    CHECK(at(1,0,1).present == false);
}

TEST_CASE("vxl span decode: multi-span column with skips, ends on a bare skip") {
    auto bytes = make_vxl_multispan();
    REQUIRE(bytes.size() == 943);
    VxlModel m = load_vxl(bytes.data(), bytes.size());
    REQUIRE(m.sections.size() == 1);
    const VxlSection& s = m.sections[0];
    CHECK(s.name == "MULTI");
    REQUIRE(s.voxels.size() == 8u);        // 1*1*8; index of (0,0,z) is z
    int present = 0;
    for (const auto& v : s.voxels) if (v.present) present++;
    CHECK(present == 3);
    CHECK(s.voxels[1].present); CHECK(s.voxels[1].color == 10); CHECK(s.voxels[1].normal == 1);
    CHECK(s.voxels[2].present); CHECK(s.voxels[2].color == 11); CHECK(s.voxels[2].normal == 2);
    CHECK(s.voxels[5].present); CHECK(s.voxels[5].color == 12); CHECK(s.voxels[5].normal == 3);
    CHECK_FALSE(s.voxels[0].present);
    CHECK_FALSE(s.voxels[3].present);
    CHECK_FALSE(s.voxels[4].present);
    CHECK_FALSE(s.voxels[6].present);
    CHECK_FALSE(s.voxels[7].present);
}

TEST_CASE("vxl span decode bounds violations throw") {
    // Voxel count pushes z past size_z: column 0 data of make_vxl() lives at
    // abs 846..850 = [skip 1, count 1, color 7, normal 3, trailing 1]
    // (body starts at abs 830 = 802+28; data at body+16). Corrupt the count.
    {
        auto b = make_vxl();
        b[847] = 2;
        CHECK_THROWS_AS(load_vxl(b.data(), b.size()), std::runtime_error);
    }
    // Span start points at the last body byte -> the count read (and beyond)
    // falls off the body region: span start[0] entry is at abs 830.
    {
        auto b = make_vxl();
        b[830] = 4;
        CHECK_THROWS_AS(load_vxl(b.data(), b.size()), std::runtime_error);
    }
    // Span start beyond the body region entirely.
    {
        auto b = make_vxl();
        b[830] = 100;
        CHECK_THROWS_AS(load_vxl(b.data(), b.size()), std::runtime_error);
    }
}

// ---------------------------------------------------------------------------
// Guarded real-asset oracle: decode the real htnk.vxl + htnk.hva (Rhino tank,
// resolved from ra2.mix -> local.mix -- NOTE: voxels live in local.mix, NOT
// conquer.mix) and compare a canonical text dump line-for-line against ground
// truth produced by XCC's own VXL/HVA code (built verbatim, OUTSIDE this repo,
// into xcc_vxl_dump -- see scripts/make_vxl_oracle.md). Floats are compared as
// raw IEEE-754 bit patterns so no formatting can diverge. Skips cleanly when
// the oracle files or the install are absent.
// ---------------------------------------------------------------------------
static std::vector<uint8_t> slurp_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

static std::vector<std::string> read_lines(const fs::path& p) {
    std::ifstream f(p);
    std::vector<std::string> out;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(line);
    }
    while (!out.empty() && out.back().empty()) out.pop_back();
    return out;
}

static std::string fbits(float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%08x", v);
    return buf;
}

static void canonical_vxl(const VxlModel& m, std::vector<std::string>& out) {
    char line[160];
    for (size_t i = 0; i < m.sections.size(); i++) {
        const VxlSection& s = m.sections[i];
        std::snprintf(line, sizeof line, "section %u %s %d %d %d %d %s",
                      (unsigned)i, s.name.c_str(), s.size_x, s.size_y, s.size_z,
                      (int)s.normals_mode, fbits(s.scale).c_str());
        out.push_back(line);
        for (int z = 0; z < s.size_z; z++)
            for (int y = 0; y < s.size_y; y++)
                for (int x = 0; x < s.size_x; x++) {
                    const VxlVoxel& v = s.voxels[(size_t)x + (size_t)s.size_x *
                                                 ((size_t)y + (size_t)s.size_y * (size_t)z)];
                    if (!v.present) continue;
                    std::snprintf(line, sizeof line, "%d %d %d %d %d",
                                  x, y, z, (int)v.color, (int)v.normal);
                    out.push_back(line);
                }
    }
}

static void canonical_hva(const HvaModel& m, std::vector<std::string>& out) {
    char line[160];
    std::snprintf(line, sizeof line, "hva %d %u",
                  m.frame_count, (unsigned)m.section_names.size());
    out.push_back(line);
    // Frame-major model emission == raw file order under our frame-major read.
    for (const auto& frame : m.frames)
        for (const auto& xf : frame)
            for (int r = 0; r < 3; r++) {
                std::snprintf(line, sizeof line, "%s %s %s %s",
                              fbits(xf.transform[r][0]).c_str(),
                              fbits(xf.transform[r][1]).c_str(),
                              fbits(xf.transform[r][2]).c_str(),
                              fbits(xf.transform[r][3]).c_str());
                out.push_back(line);
            }
}

static int compare_lines(const std::vector<std::string>& got,
                         const std::vector<std::string>& want) {
    REQUIRE(got.size() == want.size());
    size_t mismatches = 0;
    for (size_t i = 0; i < got.size(); i++) {
        if (got[i] != want[i]) {
            mismatches++;
            if (mismatches <= 5)
                MESSAGE("line " << i << ": got '" << got[i] << "' want '" << want[i] << "'");
        }
    }
    CHECK(mismatches == 0);
    return (int)got.size();
}

TEST_CASE("vxl oracle: htnk.vxl + htnk.hva match the XCC dump line-for-line") {
    const char* od = std::getenv("RA2_ORACLE_DIR");
    fs::path base = od ? od : "D:\\ra2-android\\oracle";
    fs::path vxl_txt = base / "vxl" / "htnk.vxl.txt";
    fs::path hva_txt = base / "vxl" / "htnk.hva.txt";
    if (!fs::exists(vxl_txt) || !fs::exists(hva_txt)) {
        MESSAGE("vxl oracle files absent; skipped");
        return;
    }
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
    const MixFile& outer = ra2fixture::ra2_mix();
    MixFile local = outer.open_nested("local.mix");

    int lines_checked = 0;
    {   // VXL
        const MixEntry* e = local.find("htnk.vxl");
        REQUIRE(e != nullptr);
        std::vector<uint8_t> raw = local.read(*e);
        REQUIRE(raw.size() == 34550);          // byte-verified size in this install
        VxlModel m = load_vxl(raw.data(), raw.size());
        std::vector<std::string> got;
        canonical_vxl(m, got);
        lines_checked += compare_lines(got, read_lines(vxl_txt));
    }
    {   // HVA
        const MixEntry* e = local.find("htnk.hva");
        REQUIRE(e != nullptr);
        std::vector<uint8_t> raw = local.read(*e);
        REQUIRE(raw.size() == 88);             // byte-verified size in this install
        HvaModel h = load_hva(raw.data(), raw.size());
        CHECK(h.frame_count == 1);
        CHECK(h.section_names.size() == 1);
        std::vector<std::string> got;
        canonical_hva(h, got);
        lines_checked += compare_lines(got, read_lines(hva_txt));
    }
    CHECK(lines_checked > 0);   // guard against silently comparing nothing
}
