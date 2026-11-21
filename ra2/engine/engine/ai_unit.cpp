// Wave-7 AI per-unit rules: the computer houses' Deployer-infantry auto-deploy
// (W7-01-08 / W7-02-04). (The ToProtect call for help, W7-04-18, moved to
// World::call_for_help in fv7c.) A World
// member TU split out of world.cpp; runs after step_ai_teams inside the
// ai_any_ firewall, and only for houses whose AiHouseConfig arms it
// (auto_deploy_frames >= 0 / roster_to_protect non-empty: the skirmish AI).
#include "world.hpp"
#include "combat.hpp"   // unit_lept_x/y/z, building_aim_lept

#include <algorithm>
#include <cstdint>
#include <vector>

namespace ra2engine {

namespace {

// Is (tx,ty,tz) within the DEPLOY weapon's Range of `u`? [RE: vtbl+0x3A8
// InRange(target, DeployFireWeapon) @0x5220A6; RE-FALLBACK: the plain 3-D
// lepton distance, without the elevation bonus of the active-weapon test].
bool in_deploy_range(const Unit& u, int tx, int ty, int tz) {
    // DeployFireWeapon (type+0x6A8): slot 1 for every stock deployer (GI Para,
    // GGI MissileLauncher; the elite slot once elite), slot 0 = the primary.
    double range = u.w_range;
    if (u.deploy_fire_slot == 1) {
        const WeaponSlot& w = (u.veteran_rank >= 2 && u.w2_elite.valid) ? u.w2_elite : u.w2;
        if (!w.valid) return false;
        range = w.range;
    }
    const long long r = (long long)(range * 256.0);
    const long long dx = tx - unit_lept_x(u), dy = ty - unit_lept_y(u), dz = tz - unit_lept_z(u);
    return dx * dx + dy * dy + dz * dz <= r * r;
}

}  // namespace

void World::step_ai_unit_rules() {
    const std::int64_t now = static_cast<std::int64_t>(repair_frame_);
    for (int h = 0; h < (int)ai_houses_.size(); ++h) {
        HouseAiRec& rec = ai_houses_[(std::size_t)h];
        if (!rec.is_ai) continue;
        const int delay = rec.cfg.auto_deploy_frames;
        if (delay < 0) continue;
        // W7-01-08 / W7-02-04 [RE: InfantryClass FUN_00521050 (Guard /
        // Area_Guard, via 0x51F330 / 0x51F350) and FUN_00522070 (Attack)]:
        //  * a computer-owned Deployer + DeployFire infantry whose type is NOT
        //    ImmuneToRadiation (+0xD37) and has no UndeployDelay (+0x6C4 < 0)
        //    deploys once it has stood in Guard for AIAutoDeployFrameDelay
        //    frames (Frame > CurrentMissionStartTime +0xC0 + delay);
        //  * in Attack it deploys when the target is inside its deploy weapon's
        //    range and undeploys when the target is not (a deploy weapon with
        //    AreaFire -- the Desolator's eruption -- is excluded @0x5220C0);
        //  * a move order undeploys it first (0x51F370; order_unit_move does).
        // In the clone that is exactly the deploy_fire infantry (GI / GGI): the
        // Desolator (ImmuneToRadiation, AreaFire eruption) is excluded by both
        // the Guard and the Attack branch, so it deploys only through a team
        // script Deploy line, as in the original (W7-02-04 corrected).
        // [RE-FALLBACK: the ArchiveTarget cell test @0x5210E0 is not modelled
        // -- skirmish AI units carry no archive target.]
        for (int i = 0; i < (int)units_.size(); ++i) {
            Unit& u = units_[(std::size_t)i];
            if (u.house != h || !u.alive || u.die_time >= 0.0 || !u.deploy_fire) continue;
            if (u.cargo_of >= 0 || u.garrison_of >= 0 || u.frozen || u.abil.immune_radiation) {
                u.ai.guard_since = -1;
                continue;
            }
            int tx = 0, ty = 0, tz = 0;
            bool has_target = false;
            if (u.target >= 0 && u.target < (int)units_.size() && units_[(std::size_t)u.target].alive) {
                const Unit& t = units_[(std::size_t)u.target];
                tx = unit_lept_x(t); ty = unit_lept_y(t); tz = unit_lept_z(t);
                has_target = true;
            } else if (u.target_building >= 0 && u.target_building < (int)buildings_.size() &&
                       buildings_[(std::size_t)u.target_building].alive) {
                const Building& b = buildings_[(std::size_t)u.target_building];
                building_aim_lept(b, cell_level(b.cx, b.cy), tx, ty, tz);
                has_target = true;
            }
            if (u.deployed) {
                u.ai.guard_since = -1;
                // Attack: the deployed post cannot reach the target -> undeploy
                // (Sequence Undeploy 0x1F) and pursue it.
                if (u.attack_order && has_target && !in_deploy_range(u, tx, ty, tz)) order_deploy(i);
                continue;
            }
            if (u.attack_order) {
                u.ai.guard_since = -1;
                // Attack: the target is inside the deploy weapon's range ->
                // deploy (Sequence Deploy 0x1B); the post keeps the target.
                if (has_target && in_deploy_range(u, tx, ty, tz)) order_deploy(i);
                continue;
            }
            if (u.moving() || u.enter_target >= 0 || u.occupy_target >= 0) {
                u.ai.guard_since = -1;
                continue;
            }
            if (u.ai.guard_since < 0) {
                u.ai.guard_since = now;   // entered Guard this frame
                continue;
            }
            if (now > u.ai.guard_since + delay) order_deploy(i);
        }
    }
}

// (W7-04-18 ai_protect was merged into World::call_for_help, world_w7.cpp, in fv7c.)

}  // namespace ra2engine
