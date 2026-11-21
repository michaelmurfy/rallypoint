// Skirmish page (dialog 0x102) pure model: engine/skirmish_lobby.{hpp,cpp}.
// Authority: docs/gap/07-skirmish-page.md §1.3-§1.5 and the game.exe
// decompiles it cites (0x683FC0, 0x6829F0, 0x66F1F0, 0x672BB0, 0x4D2080).
#include "doctest.h"
#include "skirmish_lobby.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

using namespace ra2engine;

namespace {
const std::vector<std::string> kCountries = {"Americans", "Alliance", "French", "Germans",
                                             "British", "Africans", "Arabs", "Confederation",
                                             "Russians"};
LobbyConvertCtx ctx(std::uint32_t seed = 7) {
    LobbyConvertCtx c;
    c.countries = kCountries;
    c.side_of = [](const std::string& s) {
        return (s == "Africans" || s == "Arabs" || s == "Confederation" || s == "Russians")
                   ? World::Side::Soviet : World::Side::Allied;
    };
    c.seed = seed;
    c.tech_level = 10;
    c.map_file = "xmp01t4.map";
    return c;
}
}  // namespace

TEST_CASE("lobby: slot type <-> AI combo value (0x683FC0 / 0x6829F0)") {
    CHECK(lobby_ai_to_slot_type(kLobbyAiNone) == 1);
    CHECK(lobby_ai_to_slot_type(kLobbyAiBrutal) == 4);
    CHECK(lobby_ai_to_slot_type(kLobbyAiMedium) == 5);
    CHECK(lobby_ai_to_slot_type(kLobbyAiEasy) == 6);
    for (int ai = -1; ai <= 2; ++ai) CHECK(lobby_slot_type_to_ai(lobby_ai_to_slot_type(ai)) == ai);
    CHECK(lobby_slot_type_to_ai(2) == kLobbyAiNone);   // human/network types -> None
}

TEST_CASE("lobby: per-row label -> AI difficulty (class = 2 - lobby choice, G-63)") {
    CHECK(lobby_ai_difficulty(kLobbyAiBrutal) == AiDifficulty::Difficult);
    CHECK(lobby_ai_difficulty(kLobbyAiMedium) == AiDifficulty::Normal);
    CHECK(lobby_ai_difficulty(kLobbyAiEasy) == AiDifficulty::Easy);
    // The DifficultyClass index the AI runs on equals the combo item data.
    for (int ai = 0; ai <= 2; ++ai)
        CHECK(difficulty_class_index(lobby_ai_difficulty(ai)) == ai);
}

TEST_CASE("lobby: colour translation lobby order -> clone [Colors] index (SK-11)") {
    const int want[8] = {0, 1, 5, 7, 2, 6, 4, 3};
    std::set<int> seen;
    for (int c = 0; c < 8; ++c) {
        CHECK(lobby_color_to_house(c) == want[c]);
        seen.insert(lobby_color_to_house(c));
    }
    CHECK(seen.size() == 8u);
    std::uint8_t r, g, b;
    lobby_color_rgb(1, r, g, b);
    CHECK((r == 255 && g == 25 && b == 25));
}

TEST_CASE("lobby: first run = rules defaults, Slot01 Easy, rest None") {
    LobbyRulesInfo ru;
    ru.money = 7000; ru.unit_count = 5; ru.game_speed = 2; ru.crates = false;
    const LobbyState st = lobby_first_run(ru);
    CHECK(st.credits == 7000);
    CHECK(st.unit_count == 5);
    CHECK(st.game_speed == 2);
    CHECK_FALSE(st.crates);
    CHECK(st.rows[1].ai == kLobbyAiEasy);
    CHECK(st.rows[1].country == kLobbyRandom);
    CHECK(st.rows[1].color == kLobbyRandom);
    for (int r = 2; r < kLobbyRows; ++r) CHECK(st.rows[r].ai == kLobbyAiNone);
    CHECK(lobby_validate(st, 4) == LobbyCheck::Ok);
}

TEST_CASE("lobby: colours are exclusive; None releases; choices omit owned colours") {
    LobbyState st = lobby_first_run({});
    st.rows[0].color = 0;
    lobby_set_ai(st, 2, kLobbyAiBrutal);
    CHECK(lobby_set_color(st, 1, 3));
    CHECK_FALSE(lobby_set_color(st, 2, 3));     // owned by row 1
    CHECK_FALSE(lobby_set_color(st, 2, 0));     // owned by the human
    CHECK(lobby_set_color(st, 2, 4));
    std::vector<int> ch = lobby_color_choices(st, 2);
    CHECK(ch.front() == kLobbyRandom);
    CHECK(std::find(ch.begin(), ch.end(), 3) == ch.end());
    CHECK(std::find(ch.begin(), ch.end(), 0) == ch.end());
    CHECK(std::find(ch.begin(), ch.end(), 4) != ch.end());   // its own colour
    CHECK(ch.size() == 1u + 6u);
    lobby_set_ai(st, 1, kLobbyAiNone);            // releases colour 3
    CHECK(st.rows[1].color == kLobbyRandom);
    CHECK(st.rows[1].country == kLobbyRandom);
    CHECK(lobby_color_owner(st, 3) == -1);
    CHECK(lobby_set_color(st, 2, 3));
    CHECK_FALSE(lobby_set_color(st, 1, 5));       // a None row owns nothing
}

TEST_CASE("lobby: map max hides/closes rows; validation messages (0x6829F0)") {
    LobbyState st = lobby_first_run({});
    for (int r = 1; r < kLobbyRows; ++r) lobby_set_ai(st, r, kLobbyAiMedium);
    CHECK(lobby_ai_count(st) == 7);
    CHECK(lobby_validate(st, 4) == LobbyCheck::ScenarioTooSmall);
    lobby_apply_max_players(st, 4);
    CHECK(lobby_ai_count(st) == 3);
    CHECK(st.rows[4].ai == kLobbyAiNone);
    CHECK(lobby_visible_rows(4) == 4);
    CHECK(lobby_visible_rows(12) == 8);
    CHECK(lobby_validate(st, 4) == LobbyCheck::Ok);
    for (int r = 1; r < kLobbyRows; ++r) lobby_set_ai(st, r, kLobbyAiNone);
    CHECK(lobby_validate(st, 4) == LobbyCheck::NeedTwoPlayers);
}

TEST_CASE("lobby: RA2.INI round trip with the original's key spellings") {
    LobbyState st = lobby_first_run({});
    st.name = "Tanya";
    st.rows[0].country = 1;     // Alliance (Korea)
    st.rows[0].color = 6;
    st.rows[1] = {kLobbyAiEasy, 4, 1};
    st.rows[2] = {kLobbyAiBrutal, kLobbyRandom, kLobbyRandom};
    st.game_speed = 1; st.credits = 10000; st.unit_count = 10;
    st.mcv_repacks = false; st.scen_index = 2; st.scen_file = "xmp01t4.map";
    IniDoc d = IniDoc::parse("[Options]\r\nGameSpeed=3\r\n");
    lobby_store(st, d, kCountries);
    const std::string t = d.serialize();
    CHECK(t.find("Handle=54,61,6e,79,61,") != std::string::npos);
    CHECK(t.find("Side=Alliance") != std::string::npos);
    CHECK(t.find("SideEx=-1") != std::string::npos);
    CHECK(t.find("Color=6") != std::string::npos);
    CHECK(t.find("Slot01=6,4,1") != std::string::npos);
    CHECK(t.find("Slot02=4,-2,-2") != std::string::npos);
    CHECK(t.find("Slot07=1,-2,-2") != std::string::npos);
    CHECK(t.find("MCVRepacks=no") != std::string::npos);
    CHECK(t.find("CratesAppear=yes") != std::string::npos);
    CHECK(t.find("[Options]\r\nGameSpeed=3") != std::string::npos);   // foreign section untouched
    const LobbyState back = lobby_load(IniDoc::parse(t), lobby_first_run({}), {}, kCountries);
    CHECK(back.name == "Tanya");
    CHECK(back.rows[0].country == 1);
    CHECK(back.rows[0].color == 6);
    CHECK(back.rows[1].ai == kLobbyAiEasy);
    CHECK(back.rows[1].country == 4);
    CHECK(back.rows[1].color == 1);
    CHECK(back.rows[2].ai == kLobbyAiBrutal);
    CHECK_FALSE(back.mcv_repacks);
    CHECK(back.scen_index == 2);
    CHECK(back.scen_file == "xmp01t4.map");
}

TEST_CASE("lobby: human Random stores Ex=-2; load sanitizes duplicates and ranges") {
    LobbyState st = lobby_first_run({});
    st.rows[0].country = kLobbyRandom;
    st.rows[0].color = kLobbyRandom;
    IniDoc d = IniDoc::parse("");
    lobby_store(st, d, kCountries);
    CHECK(*d.get("MultiPlayer", "SideEx") == "-2");
    CHECK(*d.get("MultiPlayer", "ColorEx") == "-2");
    const LobbyState back = lobby_load(d, lobby_first_run({}), {}, kCountries);
    CHECK(back.rows[0].country == kLobbyRandom);
    CHECK(back.rows[0].color == kLobbyRandom);
    IniDoc bad = IniDoc::parse(
        "[MultiPlayer]\nColor=2\nColorEx=-1\n[Skirmish]\nSlot01=4,99,2\nSlot02=5,3,2\n"
        "Slot03=1,3,5\nCredits=99999\nGameSpeed=9\n");
    const LobbyState b = lobby_load(bad, lobby_first_run({}), {}, kCountries);
    CHECK(b.rows[1].country == kLobbyRandom);   // 99 out of range
    CHECK(b.rows[1].color == kLobbyRandom);     // 2 owned by the human
    CHECK(b.rows[2].color == kLobbyRandom);
    CHECK(b.rows[3].ai == kLobbyAiNone);
    CHECK(b.rows[3].color == kLobbyRandom);     // None rows hold -2
    CHECK(b.credits == 10000);
    CHECK(b.game_speed == 6);
}

TEST_CASE("lobby: converter builds the SkirmishConfig the launcher consumes") {
    LobbyState st = lobby_first_run({});
    st.rows[0] = {kLobbyAiNone, 8, 0};                 // human: Russia, Gold
    st.rows[1] = {kLobbyAiNone, kLobbyRandom, kLobbyRandom};
    st.rows[2] = {kLobbyAiBrutal, 6, 1};               // Brutal Iraq, Red
    st.rows[3] = {kLobbyAiEasy, kLobbyRandom, kLobbyRandom};
    st.unit_count = 5; st.short_game = false; st.credits = 7500; st.game_speed = 0;
    st.mcv_repacks = false; st.superweapons = false; st.crates = true;
    const SkirmishConfig cfg = lobby_to_config(st, ctx());
    REQUIRE(cfg.slots.size() == 4u);                   // human, row2, row3, neutral
    CHECK(cfg.slots[0].country == "Russians");
    CHECK(cfg.slots[0].side == World::Side::Soviet);
    CHECK_FALSE(cfg.slots[0].is_ai);
    CHECK(cfg.slots[0].color_ix == 0);
    CHECK(cfg.slots[1].country == "Arabs");
    CHECK(cfg.slots[1].side == World::Side::Soviet);
    CHECK(cfg.slots[1].is_ai);
    CHECK(cfg.slots[1].diff == AiDifficulty::Difficult);
    CHECK(cfg.slots[1].color_ix == 1);                 // Red -> DarkRed
    CHECK(cfg.slots[2].diff == AiDifficulty::Easy);
    CHECK(cfg.slots[2].color_ix != 0);
    CHECK(cfg.slots[2].color_ix != 1);
    CHECK_FALSE(cfg.slots[3].combatant);
    for (const SkirmishSlot& s : cfg.slots) CHECK(s.alliance_team == -1);
    CHECK(cfg.unit_count == 5);
    CHECK_FALSE(cfg.short_game);
    CHECK(cfg.starting_cash == 7500.0);
    CHECK(cfg.game_speed == 0);
    CHECK(cfg.mcv_redeploys == 0);
    CHECK_FALSE(cfg.superweapons);
    CHECK(cfg.crates);
    CHECK(cfg.tech_level == 10);
    CHECK(cfg.map_file == "xmp01t4.map");
    CHECK(cfg.use_map_starts);
}

TEST_CASE("lobby: Random resolution never duplicates a colour; deterministic per seed") {
    LobbyState st = lobby_first_run({});
    st.rows[0].color = kLobbyRandom;
    st.rows[0].country = kLobbyRandom;
    for (int r = 1; r < kLobbyRows; ++r) lobby_set_ai(st, r, kLobbyAiMedium);
    lobby_set_color(st, 5, 2);
    for (std::uint32_t seed = 1; seed < 40; ++seed) {
        const std::vector<LobbyResolved> z = lobby_resolve(st, ctx(seed));
        REQUIRE(z.size() == 8u);
        std::set<int> cols;
        for (const LobbyResolved& x : z) {
            cols.insert(x.color);
            CHECK(x.country >= 0);
            CHECK(x.country < 9);
        }
        CHECK(cols.size() == 8u);
        CHECK(z[5].color == 2);
    }
    const auto a = lobby_resolve(st, ctx(11)), b = lobby_resolve(st, ctx(11));
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].color == b[i].color);
        CHECK(a[i].country == b[i].country);
    }
}
