#include "ra2assets/audio_bag.hpp"
#include "ima_adpcm.hpp"
#include <stdexcept>
#include <string>

namespace ra2assets {

// ---------------------------------------------------------------------------
// audio.idx (GABA) index. On-disk layout (little-endian, packed), verified
// against the real install: 12-byte header + count * 36-byte entries; the
// real audio.idx is 41,520 = 12 + 1153*36 bytes exactly. Corroborated by
// XCC misc/cc_structures.h t_audio_idx_header / t_audio_idx_entry (read
// off-repo at D:\ra2-re\reference\xcc -- no GPL code copied here) and the
// game's own loader (docs/re/audio.md).
//   header: char magic[4] = "GABA"; u32 version (=2); u32 count
//   entry : char fname[16] (NUL-padded, no extension); u32 offset; u32 size;
//           u32 sample_rate; u32 flags; u32 chunk_size
// ---------------------------------------------------------------------------
static constexpr size_t kIdxHeader = 12;
static constexpr size_t kIdxEntry  = 36;

// audio.bag entry flags. Real-install histogram: 12 x1126, 6 x21, 7 x4,
// 13 x2. Semantics per the game loader (docs/re/audio.md), cross-checked
// against XCC xse.cpp Cxse::extract/insert and the chunk_size correlation
// (every bit-1 entry has chunk_size 0; every bit-3 entry has 512/1024):
//   bit 0 (0x1) stereo
//   bit 1 (0x2) uncompressed 16-bit LE PCM
//   bit 2 (0x4) set on every real entry (16-bit marker; no decode effect)
//   bit 3 (0x8) IMA-ADPCM 4-bit, chunked per chunk_size
static constexpr uint32_t kFlagStereo = 0x1u;
static constexpr uint32_t kFlagPcm    = 0x2u;
static constexpr uint32_t kFlagAdpcm  = 0x8u;

static uint32_t r32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static char upcase(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool iequal(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (upcase(a[i]) != upcase(b[i])) return false;
    return true;
}

AudioIdx AudioIdx::load(const uint8_t* data, size_t len) {
    if (len < kIdxHeader)
        throw std::runtime_error("audio.idx: smaller than 12-byte header (" +
                                 std::to_string(len) + " bytes)");
    if (!(data[0] == 'G' && data[1] == 'A' && data[2] == 'B' && data[3] == 'A'))
        throw std::runtime_error("audio.idx: bad magic (want \"GABA\")");
    const uint32_t version = r32(data + 4);
    if (version != 2)
        throw std::runtime_error("audio.idx: unsupported version " +
                                 std::to_string(version) + " (want 2)");
    const uint32_t count = r32(data + 8);
    if (count > 1000000u)
        throw std::runtime_error("audio.idx: implausible entry count " +
                                 std::to_string(count));
    // Mirror XCC Caudio_idx_file::is_valid: header + count*entry must EQUAL
    // the byte length exactly.
    if (len != kIdxHeader + (size_t)count * kIdxEntry)
        throw std::runtime_error("audio.idx: size " + std::to_string(len) +
                                 " != 12 + " + std::to_string(count) + "*36");
    AudioIdx idx;
    idx.clips_.reserve(count);
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t* e = data + kIdxHeader + (size_t)i * kIdxEntry;
        BagClip c;
        size_t n = 0;
        while (n < 16 && e[n] != 0) n++;          // fname[16], NUL-padded
        c.name.assign(reinterpret_cast<const char*>(e), n);
        c.offset      = r32(e + 16);
        c.size        = r32(e + 20);
        c.sample_rate = r32(e + 24);
        c.flags       = r32(e + 28);
        c.chunk_size  = r32(e + 32);
        idx.clips_.push_back(std::move(c));
    }
    return idx;
}

const BagClip* AudioIdx::find(const std::string& name) const {
    // Case-insensitive: the game resolves sound names from INI keys whose
    // casing is inconsistent (docs/re/audio.md).
    for (const BagClip& c : clips_)
        if (iequal(c.name, name)) return &c;
    return nullptr;
}

// ---------------------------------------------------------------------------
// IMA-ADPCM step/index tables + per-nibble cascade: shared with the RIFF
// fmt-0x0011 WAV decoder (src/wav.cpp) via src/ima_adpcm.hpp -- the same
// constants the game's own decoder in gamemd.exe uses (located via
// constant-table anchoring; addresses in docs/re/audio.md). Extraction is
// behavior-identical; this file's oracle suite (tests/test_audio.cpp) proves
// it byte-for-byte against XCC's own decode.
// ---------------------------------------------------------------------------

std::vector<int16_t> decode_bag_clip(const uint8_t* bag_data, size_t bag_len,
                                     const BagClip& clip, AudioClipInfo& out_info) {
    out_info.sample_rate = (int)clip.sample_rate;
    out_info.channels    = (clip.flags & kFlagStereo) ? 2 : 1;
    out_info.bits        = 16;

    if (clip.size == 0)
        return {};
    if (clip.offset > bag_len || clip.size > bag_len - clip.offset)
        throw std::runtime_error("bag: clip '" + clip.name + "' range [" +
            std::to_string(clip.offset) + ", +" + std::to_string(clip.size) +
            ") exceeds bag (" + std::to_string(bag_len) + " bytes)");
    const uint8_t* src = bag_data + clip.offset;

    if (clip.flags & kFlagPcm) {
        // Uncompressed 16-bit little-endian PCM; stereo data is already
        // interleaved on disk, so mono and stereo share this passthrough.
        if (clip.size % 2 != 0)
            throw std::runtime_error("bag: PCM clip '" + clip.name +
                "' has odd byte size " + std::to_string(clip.size));
        std::vector<int16_t> out(clip.size / 2);
        for (size_t i = 0; i < out.size(); i++)
            out[i] = (int16_t)((uint16_t)src[2 * i] |
                               ((uint16_t)src[2 * i + 1] << 8));
        return out;
    }

    if (!(clip.flags & kFlagAdpcm))
        throw std::runtime_error("bag: clip '" + clip.name +
            "' has unsupported flags " + std::to_string(clip.flags) +
            " (neither PCM bit 1 nor ADPCM bit 3)");
    if (out_info.channels == 2) {
        // W5-E2 (R-134) [RE: docs/re/audio.md -- chunk decoder 0x0040aa70
        // generalises to N channels: one 4-byte header per channel back to
        // back (L then R), then 4-byte / 8-nibble sub-blocks cycling L, R,
        // L, R into an interleaved 16-bit output; headers are validated].
        // Only uslide1 / uslide2 use it in the install.
        if (clip.chunk_size < 8)
            throw std::runtime_error("bag: stereo ADPCM clip '" + clip.name + "' bad chunk_size");
        std::vector<int16_t> out;
        size_t remaining = clip.size;
        const uint8_t* p = src;
        while (remaining > 0) {
            const size_t chunk = remaining < clip.chunk_size ? remaining : clip.chunk_size;
            if (chunk < 8)
                throw std::runtime_error("bag: stereo ADPCM clip '" + clip.name + "' trailing bytes");
            int sl = (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)), il = p[2];
            int sr = (int16_t)((uint16_t)p[4] | ((uint16_t)p[5] << 8)), ir = p[6];
            if (il > 88 || ir > 88)
                throw std::runtime_error("bag: stereo ADPCM clip '" + clip.name + "' step index > 88");
            out.push_back((int16_t)sl);
            out.push_back((int16_t)sr);
            for (size_t w = 8; w + 8 <= chunk; w += 8) {
                const uint8_t* wl = p + w;
                const uint8_t* wr = p + w + 4;
                for (int i = 0; i < 4; ++i) {
                    const int l0 = ima::decode_nibble(wl[i] & 0xf, sl, il);
                    const int l1 = ima::decode_nibble(wl[i] >> 4, sl, il);
                    const int r0 = ima::decode_nibble(wr[i] & 0xf, sr, ir);
                    const int r1 = ima::decode_nibble(wr[i] >> 4, sr, ir);
                    out.push_back((int16_t)l0); out.push_back((int16_t)r0);
                    out.push_back((int16_t)l1); out.push_back((int16_t)r1);
                }
            }
            p += chunk;
            remaining -= chunk;
        }
        return out;
    }
    if (clip.chunk_size < 4)
        throw std::runtime_error("bag: ADPCM clip '" + clip.name +
            "' bad chunk_size " + std::to_string(clip.chunk_size) +
            " (must hold the 4-byte chunk header)");

    // Chunked IMA-ADPCM (docs/re/audio.md): the clip is chunk_size-byte
    // chunks (last may be short); each opens with a 4-byte header
    // {i16 predictor; u8 step_index; u8 reserved} whose predictor is emitted
    // verbatim as the chunk's first sample, then (chunk_bytes-4)*2 nibbles
    // decode LOW nibble first. Predictor/index restart from every chunk
    // header. Total samples: (size - 4*n_chunks)*2 + n_chunks.
    std::vector<int16_t> out;
    const size_t n_chunks =
        ((size_t)clip.size + clip.chunk_size - 1) / clip.chunk_size;
    if ((size_t)clip.size >= 4 * n_chunks)
        out.reserve(((size_t)clip.size - 4 * n_chunks) * 2 + n_chunks);

    size_t remaining = clip.size;
    const uint8_t* p = src;
    while (remaining > 0) {
        const size_t chunk =
            remaining < clip.chunk_size ? remaining : clip.chunk_size;
        if (chunk < 4)
            throw std::runtime_error("bag: ADPCM clip '" + clip.name +
                "' trailing " + std::to_string(chunk) +
                " byte(s) cannot hold a 4-byte chunk header");
        int sample = (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
        int index  = p[2];                        // p[3] reserved
        if (index > 88)
            throw std::runtime_error("bag: ADPCM clip '" + clip.name +
                "' chunk step index " + std::to_string(index) + " > 88");
        out.push_back((int16_t)sample);
        const size_t nibbles = (chunk - 4) * 2;
        const uint8_t* d = p + 4;
        for (size_t i = 0; i < nibbles; i++) {
            int code = d[i >> 1];
            code = (i & 1) ? (code >> 4) : (code & 0xf);   // low nibble first
            out.push_back((int16_t)ima::decode_nibble(code, sample, index));
        }
        p += chunk;
        remaining -= chunk;
    }
    return out;
}

} // namespace ra2assets
