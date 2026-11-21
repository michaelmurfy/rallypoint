// gF: MCV deploy / undeploy -- a World member TU (the world_abilities.cpp split
// precedent). Every value and rule below is recovered from gamemd.exe; see
// docs/re/mcv-deploy.md for the listings and confidence tags.
//
//   UnitClass deploy mission   FUN_007383E0  (unfold: facing gate -> building)
//   UnitClass try-to-deploy    FUN_00737D50  (order: legal here, else ring search)
//   BuildingType legality      vtbl+0xA8 -> FUN_00464AB0 -> FUN_007154F0
//   AdjacentCell[NW]           0x0089E614 = (-1,-1) (static init FUN_0049F200)
//   DeployFacing               BuildingType+0xEDC, default 0x80 (FUN_0045DD80)
//   Undeploy (sell ConYard)    BuildingClass::Can_Undeploy FUN_0044F5C0
//                              (repair-sell.md §B6)
#include <algorithm>
#include <cmath>

#include "building.hpp"
#include "orig_math.hpp"   // Net L0: om::ftol (_ftol 0x7C4D00) for double->int
#include "world.hpp"

namespace ra2engine {

namespace {

// The recovered 36-entry relocation ring of FUN_00737D50 (CellStruct table at
// 0x00B1BF78..0x00B1C004, statically initialised in that function): first the
// rows NORTH of the MCV (y -1..-4, x 0,+1,-1,+2,-2), then SOUTH (y +1..+4),
// then the row itself west (-1..-4) and east (+1..+4). Order is load-bearing:
// the first legal entry wins.
struct Off { int dx, dy; };
constexpr Off kDeployRing[] = {
    {0, -1}, {0, -2}, {1, -2}, {-1, -2}, {0, -3}, {1, -3}, {-1, -3}, {2, -3}, {-2, -3},
    {0, -4}, {1, -4}, {-1, -4}, {2, -4}, {-2, -4},
    {0, 1},  {0, 2},  {1, 2},  {-1, 2},  {0, 3},  {1, 3},  {-1, 3},  {2, 3},  {-2, 3},
    {0, 4},  {1, 4},  {-1, 4},  {2, 4},  {-2, 4},
    {-1, 0}, {-2, 0}, {-3, 0}, {-4, 0}, {1, 0},  {2, 0},  {3, 0},  {4, 0},
};

bool live_on_map(const Unit& u) { return u.alive && u.die_time < 0.0 && u.cargo_of < 0; }

// 16-bit facing -> the 8-bit DirStruct value the deploy gate compares
// (FUN_007383E0: ((raw >> 7) + 1) >> 1 & 0xFF).
int facing8(double f16) {
    int raw = static_cast<int>(om::lround(f16)) & 0xFFFF;
    return (((raw >> 7) + 1) >> 1) & 0xFF;
}

}  // namespace

bool World::is_mcv(int idx) const {
    if (idx < 0 || idx >= static_cast<int>(units_.size())) return false;
    const Unit& u = units_[idx];
    return live_on_map(u) && u.deploys_into >= 0 &&
           u.deploys_into < static_cast<int>(btypes().size());
}

bool World::can_deploy_at(int idx, int x, int y, int* ox, int* oy) const {
    if (!is_mcv(idx)) return false;
    const BuildingTypeDef& d = btypes()[static_cast<std::size_t>(units_[idx].deploys_into)];
    // [FUN_007383E0 @0x738480..0x7384DE] origin = cell, shifted by
    // AdjacentCell[NW] only when the foundation exceeds 2 in either axis.
    int cx = x, cy = y;
    if (d.fw > 2 || d.fh > 2) { cx -= 1; cy -= 1; }
    if (ox) *ox = cx;
    if (oy) *oy = cy;
    // The MCV lifts its own occupancy (vtbl+0x124 Mark(0)) before the test;
    // every other occupied cell blocks. Adjacency is NOT part of the test.
    Occupancy occ = occ_;
    occ.release_all_of(idx, -1, -1);
    const PlacementCheck chk = placement_check(cx, cy, d.fw, d.fh, units_[idx].house, d.adjacent,
                                               passable_, grid_w_, grid_h_, occ, buildings_,
                                               /*bypass_adjacency=*/true, nullptr,
                                               build_mask(/*wall=*/false));   // W8-03-01 [RE: FUN_0047C530 via FUN_007154F0]
    return chk.ok;
}

bool World::order_deploy_mcv(int idx) {
    if (!is_mcv(idx)) return false;
    Unit& u = units_[idx];
    if (u.frozen || u.is_aircraft) return false;
    deploy_any_ = true;
    if (can_deploy_at(idx, u.cell_x, u.cell_y)) {
        // Legal here: stop and run the deploy mission (turn, then unfold).
        if (u.moving()) {
            occ_.release_all_of(idx, u.cell_x, u.cell_y);
            occ_.set(u.cell_x, u.cell_y, idx);
            u.path.clear();
            u.path_idx = 0;
            u.fx = u.cell_x;
            u.fy = u.cell_y;
        }
        u.target = -1;
        u.target_building = -1;
        u.attack_order = false;
        u.deploy_mission = 1;
        return true;
    }
    // [FUN_00737D50] Illegal here: walk the recovered ring, drive to the first
    // legal spot and deploy on arrival.
    for (const Off& o : kDeployRing) {
        const int x = u.cell_x + o.dx, y = u.cell_y + o.dy;
        if (x < 0 || y < 0 || x >= grid_w_ || y >= grid_h_) continue;
        if (!can_deploy_at(idx, x, y)) continue;
        if (!walkable(x, y) || !occ_.can_enter(x, y, idx)) continue;
        u.target = -1;
        u.target_building = -1;
        u.attack_order = false;
        u.deploy_goal_x = x;
        u.deploy_goal_y = y;
        u.deploy_mission = 2;
        order_unit_move(idx, x, y);
        return true;
    }
    // Nowhere to go: the unfold attempt fails on the spot (EVA for a human).
    deploy_events_.push_back({DeployEvent::Kind::CannotDeploy, idx, -1, u.house});
    u.deploy_mission = 0;
    return false;
}

void World::finish_deploy(int idx) {
    Unit& u = units_[idx];
    const int type = u.deploys_into;
    const BuildingTypeDef& d = btypes()[static_cast<std::size_t>(type)];
    int ox = 0, oy = 0;
    can_deploy_at(idx, u.cell_x, u.cell_y, &ox, &oy);
    const double ratio = u.max_hp > 0 ? static_cast<double>(u.hp) / u.max_hp : 1.0;
    const int house = u.house;
    // The unit leaves first (its cell becomes part of the foundation).
    consume_unit(idx);
    units_[idx].deployed_away = true;
    const double buildup = (type < static_cast<int>(buildup_totals_.size()))
                               ? buildup_totals_[static_cast<std::size_t>(type)]
                               : 0.0;
    // Mission Construction (0x12) = the buildup animation [FUN_007383E0 ->
    // vtbl+0x1E8(0x12)]; instant only when the type has no buildup art.
    const int b = add_building(type, ox, oy, d.fw, d.fh, house, d.strength, d.armor_ix, buildup,
                               /*instant=*/buildup <= 0.0, d.sight, d.grants_map_reveal);
    // Health carried as a ratio, floored at 1 [FUN_007C4D00 ftol, min 1].
    Building& nb = buildings_[static_cast<std::size_t>(b)];
    nb.hp = std::max(1, om::ftol(ratio * nb.max_hp));
    nb.ai_repairable = !human_commanded(house);   // fv7c [RE: FUN_007383E0, non-human deploy]
    deploy_events_.push_back({DeployEvent::Kind::Deployed, idx, b, house});
}

void World::step_deploy(double dt) {
    if (!deploy_any_) return;
    for (int i = 0; i < static_cast<int>(units_.size()); ++i) {
        Unit& u = units_[i];
        if (u.deploy_mission == 0) continue;
        if (!live_on_map(u) || u.deploys_into < 0) { u.deploy_mission = 0; continue; }
        if (u.deploy_mission == 2) {
            if (u.moving()) {
                // A new player order re-pathed the unit elsewhere: drop the deploy.
                const Cellxy& g = u.path.back();
                if (g.x != u.deploy_goal_x || g.y != u.deploy_goal_y) u.deploy_mission = 0;
                continue;
            }
            u.deploy_mission = 1;   // arrived (or stopped): attempt the unfold here
        }
        if (u.moving()) { u.deploy_mission = 0; continue; }   // re-ordered mid-turn
        if (!can_deploy_at(i, u.cell_x, u.cell_y)) {
            // [FUN_007383E0 @0x738500] failure on the spot -> EVA_CannotDeployHere
            // (human player) and back to Guard.
            deploy_events_.push_back({DeployEvent::Kind::CannotDeploy, i, -1, u.house});
            u.deploy_mission = 0;
            continue;
        }
        const int want8 = btypes()[static_cast<std::size_t>(u.deploys_into)].deploy_facing & 0xFF;
        if (facing8(u.body_facing) != want8) {
            const double want16 = static_cast<double>(want8 << 8);
            const double step = u.rot_u16_per_sec > 0.0 ? u.rot_u16_per_sec * dt : 65536.0;
            u.body_facing = turn_toward(u.body_facing, want16, step);
            u.turret_facing = u.body_facing;
            if (facing8(u.body_facing) != want8) continue;   // still turning
        }
        finish_deploy(i);
    }
}

std::vector<World::DeployEvent> World::take_deploy_events() {
    std::vector<DeployEvent> out;
    out.swap(deploy_events_);
    return out;
}

void World::set_undeploy_unit(int btype, const UnitCombat& c) {
    for (std::size_t k = 0; k < undeploy_types_.size(); ++k)
        if (undeploy_types_[k] == btype) { undeploy_units_[k] = c; return; }
    undeploy_types_.push_back(btype);
    undeploy_units_.push_back(c);
}

// [RE: repair-sell.md §B6, FUN_0044F5C0 + Mission_Selling 0x00449C2A]: a
// ConstructionYard packs back into its UndeploysInto unit only in a
// multiplayer/skirmish session, for a human owner, with MCVRedeploys on, and
// when the unit can be placed; health ratio carried (min 1). Returns true when
// the building was converted (the caller then skips the refund).
bool World::try_undeploy(int b_idx) {
    if (!skirmish_session_ || !mcv_redeploys_) return false;
    Building& b = buildings_[static_cast<std::size_t>(b_idx)];
    if (ai_enabled(b.house)) return false;   // !Is_Human(Owner) -> plain refund
    const BuildingTypeDef& t = btypes()[static_cast<std::size_t>(b.type)];
    if (!t.conyard) return false;
    std::size_t k = 0;
    while (k < undeploy_types_.size() && undeploy_types_[k] != b.type) ++k;
    if (k == undeploy_types_.size()) return false;
    // Inverse of the deploy offset: the MCV stands where it unfolded.
    int ux = b.cx, uy = b.cy;
    if (b.fw > 2 || b.fh > 2) { ux += 1; uy += 1; }
    if (!walkable(ux, uy) || !occ_.is_free(ux, uy)) return false;   // fallback: refund
    const double ratio = b.max_hp > 0 ? static_cast<double>(b.hp) / b.max_hp : 1.0;
    const int house = b.house;
    Unit& u = add_combat_unit(ux, uy, house, undeploy_units_[k]);
    u.hp = std::max(1, om::ftol(ratio * u.max_hp));
    u.body_facing = u.turret_facing =
        static_cast<double>((t.deploy_facing & 0xFF) << 8);
    const int ui = static_cast<int>(units_.size()) - 1;
    deploy_events_.push_back({DeployEvent::Kind::Undeployed, ui, b_idx, house});
    return true;
}

void World::reshroud_area(int house, int cx, int cy, int radius) {
    if (house < 0 || house >= house_count()) return;
    if (house < static_cast<int>(map_reveal_.size()) && map_reveal_[house]) return;
    if (explored_.size() <= static_cast<std::size_t>(house) || explored_[house].empty()) return;
    for (int y = cy - radius; y <= cy + radius; ++y)
        for (int x = cx - radius; x <= cx + radius; ++x) {
            if (x < 0 || y < 0 || x >= grid_w_ || y >= grid_h_) continue;
            if ((x - cx) * (x - cx) + (y - cy) * (y - cy) > radius * radius) continue;
            const std::size_t i = static_cast<std::size_t>(y) * grid_w_ + x;
            explored_[house][i] = 0;
            if (!visible_[house].empty()) visible_[house][i] = 0;
        }
}

}  // namespace ra2engine
