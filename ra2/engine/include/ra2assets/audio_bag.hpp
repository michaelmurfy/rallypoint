#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace ra2assets {

// One entry of audio.idx: a named clip inside audio.bag.
// flags (docs/re/audio.md): bit0 stereo, bit1 uncompressed 16-bit PCM,
// bit2 always set on real entries (16-bit marker), bit3 chunked IMA-ADPCM.
struct BagClip {
    std::string name;               // idx name field (no extension)
    uint32_t offset = 0, size = 0;  // clip bytes within audio.bag
    uint32_t sample_rate = 0, flags = 0, chunk_size = 0;
};

// Parser for audio.idx ("GABA" magic, version 2): 12-byte header +
// count * 36-byte entries (see docs/re/audio.md).
class AudioIdx {
public:
    static AudioIdx load(const uint8_t* data, size_t len);
    const BagClip* find(const std::string& name) const;  // nullptr if absent
    const std::vector<BagClip>& clips() const { return clips_; }
private:
    std::vector<BagClip> clips_;
};

struct AudioClipInfo { int sample_rate = 0; int channels = 1; int bits = 16; };

// Decode one clip's bytes (from audio.bag) to interleaved 16-bit PCM.
// Raw-PCM clips (flags bit 1) pass through; IMA-ADPCM clips (flags bit 3)
// decode chunk-by-chunk per clip.chunk_size. Stereo IMA-ADPCM throws
// (unimplemented; 2 of 1153 real entries -- docs/re/audio.md).
std::vector<int16_t> decode_bag_clip(const uint8_t* bag_data, size_t bag_len,
                                     const BagClip& clip, AudioClipInfo& out_info);

} // namespace ra2assets
