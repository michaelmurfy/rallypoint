#pragma once
// Wave-5 abilities (agent C): the rulesmd side -- resolves BuildingTypeDef::w5
// and UnitTypeDef::w5 from a type's section [RE: docs/re/abilities-w5.md].
#include <cstdint>
#include <string>

#include "building.hpp"

namespace ra2assets { class Rules; }

namespace ra2engine {

// Fill d.w5 (spy purpose, radar, prism, overpowerable, gap radius, tech
// self-heal, cloning, psychic/sensor radii). Called at the end of
// fill_building_weapons, so every building-roster path (skirmish roster, map
// civilians/tech buildings, campaign types) gets it.
void fill_building_abilities(const ra2assets::Rules& rules, const std::string& id, BuildingTypeDef& d);

// RequiresStolenAlliedTech / SovietTech / ThirdTech as kStolen* bits.
std::uint8_t requires_stolen_bits(const ra2assets::Rules& rules, const std::string& id);

}  // namespace ra2engine
