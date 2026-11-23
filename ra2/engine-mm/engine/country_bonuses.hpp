#pragma once
// Rallypoint: per-country (HouseTypeClass) bonuses from gamemd FUN_00511850
// (HouseTypeClass::ReadFromINI) and rulesmd [Countries] / [<Country>] keys.
//
// Stock YR leaves Cost*/Veteran*/Armor* commented out (identity multipliers),
// but the exe still loads them. Country uniques (TNKD, SNIPE, AMRADR, GTGCAN,
// BEAG, DESO, …) come from Owner= / RequiredHouses= / ForbiddenHouses=.
// Pure: Rules only, so doctests use synthetic INI sections.

#include <cctype>
#include <cmath>
#include <string>
#include <vector>

#include "ra2assets/rules.hpp"
#include "unit_combat.hpp"

namespace ra2engine {
namespace country {

enum class Kind { Infantry, Unit, Aircraft, Building, Defense };

inline std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

inline std::vector<std::string> tokens(std::string v) {
    const std::size_t sc = v.find(';');
    if (sc != std::string::npos) v.erase(sc);
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] {
        std::size_t a = cur.find_first_not_of(" \t\r\n");
        std::size_t b = cur.find_last_not_of(" \t\r\n");
        if (a != std::string::npos) out.push_back(upper(cur.substr(a, b - a + 1)));
        cur.clear();
    };
    for (char c : v) {
        if (c == ',') flush();
        else cur.push_back(c);
    }
    flush();
    return out;
}

inline bool list_has(const std::vector<std::string>& l, const std::string& tok) {
    const std::string u = upper(tok);
    for (const std::string& s : l)
        if (s == u) return true;
    return false;
}

// Owner / RequiredHouses / ForbiddenHouses (same meaning as aiside::owned_by
// without the AIBasePlanningSide gate — used for start pools and sidebar).
inline bool country_allows(const ra2assets::Rules& rules, const std::string& id,
                           const std::string& country) {
    const std::vector<std::string> owner = tokens(rules.section_string(id, "Owner", ""));
    if (!owner.empty() && !list_has(owner, country)) return false;
    const std::vector<std::string> req = tokens(rules.section_string(id, "RequiredHouses", ""));
    if (!req.empty() && !list_has(req, country)) return false;
    return !list_has(tokens(rules.section_string(id, "ForbiddenHouses", "")), country);
}

// Loaded HouseType multipliers + veteran type lists [RE: FUN_00511850].
struct Bonuses {
    double cost = 1.0;         // Cost=
    double armor = 1.0;        // Armor= (damage received is divided by this)
    double rof = 1.0;          // ROF= (larger = slower fire)
    double firepower = 1.0;    // Firepower=
    double groundspeed = 1.0;  // Groundspeed=
    double airspeed = 1.0;     // Airspeed=
    double build_time = 1.0;   // BuildTime=
    double income = 1.0;       // IncomeMult=

    double cost_infantry = 1.0, cost_units = 1.0, cost_aircraft = 1.0;
    double cost_buildings = 1.0, cost_defenses = 1.0;
    double armor_infantry = 1.0, armor_units = 1.0, armor_aircraft = 1.0;
    double armor_buildings = 1.0, armor_defenses = 1.0;
    double speed_infantry = 1.0, speed_units = 1.0, speed_aircraft = 1.0;
    double build_time_infantry = 1.0, build_time_units = 1.0, build_time_aircraft = 1.0;
    double build_time_buildings = 1.0, build_time_defenses = 1.0;

    std::vector<std::string> veteran_infantry;  // VeteranInfantry=
    std::vector<std::string> veteran_units;     // VeteranUnits=
    std::vector<std::string> veteran_aircraft;  // VeteranAircraft=
};

inline Bonuses parse(const ra2assets::Rules& rules, const std::string& country) {
    Bonuses b;
    if (country.empty()) return b;
    auto d = [&](const char* key, double def) {
        return rules.section_double(country, key, def);
    };
    b.cost = d("Cost", 1.0);
    b.armor = d("Armor", 1.0);
    b.rof = d("ROF", 1.0);
    b.firepower = d("Firepower", 1.0);
    b.groundspeed = d("Groundspeed", 1.0);
    b.airspeed = d("Airspeed", 1.0);
    b.build_time = d("BuildTime", 1.0);
    b.income = d("IncomeMult", 1.0);

    b.cost_infantry = d("CostInfantryMult", 1.0);
    b.cost_units = d("CostUnitsMult", 1.0);
    b.cost_aircraft = d("CostAircraftMult", 1.0);
    b.cost_buildings = d("CostBuildingsMult", 1.0);
    b.cost_defenses = d("CostDefensesMult", 1.0);

    b.armor_infantry = d("ArmorInfantryMult", 1.0);
    b.armor_units = d("ArmorUnitsMult", 1.0);
    b.armor_aircraft = d("ArmorAircraftMult", 1.0);
    b.armor_buildings = d("ArmorBuildingsMult", 1.0);
    b.armor_defenses = d("ArmorDefensesMult", 1.0);

    b.speed_infantry = d("SpeedInfantryMult", 1.0);
    b.speed_units = d("SpeedUnitsMult", 1.0);
    b.speed_aircraft = d("SpeedAircraftMult", 1.0);

    b.build_time_infantry = d("BuildTimeInfantryMult", 1.0);
    b.build_time_units = d("BuildTimeUnitsMult", 1.0);
    b.build_time_aircraft = d("BuildTimeAircraftMult", 1.0);
    b.build_time_buildings = d("BuildTimeBuildingsMult", 1.0);
    b.build_time_defenses = d("BuildTimeDefensesMult", 1.0);

    b.veteran_infantry = tokens(rules.section_string(country, "VeteranInfantry", ""));
    b.veteran_units = tokens(rules.section_string(country, "VeteranUnits", ""));
    b.veteran_aircraft = tokens(rules.section_string(country, "VeteranAircraft", ""));
    return b;
}

inline Kind kind_of(const UnitCombat& c) {
    if (c.is_aircraft) return Kind::Aircraft;
    if (c.is_infantry) return Kind::Infantry;
    return Kind::Unit;  // vehicles + naval
}

inline Kind kind_of_building(bool is_defense) {
    return is_defense ? Kind::Defense : Kind::Building;
}

inline double cost_mul(const Bonuses& b, Kind k) {
    double m = b.cost;
    switch (k) {
    case Kind::Infantry: return m * b.cost_infantry;
    case Kind::Unit: return m * b.cost_units;
    case Kind::Aircraft: return m * b.cost_aircraft;
    case Kind::Building: return m * b.cost_buildings;
    case Kind::Defense: return m * b.cost_defenses;
    }
    return m;
}

inline double armor_mul(const Bonuses& b, Kind k) {
    double m = b.armor;
    switch (k) {
    case Kind::Infantry: return m * b.armor_infantry;
    case Kind::Unit: return m * b.armor_units;
    case Kind::Aircraft: return m * b.armor_aircraft;
    case Kind::Building: return m * b.armor_buildings;
    case Kind::Defense: return m * b.armor_defenses;
    }
    return m;
}

inline double speed_mul(const Bonuses& b, Kind k) {
    switch (k) {
    case Kind::Infantry: return b.groundspeed * b.speed_infantry;
    case Kind::Unit: return b.groundspeed * b.speed_units;
    case Kind::Aircraft: return b.airspeed * b.speed_aircraft;
    case Kind::Building:
    case Kind::Defense: return 1.0;
    }
    return 1.0;
}

inline double build_time_mul(const Bonuses& b, Kind k) {
    double m = b.build_time;
    switch (k) {
    case Kind::Infantry: return m * b.build_time_infantry;
    case Kind::Unit: return m * b.build_time_units;
    case Kind::Aircraft: return m * b.build_time_aircraft;
    case Kind::Building: return m * b.build_time_buildings;
    case Kind::Defense: return m * b.build_time_defenses;
    }
    return m;
}

inline bool starts_veteran(const Bonuses& b, const std::string& rules_id, Kind k) {
    if (rules_id.empty()) return false;
    switch (k) {
    case Kind::Infantry: return list_has(b.veteran_infantry, rules_id);
    case Kind::Unit: return list_has(b.veteran_units, rules_id);
    case Kind::Aircraft: return list_has(b.veteran_aircraft, rules_id);
    case Kind::Building:
    case Kind::Defense: return false;
    }
    return false;
}

// Scale a unit's spawn stats from country bonuses. Sets veteran_rank when the
// type is listed (leaves a higher rank alone). Identity bonuses are a no-op.
inline void apply_to_combat(UnitCombat& c, const Bonuses& b) {
    const Kind k = kind_of(c);
    if (c.veteran_rank == 0 && c.trainable && starts_veteran(b, c.tg.rules_id, k))
        c.veteran_rank = 1;
    const double sm = speed_mul(b, k);
    if (sm != 1.0) {
        c.speed_cps *= sm;
        c.flight_speed *= sm;
    }
    const double am = armor_mul(b, k);
    if (am != 1.0 && am > 0.0) {
        // Armor= divides incoming damage; bake as more hit points so combat
        // paths that never read a house armor mul stay correct.
        const int ns = static_cast<int>(std::lround(static_cast<double>(c.strength) * am));
        c.strength = ns > 0 ? ns : 1;
    }
}

inline int scale_cost(int base_cost, const Bonuses& b, Kind k) {
    const double m = cost_mul(b, k);
    if (m == 1.0) return base_cost;
    const int c = static_cast<int>(std::lround(static_cast<double>(base_cost) * m));
    return c > 0 ? c : 1;
}

}  // namespace country
}  // namespace ra2engine
