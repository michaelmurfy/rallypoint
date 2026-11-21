#include "doctest.h"
#include "ra2assets/theater_name.hpp"
#include "ra2assets/mix.hpp"
#include "asset_fixture.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using ra2assets::theater_candidates;
using ra2assets::theater_chain;
using ra2assets::TheaterChain;
using ra2assets::MixFile;
using ra2assets::MixEntry;
namespace fs = std::filesystem;

TEST_CASE("theater_candidates: temperate GACNST order") {
    auto c = theater_candidates("GACNST", 'T', "isotemp.mix", "temperat.mix", ".shp");
    REQUIRE(c.size() == 6);            // [gtcnst x3 mixes, ggcnst x3 mixes]
    CHECK(c[0].mix == "isotemp.mix");   CHECK(c[0].file == "gtcnst.shp");
    CHECK(c[1].mix == "temperat.mix");  CHECK(c[1].file == "gtcnst.shp");
    CHECK(c[2].mix == "generic.mix");   CHECK(c[2].file == "gtcnst.shp");
    CHECK(c[3].mix == "isotemp.mix");   CHECK(c[3].file == "ggcnst.shp");
    CHECK(c[5].mix == "generic.mix");   CHECK(c[5].file == "ggcnst.shp");
}

TEST_CASE("theater_candidates: snow letter A themes to the literal spelling") {
    auto c = theater_candidates("GACNST", 'A', "isosnow.mix", "snow.mix", ".shp");
    REQUIRE(c.size() == 6);
    CHECK(c[0].file == "gacnst.shp");   // letter A == literal spelling
    CHECK(c[1].mix == "snow.mix");      // where the real file lives
    CHECK(c[3].file == "ggcnst.shp");
}

TEST_CASE("theater_candidates: non-NewTheater-pattern name is literal only") {
    // First-two-chars pattern {g,n,c,y}x{a,t} not matched -> no munge
    // [RE-GATE: building-render.md §1 FUN_005F8EA0 pattern check].
    auto c = theater_candidates("XXICON", 'T', "isotemp.mix", "temperat.mix", ".shp");
    REQUIRE(c.size() == 3);
    CHECK(c[0].file == "xxicon.shp");
    CHECK(c[2].mix == "generic.mix");
}

TEST_CASE("theater_candidates: real-install resolution pins") {
    // Guarded (install_dir pattern from test_vpl.cpp). Open ra2.mix; walk the
    // temperate candidates for GACNST(.shp): the FIRST candidate that
    // resolves must be generic.mix/ggcnst.shp (63,760 bytes), proving both
    // the fallback order and the empirical layout. Then GACNSTMK: first hit
    // must be isotemp.mix/gtcnstmk.shp (428,080 bytes).
    const char* id = std::getenv("RA2_INSTALL_DIR");
    fs::path install = id ? id : "D:\\Red Alert 2";
    if (!fs::exists(install / "ra2.mix")) {
        MESSAGE("RA2 install absent; skipping");
        return;
    }

    // Borrow the shared, load-once ra2.mix (asset_fixture) instead of
    // re-slurping the ~268 MB archive; open_nested still exercises the real
    // path. REQUIRE_FALSE kept (now on the shared bytes) so the count is unchanged.
    REQUIRE_FALSE(ra2fixture::ra2_bytes().empty());
    const MixFile& outer = ra2fixture::ra2_mix();

    // Open the nested mixes that the candidate walk order names, keyed by
    // lowercase filename so lookups below can match theater_candidates()'s
    // lowercase mix names directly.
    std::vector<std::pair<std::string, MixFile>> open_mixes;
    for (const char* name : {"generic.mix", "isotemp.mix", "temperat.mix"}) {
        try {
            open_mixes.emplace_back(name, outer.open_nested(name));
        } catch (const std::exception&) {
            // Nested mix not present in this install/layout; skip it.
        }
    }
    REQUIRE_FALSE(open_mixes.empty());

    auto resolve_first = [&](const std::string& image, const std::string& ext)
        -> std::pair<std::string, size_t> {
        auto candidates = theater_candidates(image, 'T', "isotemp.mix", "temperat.mix", ext);
        for (const auto& cand : candidates) {
            for (auto& [mix_name, mix_file] : open_mixes) {
                if (mix_name != cand.mix) continue;
                if (const MixEntry* e = mix_file.find(cand.file)) {
                    return {cand.mix, e->size};
                }
            }
        }
        return {"", 0};
    };

    auto [cnst_mix, cnst_size] = resolve_first("GACNST", ".shp");
    CHECK(cnst_mix == "generic.mix");
    CHECK(cnst_size == 63760);

    auto [cnstmk_mix, cnstmk_size] = resolve_first("GACNSTMK", ".shp");
    CHECK(cnstmk_mix == "isotemp.mix");
    CHECK(cnstmk_size == 428080);
}

// --- TheaterChain table [RE: maps.md §6 + §9 RE-GATE] ---------------------
TEST_CASE("theater_chain: TEMPERATE row reproduces today's hardcoded literals") {
    // maps.md §7 rule 4 / §9: this identity is what keeps every existing
    // candidate walk -- and therefore all 18 gates -- byte-identical.
    const TheaterChain* t = theater_chain("TEMPERATE");
    REQUIRE(t != nullptr);
    CHECK(std::string(t->name) == "TEMPERATE");
    CHECK(t->letter == 'T');
    CHECK(std::string(t->iso_mix) == "isotemp.mix");
    CHECK(std::string(t->art_mix) == "temperat.mix");
    CHECK(std::string(t->control_ini) == "temperat.ini");
    CHECK(std::string(t->iso_pal) == "isotem.pal");
    CHECK(std::string(t->theater_pal) == "temperat.pal");
    CHECK(std::string(t->unit_pal) == "unittem.pal");
    CHECK(std::string(t->ext) == "tem");
    CHECK_FALSE(t->yr_only);
    CHECK(t->playable);
}

TEST_CASE("theater_chain: all six theaters resolve, case-insensitively") {
    CHECK(theater_chain("SNOW") != nullptr);
    CHECK(theater_chain("snow") != nullptr);          // case-insensitive
    CHECK(theater_chain("Snow") != nullptr);
    CHECK(theater_chain("URBAN") != nullptr);
    CHECK(theater_chain("NEWURBAN") != nullptr);
    CHECK(theater_chain("DESERT") != nullptr);
    CHECK(theater_chain("LUNAR") != nullptr);
    CHECK(theater_chain("NOSUCHTHEATER") == nullptr);
    CHECK(theater_chain("") == nullptr);
}

TEST_CASE("theater_chain: SNOW/URBAN use the *md* control INI [RE: maps.md §9.4]") {
    // Task 1 proved snowmd.ini resolves 164/164 and urbanmd.ini 116/116; the
    // non-md files differ in size and were never validated.
    CHECK(std::string(theater_chain("SNOW")->control_ini) == "snowmd.ini");
    CHECK(std::string(theater_chain("URBAN")->control_ini) == "urbanmd.ini");
    CHECK(theater_chain("SNOW")->letter == 'A');       // arctic, the ONE real letter
    CHECK(std::string(theater_chain("SNOW")->ext) == "sno");
    CHECK(std::string(theater_chain("URBAN")->ext) == "urb");
}

TEST_CASE("theater_chain: YR theaters are yr_only; LUNAR is not playable") {
    // maps.md §3: 0 LUNAR multiplayer maps exist, so its chain is unverified.
    CHECK(theater_chain("NEWURBAN")->yr_only);
    CHECK(theater_chain("DESERT")->yr_only);
    CHECK(theater_chain("LUNAR")->yr_only);
    CHECK_FALSE(theater_chain("TEMPERATE")->yr_only);
    CHECK_FALSE(theater_chain("SNOW")->yr_only);
    CHECK_FALSE(theater_chain("URBAN")->yr_only);

    CHECK(theater_chain("NEWURBAN")->playable);
    CHECK(theater_chain("DESERT")->playable);
    CHECK_FALSE(theater_chain("LUNAR")->playable);     // rejected by the browser
}
