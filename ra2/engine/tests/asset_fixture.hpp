#pragma once
// Shared, load-once accessors for the big real-install assets used by the
// install-gated ("real-asset") test cases.
//
// WHY: every real-asset case used to independently slurp the entire ~196 MB
// ra2md.mix (via a byte-at-a-time istreambuf_iterator read), re-open it with
// MixFile::open, walk the nested-candidate mixes, and re-parse rulesmd.ini
// (a multi-MB INI) into Ini/Rules. Dozens of cases across several files did
// this, so the with-install suite spent ~100% of its wall-clock re-reading and
// re-parsing the SAME bytes -- not on the assertions or any engine code.
//
// This header centralizes that work behind lazily-initialized, function-local
// `static` accessors so each shared asset is read/opened/parsed exactly ONCE
// per test-binary run (the doctest run is single-threaded, so plain
// function-local statics are safe). The accessors are `inline`, so every
// translation unit that includes this header shares ONE instance of each
// static (guaranteed by [basic.def] for statics local to an inline function).
//
// This is a pure test-infrastructure change: it does NOT alter what any test
// verifies. Cases still gate on assets_available() and skip cleanly (same
// MESSAGE + early return) when the install is absent, and cases that exercise
// the loaders themselves keep running the real code path -- they just borrow
// the shared bytes/objects instead of re-reading from disk.
//
// Only the public ra2assets API is used here (no engine/private dependency).

#include "ra2assets/mix.hpp"
#include "ra2assets/ini.hpp"
#include "ra2assets/rules.hpp"
#include "ra2assets/art.hpp"
#include "ra2assets/map.hpp"

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

namespace ra2fixture {

// Slurp an entire file into a byte vector; empty vector if it can't be opened
// (or is empty). Bulk one-shot read (seek-to-end + sized read) rather than the
// per-byte std::istreambuf_iterator the individual cases used -- the BYTES are
// identical (same file, opened binary), so test outcomes are unchanged; this
// is purely a speed detail (a byte-at-a-time read of the ~268 MB ra2.mix /
// ~196 MB ra2md.mix is what made the one-time load cost tens of seconds).
inline std::vector<uint8_t> slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const std::streamoff n = f.tellg();
    if (n <= 0) return {};   // missing/empty -> same "absent" signal as before
    std::vector<uint8_t> buf(static_cast<size_t>(n));
    f.seekg(0);
    if (!f.read(reinterpret_cast<char*>(buf.data()), n)) return {};
    return buf;
}

// The install base directory, resolved once. Matches every real-asset case's
// own resolution: RA2_INSTALL_DIR if set, else the stock "D:\Red Alert 2".
inline const std::string& install_base() {
    static const std::string base = [] {
        const char* dir = std::getenv("RA2_INSTALL_DIR");
        return std::string(dir ? dir : "D:\\Red Alert 2");
    }();
    return base;
}

// Raw bytes of the top-level ra2md.mix, slurped ONCE. Empty if not present
// (i.e. no install) -- callers gate on assets_available() before use.
inline const std::vector<uint8_t>& ra2md_bytes() {
    static const std::vector<uint8_t> bytes = slurp(install_base() + "\\ra2md.mix");
    return bytes;
}

// True iff ra2md.mix was found under the install base. Real-asset cases gate
// on this and skip cleanly (MESSAGE + return) when false -- preserving the
// original per-case skip behavior exactly.
inline bool assets_available() { return !ra2md_bytes().empty(); }

// The top-level ra2md.mix, opened ONCE via MixFile::open. MixFile::open takes
// its buffer by value, so we hand it a copy of the cached bytes (a single
// one-time copy at init; still no second disk read). Only call when
// assets_available() is true.
inline const ra2assets::MixFile& top_mix() {
    static ra2assets::MixFile mix = ra2assets::MixFile::open(ra2md_bytes());
    return mix;
}

// --- Generic top-level mix cache, keyed by filename ------------------------
//
// Several loader/oracle cases (test_mix, test_vpl, test_vxl, test_voxel_raster,
// test_facing_cache, test_theater_name, test_theater) each independently
// slurp a large top-level archive (mainly the ~268 MB ra2.mix, some via a
// manifest that names it once per line) and open it. These caches read/open
// each distinct top-level mix ONCE across the whole run. std::unordered_map
// keeps references to its elements valid across later insertions, so the
// returned const refs stay valid.

// Raw bytes of a top-level mix under the install base, slurped ONCE per name.
// Empty if not present.
inline const std::vector<uint8_t>& mix_bytes_by_name(const std::string& filename) {
    static std::unordered_map<std::string, std::vector<uint8_t>> cache;
    auto it = cache.find(filename);
    if (it != cache.end()) return it->second;
    std::string dir = install_base();
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir += '\\';
    auto res = cache.emplace(filename, slurp(dir + filename));
    return res.first->second;
}

// A top-level mix opened ONCE per name via MixFile::open (over a copy of the
// cached bytes). Only call when mix_bytes_by_name(filename) is non-empty
// (MixFile::open throws on empty/invalid input).
inline const ra2assets::MixFile& mix_by_name(const std::string& filename) {
    static std::unordered_map<std::string, ra2assets::MixFile> cache;
    auto it = cache.find(filename);
    if (it != cache.end()) return it->second;
    auto res = cache.emplace(filename,
                             ra2assets::MixFile::open(mix_bytes_by_name(filename)));
    return res.first->second;
}

// --- ra2.mix (the non-MD main archive) convenience wrappers ----------------
inline const std::vector<uint8_t>& ra2_bytes() { return mix_bytes_by_name("ra2.mix"); }
inline bool ra2_available() { return !ra2_bytes().empty(); }
inline const ra2assets::MixFile& ra2_mix() { return mix_by_name("ra2.mix"); }

// Resolve a file's bytes from the (in-memory) top ra2md.mix using the SAME
// nested-candidate walk the cases used: try the top level first (some mod
// layouts flatten it), then descend the usual homes (localmd.mix, the
// expansion mixes, cachemd.mix, conqmd.mix). Empty vector if not found in any.
// Not cached (callers that repeat a file get a cached accessor below).
inline std::vector<uint8_t> resolve_nested_bytes(const std::string& name) {
    const ra2assets::MixFile& top = top_mix();
    if (const ra2assets::MixEntry* e = top.find(ra2assets::mix_id(name)))
        return top.read(*e);
    static const char* candidates[] = {
        "localmd.mix",
        "expandmd01.mix", "expandmd02.mix", "expandmd03.mix",
        "expandmd04.mix", "expandmd05.mix", "expandmd99.mix",
        "cachemd.mix", "conqmd.mix",
    };
    for (const char* c : candidates) {
        try {
            ra2assets::MixFile nested = top.open_nested(c);
            if (const ra2assets::MixEntry* e2 = nested.find(ra2assets::mix_id(name)))
                return nested.read(*e2);
        } catch (const std::exception&) {
            // Nested mix not present in this install/layout; try the next.
        }
    }
    return {};
}

// --- rulesmd.ini (the hot one: parsed by many real-asset cases) ------------

// rulesmd.ini bytes, resolved ONCE. Empty if not found.
inline const std::vector<uint8_t>& rulesmd_bytes() {
    static const std::vector<uint8_t> bytes = resolve_nested_bytes("rulesmd.ini");
    return bytes;
}

// rulesmd.ini parsed to Ini ONCE. Only call when rulesmd_bytes() is non-empty.
inline const ra2assets::Ini& rulesmd_ini() {
    static const ra2assets::Ini ini =
        ra2assets::Ini::parse(std::string(rulesmd_bytes().begin(), rulesmd_bytes().end()));
    return ini;
}

// Rules built ONCE over the shared rulesmd Ini. Rules wraps the Ini by
// pointer, so it borrows the function-local-static Ini above (both live for
// the whole program). Only call when rulesmd_bytes() is non-empty.
inline const ra2assets::Rules& rulesmd() {
    static const ra2assets::Rules rules = ra2assets::Rules::load(rulesmd_ini());
    return rules;
}

// --- artmd.ini (parsed by the real-artmd cases) ----------------------------

inline const std::vector<uint8_t>& artmd_bytes() {
    static const std::vector<uint8_t> bytes = resolve_nested_bytes("artmd.ini");
    return bytes;
}

inline const ra2assets::Ini& artmd_ini() {
    static const ra2assets::Ini ini =
        ra2assets::Ini::parse(std::string(artmd_bytes().begin(), artmd_bytes().end()));
    return ini;
}

inline const ra2assets::Art& artmd() {
    static const ra2assets::Art art = ra2assets::Art::load(artmd_ini());
    return art;
}

// --- mapsmd03.mix real map (test_world integration cases) ------------------

// mapsmd03.mix is a separate, smaller archive (not ra2md.mix). The
// multi-unit World integration cases each re-slurped it and re-parsed the
// same map; cache the parsed MapFile ONCE. The entry id 0x133a0cb2, the path
// join, and the ok/false conditions match test_world.cpp's original
// load_real_map_w exactly (ok iff the archive is present AND the entry found).
struct Mapsmd03 { bool ok = false; ra2assets::MapFile map; };
inline const Mapsmd03& mapsmd03() {
    static const Mapsmd03 m = [] {
        Mapsmd03 r;
        std::string dir = install_base();
        if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir += '\\';
        auto b = slurp(dir + "mapsmd03.mix");
        if (b.empty()) return r;
        ra2assets::MixFile mix = ra2assets::MixFile::open(std::move(b));
        const ra2assets::MixEntry* e = mix.find(0x133a0cb2u);
        if (!e) return r;
        auto raw = mix.read(*e);
        r.map = ra2assets::MapFile::parse(std::string(raw.begin(), raw.end()));
        r.ok = true;
        return r;
    }();
    return m;
}

inline bool mapsmd03_available() { return mapsmd03().ok; }
inline const ra2assets::MapFile& mapsmd03_map() { return mapsmd03().map; }

}  // namespace ra2fixture
