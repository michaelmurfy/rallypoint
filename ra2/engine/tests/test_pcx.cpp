#include "doctest.h"
#include "ra2assets/pcx.hpp"
#include "ra2assets/mix.hpp"
#include "asset_fixture.hpp"
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;
using namespace ra2assets;

// A minimal 4x2 8-bit RLE PCX: bpl=4, one run of 4 (index 5) per scanline,
// trailer palette sets index 5 = (10,20,30). Exercises the RLE run path +
// the FULL-8-bit trailer palette (no 6-bit expansion).
static std::vector<uint8_t> tiny_pcx() {
    std::vector<uint8_t> v(128, 0);
    v[0] = 0x0A; v[2] = 1; v[3] = 8;                 // manuf, RLE, 8bpp
    v[4] = 0; v[6] = 0; v[8] = 3; v[10] = 1;         // xmin/ymin=0, xmax=3, ymax=1 -> 4x2
    v[65] = 1;                                       // 1 plane
    v[66] = 4; v[67] = 0;                            // bytes per line = 4
    for (int row = 0; row < 2; ++row) { v.push_back(0xC0 | 4); v.push_back(5); }  // run of 4 x index 5
    v.push_back(0x0C);                               // trailer marker
    std::vector<uint8_t> pal(768, 0);
    pal[5 * 3 + 0] = 10; pal[5 * 3 + 1] = 20; pal[5 * 3 + 2] = 30;
    v.insert(v.end(), pal.begin(), pal.end());
    return v;
}

TEST_CASE("pcx: synthetic 4x2 RLE decodes dims + trailer palette (8-bit, no VGA expand)") {
    PcxImage img = decode_pcx(tiny_pcx());
    CHECK(img.w == 4);
    CHECK(img.h == 2);
    REQUIRE(img.indices.size() == 8);
    CHECK(img.indices[0] == 5);
    CHECK(img.indices[7] == 5);
    CHECK(img.palette[5].r == 10);     // FULL 8-bit, NOT 10>>2
    CHECK(img.palette[5].g == 20);
    CHECK(img.palette[5].b == 30);
    std::vector<uint8_t> rgba = img.rgba();
    REQUIRE(rgba.size() == 8 * 4);
    CHECK(rgba[0] == 10); CHECK(rgba[1] == 20); CHECK(rgba[2] == 30); CHECK(rgba[3] == 255);
}

TEST_CASE("pcx: real title.pcx decodes (ra2.mix/local.mix)") {
    const char* id = std::getenv("RA2_INSTALL_DIR");
    fs::path install = id ? id : "D:\\Red Alert 2";
    fs::path ra2 = install / "ra2.mix";
    if (!fs::exists(ra2)) { MESSAGE("ra2.mix absent; skipped"); return; }
    const MixFile& top = ra2fixture::mix_by_name("ra2.mix");
    MixFile local = top.open_nested("local.mix");
    const MixEntry* e = local.find("title.pcx");
    REQUIRE(e != nullptr);
    auto bytes = local.read(*e);
    PcxImage img = decode_pcx(bytes);
    CHECK(img.w > 0);
    CHECK(img.h > 0);
    CHECK(static_cast<size_t>(img.w) * img.h == img.indices.size());
    // Every index is a valid palette entry (0..255 by type) and rgba() is sized.
    CHECK(img.rgba().size() == static_cast<size_t>(img.w) * img.h * 4);
}
