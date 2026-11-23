#pragma once
// Rallypoint: Magnetron / Magnetic Beam (Yuri's Revenge).
// Pure helpers for IsLocomotor lift-hold-drop. Key names match rulesmd
// (IsMagBeam, IsLocomotor, FallingDamageMultiplier, CurrentStrengthDamage,
// Weight, JumpjetControls CruiseHeight/Climb). Fixed-point leptons / milli-Weight.

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "orig_math.hpp"
#include "weapon.hpp"

namespace ra2engine {
namespace magnetron {

inline std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

inline bool is_magnet_locomotor_clsid(const std::string& clsid) {
    return upper(clsid).rfind("{92612C46", 0) == 0;
}

struct MagRules {
    int cruise_height = 500;
    int climb = 5;
    int drag = 64;
    int falling_damage_milli = 1000;
    bool current_strength_damage = true;
};

struct MagVictim {
    bool lifted = false;
    int altitude = 0;
    int weight_milli = 1000;
    int hp = 0;
    int max_hp = 0;
    bool is_vehicle = false;
    bool is_aircraft = false;
    bool is_infantry = false;
    bool is_naval = false;
    bool amphibious = false;
    int armor_ix = 0;
};

inline bool can_lift(const MagVictim& v, const std::vector<int>& warhead_verses) {
    if (!v.is_vehicle && !v.is_naval) return false;
    if (v.is_aircraft || v.is_infantry) return false;
    if (v.hp <= 0) return false;
    return verses_allows(warhead_verses, v.armor_ix);
}

inline int climb_step(const MagRules& r, int weight_milli) {
    const int w = std::max(1, weight_milli);
    return std::max(1, static_cast<int>((static_cast<std::int64_t>(r.climb) * 1000) / w));
}

inline int rise(const MagRules& r, int altitude, int weight_milli) {
    if (altitude >= r.cruise_height) return r.cruise_height;
    const int next = altitude + climb_step(r, weight_milli);
    return next > r.cruise_height ? r.cruise_height : next;
}

inline bool at_cruise(const MagRules& r, int altitude) { return altitude >= r.cruise_height; }

inline bool in_beam_range(int hx, int hy, int vx, int vy, double range_cells) {
    const int dx = hx > vx ? hx - vx : vx - hx;
    const int dy = hy > vy ? hy - vy : vy - hy;
    return static_cast<double>(dx > dy ? dx : dy) <= range_cells + 1e-9;
}

inline int drop_damage(const MagRules& r, const MagVictim& v) {
    const int base = r.current_strength_damage ? std::max(0, v.hp) : std::max(0, v.max_hp);
    return om::ftol(static_cast<double>(base) * (static_cast<double>(r.falling_damage_milli) / 1000.0));
}

inline bool drop_destroys_in_water(const MagVictim& v, bool cell_is_water) {
    if (!cell_is_water) return false;
    if (v.amphibious || v.is_naval) return false;
    return v.is_vehicle;
}

inline bool drag_toward(int& fx, int& fy, int tx, int ty, int drag) {
    const int dx = tx - fx, dy = ty - fy;
    const int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    if (adx <= drag && ady <= drag) {
        fx = tx;
        fy = ty;
        return true;
    }
    auto step = [drag](int d) { return d > drag ? drag : d < -drag ? -drag : d; };
    fx += step(dx);
    fy += step(dy);
    return false;
}

}  // namespace magnetron
}  // namespace ra2engine
