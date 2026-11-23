#include "doctest.h"
#include "ra2assets/shp.hpp"
#include <stdexcept>
using namespace ra2assets;

// Helper: assemble a 1-frame SHP with given frame flags + pixel payload.
static std::vector<uint8_t> make_shp(uint16_t w, uint16_t h, uint32_t flags,
                                     const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> b;
    auto p16=[&](uint16_t v){ b.push_back(v&0xff); b.push_back(v>>8); };
    auto p32=[&](uint32_t v){ for(int i=0;i<4;i++) b.push_back(v>>(8*i)); };
    p16(0); p16(w); p16(h); p16(1);                 // file header: zero,cx,cy,frames
    size_t fh = b.size();
    p16(0); p16(0); p16(w); p16(h);                 // frame: x,y,cx,cy
    p32(flags); p32(0); p32(0);                     // compression, unknown, unknown/color
    size_t off_pos = b.size(); p32(0);              // data offset (patched below)
    uint32_t data_off = (uint32_t)b.size();
    for (auto v : payload) b.push_back(v);
    b[off_pos+0]=data_off; b[off_pos+1]=data_off>>8; b[off_pos+2]=data_off>>16; b[off_pos+3]=data_off>>24;
    (void)fh;
    return b;
}

TEST_CASE("shp decodes uncompressed frame") {
    // 2x2, flags=1 (uncompressed), pixels 1,2,3,4
    auto shp = make_shp(2,2,1,{1,2,3,4});
    ShpFile s = ShpFile::load(std::move(shp));
    REQUIRE(s.count()==1);
    auto idx = s.decode_indexed(0);
    REQUIRE(idx.size()==4);
    CHECK(idx[0]==1); CHECK(idx[1]==2); CHECK(idx[2]==3); CHECK(idx[3]==4);
}

TEST_CASE("shp decodes RLE-zero frame") {
    // RLE-zero (flags & 2, XCC "format 3"): EVERY scanline begins with a
    // uint16 giving the total encoded line length IN BYTES, including the
    // 2 prefix bytes themselves. After the prefix, bytes are copied verbatim
    // EXCEPT a 0x00, which is followed by a run-length byte emitting that
    // many zero pixels.
    // Row "5 0 0 6" encodes as: len=6 (2 prefix + 4 data), 05, 00 02, 06.
    auto shp = make_shp(4,1,2,{0x06,0x00, 0x05, 0x00,0x02, 0x06});
    ShpFile s = ShpFile::load(std::move(shp));
    auto idx = s.decode_indexed(0);
    REQUIRE(idx.size()==4);
    CHECK(idx[0]==5); CHECK(idx[1]==0); CHECK(idx[2]==0); CHECK(idx[3]==6);
}

TEST_CASE("shp RLE-zero honors the per-line length prefix across rows") {
    // Two rows, w=2. Row 0 = "7 8": len=4 -> 04 00 07 08.
    //                Row 1 = "0 0": len=4 -> 04 00 00 02 (zero-run of 2).
    auto shp = make_shp(2,2,2,{0x04,0x00,0x07,0x08, 0x04,0x00,0x00,0x02});
    ShpFile s = ShpFile::load(std::move(shp));
    auto idx = s.decode_indexed(0);
    REQUIRE(idx.size()==4);
    CHECK(idx[0]==7); CHECK(idx[1]==8); CHECK(idx[2]==0); CHECK(idx[3]==0);
}

TEST_CASE("palettize maps index 0 to transparent") {
    uint8_t raw[768]={0}; raw[3]=63;raw[4]=0;raw[5]=0;   // index1 = red
    Palette pal = Palette::load(raw,768);
    std::vector<uint8_t> idx = {0,1};
    auto rgba = ShpFile::palettize(idx, pal);
    CHECK(rgba[0].a==0);                 // index 0 transparent
    CHECK(rgba[1].r==252); CHECK(rgba[1].a==255);   // 63<<2 = 252 (game-accurate expand6)
}

TEST_CASE("shp rejects truncated frame headers and degrades on corrupt offsets") {
    // Test 1: truncated frame headers — claim 5 frames but only ~10 bytes total
    {
        std::vector<uint8_t> buf;
        auto p16=[&](uint16_t v){ buf.push_back(v&0xff); buf.push_back(v>>8); };
        p16(0); p16(2); p16(2); p16(5);  // file header: zero, cx=2, cy=2, frames=5
        // Only 2 more bytes (truncated, not enough for even 1 frame header)
        buf.push_back(0);
        buf.push_back(0);
        CHECK_THROWS_AS(ShpFile::load(std::move(buf)), std::runtime_error);
    }
    // Test 2: corrupt frame data-offset field — should degrade gracefully
    {
        auto buf = make_shp(2,2,1,{1,2,3,4});
        // Corrupt the data-offset field at bytes 28..31 to 0xFFFFFFF0
        if (buf.size() >= 32) {
            buf[28]=0xF0; buf[29]=0xFF; buf[30]=0xFF; buf[31]=0xFF;
        }
        ShpFile s = ShpFile::load(std::move(buf));
        auto out = s.decode_indexed(0);
        CHECK(out.size()==4);
        CHECK(out[0]==0); CHECK(out[1]==0); CHECK(out[2]==0); CHECK(out[3]==0);
    }
}

TEST_CASE("shp guards zero-frame and out-of-range frame access") {
    // Valid 1-frame SHP: out-of-range index must throw, not read OOB.
    {
        auto shp = make_shp(2,2,1,{1,2,3,4});
        ShpFile s = ShpFile::load(std::move(shp));
        REQUIRE(s.count()==1);
        CHECK_THROWS_AS(s.decode_indexed(5), std::runtime_error);
        CHECK_THROWS_AS(s.frame_info(5), std::runtime_error);
    }
    // 0-frame SHP: file header only (zero,cx,cy,frames=0), no frame headers.
    {
        std::vector<uint8_t> buf;
        auto p16=[&](uint16_t v){ buf.push_back(v&0xff); buf.push_back(v>>8); };
        p16(0); p16(0); p16(0); p16(0);   // zero, cx=0, cy=0, frames=0
        ShpFile s = ShpFile::load(std::move(buf));
        CHECK(s.count()==0);
        CHECK_THROWS_AS(s.decode_indexed(0), std::runtime_error);
    }
}
