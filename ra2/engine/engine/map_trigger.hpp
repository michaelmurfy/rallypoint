#pragma once
// P9 map triggers / scripting [RE: docs/re/map-triggers.md].
//
// Pure C++17 (no SDL, no World): the map's [Triggers]/[Events]/[Actions]/
// [Tags]/[CellTags]/[VariableNames] parsed into MapTriggerData (RE §1), and
// MapTriggerSystem, the runtime that reproduces gamemd.exe's TagClass /
// TriggerClass / TEventClass / TActionClass behaviour (RE §3-§5). Everything
// the trigger system needs to know about (or do to) the world goes through
// the abstract TriggerHost, so the engine is unit-testable with a fake host
// and the World is never touched unless a caller builds a host on it.
//
// FIREWALL: nothing in the engine constructs a MapTriggerSystem except the
// --demo-triggers gate; every pre-existing mode/gate is unaffected.
#include <cstdint>
#include <string>
#include <vector>
#include "ra2assets/ini.hpp"

namespace ra2engine {

// ---------------------------------------------------------------------------
// Event kinds (TEventClass +0x2C) — names from the YRpp TriggerEvent enum, the
// semantics from HasOccurred FUN_0071dbe0 [RE: map-triggers.md §4].
enum MapEventKind : int {
    kEvNone = 0, kEvEnteredBy = 1, kEvSpiedBy = 2, kEvThievedBy = 3,
    kEvDiscoveredByPlayer = 4, kEvHouseDiscovered = 5, kEvAttackedByAnybody = 6,
    kEvDestroyedByAnybody = 7, kEvAnyEvent = 8, kEvDestroyedUnitsAll = 9,
    kEvDestroyedBuildingsAll = 10, kEvDestroyedAll = 11, kEvCreditsExceed = 12,
    kEvElapsedTime = 13, kEvMissionTimerExpired = 14, kEvDestroyedBuildingsNum = 15,
    kEvDestroyedUnitsNum = 16, kEvNoFactoriesLeft = 17, kEvCiviliansEvacuated = 18,
    kEvBuildBuildingType = 19, kEvBuildUnitType = 20, kEvBuildInfantryType = 21,
    kEvBuildAircraftType = 22, kEvTeamLeavesMap = 23, kEvZoneEntryBy = 24,
    kEvCrossesHorizontalLine = 25, kEvCrossesVerticalLine = 26, kEvGlobalSet = 27,
    kEvGlobalCleared = 28, kEvDestroyedFakesAll = 29, kEvLowPower = 30,
    kEvAllBridgesDestroyed = 31, kEvBuildingExists = 32, kEvSelectedByPlayer = 33,
    kEvComesNearWaypoint = 34, kEvEnemyInSpotlight = 35, kEvLocalSet = 36,
    kEvLocalCleared = 37, kEvAttackedByHouse = 44, kEvAmbientLightBelow = 45,
    kEvAmbientLightAbove = 46, kEvElapsedScenarioTime = 47,
    kEvDestroyedByAnything = 48, kEvPickupCrate = 49, kEvPickupCrateAny = 50,
    kEvRandomDelay = 51, kEvCreditsBelow = 52, kEvSpyAsHouse = 53,
    kEvSpyAsInfantry = 54, kEvDestroyedUnitsNaval = 55, kEvDestroyedUnitsLand = 56,
    kEvBuildingDoesNotExist = 57, kEvPowerFull = 58, kEvEnteredOrOverflownBy = 59,
    kEvTechTypeExists = 60, kEvTechTypeDoesntExist = 61,
};

// Attach flags [RE §3.1] (FUN_0071e920 per event, FUN_006e3450 per action).
enum : unsigned {
    kAttachCell = 0x01, kAttachObject = 0x02, kAttachMap = 0x04,
    kAttachHouse = 0x08, kAttachLogic = 0x10,
};
unsigned map_event_flags(int kind);
unsigned map_action_flags(int kind);

// TagType persistence [RE §1.4/§3.4].
enum : int { kPersistVolatile = 0, kPersistSemi = 1, kPersistPersistent = 2 };

// Waypoint letter decode, FUN_007624b0 [RE §1.3]: non-alpha first char -> -1;
// one letter -> L-'A'; two letters -> (L1-'A'+1)*26 + (L2-'A'); case-insensitive;
// characters after the second are ignored.
int map_waypoint_letters(const std::string& s);

// ---------------------------------------------------------------------------
// Parsed data [RE §1].
struct MapEvent {
    int kind = 0;
    int param = 0;        // the "Param" token (0 value, 1 team, 2 value+string)
    int value = 0;        // +0x34
    std::string str;      // Param==2 TechnoType ID (+0x38)
};

struct MapAction {
    int kind = 0;         // +0x2C
    int param_kind = 0;   // how P1 was interpreted [RE §1.3 table]
    int value = 0;        // +0x90
    int p[4] = {0, 0, 0, 0};   // +0x34..+0x40
    int waypoint = -1;    // +0x44 (letters)
    int value2 = 0;       // +0x48 (ParamKind 11)
    int team = -1;        // ParamKind 1/5: index into MapTriggerData::team_ids
    int trigger = -1;     // ParamKind 2: MapTriggerData::triggers index
    int tag = -1;         // ParamKind 3: MapTriggerData::tags index
    std::string text;     // ParamKind 4 label; 6/7/8 raw name; 9/10 TechnoID
};

struct MapTriggerType {
    std::string id;
    std::string name;
    std::string house;          // raw token
    int house_type = -1;        // HouseType index (RE §6); "<none>" -> 0
    int linked = -1;            // LinkedTrigger -> triggers index (the tag chain)
    bool enabled = false;       // !Disabled (a missing field -> false)
    bool difficulty[3] = {false, false, false};   // Easy/Normal/Hard
    bool must_transfer = false;
    std::vector<MapEvent> events;    // MEMORY order = INI order REVERSED [RE §1.2]
    std::vector<MapAction> actions;  // file order
    bool defined = false;       // a [Triggers] line was read for it
};

struct MapTagType {
    std::string id;
    std::string name;
    int persistence = 0;
    int first_trigger = -1;
    bool defined = false;
};

struct MapCellTag { int x = 0, y = 0; int tag = -1; };

struct MapTriggerData {
    std::vector<std::string> house_types;     // rules [Countries] then map-new
    std::vector<std::string> building_types;  // rules [BuildingTypes] then map-new
    std::vector<MapTriggerType> triggers;     // array order = first reference
    std::vector<MapTagType> tags;             // array order = first reference
    std::vector<std::string> team_ids;        // [TeamTypes] index order
    std::vector<std::string> local_names;     // 100 slots
    std::vector<std::uint8_t> local_init;     // 100 slots (0/1)
    std::vector<MapCellTag> cell_tags;        // file order (first tag per cell)

    int find_trigger(const std::string& id) const;
    int find_tag(const std::string& id) const;
    int find_house_type(const std::string& name) const;
    int find_building_type(const std::string& id) const;
};

// Parse a map INI [RE §1]. `rules_countries` / `rules_buildings` are the
// rules [Countries] / [BuildingTypes] value lists in file order; the map's own
// lists are appended (find-or-allocate by name, case-insensitive) [RE §6].
MapTriggerData parse_map_triggers(const ra2assets::Ini& map,
                                  const std::vector<std::string>& rules_countries,
                                  const std::vector<std::string>& rules_buildings);

// Ordered values of an INI list section (e.g. [Countries]) — helper for the
// callers that build the rules lists above.
std::vector<std::string> ini_list_values(const ra2assets::Ini& ini,
                                         const std::string& section);

// ---------------------------------------------------------------------------
// Host interface: what the trigger system asks of / does to the world.
// house_type arguments are HouseType indices (RE §6). Defaults model an
// untracked quantity conservatively and are documented per method.

// A host-level action, resolved by the trigger system [RE §5].
struct TriggerEffect {
    int action = 0;              // action kind
    int house_type = -1;         // the house the action targets (resolved)
    int trigger_house_type = -1; // the trigger's own house
    std::string team;            // TeamType id ("" = none)
    int waypoint = -1;           // waypoint index (action Waypoint, or Value for 17)
    int value = 0;               // action Value
    std::string text;            // TechnoID / label
    std::string trigger_id;      // the firing trigger
    int trigger_type = -1;       // P10: the firing trigger's MapTriggerData index
    int p[4] = {0, 0, 0, 0};     // W5-A: the action's P2..P5 (+0x34..+0x40), e.g. 40 ResizeView's rect
};

// W3-FX [RE: FUN_006dce20 cases 41 PlayAnimAt (FUN_006e1800), 48 MoveCamera
// (FUN_006d1990), 55 CreateRadarEvent (FUN_0065f4b0, ECX = Value), 88
// ParticleAnim (ParticleSystemTypes[Value] at the waypoint), 104 FlashTeam,
// 112 CenterCamera (FUN_006d55e0), 113 MakeHouseCheer (FUN_0050c6e0), 114/115
// sidebar tab/cameo, 131 FlashBuildingsOfType]: the presentation actions as
// typed requests handed to TriggerHost::present (campaign mode only). Never
// hashed, logged (the log lines stay byte-identical) or saved -- they carry
// no simulation state, and the trigger system keeps no copy.
struct VisualRequest {
    int action = 0;
    int value = 0;          // action Value (+0x90)
    int waypoint = -1;      // action Waypoint (+0x44)
    int house_type = -1;    // 113: FindHouseByIndex(Value)
    std::string team;       // 104: the TeamType id
    std::string text;
};

class TriggerHost {
public:
    virtual ~TriggerHost() = default;
    // A live house instance exists for this house type (map [Houses]).
    virtual bool house_exists(int house_type) const = 0;
    // House types of the live house instances, in HouseClass array order
    // (map [Houses] order) -- the house-loop order [RE §3.2].
    virtual std::vector<int> house_instances() const = 0;
    // The player's house type (Win/Lose are player-relative, RE §5).
    virtual int player_house_type() const = 0;
    // Owned-object counters (RE §4: +0x2F0 buildings, +0x5564 vehicles,
    // +0x5578 infantry, +0x5550 per-BuildingType).
    virtual int house_buildings(int ht) const = 0;
    virtual int house_vehicles(int ht) const = 0;
    virtual int house_infantry(int ht) const = 0;
    // W2-CA-06: live naval units of the house (a subset of house_vehicles).
    virtual int house_naval_units(int ht) const { (void)ht; return 0; }
    virtual int house_building_type_count(int ht, int building_type) const = 0;
    virtual long house_credits(int ht) const { (void)ht; return 0; }
    // Power output/drain ratio (>= 1.0 = full power; no drain -> 1.0).
    virtual double house_power_ratio(int ht) const { (void)ht; return 1.0; }
    // True iff the house owns a live factory building (event 17).
    virtual bool house_has_factory(int ht) const { (void)ht; return true; }
    // Destroyed-object counters (events 15/16). Untracked -> 0.
    virtual int house_destroyed_buildings(int ht) const { (void)ht; return 0; }
    virtual int house_destroyed_units(int ht) const { (void)ht; return 0; }
    // W4-SQ-11 event 5 HouseDiscovered: the player has seen an object of the
    // house [RE-FALLBACK: polled each frame instead of sprung on discovery].
    virtual bool house_discovered(int ht) const { (void)ht; return false; }
    // Last-built type index per category 0 building, 1 vehicle, 2 infantry,
    // 3 aircraft (events 19-22). Untracked -> -1 (never equals a Value >= 0).
    virtual int house_last_built(int ht, int category) const { (void)ht; (void)category; return -1; }
    // Live technos (all houses) whose type ID == id; -1 when the type is
    // unknown to the game (events 60/61 then evaluate false, RE §4). An EMPTY
    // id asks for the total live techno count (event 60's empty-world rule).
    virtual int techno_count(const std::string& id) const = 0;
    // W5-A events: 23 TeamLeavesMap (a team of this TeamType id left the map
    // with no members), 34 ComesNearWaypoint (the waypoint's cell), 54
    // SpyAsInfantry (the RA2 [InfantryTypes] id at this index).
    virtual bool team_left_map(const std::string& team_id) const { (void)team_id; return false; }
    virtual bool waypoint_cell(int wp, int& x, int& y) const { (void)wp; (void)x; (void)y; return false; }
    virtual std::string infantry_type_id(int idx) const { (void)idx; return std::string(); }
    // Execute a host-level action. Returns the action's success (logged).
    virtual bool apply(const TriggerEffect& e) = 0;
    // Optional detail about the last apply(), appended to its log line.
    virtual std::string take_note() { return std::string(); }
    // W3-FX: a presentation action to show (campaign mode only). No-op default.
    virtual void present(const VisualRequest& v) { (void)v; }
};

// Context of an object/cell raise (RE §3.2 object kinds).
struct RaiseContext {
    bool has_object = false;
    int object_house_type = -1;   // owner of the raising object
    int source_house_type = -1;   // attacker (event 44); -1 = no source
    // W5-A: the raising object's cell (34 ComesNearWaypoint) and disguise
    // (53 SpyAsHouse: the house type it poses as; 54 SpyAsInfantry: the
    // infantry type id it poses as).
    int obj_x = -1, obj_y = -1;
    int disguise_house_type = -1;
    std::string disguise_infantry;
};

// ---------------------------------------------------------------------------
// Runtime [RE §2/§3].
class MapTriggerSystem {
public:
    friend struct SaveAccess;  // P7 Save/Load (engine/save_fields.hpp)
    friend struct NetCrcAccess;  // W9-05: the per-frame sync CRC folds the runtime (net_frame_crc.cpp)
    MapTriggerSystem(const MapTriggerData& data, int difficulty, std::uint32_t rng_seed);

    // Scenario start [RE §3.2]: locals initialised, globals cleared, tag
    // instances + Map/Logic/House lists built (constructing triggers ->
    // ResetTimers). Frame stays 0.
    void start(TriggerHost& host);

    // One logic frame [RE §3.2]: logic loop, flag clear + mission-timer stop,
    // house loop when frame % 8 == 0, deferred tag deletion, ++frame.
    void tick(TriggerHost& host);

    // Object/cell raises [RE §3.2/§3.4]. raise_cell uses [CellTags].
    bool raise_tag(int tag_type, int kind, const RaiseContext& ctx, TriggerHost& host);
    bool raise_cell(int x, int y, int kind, const RaiseContext& ctx, TriggerHost& host);
    // W5-A: raise `kind` on every tag of the Logic list (23 TeamLeavesMap, a
    // Logic-flag kind with no object of its own).
    bool raise_logic(int kind, const RaiseContext& ctx, TriggerHost& host);

    // --- P10 campaign mode [RE: campaign.md §4, map-triggers.md §3.4/§5] ------
    // OFF by default: with it off every action/raise behaves (and logs) exactly
    // as P9, so --demo-triggers is unaffected. ON routes the object actions
    // 14 ChangeHouse, 32 DestroyAttachedObject, 36 AllChangeHouse, 37 MakeAlly,
    // 38 MakeEnemy and 63 Apply100Damage to the host (TriggerEffect carries the
    // firing trigger), and makes a SemiPersistant tag detach a non-last object.
    void set_campaign_mode(bool on) { campaign_mode_ = on; }
    bool campaign_mode() const { return campaign_mode_; }
    // An object carrying `tag_type` was placed (TagClass::GetInstance +
    // attach: InstanceCount++). Returns false when the tag type is invalid.
    bool attach_tag(int tag_type);
    // The object left the game (InstanceCount--).
    void detach_tag(int tag_type);
    // True while the tag type's instance exists and is not destroyed.
    bool tag_alive(int tag_type) const;
    // Raise `kind` on an object's tag WITHOUT creating a new instance (a
    // destroyed tag is gone for good). detached = the object lost its tag
    // (SemiPersistant with other objects still attached, RE §3.4).
    struct ObjectRaise { bool fired = false; bool detached = false; bool tag_gone = false; };
    ObjectRaise raise_object(int tag_type, int kind, const RaiseContext& ctx, TriggerHost& host);
    // True if trigger `trigger_type` is in tag type `tag_type`'s chain.
    bool tag_has_trigger(int tag_type, int trigger_type) const;
    // W5-A: true if a trigger in tag type `tag_type`'s chain has an event of
    // `kind` (the clone's own raise sites for 31/34/53/54 only raise on such
    // tags, so they never re-evaluate unrelated timer / variable events).
    bool tag_waits_on(int tag_type, int kind) const;
    bool cell_waits_on(int x, int y, int kind) const;
    // The data this system runs (for hosts that need the tables).
    const MapTriggerData& data() const { return d_; }

    // Variables [RE §1.5/§3.6].
    bool local(int i) const;
    bool global(int i) const;
    void set_local(int i, bool v);
    void set_global(int i, bool v);

    long frame() const { return frame_; }
    // 0 none, 1 player won, 2 player lost (first decision sticks).
    int outcome() const { return outcome_; }
    const std::vector<std::string>& log() const { return log_; }
    int fired_count() const { return fired_; }
    int logic_fired() const { return logic_fired_; }
    int house_fired() const { return house_fired_; }
    int variable_fired() const { return variable_fired_; }
    // Mission timer: -1 = stopped.
    long mission_timer_start() const { return mt_start_; }
    long mission_timer_left() const { return mt_left_; }
    // W6-05-04 [RE: action 103 TimerText sets Scenario+0x7D4, the label
    // FUN_006a45a0 draws before the mission timer]: the CSF label ("" = none).
    const std::string& timer_text() const { return timer_text_; }
    // Per-house flags set by actions 3/13/74/75 (RE §5): bit0 production,
    // bit1 autocreate, bit2 AI triggers.
    unsigned house_flags(int ht) const;
    // Trigger instance state, for tests: enabled/destroyed of the FIRST live
    // instance of a trigger type (false/true when none).
    bool trigger_enabled(int trigger_type) const;
    bool trigger_destroyed(int trigger_type) const;
    std::uint64_t state_hash() const;
    // W3-FX --play dev hook (`cheat camp_fire NAME`): run every instance of
    // the trigger type named `name` (or with that ID) now, enabling it first
    // (ForceTrigger without the Enabled gate). Returns the instances fired.
    int force_fire(const std::string& name, TriggerHost& host);

private:
    struct Trig {
        int type = -1;
        int house_override = -1;
        bool destroyed = false;
        long t_start = 0, t_left = 0;
        std::uint32_t bits = 0;
        bool enabled = true;
    };
    struct Tag {
        int type = -1;
        std::vector<Trig> trigs;  // chain REVERSED (ctor prepends) [RE §3.4]
        int instance_count = 0;
        bool destroyed = false;
        bool executing = false;
        bool pending_delete = false;
    };

    int get_instance(int tag_type);
    void reset_timers(Trig& t);
    bool raise(int tag_idx, int kind, const RaiseContext& ctx, TriggerHost& host);
    bool register_event(Trig& t, int kind, const RaiseContext& ctx, bool persistent,
                        TriggerHost& host);
    bool has_occurred(int type, int ev_idx, int kind, int trig_house, const RaiseContext& ctx,
                      Trig& t, bool& persistent, TriggerHost& host);
    void fire_actions(Tag* tag, int trig_slot, Trig& t, TriggerHost& host);
    void execute(const MapAction& a, Trig& t, TriggerHost& host);
    int find_house(const Trig& t, int v, TriggerHost& host) const;
    int trigger_house(const Trig& t, TriggerHost& host) const;
    void notify_variable(bool global, int idx);
    std::uint32_t rand_inclusive(int hi);
    void logf(const char* fmt, ...);
    void set_outcome(int o) { if (outcome_ == 0) outcome_ = o; }

    const MapTriggerData& d_;
    int difficulty_;
    std::uint32_t rng_;
    long frame_ = 0;
    std::vector<std::uint8_t> globals_, locals_;
    bool vars_changed_ = false;
    long mt_start_ = -1, mt_left_ = 0;
    std::string timer_text_;   // W6-05-04 action 103 label (save v25)
    int outcome_ = 0;
    std::vector<Tag> tags_;                 // instances, creation order
    std::vector<int> tag_of_type_;          // tag type -> instance (-1)
    std::vector<int> map_list_, logic_list_;
    std::vector<std::vector<int>> house_lists_;   // by house type
    std::vector<int> ev_house_;             // per (type,event) +0x54, flattened
    std::vector<int> ev_base_;              // per type: offset into ev_house_
    std::vector<unsigned> house_flags_;
    std::vector<std::string> log_;
    int fired_ = 0, logic_fired_ = 0, house_fired_ = 0, variable_fired_ = 0;
    int raise_depth_kind_ = 0;              // kind of the innermost active raise
    int force_depth_ = 0;                   // ForceTrigger recursion guard
    std::string cur_trigger_;               // for log lines
    bool campaign_mode_ = false;            // P10 firewall (see set_campaign_mode)
    bool semi_detached_ = false;            // set by raise() for raise_object
};

}  // namespace ra2engine
