// Net L0 determinism foundations: the session seed (one value seeds every sim
// RNG stream, recorded in saves).
#include "doctest.h"

#include <cstdint>
#include <vector>

#include "asset_fixture.hpp"
#include "savegame.hpp"
#include "crate.hpp"
#include "world.hpp"

using namespace ra2engine;

TEST_CASE("netL0 session seed: 0 keeps the fixed seeds, a seed derives every stream, saves keep it") {
    if (!ra2fixture::mapsmd03_available()) {
        MESSAGE("skipped: real map assets not found under RA2_INSTALL_DIR");
        return;
    }
    const ra2assets::MapFile& map = ra2fixture::mapsmd03_map();
    World plain(map);
    CHECK(plain.session_seed() == 0u);
    CHECK(plain.session_stream_seed(0x2545F491u) == 0x2545F491u);   // gates: historical seeds

    World a(map), b(map), c(map);
    a.set_session_seed(0x1234u);
    b.set_session_seed(0x1234u);
    c.set_session_seed(0x1235u);
    CHECK(a.session_stream_seed(0x2545F491u) != 0x2545F491u);
    CHECK(a.session_stream_seed(0x2545F491u) == b.session_stream_seed(0x2545F491u));
    CHECK(a.session_stream_seed(0x2545F491u) != c.session_stream_seed(0x2545F491u));
    CHECK(a.session_stream_seed(7u, 1u) != a.session_stream_seed(7u, 2u));   // per-house salt
    // Scenario->Random is the session seed itself (simdiff L0 generator).
    const GameRandom fresh(0x1234u);
    bool same_table = true;
    for (int i = 0; i < 250; ++i) same_table = same_table && a.crate_rng().table_word(i) == fresh.table_word(i);
    CHECK(same_table);
    CHECK(plain.crate_rng().table_word(0) == GameRandom(0).table_word(0));
    CHECK(save::save_world(a) == save::save_world(b));
    CHECK(save::save_world(a) != save::save_world(c));
    CHECK(save::save_world(a) != save::save_world(plain));

    World d(map);
    REQUIRE(save::load_world(d, save::save_world(a)));
    CHECK(d.session_seed() == 0x1234u);
    CHECK(save::save_world(d) == save::save_world(a));
}

#include "pathfind.hpp"

// N4-11: the path memo is keyed by content. Changing the terrain grid IN PLACE
// (same vector address, same version -- what a resync load_world does) must
// not serve the path remembered for the old terrain.
TEST_CASE("netL0 path memo: in-place terrain change at the same address is never served stale") {
    const int w = 6, h = 1;
    std::vector<std::uint8_t> pass(static_cast<std::size_t>(w * h), 1);
    std::vector<std::int8_t> level(static_cast<std::size_t>(w * h), 0);
    PathTerrain t;
    t.level = &level;
    t.version = 1;
    const std::vector<Cellxy> open = find_path_t(pass, w, h, Cellxy{0, 0}, Cellxy{5, 0}, nullptr, &t);
    REQUIRE(!open.empty());
    // A cliff wall in the middle, same vector object, same version counter.
    level[3] = 8;
    level[4] = 8;
    level[5] = 8;
    const std::vector<Cellxy> fresh_ref = [&] {
        std::vector<std::int8_t> copy = level;   // a different address: never memoised before
        PathTerrain t2 = t;
        t2.level = &copy;
        return find_path_t(pass, w, h, Cellxy{0, 0}, Cellxy{5, 0}, nullptr, &t2);
    }();
    const std::vector<Cellxy> again = find_path_t(pass, w, h, Cellxy{0, 0}, Cellxy{5, 0}, nullptr, &t);
    CHECK(again.size() == fresh_ref.size());
    CHECK(again.size() != open.size());   // the cliff cuts the old straight path
}
