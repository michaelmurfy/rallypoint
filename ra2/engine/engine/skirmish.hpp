#pragma once
#include <string>
#include <vector>
#include "world.hpp"     // ra2engine::World (World::Side)
#include "ai_base.hpp"   // ra2engine::AiDifficulty
namespace ra2assets { class Rules; }

namespace ra2engine {

// One lobby slot [RE: skirmish.md §3, D1]. side/color_ix/country/alliance_team/
// combatant map 1:1 into HouseRec; is_ai + diff drive enable_ai_house. NOT hashed.
struct SkirmishSlot {
    World::Side  side          = World::Side::Allied;
    int          color_ix      = 0;      // 0..7 into the 8 MP colors
    std::string  country       = "";
    int          alliance_team = -1;     // -1 sentinel -> own id (FFA)
    bool         combatant     = true;   // false for Neutral/Special
    bool         is_ai         = false;
    AiDifficulty diff          = AiDifficulty::Normal;  // meaningful only when is_ai
};

// The single input to the launcher [RE: skirmish.md §1, D1]. Defaults are the
// shipped [MultiplayerDialogSettings] values. GameSpeed is PRESENTATION-only and
// is NOT stored here (it cannot affect the deterministic dt=1/15 step / world_hash).
struct SkirmishConfig {
    std::string               map_id        = "mapsmd03";  // legacy label; see map_file below

    // [RE: maps.md §1/§2] Which multiplayer map to load, as a multimd.mix
    // entry name (e.g. "xmp31s2.map"). EMPTY = the engine's historical default
    // map (mapsmd03.mix / 0x133a0cb2 = the CAMPAIGN mission sov07tmd.map,
    // maps.md §1) -- which is what every byte-identity gate loads. Never
    // repoint the default (maps.md §7 rule 2).
    std::string               map_file      = "";

    // [RE: maps.md §4.3 + §9.5] Place each combatant house at the map's real
    // [Waypoints] 0-7 start (combatant k -> start_waypoints()[k], ascending).
    // FALSE (the default) keeps launch_skirmish's historical pick_start_cell +
    // i*14 fan-out, so --demo-skirmish stays byte-identical (maps.md §7 rule 3).
    // [RE-FALLBACK: the original randomizes the lobby-slot -> start-location
    // assignment; a byte-identity gate needs determinism, so we bind ascending.]
    bool                      use_map_starts = false;
    std::vector<SkirmishSlot> slots;                       // index 0 = the human player
    double                    starting_cash = 10000.0;     // slider 5000-10000
    int                       unit_count    = 10;          // 0-10 (MCV extra)
    int                       tech_level    = 10;          // 1-10
    bool                      shroud        = true;
    bool                      crates        = false;
    bool                      superweapons  = true;
    bool                      short_game    = true;
    // Skirmish page (dialog 0x102) options the game consumes. -1 = not set
    // by a lobby (gates, quick start, loaded saves): GameScene then uses
    // [Options] GameSpeed and the rules MCVRedeploys, exactly as before.
    // NOT hashed and NOT saved (a loaded game reads [Options] GameSpeed,
    // which the page also writes, 0x6829F0: OptionsClass+0 = GameSpeed).
    int                       game_speed    = -1;      // 0 fastest .. 6 slowest
    // Net L0: the session seed (the original's Seed, DAT_00A8DCFC), chosen ONCE
    // by the launcher -- the lobby host's Start click, a campaign mission's
    // name -- and handed to World::set_session_seed; every sim RNG stream
    // derives from it. 0 = none chosen (gates, quick start): GameScene derives
    // a deterministic one from the map. Never the wall clock inside the sim.
    std::uint32_t             session_seed  = 0;
    int                       mcv_redeploys = -1;      // MCV Repacks: 1 / 0
    // W9-03-04: a multiplayer game type's rules override INI (MPModesMD.ini,
    // e.g. "MPMWMD.ini" for Megawealth), overlaid on rulesmd.ini by the
    // GameScene. Empty (every skirmish, campaign and gate) = plain rulesmd.ini.
    std::string               mode_rules;
    // W9-05-09: the lobby's BuildOffAlly (FUN_004A8DC0 reads it as a session
    // global); false outside a multiplayer lobby (gates, quick start).
    bool                      build_off_ally = false;
    // W9-05-03: the multiplayer game mode (mpmodesmd.ini mode id: 1 Battle,
    // 2 Free For All, 3 Cooperative, 4 Unholy Alliance, 5-8 Megawealth / Duel
    // / Meat Grinder / Naval War, 9 Team Game); its rules override decides
    // AlliesAllowed / AllyChangeAllowed. 0 = not a multiplayer lobby.
    int                       mp_mode = 0;

    // P10 Campaign [RE: campaign.md §3]: a NON-EMPTY campaign_map turns the
    // GameScene into that campaign mission (map from Maps01/MAPS02, houses +
    // objects + triggers from the map); the skirmish fields above are then
    // unused. Empty (the default) = every existing skirmish path, unchanged.
    std::string               campaign_map;            // e.g. "ALL01t.MAP"
    std::string               campaign_side;           // mapsel side: "GDI" / "Nod"
    int                       campaign_difficulty = 1; // 0 Easy, 1 Normal, 2 Hard
};

// "Quick start" config reproducing the current run_interactive 1v1 [D1]:
// slot0 Allied human, slot1 Soviet AI Normal, slot2 Neutral non-combatant;
// starting_cash = rules.mp_money(). (T1 item 4 may add a Special slot; the
// default here stays the minimal faithful 1v1 that build_world's 3-house world
// already reproduces.)
SkirmishConfig default_skirmish_config(const ra2assets::Rules& rules);

// PURE asset-free core [D2]: apply the config's house table + toggles + per-house
// credit seed to `world`. For each slot -> set_house(i, HouseRec{...}); then
// enable_shroud(cfg.shroud) / enable_ally_reveal(true) / enable_victory(true,
// cfg.short_game); seed grant_credits_for_test(i, cfg.starting_cash) for each
// combatant; enable_ai_house(i, AiHouseConfig{.difficulty=slot.diff}) for each AI
// slot (a MINIMAL config -> ai_enabled(i)==true; launch_skirmish OVER-WRITES it
// with the full roster-backed make_ai_config before the sim runs). Never spawns
// units/bases (that needs assets -> launch_skirmish). Host-testable on a hand-
// built World; NOT read by world_hash.
// W6-a: the Paranoid computer alliance of a plain skirmish (skirmish.cpp).
void apply_computer_alliance(World& world, const SkirmishConfig& cfg);
void configure_skirmish_houses(World& world, const SkirmishConfig& cfg);

}  // namespace ra2engine
