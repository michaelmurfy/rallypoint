#include "doctest.h"
#include "skirmish.hpp"
#include "world.hpp"
#include "ra2assets/rules.hpp"
#include "ra2assets/ini.hpp"
#include "ra2assets/map.hpp"

// NOTE (deviation from brief): the brief calls a `make_test_world()` helper, but
// no such helper exists in-tree and MapFile cannot inject cells. Everything
// configure_skirmish_houses touches (house table + toggles + credits + AI-enable)
// is map-independent (pure HouseRec table + firewall flags), exactly like the
// multihouse Task 2/3/4 cases in test_world.cpp, so these build a World over a
// default-constructed MapFile -> host-only, no RA2_INSTALL_DIR needed.

TEST_CASE("skirmish: configure_skirmish_houses sets table + toggles + credits + AI [RE: skirmish.md §3]") {
    ra2engine::SkirmishConfig cfg;
    cfg.starting_cash = 8000.0;
    cfg.short_game = true;
    cfg.shroud = true;
    cfg.slots = {
        {ra2engine::World::Side::Allied,  0, "Americans", 0, true,  false, ra2engine::AiDifficulty::Normal},
        {ra2engine::World::Side::Soviet,  1, "Russians",  1, true,  true,  ra2engine::AiDifficulty::Difficult},
        {ra2engine::World::Side::Allied,  2, "British",   0, true,  false, ra2engine::AiDifficulty::Normal},
        {ra2engine::World::Side::Neutral, 3, "Civilian",  3, false, false, ra2engine::AiDifficulty::Normal},
    };
    ra2assets::MapFile map;
    ra2engine::World world(map);
    ra2engine::configure_skirmish_houses(world, cfg);

    CHECK(world.house_count() == 4);            // 3 combatant + 1 neutral
    CHECK(world.houses_configured());
    // Teams: 0+2 allied on team 0 vs 1 on team 1; neutral 3 own team.
    CHECK(world.house_team(0) == 0);
    CHECK(world.house_team(2) == 0);
    CHECK(world.are_allied(0, 2));
    CHECK(world.is_enemy(0, 1));
    CHECK_FALSE(world.is_enemy(0, 3));          // neutral never an enemy
    CHECK_FALSE(world.house_combatant(3));
    // AI enabled only for the AI slot.
    CHECK(world.ai_enabled(1));
    CHECK_FALSE(world.ai_enabled(0));
    CHECK_FALSE(world.ai_enabled(3));
    // Per-combatant credit seed; neutral not seeded.
    CHECK(world.house_credits(0) == doctest::Approx(8000.0));
    CHECK(world.house_credits(1) == doctest::Approx(8000.0));
    CHECK(world.house_credits(2) == doctest::Approx(8000.0));
    CHECK(world.house_credits(3) == doctest::Approx(0.0));
}

TEST_CASE("skirmish: default_skirmish_config reproduces the 1v1 [RE: skirmish.md §3, D1]") {
    ra2assets::Ini ini = ra2assets::Ini::parse("[MultiplayerDialogSettings]\nMoney=10000\n");
    ra2assets::Rules rules = ra2assets::Rules::load(ini);
    ra2engine::SkirmishConfig cfg = ra2engine::default_skirmish_config(rules);

    CHECK(cfg.starting_cash == doctest::Approx(10000.0));
    REQUIRE(cfg.slots.size() == 3);
    CHECK(cfg.slots[0].side == ra2engine::World::Side::Allied);
    CHECK_FALSE(cfg.slots[0].is_ai);            // slot 0 = human
    CHECK(cfg.slots[1].side == ra2engine::World::Side::Soviet);
    CHECK(cfg.slots[1].is_ai);                  // slot 1 = AI
    CHECK(cfg.slots[1].diff == ra2engine::AiDifficulty::Normal);
    CHECK_FALSE(cfg.slots[2].combatant);        // slot 2 = Neutral
}

// ---------------------------------------------------------------------------
// Task 5: rect-font (pure engine/rectfont) glyph-coverage cases. Kept on the
// ra2assets_tests / test_skirmish.cpp line; engine/rectfont.cpp is SDL-free.
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include "rectfont.hpp"

TEST_CASE("rectfont: A-Z + space + unknown glyph coverage [RE: skirmish.md §UI]") {
    // 'A' has a non-empty bitmap.
    bool any = false;
    for (std::uint8_t rowbits : ra2engine::glyph_rows('A')) any |= (rowbits != 0);
    CHECK(any);
    // Lowercase folds to uppercase.
    CHECK(ra2engine::glyph_rows('a') == ra2engine::glyph_rows('A'));
    // Space + unknown -> blank (all zero rows).
    for (std::uint8_t rowbits : ra2engine::glyph_rows(' ')) CHECK(rowbits == 0);
    for (std::uint8_t rowbits : ra2engine::glyph_rows('#')) CHECK(rowbits == 0);
    // Every A-Z is non-blank (catches an un-authored letter).
    for (char c = 'A'; c <= 'Z'; ++c) {
        bool nonblank = false;
        for (std::uint8_t rowbits : ra2engine::glyph_rows(c)) nonblank |= (rowbits != 0);
        CHECK_MESSAGE(nonblank, "glyph not authored: ", c);
    }
    // Every 0-9 is non-blank (digit shapes reused from kDigits).
    for (char c = '0'; c <= '9'; ++c) {
        bool nonblank = false;
        for (std::uint8_t rowbits : ra2engine::glyph_rows(c)) nonblank |= (rowbits != 0);
        CHECK_MESSAGE(nonblank, "digit not authored: ", c);
    }
    // Basic punctuation authored: ':' '-' '.' (+ W2-AR-03 path/message marks).
    for (char c : {':', '-', '.', '/', '\\', '_', '(', ')', ',', '\'', '!', '?', '+', '%', '[', ']', '<', '>', '='}) {
        bool nonblank = false;
        for (std::uint8_t rowbits : ra2engine::glyph_rows(c)) nonblank |= (rowbits != 0);
        CHECK_MESSAGE(nonblank, "punctuation not authored: ", c);
    }
    // Only the low 5 bits are ever used (bit 4 = leftmost column).
    for (char c = 'A'; c <= 'Z'; ++c)
        for (std::uint8_t rowbits : ra2engine::glyph_rows(c))
            CHECK((rowbits & 0xE0) == 0);
}

TEST_CASE("rectfont: string->rects set-pixel geometry for a sample [RE: skirmish.md §UI]") {
    // Reproduce draw_text's per-glyph advance (6*scale) and set-pixel geometry
    // without SDL: collect the top-left of each set pixel for "A-1".
    const std::string sample = "A-1";
    const int scale = 2;
    std::vector<std::pair<int,int>> got;
    int cx = 0;
    for (char ch : sample) {
        const std::array<std::uint8_t, 7>& rows = ra2engine::glyph_rows(ch);
        for (int row = 0; row < 7; ++row)
            for (int col = 0; col < 5; ++col)
                if (rows[row] & (1 << (4 - col)))
                    got.emplace_back(cx + col * scale, row * scale);
        cx += 6 * scale;
    }
    // 'A' row 0 = 0b01110 -> cols 1,2,3 set at cx=0: x = 2,4,6, y=0.
    CHECK(std::find(got.begin(), got.end(), std::make_pair(2, 0)) != got.end());
    CHECK(std::find(got.begin(), got.end(), std::make_pair(4, 0)) != got.end());
    CHECK(std::find(got.begin(), got.end(), std::make_pair(6, 0)) != got.end());
    // col 0 of 'A' row 0 is clear -> x=0,y=0 absent.
    CHECK(std::find(got.begin(), got.end(), std::make_pair(0, 0)) == got.end());
    // '-' is the 2nd glyph (cx=12): row 3 = 0b11111 -> col 0 set -> x=12, y=6.
    CHECK(std::find(got.begin(), got.end(), std::make_pair(12, 6)) != got.end());
    // '-' row 0 is empty -> nothing from that glyph at y=0 in its column span.
    CHECK(std::find(got.begin(), got.end(), std::make_pair(12, 0)) == got.end());
}
