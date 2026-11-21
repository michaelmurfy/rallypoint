#include "doctest.h"
#include "ra2assets/lcw.hpp"
#include <vector>
#include <cstdint>
#include <stdexcept>
using ra2assets::lcw_decompress;
static std::vector<uint8_t> dec(std::vector<uint8_t> s, size_t out){
    std::vector<uint8_t> o(out,0); size_t n=lcw_decompress(s.data(),s.size(),o.data(),o.size()); o.resize(n); return o; }

TEST_CASE("lcw: literal copy (0x80|count) then end (0x80)") {
    // 0x83 => copy 3 literal bytes 0A 0B 0C ; 0x80 => end
    auto o = dec({0x83,0x0A,0x0B,0x0C,0x80}, 8);
    REQUIRE(o.size()==3); CHECK(o[0]==0x0A); CHECK(o[1]==0x0B); CHECK(o[2]==0x0C);
}
TEST_CASE("lcw: short relative back-copy (cmd MSB=0)") {
    // literal 'AB' then relative copy count=3 offset=2 -> repeats "ABA"
    // literal: 0x82 41 42 ; rel cmd=0x00: count=((0x00&0x70)>>4)+3=3,
    // off=((0x00&0x0F)<<8)|next(0x02)=2
    // NOTE: the task brief's hand-built stream for this case used cmd=0x20,
    // but 0x20's bits 6:4 are 2 (not 0), which by the canonical short-copy
    // formula count=((cmd&0x70)>>4)+3 yields count=5, not the 3 the brief's
    // own comment/expected output assume (verified empirically: with 0x20
    // the decoder -- correctly implementing the documented Format80 command
    // layout -- produces 7 output bytes "ABABABA", failing REQUIRE(o.size()
    // ==5)). cmd=0x00 is what actually realizes the documented count=3/
    // off=2 scenario, so the stream below uses 0x00 instead of the brief's
    // 0x20 typo.
    auto o = dec({0x82,0x41,0x42, 0x00,0x02, 0x80}, 16);
    REQUIRE(o.size()==5); CHECK(o[0]=='A');CHECK(o[1]=='B');CHECK(o[2]=='A');CHECK(o[3]=='B');CHECK(o[4]=='A');
}
TEST_CASE("lcw: run fill (0xFE count value)") {
    // 0xFE, count=5 (LE u16), value=0x77
    auto o = dec({0xFE,0x05,0x00,0x77,0x80}, 16);
    REQUIRE(o.size()==5); for(auto b:o) CHECK(b==0x77);
}
TEST_CASE("lcw: large absolute copy (0xFF count pos)") {
    // literal 'XYZ'; 0xFF count=2 pos=0 -> copies dest[0..1]="XY"
    auto o = dec({0x83,'X','Y','Z', 0xFF,0x02,0x00,0x00,0x00, 0x80}, 16);
    REQUIRE(o.size()==5); CHECK(o[3]=='X'); CHECK(o[4]=='Y');
}
TEST_CASE("lcw: medium absolute copy (0xC0..0xFD, count=(cmd&0x3F)+3, pos u16)") {
    // literal 'XYZ'; cmd=0xC0 -> count=3, pos=0 -> "XYZ"
    auto o = dec({0x83,'X','Y','Z', 0xC0,0x00,0x00, 0x80}, 16);
    REQUIRE(o.size()==6); CHECK(o[3]=='X');CHECK(o[4]=='Y');CHECK(o[5]=='Z');
}
TEST_CASE("lcw: mixed command sequence (literal + short-rel-copy + run-fill + medium-abs-copy + end)") {
    // Cross-check exercising command *interaction* in one stream (adapted
    // in-repo substitute for a real-asset XCC decode80 oracle run -- see
    // task-1-report.md for why the real-data oracle is deferred to Task 2).
    // Hand-computed step by step:
    //   1) literal cmd=0x84 (0x80|4): copy 4 literal bytes "WEST"
    //      -> out[0..3] = W E S T                          (len=4)
    //   2) short rel-copy cmd=0x10, next=0x04:
    //      count=((0x10&0x70)>>4)+3 = 1+3 = 4
    //      off =((0x10&0x0F)<<8)|0x04 = 4
    //      r = out-4 = out+0 (start); off==count so no overlap: copies the
    //      original "WEST" again verbatim
    //      -> out[4..7] = W E S T                          (len=8)
    //   3) run-fill cmd=0xFE, count=3 (0x03,0x00), value=0x21 ('!')
    //      -> out[8..10] = ! ! !                            (len=11)
    //   4) medium abs-copy cmd=0xC2, pos=2 (0x02,0x00):
    //      count=(0xC2&0x3F)+3 = 2+3 = 5
    //      reads out[2..6] = S,T,W,E,S (from the buffer built by steps 1-2)
    //      -> out[11..15] = S T W E S                       (len=16)
    //   5) 0x80 end marker (count==0) -> stop
    // Final 16 bytes: "WESTWEST!!!STWES"
    auto o = dec({0x84,'W','E','S','T', 0x10,0x04, 0xFE,0x03,0x00,0x21, 0xC2,0x02,0x00, 0x80}, 16);
    REQUIRE(o.size()==16);
    std::vector<uint8_t> expected = {'W','E','S','T','W','E','S','T','!','!','!','S','T','W','E','S'};
    for (size_t i=0;i<expected.size();++i) CHECK(o[i]==expected[i]);
}
TEST_CASE("lcw rejects malformed and overrunning streams") {
    uint8_t out[16];
    size_t out_len = sizeof(out);

    // Sub-check 1: Source overrun / truncated stream
    // Literal copy command 0x85 (5 bytes) but only 2 literal bytes provided
    {
        std::vector<uint8_t> stream = {0x85, 'A', 'B'};
        CHECK_THROWS_AS(lcw_decompress(stream.data(), stream.size(), out, out_len), std::runtime_error);
    }

    // Sub-check 2: Truncated relative-copy operand
    // Short relative copy (0x00) but no following offset byte
    {
        std::vector<uint8_t> stream = {0x00};
        CHECK_THROWS_AS(lcw_decompress(stream.data(), stream.size(), out, out_len), std::runtime_error);
    }

    // Sub-check 3: Bad relative offset (offset > written)
    // Literal 1 byte 'A', then relative copy with offset=5 but only 1 byte written
    {
        std::vector<uint8_t> stream = {0x81, 'A', 0x00, 0x05};
        CHECK_THROWS_AS(lcw_decompress(stream.data(), stream.size(), out, out_len), std::runtime_error);
    }

    // Sub-check 4: Destination overrun
    // Fill 16 bytes into 4-byte buffer
    {
        std::vector<uint8_t> stream = {0xFE, 0x10, 0x00, 0x77, 0x80};
        uint8_t small_out[4];
        CHECK_THROWS_AS(lcw_decompress(stream.data(), stream.size(), small_out, sizeof(small_out)), std::runtime_error);
    }

    // Sub-check 5: Bad absolute pos
    // Medium abs copy from position 16 when nothing written
    {
        std::vector<uint8_t> stream = {0xC0, 0x10, 0x00, 0x80};
        CHECK_THROWS_AS(lcw_decompress(stream.data(), stream.size(), out, out_len), std::runtime_error);
    }
}
