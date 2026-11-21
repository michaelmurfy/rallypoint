#include "doctest.h"
#include "ra2assets/ufnt.hpp"
#include "ra2assets/mix.hpp"
#include "asset_fixture.hpp"

using namespace ra2assets;

// W2-UI-12: the real local.mix/game.fnt ("fonT" Unicode face) decodes; the
// glyph index is 1-based (0 = absent) -- 'A' has a visible apex on row 4.
TEST_CASE("ufnt: real game.fnt decodes") {
    if (!ra2fixture::ra2_available()) { MESSAGE("ra2.mix absent; skipped"); return; }
    const MixFile& top = ra2fixture::mix_by_name("ra2.mix");
    MixFile local = top.open_nested("local.mix");
    const MixEntry* e = local.find("game.fnt");
    REQUIRE(e != nullptr);
    UFont f = UFont::load(local.read(*e));
    CHECK(f.rows() == 16);
    CHECK(f.line_height() == 17);
    CHECK(f.symbol_count() > 1000);
    CHECK(f.width('A') == 7);
    CHECK(f.width(' ') == 4);
    CHECK(f.pixel('A', 3, 4));           // apex "..###.."
    CHECK_FALSE(f.pixel('A', 0, 4));
    CHECK(f.pixel('A', 0, 11));          // left foot "##...##"
    int lit = 0;
    for (int y = 0; y < f.rows(); ++y)
        for (int x = 0; x < f.width(' '); ++x) lit += f.pixel(' ', x, y);
    CHECK(lit == 0);
    CHECK(f.text_width("AA") == 14);
    CHECK(f.text_width("A\nAAA") == 21);
}

TEST_CASE("ufnt: rejects a non-fonT blob") {
    std::vector<uint8_t> junk(200000, 0);
    CHECK_THROWS(UFont::load(junk));
}
