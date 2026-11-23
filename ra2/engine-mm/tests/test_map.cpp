#include "doctest.h"
#include "ra2assets/map.hpp"
#include "ra2assets/mix.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
using namespace ra2assets;

namespace {
// Append one hand-crafted 11-byte IsoMapPack5 cell record (little-endian):
// int16 X, int16 Y, int32 Tile, uint8 SubTile, uint8 Level, uint8 (ignored).
void push_record(std::vector<uint8_t>& out, int16_t x, int16_t y, uint32_t tile,
                  uint8_t subtile, uint8_t level, uint8_t ignored = 0) {
    out.push_back((uint8_t)(x & 0xFF));
    out.push_back((uint8_t)((x >> 8) & 0xFF));
    out.push_back((uint8_t)(y & 0xFF));
    out.push_back((uint8_t)((y >> 8) & 0xFF));
    out.push_back((uint8_t)(tile & 0xFF));
    out.push_back((uint8_t)((tile >> 8) & 0xFF));
    out.push_back((uint8_t)((tile >> 16) & 0xFF));
    out.push_back((uint8_t)((tile >> 24) & 0xFF));
    out.push_back(subtile);
    out.push_back(level);
    out.push_back(ignored);
}
} // namespace

// --- SYNTHETIC: record-walking logic, driven directly with raw bytes -------
// Hand-synthesizing a real LZO1X-compressed [IsoMapPack5] blob for a
// black-box MapFile::parse() test is impractical (LZO1X has no trivial
// encoder in this codebase -- test_lzo1x.cpp's own streams are hand-traced
// token-by-token, not built from arbitrary input). Task 2b's real-map test
// already proves the LZO1X decompressor end to end. So these tests exercise
// the record-walking step directly via ra2assets::detail::parse_cell_records
// (exposed in map.hpp for exactly this purpose), feeding it raw
// ALREADY-DECOMPRESSED bytes -- precisely what decompress_mappack(...) hands
// MapFile::parse() on the real path. The INI-level parse() path
// (Theater/Size) is exercised separately below without needing terrain data.
TEST_CASE("map: record-walk parses two hand-built 11-byte cell records") {
    std::vector<uint8_t> raw;
    push_record(raw, 5, 7, 42, 1, 3);
    push_record(raw, 8, 9, 100, 2, 4);

    auto cells = detail::parse_cell_records(raw.data(), raw.size());
    REQUIRE(cells.size() == 2);
    CHECK(cells[0].x == 5);
    CHECK(cells[0].y == 7);
    CHECK(cells[0].tile == 42);
    CHECK(cells[0].subtile == 1);
    CHECK(cells[0].level == 3);
    CHECK(cells[1].x == 8);
    CHECK(cells[1].y == 9);
    CHECK(cells[1].tile == 100);
    CHECK(cells[1].subtile == 2);
    CHECK(cells[1].level == 4);
}

TEST_CASE("map: record-walk materializes the 0xFFFF no-tile sentinel as Clear; keeps tile==0; ignores a short trailer") {
    std::vector<uint8_t> raw;
    push_record(raw, 1, 1, 0, 0, 0);           // tile==0 ("Clear") IS kept -- common, legit real terrain
    push_record(raw, 2, 2, 0xFFFF, 9, 9);      // "no tile" sentinel -- materialized as Clear, NOT dropped
    push_record(raw, 3, 3, 77, 5, 6);
    raw.push_back(0); raw.push_back(0); raw.push_back(0); // <11-byte zero pad trailer, ignored

    auto cells = detail::parse_cell_records(raw.data(), raw.size());
    REQUIRE(cells.size() == 3);
    CHECK(cells[0].x == 1);
    CHECK(cells[0].tile == 0);

    // The 0xFFFF record: kept (not dropped), x/y/level preserved from the
    // record, but tile/subtile forced to Clear (0/0) -- per ModEnc, the game
    // substitutes a clear tile for this sentinel when loading, so this is
    // real, walkable ground, not a void/omitted cell.
    CHECK(cells[1].x == 2);
    CHECK(cells[1].y == 2);
    CHECK(cells[1].tile == 0);
    CHECK(cells[1].subtile == 0);
    CHECK(cells[1].level == 9);

    CHECK(cells[2].x == 3);
    CHECK(cells[2].tile == 77);
}

TEST_CASE("map: parse() reads [Map] Theater/Size even with no terrain section") {
    std::string text =
        "[Map]\n"
        "Theater=TEMPERATE\n"
        "Size=0,0,10,10\n";
    MapFile map = MapFile::parse(text);
    CHECK(map.theater() == "TEMPERATE");
    CHECK(map.width() == 10);
    CHECK(map.height() == 10);
    CHECK(map.cells().empty());
    CHECK(map.at(0, 0) == nullptr);
}

TEST_CASE("map: parse() throws without a [Map] section") {
    CHECK_THROWS_AS(MapFile::parse("[Other]\nFoo=1\n"), std::runtime_error);
}

// --- GUARDED real-map test --------------------------------------------------
// Extracts the map at RUNTIME from the install (durable -- no scratchpad
// dependency, unlike the RA2_TEST_MAP-gated tests in test_mappack.cpp /
// test_lzo1x.cpp): mapsmd03.mix, entry 0x133a0cb2 -- the same temperate
// 103x110 map Tasks 2/2b/2b already validated (see task-2-report.md /
// task-2b-report.md). Skips cleanly (does not fail) if the install/asset is
// absent.
namespace {
std::vector<uint8_t> slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}
} // namespace

TEST_CASE("map: parses the real mapsmd03.mix temperate map (entry 0x133a0cb2)") {
    const char* dir = std::getenv("RA2_INSTALL_DIR");
    std::string base = dir ? dir : "D:\\Red Alert 2";
    auto mix_bytes = slurp(base + "\\mapsmd03.mix");
    if (mix_bytes.empty()) { MESSAGE("skipped: mapsmd03.mix not found under " << base); return; }
    MixFile mix = MixFile::open(std::move(mix_bytes));

    const MixEntry* e = mix.find(0x133a0cb2u);
    if (!e) { MESSAGE("skipped: entry 0x133a0cb2 not found in mapsmd03.mix"); return; }
    std::vector<uint8_t> ini_bytes = mix.read(*e);
    std::string text(ini_bytes.begin(), ini_bytes.end());

    MapFile map = MapFile::parse(text);
    CHECK(map.theater() == "TEMPERATE");
    CHECK(map.width() == 103);
    CHECK(map.height() == 110);

    // Exact real-data count: 22550 raw records (== (2*103-1)*110, the
    // isometric cell count -- see task-2b-report.md). Per the Task 5 review
    // finding (see task-5-report.md's "Fix: 0xFFFF -> Clear" section), the
    // 829 records whose Tile==0xFFFF are NOT dropped anymore -- they are
    // materialized as Clear cells (per ModEnc: the game substitutes a clear
    // tile for this sentinel at load time, so it's real, walkable ground).
    // So every one of the 22550 raw records now produces exactly one
    // MapCell: cells().size() must equal the total record count, i.e.
    // nothing is silently dropped.
    const size_t kTotalRecords = 22550;
    REQUIRE(map.cells().size() == kTotalRecords);

    // Spot-check: raw record 0 is {X=103,Y=1,Tile=99,SubTile=2,Level=3}
    // (independently confirmed by Task 2b's own real-data dump in
    // task-2b-report.md) and is not the 0xFFFF sentinel, so it is kept
    // unchanged and must be cells()[0] here too.
    const MapCell& c0 = map.cells()[0];
    CHECK(c0.x == 103);
    CHECK(c0.y == 1);
    CHECK(c0.tile == 99);
    CHECK(c0.subtile == 2);
    CHECK(c0.level == 3);

    // at() finds that same cell by its raw (x,y); an absent coordinate
    // (well outside the observed 1..212 raw coordinate range) returns null.
    const MapCell* found = map.at(103, 1);
    REQUIRE(found != nullptr);
    CHECK(found->tile == 99);
    CHECK(map.at(9999, 9999) == nullptr);

    // Plausibility tripwire: every kept cell's raw coordinate must be well
    // within the TS/RA2 engine's hard 512x512 map-size cap (real data stays
    // in ~1..212 for this map -- see map.hpp for why this differs from
    // width()/height()).
    for (const auto& c : map.cells()) {
        CHECK(c.x >= 0);
        CHECK(c.x < 512);
        CHECK(c.y >= 0);
        CHECK(c.y < 512);
    }

    MESSAGE("real map: theater=" << map.theater() << " size=" << map.width() << "x" << map.height()
            << " cells=" << map.cells().size());
}

// --- OVERLAY: [OverlayPack]/[OverlayDataPack] synthetic round-trip ---------
// Both sections are Format5 packs whose per-chunk BODY codec is LCW/Format80
// (confirmed in task-2-report.md / test_mappack.cpp), unlike [IsoMapPack5]
// which is LZO1X. To black-box test MapFile::parse() end to end (base64 ->
// Format5 chunking -> LCW decompression -> 512x512 byte-array accessors)
// without a real map file, these helpers hand-encode a full 262,144-byte
// (512x512) overlay/data array as a LITERAL-ONLY LCW/Format80 stream (opcode
// 0x80|n copies n<=0x3F literal bytes; a bare 0x80 ends the stream -- see
// src/lcw.cpp), chunked into <=8192-byte pieces (mirroring the ~8192-byte
// chunking XCC uses on real map packs, per test_mappack.cpp), each wrapped in
// the shared Format5 chunk header ([u16 le compressedSize][u16 le
// uncompressedSize]), concatenated, then base64-encoded and wrapped as an INI
// section with 70-char-wide numbered keys -- exactly the shape MapFile::parse
// expects to find in a real map INI.
namespace {

std::vector<uint8_t> lcw_literal_encode(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> out;
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t n = std::min<size_t>(0x3F, raw.size() - pos); // literal count fits cmd's low 6 bits
        out.push_back(static_cast<uint8_t>(0x80 | n));
        for (size_t i = 0; i < n; i++) out.push_back(raw[pos + i]);
        pos += n;
    }
    out.push_back(0x80); // end-of-stream marker (count==0)
    return out;
}

std::string b64_encode(const std::vector<uint8_t>& v) {
    static const char* alpha =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";
    std::string out;
    size_t i = 0;
    for (; i + 3 <= v.size(); i += 3) {
        uint32_t n = (static_cast<uint32_t>(v[i]) << 16) | (static_cast<uint32_t>(v[i + 1]) << 8) |
                     static_cast<uint32_t>(v[i + 2]);
        out.push_back(alpha[(n >> 18) & 0x3F]);
        out.push_back(alpha[(n >> 12) & 0x3F]);
        out.push_back(alpha[(n >> 6) & 0x3F]);
        out.push_back(alpha[n & 0x3F]);
    }
    size_t rem = v.size() - i;
    if (rem == 1) {
        uint32_t n = static_cast<uint32_t>(v[i]) << 16;
        out.push_back(alpha[(n >> 18) & 0x3F]);
        out.push_back(alpha[(n >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (rem == 2) {
        uint32_t n = (static_cast<uint32_t>(v[i]) << 16) | (static_cast<uint32_t>(v[i + 1]) << 8);
        out.push_back(alpha[(n >> 18) & 0x3F]);
        out.push_back(alpha[(n >> 12) & 0x3F]);
        out.push_back(alpha[(n >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

// Wraps a byte blob as an INI section: [name] then 1=...,2=... 70-char lines.
std::string pack_section(const std::string& name, const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> packed;
    size_t pos = 0;
    const size_t kChunk = 8192; // mirrors real map packs' chunk size
    while (pos < raw.size()) {
        size_t n = std::min(kChunk, raw.size() - pos);
        std::vector<uint8_t> sub(raw.begin() + static_cast<long>(pos),
                                  raw.begin() + static_cast<long>(pos + n));
        std::vector<uint8_t> body = lcw_literal_encode(sub);
        // Format5 chunk header: [u16 le compressedSize][u16 le uncompressedSize].
        packed.push_back(static_cast<uint8_t>(body.size() & 0xFF));
        packed.push_back(static_cast<uint8_t>((body.size() >> 8) & 0xFF));
        packed.push_back(static_cast<uint8_t>(n & 0xFF));
        packed.push_back(static_cast<uint8_t>((n >> 8) & 0xFF));
        packed.insert(packed.end(), body.begin(), body.end());
        pos += n;
    }
    std::string b64 = b64_encode(packed);

    std::string out = "[" + name + "]\n";
    size_t line = 1;
    for (size_t i = 0; i < b64.size(); i += 70) {
        out += std::to_string(line++) + "=" + b64.substr(i, 70) + "\n";
    }
    return out;
}

} // namespace

TEST_CASE("map: overlay packs decode and address as x + y*512") {
    std::vector<uint8_t> ovl(262144, 0xFF), dat(262144, 0x00);
    ovl[static_cast<size_t>(7) + static_cast<size_t>(9) * 512] = 104;  // TIB01
    dat[static_cast<size_t>(7) + static_cast<size_t>(9) * 512] = 5;
    std::string ini = "[Map]\nSize=0,0,50,50\nTheater=TEMPERATE\n" +
                      pack_section("OverlayPack", ovl) + pack_section("OverlayDataPack", dat);
    auto m = ra2assets::MapFile::parse(ini);
    CHECK(m.has_overlay());
    CHECK(m.overlay_at(7, 9) == 104);
    CHECK(m.overlay_data_at(7, 9) == 5);
    CHECK(m.overlay_at(8, 9) == -1);      // 0xFF = none
    CHECK(m.overlay_at(-1, 0) == -1);     // out of range
    CHECK(m.overlay_at(512, 0) == -1);
}

TEST_CASE("map: missing overlay sections are not an error") {
    auto m = ra2assets::MapFile::parse("[Map]\nSize=0,0,10,10\nTheater=TEMPERATE\n");
    CHECK_FALSE(m.has_overlay());
    CHECK(m.overlay_at(0, 0) == -1);
    CHECK(m.overlay_data_at(0, 0) == -1);
}

TEST_CASE("map: overlay body with invalid base64 is treated as absent, not a crash") {
    // base64_decode throws std::runtime_error on any non-alphabet character
    // (src/mappack.cpp); a garbage [OverlayPack]/[OverlayDataPack] body must
    // be swallowed by parse() (section treated as absent), never propagate
    // out of MapFile::parse.
    std::string ini =
        "[Map]\nSize=0,0,10,10\nTheater=TEMPERATE\n"
        "[OverlayPack]\n1=!!!not-base64@@@\n"
        "[OverlayDataPack]\n1=???also*garbage???\n";
    ra2assets::MapFile m = ra2assets::MapFile::parse(ini); // must not throw
    CHECK_FALSE(m.has_overlay());
    CHECK(m.overlay_at(0, 0) == -1);
    CHECK(m.overlay_data_at(0, 0) == -1);
}

TEST_CASE("map: overlay body with corrupt Format5/LCW data is treated as absent, not a crash") {
    // Well-formed base64 whose decoded bytes are NOT a valid Format5 pack:
    // a chunk header declaring a 0xFFFF-byte body when only 4 bytes remain
    // (decompress_mappack throws "chunk body ... overruns input"). parse()
    // must swallow it and report the section absent.
    std::vector<uint8_t> bogus = {0xFF, 0xFF, 0x00, 0x20, 0xDE, 0xAD, 0xBE, 0xEF};
    std::string ini =
        "[Map]\nSize=0,0,10,10\nTheater=TEMPERATE\n"
        "[OverlayPack]\n1=" + b64_encode(bogus) + "\n" +
        "[OverlayDataPack]\n1=" + b64_encode(bogus) + "\n";
    ra2assets::MapFile m = ra2assets::MapFile::parse(ini); // must not throw
    CHECK_FALSE(m.has_overlay());
    CHECK(m.overlay_at(0, 0) == -1);
    CHECK(m.overlay_data_at(0, 0) == -1);
}

// --- DUMP/PINS: real reference map's ore overlay -----------------------------
// Guarded (RA2_INSTALL_DIR pattern mirrored from the existing real-map test
// above / test_theater_name.cpp / test_theater.cpp): opens mapsmd03.mix,
// entry 0x133a0cb2 (the same temperate 103x110 map Tasks 2/2b validated), and
// inspects the real [OverlayPack]/[OverlayDataPack] content. Per economy.md
// §5's [RE-GATE], the engine's tiberium bases are {27, 102, 127, 147} each
// spanning NumImages(12)+NumSlopes(8)=20 indices, i.e. overlay indices in
// [27,47) u [102,122) u [127,147) u [147,167) are candidate tiberium/gem
// cells (a superset of the authoritative [OverlayTypes] key-1 blocks
// GEM01..12=27..38, TIB01..20=104..123, TIB2_01..20=129..148,
// TIB3_01..20=149..168).
//
// DUMP RESULT (this reference map; full histogram + verdict also in
// task-2-report.md): only 3 DISTINCT overlay indices ever appear on
// tiberium-range cells -- exactly the 3 hardcoded bases {27, 102, 127} (no
// 147/TIB3 on this map) -- and EACH of those 3 indices individually carries
// a wide spread of data-byte values (idx=27: 176 cells, data 0..10; idx=102:
// 835 cells, data 0..11; idx=127: 23 cells, data 0..4). That is: the overlay
// INDEX is a constant per tiberium TYPE (not enumerated per density step),
// while overlay_data_at (0..11) is the density. This CONFIRMS the engine
// model already established by decompile in economy.md §5
// (TiberiumClass::FindIndex range-tests idx in [Image.ArrayIndex,+12), frame
// = ArrayIndex + density) -- it is NOT the alternative "density lives in the
// index" model (which would show ~12 consecutive indices per type with a
// constant/absent data byte). Also confirms economy.md's alignment note:
// GEM's base (27) matches [OverlayTypes] GEM01 key-1 (27) exactly, while
// TIB's observed base (102) sits 2 below the TIB01 block start (104) and
// TIB2's (127) sits 2 below TIB2_01's start (129) -- real data, not just the
// jump-table read, so Task 4 should key classification off the recovered
// bases {27,102,127,147} (+12 density / +8 slope), not the [OverlayTypes]
// key-1 block starts.
TEST_CASE("map: real reference map has ore overlay (pins)") {
    const char* dir_env = std::getenv("RA2_INSTALL_DIR");
    std::string base = dir_env ? dir_env : "D:\\Red Alert 2";
    auto mix_bytes = slurp(base + "\\mapsmd03.mix");
    if (mix_bytes.empty()) { MESSAGE("skipped: mapsmd03.mix not found under " << base); return; }
    MixFile maps = MixFile::open(std::move(mix_bytes));

    const MixEntry* me = maps.find(0x133a0cb2u);
    if (!me) { MESSAGE("skipped: entry 0x133a0cb2 not found in mapsmd03.mix"); return; }
    std::vector<uint8_t> ini_bytes = maps.read(*me);
    auto m = ra2assets::MapFile::parse(std::string(ini_bytes.begin(), ini_bytes.end()));
    REQUIRE(m.has_overlay());

    // Candidate tiberium/gem index ranges (see comment above).
    auto is_tiberium_range = [](int idx) {
        static const int bases[] = {27, 102, 127, 147};
        for (int b : bases) if (idx >= b && idx < b + 20) return true;
        return false;
    };

    std::map<int, int> histogram; // overlay index -> matching-cell count
    std::map<int, int> data_min, data_max;
    std::vector<std::pair<int, int>> first_hits; // (x,y) of first 10 matches, in cells() order
    int total_matches = 0;
    for (const auto& c : m.cells()) {
        int idx = m.overlay_at(c.x, c.y);
        if (idx < 0 || !is_tiberium_range(idx)) continue;
        int dat = m.overlay_data_at(c.x, c.y);
        total_matches++;
        histogram[idx]++;
        if (!data_min.count(idx) || dat < data_min[idx]) data_min[idx] = dat;
        if (!data_max.count(idx) || dat > data_max[idx]) data_max[idx] = dat;
        if (first_hits.size() < 10) first_hits.push_back({c.x, c.y});
    }

    MESSAGE("ore cells matched: " << total_matches << " (distinct indices: " << histogram.size() << ")");
    for (const auto& kv : histogram) {
        MESSAGE("  index=" << kv.first << " count=" << kv.second
                << " data_min=" << data_min[kv.first] << " data_max=" << data_max[kv.first]);
    }
    for (const auto& xy : first_hits) {
        MESSAGE("  first-hit cell (" << xy.first << "," << xy.second << ") idx="
                << m.overlay_at(xy.first, xy.second) << " data=" << m.overlay_data_at(xy.first, xy.second));
    }

    // [RE-GATE] literal pins, one per observed tiberium type on this map
    // (harvested from the dump above -- see task-2-report.md):
    CHECK(m.overlay_at(106, 14) == 27);    // GEM (Cruentus) base
    CHECK(m.overlay_data_at(106, 14) == 3);
    CHECK(m.overlay_at(104, 14) == 102);   // TIB (Riparius) base
    CHECK(m.overlay_data_at(104, 14) == 1);
    CHECK(m.overlay_at(140, 63) == 127);   // TIB2 base
    CHECK(m.overlay_data_at(140, 63) == 0);
}

// --- [Waypoints] decode [RE: maps.md §4.3] --------------------------------
TEST_CASE("map: [Waypoints] decode x=v%1000 y=v/1000, starts are keys 0..7") {
    const std::string text =
        "[Map]\nTheater=TEMPERATE\nSize=0,0,105,95\n"
        "[Waypoints]\n"
        "0=98057\n"        // -> (57, 98)   (maps.md §4.3's own xmp01t4 example)
        "1=148099\n"       // -> (99, 148)
        "2=96152\n"        // -> (152, 96)
        "3=55110\n"        // -> (110, 55)
        "26=93052\n"       // > 7 -> NOT a start
        "699=100100\n"     // map center -> NOT a start
        "bogus=1\n"        // non-numeric key   -> skipped, no throw
        "9=notanumber\n";  // non-numeric value -> skipped, no throw

    MapFile m = MapFile::parse(text);
    REQUIRE(m.waypoints().size() == 6);           // 4 starts + 26 + 699
    CHECK(m.waypoints()[0].index == 0);
    CHECK(m.waypoints()[0].x == 57);
    CHECK(m.waypoints()[0].y == 98);
    CHECK(m.waypoints()[1].x == 99);
    CHECK(m.waypoints()[1].y == 148);
    CHECK(m.waypoints()[4].index == 26);          // ascending by index
    CHECK(m.waypoints()[5].index == 699);

    const std::vector<MapWaypoint> starts = m.start_waypoints();
    REQUIRE(starts.size() == 4);
    CHECK(m.start_count() == 4);
    CHECK(starts[0].index == 0);
    CHECK(starts[3].index == 3);
    CHECK(starts[3].x == 110);
    CHECK(starts[3].y == 55);
}

TEST_CASE("map: no [Waypoints] section still parses (campaign/regression guard)") {
    MapFile m = MapFile::parse("[Map]\nTheater=TEMPERATE\nSize=0,0,10,10\n");
    CHECK(m.waypoints().empty());
    CHECK(m.start_count() == 0);
    CHECK(m.theater() == "TEMPERATE");            // existing behavior UNCHANGED
    CHECK(m.width() == 10);
    CHECK(m.height() == 10);
}

// --- REAL ASSETS (guarded): the multiplayer maps --------------------------
TEST_CASE("map: real xmp01t4.map -> TEMPERATE 105x95, 4 starts [RE: maps.md §3/§4]") {
    const char* dir = std::getenv("RA2_INSTALL_DIR");
    const std::string base = dir ? dir : "D:\\Red Alert 2";
    auto bytes = slurp(base + "\\multimd.mix");
    if (bytes.empty()) { MESSAGE("skipped: multimd.mix not found under " << base); return; }
    MixFile mix = MixFile::open(std::move(bytes));
    const MixEntry* e = mix.find(std::string("xmp01t4.map"));
    if (!e) { MESSAGE("skipped: xmp01t4.map not in multimd.mix"); return; }
    auto raw = mix.read(*e);
    MapFile m = MapFile::parse(std::string(raw.begin(), raw.end()));

    CHECK(m.theater() == "TEMPERATE");
    CHECK(m.width() == 105);
    CHECK(m.height() == 95);
    CHECK(m.cells().size() == 19855);             // (2w-1)*h  [maps.md §4.2]
    REQUIRE(m.start_count() == 4);
    CHECK(m.start_waypoints()[0].x == 57);        // 0=98057
    CHECK(m.start_waypoints()[0].y == 98);
}

TEST_CASE("map: real xmp31s2.map -> SNOW 70x83, 2 starts (the --demo-maps map)") {
    const char* dir = std::getenv("RA2_INSTALL_DIR");
    const std::string base = dir ? dir : "D:\\Red Alert 2";
    auto bytes = slurp(base + "\\multimd.mix");
    if (bytes.empty()) { MESSAGE("skipped: multimd.mix not found under " << base); return; }
    MixFile mix = MixFile::open(std::move(bytes));
    const MixEntry* e = mix.find(std::string("xmp31s2.map"));
    if (!e) { MESSAGE("skipped: xmp31s2.map not in multimd.mix"); return; }
    auto raw = mix.read(*e);
    MapFile m = MapFile::parse(std::string(raw.begin(), raw.end()));
    CHECK(m.theater() == "SNOW");
    CHECK(m.width() == 70);
    CHECK(m.height() == 83);
    CHECK(m.start_count() == 2);
    // Every start must land on a real IsoMapPack5 cell (maps.md §4.3).
    for (const MapWaypoint& w : m.start_waypoints()) CHECK(m.at(w.x, w.y) != nullptr);
}

// W2-GA-04: the whole [Map] Size diamond is ground; omitted pack cells are
// materialised as tile 0 / level 0 and present cells are never touched.
TEST_CASE("map: fill_missing_diamond completes the Size diamond with clear ground") {
    const int w = 4, h = 3;
    std::vector<MapCell> all;
    detail::fill_missing_diamond(all, w, h);
    CHECK(all.size() == (size_t)((2 * w - 1) * h));   // the pack enumeration size
    // Drop two cells, give the rest a marker level, refill.
    std::vector<MapCell> some(all.begin() + 2, all.end());
    for (MapCell& c : some) { c.tile = 7; c.level = 2; }
    detail::fill_missing_diamond(some, w, h);
    REQUIRE(some.size() == all.size());
    int filled = 0;
    for (const MapCell& c : some)
        if (c.tile == 0) { ++filled; CHECK(c.level == 0); }
        else CHECK(c.level == 2);
    CHECK(filled == 2);
    // A complete set is left unchanged.
    const size_t before = some.size();
    detail::fill_missing_diamond(some, w, h);
    CHECK(some.size() == before);
}
