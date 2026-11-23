#include "doctest.h"
#include "ra2assets/ini.hpp"
#include <string>
#include <vector>
using namespace ra2assets;

TEST_CASE("ini: basic sections, key=value, get/get_int, has") {
    std::string text =
        "[Basic]\n"
        "Name=Rifle Infantry\n"
        "Speed=4\n"
        "\n"
        "[Map]\n"
        "Theater=temperate\n";
    Ini ini = Ini::parse(text);

    CHECK(ini.has("Basic"));
    CHECK(ini.has("Map"));
    CHECK_FALSE(ini.has("Nope"));

    CHECK(ini.get("Basic", "Name") == "Rifle Infantry");
    CHECK(ini.get_int("Basic", "Speed") == 4);
    CHECK(ini.get("Map", "Theater") == "temperate");

    // Missing key/section fall back to the supplied default.
    CHECK(ini.get("Basic", "NoSuchKey", "fallback") == "fallback");
    CHECK(ini.get("NoSuchSection", "Key", "fallback2") == "fallback2");
    CHECK(ini.get_int("Basic", "NoSuchKey", 99) == 99);
}

TEST_CASE("ini: ';' and '#' full-line comments are ignored") {
    std::string text =
        "; this whole file starts with a comment\n"
        "[Sect]\n"
        "; another comment line\n"
        "A=1\n"
        "# hash-style comment too\n"
        "B=2\n";
    Ini ini = Ini::parse(text);
    REQUIRE(ini.has("Sect"));
    CHECK(ini.get_int("Sect", "A") == 1);
    CHECK(ini.get_int("Sect", "B") == 2);
    // The comment lines must not have been parsed as key=value entries.
    CHECK(ini.section("Sect").size() == 2);
}

TEST_CASE("ini: whitespace is trimmed on section names, keys, and values") {
    std::string text =
        "[   Spacey Section   ]\n"
        "   Key1   =   Value With Spaces   \n"
        "\tKey2\t=\tTabbed\t\n";
    Ini ini = Ini::parse(text);
    REQUIRE(ini.has("Spacey Section"));
    CHECK(ini.get("Spacey Section", "Key1") == "Value With Spaces");
    CHECK(ini.get("Spacey Section", "Key2") == "Tabbed");
    CHECK(ini.get("  spacey section  ", "  key1  ") == ""); // caller must trim its own query strings
}

TEST_CASE("ini: duplicate keys - get() returns last, section() preserves all in order") {
    std::string text =
        "[Dup]\n"
        "K=first\n"
        "K=second\n"
        "K=third\n";
    Ini ini = Ini::parse(text);

    // Override semantics: get() must return the LAST value.
    CHECK(ini.get("Dup", "K") == "third");

    // But section() must retain every occurrence, in file order, so callers
    // that need the raw ordered list (e.g. numbered-key concatenation) can
    // still see all three.
    const auto& kv = ini.section("Dup");
    REQUIRE(kv.size() == 3);
    CHECK(kv[0].second == "first");
    CHECK(kv[1].second == "second");
    CHECK(kv[2].second == "third");
}

TEST_CASE("ini: case-insensitive section and key lookup, both directions") {
    std::string text =
        "[MAP]\n"
        "theater=temperate\n";
    Ini ini = Ini::parse(text);

    // File has upper-case section / lower-case key; query with the opposite
    // case combinations and mixed case to prove normalization is symmetric.
    CHECK(ini.get("Map", "Theater") == "temperate");
    CHECK(ini.get("map", "THEATER") == "temperate");
    CHECK(ini.get("mAp", "ThEaTeR") == "temperate");
    CHECK(ini.has("map"));
    CHECK(ini.has("MAP"));

    // section() lookup must be equally case-insensitive.
    CHECK(ini.section("map").size() == 1);
    CHECK(ini.section("MAP").size() == 1);
}

TEST_CASE("ini: IsoMapPack5-style numbered keys concatenate in insertion order") {
    // Real map INIs wrap a base64 blob across many "N=chunk" keys; Task 5
    // depends on section() preserving that order so the chunks can be
    // concatenated correctly before base64-decoding.
    std::string text =
        "[IsoMapPack5]\n"
        "1=AAAA\n"
        "2=BBBB\n"
        "3=CCCC\n"
        "4=DDDD\n";
    Ini ini = Ini::parse(text);
    const auto& kv = ini.section("IsoMapPack5");
    REQUIRE(kv.size() == 4);
    std::string concat;
    for (auto& p : kv) concat += p.second;
    CHECK(concat == "AAAABBBBCCCCDDDD");

    // get_int on a numbered key should also work as a sanity check that key
    // normalization doesn't disturb purely-numeric keys.
    CHECK(ini.get("IsoMapPack5", "1") == "AAAA");
    CHECK(ini.get("IsoMapPack5", "3") == "CCCC");
}

TEST_CASE("ini: get_int parses a leading integer, ignores unparseable/absent") {
    std::string text =
        "[Rules]\n"
        "Speed=4\n"
        "Negative=-7\n"
        "Name=NotANumber\n"
        "Empty=\n";
    Ini ini = Ini::parse(text);
    CHECK(ini.get_int("Rules", "Speed") == 4);
    CHECK(ini.get_int("Rules", "Negative") == -7);
    CHECK(ini.get_int("Rules", "Name", -1) == -1);   // unparseable -> def
    CHECK(ini.get_int("Rules", "Empty", -2) == -2);  // empty value -> def
    CHECK(ini.get_int("Rules", "Missing", -3) == -3); // absent key -> def
}

TEST_CASE("ini: absent section returns empty ref (no throw), has() false") {
    Ini ini = Ini::parse("[Only]\nA=1\n");
    CHECK_FALSE(ini.has("Nowhere"));
    const auto& kv = ini.section("Nowhere");
    CHECK(kv.empty());
    CHECK(ini.get("Nowhere", "A", "def") == "def");
    CHECK(ini.get_int("Nowhere", "A", 42) == 42);
}

TEST_CASE("ini: handles CRLF and LF line endings, including mixed") {
    std::string text =
        "[Mixed]\r\n"
        "A=1\r\n"
        "B=2\n"
        "C=3\r\n";
    Ini ini = Ini::parse(text);
    REQUIRE(ini.has("Mixed"));
    CHECK(ini.get_int("Mixed", "A") == 1);
    CHECK(ini.get_int("Mixed", "B") == 2);
    CHECK(ini.get_int("Mixed", "C") == 3);
    // Values must not retain a trailing '\r' (which would break get_int / equality).
    CHECK(ini.get("Mixed", "A") == "1");
}

TEST_CASE("ini: section_names lists parsed sections") {
    std::string text = "[Alpha]\nA=1\n[Beta]\nB=2\n";
    Ini ini = Ini::parse(text);
    auto names = ini.section_names();
    REQUIRE(names.size() == 2);
    // Order/case not strictly mandated beyond case-insensitive matching, but
    // both sections must be discoverable via has().
    CHECK(ini.has("Alpha"));
    CHECK(ini.has("Beta"));
}

TEST_CASE("ini: inline ';' comments are cut like INIClass::Load (RC-5)") {
    std::string text =
        "[GAPILL]\n"
        "Primary=Vulcan2 ;comment\n"
        "Teleporter=yes;\n"
        "Naval=yes ;GS\n"
        "Empty=\n"
        "OnlyComment= ; nothing\n"
        "Dup=first\n"
        "Dup=;shadow attempt\n"
        "=novalue\n";
    Ini ini = Ini::parse(text);
    CHECK(ini.get("GAPILL", "Primary") == "Vulcan2");
    CHECK(ini.get("GAPILL", "Teleporter") == "yes");
    CHECK(ini.get("GAPILL", "Naval") == "yes");
    // Empty values are not entries at all (game skips strlen==0 values).
    CHECK(ini.get("GAPILL", "Empty", "def") == "def");
    CHECK(ini.get("GAPILL", "OnlyComment", "def") == "def");
    CHECK(ini.get("GAPILL", "Dup") == "first");
    CHECK(ini.section("GAPILL").size() == 4);
}
