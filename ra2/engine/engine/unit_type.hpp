#pragma once
// One producible unit type's production-relevant stats, resolved ONCE by the
// caller (main.cpp from Rules/Art, or a test by hand) — the UnitType mirror of
// BuildingTypeDef. Index into the unit roster is the queue's `type` key.
// Calibration: docs/re/production.md §1.5 (units have NO FreeUnit subtraction:
// charge == cost — the FreeUnit charge subtraction is BuildingType-only). No SDL/OS.
#include <string>
#include <vector>
#include "unit_combat.hpp"   // UnitCombat
#include "abil_w5.hpp"       // wave-5 stolen tech (UnitTypeW5)

namespace ra2engine {

struct UnitTypeDef {
    std::string id;                         // "E1" / "MTNK"
    int cost = 0;                           // Rules Cost=; charge == cost (§1.5)
    int tech_level = 0;                     // Rules TechLevel=; <=0 = never buildable
    bool is_vehicle = false;                // Infantry queue vs Vehicle queue
    bool chrono_miner = false;              // CMIN flag (add_harvester path); unused for combat units
    std::vector<std::string> prerequisite;  // uppercased tokens (BARRACKS/FACTORY groups or ids)
    std::string cameo;                      // Art Cameo= ("" = never in sidebar)
    UnitCombat combat;                      // resolved stats for World::add_combat_unit/add_harvester
    // Gap owner A (real-play roster, G-22/G-24): which factory queue builds it.
    // Both false = Infantry (is_vehicle=false) or ground Vehicle (is_vehicle).
    bool naval = false;                     // rules Naval=yes  -> Naval Yard / Ships queue
    bool aircraft = false;                  // [AircraftTypes]  -> Airfield / Aircraft queue
    bool harvester = false;                 // rules Harvester=yes -> launched as a miner
    int storage = 0;                        // rules Storage= (bales) for a harvester
    UnitTypeW5 w5;                          // wave-5: RequiresStolen*Tech gate
};

}  // namespace ra2engine
