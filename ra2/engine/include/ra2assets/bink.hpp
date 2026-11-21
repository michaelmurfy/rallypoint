#pragma once
#include <cstdint>
#include <cstddef>
#include <utility>
#include <vector>

#include "ra2assets/bink_audio.hpp"

namespace ra2assets {

// A parsed Bink 1 ("BIKi") video container: the 44-byte little-endian header
// plus the (num_frames+1)-entry frame index. This is the CONTAINER only -- no
// video decoding. [RE: docs/re/bink.md §1]
//
// `frame_offsets` holds the (num_frames+1) raw index entries as stored in the
// file. Entry i is the absolute file offset of frame i's data, and its bit 0 is
// the keyframe flag (1 = keyframe) -- so the raw entry is kept and bit 0 is
// masked off wherever an actual offset is needed (is_keyframe / frame). The
// final entry equals the total file size. [RE: docs/re/bink.md §1 "Frame index"]
struct BinkFile {
    int width = 0, height = 0;
    int num_frames = 0;
    int fps_num = 0, fps_den = 0;
    std::vector<uint32_t> frame_offsets;   // size num_frames+1, raw (bit0 = keyframe)
    std::vector<uint8_t> data;             // the whole file, verbatim
    // W5-E2 (R-134): the audio track headers (none in the RA2 files) --
    // per track u32 max packet size, u16 sample rate + u16 flags, u32 id.
    std::vector<BinkAudioTrack> audio;
    // The audio packet of track t in frame f (its u32 sample count, then the
    // blocks), or {nullptr, 0} when the frame has none for that track.
    std::pair<const uint8_t*, std::size_t> audio_packet(int f, int t) const;

    // True iff frame f's index entry has bit 0 set (keyframe). In the RA2 files
    // only frame 0 is a keyframe.
    bool is_keyframe(int f) const;

    // Frame f's video packet byte range: [offset[f] & ~1, offset[f+1] & ~1)
    // after the frame's audio packets (W5-E2), pointing into `data`. Throws std::out_of_range if f is not in [0,num_frames).
    std::pair<const uint8_t*, std::size_t> frame(int f) const;

    // Parse a Bink container from a whole-file byte buffer. Throws
    // std::runtime_error on a bad magic ("BIK" prefix) or a truncated
    // header/frame-index.
    static BinkFile parse(std::vector<uint8_t> bytes);
};

// ---------------------------------------------------------------------------
// Bink 1 ("BIKi" revision 'i') VIDEO decoder. Decodes a frame into YUV 4:2:0
// planes (Y full resolution, U/V half resolution), byte-exact vs ffmpeg's
// binkvideo decoder. [RE: docs/re/bink.md §2-§10; ported statement-for-statement
// from docs/re/bink_proof_decoder.py, which is byte-exact on all 431 frames of
// ra2ts_s.bik.]
//
// The decoder holds the current and previous frame's planes internally
// (INTER/MOTION/RESIDUE/SKIP blocks reference the previous frame). Because inter
// frames depend on the previous frame, decode is SEQUENTIAL: decode_frame(n)
// requires frame n-1 to have been decoded. Callers may request any frame in any
// order -- decode_frame() transparently (re)decodes forward from the nearest
// preceding keyframe when the requested frame is not the immediate successor of
// the last one decoded. (In the RA2 files only frame 0 is a keyframe, so a
// random-access request rewinds to frame 0.)
//
// Planes are MPEG/limited-range BT.601. Plane buffers are padded to whole 8x8
// blocks (stride = block_cols*8, rows = block_rows*8); the accessors expose the
// padded buffer pointer plus its stride and the cropped DISPLAY dimensions, so a
// consumer reads y_w() bytes from each of the first y_h() rows (row r starts at
// y() + r*y_stride()), and likewise for chroma.
//
// The referenced BinkFile must outlive the decoder.
class BinkVideoDecoder {
public:
    explicit BinkVideoDecoder(const BinkFile& file);
    ~BinkVideoDecoder();
    BinkVideoDecoder(const BinkVideoDecoder&) = delete;
    BinkVideoDecoder& operator=(const BinkVideoDecoder&) = delete;

    // Decode frame n (0-based). Sequential; see the class comment for the
    // random-access / rewind behavior. Throws std::out_of_range if n is out of
    // range. After the call the y()/u()/v() accessors expose frame n.
    void decode_frame(int n);

    // Decoded-plane accessors (valid after the first decode_frame call).
    // y()/u()/v() point at the padded plane buffers (storage: Y, U, V).
    const uint8_t* y() const;
    const uint8_t* u() const;
    const uint8_t* v() const;

    int y_w() const;   // Y display width  (= file width)
    int y_h() const;   // Y display height (= file height)
    int uv_w() const;  // chroma display width  = (width+1)/2
    int uv_h() const;  // chroma display height = (height+1)/2
    int y_stride() const;   // Y  buffer row stride (>= y_w)
    int uv_stride() const;  // UV buffer row stride (>= uv_w)

    // Convert the currently-decoded frame (the last decode_frame call) to a
    // tightly-packed RGBA8888 image, y_w()*y_h()*4 bytes, row-major, R,G,B,A
    // order with A = 255 (the RA2 files carry no alpha plane). Uses integer
    // BT.601 LIMITED-range (color_range=tv) YUV->RGB with the 4:2:0 chroma
    // planes upsampled to full resolution by pixel duplication (nearest, cosited
    // top-left), matching ffmpeg's decoded frames within +/-2 per channel
    // (ffmpeg's PNG export path dithers through swscale). [RE: docs/re/bink.md
    // §11.] Must be called after decode_frame(); returns an empty vector if no
    // frame has been decoded yet.
    std::vector<uint8_t> rgba() const;

private:
    struct Impl;
    Impl* impl_;
};

}  // namespace ra2assets
