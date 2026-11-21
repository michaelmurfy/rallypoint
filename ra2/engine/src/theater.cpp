#include "ra2assets/theater.hpp"
#include <string>

namespace ra2assets {

// Fixed on-disk sizes of the TMP TS structures (little-endian, packed).
// Cross-checked three ways:
//   * XCC misc/cc_structures.h  : t_tmp_ts_header = 4*i32 = 16;
//     t_tmp_image_header = 9*i32 + a 4-byte bitfield word + (height,
//     terrain_type, ramp_type) + 6 radar bytes + 3 pad = 36+4+3+6+3 = 52.
//   * XCC tmp_ts_file.h get_image(i) = image_header + 1 (pixels start one
//     struct past the header) and get_cb_diamond() = cx*cy/2.
//   * OpenRA TmpTSLoader.IsTmpTS asserts (z_ofs at header+12) == cx*cy/2 + 52,
//     i.e. header size 52 and diamond size cx*cy/2 -- confirmed against real
//     clear01.tem (cx=60,cy=30): z_ofs==952==900+52.
static constexpr size_t kFileHeader  = 16;   // cblocks_x, cblocks_y, cx, cy
static constexpr size_t kImageHeader = 52;

static uint32_t r32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

TmpFile TmpFile::load(std::vector<uint8_t> bytes) {
    TmpFile t;
    t.bytes_ = std::move(bytes);
    const std::vector<uint8_t>& b = t.bytes_;
    if (b.size() < kFileHeader)
        throw std::runtime_error("tmp: file smaller than header (" +
                                 std::to_string(b.size()) + " bytes)");

    t.cblocks_x_ = (int)(int32_t)r32(b.data() + 0);
    t.cblocks_y_ = (int)(int32_t)r32(b.data() + 4);
    t.cx_        = (int)(int32_t)r32(b.data() + 8);
    t.cy_        = (int)(int32_t)r32(b.data() + 12);

    if (t.cblocks_x_ <= 0 || t.cblocks_y_ <= 0)
        throw std::runtime_error("tmp: bad block grid " +
            std::to_string(t.cblocks_x_) + "x" + std::to_string(t.cblocks_y_));
    // The diamond-unpack geometry is only exact when cx is even, a multiple of
    // 4, and cy == cx/2 (max row width 4*(cy/2) == 2*cy must equal cx). This
    // matches XCC's is_valid (cx in {48,60}, 2*cy == cx).
    if (t.cx_ <= 0 || t.cy_ <= 0 || (t.cx_ & 3) != 0 || t.cx_ != 2 * t.cy_)
        throw std::runtime_error("tmp: bad tile dimensions cx=" +
            std::to_string(t.cx_) + " cy=" + std::to_string(t.cy_));

    const long long count = (long long)t.cblocks_x_ * t.cblocks_y_;
    if (count > 1000000LL)
        throw std::runtime_error("tmp: implausible tile count " + std::to_string(count));
    const size_t index_bytes = (size_t)count * 4;
    if (b.size() < kFileHeader + index_bytes)
        throw std::runtime_error("tmp: index table exceeds file (need " +
            std::to_string(kFileHeader + index_bytes) + ", have " +
            std::to_string(b.size()) + ")");

    t.index_.reserve((size_t)count);
    for (long long i = 0; i < count; i++)
        t.index_.push_back(r32(b.data() + kFileHeader + (size_t)i * 4));
    return t;
}

// Offsets of the int32 fields we read within the 52-byte t_tmp_image_header
// (XCC misc/cc_structures.h t_tmp_image_header): x, y, extra_ofs, then the
// extra placement/size block, then a 4-byte flag word whose bit 0 is
// has_extra_data.
static constexpr size_t kOfsX        = 0;
static constexpr size_t kOfsY        = 4;
static constexpr size_t kOfsExtraOfs = 8;
static constexpr size_t kOfsXExtra   = 20;
static constexpr size_t kOfsYExtra   = 24;
static constexpr size_t kOfsCxExtra  = 28;
static constexpr size_t kOfsCyExtra  = 32;
static constexpr size_t kOfsFlags    = 36;
static constexpr uint32_t kHasExtraDataBit = 0x1u;

const uint8_t* TmpFile::image_header(int i) const {
    if (i < 0 || i >= count())
        throw std::runtime_error("tmp: tile index out of range: " + std::to_string(i));
    const uint32_t hdr_off = index_[(size_t)i];
    if (hdr_off == 0)
        return nullptr;                         // absent tile
    if ((size_t)hdr_off + kImageHeader > bytes_.size())
        throw std::runtime_error("tmp: tile " + std::to_string(i) +
            " image header at " + std::to_string(hdr_off) + " exceeds file");
    return bytes_.data() + hdr_off;
}

TileImage TmpFile::decode(int i) const {
    TileImage out;
    out.w = cx_;
    out.h = cy_;
    out.indices.assign((size_t)cx_ * cy_, 0);   // transparent by default

    const uint8_t* hdr = image_header(i);       // validates i and header bounds
    if (hdr == nullptr)
        return out;                             // absent tile -> all transparent
    const uint32_t hdr_off = index_[(size_t)i];

    const size_t size = bytes_.size();

    // Pixels (the diamond) start immediately after the 52-byte image header.
    const size_t pixel_off = (size_t)hdr_off + kImageHeader;
    const size_t diamond   = (size_t)cx_ * cy_ / 2;
    if (pixel_off + diamond > size)
        throw std::runtime_error("tmp: tile " + std::to_string(i) +
            " diamond pixels (" + std::to_string(diamond) + " bytes at " +
            std::to_string(pixel_off) + ") exceed file");

    // Diamond unpack. The cx*cy/2 pixel bytes are an isometric diamond centered
    // in the cx-wide rectangle. Row y's run of opaque pixels has width w(y):
    // the top half (y = 0..cy/2-1) widens by 4 per row -> 4, 8, ..., cx; the
    // bottom half (y = cy/2..cy-1) narrows by 4 per row -> cx-4, ..., 0. Each
    // run is horizontally centered (left margin = (cx - w)/2), leaving the four
    // corner triangles at the default transparent 0. The bytes are read in row
    // order and consume exactly cx*cy/2 of them.
    const uint8_t* src = bytes_.data() + pixel_off;
    const int half = cy_ / 2;
    int width = 0;
    for (int y = 0; y < cy_; y++) {
        if (y < half) width += 4; else width -= 4;
        const int left = (cx_ - width) / 2;
        uint8_t* row = out.indices.data() + (size_t)y * cx_ + left;
        for (int x = 0; x < width; x++)
            *row++ = *src++;
    }
    return out;
}

int TmpFile::tile_x(int i) const {
    const uint8_t* hdr = image_header(i);
    return hdr ? (int)(int32_t)r32(hdr + kOfsX) : 0;
}

int TmpFile::tile_y(int i) const {
    const uint8_t* hdr = image_header(i);
    return hdr ? (int)(int32_t)r32(hdr + kOfsY) : 0;
}

bool TmpFile::has_extra(int i) const {
    const uint8_t* hdr = image_header(i);
    if (hdr == nullptr) return false;
    return (r32(hdr + kOfsFlags) & kHasExtraDataBit) != 0;
}

int TmpFile::extra_x(int i) const {
    const uint8_t* hdr = image_header(i);
    return hdr ? (int)(int32_t)r32(hdr + kOfsXExtra) : 0;
}

int TmpFile::extra_y(int i) const {
    const uint8_t* hdr = image_header(i);
    return hdr ? (int)(int32_t)r32(hdr + kOfsYExtra) : 0;
}

TileImage TmpFile::decode_extra(int i) const {
    TileImage out;                              // empty (w==h==0) by default
    const uint8_t* hdr = image_header(i);       // validates i and header bounds
    if (hdr == nullptr)
        return out;                             // absent tile -> no extra
    if ((r32(hdr + kOfsFlags) & kHasExtraDataBit) == 0)
        return out;                             // no extra image for this tile

    const int cxe = (int)(int32_t)r32(hdr + kOfsCxExtra);
    const int cye = (int)(int32_t)r32(hdr + kOfsCyExtra);
    if (cxe <= 0 || cye <= 0)
        throw std::runtime_error("tmp: tile " + std::to_string(i) +
            " has_extra_data but bad extra size " + std::to_string(cxe) + "x" +
            std::to_string(cye));
    const long long npix = (long long)cxe * cye;
    if (npix > 64LL * 1024 * 1024)              // guard absurd allocation
        throw std::runtime_error("tmp: tile " + std::to_string(i) +
            " implausible extra size " + std::to_string(cxe) + "x" + std::to_string(cye));

    // extra_ofs is relative to the image header position (same base the diamond
    // offset uses); matches XCC get_extra_data = data + index[i] + extra_ofs.
    const int32_t extra_ofs = (int32_t)r32(hdr + kOfsExtraOfs);
    const uint32_t hdr_off = index_[(size_t)i];
    if (extra_ofs < 0)
        throw std::runtime_error("tmp: tile " + std::to_string(i) +
            " negative extra_ofs " + std::to_string(extra_ofs));
    const size_t start = (size_t)hdr_off + (size_t)extra_ofs;
    if (start + (size_t)npix > bytes_.size())
        throw std::runtime_error("tmp: tile " + std::to_string(i) +
            " extra pixels (" + std::to_string(npix) + " bytes at " +
            std::to_string(start) + ") exceed file");

    // The extra image is a plain, row-major cx_extra*cy_extra byte rectangle
    // (NOT diamond-packed); copy it verbatim (index 0 stays transparent).
    out.w = cxe;
    out.h = cye;
    out.indices.assign(bytes_.begin() + start, bytes_.begin() + start + (size_t)npix);
    return out;
}

} // namespace ra2assets
