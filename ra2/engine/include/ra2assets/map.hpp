#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace ra2assets {

// One decoded [IsoMapPack5] cell record. x/y are the RAW coordinates stored
// in the record itself -- an isometric coordinate space used only inside
// IsoMapPack5 (confirmed empirically against a real 103x110 map: both range
// roughly 1..212, i.e. on the order of width+height, NOT the same space as
// MapFile::width()/height(), which come from [Map] Size, the map's ordinary
// playable-grid rectangle). A raw tile index of 0xFFFF ("no tile", per
// ModEnc) is NOT dropped: the game replaces it with a clear tile when
// loading, so detail::parse_cell_records materializes it as tile 0
// ("Clear") with subtile 0, keeping the record's x/y/level. So every
// in-bounds record becomes exactly one MapCell here -- none are dropped --
// including the very common, legitimate tile id 0 ("Clear" terrain).
struct MapCell {
    int x = 0, y = 0;
    uint32_t tile = 0;
    uint8_t subtile = 0;
    uint8_t level = 0;
};

namespace detail {
// Parse an ALREADY-DECOMPRESSED IsoMapPack5 byte buffer (i.e. the output of
// decompress_mappack(..., MapPackCodec::LZO) -- NOT the base64/LZO-packed
// bytes) into cell records. Each record is 11 bytes, little-endian:
//   int16  X
//   int16  Y
//   int32  Tile      (real layout, confirmed against Task 2b's real-map
//                      dump: a uint16 tile id followed by an always-zero
//                      uint16 pad half, read together as one LE int32 --
//                      equivalently OpenRA's ReadTiles reads them as two
//                      separate uint16/int16 fields)
//   uint8  SubTile
//   uint8  Level
//   uint8  (ignored -- "IceGrowth" per some community docs)
// Walks floor(len/11) records; any trailing <11 bytes (real maps emit a
// small zero pad, e.g. 4 bytes for the reference 103x110 map) are ignored,
// not treated as a malformed/short record.
//
// Every in-map record becomes exactly one MapCell -- none are dropped.
// Records whose Tile == 0xFFFF ("no tile", per ModEnc -- the low 16 bits
// only; the upper Tile half is always 0 in practice, so the combined LE
// int32 reads as plain 65535) are MATERIALIZED as tile 0 ("Clear")/subtile 0
// rather than dropped, since the game itself substitutes a clear tile for
// this sentinel when loading -- the coordinate is real, walkable ground,
// not void. (NOTE vs. an earlier version of this code: it used to drop
// these records, which left holes in terrain rendering/pathfinding; see
// task-5-report.md's "Fix: 0xFFFF -> Clear" section.) Every other Tile
// value (INCLUDING literal 0, the common "Clear" terrain tile) is kept
// as-is. The brief's original guess of a 0xFFFFFFFF sentinel is not
// specially handled -- ModEnc specifies the sentinel as a 16-bit tile
// index, and 0xFFFFFFFF has never been observed in real data.
//
// Exposed here (not `static` in map.cpp) specifically so Task 5's synthetic
// unit test can drive the record-walking logic directly with hand-built raw
// bytes: hand-synthesizing a real LZO1X-compressed [IsoMapPack5] blob for a
// black-box MapFile::parse() test is impractical (LZO1X has no trivial
// encoder in this codebase -- see test_lzo1x.cpp). This function is the
// entire record-walking contract MapFile::parse() relies on for terrain, so
// testing it directly with raw bytes exercises exactly the same logic.
std::vector<MapCell> parse_cell_records(const uint8_t* data, size_t len);
// W2-GA-04: append a tile-0/level-0 cell for every in-diamond (x,y) of a
// w x h [Map] Size that `cells` lacks (the original clears the whole map
// to ground before applying IsoMapPack5). No-op on a complete pack.
void fill_missing_diamond(std::vector<MapCell>& cells, int w, int h, uint8_t level = 0);
} // namespace detail

// One [Waypoints] entry, decoded [RE: maps.md §4.3, CONFIRMED]. The INI value
// is a PACKED cell: x = value % 1000, y = value / 1000, giving a cell in the
// SAME coordinate space as the [IsoMapPack5] cell records (MapCell::x/y).
// Triple-sourced: OpenRA ImportGen2MapCommand.ReadWaypoints, XCC
// map_ts_ini_reader, and this install's own [Header] affine cross-check
// (HeaderY = round((x+y)/2), HeaderX = (x-y)/2 + 255.5+-0.5 hold ONLY under
// this decode order). Waypoints 0..7 are the MULTIPLAYER START POSITIONS and
// their count equals [Header] NumberStartingPoints on 267/267 MP maps.
// [RE-FALLBACK: maps.md §4.3 item 4 -- the meaning of waypoints > 7 (10-55,
// 98, 99, 699=map center) was NOT determined; in-map script references were
// ruled out. Only 0..7 matter for house placement.]
struct MapWaypoint { int index = 0; int x = 0; int y = 0; };

class MapFile {
public:
    // Parses a full map/.mpr INI: `[Map] Theater=`/`Size=`, and the
    // `[IsoMapPack5]` terrain section (concatenated base64 -> LZO1X Format5
    // -> cell records via detail::parse_cell_records). Throws
    // std::runtime_error (with context) if the input has no `[Map]` section.
    // A missing `[IsoMapPack5]` section is NOT an error -- cells() is simply
    // empty in that case.
    static MapFile parse(const std::string& ini_text);

    // Playable map dimensions, from `[Map] Size=x,y,w,h` (width is the 3rd
    // comma-separated field, height the 4th). 0 if `[Map]`/`Size=` is
    // absent or a field fails to parse.
    int width() const { return width_; }
    int height() const { return height_; }

    // Decoded, sentinel-filtered [IsoMapPack5] cell records, in file/decode
    // order. Empty if the map has no [IsoMapPack5] section.
    const std::vector<MapCell>& cells() const { return cells_; }

    // [Map] Theater=, e.g. "TEMPERATE". Empty string if absent.
    std::string theater() const { return theater_; }

    // First cell whose (x,y) match the query, in the RAW IsoMapPack5
    // coordinate space (see MapCell) -- or nullptr if none. Linear scan:
    // fine for the vertical slice's one-shot-per-map lookups; revisit with
    // an index if a caller needs repeated random-access lookups.
    const MapCell* at(int x, int y) const;

    // [OverlayPack]/[OverlayDataPack]: 262,144-byte (512x512) per-cell
    // arrays in the SAME raw iso coordinate space as MapCell::x/y,
    // cell = x + y*512. 0xFF in the overlay array = "no overlay".
    // Accessors return -1 for out-of-range, absent section, or no-overlay;
    // otherwise the raw byte. [RE: economy.md §5]
    bool has_overlay() const { return !overlay_.empty(); }
    int overlay_at(int x, int y) const;
    int overlay_data_at(int x, int y) const;

    // ALL [Waypoints] entries, ascending by index. Empty if the section is
    // absent (campaign maps have one; a map without the section is legal).
    // [RE: maps.md §4.3]
    const std::vector<MapWaypoint>& waypoints() const { return waypoints_; }

    // The MULTIPLAYER START POSITIONS: waypoint indices 0..7 only, ascending.
    // The start count is simply how many of the keys 0..7 are present
    // (2..8, never more, on all 267 MP maps). [RE: maps.md §4.3 item 2]
    std::vector<MapWaypoint> start_waypoints() const;

    // Convenience: start_waypoints().size(). 0 for a map with no starts.
    int start_count() const;

private:
    int width_ = 0;
    int height_ = 0;
    std::string theater_;
    std::vector<MapCell> cells_;
    std::vector<int32_t> index_;   // 512x512 (x,y) -> cells_ index, -1 absent
    std::vector<uint8_t> overlay_, overlay_data_;
    std::vector<MapWaypoint> waypoints_;
};

} // namespace ra2assets
