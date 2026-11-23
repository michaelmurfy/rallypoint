#include "doctest.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <vector>

#include "../engine/cargo.hpp"
using namespace ra2engine;

namespace {
// Hand-built units -- no map/World/install needed (spec §9: capacity>1 and
// Size>1 are covered HOST-ONLY regardless of which transport art the install
// fields).
Unit cargo_unit(int house = 0, int size = 1) {
    Unit u;
    u.house = house;
    u.size = size;
    return u;   // alive=true, cargo_of=-1, cargo empty -- the inert defaults
}
Unit transport_unit(int passengers, int size_limit, int house = 0) {
    Unit u = cargo_unit(house, 3);      // vehicles are Size=3 (rulesmd ground truth)
    u.passengers_max = passengers;
    u.size_limit = size_limit;
    return u;
}
}  // namespace

TEST_CASE("cargo: total_size is the pip-only Size sum, DERIVED from the cargo list") {
    // [RE-GATE APPLIED: transport-cargo.md §1 -- GetTotalSize is DISPLAY-
    // only; capacity is the COUNT, pinned in the next case.]
    std::vector<Unit> us;
    us.push_back(transport_unit(5, 2));          // [0] HTK-shaped: Passengers=5, SizeLimit=2
    us.push_back(cargo_unit(0, 1));              // [1] GI-shaped (Size=1)
    us.push_back(cargo_unit(0, 2));              // [2] DRON-shaped (Size=2)
    CHECK(cargo_total_size(us, us[0]) == 0);
    us[0].cargo = {1};
    CHECK(cargo_total_size(us, us[0]) == 1);
    us[0].cargo = {1, 2};
    CHECK(cargo_total_size(us, us[0]) == 3);     // 1 + 2: the Size=2 DRON shows
                                                 // TWO pips while consuming ONE
                                                 // count slot [RE-GATE APPLIED:
                                                 // transport-cargo.md §1]
    CHECK(static_cast<int>(us[0].cargo.size()) == 2);   // ...the COUNT the gate uses
    us[0].cargo = {1, 2, 99};                    // out-of-range entry ignored
    CHECK(cargo_total_size(us, us[0]) == 3);
}

TEST_CASE("cargo: exact fill; vehicle transports are a Size pool, aircraft a count; SizeLimit refusal") {
    // Exact fill is COUNT-based [RE-GATE APPLIED: transport-cargo.md §1 --
    // NumPassengers < Passengers]: 5 Size-1 GIs fill Passengers=5, the 6th
    // is refused.
    std::vector<Unit> us;
    us.push_back(transport_unit(5, 2));                          // [0]
    for (int i = 0; i < 6; ++i) us.push_back(cargo_unit(0, 1));  // [1..6]
    for (int i = 1; i <= 5; ++i) {               // exact fill: 5 Size-1 riders
        CHECK(can_load(us, 0, i));
        us[0].cargo.push_back(i);
        us[i].cargo_of = 0;
    }
    CHECK(static_cast<int>(us[0].cargo.size()) == 5);   // the exact-fill boundary
    CHECK_FALSE(can_load(us, 0, 6));             // the 6th is REFUSED at capacity
    // W6-04-08 [RE: UnitClass::ReceiveMessage FUN_00736500 case 0xE/0xF --
    // Passengers < GetTotalSize() + Size refuses]: a VEHICLE transport is a
    // Size pool. Two Size-2 DRON-shaped riders fill 4 of Passengers=5; a third
    // Size-2 rider is refused (4 + 2 > 5), a Size-1 GI still boards (4 + 1).
    std::vector<Unit> vs;
    vs.push_back(transport_unit(5, 2));                          // [0]
    for (int i = 0; i < 3; ++i) vs.push_back(cargo_unit(0, 2));  // [1..3] DRON-shaped
    vs.push_back(cargo_unit(0, 1));                              // [4] GI-shaped
    for (int i = 1; i <= 2; ++i) {
        CHECK(can_load(vs, 0, i));
        vs[0].cargo.push_back(i);
        vs[i].cargo_of = 0;
    }
    CHECK(cargo_total_size(vs, vs[0]) == 4);
    CHECK_FALSE(can_load(vs, 0, 3));             // 4 + 2 > 5
    CHECK(can_load(vs, 0, 4));                   // 4 + 1 <= 5
    // AircraftClass::ReceiveMessage FUN_004190B0 (the Night Hawk) keeps the
    // body COUNT: two Size-2 riders take 2 of Passengers=2 regardless of Size.
    std::vector<Unit> as;
    as.push_back(transport_unit(2, 2));
    as[0].is_aircraft = true;
    as.push_back(cargo_unit(0, 2));
    as.push_back(cargo_unit(0, 2));
    as.push_back(cargo_unit(0, 1));
    CHECK(can_load(as, 0, 1));
    as[0].cargo.push_back(1);
    as[1].cargo_of = 0;
    CHECK(can_load(as, 0, 2));                   // count 1 < 2 (size-sum would refuse)
    as[0].cargo.push_back(2);
    as[2].cargo_of = 0;
    CHECK_FALSE(can_load(as, 0, 3));             // count 2 = Passengers
    // The Amphibious Transport (Passengers=12 SizeLimit=6) takes 4 Grizzlies.
    std::vector<Unit> ls;
    ls.push_back(transport_unit(12, 6));
    for (int i = 0; i < 5; ++i) ls.push_back(cargo_unit(0, 3));
    for (int i = 1; i <= 4; ++i) {
        CHECK(can_load(ls, 0, i));
        ls[0].cargo.push_back(i);
        ls[i].cargo_of = 0;
    }
    CHECK_FALSE(can_load(ls, 0, 5));
    // SizeLimit gates the LARGEST admissible passenger independently of room
    // [RE-GATE APPLIED: transport-cargo.md §1 -- PER-PASSENGER Size <=
    // SizeLimit]: a Size=3 vehicle is rejected by SizeLimit=2 (HTK-shaped),
    // while a Size=2 DRON is admitted.
    std::vector<Unit> ws;
    ws.push_back(transport_unit(5, 2));          // [0] SizeLimit=2 (HTK-shaped)
    ws.push_back(cargo_unit(0, 3));              // [1] a Size=3 vehicle-shaped passenger
    ws.push_back(cargo_unit(0, 2));              // [2] a Size=2 DRON-shaped passenger
    CHECK_FALSE(can_load(ws, 0, 1));             // Size 3 > SizeLimit 2
    CHECK(can_load(ws, 0, 2));                   // Size 2 <= SizeLimit 2
}

TEST_CASE("cargo: the can_load matrix rejects every ineligible pairing") {
    std::vector<Unit> us;
    us.push_back(transport_unit(2, 2));          // [0] the transport
    us.push_back(cargo_unit(0, 1));              // [1] eligible baseline
    CHECK(can_load(us, 0, 1));
    CHECK_FALSE(can_load(us, 1, 0));             // not-a-transport (passengers_max=0)
    CHECK_FALSE(can_load(us, 0, 0));             // self
    us.push_back(cargo_unit(1, 1));              // [2] wrong house
    CHECK_FALSE(can_load(us, 0, 2));
    us.push_back(cargo_unit(0, 1));              // [3] the mutation probe
    us[0].die_time = 0.5;                        // dying transport
    CHECK_FALSE(can_load(us, 0, 3));
    us[0].die_time = -1.0;
    us[0].alive = false;                         // dead transport
    CHECK_FALSE(can_load(us, 0, 3));
    us[0].alive = true;
    us[3].die_time = 0.5;                        // dying passenger
    CHECK_FALSE(can_load(us, 0, 3));
    us[3].die_time = -1.0;
    us[3].alive = false;                         // dead passenger
    CHECK_FALSE(can_load(us, 0, 3));
    us[3].alive = true;
    CHECK(can_load(us, 0, 3));                   // probe restored -> eligible again
    us[3].cargo_of = 0;                          // already carried
    CHECK_FALSE(can_load(us, 0, 3));
    us[3].cargo_of = -1;
    us[3].cargo = {1};                           // passenger carrying cargo itself
    CHECK_FALSE(can_load(us, 0, 3));             // (no nested loaded transports)
    us[3].cargo.clear();
    us[0].cargo_of = 1;                          // the TRANSPORT is carried
    CHECK_FALSE(can_load(us, 0, 3));
    us[0].cargo_of = -1;
    CHECK_FALSE(can_load(us, -1, 1));            // out-of-range transport
    CHECK_FALSE(can_load(us, 0, 99));            // out-of-range passenger
}

TEST_CASE("cargo: eject_cell picks the facing +/-90-deg scatter cell, alternating by counter parity") {
    // Facing space = the engine's ONE convention (facing_u16_from_delta,
    // voxel-render.md §2): 0 = North (0,-1), 0x4000 = East (+1,0),
    // 0x8000 = South (0,+1), 0xC000 = West (-1,0), clockwise [RE-GATE
    // APPLIED: transport-cargo.md §4 -- facing +/- 0x4000 scatter,
    // alternating via (counter & 1)].
    auto all_free = [](int, int) { return true; };
    // Transport facing North (0.0): counter 0 -> +0x4000 = East -> (11,10);
    // counter 1 -> -0x4000 = West -> (9,10).
    const Cellxy c0 = eject_cell(10, 10, 0.0, 0, all_free);
    CHECK(c0.x == 11);
    CHECK(c0.y == 10);
    const Cellxy c1 = eject_cell(10, 10, 0.0, 1, all_free);
    CHECK(c1.x == 9);
    CHECK(c1.y == 10);
    // Spawn-default facing South (32768.0): the sides flip -- counter 0 ->
    // West, counter 1 -> East.
    const Cellxy s0 = eject_cell(10, 10, 32768.0, 0, all_free);
    CHECK(s0.x == 9);
    CHECK(s0.y == 10);
    const Cellxy s1 = eject_cell(10, 10, 32768.0, 1, all_free);
    CHECK(s1.x == 11);
    CHECK(s1.y == 10);
    // Deterministic: identical inputs, identical answer.
    const Cellxy again = eject_cell(10, 10, 0.0, 0, all_free);
    CHECK(again.x == c0.x);
    CHECK(again.y == c0.y);
}

TEST_CASE("cargo: eject_cell falls back to the nearest free neighbour and signals HOLD when boxed") {
    // Primary (East of a North-facing transport at counter 0) blocked -> the
    // r=1 row-major fallback finds (9,9) first (dy=-1 row, dx ascending)
    // [RE-GATE APPLIED: transport-cargo.md §4 -- documented deviation:
    // FUN_00481090's fine placement is unpinned; first free nearby cell].
    auto blocked_primary = [](int x, int y) { return !(x == 11 && y == 10); };
    const Cellxy f = eject_cell(10, 10, 0.0, 0, blocked_primary);
    CHECK(f.x == 9);
    CHECK(f.y == 9);
    // r=1 fully blocked -> the r=2 perimeter row-major scan -> (8,8).
    auto r1_blocked = [](int x, int y) {
        return std::max(std::abs(x - 10), std::abs(y - 10)) >= 2;
    };
    const Cellxy f2 = eject_cell(10, 10, 0.0, 0, r1_blocked);
    CHECK(f2.x == 8);
    CHECK(f2.y == 8);
    // Fully boxed -> {-1,-1} (the unload HOLD signal: put-back/retry [§4]).
    const Cellxy none = eject_cell(10, 10, 0.0, 0, [](int, int) { return false; });
    CHECK(none.x == -1);
    CHECK(none.y == -1);
}

TEST_CASE("cargo: next_eject is LIFO -- the LAST-boarded passenger leaves first") {
    Unit t = transport_unit(5, 2);
    CHECK(next_eject(t) == -1);                  // empty -> nothing to eject
    t.cargo = {11, 12, 13, 14, 15};              // boarded A,B,C,D,E
    std::vector<int> out;
    while (next_eject(t) != -1) {                // drain the way step_cargo does
        out.push_back(next_eject(t));
        t.cargo.pop_back();
    }
    CHECK(out == std::vector<int>{15, 14, 13, 12, 11});   // E,D,C,B,A [RE-GATE
                                                 // APPLIED: transport-cargo.md
                                                 // §6 -- LIFO CONFIRMED]
}
