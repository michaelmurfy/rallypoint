#include "doctest.h"
#include "production.hpp"

using namespace ra2engine;

// W5-E2 [RE: FUN_00452350 GoOffline]: a powered-down consumer draws nothing.
TEST_CASE("power: a powered-down building drops out of the house drain") {
    std::vector<BuildingTypeDef> types(2);
    types[0].power = 100;
    types[1].power = -40;
    std::vector<Building> bs(2);
    bs[0].type = 0; bs[0].state = Building::State::Idle;
    bs[1].type = 1; bs[1].state = Building::State::Idle;
    CHECK(power_totals(bs, types, 0).drain == 40);
    bs[1].power_off = true;
    CHECK(power_totals(bs, types, 0).drain == 0);
    CHECK(power_totals(bs, types, 0).output == 100);
}
