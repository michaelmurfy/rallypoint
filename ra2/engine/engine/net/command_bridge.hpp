// engine/net/command_bridge.hpp -- NET L1: the clone's ra2engine::Command
// (engine/command.hpp) <-> the wire-level NetCommand (event_map.hpp), the
// entry points event_map.hpp reserved for L1.
//
// A Command carries a whole Whom run (the units that got the same order on the
// same click); the original sends one EventClass per object and the codec
// run-length groups consecutive MEGAMISSIONs (01-original-protocol.md §3.4).
// to_net() expands a Command into its records (one per Whom object, in order);
// from_net() re-assembles consecutive records of one house / frame / payload
// back into one Command, so a group move computes the same formation on every
// peer. The Chronosphere's two picks travel as SPECIAL_PLACE(ChronoSphere) +
// SPECIAL_PLACE(ChronoWarp) and are paired again.
//
// Identities: a unit / building TargetRef id is the clone's World index (low 24
// bits); a cell Target keeps the CellStruct packing (x | y << 16); the SW index
// is the original's binary index (SuperweaponType - 1); PLACE / PRODUCE HeapID
// is the clone's roster index (peers run the same rules -- the session's data
// hash check guarantees it).
#pragma once

#include <vector>

#include "command.hpp"
#include "net/event_map.hpp"

namespace ra2::net {

std::vector<NetCommand> to_net(const ra2engine::Command& c);
// Re-assembles a frame's records (any order of houses is fine; records of one
// house must keep their send order). Non-sim records are skipped.
std::vector<ra2engine::Command> from_net(const std::vector<NetCommand>& records);

// The wire-record forms event_map.hpp proposed. to_events() gives every
// EventClass record of a Command (a Whom run is one record per object);
// to_event() succeeds only for a Command that is exactly one record.
// from_event() decodes ONE record (a single-object Command; use from_net on a
// frame's records to re-group runs).
std::vector<Event> to_events(const ra2engine::Command& c);
bool to_event(const ra2engine::Command& c, Event& out);
bool from_event(const Event& e, ra2engine::Command& out);

}  // namespace ra2::net
