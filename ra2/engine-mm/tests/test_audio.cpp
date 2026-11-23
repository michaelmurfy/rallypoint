#include "doctest.h"
#include "ra2assets/audio_bag.hpp"
#include "ra2assets/wav.hpp"
#include "ra2assets/mix.hpp"     // used by the Task-4 real-asset oracle test
#include "asset_fixture.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>            // used by the Task-4 real-asset oracle test
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace ra2assets;

// ---------------------------------------------------------------------------
// Synthetic audio.idx builder. On-disk layout (little-endian, packed) as
// verified against the real install (12 + 1153*36 = 41,520 bytes exactly) and
// XCC's t_audio_idx_header/t_audio_idx_entry:
//   header: char magic[4]="GABA"; u32 version(=2); u32 count
//   entry : char fname[16] (NUL-padded); u32 offset; u32 size;
//           u32 sample_rate; u32 flags; u32 chunk_size
// ---------------------------------------------------------------------------
struct IdxEntrySpec {
    std::string name;
    uint32_t offset, size, sample_rate, flags, chunk_size;
};

static void put32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; i++) b.push_back((uint8_t)((v >> (8 * i)) & 0xff));
}

static std::vector<uint8_t> make_idx(const std::vector<IdxEntrySpec>& entries) {
    std::vector<uint8_t> b;
    b.push_back('G'); b.push_back('A'); b.push_back('B'); b.push_back('A');
    put32(b, 2);                                  // version
    put32(b, (uint32_t)entries.size());           // count
    for (const IdxEntrySpec& e : entries) {
        for (size_t i = 0; i < 16; i++)           // fname[16], NUL-padded
            b.push_back(i < e.name.size() ? (uint8_t)e.name[i] : 0);
        put32(b, e.offset); put32(b, e.size);
        put32(b, e.sample_rate); put32(b, e.flags); put32(b, e.chunk_size);
    }
    return b;
}

TEST_CASE("audio idx: valid header parses, order preserved") {
    auto b = make_idx({
        {"IchRatta", 0, 71148, 22050, 12, 512},
        {"gradio3f", 71148, 512, 22050, 12, 512},
    });
    REQUIRE(b.size() == 12 + 2 * 36);             // mirror XCC is_valid arithmetic
    AudioIdx idx = AudioIdx::load(b.data(), b.size());
    REQUIRE(idx.clips().size() == 2);
    CHECK(idx.clips()[0].name == "IchRatta");     // raw on-disk name preserved
    CHECK(idx.clips()[0].offset == 0);
    CHECK(idx.clips()[0].size == 71148);
    CHECK(idx.clips()[0].sample_rate == 22050);
    CHECK(idx.clips()[0].flags == 12);
    CHECK(idx.clips()[0].chunk_size == 512);
    CHECK(idx.clips()[1].name == "gradio3f");     // order preserved
    CHECK(idx.clips()[1].offset == 71148);
}

TEST_CASE("audio idx: find is case-insensitive, absent -> nullptr") {
    auto b = make_idx({{"IchRatta", 0, 100, 22050, 12, 512}});
    AudioIdx idx = AudioIdx::load(b.data(), b.size());
    const BagClip* c1 = idx.find("ichratta");
    const BagClip* c2 = idx.find("ICHRATTA");
    const BagClip* c3 = idx.find("IchRatta");
    REQUIRE(c1 != nullptr);
    CHECK(c1 == c2);
    CHECK(c1 == c3);
    CHECK(c1->size == 100);
    CHECK(idx.find("nope") == nullptr);
    CHECK(idx.find("") == nullptr);
    CHECK(idx.find("ichratta_longer_than_16") == nullptr);
}

TEST_CASE("audio idx: 16-char name (no NUL terminator on disk)") {
    auto b = make_idx({{"abcdefghijklmnop", 4, 8, 22050, 12, 512}});
    AudioIdx idx = AudioIdx::load(b.data(), b.size());
    REQUIRE(idx.clips().size() == 1);
    CHECK(idx.clips()[0].name == "abcdefghijklmnop");
    CHECK(idx.find("ABCDEFGHIJKLMNOP") == &idx.clips()[0]);
}

TEST_CASE("audio idx: empty index (count=0) is valid") {
    auto b = make_idx({});
    AudioIdx idx = AudioIdx::load(b.data(), b.size());
    CHECK(idx.clips().empty());
    CHECK(idx.find("anything") == nullptr);
}

TEST_CASE("audio idx: validation rejects malformed input") {
    // Too small for even the 12-byte header.
    {
        std::vector<uint8_t> b = {'G', 'A', 'B', 'A', 2, 0, 0, 0};
        CHECK_THROWS_AS(AudioIdx::load(b.data(), b.size()), std::runtime_error);
    }
    // Wrong magic.
    {
        auto b = make_idx({{"x", 0, 1, 22050, 12, 512}});
        b[0] = 'X';
        CHECK_THROWS_AS(AudioIdx::load(b.data(), b.size()), std::runtime_error);
    }
    // Wrong version (XCC is_valid: h.two != 2 -> invalid).
    {
        auto b = make_idx({{"x", 0, 1, 22050, 12, 512}});
        b[4] = 1;
        CHECK_THROWS_AS(AudioIdx::load(b.data(), b.size()), std::runtime_error);
    }
    // Size mismatch: trailing byte (XCC: 12 + count*36 must EQUAL the size).
    {
        auto b = make_idx({{"x", 0, 1, 22050, 12, 512}});
        b.push_back(0);
        CHECK_THROWS_AS(AudioIdx::load(b.data(), b.size()), std::runtime_error);
    }
    // Size mismatch: truncated final entry.
    {
        auto b = make_idx({{"x", 0, 1, 22050, 12, 512}});
        b.pop_back();
        CHECK_THROWS_AS(AudioIdx::load(b.data(), b.size()), std::runtime_error);
    }
    // Count claims more entries than the bytes hold.
    {
        auto b = make_idx({{"x", 0, 1, 22050, 12, 512}});
        b[8] = 200;                                // count = 200, size unchanged
        CHECK_THROWS_AS(AudioIdx::load(b.data(), b.size()), std::runtime_error);
    }
}

// ---------------------------------------------------------------------------
// decode_bag_clip synthetic tests. BAG ADPCM clips are chunked: chunk_size-
// byte chunks, each opening with a 4-byte header {i16 predictor; u8 index;
// u8 reserved} whose predictor is emitted as the chunk's first sample, then
// (chunk_bytes-4)*2 nibbles, LOW nibble first (docs/re/audio.md; XCC
// Cima_adpcm_wav_decode::load + aud_decode_ima_chunk are the cross-check).
// All expected PCM vectors below are HAND-COMPUTED from the step/index
// tables -- the arithmetic is worked in the plan and docs/re/audio.md.
// ---------------------------------------------------------------------------
static BagClip clip_of(uint32_t offset, uint32_t size, uint32_t flags,
                       uint32_t chunk_size) {
    BagClip c;
    c.name = "test";
    c.offset = offset; c.size = size;
    c.sample_rate = 22050; c.flags = flags; c.chunk_size = chunk_size;
    return c;
}

static void put_chunk_header(std::vector<uint8_t>& b, int16_t sample, uint8_t index) {
    b.push_back((uint8_t)((uint16_t)sample & 0xff));
    b.push_back((uint8_t)(((uint16_t)sample >> 8) & 0xff));
    b.push_back(index);
    b.push_back(0);                                // reserved
}

TEST_CASE("bag decode: worked IMA example (predictor 0, index 0)") {
    std::vector<uint8_t> bag;
    put_chunk_header(bag, 0, 0);
    bag.push_back(0x07);                           // nibbles: 7 (low), 0 (high)
    bag.push_back(0x80);                           // nibbles: 0 (low), 8 (high)
    AudioClipInfo info;
    auto pcm = decode_bag_clip(bag.data(), bag.size(),
                               clip_of(0, 6, /*flags*/ 12, /*chunk*/ 512), info);
    // Hand-computed: header sample 0; nibble 7 (step 7, delta 0+1+3+7=11) -> 11,
    // index += 8; nibble 0 (step 16, delta 2) -> 13, index 7; nibble 0 (step 14,
    // delta 1) -> 14, index 6; nibble 8 (step 13, delta 1, sign) -> 13.
    const std::vector<int16_t> want = {0, 11, 13, 14, 13};
    CHECK(pcm == want);
    CHECK(info.sample_rate == 22050);
    CHECK(info.channels == 1);
    CHECK(info.bits == 16);
}

TEST_CASE("bag decode: each chunk restarts from its own header") {
    // chunk_size=8 -> each chunk is a 4-byte header + 4 nibble bytes.
    std::vector<uint8_t> bag;
    put_chunk_header(bag, 0, 0);
    for (int i = 0; i < 4; i++) bag.push_back(0xFF);   // 8 x code 15
    put_chunk_header(bag, 1000, 0);
    for (int i = 0; i < 4; i++) bag.push_back(0x00);   // 8 x code 0
    AudioClipInfo info;
    auto pcm = decode_bag_clip(bag.data(), bag.size(), clip_of(0, 16, 12, 8), info);
    // Chunk 2 must NOT continue from -5431: it restarts at its header's
    // predictor 1000, and code-0 nibbles at index 0 (step 7, delta 7>>3 = 0)
    // leave the predictor unchanged.
    const std::vector<int16_t> want = {
        0, -11, -41, -104, -240, -533, -1164, -2521, -5431,     // chunk 1
        1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000,   // chunk 2 (reset)
    };
    CHECK(pcm == want);
}

TEST_CASE("bag decode: predictor clamps to int16 range") {
    std::vector<uint8_t> bag;
    put_chunk_header(bag, 0, 0);
    for (int i = 0; i < 6; i++) bag.push_back(0xFF);   // 12 x code 15
    AudioClipInfo info;
    auto pcm = decode_bag_clip(bag.data(), bag.size(), clip_of(0, 10, 12, 512), info);
    // Hand-computed walk (index 0,8,16,...,80,88; steps 7,16,34,73,157,337,
    // 724,1552,3327,7132,15289,32767; deltas 11,30,63,136,293,631,1357,2910,
    // 6236,13372,28666,61436): the 11th subtraction hits -53705 -> clamp.
    const std::vector<int16_t> want = {
        0, -11, -41, -104, -240, -533, -1164, -2521, -5431,
        -11667, -25039, -32768, -32768,
    };
    CHECK(pcm == want);
}

TEST_CASE("bag decode: raw 16-bit PCM path (flags bit 1)") {
    // flags 6 = 0x4|0x2 (mono raw PCM), chunk_size 0 -- 21 entries in the real
    // audio.idx use exactly this (e.g. "utext"); bytes pass through as LE i16.
    std::vector<uint8_t> bag = {0xFE, 0xFF, 0xBC, 0x02, 0xFF, 0x7F};  // -2, 700, 32767
    AudioClipInfo info;
    auto pcm = decode_bag_clip(bag.data(), bag.size(), clip_of(0, 6, 6, 0), info);
    const std::vector<int16_t> want = {-2, 700, 32767};
    CHECK(pcm == want);
    CHECK(info.channels == 1);

    // flags 7 adds the stereo bit: same passthrough (already interleaved).
    auto pcm2 = decode_bag_clip(bag.data(), bag.size(), clip_of(0, 6, 7, 0), info);
    CHECK(pcm2 == want);
    CHECK(info.channels == 2);
}

TEST_CASE("bag decode: empty clip -> empty vector") {
    std::vector<uint8_t> bag = {0x00};
    AudioClipInfo info;
    auto pcm = decode_bag_clip(bag.data(), bag.size(), clip_of(0, 0, 12, 512), info);
    CHECK(pcm.empty());
    CHECK(info.sample_rate == 22050);
    CHECK(info.channels == 1);
}

TEST_CASE("bag decode: errors") {
    std::vector<uint8_t> bag(16, 0);
    AudioClipInfo info;
    // Clip range beyond bag_len.
    CHECK_THROWS_AS(decode_bag_clip(bag.data(), bag.size(),
                                    clip_of(8, 12, 12, 512), info),
                    std::runtime_error);
    // Offset far beyond the bag (also exercises the overflow-safe check).
    CHECK_THROWS_AS(decode_bag_clip(bag.data(), bag.size(),
                                    clip_of(0xFFFFFFFFu, 4, 12, 512), info),
                    std::runtime_error);
    // ADPCM with chunk_size too small to hold the 4-byte chunk header.
    CHECK_THROWS_AS(decode_bag_clip(bag.data(), bag.size(),
                                    clip_of(0, 8, 12, 0), info),
                    std::runtime_error);
    // W5-E2: stereo IMA-ADPCM (flags 13) decodes now; a chunk too small for
    // the two 4-byte channel headers still throws.
    CHECK_THROWS_AS(decode_bag_clip(bag.data(), bag.size(),
                                    clip_of(0, 16, 13, 4), info),
                    std::runtime_error);
    // Neither PCM (bit 1) nor ADPCM (bit 3) flagged.
    CHECK_THROWS_AS(decode_bag_clip(bag.data(), bag.size(),
                                    clip_of(0, 8, 4, 0), info),
                    std::runtime_error);
    // Raw PCM with odd byte size.
    CHECK_THROWS_AS(decode_bag_clip(bag.data(), bag.size(),
                                    clip_of(0, 5, 6, 0), info),
                    std::runtime_error);
    // Trailing partial chunk too small to hold its 4-byte header
    // (size 10, chunk 8 -> second chunk is only 2 bytes).
    CHECK_THROWS_AS(decode_bag_clip(bag.data(), bag.size(),
                                    clip_of(0, 10, 12, 8), info),
                    std::runtime_error);
    // Chunk-header step index out of range (> 88).
    {
        std::vector<uint8_t> b2;
        put_chunk_header(b2, 0, 89);
        b2.push_back(0x00);
        CHECK_THROWS_AS(decode_bag_clip(b2.data(), b2.size(),
                                        clip_of(0, 5, 12, 512), info),
                        std::runtime_error);
    }
}

// ---------------------------------------------------------------------------
// Guarded real-asset oracle: locate clips through OUR AudioIdx +
// decode_bag_clip from the REAL install files and compare the PCM
// byte-for-byte against ground truth decoded by XCC's own ADPCM code
// (built verbatim, OUTSIDE this repo, into xcc_adpcm_dump -- see
// scripts/make_audio_oracle.md). Skips cleanly if the install or the oracle
// set is absent, so asset-free CI still builds and passes.
//
// oracle/audio/manifest.txt: one clip per line, whitespace-separated:
//   <name> <sample_rate> <flags> <chunk_size>
// oracle/audio/<name>.pcm: raw little-endian int16 interleaved PCM.
// ---------------------------------------------------------------------------
static std::vector<uint8_t> slurp_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

TEST_CASE("audio bag: real clips match XCC oracle byte-for-byte") {
    const char* od = std::getenv("RA2_ORACLE_DIR");
    fs::path base = od ? od : "D:\\ra2-android\\oracle";
    fs::path manifest = base / "audio" / "manifest.txt";
    if (!fs::exists(manifest)) { MESSAGE("no audio oracle manifest; skipped"); return; }
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
    const MixEntry* ie = audio.find("audio.idx");
    REQUIRE(ie != nullptr);
    auto idx_bytes = audio.read(*ie);
    AudioIdx idx = AudioIdx::load(idx_bytes.data(), idx_bytes.size());
    CHECK(idx.clips().size() > 0);        // 1153 in this install
    const MixEntry* be = audio.find("audio.bag");
    REQUIRE(be != nullptr);
    auto bag = audio.read(*be);

    std::ifstream mf(manifest);
    std::string name;
    uint32_t rate = 0, flags = 0, chunk = 0;
    int clips_checked = 0;
    while (mf >> name >> rate >> flags >> chunk) {
        const BagClip* clip = idx.find(name);
        REQUIRE(clip != nullptr);
        CHECK(clip->sample_rate == rate);   // idx fields match the manifest
        CHECK(clip->flags == flags);
        CHECK(clip->chunk_size == chunk);

        AudioClipInfo info;
        auto pcm = decode_bag_clip(bag.data(), bag.size(), *clip, info);
        CHECK(info.sample_rate == (int)rate);

        // Sample count must match what the format implies.
        if (clip->flags & 0x8u) {
            size_t n_chunks =
                ((size_t)clip->size + clip->chunk_size - 1) / clip->chunk_size;
            CHECK(pcm.size() == ((size_t)clip->size - 4 * n_chunks) * 2 + n_chunks);
        } else {
            CHECK(pcm.size() == (size_t)clip->size / 2);
        }

        auto want = slurp_file(base / "audio" / (name + ".pcm"));
        REQUIRE_FALSE(want.empty());
        REQUIRE(want.size() == pcm.size() * 2);
        bool equal = true;
        for (size_t i = 0; i < pcm.size() && equal; i++) {
            uint16_t u = (uint16_t)pcm[i];
            if (want[2 * i] != (uint8_t)(u & 0xff) ||
                want[2 * i + 1] != (uint8_t)(u >> 8))
                equal = false;
        }
        CHECK(equal);                       // byte-for-byte LE int16 parity
        clips_checked++;
    }
    CHECK(clips_checked > 0);   // a manifest exercising zero clips is a failure
}

// W2-GA-22: the streaming IMA decoder reproduces load_wav_ima exactly (the
// menu theme, stereo) -- music is streamed block by block, never decoded whole.
TEST_CASE("audio: WavImaStream blocks concatenate to load_wav_ima (theme.mix grinder.wav)") {
    const std::string base = ra2fixture::install_base();
    std::string path = base;
    if (!path.empty() && path.back() != '/' && path.back() != '\\') path += "/";
    path += "theme.mix";
    ra2assets::MixFile theme;
    try { theme = ra2assets::MixFile::open_path(path); } catch (const std::exception&) {
        MESSAGE("theme.mix absent; skipped");
        return;
    }
    const ra2assets::MixEntry* e = theme.find(std::string("grinder.wav"));
    REQUIRE(e);
    const std::vector<uint8_t> bytes = theme.read(*e);
    ra2assets::WavInfo info;
    const std::vector<int16_t> whole = ra2assets::load_wav_ima(bytes.data(), bytes.size(), info);
    ra2assets::WavImaStream st = ra2assets::WavImaStream::open(bytes);
    CHECK(st.info().channels == info.channels);
    CHECK(st.info().sample_rate == info.sample_rate);
    std::vector<int16_t> acc;
    while (st.next_block(acc)) {}
    CHECK(acc.size() == whole.size());
    CHECK(acc == whole);
    CHECK(st.total_frames() * static_cast<std::size_t>(info.channels) == whole.size());
}

// W5-E2 (R-134) [RE: docs/re/audio.md chunk decoder 0x0040aa70, N-channel
// shape]: two back-to-back channel headers, then 4-byte L / R sub-blocks into
// an interleaved output (the header predictors first).
TEST_CASE("bag decode: stereo IMA-ADPCM (flags 13)") {
    std::vector<uint8_t> bag = {0x10, 0x00, 0, 0,   0x20, 0x00, 0, 0,   // L pred 16, R pred 32
                                0x00, 0x00, 0x00, 0x00,   0x00, 0x00, 0x00, 0x00};   // 8 zero codes each
    AudioClipInfo info;
    auto pcm = decode_bag_clip(bag.data(), bag.size(), clip_of(0, 16, 13, 16), info);
    CHECK(info.channels == 2);
    REQUIRE(pcm.size() == 2 + 16);
    CHECK(pcm[0] == 16);
    CHECK(pcm[1] == 32);
    // code 0 at step 7: delta 7>>3 = 0 -> the predictor holds.
    CHECK(pcm[2] == 16);
    CHECK(pcm[3] == 32);
}
