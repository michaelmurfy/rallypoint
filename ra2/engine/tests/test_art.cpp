#include "doctest.h"
#include "ra2assets/art.hpp"
#include "ra2assets/ini.hpp"
#include "ra2assets/mix.hpp"
#include "asset_fixture.hpp"
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

using ra2assets::Art;
using ra2assets::ArtSequenceEntry;
using ra2assets::Ini;
using namespace ra2assets;

static const char* kSyntheticArt =
    "[GI] ; GI\n"
    "Sequence=GISequence\n"
    "\n"
    "[GISequence]\n"
    "Ready=0,1,1\n"
    "Walk=8,6,6\n"
    "FireUp=164,6,6\n"
    "Die1=134,15,0\n"
    "Idle1=56,15,0,S\n"          // 4th field (facing label) must be tolerated
    "Cheer= 293 , 8 , 0 ,E\n";   // embedded spaces tolerated

TEST_CASE("art: sequence_name resolves [image] Sequence=") {
    Ini ini = Ini::parse(kSyntheticArt);
    Art art = Art::load(ini);
    CHECK(art.sequence_name("GI") == "GISequence");
    CHECK(art.sequence_name("gi") == "GISequence");  // section lookup case-insensitive
    CHECK(art.sequence_name("NOPE") == "");
}

TEST_CASE("art: entry parses Start,Count,FacingMult and tolerates extras") {
    Ini ini = Ini::parse(kSyntheticArt);
    Art art = Art::load(ini);
    ArtSequenceEntry fire = art.entry("GI", "FireUp");
    CHECK(fire.start == 164);
    CHECK(fire.count == 6);
    CHECK(fire.facing_mult == 6);
    ArtSequenceEntry die = art.entry("GI", "Die1");
    CHECK(die.start == 134);
    CHECK(die.count == 15);
    CHECK(die.facing_mult == 0);
    ArtSequenceEntry idle = art.entry("GI", "Idle1");   // 4-field line
    CHECK(idle.start == 56);
    CHECK(idle.count == 15);
    CHECK(idle.facing_mult == 0);
    ArtSequenceEntry cheer = art.entry("GI", "Cheer");  // spaced fields
    CHECK(cheer.start == 293);
    CHECK(cheer.count == 8);
    CHECK(cheer.facing_mult == 0);
}

TEST_CASE("art: absent image/section/key -> defaults {0,1,0}") {
    Ini ini = Ini::parse(kSyntheticArt);
    Art art = Art::load(ini);
    ArtSequenceEntry e = art.entry("NOPE", "FireUp");
    CHECK(e.start == 0);
    CHECK(e.count == 1);
    CHECK(e.facing_mult == 0);
    ArtSequenceEntry e2 = art.entry("GI", "NoSuchEntry");
    CHECK(e2.start == 0);
    CHECK(e2.count == 1);
    CHECK(e2.facing_mult == 0);
}

TEST_CASE("art: voxel getters") {
    ra2assets::Ini ini = ra2assets::Ini::parse(
        "[HTNK]\nVoxel=yes\nRemapable=yes\nPrimaryFireFLH=150,0,100\n"
        "[GI]\nCameo=GIICON\n");
    ra2assets::Art a = ra2assets::Art::load(ini);
    CHECK(a.voxel("HTNK"));
    CHECK_FALSE(a.voxel("GI"));
    CHECK(a.remapable("HTNK"));
    int flh[3] = {-1, -1, -1};
    CHECK(a.fire_flh("HTNK", flh));
    CHECK(flh[0] == 150); CHECK(flh[1] == 0); CHECK(flh[2] == 100);
    int flh2[3] = {-1, -1, -1};
    CHECK_FALSE(a.fire_flh("GI", flh2));
    CHECK(flh2[0] == -1);
}

TEST_CASE("art: real artmd.ini pins (GISequence == ConSequence for our entries)") {
    // Borrow the shared, load-once ra2md.mix + artmd.ini parse (asset_fixture)
    // instead of re-slurping the ~196 MB archive and re-parsing artmd per case.
    if (!ra2fixture::assets_available()) { MESSAGE("skipped: ra2md.mix not found"); return; }
    if (ra2fixture::artmd_bytes().empty()) {
        MESSAGE("skipped: artmd.ini not found in ra2md.mix top level or in "
                "localmd.mix/expandmd*.mix/cachemd.mix/conqmd.mix");
        return;
    }
    const Art& art = ra2fixture::artmd();
    CHECK(art.sequence_name("GI") == "GISequence");
    CHECK(art.sequence_name("CONS") == "ConSequence");
    CHECK(art.voxel("HTNK"));
    CHECK(art.remapable("HTNK")); /* fire_flh pins 150,0,100 */
    for (const char* img : {"GI", "CONS"}) {
        CHECK(art.entry(img, "Ready").start == 0);
        CHECK(art.entry(img, "Ready").count == 1);
        CHECK(art.entry(img, "Ready").facing_mult == 1);
        CHECK(art.entry(img, "Walk").start == 8);
        CHECK(art.entry(img, "Walk").count == 6);
        CHECK(art.entry(img, "Walk").facing_mult == 6);
        CHECK(art.entry(img, "FireUp").start == 164);
        CHECK(art.entry(img, "FireUp").count == 6);
        CHECK(art.entry(img, "FireUp").facing_mult == 6);
        CHECK(art.entry(img, "Die1").start == 134);
        CHECK(art.entry(img, "Die1").count == 15);
        CHECK(art.entry(img, "Die1").facing_mult == 0);
    }
}

TEST_CASE("art: building accessors (synthetic)") {
    ra2assets::Ini ini = ra2assets::Ini::parse(
        "[GAPOWR]\nFoundation=2x2\nBuildup=GAPOWRMK\nCameo=POWRICON\nNewTheater=yes\n"
        "[BAD]\nFoundation=huge\n");
    ra2assets::Art a = ra2assets::Art::load(ini);
    int fw = -1, fh = -1;
    REQUIRE(a.foundation("GAPOWR", fw, fh));
    CHECK(fw == 2); CHECK(fh == 2);
    int ow = 9, oh = 9;
    CHECK_FALSE(a.foundation("BAD", ow, oh));
    CHECK(ow == 9);                       // untouched on failure
    CHECK_FALSE(a.foundation("MISSING", ow, oh));
    CHECK(a.buildup("GAPOWR") == "GAPOWRMK");
    CHECK(a.buildup("MISSING") == "");
    CHECK(a.cameo("GAPOWR") == "POWRICON");
    CHECK(a.new_theater("GAPOWR"));
    CHECK_FALSE(a.new_theater("MISSING"));
}

TEST_CASE("art: real artmd building pins") {
    if (!ra2fixture::assets_available()) { MESSAGE("skipped: ra2md.mix not found"); return; }
    if (ra2fixture::artmd_bytes().empty()) {
        MESSAGE("skipped: artmd.ini not found in ra2md.mix top level or in "
                "localmd.mix/expandmd*.mix/cachemd.mix/conqmd.mix");
        return;
    }
    const Art& art = ra2fixture::artmd();

    int fw = -1, fh = -1;
    REQUIRE(art.foundation("GACNST", fw, fh));
    CHECK(fw == 4); CHECK(fh == 4);
    REQUIRE(art.foundation("GAPOWR", fw, fh));
    CHECK(fw == 2); CHECK(fh == 2);
    REQUIRE(art.foundation("GAPILE", fw, fh));
    CHECK(fw == 3); CHECK(fh == 2);
    REQUIRE(art.foundation("GAWEAP", fw, fh));
    CHECK(fw == 5); CHECK(fh == 3);
    REQUIRE(art.foundation("NAPOWR", fw, fh));
    CHECK(fw == 3); CHECK(fh == 2);
    REQUIRE(art.foundation("NAHAND", fw, fh));
    CHECK(fw == 2); CHECK(fh == 2);
    REQUIRE(art.foundation("NAWEAP", fw, fh));
    CHECK(fw == 5); CHECK(fh == 3);

    CHECK(art.buildup("GACNST") == "GACNSTMK");
    CHECK(art.buildup("NAWEAP") == "NAWEAPMK");

    CHECK(art.cameo("GAPOWR") == "POWRICON");
    CHECK(art.cameo("GAWEAP") == "gwepicon");
    CHECK(art.cameo("GACNST") == "");

    CHECK(art.new_theater("GACNST"));
}
