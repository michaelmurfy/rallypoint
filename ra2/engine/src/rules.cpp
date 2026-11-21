#include "ra2assets/rules.hpp"
#include <cctype>
#include <cstdlib>
#include <vector>

// Thin wrapper over Ini -- see rules.hpp for the field/lifetime contract.
// This file intentionally has no parsing logic of its own beyond a couple of
// small local helpers (bool-ish "yes"/"true" values, comma-split) that Ini
// doesn't provide: Ini already handles case-insensitivity, defaults, and
// duplicate-key semantics for everything else.
namespace ra2assets {

namespace {

// The game's INI bool (INIClass::Get_Bool, FUN_0050f410 -- docs/re/
// ini-reader.md): toupper(first char) in {1,T,Y} -> true, {0,F,N} -> false,
// anything else -> the caller's default (false here; section_flag passes its own).
int ini_bool(const std::string& v) {
    if (v.empty()) return -1;
    const char c = (char)std::toupper((unsigned char)v[0]);
    if (c == '1' || c == 'T' || c == 'Y') return 1;
    if (c == '0' || c == 'F' || c == 'N') return 0;
    return -1;
}
bool is_truthy(const std::string& v) { return ini_bool(v) == 1; }

// Comma-split `v`, trimming whitespace and uppercasing each token; empty
// tokens (from "" or trailing commas) are skipped. Used by prerequisite()/
// prerequisite_group() to normalize section-id lists.
std::vector<std::string> split_trim_upper(const std::string& v) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos <= v.size()) {
        size_t comma = v.find(',', pos);
        std::string tok = v.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        size_t b = tok.find_first_not_of(" \t");
        size_t e = tok.find_last_not_of(" \t");
        if (b != std::string::npos) {
            std::string trimmed = tok.substr(b, e - b + 1);
            for (char& c : trimmed) c = (char)std::toupper((unsigned char)c);
            out.push_back(trimmed);
        }
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return out;
}

// True iff `id` (case-insensitive) is a VALUE in [section]'s key/value list.
// The [InfantryTypes]/[VehicleTypes]/[BuildingTypes] list sections map numbered
// keys ("0=E1") to ids, so membership is tested against the values, not keys.
bool in_list_section(const ra2assets::Ini& ini, const std::string& section,
                     const std::string& id) {
    for (const auto& kv : ini.section(section)) {
        const std::string& val = kv.second;  // Ini already cut the ';' comment
        if (val.size() != id.size()) continue;
        bool eq = true;
        for (std::size_t i = 0; i < id.size(); ++i)
            if (std::toupper((unsigned char)val[i]) != std::toupper((unsigned char)id[i])) {
                eq = false; break;
            }
        if (eq) return true;
    }
    return false;
}

} // namespace

Rules Rules::load(const Ini& ini) {
    return Rules(ini);
}

std::string Rules::image(const std::string& unit_id) const {
    return ini_->get(unit_id, "Image", unit_id);
}

int Rules::speed(const std::string& unit_id) const {
    return ini_->get_int(unit_id, "Speed", 0);
}

int Rules::strength(const std::string& unit_id) const {
    return ini_->get_int(unit_id, "Strength", 0);
}

int Rules::sight(const std::string& unit_id) const {
    // [RE: shroud.md §2] hard cap 10 cells; absent -> 0.
    int s = ini_->get_int(unit_id, "Sight", 0);
    return s > 10 ? 10 : s;
}

bool Rules::spy_sat(const std::string& id) const {
    // [RE: shroud.md §7A] SpySat=yes -> whole-map reveal provider (GASPYSAT).
    return is_truthy(ini_->get(id, "SpySat", ""));
}

std::string Rules::armor(const std::string& unit_id) const {
    std::string a = ini_->get(unit_id, "Armor", "");
    return a.empty() ? "none" : a;
}

std::string Rules::primary(const std::string& unit_id) const {
    return ini_->get(unit_id, "Primary", "");
}

std::string Rules::voice_select(const std::string& unit_id) const {
    return ini_->get(unit_id, "VoiceSelect", "");
}

std::string Rules::voice_move(const std::string& unit_id) const {
    return ini_->get(unit_id, "VoiceMove", "");
}

std::string Rules::voice_attack(const std::string& unit_id) const {
    return ini_->get(unit_id, "VoiceAttack", "");
}

std::string Rules::die_sound(const std::string& unit_id) const {
    return ini_->get(unit_id, "DieSound", "");
}

int Rules::weapon_damage(const std::string& w) const {
    return ini_->get_int(w, "Damage", 0);
}

int Rules::weapon_rof(const std::string& w) const {
    return ini_->get_int(w, "ROF", 0);
}

double Rules::weapon_range(const std::string& w) const {
    // Range= can be fractional (e.g. 120mm's Range=5.75) -- Ini::get_int
    // would truncate it, so parse the raw string with strtod instead.
    std::string v = ini_->get(w, "Range", "");
    if (v.empty()) return 0.0;
    return std::strtod(v.c_str(), nullptr);
}

std::string Rules::weapon_warhead(const std::string& w) const {
    return ini_->get(w, "Warhead", "");
}

std::string Rules::weapon_report(const std::string& w) const {
    return ini_->get(w, "Report", "");
}

std::string Rules::weapon_projectile(const std::string& w) const {
    return ini_->get(w, "Projectile", "");
}

int Rules::weapon_speed(const std::string& w) const {
    return ini_->get_int(w, "Speed", 0);
}

std::vector<int> Rules::verses(const std::string& warhead) const {
    // Verses=100%,80%,... -> percent ints, one per armor class, in ini order.
    // Tolerates spaces and a missing '%' (the game's parser reads the number).
    std::vector<int> out;
    std::string s = ini_->get(warhead, "Verses", "");
    if (s.empty()) return out;
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t comma = s.find(',', pos);
        std::string tok = s.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        size_t b = tok.find_first_not_of(" \t");
        size_t e = tok.find_last_not_of(" \t%");
        out.push_back(b == std::string::npos ? 0 : std::atoi(tok.substr(b, e - b + 1).c_str()));
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return out;
}

int Rules::rot(const std::string& unit_id) const {
    return ini_->get_int(unit_id, "ROT", 0);
}

bool Rules::has_turret(const std::string& unit_id) const {
    return is_truthy(ini_->get(unit_id, "Turret", ""));
}

std::string Rules::explosion_first(const std::string& unit_id) const {
    // Explosion=TWLT070,S_BANG48,... -- only the first entry (the actual
    // death VoxelAnim/animation) is needed today.
    std::string v = ini_->get(unit_id, "Explosion", "");
    if (v.empty()) return "";
    size_t comma = v.find(',');
    std::string first = (comma == std::string::npos) ? v : v.substr(0, comma);
    size_t b = first.find_first_not_of(" \t");
    size_t e = first.find_last_not_of(" \t");
    return b == std::string::npos ? "" : first.substr(b, e - b + 1);
}

int Rules::cost(const std::string& id) const {
    return ini_->get_int(id, "Cost", 0);
}

int Rules::power(const std::string& id) const {
    return ini_->get_int(id, "Power", 0);
}

int Rules::adjacent(const std::string& id) const {
    return ini_->get_int(id, "Adjacent", 0);
}

int Rules::tech_level(const std::string& id) const {
    return ini_->get_int(id, "TechLevel", 0);
}

bool Rules::is_construction_yard(const std::string& id) const {
    return is_truthy(ini_->get(id, "ConstructionYard", ""));
}

std::vector<std::string> Rules::prerequisite(const std::string& id) const {
    return split_trim_upper(ini_->get(id, "Prerequisite", ""));
}

std::vector<std::string> Rules::prerequisite_group(const std::string& group) const {
    return split_trim_upper(ini_->get("General", "Prerequisite" + group, ""));
}

std::string Rules::general_string(const std::string& key, const std::string& fallback) const {
    const std::string v = ini_->get("General", key, "");
    return v.empty() ? fallback : v;
}

double Rules::general_double(const std::string& key, double fallback) const {
    // [General] first (BuildSpeed/RefundPercent/LowPowerPenaltyModifier's
    // real home); [AudioVisual] second (ConditionYellow/ConditionRed's real
    // home -- see the header comment).
    std::string v = ini_->get("General", key, "");
    if (v.empty()) v = ini_->get("AudioVisual", key, "");
    if (v.empty()) return fallback;
    return std::strtod(v.c_str(), nullptr);
}

double Rules::section_double(const std::string& section, const std::string& key,
                             double fallback) const {
    // [RE: ai-base.md §2/§4] the [AI] + difficulty sections; same tolerant
    // strtod parse as general_double (trailing '%' tolerated by strtod stop).
    const std::string v = ini_->get(section, key, "");
    if (v.empty()) return fallback;
    return std::strtod(v.c_str(), nullptr);
}

bool Rules::section_flag(const std::string& id, const std::string& key, bool fallback) const {
    // [RE: repair-sell.md §A4/§B3/§B5] the caller owns the absent-key default:
    // ClickRepairable's is YES while Unsellable's and Crewed's are NO, so this
    // must NOT bake one in. Present values go through the same tolerant
    // is_truthy() as is_construction_yard/spy_sat/has_turret.
    const std::string v = ini_->get(id, key, "");
    const int b = ini_bool(v);
    return b < 0 ? fallback : b == 1;
}

std::string Rules::section_string(const std::string& section, const std::string& key,
                                  const std::string& fallback) const {
    const std::string v = ini_->get(section, key, "");   // comment cut + trimmed by Ini
    return v.empty() ? fallback : v;
}

int Rules::mp_money() const {
    return ini_->get_int("MultiplayerDialogSettings", "Money", 10000);
}

int Rules::value(const std::string& id) const {
    return ini_->get_int(id, "Value", 0);
}

double Rules::growth_percentage(const std::string& id) const {
    std::string v = ini_->get(id, "GrowthPercentage", "");
    if (v.empty()) return 0.0;
    return std::strtod(v.c_str(), nullptr);
}

double Rules::spread_percentage(const std::string& id) const {
    std::string v = ini_->get(id, "SpreadPercentage", "");
    if (v.empty()) return 0.0;
    return std::strtod(v.c_str(), nullptr);
}

int Rules::storage(const std::string& id) const {
    return ini_->get_int(id, "Storage", 0);
}

bool Rules::harvester(const std::string& id) const {
    return is_truthy(ini_->get(id, "Harvester", ""));
}

std::vector<std::string> Rules::dock(const std::string& id) const {
    return split_trim_upper(ini_->get(id, "Dock", ""));
}

std::string Rules::free_unit(const std::string& id) const {
    return ini_->get(id, "FreeUnit", "");
}

std::string Rules::locomotor(const std::string& id) const {
    // The real [ZEP] value "{92612C46-...} ;Jumpjet" arrives comment-free:
    // Ini::parse cuts at ';' like the game's INIClass::Load.
    return ini_->get(id, "Locomotor", "");
}

std::string Rules::speed_type(const std::string& id) const {
    return ini_->get(id, "SpeedType", "");
}

bool Rules::balloon_hover(const std::string& id) const {
    return is_truthy(ini_->get(id, "BalloonHover", "no"));
}

bool Rules::weapon_omnifire(const std::string& weapon) const {
    return is_truthy(ini_->get(weapon, "OmniFire", "no"));
}

int Rules::number_of_docks(const std::string& id) const {
    return ini_->get_int(id, "NumberOfDocks", 0);
}

double Rules::growth_rate_minutes() const {
    return general_double("GrowthRate", 5.0);
}

std::string Rules::unloading_class(const std::string& id) const {
    return ini_->get(id, "UnloadingClass", "");
}

bool Rules::is_infantry(const std::string& id) const {
    return in_list_section(*ini_, "InfantryTypes", id);
}

bool Rules::is_vehicle(const std::string& id) const {
    return in_list_section(*ini_, "VehicleTypes", id);
}

bool Rules::is_buildcat_combat(const std::string& id) const {
    // BuildCat=Combat -> Defense queue [RE-GATE APPLIED: production.md §1.1].
    // Case-insensitive value compare ("Combat"/"combat").
    const std::string v = ini_->get(id, "BuildCat", "");
    static const char kCombat[] = "combat";
    if (v.size() != 6) return false;
    for (std::size_t i = 0; i < 6; ++i)
        if (std::tolower((unsigned char)v[i]) != kCombat[i]) return false;
    return true;
}

Rules::BuildCategory Rules::build_category(const std::string& id) const {
    if (is_infantry(id)) return BuildCategory::Infantry;
    if (is_vehicle(id)) {
        // Naval=yes -> the Ships queue, out of scope this phase -> None
        // [RE-GATE APPLIED: production.md §1.1 -- Vehicles = [VehicleTypes]
        // member AND Naval==0; Naval==1 routes to the separate Ships slot].
        // Reuse is_truthy() rather than duplicate the yes/true literal list.
        // "Naval=yes ;GS" (DEST/DLPH/CARRIER/CDEST): Ini cuts the comment.
        return is_truthy(ini_->get(id, "Naval", ""))
                   ? BuildCategory::None
                   : BuildCategory::Vehicle;
    }
    if (in_list_section(*ini_, "BuildingTypes", id))
        return is_buildcat_combat(id) ? BuildCategory::Defense : BuildCategory::Structure;
    return BuildCategory::None;
}

} // namespace ra2assets
