#include "doctest.h"
#include "ra2assets/bink.hpp"
#include "ra2assets/fileio.hpp"

#include <cstdlib>
#include <string>
#include <vector>

using namespace ra2assets;

// W5-E2 (R-134): Bink Audio (RDFT) against ffmpeg's decode of the same file
// (tools/make_bink_audio_fixture.py builds the file; the .s16 is ffmpeg's
// s16le output). Mono and stereo; within +-2 LSB.
namespace {
void check_against_ffmpeg(const std::string& stem) {
    const std::string dir = std::string(RA2_TEST_DATA_DIR) + "/";
    const BinkFile bf = BinkFile::parse(read_file(dir + stem + ".bik"));
    REQUIRE(bf.audio.size() == 1);
    const BinkAudioTrack& t = bf.audio[0];
    BinkAudioDecoder dec(t.sample_rate, t.channels(), t.dct());
    std::vector<int16_t> pcm;
    for (int f = 0; f < bf.num_frames; ++f) {
        const auto pk = bf.audio_packet(f, 0);
        if (pk.first) dec.decode_packet(pk.first, pk.second, pcm);
    }
    const std::vector<uint8_t> ref = read_file(dir + stem + ".s16");
    REQUIRE(pcm.size() * 2 == ref.size());
    int worst = 0;
    for (std::size_t i = 0; i < pcm.size(); ++i) {
        const int r = static_cast<int16_t>(ref[2 * i] | (ref[2 * i + 1] << 8));
        worst = std::max(worst, std::abs(r - pcm[i]));
    }
    CHECK(worst <= 2);
    // The video part of each frame skips the audio packet.
    CHECK(bf.frame(0).second == 4);
}
}  // namespace

TEST_CASE("bink audio: RDFT mono matches ffmpeg") { check_against_ffmpeg("bink_audio_rdft"); }
TEST_CASE("bink audio: RDFT stereo matches ffmpeg") { check_against_ffmpeg("bink_audio_rdft_st"); }
TEST_CASE("bink audio: DCT stereo matches ffmpeg") { check_against_ffmpeg("bink_audio_dct_st"); }
