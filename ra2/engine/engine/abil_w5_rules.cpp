// Wave-5 abilities (agent C): rulesmd -> BuildingTypeDef::w5 / UnitTypeDef::w5.
// [RE: docs/re/abilities-w5.md]
#include "abil_w5_rules.hpp"
#include "orig_math.hpp"   // Net L0: om::ftol (_ftol 0x7C4D00) for double->int

#include <algorithm>
#include <cctype>
#include <vector>

#include "ra2assets/rules.hpp"

namespace ra2engine {

namespace {

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> list_of(const ra2assets::Rules& rules, const std::string& sec, const char* key) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] {
        std::size_t a = 0, b = cur.size();
        while (a < b && std::isspace(static_cast<unsigned char>(cur[a]))) ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(cur[b - 1]))) --b;
        if (b > a) out.push_back(upper(cur.substr(a, b - a)));
        cur.clear();
    };
    for (char c : rules.section_string(sec, key, "")) {
        if (c == ',') flush();
        else cur += c;
    }
    flush();
    return out;
}

}  // namespace

void fill_building_abilities(const ra2assets::Rules& rules, const std::string& id, BuildingTypeDef& d) {
    BldTypeW5& w = d.w5;
    const std::string uid = upper(id);
    w.radar = rules.section_flag(id, "Radar", false);
    w.can_c4 = rules.section_flag(id, "CanC4", true);   // W7-01-22
    // [RE: abilities-w5.md §1] BuildingClass::Infiltrate (FUN_004571E0) tests,
    // in this order: Radar (+0x16A4) -> owner shroud reset; Power (+0xEE0) >= 1
    // -> blackout; listed in Rules [AI] BuildTech (+0x920) -> stolen tech by
    // AIBasePlanningSide (+0x6D0); SuperWeapon (+0x16F0) != -1 -> SW reset;
    // Storage (+0x800) > 0 -> cash steal; Factory (+0xEB8) UnitType 0x28 /
    // InfantryType 0x10 -> veteran production; anything else does nothing.
    {
        const int power = om::ftol(rules.section_double(id, "Power", 0.0));
        const std::vector<std::string> tech = list_of(rules, "AI", "BuildTech");
        const std::string factory = upper(rules.section_string(id, "Factory", ""));
        int p = 0;   // None
        if (w.radar) p = 5;                                   // RadarLab
        else if (power >= 1) p = 2;                           // PowerPlant
        else if (std::find(tech.begin(), tech.end(), uid) != tech.end()) {
            const int side = om::ftol(rules.section_double(id, "AIBasePlanningSide", 0.0));
            p = side == 0 ? kSpyStolenAllied : side == 1 ? kSpyStolenSoviet : kSpyStolenThird;
        } else if (!rules.section_string(id, "SuperWeapon", "").empty()) p = 3;   // Superweapon
        else if (rules.section_double(id, "Storage", 0.0) > 0.0) p = 1;          // Refinery
        else if (factory == "UNITTYPE" || factory == "INFANTRYTYPE") p = 4;      // Factory
        w.spy_purpose = p;
    }
    w.prism = !uid.empty() && uid == upper(rules.general_string("PrismType", ""));
    w.overpowerable = rules.section_flag(id, "Overpowerable", false);
    w.gap_radius = rules.section_flag(id, "GapGenerator", false)
                       ? om::ftol(rules.section_double(id, "GapRadiusInCells", 0.0))
                       : 0;
    w.inf_self_heal = om::ftol(rules.section_double(id, "InfantryGainSelfHeal", 0.0));
    w.unit_self_heal = om::ftol(rules.section_double(id, "UnitsGainSelfHeal", 0.0));
    w.cloning = rules.section_flag(id, "Cloning", false);
    w.psychic_radius = om::ftol(rules.section_double(id, "PsychicDetectionRadius", 0.0));
    w.sensors_sight = rules.section_flag(id, "SensorArray", false)
                          ? om::ftol(rules.section_double(id, "SensorsSight", 0.0))
                          : 0;
}

std::uint8_t requires_stolen_bits(const ra2assets::Rules& rules, const std::string& id) {
    std::uint8_t m = 0;
    if (rules.section_flag(id, "RequiresStolenAlliedTech", false)) m |= kStolenAllied;
    if (rules.section_flag(id, "RequiresStolenSovietTech", false)) m |= kStolenSoviet;
    if (rules.section_flag(id, "RequiresStolenThirdTech", false)) m |= kStolenThird;
    return m;
}

}  // namespace ra2engine
