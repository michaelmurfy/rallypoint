// W5-E2 (R-173): the BinkAsync worker produces the same pictures as decoding
// on the calling thread, in blocking (--play) mode and when the main thread
// skips ahead.
#include "doctest.h"
#include "ra2assets/bink.hpp"
#include "ra2assets/mix.hpp"
#include "asset_fixture.hpp"
#include "bink_async.hpp"
#include <cstdint>
#include <vector>

using namespace ra2assets;

TEST_CASE("bink async: worker frames match synchronous decode (ra2ts_s.bik)") {
    const std::vector<uint8_t>& lang = ra2fixture::mix_bytes_by_name("language.mix");
    if (lang.empty()) { MESSAGE("language.mix absent; skipped"); return; }
    const MixFile& mix = ra2fixture::mix_by_name("language.mix");
    const MixEntry* e = mix.find("ra2ts_s.bik");
    REQUIRE(e != nullptr);
    BinkFile bf = BinkFile::parse(mix.read(*e));
    BinkVideoDecoder ref(bf);
    std::vector<std::vector<uint8_t>> want_px;
    for (int f = 0; f <= 20; ++f) {
        ref.decode_frame(f);
        want_px.push_back(ref.rgba());
    }

    BinkVideoDecoder dec(bf);
    std::vector<uint8_t> px;
    {
        ra2engine::BinkAsync a(&dec, bf.num_frames);
        a.set_blocking(true);
        CHECK(a.take(-1, px) == -1);           // nothing yet
        a.want(0);
        REQUIRE(a.take(-1, px) == 0);
        CHECK(px == want_px[0]);
        for (int f = 1; f <= 5; ++f) a.want(f);   // one step at a time
        REQUIRE(a.take(0, px) == 5);
        CHECK(px == want_px[5]);
        a.want(20);                             // a skip-ahead decodes 6..20 in order
        REQUIRE(a.take(5, px) == 20);
        CHECK(px == want_px[20]);
        std::vector<uint8_t> untouched{1, 2, 3};
        CHECK(a.take(20, untouched) == 20);      // same frame: no copy
        CHECK(untouched.size() == 3);
        CHECK_FALSE(a.failed());
    }   // the destructor joins the worker
}
