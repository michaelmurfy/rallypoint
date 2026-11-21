#include "doctest.h"
#include "ra2assets/wav.hpp"
#include "ra2assets/mix.hpp"      // used by the real-asset oracle pin test
#include "asset_fixture.hpp"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>             // used by the real-asset oracle pin test
#include <fstream>
#include <vector>

namespace fs = std::filesystem;
using namespace ra2assets;

// Build a minimal in-memory fmt-0x0011 mono WAV: one 12-byte block
// (4-byte header {pred=0,idx=0,rsvd=0} + bytes {0x07,0x80} + 6 pad bytes of
// 0x00 nibbles), fact = 5 samples. The nibble stream 7,0,0,8 must decode to
// the docs/re/audio.md worked vector {0, 11, 13, 14, 13}; fact caps output
// at 5 so the pad nibbles are dropped.
static std::vector<uint8_t> tiny_ima_wav() {
    const uint16_t block_align = 12, channels = 1, tag = 0x0011, bits = 4;
    const uint32_t rate = 22050;
    const uint16_t samples_per_block = (block_align - 4) * 2 + 1;  // 17
    std::vector<uint8_t> v;
    auto u16 = [&](uint16_t x) { v.push_back(x & 0xff); v.push_back(x >> 8); };
    auto u32 = [&](uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back((x >> (8 * i)) & 0xff); };
    auto tag4 = [&](const char* t) { v.insert(v.end(), t, t + 4); };
    tag4("RIFF"); u32(4 + 26 + 12 + 20);  // riff payload size (WAVE + chunks)
    tag4("WAVE");
    tag4("fmt "); u32(20); u16(tag); u16(channels); u32(rate); u32(11100);
    u16(block_align); u16(bits); u16(2); u16(samples_per_block);
    tag4("fact"); u32(4); u32(5);        // 5 valid samples
    tag4("data"); u32(block_align);
    u16(0); v.push_back(0); v.push_back(0);          // block header pred=0 idx=0 rsvd=0
    v.push_back(0x07); v.push_back(0x80);            // nibbles 7,0 then 0,8
    for (int i = 0; i < 6; ++i) v.push_back(0x00);   // pad to block_align
    return v;
}

TEST_CASE("wav: tiny synthetic IMA block decodes to the documented vector") {
    std::vector<uint8_t> w = tiny_ima_wav();
    WavInfo info;
    std::vector<int16_t> pcm = load_wav_ima(w.data(), w.size(), info);
    CHECK(info.sample_rate == 22050);
    CHECK(info.channels == 1);
    REQUIRE(pcm.size() == 5);            // fact cap
    CHECK(pcm[0] == 0);                  // block-header predictor emitted first
    CHECK(pcm[1] == 11);
    CHECK(pcm[2] == 13);
    CHECK(pcm[3] == 14);
    CHECK(pcm[4] == 13);
}

TEST_CASE("wav: rejects non-IMA and truncated") {
    std::vector<uint8_t> w = tiny_ima_wav();
    w[20] = 0x01;                        // fmt tag -> 0x0001 PCM
    WavInfo info;
    CHECK_THROWS(load_wav_ima(w.data(), w.size(), info));
    CHECK_THROWS(load_wav_ima(w.data(), 8, info));   // truncated
}

// A minimal fmt-0x0011 STEREO IMA WAV: block_align 16 = 8 header (L:{pred=0,
// idx=0}, R:{pred=0,idx=0}) + 8 data (ch0 word {0x07,0x80,0,0}, ch1 word all 0).
// The L nibble stream 7,0,0,8 decodes to the documented {0,11,13,14,13,...}
// vector; R stays 0. Output is interleaved L,R.
static std::vector<uint8_t> tiny_stereo_ima_wav() {
    const uint16_t block_align = 16, channels = 2, tag = 0x0011, bits = 4;
    const uint32_t rate = 22050;
    const uint16_t spb = 1 + (4 * 2);    // samples per channel per block = 9
    std::vector<uint8_t> v;
    auto u16 = [&](uint16_t x){ v.push_back(x & 0xff); v.push_back(x >> 8); };
    auto u32 = [&](uint32_t x){ for (int i=0;i<4;++i) v.push_back((x >> (8*i)) & 0xff); };
    auto tag4 = [&](const char* t){ v.insert(v.end(), t, t + 4); };
    tag4("RIFF"); u32(4 + 26 + 12 + 16); tag4("WAVE");
    tag4("fmt "); u32(20); u16(tag); u16(channels); u32(rate); u32(44100);
    u16(block_align); u16(bits); u16(2); u16(spb);
    tag4("fact"); u32(4); u32(spb);      // 9 samples per channel
    tag4("data"); u32(block_align);
    u16(0); v.push_back(0); v.push_back(0);     // L header pred=0 idx=0 rsvd=0
    u16(0); v.push_back(0); v.push_back(0);     // R header pred=0 idx=0 rsvd=0
    v.push_back(0x07); v.push_back(0x80); v.push_back(0); v.push_back(0);  // ch0 word
    v.push_back(0x00); v.push_back(0x00); v.push_back(0); v.push_back(0);  // ch1 word
    return v;
}

TEST_CASE("wav: stereo IMA decodes both channels, interleaved") {
    std::vector<uint8_t> w = tiny_stereo_ima_wav();
    WavInfo info;
    std::vector<int16_t> pcm = load_wav_ima(w.data(), w.size(), info);
    CHECK(info.channels == 2);
    CHECK(info.sample_rate == 22050);
    REQUIRE(pcm.size() == 9 * 2);        // 9 samples/channel * 2 channels, fact-capped
    CHECK(pcm[0] == 0);                  // L predictor
    CHECK(pcm[1] == 0);                  // R predictor
    CHECK(pcm[2] == 11);                 // L sample 1 (documented cascade)
    CHECK(pcm[3] == 0);                  // R sample 1 (silent channel)
    CHECK(pcm[4] == 13);                 // L sample 2
    CHECK(pcm[5] == 0);                  // R sample 2
}

// ---------------------------------------------------------------------------
// Guarded real-asset pin: language.mix -> audio.mix -> "ceva064.wav" (EVA
// "Unit lost"), verified this session against the real install (RIFF,
// fmt 0x0011, mono, 22050 Hz, block_align 512, samples_per_block 1017 =
// (512-4)*2+1, fact = 18559 samples, data = 19x512 bytes, first block
// predictor -13, bytes f3 ff). Guard mirrors tests/test_audio.cpp's
// real-asset case: skip cleanly (MESSAGE + return) if the install is
// absent, so asset-free CI still builds and passes.
// ---------------------------------------------------------------------------
TEST_CASE("wav: real ceva064 (EVA 'Unit lost') pins") {
    const char* id = std::getenv("RA2_INSTALL_DIR");
    fs::path install = id ? id : "D:\\Red Alert 2";
    fs::path lang = install / "language.mix";
    if (!fs::exists(lang)) { MESSAGE("language.mix absent; skipped"); return; }

    // Borrow the shared, load-once language.mix (asset_fixture); open_nested
    // still exercises the real path. REQUIRE_FALSE kept (now on the shared
    // bytes) so this case's assertion count is unchanged.
    REQUIRE_FALSE(ra2fixture::mix_bytes_by_name("language.mix").empty());
    const MixFile& language = ra2fixture::mix_by_name("language.mix");
    MixFile audio = language.open_nested("audio.mix");
    const MixEntry* we = audio.find("ceva064.wav");
    REQUIRE(we != nullptr);
    auto bytes = audio.read(*we);

    WavInfo info;
    std::vector<int16_t> pcm = load_wav_ima(bytes.data(), bytes.size(), info);
    CHECK(info.sample_rate == 22050);
    CHECK(info.channels == 1);
    CHECK(pcm.size() == 18559);          // fact chunk
    CHECK(pcm[0] == -13);                // first block predictor (hex f3 ff)
}
