#pragma once
#include <cstdint>
#include <vector>
#include <stdexcept>

namespace ra2assets {

// One decoded isometric theater tile: a frame-local, row-major indexed image.
// w == tile width (cx, 48 or 60), h == tile height (cy, cx/2). `indices` holds
// w*h palette indices; index 0 is transparent (the out-of-diamond corners are
// always 0). Compositing tiles into a map canvas is a separate concern (Task 9).
struct TileImage {
    int w = 0, h = 0;
    std::vector<uint8_t> indices;
};

// Decoder for TS/RA2 `.tmp` isometric theater templates (as found in
// temperat.mix / isotemp.mix, extension .tem/.sno/.urb). A template is a grid
// of cblocks_x*cblocks_y diamond tiles; each present tile is a diamond of
// cx*cy/2 palette-index bytes that unpacks into a cx*cy rectangle with the
// four corner triangles left transparent (0).
class TmpFile {
public:
    // Parse the header + index table. Throws std::runtime_error (with context)
    // on a malformed/truncated header or index table. Individual tile pixels
    // are validated lazily in decode(i).
    static TmpFile load(std::vector<uint8_t> bytes);

    int count() const { return cblocks_x_ * cblocks_y_; }   // cblocks_x*cblocks_y
    int tile_w() const { return cx_; }                      // cx
    int tile_h() const { return cy_; }                      // cy

    int cblocks_x() const { return cblocks_x_; }
    int cblocks_y() const { return cblocks_y_; }

    // Decode tile i (0..count-1) into a frame-local cx*cy indexed buffer. An
    // absent tile (index-table entry 0) decodes to an all-zero (transparent)
    // buffer. Throws std::runtime_error if i is out of range or the tile's
    // image header / diamond pixels fall outside the file.
    TileImage decode(int i) const;

    // --- Base-diamond placement offset (image header x, y) -----------------
    // The screen-space offset of the base diamond relative to the cell anchor
    // (top-left of the cx*cy bounding rect). For stock flat terrain this is
    // (0,0); it is the reference origin the extra image's offset is measured
    // against. Returns 0 for an absent tile. Throws if i is out of range or
    // the image header is out of bounds.
    int tile_x(int i) const;
    int tile_y(int i) const;

    // --- Extra image ("cliff/slope face") ----------------------------------
    // TS/RA2 cliff/slope tiles carry their visible above-ground geometry in an
    // "extra image": a PLAIN (not diamond-packed) cx_extra*cy_extra rectangle
    // of palette indices (index 0 = transparent), stored at `extra_ofs`
    // (relative to the image header, same base the diamond offset uses --
    // matches XCC Ctmp_ts_file::get_extra_data). It is placed at (x_extra,
    // y_extra) in the SAME cell-anchor space as the base diamond's (x, y), so
    // its offset relative to the base is (x_extra - x, y_extra - y). The face
    // typically rises UPWARD from the diamond (y_extra < y).
    //
    // has_extra(i): true iff the tile's has_extra_data flag bit is set (false
    //   for an absent tile). extra_x/extra_y return the raw x_extra/y_extra.
    //   All throw if i is out of range / the header is out of bounds.
    bool has_extra(int i) const;
    int extra_x(int i) const;
    int extra_y(int i) const;

    // Decode the extra image into a cx_extra*cy_extra row-major indexed buffer
    // (TileImage.w == cx_extra, h == cy_extra). Returns an empty (w==h==0)
    // image if the tile is absent or has no extra data. Throws if i is out of
    // range, the extra dimensions are non-positive/implausible, or the extra
    // pixels fall outside the file.
    TileImage decode_extra(int i) const;

    // G-09: raw TMP image header (XCC t_tmp_image_header; radar RGB at 43..48).
    const uint8_t* image_header(int i) const;
private:
    // Bounds-checked pointer to tile i's 52-byte image header, or nullptr if
    // the tile is absent (index-table entry 0). Throws if i is out of range or
    // the header would fall outside the file.

    std::vector<uint8_t> bytes_;
    std::vector<uint32_t> index_;   // per-tile byte offset to its image header (0 = absent)
    int cblocks_x_ = 0, cblocks_y_ = 0;
    int cx_ = 0, cy_ = 0;
};

} // namespace ra2assets
