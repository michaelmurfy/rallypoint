#include "ra2assets/wav.hpp"
#include "ima_adpcm.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace ra2assets {
namespace {
uint32_t rd32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
}  // namespace

std::vector<int16_t> load_wav_ima(const uint8_t* data, std::size_t len, WavInfo& out_info) {
    if (len < 12 || std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0)
        throw std::runtime_error("wav: not a RIFF WAVE");
    std::size_t pos = 12;
    uint16_t tag = 0, channels = 0, block_align = 0;
    uint32_t rate = 0, fact_samples = 0;
    const uint8_t* body = nullptr;
    uint32_t body_len = 0;
    bool have_fmt = false, have_fact = false;
    while (pos + 8 <= len) {
        const uint32_t csz = rd32(data + pos + 4);
        const uint8_t* c = data + pos + 8;
        if (pos + 8 + csz > len) throw std::runtime_error("wav: truncated chunk");
        if (!std::memcmp(data + pos, "fmt ", 4)) {
            if (csz < 16) throw std::runtime_error("wav: short fmt");
            tag = rd16(c); channels = rd16(c + 2); rate = rd32(c + 4);
            block_align = rd16(c + 12);
            have_fmt = true;
        } else if (!std::memcmp(data + pos, "fact", 4) && csz >= 4) {
            fact_samples = rd32(c);
            have_fact = true;
        } else if (!std::memcmp(data + pos, "data", 4)) {
            body = c; body_len = csz;
        }
        pos += 8 + csz + (csz & 1);  // chunks are word-aligned
    }
    if (!have_fmt || !body) throw std::runtime_error("wav: missing fmt/data");
    if (tag != 0x0011) throw std::runtime_error("wav: fmt tag not IMA-ADPCM (0x0011)");
    if (channels != 1 && channels != 2)
        throw std::runtime_error("wav: only mono/stereo IMA supported (docs/re/audio.md)");
    if (block_align <= 4) throw std::runtime_error("wav: bad block align");

    // NEW stereo branch (channels == 2). MS IMA-ADPCM stereo block layout:
    //   header = 4 bytes PER channel: predictor int16 LE, step index (u8),
    //            reserved (u8)  -> 8 bytes for the two channels (L then R).
    //   data   = interleaved 4-byte words: a 4-byte word for ch0 (8 nibbles =
    //            8 samples) then a 4-byte word for ch1, repeating.
    // Nibble order within a byte is low-then-high (same as the mono path).
    // Output is interleaved L,R,L,R,...; out_info.channels = 2. The mono path
    // below is UNREACHED for stereo input, so it stays byte-identical.
    // (Layout confirmed against theme.mix/grinder.wav: block_align 1024,
    //  8-byte dual header, e.g. mid-block L{pred,idx=53} R{pred,idx=48}.)
    if (channels == 2) {
        std::vector<int16_t> pcm;
        for (uint32_t off = 0; off + 8 <= body_len; off += block_align) {
            const uint32_t blk = std::min<uint32_t>(block_align, body_len - off);
            const uint8_t* b = body + off;
            int sampL = static_cast<int16_t>(rd16(b + 0)), idxL = b[2];
            int sampR = static_cast<int16_t>(rd16(b + 4)), idxR = b[6];
            if (idxL > 88 || idxR > 88) throw std::runtime_error("wav: bad block step index");
            pcm.push_back(static_cast<int16_t>(sampL));   // block-header predictors, verbatim
            pcm.push_back(static_cast<int16_t>(sampR));
            // Interleaved 4-byte words, ch0 word then ch1 word.
            for (uint32_t w = 8; w + 8 <= blk; w += 8) {
                const uint8_t* wl = b + w;
                const uint8_t* wr = b + w + 4;
                for (int i = 0; i < 4; ++i) {
                    const int l0 = ima::decode_nibble(wl[i] & 0xf, sampL, idxL);
                    const int l1 = ima::decode_nibble(wl[i] >> 4,  sampL, idxL);
                    const int r0 = ima::decode_nibble(wr[i] & 0xf, sampR, idxR);
                    const int r1 = ima::decode_nibble(wr[i] >> 4,  sampR, idxR);
                    pcm.push_back(static_cast<int16_t>(l0)); pcm.push_back(static_cast<int16_t>(r0));
                    pcm.push_back(static_cast<int16_t>(l1)); pcm.push_back(static_cast<int16_t>(r1));
                }
            }
        }
        if (have_fact && fact_samples * 2 < pcm.size()) pcm.resize(fact_samples * 2);
        out_info.sample_rate = static_cast<int>(rate);
        out_info.channels = 2;
        out_info.bits = 16;
        return pcm;
    }

    std::vector<int16_t> pcm;
    for (uint32_t off = 0; off + 4 <= body_len; off += block_align) {
        const uint32_t blk = std::min<uint32_t>(block_align, body_len - off);
        const uint8_t* b = body + off;
        int sample = static_cast<int16_t>(rd16(b));   // predictor, emitted verbatim
        int index = b[2];
        if (index > 88) throw std::runtime_error("wav: bad block step index");
        pcm.push_back(static_cast<int16_t>(sample));
        for (uint32_t i = 4; i < blk; ++i) {
            pcm.push_back(static_cast<int16_t>(ima::decode_nibble(b[i] & 0xf, sample, index)));
            pcm.push_back(static_cast<int16_t>(ima::decode_nibble(b[i] >> 4, sample, index)));
        }
    }
    if (have_fact && fact_samples < pcm.size()) pcm.resize(fact_samples);
    out_info.sample_rate = static_cast<int>(rate);
    out_info.channels = 1;
    out_info.bits = 16;
    return pcm;
}
// --- W2-GA-22: block-at-a-time IMA-ADPCM WAV decoder -------------------------
WavImaStream WavImaStream::open(std::vector<uint8_t> bytes) {
    WavImaStream w;
    w.bytes_ = std::move(bytes);
    const uint8_t* data = w.bytes_.data();
    const std::size_t len = w.bytes_.size();
    if (len < 12 || std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0)
        throw std::runtime_error("wav: not a RIFF WAVE");
    std::size_t pos = 12;
    uint16_t tag = 0;
    bool have_fmt = false, have_data = false;
    while (pos + 8 <= len) {
        const uint32_t csz = rd32(data + pos + 4);
        const uint8_t* c = data + pos + 8;
        if (pos + 8 + csz > len) throw std::runtime_error("wav: truncated chunk");
        if (!std::memcmp(data + pos, "fmt ", 4)) {
            if (csz < 16) throw std::runtime_error("wav: short fmt");
            tag = rd16(c);
            w.info_.channels = rd16(c + 2);
            w.info_.sample_rate = static_cast<int>(rd32(c + 4));
            w.block_align_ = rd16(c + 12);
            have_fmt = true;
        } else if (!std::memcmp(data + pos, "fact", 4) && csz >= 4) {
            w.total_frames_ = rd32(c);
        } else if (!std::memcmp(data + pos, "data", 4)) {
            w.body_ = static_cast<std::size_t>(c - data);
            w.body_len_ = csz;
            have_data = true;
        }
        pos += 8 + csz + (csz & 1);
    }
    if (!have_fmt || !have_data) throw std::runtime_error("wav: missing fmt/data");
    if (tag != 0x0011) throw std::runtime_error("wav: fmt tag not IMA-ADPCM (0x0011)");
    if (w.info_.channels != 1 && w.info_.channels != 2)
        throw std::runtime_error("wav: only mono/stereo IMA supported (docs/re/audio.md)");
    if (w.block_align_ <= 4) throw std::runtime_error("wav: bad block align");
    if (w.total_frames_ == 0) {
        // No fact chunk: every block's full sample count.
        const uint32_t ch = static_cast<uint32_t>(w.info_.channels);
        const uint32_t per_block = (w.block_align_ - 4 * ch) * 2 / ch + 1;
        w.total_frames_ = (w.body_len_ / w.block_align_) * per_block;
    }
    return w;
}

// Appends the next block's samples (interleaved when stereo) -- the exact
// arithmetic of load_wav_ima, one block at a time. Returns false at the end.
bool WavImaStream::next_block(std::vector<int16_t>& out) {
    if (done()) return false;
    const uint8_t* body = bytes_.data() + body_;
    const uint32_t off = static_cast<uint32_t>(off_);
    const std::size_t before = out.size();
    if (info_.channels == 2) {
        if (off + 8 > body_len_) { off_ = body_len_; return false; }
        const uint32_t blk = std::min<uint32_t>(block_align_, body_len_ - off);
        const uint8_t* b = body + off;
        int sampL = static_cast<int16_t>(rd16(b + 0)), idxL = b[2];
        int sampR = static_cast<int16_t>(rd16(b + 4)), idxR = b[6];
        if (idxL > 88 || idxR > 88) throw std::runtime_error("wav: bad block step index");
        out.push_back(static_cast<int16_t>(sampL));
        out.push_back(static_cast<int16_t>(sampR));
        for (uint32_t w = 8; w + 8 <= blk; w += 8) {
            const uint8_t* wl = b + w;
            const uint8_t* wr = b + w + 4;
            for (int i = 0; i < 4; ++i) {
                const int l0 = ima::decode_nibble(wl[i] & 0xf, sampL, idxL);
                const int l1 = ima::decode_nibble(wl[i] >> 4,  sampL, idxL);
                const int r0 = ima::decode_nibble(wr[i] & 0xf, sampR, idxR);
                const int r1 = ima::decode_nibble(wr[i] >> 4,  sampR, idxR);
                out.push_back(static_cast<int16_t>(l0)); out.push_back(static_cast<int16_t>(r0));
                out.push_back(static_cast<int16_t>(l1)); out.push_back(static_cast<int16_t>(r1));
            }
        }
    } else {
        if (off + 4 > body_len_) { off_ = body_len_; return false; }
        const uint32_t blk = std::min<uint32_t>(block_align_, body_len_ - off);
        const uint8_t* b = body + off;
        int sample = static_cast<int16_t>(rd16(b));
        int index = b[2];
        if (index > 88) throw std::runtime_error("wav: bad block step index");
        out.push_back(static_cast<int16_t>(sample));
        for (uint32_t i = 4; i < blk; ++i) {
            out.push_back(static_cast<int16_t>(ima::decode_nibble(b[i] & 0xf, sample, index)));
            out.push_back(static_cast<int16_t>(ima::decode_nibble(b[i] >> 4, sample, index)));
        }
    }
    off_ += block_align_;
    // Honour the fact-chunk length exactly like load_wav_ima.
    const std::size_t ch = static_cast<std::size_t>(info_.channels);
    const std::size_t got = (out.size() - before) / ch;
    if (frames_out_ + got > total_frames_) {
        out.resize(before + (total_frames_ - std::min<std::size_t>(frames_out_, total_frames_)) * ch);
        frames_out_ = total_frames_;
        off_ = body_len_;
    } else {
        frames_out_ += got;
    }
    return true;
}

}  // namespace ra2assets
