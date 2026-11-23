#include "doctest.h"
#include "ra2assets/mix.hpp"
#include "asset_fixture.hpp"
#include <cstdint>
#include <stdexcept>
#include <string>
#include <cstdlib>
#include <fstream>

TEST_CASE("westwood key derivation is deterministic and full-length") {
    uint8_t src[80];
    for (int i = 0; i < 80; i++) src[i] = (uint8_t)(i * 7 + 3);
    uint8_t k1[56] = {0}, k2[56] = {0};
    size_t n1 = ra2assets::detail::westwood_blowfish_key(src, k1);
    size_t n2 = ra2assets::detail::westwood_blowfish_key(src, k2);
    CHECK(n1 == 56);
    CHECK(n2 == 56);
    for (int i = 0; i < 56; i++) CHECK(k1[i] == k2[i]);   // deterministic
    bool all_zero = true;
    for (int i = 0; i < 56; i++) if (k1[i]) all_zero = false;
    CHECK_FALSE(all_zero);                                 // non-degenerate
}

TEST_CASE("westwood key derivation reproduces golden vectors") {
    // Known-answer test: two real (80-byte key source) -> (56-byte Blowfish
    // key) pairs captured as black-box output from ra2md.mix and ra2.mix.
    // Self-contained (no assets required) regression guard for the clean-room
    // RSA key-derivation. The repeated 0x18/0xbb runs are genuine, deterministic
    // output bytes, not padding.
    auto hex2bytes = [](const std::string& h) {
        std::vector<uint8_t> v;
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
        };
        for (size_t i = 0; i + 1 < h.size(); i += 2)
            v.push_back((uint8_t)((nib(h[i]) << 4) | nib(h[i + 1])));
        return v;
    };
    struct Vec { const char* in80; const char* out56; };
    const Vec vecs[] = {
        {"14b9ca52002b8edd5134483fdc97e4d3b692a017c649ca31649b5ce2532c740a7cbbed89b8ea13498ed103323fbe74980d7acdb586336a1dde055c7d44a922a5a9fd619330542d8a811f310ca8e34d4d",
         "86cc35620a8a8caa65e42b1ecc181818181818181818181818181818181818182c394e6db40a7042278e39fbfebbbbbbbbbbbbbbbbbbbbbb"},
        {"1631ea5f4cef1fa2b3134f0c913608115decf296ae8e99690e42dd23140cb78003c8bb2d0710284a30a0d97a6005caa127c839c1f5a28094a6aa4b1ab91156ab881e3061afa04c614d96f580b5753e03",
         "91a29e4ba4231af6d70bd91818181818181818181818181818181818181818181c2d57aa86c2ac04f454babbbbbbbbbbbbbbbbbbbbbbbbbb"},
    };
    for (const auto& v : vecs) {
        auto in = hex2bytes(v.in80);
        auto expected = hex2bytes(v.out56);
        REQUIRE(in.size() == 80);
        REQUIRE(expected.size() == 56);
        uint8_t key[56] = {0};
        size_t n = ra2assets::detail::westwood_blowfish_key(in.data(), key);
        CHECK(n == 56);
        for (int i = 0; i < 56; ++i) CHECK((int)key[i] == (int)expected[i]);
    }
}

TEST_CASE("mix parses unencrypted new-format header and reads entries") {
    using namespace ra2assets;
    std::vector<uint8_t> buf;
    auto put32 = [&](uint32_t v){ for(int i=0;i<4;i++) buf.push_back(v>>(8*i)); };
    auto put16 = [&](uint16_t v){ buf.push_back(v&0xff); buf.push_back(v>>8); };
    // flags = 0 (new format, not encrypted / not checksummed)
    put32(0);
    // header: count, body size
    const char* A = "hello"; const char* B = "world!!";
    uint32_t offA = 0, sizeA = 5, offB = 5, sizeB = 7;
    put16(2); put32(sizeA + sizeB);
    // index entries (order by id not required for this test)
    put32(1234); put32(offA); put32(sizeA);
    put32(5678); put32(offB); put32(sizeB);
    // body
    for (const char* p = A; *p; ++p) buf.push_back((uint8_t)*p);
    for (const char* p = B; *p; ++p) buf.push_back((uint8_t)*p);

    MixFile m = MixFile::open(std::move(buf));
    CHECK_FALSE(m.encrypted());
    CHECK(m.count() == 2);
    const MixEntry* e = m.find(5678);
    REQUIRE(e != nullptr);
    auto data = m.read(*e);
    REQUIRE(data.size() == 7);
    CHECK(std::string(data.begin(), data.end()) == "world!!");
}

TEST_CASE("mix rejects truncated and out-of-range input") {
    using namespace ra2assets;

    // Case 1: Truncated new-format file (4 bytes, not enough for header)
    CHECK_THROWS_AS(MixFile::open({0, 0, 0, 0}), std::runtime_error);

    // Case 2: New-format with count=100 but no index data
    {
        std::vector<uint8_t> buf;
        auto put32 = [&](uint32_t v){ for(int i=0;i<4;i++) buf.push_back(v>>(8*i)); };
        auto put16 = [&](uint16_t v){ buf.push_back(v&0xff); buf.push_back(v>>8); };
        put32(0);  // flags = 0 (new format)
        put16(100);  // count = 100
        put32(0);  // body size = 0
        // Missing 100*12 bytes of index; buffer ends here
        CHECK_THROWS_AS(MixFile::open(std::move(buf)), std::runtime_error);
    }

    // Case 3: Valid header/index but out-of-range entry read
    {
        std::vector<uint8_t> buf;
        auto put32 = [&](uint32_t v){ for(int i=0;i<4;i++) buf.push_back(v>>(8*i)); };
        auto put16 = [&](uint16_t v){ buf.push_back(v&0xff); buf.push_back(v>>8); };
        // flags = 0 (new format, not encrypted / not checksummed)
        put32(0);
        // header: count=2, body size = 100
        put16(2);
        put32(100);
        // index entries: one valid, one out-of-range
        put32(1234); put32(0); put32(10);  // id=1234, offset=0, size=10 (valid)
        put32(5678); put32(0xFFFFFFF0u); put32(0x20u);  // id=5678, offset=huge, size=32 (out of range)
        // body (just 100 bytes)
        for (int i = 0; i < 100; i++) buf.push_back((uint8_t)i);

        MixFile m = MixFile::open(std::move(buf));
        const MixEntry* e = m.find(5678);
        REQUIRE(e != nullptr);
        CHECK_THROWS_AS(m.read(*e), std::runtime_error);
    }
}

TEST_CASE("mix opens a real encrypted archive from the install") {
    using namespace ra2assets;
    // Borrow the shared, load-once ra2md.mix (asset_fixture): top_mix() itself
    // opened it via MixFile::open on the real encrypted bytes, so this still
    // exercises the real loader path -- it just doesn't re-read the ~196 MB
    // file from disk.
    if (!ra2fixture::assets_available()) { MESSAGE("skipped: ra2md.mix not found"); return; }
    const MixFile& m = ra2fixture::top_mix();
    CHECK(m.count() > 0);
    // ra2md.mix is a top-level container of nested mixes; entry count is stable.
    MESSAGE("ra2md.mix entries: " << m.count());
}

TEST_CASE("mix_id resolves well-known nested mix names (stock assets)") {
    using namespace ra2assets;
    int archives_checked = 0;
    if (ra2fixture::ra2_available()) {
        const MixFile& m = ra2fixture::ra2_mix();
        CHECK(m.find(mix_id("cache.mix"))   != nullptr);
        CHECK(m.find(mix_id("local.mix"))   != nullptr);
        CHECK(m.find(mix_id("conquer.mix")) != nullptr);
        archives_checked++;
    }
    if (ra2fixture::assets_available()) {
        const MixFile& m = ra2fixture::top_mix();
        CHECK(m.find(mix_id("cachemd.mix")) != nullptr);
        CHECK(m.find(mix_id("localmd.mix")) != nullptr);
        CHECK(m.find(mix_id("conqmd.mix"))  != nullptr);
        archives_checked++;
    }
    if (archives_checked == 0) {
        MESSAGE("skipped: no stock archives under RA2_INSTALL_DIR");
        return;
    }
}

TEST_CASE("mix_id matches real IDs via local mix database (if present)") {
    using namespace ra2assets;
    // ecache01.mix is read exactly once across the suite, so it keeps a direct
    // read (via the shared slurp helper) -- this case exercises local_names()/
    // mix_id() against a real local mix database.
    auto bytes = ra2fixture::slurp(ra2fixture::install_base() + "\\ecache01.mix");
    if (bytes.empty()) { MESSAGE("skipped: ecache01.mix not found"); return; }
    MixFile m = MixFile::open(std::move(bytes));
    auto names = m.local_names();
    if (names.empty()) { MESSAGE("no local db in this mix; skipped"); return; }
    int matched = 0;
    for (auto& nm : names)
        if (m.find(mix_id(nm))) matched++;
    // The vast majority of named files must resolve.
    CHECK(matched > 0);
    CHECK(matched >= (int)names.size() * 3 / 4);
}

TEST_CASE("mix_id known vector") {
    // Oracle: the real YR 1.001 ra2md.mix index (read from RA2_INSTALL_DIR),
    // where find(mix_id("cachemd.mix")) resolves to entry id 0x68EEC99A. This
    // literal was captured from Westwood's own shipped archive data (not
    // derived by re-running our own CRC code path) and also independently
    // appears in docs/re/mix.md's Task 4 dump of ra2md.mix's real entry IDs,
    // recorded before this test existed. Frozen here as a fixed regression
    // vector that needs no install present to run.
    CHECK(ra2assets::mix_id("cachemd.mix") == 0x68EEC99Au);
}

TEST_CASE("open_nested opens a real child mix and throws on unknown names") {
    using namespace ra2assets;
    // Borrow the shared, load-once ra2.mix (asset_fixture): open_nested is
    // still exercised on the real archive, just without re-reading the file.
    if (!ra2fixture::ra2_available()) { MESSAGE("skipped: ra2.mix not found"); return; }
    const MixFile& m = ra2fixture::ra2_mix();

    // Open cache.mix as a nested archive
    MixFile cache_child = m.open_nested("cache.mix");
    CHECK(cache_child.count() > 0);
    MESSAGE("cache.mix entries: " << cache_child.count());

    // Open local.mix as a nested archive
    MixFile local_child = m.open_nested("local.mix");
    CHECK(local_child.count() > 0);
    MESSAGE("local.mix entries: " << local_child.count());

    // Verify that unknown nested files throw std::runtime_error
    CHECK_THROWS_AS(m.open_nested("no-such-file.mix"), std::runtime_error);
}

// W2-GA-P5: a file-backed MIX (offset reads, nested VIEWS) reads exactly the
// same bytes as the whole-file in-memory open.
TEST_CASE("mix: open_path + nested views match the in-memory open (real install)") {
    if (!ra2fixture::assets_available()) { MESSAGE("install absent; skipped"); return; }
    std::string path = ra2fixture::install_base();
    if (!path.empty() && path.back() != '/' && path.back() != '\\') path += "/";
    path += "ra2md.mix";
    const ra2assets::MixFile& mem = ra2fixture::top_mix();
    const ra2assets::MixFile disk = ra2assets::MixFile::open_path(path);
    REQUIRE(disk.count() == mem.count());
    const ra2assets::MixFile a = mem.open_nested("localmd.mix");
    const ra2assets::MixFile b = disk.open_nested("localmd.mix");
    REQUIRE(a.count() == b.count());
    const ra2assets::MixEntry* ea = a.find(std::string("rulesmd.ini"));
    const ra2assets::MixEntry* eb = b.find(std::string("rulesmd.ini"));
    REQUIRE(ea);
    REQUIRE(eb);
    CHECK(a.read(*ea) == b.read(*eb));
    // A second open of the same path shares the handle and still reads right.
    const ra2assets::MixFile disk2 = ra2assets::MixFile::open_path(path);
    CHECK(disk2.open_nested("localmd.mix").read(*eb) == a.read(*ea));
    CHECK_THROWS(ra2assets::MixFile::open_path(path + ".absent"));
}
