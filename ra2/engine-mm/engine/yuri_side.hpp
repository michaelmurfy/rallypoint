#pragma once
// Rallypoint: the third side (Yuri). Yuri's Revenge rulesmd names three sides
// in [Sides] (GDI = the Allied countries, Nod = the Soviet ones, ThirdSide =
// YuriCountry) and each country says which one it is with Side= ([Americans]
// Side=GDI, [YuriCountry] Side=ThirdSide). The per-side [General]/[AI] keys
// follow the same three names: AlliedCrew / SovietCrew / ThirdCrew,
// AlliedBaseDefenses / SovietBaseDefenses / ThirdBaseDefenses, and the
// AIBasePlanningSide= values 0 / 1 / 2 (public modding docs, modenc). Pure: no
// Rules, no assets, so the tests use it directly.

#include <cctype>
#include <string>
#include <vector>

#include "world.hpp"

namespace ra2engine {
namespace yuri {

inline std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// A country's rules Side= value -> the on-screen side. Unknown -> Neutral.
inline World::Side side_of_rules_side(const std::string& v) {
    const std::string s = upper(v);
    if (s == "GDI") return World::Side::Allied;
    if (s == "NOD") return World::Side::Soviet;
    if (s == "THIRDSIDE") return World::Side::Yuri;
    return World::Side::Neutral;
}

// The [Sides] key that lists a side's countries.
inline const char* sides_key(World::Side s) {
    return s == World::Side::Soviet ? "Nod" : s == World::Side::Yuri ? "ThirdSide" : "GDI";
}

// The rules prefix of the per-side keys ("<prefix>Crew", "<prefix>BaseDefenses",
// "<prefix>BaseDefenseCounts", "<prefix>SurvivorDivisor").
inline const char* key_prefix(World::Side s) {
    return s == World::Side::Soviet ? "Soviet" : s == World::Side::Yuri ? "Third" : "Allied";
}

// AIBasePlanningSide= ("0 for Good, 1 for Evil"; YR's Yuri structures say 2).
inline int planning_side(World::Side s) {
    return s == World::Side::Soviet ? 1 : s == World::Side::Yuri ? 2 : 0;
}

// aimd [AITriggerTypes] side field: 0 any, 1 Allied, 2 Soviet, 3 Yuri.
inline int trigger_side(World::Side s) {
    return s == World::Side::Soviet ? 2 : s == World::Side::Yuri ? 3 : 1;
}

// The side's stock country (a slot whose country is blank or of another side).
inline const char* default_country(World::Side s) {
    return s == World::Side::Soviet ? "Russians" : s == World::Side::Yuri ? "YuriCountry" : "Americans";
}

// Survivor / divisor index used by World (0 Allied, 1 Soviet, 2 Yuri).
inline int side_index(World::Side s) {
    return s == World::Side::Soviet ? 1 : s == World::Side::Yuri ? 2 : 0;
}

inline const char* side_name(World::Side s) {
    return s == World::Side::Soviet ? "Soviet" : s == World::Side::Yuri ? "Yuri" : s == World::Side::Allied ? "Allied"
                                                                                                           : "Neutral";
}

// Comma list (an Owner= / Prerequisite= value; a ';' comment cut) -> upper-
// cased tokens without blanks.
inline std::vector<std::string> tokens(std::string v) {
    const std::size_t sc = v.find(';');
    if (sc != std::string::npos) v.erase(sc);
    std::vector<std::string> out;
    std::string cur;
    for (char c : v + ",") {
        if (c != ',') {
            if (!std::isspace(static_cast<unsigned char>(c))) cur.push_back(c);
            continue;
        }
        if (!cur.empty()) out.push_back(upper(cur));
        cur.clear();
    }
    return out;
}

// Is a rules type part of the third side's own set? YR's Owner= lists name
// every country for most buildings (side membership comes from the
// prerequisites), so "Owner has YuriCountry" alone is not enough. A type is
// Yuri's when its Owner= names YuriCountry and
//   * Owner= names nobody else (YACNST, PCV, INIT, LTNK, ...), or
//   * AIBasePlanningSide=2 (YAPOWR, NAPSIS, NACLON, NATBNK, ...), or
//   * one of its prerequisites is such a Yuri building (YURI: YABRCK,NAPSIS).
// `yuri_buildings` holds the upper-cased ids found by the first two rules.
inline bool is_yuri_type(const std::string& owner, const std::string& planning_side_value,
                         const std::string& prerequisite, const std::vector<std::string>& yuri_buildings) {
    const std::vector<std::string> own = tokens(owner);
    bool yuri = false;
    for (const std::string& o : own) yuri |= o == "YURICOUNTRY";
    if (!yuri) return false;
    if (own.size() == 1) return true;
    const std::vector<std::string> ps = tokens(planning_side_value);
    if (!ps.empty() && ps[0] == "2") return true;
    for (const std::string& p : tokens(prerequisite))
        for (const std::string& b : yuri_buildings)
            if (p == b) return true;
    return false;
}

}  // namespace yuri
}  // namespace ra2engine
