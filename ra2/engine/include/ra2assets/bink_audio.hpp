#pragma once
// W5-E2 (R-134): the Bink Audio decoder (RDFT and DCT variants) for movie
// files that carry audio tracks. The RA2 install's own Bink files carry none
// (docs/re/bink.md §1: ra2ts_s.bik num_tracks = 0); the campaign movies a
// player supplies do. [RE-FALLBACK: the decoder follows the published Bink
// Audio bitstream (as implemented by ffmpeg's binkaudio.c, version 'i'
// files); it is checked against ffmpeg's decode of a crafted file in
// tests/test_bink_audio.cpp, not against binkw32.dll.]
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ra2assets {

struct BinkAudioTrack {
    int sample_rate = 0;
    int flags = 0;          // bit 13 stereo, bit 15 DCT (else RDFT)
    std::uint32_t id = 0;
    int channels() const { return (flags & 0x2000) ? 2 : 1; }
    bool dct() const { return (flags & 0x1000) != 0; }
};

class BinkAudioDecoder {
public:
    // `version_b` = a 'BIKb' file (fixed 16-coefficient runs, IEEE floats).
    BinkAudioDecoder(int sample_rate, int channels, bool dct, bool version_b = false);
    // One audio packet (after its u32 length: the u32 sample count, then the
    // blocks). Appends interleaved S16 samples to `out`.
    void decode_packet(const std::uint8_t* data, std::size_t size, std::vector<std::int16_t>& out);
    int channels() const { return out_channels_; }
    int sample_rate() const { return out_rate_; }

private:
    void decode_block(class BinkAudioBits& gb, std::vector<std::int16_t>& out);
    int out_channels_, out_rate_;
    int channels_;          // coded channels (1 for RDFT: interleaved in the transform)
    bool dct_, version_b_;
    int frame_len_, overlap_len_, block_size_, num_bands_;
    float root_;
    std::vector<float> quant_table_;
    std::vector<int> bands_;
    std::vector<std::vector<float>> previous_;
    bool first_ = true;
};

}  // namespace ra2assets
