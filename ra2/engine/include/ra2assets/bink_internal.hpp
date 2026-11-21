#pragma once
#include <stdexcept>
// INTERNAL entropy-layer primitives for the from-scratch Bink 1 ("BIKi" rev-'i')
// video decoder. This header is NOT part of the public ra2assets API: it exposes
// the bit reader, Huffman-tree machinery, and per-plane "bundle" decode
// infrastructure that the frame decoder (Task 3) consumes. Everything here is a
// faithful C++ port of docs/re/bink_proof_decoder.py (byte-exact vs ffmpeg on all
// 431 frames of ra2ts_s.bik); tables are verbatim from ffmpeg binkdata.h
// (docs/re/bink.md §10). Definitions live in src/bink.cpp.
//
// [RE: docs/re/bink.md §3 (bit reader), §4 (Huffman), §5 (bundles)]
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ra2assets {
namespace binkdetail {

// ---------------------------------------------------------------------------
// Static tables (defined in src/bink.cpp; verbatim from ffmpeg binkdata.h).
// ---------------------------------------------------------------------------
extern const uint8_t kScan[64];           // bink_scan       (§10.1) scan->raster
extern const uint8_t kTreeBits[16][16];   // bink_tree_bits  (§10.2) LSB-first codes
extern const uint8_t kTreeLens[16][16];   // bink_tree_lens  (§10.2) code bit-lengths
extern const uint8_t kPatterns[16][64];   // bink_patterns   (§10.3) 16 RUN scan orders

// ---------------------------------------------------------------------------
// LSB-first bit reader (docs/re/bink.md §3; ffmpeg BITSTREAM_READER_LE).
// Bits are consumed LSB-first from little-endian bytes; the first-read bit is
// bit 0 of the result. Bounds-safe: reads past the end of the buffer yield 0
// bits (never an OOB access) -- the caller/format guarantees well-formed input.
// ---------------------------------------------------------------------------
class BitReader {
public:
    BitReader(const uint8_t* data, std::size_t size_bytes)
        : data_(data), size_bits_(size_bytes * 8), size_bytes_(size_bytes), pos_(0) {}

    // Read n (0..32) bits LSB-first.
    uint32_t get_bits(int n) {
        if (n <= 0) return 0;
        const std::size_t bytepos = pos_ >> 3;
        const int bitoff = static_cast<int>(pos_ & 7u);
        // Gather 8 bytes (64 bits) so bitoff (<=7) + n (<=32) always fits.
        uint64_t acc = 0;
        for (int i = 0; i < 8; ++i)
            acc |= static_cast<uint64_t>(byte_at(bytepos + static_cast<std::size_t>(i))) << (8 * i);
        const uint64_t mask = (n >= 64) ? ~0ull : ((1ull << n) - 1ull);
        const uint32_t v = static_cast<uint32_t>((acc >> bitoff) & mask);
        pos_ += static_cast<std::size_t>(n);
        return v;
    }

    // Read a single bit.
    uint32_t get_bit() {
        const uint32_t v = (byte_at(pos_ >> 3) >> (pos_ & 7u)) & 1u;
        ++pos_;
        return v;
    }

    // Advance to the next 32-bit boundary relative to the packet start.
    void align32() {
        if (pos_ & 31u) pos_ += 32u - (pos_ & 31u);
    }

    std::size_t pos() const { return pos_; }
    std::size_t size() const { return size_bits_; }
    bool at_end() const { return pos_ >= size_bits_; }

private:
    uint32_t byte_at(std::size_t idx) const {
        return idx < size_bytes_ ? static_cast<uint32_t>(data_[idx]) : 0u;
    }
    const uint8_t* data_;
    std::size_t size_bits_;
    std::size_t size_bytes_;
    std::size_t pos_;
};

// ---------------------------------------------------------------------------
// Huffman tree descriptor (docs/re/bink.md §4.2). Selects one of the 16 static
// trees (`vlc_num`) plus a permutation of the 16 symbols. Default is the trivial
// identity descriptor (tree 0, syms[i]=i).
// ---------------------------------------------------------------------------
struct TreeDesc {
    int vlc_num = 0;
    uint8_t syms[16];
    TreeDesc() {
        for (int i = 0; i < 16; ++i) syms[i] = static_cast<uint8_t>(i);
    }
};

// Decode one Huffman leaf index (0..15) from the static tree `vlc_num`,
// LSB-first per §4.1.
int decode_vlc(BitReader& br, int vlc_num);

// Decode a value through a bundle's tree: syms[decode_vlc(...)] (§4.3).
inline uint8_t get_huff(BitReader& br, const TreeDesc& t) {
    return t.syms[decode_vlc(br, t.vlc_num)];
}

// Read a tree descriptor from the bitstream (`read_tree`, §4.2).
TreeDesc read_tree(BitReader& br);

// av_log2 = floor(log2(v)) for v>=1 (ffmpeg semantics; used for count widths).
int av_log2(uint32_t v);
// CEIL_LOG2_PLUS(x) = av_log2(x + 511) + 1  (docs/re/bink.md §5.1).
inline int ceil_log2_plus(int x) {
    return av_log2(static_cast<uint32_t>(x + 511)) + 1;
}

// ---------------------------------------------------------------------------
// Bundles (docs/re/bink.md §5). Nine typed value streams decoded in batches and
// consumed FIFO by the block loop. Values are stored as int to hold signed
// motion/DC entries as well as unsigned bytes.
// ---------------------------------------------------------------------------
enum BundleId {
    BT = 0,   // BLOCK_TYPES
    SBT,      // SUB_BLOCK_TYPES
    COL,      // COLORS
    PAT,      // PATTERN
    XO,       // X_OFF (motion x)
    YO,       // Y_OFF (motion y)
    IDC,      // INTRA_DC
    PDC,      // INTER_DC
    RUN,      // RUN lengths
    kNumBundles = 9
};

struct Bundle {
    int lenbits = 0;             // batch-count bit width (§5.1)
    TreeDesc tree;               // per-bundle Huffman tree (unused for DC bundles)
    std::vector<int> data;       // decoded values, appended in decode order
    int decoded = 0;             // number appended so far this plane
    int consumed = 0;            // number consumed via get_value
    bool dead = false;           // exhausted-flag: a zero-count batch was seen

    void reset() {
        data.clear();
        decoded = 0;
        consumed = 0;
        dead = false;
    }
    // Still has undecoded room (mirrors proof's Bundle.empty()).
    bool empty() const { return !dead && decoded <= consumed; }
};

// Per-plane bundle decode state (the entropy half of ffmpeg's BinkContext).
struct BinkContext {
    Bundle b[kNumBundles];
    TreeDesc col_high[16];       // 16 high-nibble context trees for COLORS (§5.4)
    int col_lastval = 0;         // previous COLORS high nibble (resets per plane)

    // --- Per-plane setup (docs/re/bink.md §5.1) ---
    // `aw` = plane width aligned up to 8 (max(display_w>>is_chroma, 8), then &~7);
    // `bw` = block-columns of the plane.
    void set_bundle_lengths(int aw, int bw);
    // Read the per-bundle tree descriptors (incl. the 16 COLORS high-nibble
    // trees) in the fixed order and reset every bundle buffer.
    void read_bundle_trees(BitReader& br);

    // --- Batch refills (docs/re/bink.md §5.2/§5.3), one per bundle per block row.
    // check_count returns the batch element count t (0 => bundle now dead / no
    // refill this row); the read_* helpers append t decoded elements.
    int  check_count(BitReader& br, Bundle& bundle);
    void read_runs(BitReader& br, Bundle& bundle);
    void read_motion(BitReader& br, Bundle& bundle);
    void read_block_types(BitReader& br, Bundle& bundle);
    void read_patterns(BitReader& br, Bundle& bundle);
    void read_colors(BitReader& br, Bundle& bundle);
    void read_dcs(BitReader& br, Bundle& bundle, int has_sign);

    // Consume one buffered value from bundle `id` (§5.4). Signedness is the
    // caller's concern (X_OFF/Y_OFF -> int8, DC -> int16).
    int get_value(int id) {
        Bundle& bundle = b[id];
        // W5-E2: a damaged / foreign file must throw, never read past the bundle.
        if (bundle.consumed < 0 || static_cast<std::size_t>(bundle.consumed) >= bundle.data.size())
            throw std::runtime_error("bink: bundle exhausted (damaged frame)");
        return bundle.data[static_cast<std::size_t>(bundle.consumed++)];
    }
};

}  // namespace binkdetail
}  // namespace ra2assets
