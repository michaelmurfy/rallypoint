// Skirmish / multiplayer CaptureManager (W7-08-14 / W7-10-02..04).
// Campaign keeps its own poll_mind_control (teams, DecideUnitFate, Track::mc_orig);
// it sets campaign_mind_control_ so this pass is a no-op there. Same rules as the
// campaign path: MindControl primary warhead, shot via try_fire (mc_shot),
// ImmuneToPsionics / Iron Curtain / already-controlled refused, capacity from
// weapon Damage (InfiniteMindControl lifts it), FreeAll on death / board / Temporal.

#include "world.hpp"

#include <algorithm>

namespace ra2engine {
namespace {

bool mc_live(const Unit& u) { return u.alive && u.die_time < 0.0; }

bool mc_boarded(const Unit& u) {
    return (u.cargo_of >= 0 && u.cargo_of != kParachuteCargo) || u.garrison_of >= 0;
}

}  // namespace

void World::set_campaign_mind_control(bool on) { campaign_mind_control_ = on; }

std::vector<std::pair<int, int>> World::mind_control_pairs() const {
    std::vector<std::pair<int, int>> out;
    for (std::size_t i = 0; i < units_.size(); ++i) {
        for (int v : units_[i].w7.mc_victims)
            if (v >= 0 && v < static_cast<int>(units_.size()))
                out.push_back({static_cast<int>(i), v});
    }
    return out;
}

void World::mc_release(int ctl, int vi) {
    if (ctl < 0 || ctl >= static_cast<int>(units_.size())) return;
    if (vi < 0 || vi >= static_cast<int>(units_.size())) return;
    Unit& cu = units_[static_cast<std::size_t>(ctl)];
    Unit& vu = units_[static_cast<std::size_t>(vi)];
    auto& vv = cu.w7.mc_victims;
    vv.erase(std::remove(vv.begin(), vv.end(), vi), vv.end());
    if (vu.w7.mc_controller != ctl) return;
    vu.w7.mc_controller = -1;
    const int orig = vu.w7.mc_orig_house;
    vu.w7.mc_orig_house = -1;
    if (orig < 0 || !mc_live(vu)) return;
    set_unit_owner(vi, orig);
}

void World::mc_release_all(int ctl) {
    if (ctl < 0 || ctl >= static_cast<int>(units_.size())) return;
    const std::vector<int> vv = units_[static_cast<std::size_t>(ctl)].w7.mc_victims;
    for (int v : vv) mc_release(ctl, v);
    units_[static_cast<std::size_t>(ctl)].w7.mc_victims.clear();
}

bool World::mc_capture(int ctl, int vi) {
    if (ctl < 0 || ctl >= static_cast<int>(units_.size())) return false;
    if (vi < 0 || vi >= static_cast<int>(units_.size())) return false;
    Unit& u = units_[static_cast<std::size_t>(ctl)];
    Unit& v = units_[static_cast<std::size_t>(vi)];
    if (!mc_live(u) || !mc_live(v) || mc_boarded(v) || v.house == u.house || v.is_aircraft)
        return false;
    if (!psionic_target_ok(vi) || v.an.iron_left_frames > 0) return false;
    if (v.w7.mc_controller >= 0) return false;   // W7-08-11: no stealing
    const int cap = std::max(1, u.w7.t.mc_capacity);
    const bool infinite = u.w7.t.mc_infinite;
    const bool full = !infinite && static_cast<int>(u.w7.mc_victims.size()) >= cap;
    if (full) {
        if (cap != 1 || infinite) return false;
        mc_release(ctl, u.w7.mc_victims.front());   // Max == 1: swap
    }
    if (v.w7.mc_orig_house < 0) v.w7.mc_orig_house = v.house;
    set_unit_owner(vi, u.house);
    v.w7.mc_controller = ctl;
    u.w7.mc_victims.push_back(vi);
    u.target = -1;
    u.attack_order = false;
    return true;
}

// --- Psychic Tower (Rallypoint) ------------------------------------------------
bool World::tower_mc_ok(int bi, int vi) const {
    if (campaign_mind_control_) return false;   // the campaign poll only knows unit controllers
    if (bi < 0 || bi >= static_cast<int>(buildings_.size())) return false;
    if (vi < 0 || vi >= static_cast<int>(units_.size())) return false;
    const Building& b = buildings_[static_cast<std::size_t>(bi)];
    const Unit& v = units_[static_cast<std::size_t>(vi)];
    if (!b.alive || b.state == Building::State::Dead || b.type < 0 ||
        b.type >= static_cast<int>(btypes().size()))
        return false;
    const BuildingTypeDef& t = btypes()[static_cast<std::size_t>(b.type)];
    if (t.mc_capacity <= 0 || !mc_live(v) || mc_boarded(v) || v.house == b.house || v.is_aircraft) return false;
    if (!psionic_target_ok(vi) || v.an.iron_left_frames > 0 || v.w7.mc_controller >= 0) return false;
    return t.mc_infinite || static_cast<int>(b.mc_victims.size()) < t.mc_capacity;
}

bool World::tower_capture(int bi, int vi) {
    if (!tower_mc_ok(bi, vi)) return false;
    Building& b = buildings_[static_cast<std::size_t>(bi)];
    Unit& v = units_[static_cast<std::size_t>(vi)];
    if (v.w7.mc_orig_house < 0) v.w7.mc_orig_house = v.house;
    set_unit_owner(vi, b.house);
    v.w7.mc_controller = kTowerCtl + bi;
    b.mc_victims.push_back(vi);
    return true;
}

void World::tower_release_all(int bi) {
    Building& b = buildings_[static_cast<std::size_t>(bi)];
    const std::vector<int> vv = b.mc_victims;
    b.mc_victims.clear();
    for (int vi : vv) {
        if (vi < 0 || vi >= static_cast<int>(units_.size())) continue;
        Unit& vu = units_[static_cast<std::size_t>(vi)];
        if (vu.w7.mc_controller != kTowerCtl + bi) continue;
        vu.w7.mc_controller = -1;
        const int orig = vu.w7.mc_orig_house;
        vu.w7.mc_orig_house = -1;
        if (orig >= 0 && mc_live(vu)) set_unit_owner(vi, orig);
    }
}

void World::step_tower_mind_control() {
    for (std::size_t i = 0; i < buildings_.size(); ++i) {
        Building& b = buildings_[i];
        if (b.mc_victims.empty()) continue;
        const int bi = static_cast<int>(i);
        // A dead, captured, sold or powered-down tower lets everyone go.
        if (!b.alive || b.state == Building::State::Dead || b.def_offline || b.power_off) {
            tower_release_all(bi);
            continue;
        }
        auto& lst = b.mc_victims;
        for (std::size_t k = 0; k < lst.size();) {
            const int vi = lst[k];
            bool drop = vi < 0 || vi >= static_cast<int>(units_.size());
            if (!drop) {
                Unit& vu = units_[static_cast<std::size_t>(vi)];
                if (vu.w7.mc_controller != kTowerCtl + bi) drop = true;      // someone else holds it now
                else if (!mc_live(vu)) { vu.w7.mc_controller = -1; vu.w7.mc_orig_house = -1; drop = true; }
                else if (mc_boarded(vu)) { vu.w7.mc_controller = -1; vu.w7.mc_orig_house = -1; drop = true; }   // absorbed: keeps the owner
            }
            if (drop) lst.erase(lst.begin() + static_cast<std::ptrdiff_t>(k));
            else ++k;
        }
    }
}

void World::step_mind_control() {
    if (campaign_mind_control_) return;
    step_tower_mind_control();
    for (std::size_t i = 0; i < units_.size(); ++i) {
        Unit& u = units_[i];
        const int ci = static_cast<int>(i);
        if (!u.w7.mc_victims.empty()) {
            const std::vector<int> vv = u.w7.mc_victims;
            for (int v : vv) {
                if (v < 0 || v >= static_cast<int>(units_.size())) continue;
                const Unit& vu = units_[static_cast<std::size_t>(v)];
                if (!mc_live(vu) || vu.w7.mc_controller != ci) {
                    auto& lst = u.w7.mc_victims;
                    lst.erase(std::remove(lst.begin(), lst.end(), v), lst.end());
                    if (!mc_live(vu) && v < static_cast<int>(units_.size())) {
                        if (vu.w7.mc_controller == ci)
                            units_[static_cast<std::size_t>(v)].w7.mc_controller = -1;
                        units_[static_cast<std::size_t>(v)].w7.mc_orig_house = -1;
                    }
                } else if (mc_boarded(vu)) {
                    // YAPOWR InfantryAbsorb: free the slot, keep controller house
                    // (YR: victims stay inside and still make ExtraPower).
                    bool absorb = false;
                    if (vu.garrison_of >= 0 &&
                        vu.garrison_of < static_cast<int>(buildings_.size())) {
                        const Building& bb = buildings_[static_cast<std::size_t>(vu.garrison_of)];
                        if (bb.type >= 0 && bb.type < static_cast<int>(btypes().size()))
                            absorb = btypes()[static_cast<std::size_t>(bb.type)].infantry_absorb;
                    }
                    if (absorb) mc_free_keep_owner(ci, v);
                    else mc_release(ci, v);
                }
            }
        }
        if (!mc_live(u) || u.cargo_of >= 0 || u.warp_remaining > 0) {
            if (!u.w7.mc_victims.empty()) mc_release_all(ci);
            u.w7.mc_shot = -1;
            u.w_mind_control = false;
            continue;
        }
        u.w_mind_control = u.w7.t.mind_control;
        if (!u.w_mind_control) continue;
        const int cap = std::max(1, u.w7.t.mc_capacity);
        const bool infinite = u.w7.t.mc_infinite;
        const bool full = !infinite && static_cast<int>(u.w7.mc_victims.size()) >= cap;
        if (u.target >= 0 && u.target < static_cast<int>(units_.size())) {
            const Unit& tv = units_[static_cast<std::size_t>(u.target)];
            if (!psionic_target_ok(u.target) || tv.an.iron_left_frames > 0 ||
                (tv.w7.mc_controller >= 0 && tv.w7.mc_controller != ci)) {
                u.target = -1;
                u.attack_order = false;
            }
        }
        if (full && !u.attack_order && u.target >= 0) u.target = -1;
        const int vi = u.w7.mc_shot;
        u.w7.mc_shot = -1;
        if (vi < 0) continue;
        mc_capture(ci, vi);
    }
}

}  // namespace ra2engine
