#pragma once
// P10 Campaign missions [RE: docs/re/campaign.md].
//
// Pure C++17 (no SDL, no World): the campaign control files (battle.ini /
// mission.ini / mapsel.ini), the scenario [Basic] block, the map object
// records ([Structures]/[Units]/[Infantry]/[Aircraft]), the BorrowedTime
// mission-end state machine and the interpreter for a map's OWN team scripts
// ([ScriptTypes], RE §7 numbering). Everything world-facing is done by the
// caller (CampaignRuntime in main.cpp) -- this file is unit-testable alone.
//
// FIREWALL: only the campaign GameScene and the --demo-campaign gate use it.
#include <cstdint>
#include <string>
#include <vector>
#include "ra2assets/ini.hpp"
#include "script_actions.hpp"  // the shared 0..64 ScriptType action table [G-61]

namespace ra2engine {

// ---------------------------------------------------------------------------
// Control files [RE §1/§3].
struct BattleEntry {
    std::string id;            // [Battles] value (section name), e.g. "ALL1"
    std::string scenario;      // Scenario= (first map), e.g. "ALL01t.MAP"
    std::string description;   // Description= CSF label
    std::string final_movie;   // FinalMovie=
    int cd = -1;               // CD=
    bool debug_only = false;   // DebugOnly=yes
};

struct MissionInfo {
    bool found = false;
    std::string briefing;          // Briefing= CSF label
    std::string ui_name;           // UIName= CSF label
    std::string ls_load_message;   // LSLoadMessage= CSF label
    std::string ls_load_briefing;  // LSLoadBriefing= CSF label
    std::string ls640_bkgd, ls800_bkgd;   // LS640/800BkgdName= SHP
    int ls800_brief_x = 20, ls800_brief_y = 20;   // LS800BriefLocX/Y
};

struct MapSelStage {
    std::string name;                 // section name, e.g. "ALL01"
    std::string scenario;             // Scenario=
    std::vector<std::string> next;    // numbered keys in file order (next stages)
};

class CampaignTables {
public:
    // Any of the three may be an empty Ini (the lookups then fail soft).
    static CampaignTables parse(const ra2assets::Ini& battle, const ra2assets::Ini& mission,
                                const ra2assets::Ini& mapsel);

    const std::vector<BattleEntry>& battles() const { return battles_; }
    const BattleEntry* battle(const std::string& id) const;   // case-insensitive
    MissionInfo mission(const std::string& map_name) const;   // [<MAPNAME>] section
    // Stage list of a side section ([GDI] / [Nod]), file order.
    std::vector<std::string> side_stages(const std::string& side) const;
    const MapSelStage* stage(const std::string& name) const;   // case-insensitive
    // RE §3.5: the stage of `side` whose Scenario= matches `current_map`
    // (case-insensitive), then its first next stage that has a non-empty
    // Scenario=. "" when there is none (campaign over / not a campaign map).
    std::string next_scenario(const std::string& side, const std::string& current_map) const;

private:
    std::vector<BattleEntry> battles_;
    ra2assets::Ini mission_ = ra2assets::Ini::parse("");
    ra2assets::Ini mapsel_ = ra2assets::Ini::parse("");
    std::vector<MapSelStage> stages_;
};

// ---------------------------------------------------------------------------
// Scenario [Basic] [RE §2 FUN_00689890].
struct ScenarioBasic {
    std::string player;              // Player= (default "Americans")
    std::string briefing;            // Briefing= CSF label
    std::string intro, brief, win, lose, action, post_score, pre_map_select;   // movies ("" = <none>)
    std::string theme;               // Theme= ("" = No theme)
    std::string next_scenario;       // NextScenario= (TS leftover; recorded only, RE §3.5)
    bool end_of_game = false;
    bool skip_score = false;
    bool one_time_only = false;
    bool skip_map_select = false;
    double carry_over_money = 0.0;
    // W4-MC-03 [RE: gamemd ScenarioClass::Read FUN_00689890 reads [Basic]
    // HomeCell -> Scenario+0x20c, AltHomeCell -> +0x210; the map load
    // FUN_004acd80 centres the tactical view (FUN_006d55e0) on
    // Waypoint[HomeCell] (AltHomeCell when the alt flag +0x1cb0 is set; a
    // missing waypoint is first set to the map centre).]
    int home_cell = 98;
    int alt_home_cell = 99;
};
ScenarioBasic parse_scenario_basic(const ra2assets::Ini& map);

// W4-MC-04 [RE: gamemd MapClass::Read FUN_004acd80 stores [Map] Size at
// Map+0xEC and LocalSize at Map+0xFC; IsWithinUsableArea FUN_00577e90]:
// the playable rectangle. Cells outside it are the off-board staging strip.
struct MapLocalSize {
    bool valid = false;
    int size_w = 0, size_h = 0;          // [Map] Size= w,h (x,y are 0)
    int x = 0, y = 0, w = 0, h = 0;      // [Map] LocalSize=
};
MapLocalSize parse_map_local_size(const ra2assets::Ini& map);
// FUN_00577e90 (CONFIRMED, decompiled): with W = Size.w, s = cx+cy, d = cx-cy
//   W + 2*L.y + lvl < s <= W + 2 + 2*(L.y+L.h) + lvl,
//   2*L.x - W < d < 2*(L.x+L.w) - W.
// `level` is the cell height when the caller checks levels (0 otherwise).
bool within_usable_area(const MapLocalSize& m, int cx, int cy, int level = 0);

// ---------------------------------------------------------------------------
// Pre-placed objects [RE §4]. kind: 0 structure, 1 vehicle unit, 2 infantry,
// 3 aircraft.
struct MapObjectRec {
    int kind = 0;
    std::string key;         // INI key (file order is kept)
    std::string owner;       // [Houses] name
    std::string type;        // TechnoType id
    int health = 256;        // 256ths
    int x = 0, y = 0;
    int facing = 0;
    int subcell = 0;         // infantry only
    std::string mission;     // units/infantry/aircraft
    std::string tag;         // "" when None
    int veterancy = 0;
    int group = -1;
    // fv7c [RE: [Structures] reader FUN_0044F810]: AISellable (field 8),
    // AIRebuildable (9), AIRepairable (16) -- structures only.
    bool ai_sellable = false, ai_rebuildable = false, ai_repairable = false;
};
// Field order per section [RE §4]; malformed lines (too few fields) are
// skipped. Output order: [Structures], [Units], [Infantry], [Aircraft],
// each in file order.
std::vector<MapObjectRec> parse_map_objects(const ra2assets::Ini& map);

// RE §4: ftol(strength * health / 256); > strength-3 snaps to strength; floor 1.
// Buildings clamp health to 256 first.
int campaign_strength(int type_strength, int health256, bool building);

// [Houses] order with the player house first (engine house 0); every other
// house keeps its [Houses] order. `player` matched case-insensitively against
// the house names, then against each house's Country=.
std::vector<std::string> campaign_house_order(const ra2assets::Ini& map, const std::string& player);

// ---------------------------------------------------------------------------
// Mission end [RE §5]: the first Win/Lose flags the outcome; the mission ends
// once the BorrowedTime (SavourDelay * 900 frames) has run out.
class MissionEnd {
public:
    explicit MissionEnd(int borrowed_frames = 90) : borrowed_(borrowed_frames) {}
    // outcome 1 = won, 2 = lost. Ignored once flagged (the +0x1F7/+0x1F8 guards).
    void flag(int outcome, long frame);
    // True from the frame the BorrowedTime has fully elapsed.
    bool ended(long frame) const {
        return outcome_ != 0 && frame - flagged_at_ >= borrowed_;
    }
    int outcome() const { return outcome_; }
    long flagged_at() const { return flagged_at_; }
    int borrowed_frames() const { return borrowed_; }

private:
    int borrowed_;
    int outcome_ = 0;
    long flagged_at_ = -1;
};
// round(SavourDelay minutes * 900) [RE §5].
int borrowed_time_frames(double savour_delay_minutes);

// ---------------------------------------------------------------------------
// Map team scripts [RE §7].
struct MapScriptAction { int action = 0; int arg = 0; };
struct MapScript { std::string id, name; std::vector<MapScriptAction> actions; };
struct MapTaskMember { int count = 0; std::string type; };
struct MapTeamType {
    std::string id, name;
    std::string house;        // House= (a country name, e.g. "BadGuy2")
    std::string script;       // Script= id
    std::string taskforce;    // TaskForce= id
    std::string tag;          // Tag= id ("" none)
    int waypoint = -1;        // Waypoint= letter code
    int max = 0;
    bool full = false;
    bool recruitable = false;   // AreTeamMembersRecruitable= (W2-CA-09)
    int veteran_level = 0;
    int priority = 0;           // W5-A: Priority= (+0xB4), the recruit rule
    bool reinforce = false;     // W6-02-16: Reinforce= (+0xAB) -- refill under strength
    // W4-SQ-01/06 [RE: TeamType writer FUN_006f0ab0 -- +0xB0 Droppod, +0xB1
    // UseTransportOrigin, +0xF4 TransportsReturnOnUnload; TransportWaypoint=].
    bool droppod = false;
    bool use_transport_origin = false;
    bool transports_return = false;
    int transport_waypoint = -1;
    // fv7c (13b 1b) [RE: TeamType +0xAD Aggressive, +0xAF Suicide, +0xF6
    // IsBaseDefense -- read through Team +0x5D4 for EVERY house, campaign
    // included]: published to the World each frame (World::set_campaign_team_flags).
    bool aggressive = false;
    bool suicide = false;
    bool is_base_defense = false;
};
struct MapTeamData {
    std::vector<MapScript> scripts;
    std::vector<MapTeamType> teams;
    std::vector<std::pair<std::string, std::vector<MapTaskMember>>> taskforces;
    int find_team(const std::string& id) const;
    int find_script(const std::string& id) const;
    const std::vector<MapTaskMember>* taskforce(const std::string& id) const;
};
MapTeamData parse_map_teams(const ra2assets::Ini& map);

// Script action numbers + mission numbers: the ONE shared table [G-61/G-76].
int mission_from_name(const std::string& name);   // "Guard" -> 5, "Hunt" -> 15 ...

// What the caller tells the interpreter each tick.
struct MapTeamView {
    int live_members = 0;
    bool arrived = false;          // every live member within CloseEnough of the move goal
    bool waypoint_ok = true;       // the current action's waypoint exists
    bool target_exists = true;     // an attack target for the current action exists
    bool members_idle = false;     // all live members idle (attack finished)
    // W4-SQ-01 [RE: FUN_006ee670 / FUN_006ec760 / case 43 @0x6E9192]:
    bool unloaded = true;          // every member transport's cargo is empty
    bool loaded = true;            // Load (14): no member is left on the map to board
    bool full = true;              // WaitLoaded (43): every member transport is full
    // W5-A 55 IronCurtainMe [RE: FUN_006EF1D0]: 0 the house holds no ready-able
    // Iron Curtain (line completes), 1 one is ready (fire at the team, line
    // completes), 2 it is charging past AIMinorSuperReadyPercent (wait).
    int iron_state = 0;
};
// The single command for this tick.
struct MapTeamCommand {
    enum class Kind {
        None, MoveWaypoint, MoveCell, Attack, AttackWaypoint, Guard, Win, Lose, SetGlobal,
        ClearGlobal, SetLocal, ClearLocal, ChangeHouse, Scatter, DeleteMembers, Mission,
        AttackBuilding, Skip, Done,
        Deploy, Unload,  // W2-CA-17
        UnloadDone,      // W4-SQ-01: the transports are empty -> team membership per arg mode
        Load,            // W4-SQ-01: script 14, members board the team's transport
        // W5-A R-027 [RE: the 0x6E94E4 handlers]:
        ForceFacing,     // 42 FUN_006ED200: every member turns to (arg & 7) << 13
        IronCurtain,     // 55 FUN_006EF1D0: the house's Iron Curtain on the team
        Panic,           // 19: every member vtbl +0x518 (InfantryClass::Panic)
        Unpanic,         // 41: every member vtbl +0x51C
        Follow,          // 10 FUN_006ED0B0: never completes
        Flash,           // 50: every member's flash counter (+0xF0) = arg
        ChangeTeam       // 18: the members move into a new team of TeamType[arg]
    };
    Kind kind = Kind::None;
    int arg = 0;
    int action = -1;
    bool fresh = false;   // first tick of this action (issue orders); later ticks re-target idle members
};
struct MapTeamState {
    int pc = 0;
    bool issued = false;     // the current action's order went out
    long wait_until = -1;    // guard timer end frame
    bool done = false;
};
// Guard area: arg x 15 frames [RE: c14.log idx 5; script_actions.hpp] (was the
// RA1-lineage 1/10-minute x90 assumption).
constexpr int kMapGuardFramesPerArg = kScriptGuardFramesPerArg;
// Advance one tick. A move action completes when view.arrived; an attack when
// the members went idle or no target exists; guard when its timer ran out;
// instant actions (vars, win, lose, change house, scatter, jump) complete in
// the tick they are issued; unknown actions are Skip (pc++).
MapTeamCommand step_map_team(MapTeamState& st, const MapScript& script, const MapTeamView& view,
                             long frame);

// ---------------------------------------------------------------------------
// P7/P10 save: the CampaignRuntime state that lives OUTSIDE the World and the
// MapTriggerSystem (those have their own WRLD / TRIG sections). Flat, so the
// save visitor (save_fields.hpp) and the arity tripwire cover it.
struct CampaignHostSave {
    // per World unit / building tracking (parallel to the World arrays)
    std::vector<int> u_tag, u_cx, u_cy, u_hp, u_team;
    std::vector<std::string> u_type;
    std::vector<std::uint8_t> u_flags;   // dead|discovered<<1|selected<<2|hunt<<3|initial<<4|dmg<<5
    std::vector<int> u_mc_victim, u_mc_orig;   // W2-CA-15 mind control (save v12)
    std::vector<std::uint8_t> u_mission;       // W4-SQ-05: 1 Sleep, 2 Harmless (save v16)
    std::vector<int> b_tag, b_hp;
    std::vector<std::string> b_type;
    std::vector<std::uint8_t> b_flags;
    // map teams
    std::vector<int> t_type, t_script, t_house, t_pc, t_goal_x, t_goal_y;
    std::vector<std::vector<int>> t_members;
    std::vector<std::int64_t> t_wait_until, t_issued_frame, t_created;
    std::vector<std::uint8_t> t_flags;   // issued|done<<1|recruiting<<2
    // house counters (events 15/16/19-22)
    std::vector<int> destroyed_units, destroyed_buildings, last_built;   // last_built: 4 per house
    // objectives shown on screen + mission end + clocks
    std::vector<std::int64_t> obj_frames;
    std::vector<std::string> obj_texts;
    int end_outcome = 0;
    std::int64_t end_flagged_at = -1;
    std::int64_t frame = 0;
    double accum = 0.0;
    std::uint64_t logged = 0;
    int teams_created = 0;
    int raises = 0;
    // W5-A (save v22): 61/62 building power switch, 35 house quarries, 9 FireSale
    // houses, 23 TeamTypes that left the map, 40 ResizeView area (valid,x,y,w,h).
    std::vector<std::uint8_t> b_off;
    std::vector<int> house_quarry;
    std::vector<std::uint8_t> house_fire_sale;
    std::vector<std::string> left_teams;
    std::vector<int> local_override;
};

}  // namespace ra2engine
