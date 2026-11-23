#include "doctest.h"
#include "ra2assets/csf.hpp"
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace ra2assets;

// ---------------------------------------------------------------------------
// Synthetic CSF builder. Emits the exact on-disk layout (little-endian,
// packed; byte-verified against the install's ra2.csf -- see src/csf.cpp):
//   file header (24B): " FSC", u32 version=3, u32 num_labels, u32 num_strings,
//                      u32 unused=0, u32 language=0
//   per label: " LBL", u32 num_values, u32 label_len, label bytes (no NUL)
//   per value: " RTS" (or "WRTS"), u32 val_len (UTF-16 code units), then
//              val_len*2 bytes UTF-16LE with every byte XOR 0xFF; a "WRTS"
//              record adds u32 extra_len + extra_len ASCII bytes.
// ---------------------------------------------------------------------------
struct CsfValue {
    std::u16string text;
    bool wide = false;      // emit "WRTS" instead of " RTS"
    std::string extra;      // WRTS-only trailing ASCII payload
};
struct CsfLabel {
    std::string name;
    std::vector<CsfValue> values;
};

static std::vector<uint8_t> make_csf(const std::vector<CsfLabel>& labels) {
    std::vector<uint8_t> b;
    auto p8  = [&](uint8_t v){ b.push_back(v); };
    auto p32 = [&](uint32_t v){ for (int i=0;i<4;i++) b.push_back((uint8_t)((v>>(8*i))&0xff)); };
    auto pmagic = [&](const char* m){ for (int i=0;i<4;i++) b.push_back((uint8_t)m[i]); };
    // file header (24 bytes)
    pmagic(" FSC");                            // 0x20 0x46 0x53 0x43
    p32(3);                                    // version
    p32((uint32_t)labels.size());              // num_labels
    uint32_t num_strings = 0;
    for (const auto& l : labels) num_strings += (uint32_t)l.values.size();
    p32(num_strings);                          // num_strings
    p32(0);                                    // unused
    p32(0);                                    // language
    for (const auto& l : labels) {
        pmagic(" LBL");                        // 0x20 0x4C 0x42 0x4C
        p32((uint32_t)l.values.size());
        p32((uint32_t)l.name.size());
        for (char c : l.name) p8((uint8_t)c);
        for (const auto& v : l.values) {
            pmagic(v.wide ? "WRTS" : " RTS");
            p32((uint32_t)v.text.size());
            for (char16_t u : v.text) {        // UTF-16LE, every byte XOR 0xFF
                p8((uint8_t)((((uint16_t)u)      & 0xff) ^ 0xff));
                p8((uint8_t)((((uint16_t)u >> 8) & 0xff) ^ 0xff));
            }
            if (v.wide) {
                p32((uint32_t)v.extra.size());
                for (char c : v.extra) p8((uint8_t)c);
            }
        }
    }
    return b;
}

TEST_CASE("csf synthetic: two labels load, find, count, absent label") {
    auto bytes = make_csf({
        {"GUI:Ok",    {{u"OK"}}},
        {"Name:HTNK", {{u"Rhino Heavy Tank"}}},
    });
    CsfStrings cs = CsfStrings::load(bytes.data(), bytes.size());
    CHECK(cs.count() == 2);
    const std::u16string* ok = cs.find("GUI:Ok");
    REQUIRE(ok != nullptr);
    CHECK(*ok == u"OK");
    const std::u16string* tank = cs.find("Name:HTNK");
    REQUIRE(tank != nullptr);
    CHECK(*tank == u"Rhino Heavy Tank");
    CHECK(cs.find("Name:E1") == nullptr);      // absent -> nullptr
}

TEST_CASE("csf synthetic: label lookup is case-insensitive") {
    auto bytes = make_csf({ {"GUI:Ok", {{u"OK"}}} });
    CsfStrings cs = CsfStrings::load(bytes.data(), bytes.size());
    const std::u16string* a = cs.find("gui:ok");
    REQUIRE(a != nullptr);
    CHECK(*a == u"OK");
    const std::u16string* c = cs.find("GUI:OK");
    REQUIRE(c != nullptr);
    CHECK(*c == u"OK");
}

TEST_CASE("csf synthetic: non-ASCII UTF-16 units survive the XOR transform") {
    // A Latin-1 char (U+00E9), a CJK char (U+4E2D), and a space: exercises
    // both bytes of each 16-bit unit through the XOR-0xFF store.
    const std::u16string text = u"Café 中";
    auto bytes = make_csf({ {"TEST:Unicode", {{text}}} });
    CsfStrings cs = CsfStrings::load(bytes.data(), bytes.size());
    const std::u16string* got = cs.find("TEST:Unicode");
    REQUIRE(got != nullptr);
    CHECK(*got == text);
    CHECK(got->size() == 6);
}

TEST_CASE("csf synthetic: WRTS value skips extra ASCII and stays in sync") {
    // A WRTS first value carries trailing ASCII data; the reader must skip
    // it and land exactly on the next label record.
    auto bytes = make_csf({
        {"GUI:Save", {{u"Save", true, "save_extra"}}},
        {"GUI:Load", {{u"Load"}}},
    });
    CsfStrings cs = CsfStrings::load(bytes.data(), bytes.size());
    CHECK(cs.count() == 2);
    const std::u16string* save = cs.find("GUI:Save");
    REQUIRE(save != nullptr);
    CHECK(*save == u"Save");
    const std::u16string* load = cs.find("GUI:Load");
    REQUIRE(load != nullptr);                  // the walk stayed in sync
    CHECK(*load == u"Load");
}

TEST_CASE("csf synthetic: multi-value label stores first value, walks the rest") {
    auto bytes = make_csf({
        {"Multi:Val",   {{u"first"}, {u"second"}}},
        {"After:Multi", {{u"still here"}}},
    });
    CsfStrings cs = CsfStrings::load(bytes.data(), bytes.size());
    CHECK(cs.count() == 2);
    const std::u16string* v = cs.find("Multi:Val");
    REQUIRE(v != nullptr);
    CHECK(*v == u"first");                     // value 0 wins
    const std::u16string* after = cs.find("After:Multi");
    REQUIRE(after != nullptr);                 // second value was walked over
    CHECK(*after == u"still here");
}

TEST_CASE("csf synthetic: zero-value label is walked but not stored") {
    auto bytes = make_csf({
        {"EMPTY:NoValues", {}},                // num_values == 0
        {"GUI:Ok", {{u"OK"}}},
    });
    CsfStrings cs = CsfStrings::load(bytes.data(), bytes.size());
    CHECK(cs.count() == 1);                    // zero-value label excluded
    CHECK(cs.find("EMPTY:NoValues") == nullptr);
    const std::u16string* ok = cs.find("GUI:Ok");
    REQUIRE(ok != nullptr);
    CHECK(*ok == u"OK");
}

TEST_CASE("csf synthetic: empty-TEXT value is stored (distinct from zero values)") {
    auto bytes = make_csf({ {"Blank:Text", {{u""}}} });   // one value, val_len 0
    CsfStrings cs = CsfStrings::load(bytes.data(), bytes.size());
    CHECK(cs.count() == 1);
    const std::u16string* blank = cs.find("Blank:Text");
    REQUIRE(blank != nullptr);                 // present ...
    CHECK(blank->empty());                     // ... and empty
}

TEST_CASE("csf malformed input throws with context") {
    // Base image for the corruption cases: one label "AA" with one " RTS"
    // value u"X". Offsets: file header 0..23, label magic @24, num_values
    // @28, label_len @32, name "AA" @36..37, value magic @38, val_len @42,
    // payload @46..47. Total 48 bytes.
    auto base = make_csf({ {"AA", {{u"X"}}} });
    REQUIRE(base.size() == 48);

    // Too small for the 24-byte file header.
    CHECK_THROWS_AS(CsfStrings::load(base.data(), 3), std::runtime_error);

    // Wrong file magic.
    {
        auto b = base;
        b[0] = 0x21;                           // " FSC" -> "!FSC"
        CHECK_THROWS_AS(CsfStrings::load(b.data(), b.size()), std::runtime_error);
    }
    // Wrong label magic -> the message reports the file offset (24).
    {
        auto b = base;
        b[24] = 0x00;
        try {
            CsfStrings::load(b.data(), b.size());
            FAIL("expected std::runtime_error");
        } catch (const std::runtime_error& e) {
            CHECK(std::string(e.what()).find("offset 24") != std::string::npos);
        }
    }
    // Label name length runs past EOF.
    {
        auto b = base;
        b[32]=0xFF; b[33]=0xFF; b[34]=0x00; b[35]=0x00;   // label_len = 65535
        CHECK_THROWS_AS(CsfStrings::load(b.data(), b.size()), std::runtime_error);
    }
    // Wrong value magic.
    {
        auto b = base;
        b[38] = 0x00;                          // " RTS" -> "\0RTS"
        CHECK_THROWS_AS(CsfStrings::load(b.data(), b.size()), std::runtime_error);
    }
    // Value payload length runs past EOF.
    {
        auto b = base;
        b[42]=0xFF; b[43]=0xFF; b[44]=0x00; b[45]=0x00;   // val_len = 65535 units
        CHECK_THROWS_AS(CsfStrings::load(b.data(), b.size()), std::runtime_error);
    }
    // Truncated mid value header.
    CHECK_THROWS_AS(CsfStrings::load(base.data(), 40), std::runtime_error);

    // num_labels claims more records than the file holds.
    {
        auto b = base;
        b[8] = 2;                              // num_labels 1 -> 2
        CHECK_THROWS_AS(CsfStrings::load(b.data(), b.size()), std::runtime_error);
    }

    // WRTS extra_len runs past EOF -> the error mentions the extra data.
    // Layout: label "AA" @24 (12B hdr + 2B name), "WRTS" @38, val_len @42,
    // payload @46..47, extra_len @48, extra "hi" @52..53. Total 54 bytes.
    {
        auto b = make_csf({ {"AA", {{u"X", true, "hi"}}} });
        REQUIRE(b.size() == 54);
        b[48]=0xFF; b[49]=0xFF; b[50]=0x00; b[51]=0x00;   // extra_len = 65535
        try {
            CsfStrings::load(b.data(), b.size());
            FAIL("expected std::runtime_error");
        } catch (const std::runtime_error& e) {
            CHECK(std::string(e.what()).find("extra") != std::string::npos);
        }
    }
    // WRTS truncated before the extra_len field itself.
    {
        auto b = make_csf({ {"AA", {{u"X", true, "hi"}}} });
        CHECK_THROWS_AS(CsfStrings::load(b.data(), 50), std::runtime_error);
    }
}

// ---------------------------------------------------------------------------
// Guarded real-asset oracle: loads the LOOSE ra2.csf from the install root
// (no MIX extraction needed) and checks known label->text vectors decoded
// independently (Python byte-walk of the same file, 2026-07-09). Skips
// cleanly with a doctest MESSAGE when the install is absent so asset-free
// CI still builds and passes.
// ---------------------------------------------------------------------------
static std::vector<uint8_t> slurp_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

TEST_CASE("csf real asset: ra2.csf known vectors") {
    const char* id = std::getenv("RA2_INSTALL_DIR");
    fs::path install = id ? id : "D:\\Red Alert 2";
    fs::path csf = install / "ra2.csf";
    if (!fs::exists(csf)) { MESSAGE("ra2.csf absent; skipped"); return; }

    auto bytes = slurp_file(csf);
    REQUIRE_FALSE(bytes.empty());
    CsfStrings cs = CsfStrings::load(bytes.data(), bytes.size());

    // THIS install's ra2.csf has 4562 label records = 4558 distinct labels
    // after case-insensitive normalization (4 duplicate pairs, keep-first);
    // assert loosely so other language/patch variants still pass.
    CHECK(cs.count() > 4000);

    struct Vec { const char* label; std::u16string want; };
    const Vec vecs[] = {
        {"GUI:Cancel",   u"Cancel"},
        {"Name:HTNK",    u"Rhino Heavy Tank"},
        {"Name:E1",      u"GI"},
        {"GUI:LoadGame", u"Load Game"},
    };
    int checked = 0;
    for (const auto& v : vecs) {
        const std::u16string* got = cs.find(v.label);
        REQUIRE(got != nullptr);
        CHECK(*got == v.want);
        checked++;
    }
    CHECK(checked == 4);   // guards against a silent skip when assets exist
}
