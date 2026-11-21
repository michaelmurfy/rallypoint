#include "roster2.hpp"
#include "orig_math.hpp"   // Net L0: the original's FastMath, never libm in the sim
#include <algorithm>
#include <cmath>
#include "factory.hpp"      // exit_cell
#include "production.hpp"   // build_frames

namespace ra2engine {

namespace {
// AdjacentCell[] @0x89E5F8 (static init 0x0049F200) at the wall facings
// {0,2,4,6} = N, E, S, W -- bit i of the wall mask [RE: roster2.md TL;DR 3].
constexpr int kWallDx[4] = {0, 1, 0, -1};
constexpr int kWallDy[4] = {-1, 0, 1, 0};
}  // namespace

int wall_mask(int x, int y, const std::function<bool(int, int)>& same_wall) {
    int m = 0;
    for (int i = 0; i < 4; ++i)
        if (same_wall(x + kWallDx[i], y + kWallDy[i])) m |= 1 << i;
    return m;
}

int wall_frame(int mask, int stage) {
    return std::max(0, stage) * 16 + (mask & 0xF);
}

int wall_stage(int hp, int max_hp, double condition_yellow) {
    if (max_hp <= 0) return 0;
    return (static_cast<double>(hp) / static_cast<double>(max_hp) <= condition_yellow) ? 1 : 0;
}

int wall_fill_range(double guard_range_cells) {
    const int leptons = om::ftol(guard_range_cells * 256.0);
    return leptons >> 8;
}

std::vector<Cellxy> wall_line_fill(int x, int y, int range,
                                   const std::function<bool(int, int)>& own_wall,
                                   const std::function<bool(int, int)>& can_place) {
    // FUN_00588180: per facing, local_2c counts placeable cells walked; meeting
    // an own wall fills those cells (start+dir .. start+count*dir) and breaks.
    std::vector<Cellxy> out;
    for (int i = 0; i < 4; ++i) {
        int count = 0;
        for (int k = 1; k <= range; ++k) {
            const int cx = x + kWallDx[i] * k, cy = y + kWallDy[i] * k;
            if (own_wall(cx, cy)) {
                for (int j = 1; j <= count; ++j)
                    out.push_back(Cellxy{x + kWallDx[i] * j, y + kWallDy[i] * j});
                break;
            }
            if (!can_place(cx, cy)) break;
            ++count;
            if (count >= range) break;
        }
    }
    return out;
}

int wall_build_frames(int cost, double build_speed, double coefficient) {
    // FUN_006F3D00: FILD the integer time, FMUL WallBuildSpeedCoefficient, ftol.
    return om::ftol(static_cast<double>(build_frames(cost, build_speed)) * coefficient);
}

bool water_bound_ok(int cx, int cy, int fw, int fh, const std::vector<std::uint8_t>& water,
                    int grid_w, int grid_h) {
    for (int j = 0; j < fh; ++j)
        for (int i = 0; i < fw; ++i) {
            const int x = cx + i, y = cy + j;
            if (x < 0 || y < 0 || x >= grid_w || y >= grid_h) return false;
            const std::size_t k = static_cast<std::size_t>(y) * grid_w + x;
            if (k >= water.size() || water[k] == 0) return false;
        }
    return true;
}

Cellxy naval_exit_cell(const Building& yard, const BuildingTypeDef& def,
                       const std::vector<std::uint8_t>& water, int grid_w, int grid_h,
                       const std::function<bool(int, int)>& free_extra) {
    auto free = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= grid_w || y >= grid_h) return false;
        if (x >= yard.cx && x < yard.cx + def.fw && y >= yard.cy && y < yard.cy + def.fh)
            return false;   // the yard's own foundation is never an exit
        const std::size_t k = static_cast<std::size_t>(y) * grid_w + x;
        if (k >= water.size() || water[k] == 0) return false;
        return free_extra(x, y);
    };
    return exit_cell(yard, def, free);
}

std::vector<int> unit_factory_types(const std::vector<bool>& unit_factory,
                                    const std::vector<bool>& naval, bool want_naval) {
    std::vector<int> v;
    for (std::size_t i = 0; i < unit_factory.size(); ++i) {
        const bool nv = i < naval.size() && naval[i];
        if (unit_factory[i] && nv == want_naval) v.push_back(static_cast<int>(i));
    }
    return v;
}

LeptonXY building_center_leptons(const Building& b) {
    return LeptonXY{b.cx * 256 + b.fw * 128, b.cy * 256 + b.fh * 128};
}

LeptonXY dock_pad_leptons(const Building& b, int pad, const std::vector<LeptonXY>& offsets) {
    LeptonXY c = building_center_leptons(b);
    if (pad >= 0 && pad < static_cast<int>(offsets.size())) {
        c.x += offsets[pad].x;
        c.y += offsets[pad].y;
    }
    return c;
}

Cellxy lepton_cell(LeptonXY p) {
    auto fl = [](int v) { return v >= 0 ? v / 256 : -((-v + 255) / 256); };
    return Cellxy{fl(p.x), fl(p.y)};
}

int reload_cycle_frames(double reload_rate_minutes) {
    return std::max(1, om::ftol(reload_rate_minutes * 900.0));
}

int first_free_pad(const std::vector<int>& pad_owner) {
    for (std::size_t i = 0; i < pad_owner.size(); ++i)
        if (pad_owner[i] < 0) return static_cast<int>(i);
    return -1;
}

namespace {
// Straight-line move toward (tx,ty) by at most `step`; true on arrival (snap).
bool move_toward(double& x, double& y, double tx, double ty, double step) {
    const double dx = tx - x, dy = ty - y;
    const double d = om::sqrt(dx * dx + dy * dy);
    if (d <= step || d == 0.0) { x = tx; y = ty; return true; }
    x += dx / d * step;
    y += dy / d * step;
    return false;
}
}  // namespace

SortieEvent step_sortie(Sortie& s, int reload_cycle) {
    switch (s.state) {
    case Sortie::State::Docked:
        if (s.ammo < s.max_ammo) {
            if (++s.reload_timer >= std::max(1, reload_cycle)) {
                s.reload_timer = 0;
                ++s.ammo;
                return SortieEvent::Reloaded;
            }
            return SortieEvent::None;
        }
        s.reload_timer = 0;
        if (s.has_target && s.ammo > 0) {
            s.state = Sortie::State::TakeOff;
            return SortieEvent::TookOff;
        }
        return SortieEvent::None;
    case Sortie::State::TakeOff:
        s.z = std::min(s.flight_level, s.z + s.climb_lpf);
        if (s.z >= s.flight_level) {
            s.state = Sortie::State::Cruise;
            return SortieEvent::ReachedCruise;
        }
        return SortieEvent::None;
    case Sortie::State::Cruise: {
        const double dx = s.target.x - s.x, dy = s.target.y - s.y;
        const double d = om::sqrt(dx * dx + dy * dy);
        if (d <= static_cast<double>(s.range_leptons)) {
            --s.ammo;
            ++s.shots;
            if (s.ammo <= 0) {
                s.ammo = 0;
                s.has_target = false;
                s.state = Sortie::State::Return;
            }
            return SortieEvent::Fired;
        }
        move_toward(s.x, s.y, s.target.x, s.target.y, s.speed_lpf);
        return SortieEvent::None;
    }
    case Sortie::State::Return: {
        const bool first = !s.rtb_started;   // the first homeward frame announces RTB
        s.rtb_started = true;
        if (move_toward(s.x, s.y, s.home.x, s.home.y, s.speed_lpf))
            s.state = Sortie::State::Landing;
        return first ? SortieEvent::StartedReturn : SortieEvent::None;
    }
    case Sortie::State::Landing:
        s.z = std::max(0, s.z - s.climb_lpf);
        if (s.z <= 0) {
            s.state = Sortie::State::Docked;
            s.reload_timer = 0;
            s.rtb_started = false;
            return SortieEvent::Landed;
        }
        return SortieEvent::None;
    }
    return SortieEvent::None;
}

const char* sortie_state_name(Sortie::State st) {
    switch (st) {
    case Sortie::State::Docked:  return "Docked";
    case Sortie::State::TakeOff: return "TakeOff";
    case Sortie::State::Cruise:  return "Cruise";
    case Sortie::State::Return:  return "Return";
    case Sortie::State::Landing: return "Landing";
    }
    return "?";
}

}  // namespace ra2engine
