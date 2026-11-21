#include "doctest.h"
#include "ra2assets/pal.hpp"

TEST_CASE("palette expands 6-bit channels to 8-bit game-accurately (v<<2)") {
    using namespace ra2assets;
    uint8_t raw[768] = {0};
    raw[0]=0;  raw[1]=63; raw[2]=32;      // index 0
    raw[3]=63; raw[4]=63; raw[5]=63;      // index 1 -> brightest (252,252,252)
    Palette p = Palette::load(raw, 768);
    CHECK(p[0].r == 0);
    CHECK(p[0].g == 252);                 // 63<<2 = 252
    CHECK(p[0].b == (32<<2));             // 128
    CHECK(p[0].a == 255);
    CHECK(p[1].r == 252);
    CHECK(p[1].g == 252);
    CHECK(p[1].b == 252);
}
