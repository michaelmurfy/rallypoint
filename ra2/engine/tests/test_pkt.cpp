#include "doctest.h"
#include "ra2assets/pkt.hpp"
#include "ra2assets/mix.hpp"
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
using namespace ra2assets;

namespace {
std::vector<uint8_t> slurp_pkt(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());
}
} // namespace

// --- SYNTHETIC: the exact shape of maps.md §2's real excerpt ---------------
TEST_CASE("pkt: [MultiMaps] index + per-map sections [RE: maps.md §2]") {
    const std::string text =
        "[MultiMaps]\n"
        "; 2 Player Maps\n"
        "1=XMP29U2\n"
        "2=BldFeud\n"
        "3=Ghost\n"          // listed but has NO section -> still an entry
        ";4=Commented\n"     // full-line comment -> dropped by Ini
        "\n"
        "[XMP29U2]\n"
        "Description=DESC:MP29U2\n"
        "CD=2\n"
        "MinPlayers=2\n"
        "MaxPlayers=2\n"
        "GameMode=standard, meatgrind\n"
        "\n"
        "[BldFeud]\n"
        "Description=DESC:BLDFEUD\n"
        "MinPlayers=2\n"
        "MaxPlayers=4\n"
        "GameMode=megawealth\n";

    PktFile pkt = PktFile::parse(text);
    REQUIRE(pkt.entries().size() == 3);              // the ;4= row is NOT listed

    const PktMapEntry& a = pkt.entries()[0];
    CHECK(a.index_key == "1");
    CHECK(a.base_name == "XMP29U2");
    CHECK(a.file_name == "xmp29u2.map");             // lowercased + ".map"
    CHECK(a.description == "DESC:MP29U2");
    CHECK(a.min_players == 2);
    CHECK(a.max_players == 2);
    CHECK(a.cd == 2);
    REQUIRE(a.game_modes.size() == 2);
    CHECK(a.game_modes[0] == "standard");            // trimmed + lowercased
    CHECK(a.game_modes[1] == "meatgrind");
    CHECK(a.has_mode("STANDARD"));                   // case-insensitive
    CHECK(a.has_mode("meatgrind"));
    CHECK_FALSE(a.has_mode("duel"));

    const PktMapEntry& b = pkt.entries()[1];
    CHECK(b.file_name == "bldfeud.map");
    CHECK(b.max_players == 4);
    CHECK(b.has_mode("megawealth"));
    CHECK_FALSE(b.has_mode("standard"));

    // A [MultiMaps] value with no section is STILL an entry (the existence
    // filter is the caller's job, maps.md §2).
    const PktMapEntry& c = pkt.entries()[2];
    CHECK(c.base_name == "Ghost");
    CHECK(c.file_name == "ghost.map");
    CHECK(c.description.empty());
    CHECK(c.game_modes.empty());
    CHECK(c.max_players == 0);
}

TEST_CASE("pkt: no [MultiMaps] section -> empty, no throw") {
    CHECK(PktFile::parse("[Header]\nFoo=1\n").entries().empty());
    CHECK(PktFile::parse("").entries().empty());
}

// --- REAL ASSET (guarded): langmd.mix/missionsmd.pkt ----------------------
TEST_CASE("pkt: real missionsmd.pkt = 161 active entries [RE: maps.md §2/§3/§9.6]") {
    const char* dir = std::getenv("RA2_INSTALL_DIR");
    const std::string base = dir ? dir : "D:\\Red Alert 2";
    auto bytes = slurp_pkt(base + "\\langmd.mix");
    if (bytes.empty()) { MESSAGE("skipped: langmd.mix not found under " << base); return; }
    MixFile lang = MixFile::open(std::move(bytes));
    const MixEntry* e = lang.find(std::string("missionsmd.pkt"));
    if (!e) { MESSAGE("skipped: missionsmd.pkt not in langmd.mix"); return; }
    auto raw = lang.read(*e);
    PktFile pkt = PktFile::parse(std::string(raw.begin(), raw.end()));

    CHECK(pkt.entries().size() == 161);          // [RE: maps.md §9.6]
    int standard = 0;
    for (const auto& m : pkt.entries()) if (m.has_mode("standard")) ++standard;
    CHECK(standard == 91);                       // the skirmish/Battle list

    // The §2 excerpt's own row.
    const PktMapEntry* sp = nullptr;
    for (const auto& m : pkt.entries()) if (m.file_name == "xmp01t4.map") sp = &m;
    REQUIRE(sp != nullptr);
    CHECK(sp->description == "DESC:MP01DU");
    CHECK(sp->min_players == 2);
    CHECK(sp->max_players == 4);
    CHECK(sp->has_mode("standard"));
}
