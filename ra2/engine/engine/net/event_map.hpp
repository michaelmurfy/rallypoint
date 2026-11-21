// engine/net/event_map.hpp -- mapping between the wire EventClass and the
// clone's command type.
//
// Agent L1 is adding engine/command.hpp (a Command modelled on EventClass).
// It was not on master when this module was written, so the mapping is
// defined against the minimal NetCommand below, which carries exactly the
// EventClass fields. When L1's type lands, the agreed entry points are:
//
//     bool ra2::net::to_event(const ra2::Command& c, ra2::net::Event& out);
//     bool ra2::net::from_event(const ra2::net::Event& e, ra2::Command& out);
//
// implemented here in event_map.cpp on top of command_to_event() /
// event_to_command() (field-for-field; no semantic translation, so a future
// bit-exact sim sees exactly what gamemd would execute).
#pragma once

#include "net/event.hpp"

#include <cstdint>
#include <vector>

namespace ra2::net {

struct NetCommand {
    EventType kind = EventType::EMPTY;
    int house = -1;
    uint32_t frame = 0;
    // Object references (TargetClass: 24-bit UniqueID + RTTI).
    TargetRef whom, target, dest, follow;
    uint8_t mission = 0;     // MEGAMISSION / MEGAMISSION_F
    bool planning = false;   // MEGAMISSION IsPlanningEvent
    int32_t speed = 0, max_speed = 0;  // MEGAMISSION_F
    // PRODUCE / SUSPEND / ABANDON / ABANDON_ALL / PLACE
    int32_t rtti = 0, heap_id = 0, is_naval = 0;
    CellXY cell;             // PLACE / SPECIAL_PLACE / SELLCELL
    int32_t value = 0;       // ALLY, GAMESPEED, SPECIAL, REMOVEPLAYER, SPECIAL_PLACE index, ...
    int32_t x = 0, y = 0;    // ANIMATION location (value = AnimID, rtti = HouseID)
    std::vector<uint8_t> blob;  // ADDPLAYER / raw payloads (MEGAFRAMEINFO, PACKETTIMING)
};

// Returns false for types that are not commands (network-internal or
// packet-level: FRAMESYNC, FRAMEINFO, TIMING, ...) or unknown types.
bool command_to_event(const NetCommand& c, Event& out);
bool event_to_command(const Event& e, NetCommand& out);

// True for events the simulation executes (everything except the scheduler's
// own TIMING / RESPONSE_TIME / PROCESS_TIME / LATENCYFUDGE / FRAMEINFO /
// FRAMESYNC / RESPONSE_TIME2 and the router-only types).
bool is_sim_event(EventType t);

}  // namespace ra2::net
