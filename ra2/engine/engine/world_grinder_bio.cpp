// Yuri Grinder / Bio Reactor enter fates (skirmish CaptureManager victims).
//
// RE (Steam YR gamemd.exe):
//   BuildingType LoadFromINI @0x460971 / @0x4609A9: Grinding=+0x16AD,
//   UnitAbsorb=+0x16AE, InfantryAbsorb=+0x16AF; ExtraPower=+0xEE8 @0x4610B3.
//   InfantryClass::What_Action FUN_0051E010 @0x51E643 (and UnitClass @0x73F645 /
//   @0x7401AD): Grinding -> ACTION_ENTER 0x0B (else NoEnter).
//   Mission_Enter FUN_00519449 @0x51989F grinds; @0x51A2E5 / FUN_004598A0
//   gates InfantryAbsorb|UnitAbsorb boarding.
//   BuildingClass::Mission_Unload FUN_0044D880 @0x44D8B1 ejects absorb
//   occupants via UnloadOccupants FUN_00457DD0; What_Action @0x44731C shows
//   ACTION_DEPLOY when those buildings have occupants.
//
// Grinding: destroy the unit, pay Soylent to the building's house, free the
// MC slot without restoring the original house (unit is dead). InfantryAbsorb
// (YAPOWR): limbo into occupants, free the MC slot but keep the controller's
// house so ExtraPower still counts (YR: release capacity while they sit inside).
#include "world.hpp"

#include "grinder_bio.hpp"
#include "group_move.hpp"  // nearest_free_cells

#include <algorithm>
#include <cmath>

namespace ra2engine {
namespace {

bool live_unit(const Unit& u) { return u.alive && u.die_time < 0.0; }

bool live_bld(const Building& b) {
    return b.alive && b.state == Building::State::Idle && !b.selling;
}

int cheb_to_foundation(const Building& b, int x, int y) {
    int best = -1;
    for (const Cellxy& c : foundation_cells(b.cx, b.cy, b.fw, b.fh)) {
        const int d = std::max(std::abs(x - c.x), std::abs(y - c.y));
        if (best < 0 || d < best) best = d;
    }
    return best;
}

}  // namespace

void World::mc_free_keep_owner(int ctl, int vi) {
    if (ctl < 0 || ctl >= static_cast<int>(units_.size())) return;
    if (vi < 0 || vi >= static_cast<int>(units_.size())) return;
    Unit& cu = units_[static_cast<std::size_t>(ctl)];
    Unit& vu = units_[static_cast<std::size_t>(vi)];
    auto& vv = cu.w7.mc_victims;
    vv.erase(std::remove(vv.begin(), vv.end(), vi), vv.end());
    if (vu.w7.mc_controller != ctl) return;
    vu.w7.mc_controller = -1;
    vu.w7.mc_orig_house = -1;
}

bool World::can_grind(int ui, int bi) const {
    if (ui < 0 || ui >= static_cast<int>(units_.size())) return false;
    if (bi < 0 || bi >= static_cast<int>(buildings_.size())) return false;
    const Unit& u = units_[static_cast<std::size_t>(ui)];
    const Building& b = buildings_[static_cast<std::size_t>(bi)];
    if (!live_unit(u) || u.is_aircraft || u.cargo_of >= 0) return false;
    if (!live_bld(b) || b.house != u.house) return false;
    if (b.type < 0 || b.type >= static_cast<int>(btypes().size())) return false;
    return grinder_bio::type_grinds(btypes()[static_cast<std::size_t>(b.type)]);
}

bool World::grind_now(int ui, int bi) {
    if (!can_grind(ui, bi)) return false;
    Unit& u = units_[static_cast<std::size_t>(ui)];
    const Building& b = buildings_[static_cast<std::size_t>(bi)];
    const bool human = is_human(b.house);
    const int pay = grinder_bio::grind_refund(u.vet.cost, u.vet.soylent, repair_rules_.refund_percent,
                                              human);
    if (u.w7.mc_controller >= 0) mc_free_keep_owner(u.w7.mc_controller, ui);
    deposit_credits(b.house, static_cast<double>(pay));
    ability_kill_unit(ui);
    return true;
}

bool World::can_absorb(int ui, int bi) const {
    if (ui < 0 || ui >= static_cast<int>(units_.size())) return false;
    if (bi < 0 || bi >= static_cast<int>(buildings_.size())) return false;
    const Unit& u = units_[static_cast<std::size_t>(ui)];
    const Building& b = buildings_[static_cast<std::size_t>(bi)];
    if (!live_unit(u) || u.is_vehicle || u.is_aircraft || u.cargo_of >= 0) return false;
    if (!live_bld(b) || b.house != u.house) return false;
    if (b.type < 0 || b.type >= static_cast<int>(btypes().size())) return false;
    return grinder_bio::absorb_has_room(btypes()[static_cast<std::size_t>(b.type)], b);
}

bool World::absorb_now(int ui, int bi) {
    if (!can_absorb(ui, bi)) return false;
    Unit& u = units_[static_cast<std::size_t>(ui)];
    if (u.w7.mc_controller >= 0) mc_free_keep_owner(u.w7.mc_controller, ui);
    Building& b = buildings_[static_cast<std::size_t>(bi)];
    occ_.release_all_of(ui, -1, -1);
    u.path.clear();
    u.path_idx = 0;
    u.target = -1;
    u.target_building = -1;
    u.occupy_target = -1;
    u.cargo_of = kGarrisonCargo;
    u.garrison_of = bi;
    b.occupants.push_back(ui);
    garrison_any_ = true;
    return true;
}

void World::order_grind(int ui, int bi) {
    if (!can_grind(ui, bi)) return;
    garrison_any_ = true;
    Unit& u = units_[static_cast<std::size_t>(ui)];
    u.target = -1;
    u.target_building = -1;
    u.attack_order = false;
    u.enter_target = -1;
    u.capture_target = -1;
    u.infiltrate_target = -1;
    u.attack_cell_x = u.attack_cell_y = -1;
    u.occupy_target = bi;
    const Building& b = buildings_[static_cast<std::size_t>(bi)];
    if (cheb_to_foundation(b, u.cell_x, u.cell_y) <= 1) return;
    int nx = b.cx, ny = b.cy, best = -1;
    for (const Cellxy& c : foundation_cells(b.cx, b.cy, b.fw, b.fh)) {
        const int d = std::max(std::abs(u.cell_x - c.x), std::abs(u.cell_y - c.y));
        if (best < 0 || d < best) {
            best = d;
            nx = c.x;
            ny = c.y;
        }
    }
    const std::vector<Cellxy> free = nearest_free_cells({nx, ny}, 8, passable_, grid_w_, grid_h_, occ_);
    for (const Cellxy& g : free) {
        order_unit_move(ui, g.x, g.y);
        if (units_[static_cast<std::size_t>(ui)].moving()) break;
    }
}

void World::order_absorb(int ui, int bi) {
    if (!can_absorb(ui, bi)) return;
    garrison_any_ = true;
    Unit& u = units_[static_cast<std::size_t>(ui)];
    u.target = -1;
    u.target_building = -1;
    u.attack_order = false;
    u.enter_target = -1;
    u.capture_target = -1;
    u.infiltrate_target = -1;
    u.attack_cell_x = u.attack_cell_y = -1;
    u.occupy_target = bi;
    const Building& b = buildings_[static_cast<std::size_t>(bi)];
    if (cheb_to_foundation(b, u.cell_x, u.cell_y) <= 1) return;
    int nx = b.cx, ny = b.cy, best = -1;
    for (const Cellxy& c : foundation_cells(b.cx, b.cy, b.fw, b.fh)) {
        const int d = std::max(std::abs(u.cell_x - c.x), std::abs(u.cell_y - c.y));
        if (best < 0 || d < best) {
            best = d;
            nx = c.x;
            ny = c.y;
        }
    }
    const std::vector<Cellxy> free = nearest_free_cells({nx, ny}, 8, passable_, grid_w_, grid_h_, occ_);
    for (const Cellxy& g : free) {
        order_unit_move(ui, g.x, g.y);
        if (units_[static_cast<std::size_t>(ui)].moving()) break;
    }
}

}  // namespace ra2engine
