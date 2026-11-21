#include "doctest.h"
#include "ra2assets/fileio.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace ra2assets;

// Writes `bytes` to a fresh binary file under the system temp dir and returns
// its path. Truncates any stale file left by a previous crashed run. The name
// gets a per-process random suffix so two concurrently running test suites
// (parallel CI jobs, two dev shells) cannot race each other on the same path.
static fs::path write_tmp_file(const std::string& name,
                               const std::vector<uint8_t>& bytes) {
    static const std::string suffix =
        "." + std::to_string(std::random_device{}());
    fs::path p = fs::temp_directory_path() / (name + suffix);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    REQUIRE(f.good());
    if (!bytes.empty())
        f.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    f.close();
    REQUIRE(f.good());
    return p;
}

TEST_CASE("read_file: binary round-trip including DOS-EOF and CR/LF bytes") {
    // 0x1A (DOS EOF) truncates text-mode reads on Windows, and 0x0D 0x0A would
    // be collapsed by newline translation -- surviving them proves the file is
    // opened in binary mode.
    const std::vector<uint8_t> want = {0x00, 0x1A, 0xFF, 0x0D, 0x0A,
                                       0x1A, 0x00, 0x7F, 0x80, 0x0A,
                                       0x0D, 0x1A, 0xFE, 0x01, 0x1A};
    fs::path p = write_tmp_file("ra2_fileio_roundtrip.bin", want);
    std::vector<uint8_t> got = read_file(p.string());
    CHECK(got == want);
    fs::remove(p);
}

TEST_CASE("read_file: equals the old istreambuf_iterator slurp byte-for-byte") {
    // 4 KiB + an odd tail so the content is not block-aligned.
    std::vector<uint8_t> want(4096 + 37);
    for (size_t i = 0; i < want.size(); ++i)
        want[i] = static_cast<uint8_t>(i * 7 + 3);
    fs::path p = write_tmp_file("ra2_fileio_equiv.bin", want);

    // The OLD engine slurp, inline: byte-at-a-time via istreambuf_iterator.
    std::ifstream f(p, std::ios::binary);
    REQUIRE(f.good());
    std::vector<uint8_t> old_way((std::istreambuf_iterator<char>(f)), {});
    f.close();  // Windows: fs::remove(p) below fails with a sharing violation
                // while this handle is open (ifstream lacks FILE_SHARE_DELETE).

    std::vector<uint8_t> new_way = read_file(p.string());
    CHECK(new_way == old_way);
    CHECK(new_way == want);
    fs::remove(p);
}

TEST_CASE("read_file: missing file throws runtime_error naming the path") {
    try {
        read_file("Z:\\no\\such\\file.bin");
        FAIL("read_file did not throw for a missing file");
    } catch (const std::runtime_error& ex) {
        // Message prefix is load-bearing: engine/main.cpp's old slurp threw
        // exactly "cannot open file: <path>" and README's Android logcat
        // diagnostics document that phrasing.
        CHECK(std::string(ex.what()).find("cannot open file") != std::string::npos);
        CHECK(std::string(ex.what()).find("file.bin") != std::string::npos);
    }
}

TEST_CASE("read_file: empty file returns an empty vector, no throw") {
    fs::path p = write_tmp_file("ra2_fileio_empty.bin", {});
    std::vector<uint8_t> got = read_file(p.string());
    CHECK(got.empty());
    fs::remove(p);
}
