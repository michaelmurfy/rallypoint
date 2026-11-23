// W3-FIX-A: the shared per-theater MIX search list (src/asset_fs.cpp).
#include "doctest.h"
#include "ra2assets/asset_fs.hpp"
#include "asset_fixture.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

using ra2assets::Art;
using ra2assets::AssetFS;
using ra2assets::AssetMixSpec;
using ra2assets::asset_search_order;
using ra2assets::art_base_names;
using ra2assets::theater_chain;

namespace {
int index_of(const std::vector<AssetMixSpec>& v, const std::string& label) {
    for (std::size_t i = 0; i < v.size(); ++i)
        if (v[i].label() == label) return static_cast<int>(i);
    return -1;
}
bool install_present() {
    const std::string p = ra2fixture::install_base() + "\\ra2.mix";
    if (std::FILE* f = std::fopen(p.c_str(), "rb")) { std::fclose(f); return true; }
    return false;
}
std::string dir() { return ra2fixture::install_base() + "\\"; }
// '/' join so the Yuri cameo census runs on Linux (fixture still uses '\\').
std::string dir_posix() {
    std::string b = ra2fixture::install_base();
    if (!b.empty() && b.back() != '/' && b.back() != '\\') b.push_back('/');
    return b;
}
bool install_present_posix() {
    if (std::FILE* f = std::fopen((dir_posix() + "ra2.mix").c_str(), "rb")) {
        std::fclose(f);
        return true;
    }
    return false;
}
std::vector<uint8_t> read_ra2md_file(const std::string& d, const std::string& name) {
    ra2assets::MixFile top = ra2assets::MixFile::open_path(d + "ra2md.mix");
    if (const ra2assets::MixEntry* e = top.find(ra2assets::mix_id(name))) return top.read(*e);
    for (const char* c : {"localmd.mix", "expandmd01.mix", "expandmd02.mix", "expandmd03.mix",
                          "expandmd04.mix", "expandmd05.mix", "expandmd99.mix", "cachemd.mix",
                          "conqmd.mix"}) {
        try {
            ra2assets::MixFile n = top.open_nested(c);
            if (const ra2assets::MixEntry* e2 = n.find(ra2assets::mix_id(name))) return n.read(*e2);
        } catch (...) {
        }
    }
    return {};
}
}  // namespace

TEST_CASE("asset_search_order: RA2 game.exe load order, YR tail last") {
    const auto o = asset_search_order(theater_chain("URBAN"), {"expand01.mix", "ecache01.mix"});
    // Bootstrap: EXPAND, RA2, CACHE, LOCAL; then ECACHE; then CONQUER.
    CHECK(index_of(o, "expand01.mix") == 0);
    CHECK(index_of(o, "ra2.mix") == 1);
    CHECK(index_of(o, "ra2.mix/cache.mix") < index_of(o, "ra2.mix/local.mix"));
    CHECK(index_of(o, "ra2.mix/local.mix") < index_of(o, "ecache01.mix"));
    CHECK(index_of(o, "ecache01.mix") < index_of(o, "ra2.mix/conquer.mix"));
    // Init_Theater: GENERIC, <THEATER>, <EXT>, ISO<..>, ISOGEN.
    const int g = index_of(o, "ra2.mix/generic.mix"), t = index_of(o, "ra2.mix/urban.mix"),
              e = index_of(o, "ra2.mix/urb.mix"), i = index_of(o, "ra2.mix/isourb.mix"),
              ig = index_of(o, "ra2.mix/isogen.mix");
    CHECK(index_of(o, "ra2.mix/conquer.mix") < g);
    // RA2 cameos (XXICON, FIXICON, GIICON, …) live in language.mix/cameo.mix.
    CHECK(index_of(o, "language.mix/cameo.mix") > index_of(o, "ra2.mix/conquer.mix"));
    CHECK(index_of(o, "language.mix/cameo.mix") < g);
    CHECK(g < t);
    CHECK(t < e);
    CHECK(e < i);
    CHECK(i < ig);
    // Every RA2 archive precedes every YR one.
    const int md = index_of(o, "ra2md.mix");
    REQUIRE(md > 0);
    CHECK(ig < md);
    CHECK(index_of(o, "ra2md.mix/conqmd.mix") > md);
    CHECK(index_of(o, "ra2md.mix/isourbmd.mix") > md);
    CHECK(index_of(o, "ra2md.mix/isogenmd.mix") > md);
    // Yuri cameos: cameomd.mix is nested in langmd.mix (not ra2md.mix).
    CHECK(index_of(o, "langmd.mix/cameomd.mix") > md);
    CHECK(index_of(o, "ra2md.mix/cameomd.mix") < 0);
}

TEST_CASE("asset_search_order: SNOW opens no GENERIC before the theater archives") {
    const auto o = asset_search_order(theater_chain("SNOW"), {});
    const int g = index_of(o, "ra2.mix/generic.mix");
    CHECK(g > index_of(o, "ra2.mix/isogen.mix"));   // [RE-FALLBACK] tail copy only
    CHECK(index_of(o, "ra2.mix/snow.mix") < index_of(o, "ra2.mix/sno.mix"));
    CHECK(index_of(o, "ra2.mix/sno.mix") < index_of(o, "ra2.mix/isosnow.mix"));
    CHECK(index_of(o, "ra2md.mix/isosnomd.mix") > 0);
}

TEST_CASE("art_base_names: NewTheater munge matches theater_candidates") {
    auto t = art_base_names("CANEWY04", 'U');
    REQUIRE(t.size() == 2);
    CHECK(t[0] == "cunewy04");
    CHECK(t[1] == "cgnewy04");
    auto s = art_base_names("GACNST", 'A');
    REQUIRE(s.size() == 2);
    CHECK(s[0] == "gacnst");
    CHECK(s[1] == "ggcnst");
    auto l = art_base_names("AMMOCRAT", 'A');
    REQUIRE(l.size() == 1);
    CHECK(l[0] == "ammocrat");
}

TEST_CASE("AssetFS: real install -- landmarks, snow art, patch art, YR civilians") {
    if (!install_present()) { MESSAGE("install absent; skipping"); return; }
    AssetFS urb(dir(), theater_chain("URBAN"));
    // Statue of Liberty / White House: cg* names live only in isogen.mix.
    CHECK(urb.where("cgnewy04.shp") == "ra2.mix/isogen.mix");
    CHECK(urb.where("cgwash01.shp") == "ra2.mix/isogen.mix");
    // Palette=lib -> lib<ext>.pal in cache.mix.
    CHECK(urb.where("liburb.pal") == "ra2.mix/cache.mix");
    // hillbtwn civilian art: YR-only, reached through the tail.
    CHECK(urb.where("cula07.shp").rfind("ra2md.mix", 0) == 0);
    CHECK(urb.find("cula07.shp", nullptr, nullptr, /*ra2_only=*/true) == nullptr);
    AssetFS sno(dir(), theater_chain("SNOW"));
    // Ammo crates (all10s): only in <ext>.mix.
    CHECK(sno.where("ammo01.shp") == "ra2.mix/sno.mix");
    // Snow cottage art (not the temperate twin).
    CHECK(sno.where("caeur1.shp") != "");
    // The 1.006 patch caches are on the list, ahead of conquer.mix.
    bool has_ecache = false;
    for (const auto& m : sno.mixes()) has_ecache |= m.first == "ecache01.mix";
    CHECK(has_ecache);
}

// Yuri sidebar icons (YCONICON, YPWRICON, ...) live only in langmd.mix/cameomd.mix.
// Lists any still-missing Cameo= for the stock Yuri structure/unit set so a
// blank sidebar is caught here instead of in-game.
TEST_CASE("AssetFS: real install -- Yuri cameos resolve from langmd/cameomd") {
    if (!install_present_posix()) { MESSAGE("install absent; skipping"); return; }
    const std::string d = dir_posix();
    const std::vector<uint8_t> art_bytes = read_ra2md_file(d, "artmd.ini");
    const std::vector<uint8_t> rules_bytes = read_ra2md_file(d, "rulesmd.ini");
    if (art_bytes.empty() || rules_bytes.empty()) {
        MESSAGE("skipped: artmd.ini/rulesmd.ini not found");
        return;
    }
    const ra2assets::Ini art_ini =
        ra2assets::Ini::parse(std::string(art_bytes.begin(), art_bytes.end()));
    const Art art = Art::load(art_ini);
    const ra2assets::Ini rules =
        ra2assets::Ini::parse(std::string(rules_bytes.begin(), rules_bytes.end()));

    AssetFS fs(d, theater_chain("TEMPERATE"));
    CHECK(fs.where("yconicon.shp") == "langmd.mix/cameomd.mix");
    CHECK(fs.where("ypwricon.shp") == "langmd.mix/cameomd.mix");
    CHECK(fs.where("xxicon.shp") == "language.mix/cameo.mix");
    CHECK(fs.where("fixicon.shp") == "language.mix/cameo.mix");
    CHECK(fs.find("yconicon.shp", nullptr, nullptr, /*ra2_only=*/true) == nullptr);

    // Same name resolve as the sidebar loaders (Cameo / AltCameo / Image+ICON /
    // XXICON). Blank artmd Cameo= types (YAROCK, YACOMD, YAPPPT) get XXICON.
    auto resolve = [&](const std::string& id) -> std::string {
        std::string image = rules.get(id, "Image", id);
        if (image.empty()) image = id;
        std::string cameo = art.cameo(image);
        if (cameo.empty()) cameo = art.cameo(id);
        if (cameo.empty()) cameo = art.alt_cameo(image);
        if (cameo.empty()) cameo = art.alt_cameo(id);
        auto probe = [&](const std::string& base) -> std::string {
            if (base.empty()) return {};
            std::string cn = base + "ICON";
            std::string file = cn;
            for (char& c : file) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            file += ".shp";
            return fs.has(file) ? cn : std::string();
        };
        if (cameo.empty()) cameo = probe(image);
        if (cameo.empty() && id != image) cameo = probe(id);
        if (cameo.empty()) cameo = "XXICON";
        return cameo;
    };

    static const char* kYuriTypes[] = {
        "YACNST", "YAPOWR", "YABRCK", "YAWEAP", "YAREFN", "YAYARD", "YADEPT",
        "YATECH", "YAGGUN", "YAPSYT", "YAGRND", "YAGNTC", "YAPPET", "NAPSIS",
        "NACLON", "NATBNK", "INIT", "SMIN", "PCV", "YTNK", "TELE", "DISK",
        "MIND", "BRUTE", "VIRUS", "YURI", "LTNK",
        // Blank Cameo= in artmd — retail draws XXICON; Image+ICON SHPs absent.
        "YAROCK", "YACOMD", "YAPPPT",
    };
    std::vector<std::string> missing;
    for (const char* id : kYuriTypes) {
        std::string cameo = resolve(id);
        for (char& c : cameo) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        cameo += ".shp";
        if (!fs.has(cameo)) missing.push_back(std::string(id) + " -> " + cameo);
    }
    for (const std::string& m : missing) MESSAGE("missing cameo: ", m);
    CHECK(missing.empty());
    CHECK(resolve("YAROCK") == "XXICON");
    CHECK(resolve("YACOMD") == "XXICON");
    CHECK(resolve("YAPPPT") == "XXICON");
    CHECK(resolve("YADEPT") == "FIXICON");   // rules Image=GADEPT
    CHECK(resolve("YAREFN") == "SMINICON");
    CHECK(art.alt_cameo("YAREFN") == "SMINUICO");
}
