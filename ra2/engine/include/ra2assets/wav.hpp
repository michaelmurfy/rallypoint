#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

namespace ra2assets {

struct WavInfo { int sample_rate = 0; int channels = 1; int bits = 16; };

// Decode a RIFF fmt-0x0011 (IMA-ADPCM) WAV -- the container EVA speech and
// music ship in (docs/re/audio.md). Shares its per-nibble arithmetic with
// the BAG chunked decoder (src/audio_bag.cpp) via src/ima_adpcm.hpp; the two
// containers use the identical IMA cascade, confirmed independently against
// two separate copies of the game's own decoder.
//
// Mono (channels==1) yields one sample per frame; stereo (channels==2, e.g.
// the menu theme theme.mix/grinder.wav) decodes the MS IMA-ADPCM stereo
// layout -- an 8-byte per-block dual header (L then R: int16 predictor + u8
// step index + u8 reserved) followed by interleaved 4-byte words (ch0 word
// then ch1 word) -- into interleaved L,R,L,R samples with out_info.channels=2.
//
// Throws std::runtime_error on: non-RIFF/WAVE input, missing fmt/data
// chunks, a fmt tag other than 0x0011, or channels outside {1, 2}.
std::vector<int16_t> load_wav_ima(const uint8_t* data, std::size_t len, WavInfo& out_info);

// W2-GA-22: the same decode one IMA block at a time, so a music track can be
// streamed (decoded just ahead of playback on a worker thread) instead of
// decoded whole up front. Concatenating every next_block() output equals
// load_wav_ima() exactly. Owns the WAV bytes.
class WavImaStream {
public:
    static WavImaStream open(std::vector<uint8_t> bytes);   // throws like load_wav_ima
    const WavInfo& info() const { return info_; }
    std::size_t total_frames() const { return total_frames_; }   // per channel
    bool done() const { return off_ >= body_len_; }
    bool next_block(std::vector<int16_t>& out);                 // appends; false at end
private:
    std::vector<uint8_t> bytes_;
    WavInfo info_;
    std::size_t body_ = 0, off_ = 0, frames_out_ = 0, total_frames_ = 0;
    uint32_t body_len_ = 0;
    uint16_t block_align_ = 0;
};

}  // namespace ra2assets
