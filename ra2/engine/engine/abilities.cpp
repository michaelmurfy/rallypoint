#include "abilities.hpp"
#include "orig_math.hpp"   // Net L0: the original's FastMath, never libm in the sim

#include <algorithm>
#include <cmath>

namespace ra2engine {

// --- §A Radiation -----------------------------------------------------------

int rad_spread_leptons(int spread_cells) { return spread_cells * 256 + 128; }  // 0x65AF17..1A

double rad_cell_amount(int d_leptons, int spread_leptons, int rad_level) {
    // 0x65B4xx: dVar = d <= SL ? (SL - d)/SL * RadLevel : 0.0 ([0x7E1800] = 0.0)
    if (spread_leptons <= 0 || d_leptons > spread_leptons) return 0.0;
    return (static_cast<double>(spread_leptons - d_leptons) / static_cast<double>(spread_leptons)) *
           static_cast<double>(rad_level);
}

int rad_level_steps(int duration, int level_delay) {
    return level_delay > 0 ? duration / level_delay : 0;   // 0x65B05A (int div)
}

int rad_add_current(int rad_level, int time_left, int duration) {
    // 0x65AF74: FILD level; FILD left; FMULP; FILD duration; FDIVP; __ftol
    if (duration <= 0) return 0;
    return ability_ftol(static_cast<double>(rad_level) * static_cast<double>(time_left) /
                static_cast<double>(duration));
}

double rad_deactivate_amount(double amt, int steps, int time_left, int level_delay) {
    if (steps <= 0 || level_delay <= 0) return 0.0;
    return (amt / static_cast<double>(steps)) * static_cast<double>(time_left / level_delay + 1);
}

int rad_damage(double cell_level, const AbilityRules& r) {
    // 0x487BC0: ability_ftol(min(cell.RadLevel, RadLevelMax))
    const double capped = std::min(cell_level, static_cast<double>(r.rad_level_max));
    const int lvl = ability_ftol(capped);
    if (lvl <= 0) return 0;                                 // 0x4DA502: CMP EAX,0; JLE skip
    return ability_ftol(static_cast<double>(lvl) * r.rad_level_factor);   // FILD; FMUL; __ftol
}

bool rad_apply_due(std::uint64_t frame, const AbilityRules& r) {
    return r.rad_application_delay > 0 &&
           frame % static_cast<std::uint64_t>(r.rad_application_delay) == 0;
}

void RadField::init(int w, int h) {
    w_ = std::max(0, w);
    h_ = std::max(0, h);
    cell_.assign(static_cast<std::size_t>(w_) * static_cast<std::size_t>(h_), 0.0);
    sites_.clear();
}

double RadField::level_at(int x, int y) const {
    if (x < 0 || y < 0 || x >= w_ || y >= h_) return 0.0;
    return cell_[static_cast<std::size_t>(y) * w_ + x];
}

// Radiate (add) / DecreaseRadiation (remove amt*scale) over the site square.
// scale is 1 for Radiate, 1/LevelSteps for a decay step, the Deactivate
// multiple for Add's teardown. Decrements clamp at 0 (0x487C10).
void RadField::radiate(const RadSite& s, double scale, bool add) {
    for (int y = s.cy - s.spread; y <= s.cy + s.spread; ++y) {
        for (int x = s.cx - s.spread; x <= s.cx + s.spread; ++x) {
            if (x < 0 || y < 0 || x >= w_ || y >= h_) continue;
            const double dx = (x - s.cx) * kAbilityLeptonsPerCell;
            const double dy = (y - s.cy) * kAbilityLeptonsPerCell;
            const int d = ability_ftol(om::sqrt(dx * dx + dy * dy));   // FUN_004CAB60 + __ftol
            const double amt = rad_cell_amount(d, s.spread_leptons, s.rad_level);
            double& c = cell_[static_cast<std::size_t>(y) * w_ + x];
            if (add) {
                c += amt * scale;
            } else {
                c -= amt * scale;
                if (c < 0.0) c = 0.0;
            }
        }
    }
}

void RadField::detonate(int cx, int cy, int spread, int level, std::uint64_t frame,
                        const AbilityRules& r) {
    if (empty() || level <= 0) return;
    if (cx < 0 || cy < 0 || cx >= w_ || cy >= h_) return;
    auto arm = [&](RadSite& s) {
        // SetRadLevel (0x65AF30) + Activate (0x65AFC0)
        s.duration = s.rad_level * r.rad_duration_multiple;
        s.time_left = s.duration;
        s.timer_start = frame;
        s.timer_left = r.rad_level_delay;
        s.level_steps = rad_level_steps(s.duration, r.rad_level_delay);
        radiate(s, 1.0, /*add=*/true);
    };
    for (RadSite& s : sites_) {
        if (!s.alive || s.cx != cx || s.cy != cy) continue;
        // Add (0x65AF70): cur = ability_ftol(L*left/dur); Deactivate; L = cur + level.
        const int cur = rad_add_current(s.rad_level, s.time_left, s.duration);
        if (s.level_steps > 0 && r.rad_level_delay > 0)
            radiate(s,
                    static_cast<double>(s.time_left / r.rad_level_delay + 1) /
                        static_cast<double>(s.level_steps),
                    /*add=*/false);
        s.rad_level = cur + level;
        arm(s);
        return;
    }
    RadSite s;
    s.cx = cx;
    s.cy = cy;
    s.spread = spread;
    s.spread_leptons = rad_spread_leptons(spread);
    s.rad_level = level;
    arm(s);
    sites_.push_back(s);
}

void RadField::step(std::uint64_t frame, const AbilityRules& r) {
    for (RadSite& s : sites_) {
        if (!s.alive) continue;
        // RadSite::AI (0x65B240): DEC RadTimeLeft; LevelTimer expired ->
        // DecreaseRadiation + restart; delete at TimeLeft <= 0.
        s.time_left -= 1;
        if (frame - s.timer_start >= static_cast<std::uint64_t>(s.timer_left)) {
            if (s.level_steps > 0)
                radiate(s, 1.0 / static_cast<double>(s.level_steps), /*add=*/false);
            s.timer_start = frame;
            s.timer_left = r.rad_level_delay;
        }
        if (s.time_left <= 0) s.alive = false;   // vtbl+0x20(1): the site is deleted
    }
}

int RadField::live_sites() const {
    int n = 0;
    for (const RadSite& s : sites_)
        if (s.alive) ++n;
    return n;
}

double RadField::total() const {
    double t = 0.0;
    for (double c : cell_) t += c;
    return t;
}

// --- §B -----------------------------------------------------------------------
int suppression_frames(int damage, int threshold) {
    return damage > threshold ? damage * 2 - threshold : 0;   // 0x4D72C6
}

// --- §C -----------------------------------------------------------------------
int teleport_delay_frames(int dist_leptons, const AbilityRules& r) {
    int delay = 0;
    if (r.chrono_trigger && r.chrono_distance_factor > 0)
        delay = dist_leptons / r.chrono_distance_factor;      // IDIV [+0xBF4]
    if (delay <= r.chrono_minimum_delay) delay = r.chrono_minimum_delay;   // 0x71882F
    if (dist_leptons < r.chrono_range_minimum) delay = r.chrono_minimum_delay;  // 0x718865
    return delay;
}

int warp_remaining_init(int type_strength) { return type_strength * 10; }   // [+0xA0]*10

// --- §D -----------------------------------------------------------------------
int c4_delay_frames(double c4_delay_minutes) {
    return ability_ftol(c4_delay_minutes * 900.0);   // FMUL [0x7E17F8]=900.0; __ftol
}

// --- §E -----------------------------------------------------------------------
bool bomb_due(std::int64_t detonation_frame, std::uint64_t frame) {
    return detonation_frame >= 0 && detonation_frame < static_cast<std::int64_t>(frame);
}

// --- §G -----------------------------------------------------------------------
int ifv_weapon_index(int ifv_mode, int weapon_count) {
    if (ifv_mode < 0 || ifv_mode >= 18 || ifv_mode >= weapon_count) return 0;
    return ifv_mode;
}

double area_scale(double d_leptons, double radius_leptons, double pmax) {
    if (d_leptons > radius_leptons) return -1.0;
    const double frac = radius_leptons > 0.0 ? d_leptons / radius_leptons : 0.0;
    return 1.0 - (1.0 - pmax) * frac;
}

}  // namespace ra2engine
