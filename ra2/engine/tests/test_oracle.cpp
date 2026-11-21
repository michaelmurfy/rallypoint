#include "doctest.h"
#include "ra2assets/mix.hpp"
#include "ra2assets/shp.hpp"
#include "asset_fixture.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
namespace fs = std::filesystem;
using namespace ra2assets;

static std::vector<uint8_t> slurp_file(const fs::path& p){
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),{});
}

// Same nested-path resolver as tools/ra2extract.cpp ("inner.mix/name.shp").
static std::vector<uint8_t> read_path(const MixFile& m, const std::string& path){
    size_t s = path.find('/');
    if (s == std::string::npos){
        const MixEntry* e = m.find(path);
        REQUIRE(e != nullptr);
        return m.read(*e);
    }
    MixFile inner = m.open_nested(path.substr(0,s));
    return read_path(inner, path.substr(s+1));
}

TEST_CASE("indexed output matches XCC oracle byte-for-byte") {
    const char* od = std::getenv("RA2_ORACLE_DIR");
    fs::path base = od ? od : "D:\\ra2-android\\oracle";
    if (!fs::exists(base)) { MESSAGE("oracle dir absent; skipped"); return; }
    const char* id = std::getenv("RA2_INSTALL_DIR");
    fs::path install = id ? id : "D:\\Red Alert 2";
    if (!fs::exists(install / "ra2.mix")) {
        MESSAGE("install absent; skipped (oracle files present but no game mixes)");
        return;
    }
    // manifest.txt: one asset per line, whitespace-separated fields:
    //   <mix-name-relative-to-install> <shp-path-within-mix> <frame-dir-under-oracle>
    // e.g.:  ra2.mix  conquer.mix/e1.shp  ra2/e1
    // (Mix names are install-relative so fields never contain spaces.)
    fs::path manifest = base / "manifest.txt";
    if (!fs::exists(manifest)) { MESSAGE("no manifest; skipped"); return; }
    std::ifstream mf(manifest);
    std::string mixname, shppath, framedir;
    int frames_checked = 0;
    while (mf >> mixname >> shppath >> framedir) {
        // Borrow the shared, load-once mix (asset_fixture): the manifest names
        // ra2.mix on every line, so this reads/opens it once instead of once
        // per line. REQUIRE_FALSE kept (now on the shared bytes) so the count
        // is unchanged.
        REQUIRE_FALSE(ra2fixture::mix_bytes_by_name(mixname).empty());
        const MixFile& m = ra2fixture::mix_by_name(mixname);
        ShpFile s = ShpFile::load(read_path(m, shppath));
        for (int i = 0; i < s.count(); i++) {
            char fn[32]; std::snprintf(fn, sizeof fn, "frame%02d.raw", i);
            fs::path fp = base / framedir / fn;
            if (!fs::exists(fp)) continue;      // only exported frames are compared
            auto want = slurp_file(fp);
            auto got  = s.decode_indexed(i);
            REQUIRE(got.size() == want.size());
            CHECK(std::equal(got.begin(), got.end(), want.begin()));
            frames_checked++;
        }
    }
    CHECK(frames_checked > 0);   // a manifest that exercises zero frames is a failure
}
