#include "doctest.h"
#include "ra2assets/ini.hpp"
#include "ra2assets/mix.hpp"
#include "ra2assets/sounds.hpp"
#include "asset_fixture.hpp"
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

using namespace ra2assets;

static const char* kSyntheticSound =
    "[Defaults]\n"
    "MinVolume=50\nRange=10\nVolume=80\nLimit=5\n"
    "Type= NORMAL SCREEN UNSHROUD\nPriority=NORMAL\n"
    "\n"
    "[GIAttack]\n"
    "Sounds=igiat1a igiat1b igiat1c\n"
    "Control= random interrupt\n"
    "VShift=15\nVolume=65\n"
    "\n"
    "[GIDie]\n"
    "Sounds= $igidia $igidib\n"
    "Priority=low\nControl= random\nFShift= -10 10\nVolume=85\n"
    "\n"
    "[HashCase]\n"
    "Sounds= #habc $hdef\n";

TEST_CASE("sounds: entry inherits [Defaults] for absent keys") {
    Ini ini = Ini::parse(kSyntheticSound);
    Sounds snd = Sounds::load(ini);
    SoundEntry e = snd.entry("GIAttack");
    CHECK(e.volume == 65);      // overridden
    CHECK(e.limit == 5);        // from [Defaults]
    CHECK(e.range == doctest::Approx(10.0));
    CHECK(e.min_volume == 50);
    CHECK(e.priority == "normal");
    REQUIRE(e.type.size() == 3);
    CHECK(e.type[0] == "normal");
    CHECK(e.type[1] == "screen");
    CHECK(e.type[2] == "unshroud");
}

TEST_CASE("sounds: variant list, $ stripping, control tokens, shifts") {
    Ini ini = Ini::parse(kSyntheticSound);
    Sounds snd = Sounds::load(ini);
    SoundEntry a = snd.entry("GIAttack");
    REQUIRE(a.sounds.size() == 3);
    CHECK(a.sounds[0] == "igiat1a");
    CHECK(a.sounds[2] == "igiat1c");
    REQUIRE(a.control.size() == 2);
    CHECK(a.control[0] == "random");
    CHECK(a.control[1] == "interrupt");
    CHECK(a.vshift == 15);
    SoundEntry d = snd.entry("GIDie");
    REQUIRE(d.sounds.size() == 2);
    CHECK(d.sounds[0] == "igidia");   // '$' stripped
    CHECK(d.priority == "low");
    CHECK(d.fshift_lo == -10);
    CHECK(d.fshift_hi == 10);
    // '#' strips exactly like '$' (docs/re/audio-playback.md finding 1: the
    // real add_sound() strips BOTH leading sigils, not just '$'):
    SoundEntry hc = snd.entry("HashCase");
    REQUIRE(hc.sounds.size() == 2);
    CHECK(hc.sounds[0] == "habc");
    CHECK(hc.sounds[1] == "hdef");
    CHECK(snd.has("GIDie"));
    CHECK_FALSE(snd.has("NoSuchSound"));
    SoundEntry missing = snd.entry("NoSuchSound");   // -> defaults verbatim
    CHECK(missing.sounds.empty());
    CHECK(missing.volume == 80);
    CHECK(missing.limit == 5);
}

TEST_CASE("sounds: real soundmd.ini pins") {
    // Borrow the shared, load-once ra2md.mix (asset_fixture) instead of
    // re-slurping the ~196 MB archive; soundmd.ini lives in the same
    // localmd.mix as artmd.ini/rulesmd.ini and is resolved via the SAME
    // nested-candidate walk. This case is loaded once, so it keeps its own
    // Ini::parse + Sounds::load (its actual subject) after the shared read.
    if (!ra2fixture::assets_available()) { MESSAGE("skipped: ra2md.mix not found"); return; }
    auto sound_bytes = ra2fixture::resolve_nested_bytes("soundmd.ini");
    if (sound_bytes.empty()) {
        MESSAGE("skipped: soundmd.ini not found in ra2md.mix top level or in "
                "localmd.mix/expandmd*.mix/cachemd.mix/conqmd.mix");
        return;
    }

    Ini ini = Ini::parse(std::string(sound_bytes.begin(), sound_bytes.end()));
    Sounds snd = Sounds::load(ini);
    SoundEntry def = snd.entry("NoSuchSound__");
    CHECK(def.volume == 80);
    CHECK(def.limit == 5);
    CHECK(def.range == doctest::Approx(10.0));
    CHECK(def.min_volume == 50);
    SoundEntry gia = snd.entry("GIAttack");
    REQUIRE(gia.sounds.size() == 3);
    CHECK(gia.sounds[0] == "igiat1a");
    CHECK(gia.volume == 65);
    CHECK(gia.vshift == 15);
    REQUIRE(gia.control.size() == 2);
    CHECK(gia.control[1] == "interrupt");
    SoundEntry cona = snd.entry("ConscriptAttack");
    CHECK(cona.sounds.size() == 7);
    CHECK(cona.limit == 3);
    SoundEntry gid = snd.entry("GIDie");
    CHECK(gid.sounds.size() == 5);
    CHECK(gid.sounds[0] == "igidia");
    CHECK(gid.priority == "low");
    CHECK(gid.fshift_lo == -10);
    CHECK(gid.fshift_hi == 10);
    SoundEntry gis = snd.entry("GISelect");
    CHECK(gis.sounds.size() == 6);
    CHECK(gis.volume == 85);
}

// W5-E: Delay= min max (ms); one value = both.
TEST_CASE("sounds: Delay parses min/max milliseconds") {
    Ini ini = Ini::parse("[SoundList]\n0=A\n1=B\n[A]\nSounds=x\nDelay= 500 3000\nControl=predelay\n"
                         "[B]\nSounds=y\nDelay=1500\nControl=loop ambient\nType=local\n");
    Sounds snd = Sounds::load(ini);
    const SoundEntry a = snd.entry("A");
    CHECK(a.delay_lo == 500);
    CHECK(a.delay_hi == 3000);
    const SoundEntry b = snd.entry("B");
    CHECK(b.delay_lo == 1500);
    CHECK(b.delay_hi == 1500);
    REQUIRE(b.type.size() == 1);
    CHECK(b.type[0] == "local");
}
