#pragma once
// NET L1: the command layer. Every player action is a Command, modelled on the
// original game's EventClass [RE: docs/gap/net/01-original-protocol.md §3]:
//   EventClass (111 bytes, packed) = u8 Type, u8 IsExecuted, i8 HouseIndex,
//   u32 Frame, 104-byte payload union (only size[type] bytes meaningful; the
//   size table is 0x81F8FC, the executor EventClass::Execute FUN_004C6BD0).
// The type numbering below IS the original's (EVENT_EMPTY 0x00 .. EVENT_
// ABANDON_ALL 0x2E), the fields are the original's payload fields, so the wire
// codec (agent T) maps a Command to an EventClass record 1:1. A MEGAMISSION
// Command carries the WHOLE run of units that got the same mission (the
// original's run-length group, §3.4) as its `whom` list; the codec emits it as
// [0x04][count][23-byte payload]{[5-byte Whom] x (count-1)}.
//
// Lifecycle: the UI builds a Command, the local CommandScheduler stamps it
// (issuing house, execution frame = now + delay, per-house sequence) and pushes
// it into the CommandQueue; the game applies the queue's due commands at the
// START of a logic tick in a fixed order (frame, then issuing house, then
// sequence) through apply_command(). Single player uses delay 0.
//
// Object references: a unit is its World::units() index (never compacted, so
// stable for the life of a game), a building its World::buildings() index, a
// cell (x, y). Every apply re-validates liveness and ownership.

#include <cstdint>
#include <string>
#include <vector>

#include "world.hpp"

namespace ra2engine {

// The original's EventType [RE: 01-original-protocol.md §3.2; YRpp EventType].
enum class EventType : std::uint8_t {
    Empty = 0x00,
    PowerOn = 0x01,         // Target                       -> building powered up
    PowerOff = 0x02,        // Target                       -> building powered down
    Ally = 0x03,            // i32 HouseID                  -> MakeAlly / MakeEnemy (one-way, W9-05-03)
    MegaMission = 0x04,     // Whom, Mission, Target, Destination, Follow, IsPlanning
    MegaMissionF = 0x05,    // + Speed / MaxSpeed (formation move; not issued by the clone)
    Idle = 0x06,            // Whom                         -> Stop (StopObject FUN_0072FF70)
    Scatter = 0x07,         // Whom                         -> Scatter (FUN_007300B0)
    Destruct = 0x08,        //                              -> house self-destruct (resign, W9-05-06)
    Deploy = 0x09,          // Whom                         -> DeployObject (unload / deploy)
    Detonate = 0x0A,        // Whom
    Place = 0x0B,           // i32 RTTI, i32 HeapID, i32 IsNaval, CellStruct
    Options = 0x0C,
    GameSpeed = 0x0D,       // i32 speed
    Produce = 0x0E,         // i32 RTTI, i32 HeapID, i32 IsNaval
    Suspend = 0x0F,         // same
    Abandon = 0x10,         // same
    Primary = 0x11,         // Whom                         -> primary factory
    SpecialPlace = 0x12,    // i32 SWIdx, CellStruct        -> superweapon launch
    Exit = 0x13,
    Animation = 0x14,
    Repair = 0x15,          // Whom                         -> toggle repair
    Sell = 0x16,            // Whom                         -> sell
    SellCell = 0x17,        // CellStruct                   -> sell a wall
    Special = 0x18,
    FrameSync = 0x19,
    Message = 0x1A,
    ResponseTime = 0x1B,
    FrameInfo = 0x1C,
    SaveGame = 0x1D,
    Archive = 0x1E,         // Target, Target               -> factory rally point (ArchiveTarget)
    AddPlayer = 0x1F,
    Timing = 0x20,
    ProcessTime = 0x21,
    PageUser = 0x22,
    RemovePlayer = 0x23,
    LatencyFudge = 0x24,
    MegaFrameInfo = 0x25,
    PacketTiming = 0x26,
    AboutToExit = 0x27,
    FallbackHost = 0x28,
    AddressChange = 0x29,
    PlanConnect = 0x2A,
    PlanCommit = 0x2B,
    PlanNodeDelete = 0x2C,
    AllCheer = 0x2D,        //                              -> every own infantry cheers
    AbandonAll = 0x2E,      // RTTI, HeapID, IsNaval        -> abandon every queued copy (YR)
    // ---- clone-local extensions (never sent to an original peer) ----
    // The player's selection, for the campaign event "Selected by player"
    // (33). The original raises it locally from TechnoClass::Select; the
    // clone logs it as a command so a replay sees the same selection.
    SelectNotify = 0x40,
};
const char* event_name(EventType t);

// The original's Mission enum [YRpp Mission] (MEGAMISSION payload byte).
enum class Mission : std::int8_t {
    None = -1, Sleep = 0, Attack = 1, Move = 2, QMove = 3, Retreat = 4, Guard = 5, Sticky = 6,
    Enter = 7, Capture = 8, Eaten = 9, Harvest = 10, AreaGuard = 11, Return = 12, Stop = 13,
    Ambush = 14, Hunt = 15, Unload = 16, Sabotage = 17, Construction = 18, Selling = 19,
    Repair = 20, Rescue = 21, Missile = 22, Harmless = 23, Open = 24, Patrol = 25,
    ParadropApproach = 26, ParadropOverfly = 27, Wait = 28, AttackMove = 29,
    SpyplaneApproach = 30, SpyplaneOverfly = 31,
};

// The original's AbstractType (RTTI) values used by events [YRpp AbstractType].
enum class Rtti : std::uint8_t {
    None = 0, Unit = 1, Aircraft = 2, AircraftType = 3, Building = 6, BuildingType = 7,
    Cell = 11, House = 13, Infantry = 15, InfantryType = 16, SuperWeaponType = 32, UnitType = 40,
};

// TargetClass: on the wire u32 id (low 24 bits) + u8 RTTI. Here: a unit /
// building index, or a cell packed as x | y << 16 (the CellStruct layout).
struct Target {
    Rtti rtti = Rtti::None;
    std::int32_t id = -1;

    static Target unit(const World& w, int idx);      // Unit / Infantry / Aircraft by kind
    static Target building(int idx) { return {Rtti::Building, idx}; }
    static Target cell(int x, int y) {
        return {Rtti::Cell, static_cast<std::int32_t>((static_cast<std::uint32_t>(y & 0xFFFF) << 16) |
                                                      static_cast<std::uint32_t>(x & 0xFFFF))};
    }
    bool none() const { return rtti == Rtti::None; }
    bool is_unit() const { return rtti == Rtti::Unit || rtti == Rtti::Infantry || rtti == Rtti::Aircraft; }
    bool is_building() const { return rtti == Rtti::Building; }
    bool is_cell() const { return rtti == Rtti::Cell; }
    int cell_x() const { return static_cast<std::int16_t>(static_cast<std::uint32_t>(id) & 0xFFFF); }
    int cell_y() const { return static_cast<std::int16_t>(static_cast<std::uint32_t>(id) >> 16); }
    bool operator==(const Target& o) const { return rtti == o.rtti && id == o.id; }
};

struct Command {
    EventType type = EventType::Empty;
    std::int8_t house = 0;          // HouseIndex: the issuing house
    std::uint32_t frame = 0;        // execution frame (logic tick)
    std::uint32_t seq = 0;          // per-house issue order (deterministic tie-break; not on the wire)
    // Whom: MEGAMISSION run members (ascending unit ids), IDLE / SCATTER /
    // DEPLOY objects, or the building of POWERON/OFF, REPAIR, SELL, PRIMARY,
    // ARCHIVE.
    std::vector<Target> whom;
    Mission mission = Mission::None;   // MEGAMISSION
    Target target;                     // MEGAMISSION Target / ARCHIVE second target
    Target dest;                       // MEGAMISSION Destination
    // PLACE / PRODUCE / SUSPEND / ABANDON / ABANDON_ALL: the object type.
    std::int32_t rtti_type = 0;        // Rtti of the type (BuildingType / UnitType / ...)
    std::int32_t heap_id = -1;         // type index (the clone's roster index)
    std::int32_t is_naval = 0;
    // PLACE / SPECIAL_PLACE / SELLCELL cell; SPECIAL_PLACE second pick
    // (the Chronosphere's ChronoWarp destination; the codec sends it as a
    // second SPECIAL_PLACE of the ChronoWarp type).
    std::int16_t cx = -1, cy = -1;
    std::int16_t cx2 = -1, cy2 = -1;
    std::int32_t value = 0;            // ALLY HouseID / GAMESPEED speed / SPECIAL_PLACE SWIdx
    std::vector<std::int32_t> ext;     // SelectNotify: the selected unit ids

    std::vector<int> whom_ids() const;   // whom ids (any RTTI)
    bool operator==(const Command& o) const;
};

// --- construction helpers (the UI side) ------------------------------------
Command make_mega(const World& w, const std::vector<int>& ids, Mission m, Target target, Target dest);
Command make_whom(const World& w, EventType t, const std::vector<int>& unit_ids);
Command make_building_event(EventType t, int building);
Command make_type_event(EventType t, Rtti rtti, int heap_id, bool naval);

// --- the queue --------------------------------------------------------------
// Pending commands, kept in the deterministic apply order: frame, then
// issuing house, then sequence. push() is stable for equal keys.
class CommandQueue {
public:
    void push(Command c);
    // Removes and returns every command with frame <= `frame`, in order.
    std::vector<Command> take_due(std::uint32_t frame);
    bool empty() const { return q_.empty(); }
    std::size_t size() const { return q_.size(); }
    const std::vector<Command>& pending() const { return q_; }
    void clear() { q_.clear(); }

private:
    std::vector<Command> q_;
};

// The local scheduler: stamps a UI command for frame now + delay (delay 0 in
// single player, so the order lands at the very next tick like before).
class CommandScheduler {
public:
    explicit CommandScheduler(CommandQueue& q, int house = 0, std::uint32_t delay = 0)
        : q_(&q), house_(house), delay_(delay) {}
    void set_now(std::uint32_t next_tick) { now_ = next_tick; }
    std::uint32_t now() const { return now_; }
    void set_delay(std::uint32_t d) { delay_ = d; }
    std::uint32_t delay() const { return delay_; }
    void set_house(int h) { house_ = h; }
    int house() const { return house_; }
    // Stamps house / frame / sequence and queues it; returns the frame.
    std::uint32_t schedule(Command c);

private:
    CommandQueue* q_;
    int house_;
    std::uint32_t delay_;
    std::uint32_t now_ = 0;
    std::uint32_t seq_ = 0;
};

// The game-side tables an apply needs that the asset-free World does not hold
// (the same ones Input's gestures use, so the gesture and its apply agree).
struct OrderRules {
    std::vector<int> refinery_types;                         // GAREFN / NAREFN roster indices
    std::vector<int> inf_factory_types, veh_factory_types;   // primary-factory groups
    std::vector<World::InfiltratePurpose> infiltrate_purposes;   // per roster index
    double spy_steal_pct = 0.5;                              // SpyMoneyStealPercent
    int spy_blackout_frames = 1000;                          // SpyPowerBlackout
    bool is_refinery(int t) const;
    World::InfiltratePurpose purpose_of(int t) const;
    // The factory group set_primary clears (infantry / vehicle / own type).
    std::vector<int> primary_group(int type) const;
};

// The scene-side half of an apply: production, placement and the
// selection notify live outside the World (GameScene today; L2 moves the
// economy into the World and swaps this implementation). The defaults are
// inert, so a bare World (unit tests, Input's immediate mode) still applies
// every unit / building order.
class CommandHost {
public:
    virtual ~CommandHost() = default;
    virtual const OrderRules* order_rules() const { return nullptr; }
    virtual void produce(int /*house*/, int /*rtti*/, int /*type*/, bool /*naval*/) {}
    virtual void suspend(int, int, int, bool) {}
    virtual void abandon(int, int, int, bool) {}
    virtual void abandon_all(int, int, int, bool) {}
    virtual void place(int /*house*/, int /*rtti*/, int /*type*/, int /*cx*/, int /*cy*/) {}
    virtual void selection_notify(int /*house*/, const std::vector<int>& /*ids*/) {}
    // W9-05-03: an ALLY event changed `house`'s view of `other` (the World's
    // AllyChange) -- the scene's TXT_HAS_ALLIED / TXT_AT_WAR line + EVA.
    virtual void diplomacy(int /*house*/, int /*other*/, World::AllyChange /*change*/) {}
    // W9-05-06: a DESTRUCT resigned `house` (Leave / Observe surrender).
    virtual void destructed(int /*house*/) {}
};

// Applies one command to the World (+ host). Every reference is re-checked
// (range, liveness, the issuer may command the object).
void apply_command(World& w, const Command& c, CommandHost* host = nullptr);
// W9-05-12 (clone hardening for commands that arrived from the network): the
// issuing house exists, every unit / building reference indexes the World's
// arrays, every cell is on the map. False = a forged or corrupt event: drop it
// (every peer decides the same from the same World and event).
bool valid_net_command(const World& w, const Command& c);

// --- text form (replay logs) -------------------------------------------------
// One line, no newline: "C <frame> <house> <seq> <type> <mission> <target>
// <dest> <rtti_type> <heap_id> <naval> <cx> <cy> <cx2> <cy2> <value> <n> whom..
// <m> ext..". Round-trips exactly.
std::string command_to_line(const Command& c);
bool command_from_line(const std::string& line, Command& out);

}  // namespace ra2engine
