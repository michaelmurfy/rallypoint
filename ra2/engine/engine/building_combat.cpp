// Gap fix C (G-40, G-29 defense half) -- base defenses acquire, aim and fire.
// [RE: docs/re/defenses.md]. World members kept in their own TU (the
// world_veterancy.cpp precedent); world.cpp only calls step_building_combat.
#include <algorithm>
#include <cmath>

#include "capture.hpp"     // apply_blackout
#include "orig_math.hpp"   // Net L0: the original's FastMath, never libm in the sim
#include "combat.hpp"      // deliver_hit_from, apply_spread_damage, damage_vs
#include "production.hpp"  // power_totals
#include "weapon_unit.hpp"
#include "world.hpp"

namespace ra2engine {

namespace {
constexpr double kLept = 256.0;
constexpr double kFireFlashSecs = 0.4;    // renderer cue, same as units

double bcenter_x(const Building& b) { return b.cx + (b.fw - 1) / 2.0; }
double bcenter_y(const Building& b) { return b.cy + (b.fh - 1) / 2.0; }
}  // namespace

std::vector<World::FireEvent> World::take_fire_events() {
    std::vector<FireEvent> out;
    out.swap(fire_events_);
    return out;
}

bool World::defense_power_ok(int house) const {
    // [RE: defenses.md §2] FUN_004555c0: Powered && drain > 0 && the house's
    // output/drain ratio (FUN_004fcc50) < 1.0 -> not operational. Same
    // predicate as A's house_low_power (blackout included).
    return !house_low_power(house);
}

Unit World::defense_shooter(int b_idx, int slot) const {
    const Building& b = buildings_[b_idx];
    const BuildingTypeDef& t = btypes()[b.type];
    const WeaponSlot& w = t.weapon[slot];
    Unit s;
    s.house = b.house;
    s.fx = bcenter_x(b);
    s.fy = bcenter_y(b);
    s.cell_x = static_cast<int>(om::lround(s.fx));
    s.cell_y = static_cast<int>(om::lround(s.fy));
    s.w_damage = w.damage;
    s.w_rof = w.rof;
    s.w_range = w.range;
    s.w_verses = w.verses;
    s.w_cell_spread = w.cell_spread;
    s.w_percent_at_max = w.percent_at_max;
    s.w_affects_allies = w.affects_allies;   // W5-B R-060
    s.w_inf_death = w.inf_death;          // W3-B
    s.w_prone_damage = w.prone_damage;
    s.w_proj = w.proj;
    s.w_aa = w.aa;
    s.w6.wall_flags = w.wall_flags;   // W6-04-04
    s.w_ag = w.ag;
    s.alive = false;   // never a real unit
    return s;
}

void World::launch_defense_projectile(int b_idx, int target_unit, int slot, int target_building) {
    const Building& b = buildings_[b_idx];
    const ProjectileSpec& spec = btypes()[b.type].weapon[slot].proj;
    if (target_unit < 0) {   // W6-04-13: a structure target -- aim at its centre
        if (target_building < 0 || target_building >= static_cast<int>(buildings_.size())) return;
        const Building& o = buildings_[static_cast<std::size_t>(target_building)];
        int ox = 0, oy = 0, oz = 0;
        building_aim_lept(o, cell_level(o.cx, o.cy), ox, oy, oz);
        const int muzzle_z0 = spec.flh[2] > 0 ? spec.flh[2] : 0;
        const Vec3i src0{ftol(bcenter_x(b) * kLept), ftol(bcenter_y(b) * kLept), muzzle_z0};
        const Vec3i tgt0{ox, oy, 0};
        const LaunchResult lr0 = compute_launch(spec, proj_rules_, src0, tgt0, proj_rng_);
        ProjectileEvent ev0;
        ev0.frame = proj_frame_;
        ev0.attacker = -1;
        ev0.x = src0.x; ev0.y = src0.y; ev0.z = src0.z;
        ev0.speed = lr0.speed;
        ev0.scatter_dx = lr0.scatter_dx;
        ev0.scatter_dy = lr0.scatter_dy;
        ev0.kind = lr0.ok ? ProjectileEvent::Launch : ProjectileEvent::Refused;
        proj_events_.push_back(ev0);
        if (!lr0.ok) return;
        Projectile p;
        p.kind = spec.kind;
        p.attacker = -1;
        p.attacker_building = b_idx;
        p.weapon_slot = slot;
        p.target_building = target_building;
        p.x = src0.x; p.y = src0.y; p.z = src0.z;
        p.vx = lr0.vx; p.vy = lr0.vy; p.vz = lr0.vz;
        p.tx = tgt0.x; p.ty = tgt0.y; p.tz = tgt0.z;
        p.speed_cap = spec.speed_cap;
        p.acceleration = spec.acceleration;
        p.detonation_altitude = spec.detonation_altitude;
        p.inaccurate = spec.inaccurate;
        p.floater = spec.floater;
        p.launch_frame = proj_frame_;
        p.launch_speed = lr0.speed;
        p.max_z = src0.z;
        if (p.kind == ProjKind::Homing) init_homing(p, spec, lr0.speed);
        projectiles_.push_back(p);
        return;
    }
    const Unit& v = units_[target_unit];
    // W4: the muzzle height. [RE-FALLBACK: the building art PrimaryFireFLH is
    // not resolved for defence weapon slots; 100 = NASAM's H (90,50,100) --
    // a homing bullet launched at height 0 would fail its height < 1 test.]
    const int muzzle_z = spec.flh[2] > 0 ? spec.flh[2] : (spec.kind == ProjKind::Homing ? 100 : 0);
    const Vec3i src{ftol(bcenter_x(b) * kLept), ftol(bcenter_y(b) * kLept), muzzle_z};
    const Vec3i tgt{ftol(v.fx * kLept), ftol(v.fy * kLept),
                    v.is_aircraft ? ftol(v.altitude) : 0};
    const LaunchResult lr = compute_launch(spec, proj_rules_, src, tgt, proj_rng_);
    ProjectileEvent ev;
    ev.frame = proj_frame_;
    ev.attacker = -1;
    ev.x = src.x; ev.y = src.y; ev.z = src.z;
    ev.speed = lr.speed;
    ev.scatter_dx = lr.scatter_dx;
    ev.scatter_dy = lr.scatter_dy;
    if (!lr.ok) {
        ev.kind = ProjectileEvent::Refused;
        proj_events_.push_back(ev);
        return;
    }
    ev.kind = ProjectileEvent::Launch;
    proj_events_.push_back(ev);
    Projectile p;
    p.kind = spec.kind;
    p.attacker = -1;
    p.attacker_building = b_idx;
    p.weapon_slot = slot;
    p.target_unit = target_unit;
    p.x = src.x; p.y = src.y; p.z = src.z;
    p.vx = lr.vx; p.vy = lr.vy; p.vz = lr.vz;
    p.tx = tgt.x; p.ty = tgt.y; p.tz = tgt.z;
    p.speed_cap = spec.speed_cap;
    p.acceleration = spec.acceleration;
    p.detonation_altitude = spec.detonation_altitude;
    p.inaccurate = spec.inaccurate;
    p.floater = spec.floater;
    p.launch_frame = proj_frame_;
    p.launch_speed = lr.speed;
    p.max_z = src.z;
    if (p.kind == ProjKind::Homing) {   // W4: the Patriot's missile flies
        init_homing(p, spec, lr.speed);
        p.target_air = unit_airborne(v);
    }
    projectiles_.push_back(p);
}

void World::step_building_combat(double dt) {
    const std::vector<BuildingTypeDef>& types = btypes();
    if (types.empty()) return;
    std::vector<int> power_cache;   // per house: -1 unknown, 0 low, 1 ok
    const int nb = static_cast<int>(buildings_.size());
    const int nu = static_cast<int>(units_.size());
    for (int bi = 0; bi < nb; ++bi) {
        Building& b = buildings_[bi];
        if (!b.alive || b.state != Building::State::Idle) continue;
        if (b.type < 0 || b.type >= static_cast<int>(types.size())) continue;
        const BuildingTypeDef& t = types[b.type];
        if (!t.armed) continue;

        b.def_fire_flash = std::max(0.0, b.def_fire_flash - dt);
        // Wave-5 (R-043) [RE: abilities-w5.md §2, BuildingClass::PrismUpdate
        // FUN_004503E0, every frame from BuildingClass::AI]: a charging prism
        // (master or slave) counts DelayBeforeFiring down; at zero a slave
        // fires its support beam (FireLaser FUN_0044ABD0) and a master its
        // boosted shot. Nothing else fires while charging (GetFireError
        // REARM).
        if (w5_.armed && b.w5.prism_stage != 0) {
            if (b.def_rearm_frames > 0) --b.def_rearm_frames;   // Net L0: whole frames
            if (--b.w5.delay > 0) continue;
            b.w5.delay = 0;
            const int stage = b.w5.prism_stage;
            b.w5.prism_stage = 0;
            if (stage == 2) {
                // FireLaser: beam to the master's muzzle (PrismSupportDuration),
                // SupportingPrisms = 0, ReloadTimer = PrismSupportDelay.
                fire_events_.push_back({-1, bi, -1, b.w5.master, 1, "PrismSupport"});
                b.def_fire_flash = kFireFlashSecs;
                b.w5.supporting = 0;
                b.w5.master = -1;
                b.def_rearm_frames = w5_.rules.prism_support_delay;   // frames
                ++w5_.prism_supports;
                continue;
            }
            const int tu = b.def_target;
            const bool valid = tu >= 0 && tu < nu && units_[tu].alive && units_[tu].die_time < 0.0 &&
                               units_[tu].cargo_of < 0 && is_enemy(b.house, units_[tu].house);
            if (valid) w5_prism_fire(bi, tu, 0);
            b.w5.supporting = 0;
            continue;
        }
        // [defenses.md §2] Powered=yes: offline while the house is short of power.
        b.def_offline = false;
        // W7-01-11: a Powered defence being erased has no power (HasPower=0).
        if (b.power_off || (t.powered && b.w7.warp_unpowered)) {   // W5-E2: powered down by its owner -- no fire at all
            b.def_offline = true;
            b.def_target = -1;
            continue;
        }
        if (w5_.armed && t.powered) {
            // Wave-5 (R-045): IsPowerOnline with the overpower exception
            // (two Tesla troopers keep a coil firing without base power).
            if (!w5_power_online(bi)) {
                b.def_offline = true;
                b.def_target = -1;
                continue;
            }
        } else if (t.powered) {
            if (b.house >= static_cast<int>(power_cache.size()))
                power_cache.resize(static_cast<std::size_t>(b.house) + 1, -1);
            if (power_cache[b.house] < 0) power_cache[b.house] = defense_power_ok(b.house) ? 1 : 0;
            if (power_cache[b.house] == 0) {
                b.def_offline = true;
                b.def_target = -1;
                continue;
            }
        }
        if (b.def_rearm_frames > 0) --b.def_rearm_frames;   // Net L0: whole frames

        const double cx = bcenter_x(b), cy = bcenter_y(b);
        auto slot_for = [&](const Unit& v) {
            // w7r3: the forced slot must still be able to hit the target --
            // AA/AG from the projectile (ThreatFlags FUN_007718B0), the
            // GetFireError legality every weapon passes (slot_can_target).
            // PrismShot and the coil bolts fly InvisibleHigh / Invisible (no
            // AA=yes), so a Prism Tower or a Tesla Coil never engages an
            // airborne Kirov; the forced index skipped the test and they shot
            // airships down.
            const bool air = unit_airborne(v);
            if (w5_.armed && t.w5.prism)   // PrismSupport is the beam, not a gun
                return slot_can_target(t.weapon[0], air, v.armor_ix) ? 0 : -1;
            // Wave-5 (R-045) [TechnoClass::SelectWeapon FUN_006F2890]: an
            // overpowered building fires its Secondary (OPCoilBolt).
            if (w5_.armed && t.w5.overpowerable) {
                const int s = b.w5.overpowered ? 1 : 0;
                return slot_can_target(t.weapon[s], air, v.armor_ix) ? s : -1;
            }
            return choose_weapon(t.weapon[0], t.weapon[1], air, v.armor_ix);
        };
        // W6-04-22 [RE: TechnoClass in-range test FUN_006F6750, combat.md §9]:
        // 3-D distance from the building's centre (z = its level), the
        // [ElevationModel] bonus FUN_006F6490 for a SubjectToElevation
        // projectile, the cliff trace, and MinimumRange (+0xB8) refusing
        // closer targets (the Grand Cannon's 3-cell dead zone). It was a 2-D
        // hypot with no minimum. GuardRange still widens the reach.
        int bx = 0, by = 0, bz = 0;
        building_aim_lept(b, cell_level(b.cx, b.cy), bx, by, bz);
        auto in_range = [&](const Unit& v, int slot) {
            const WeaponSlot& ws = t.weapon[slot];
            // W9-02-07: ONE scratch shooter, reused (a default Unit was
            // constructed + destroyed per candidate unit per defence per
            // frame); only w_range / w_proj are ever set on it, the rest stays
            // default, so the range test is unchanged.
            static thread_local Unit sh;
            sh.w_range = t.guard_range > 0.0 ? std::max(t.guard_range, ws.range) : ws.range;
            sh.w_proj = ws.proj;
            const int tx = unit_lept_x(v), ty = unit_lept_y(v), tz = unit_lept_z(v);
            if (ws.min_range > 0.0) {
                const double dx = tx - bx, dy = ty - by, dz = tz - bz;
                if (om::sqrt(dx * dx + dy * dy + dz * dz) < ws.min_range * kLept) return false;
            }
            return weapon_in_range_from(*this, sh, bx, by, bz, tx, ty, tz);
        };
        auto engageable = [&](int j) {
            if (j < 0 || j >= nu) return false;
            const Unit& v = units_[j];
            if (!v.alive || v.die_time >= 0.0 || v.cargo_of >= 0) return false;
            if (!is_enemy(b.house, v.house) || disguise_fools(b.house, v)) return false;   // W7-01-02
            // W2-AN-13/14: base defenses are NavalTargeting=0 (UNDERWATER_NEVER).
            if (v.an.submerged) return false;
            if (rocket_on_launcher(v)) return false;   // W4: not yet in the air
            if (!v.seen_by_house(b.house)) return false;   // fv7c: 32-bit house mask
            const int s = slot_for(v);
            return s >= 0 && in_range(v, s);
        };
        // Target validation, then acquire the nearest engageable enemy (ties:
        // lowest index) -- a defense never moves, so out of range = dropped.
        if (!engageable(b.def_target)) {
            b.def_target = -1;
            double best = 0.0;
            for (int j = 0; j < nu; ++j) {
                if (!engageable(j)) continue;
                const Unit& v = units_[j];
                const double d = om::hypot(v.fx - cx, v.fy - cy);
                if (b.def_target < 0 || d < best) { b.def_target = j; best = d; }
            }
        }
        // W6-04-13 [RE: BuildingClass::Greatest_Threat FUN_00445E60 = the
        // weapons' GetThreatFlags FUN_007718B0 | THREAT_RANGE; an AG weapon
        // returns 0xB8, which includes THREAT_BUILDINGS (0x20), so
        // TechnoClass::Greatest_Threat FUN_006F8320 @0x6F8410 adds the building
        // kind (bit 6) to the scan; Evaluate_Object FUN_006F71D0 then applies
        // the R-064 rule -- a human house's shooter takes only an armed
        // structure (a weapon, or a garrison firing out), a computer house's
        // any]: with no unit to shoot, a defence engages an enemy structure in
        // range (to its centre + (w+h)*64, combat.md §9). Walls and
        // Insignificant= structures are never picked (W6-03-01). [RE-FALLBACK:
        // units first, then the nearest structure; a delayed-fire (Prism)
        // tower keeps to units.]
        if (b.def_target < 0 && !(w5_.armed && t.w5.delayed_fire_delay > 0)) {
            auto bld_slot = [&](const Building& o) {
                if (w5_.armed && t.w5.overpowerable) return b.w5.overpowered ? 1 : 0;
                return choose_weapon(t.weapon[0], t.weapon[1], false, o.armor_ix);
            };
            auto bld_ok = [&](int j) -> bool {
                if (j < 0 || j >= nb || j == bi) return false;
                const Building& o = buildings_[static_cast<std::size_t>(j)];
                if (!o.alive || o.state == Building::State::Dead || !is_enemy(b.house, o.house)) return false;
                if (o.type < 0 || o.type >= static_cast<int>(types.size())) return false;
                const BuildingTypeDef& ot = types[static_cast<std::size_t>(o.type)];
                if (ot.wall || ot.insignificant || o.iron_left_frames > 0.0) return false;
                const bool armed = ot.armed || (ot.can_occupy_fire && !o.occupants.empty());
                if (!armed && !(house_is_computer(b.house) && ai_threat_any_building())) return false;
                if (!object_visible_at(b.house, o.cx, o.cy)) return false;
                const int s = bld_slot(o);
                if (s < 0 || !t.weapon[s].valid || !t.weapon[s].ag) return false;
                const WeaponSlot& ws = t.weapon[s];
                static thread_local Unit sh;   // W9-02-07 (see in_range)
                sh.w_range = t.guard_range > 0.0 ? std::max(t.guard_range, ws.range) : ws.range;
                sh.w_proj = ws.proj;
                int ox = 0, oy = 0, oz = 0;
                building_aim_lept(o, cell_level(o.cx, o.cy), ox, oy, oz);
                if (ws.min_range > 0.0) {
                    const double dx = ox - bx, dy = oy - by, dz = oz - bz;
                    if (om::sqrt(dx * dx + dy * dy + dz * dz) < ws.min_range * kLept) return false;
                }
                return weapon_in_range_from(*this, sh, bx, by, bz, ox, oy, oz, (o.fw + o.fh) * 64);
            };
            int tb = b.w6.def_target_building;
            if (!bld_ok(tb)) {
                tb = -1;
                double best = 0.0;
                for (int j = 0; j < nb; ++j) {
                    if (!bld_ok(j)) continue;
                    const Building& o = buildings_[static_cast<std::size_t>(j)];
                    const double d = om::hypot(bcenter_x(o) - cx, bcenter_y(o) - cy);
                    if (tb < 0 || d < best) { tb = j; best = d; }
                }
            }
            b.w6.def_target_building = tb;
            if (tb < 0) continue;
            const Building& o = buildings_[static_cast<std::size_t>(tb)];
            const int bslot = bld_slot(o);
            if (t.turret) {
                const double want = facing_u16_from_delta(bcenter_x(o) - cx, bcenter_y(o) - cy);
                const double step = std::max(1, t.rot) * kRotU16PerRotPerSec * dt;
                b.def_turret_facing = turn_toward(b.def_turret_facing, want, step);
                if (b.def_turret_facing != want) continue;
            }
            if (b.def_rearm_frames > 0.0) continue;
            const WeaponSlot& bw = t.weapon[bslot];
            b.def_rearm_frames = burst_rearm_frames(b.w6.burst_index, bw.burst, house_rof_frames(b.house, bw.rof), &combat_rng_);   // W6-02-03
            b.def_fire_flash = kFireFlashSecs;
            b.def_slot = bslot;
            fire_events_.push_back({-1, bi, -1, tb, bslot, bw.id});
            if (projectiles_enabled_ && bw.proj.flies()) {
                launch_defense_projectile(bi, -1, bslot, tb);
                continue;
            }
            const Unit bshooter = defense_shooter(bi, bslot);
            int ax = 0, ay = 0, az = 0;
            building_aim_lept(o, cell_level(o.cx, o.cy), ax, ay, az);
            damage_area(*this, units_, buildings_, occ_, bshooter, -1, bi, ax, ay, az);
            continue;
        }
        b.w6.def_target_building = -1;
        if (b.def_target < 0) continue;
        const Unit& v = units_[b.def_target];
        const int slot = slot_for(v);
        // Turret=yes: slew at ROT (Set_ROT, voxel-render.md §5) and fire only
        // once on target; Turret=no (Prism/Tesla) fires at once.
        if (t.turret) {
            const double want = facing_u16_from_delta(v.fx - cx, v.fy - cy);
            const double step = std::max(1, t.rot) * kRotU16PerRotPerSec * dt;
            b.def_turret_facing = turn_toward(b.def_turret_facing, want, step);
            if (b.def_turret_facing != want) continue;
        }
        if (b.def_rearm_frames > 0.0) continue;
        // Wave-5 (R-043) [RE: abilities-w5.md §2, BuildingClass::Mission_Attack
        // 0x44B2F8]: a delayed-fire building (art IsAnimDelayedFire) charges
        // first. A prism recruits ONE supporter per frame -- the nearest own
        // idle prism within this tower's Secondary range whose reload is over
        // and that is not charging -- until none is left or PrismSupportMax;
        // then it becomes the master and fires DelayedFireDelay frames later.
        if (w5_.armed && t.w5.delayed_fire_delay > 0) {
            // [RE-FALLBACK: the original's per-object target-scan stagger means
            // the first tower to acquire recruits the idle ones around it; here
            // every tower acquires in the same frame, so a tower inside the
            // reach of an own prism that is already recruiting stands down and
            // stays available as its supporter.]
            if (t.w5.prism) {
                bool lead = false;
                for (int li = 0; li < nb && !lead; ++li) {
                    if (li == bi) continue;
                    const Building& L = buildings_[li];
                    if (!L.alive || L.house != b.house || L.type != b.type || L.w5.prism_stage != 0) continue;
                    if (L.w5.supporting <= 0 || L.def_target < 0) continue;
                    lead = om::hypot(bcenter_x(L) - cx, bcenter_y(L) - cy) <= types[L.type].weapon[1].range;
                }
                if (lead) {
                    b.def_target = -1;
                    continue;
                }
            }
            if (t.w5.prism && b.w5.supporting < w5_.rules.prism_support_max) {
                int best = -1;
                double bestd = 0.0;
                const double reach = t.weapon[1].range;
                for (int ci = 0; ci < nb; ++ci) {
                    if (ci == bi) continue;
                    const Building& c = buildings_[ci];
                    if (!c.alive || c.state != Building::State::Idle || c.house != b.house) continue;
                    if (c.type != b.type) continue;   // Rules->PrismType
                    if (c.def_rearm_frames > 0.0 || c.w5.delay != 0 || c.w5.prism_stage != 0) continue;
                    if (c.def_target >= 0) continue;  // a tower in its Attack mission
                    const double d = om::hypot(bcenter_x(c) - cx, bcenter_y(c) - cy);
                    if (d > reach) continue;
                    if (best >= 0 && d >= bestd) continue;
                    best = ci;
                    bestd = d;
                }
                if (best >= 0) {
                    Building& c = buildings_[best];
                    ++b.w5.supporting;
                    c.w5.delay = types[c.type].w5.delayed_fire_delay;
                    c.w5.prism_stage = 2;
                    c.w5.master = bi;
                    continue;
                }
            }
            b.w5.delay = t.w5.delayed_fire_delay;
            b.w5.prism_stage = 1;
            continue;
        }
        const WeaponSlot& w = t.weapon[slot];
        // W6-02-03 [RE: Building vtbl 0x7E2EBC +0x318 = GetROF FUN_006FC460]: the owner's
        // handicap ROF multiplies a defence rearm too.
        b.def_rearm_frames = burst_rearm_frames(b.w6.burst_index, w.burst, house_rof_frames(b.house, w.rof), &combat_rng_);   // G-50 jitter, W6-04-01 Burst
        b.def_fire_flash = kFireFlashSecs;
        b.def_slot = slot;
        fire_events_.push_back({-1, bi, b.def_target, -1, slot, w.id});
        if (projectiles_enabled_ && w.proj.flies()) {
            launch_defense_projectile(bi, b.def_target, slot);
            continue;
        }
        const Unit shooter = defense_shooter(bi, slot);
        // W5-B R-060 [RE: DamageArea FUN_00489190]: detonate at the target.
        const Unit& tv = units_[b.def_target];
        damage_area(*this, units_, buildings_, occ_, shooter, -1, bi, unit_lept_x(tv),
                    unit_lept_y(tv), unit_lept_z(tv));
    }
}

// The master prism's shot [RE: abilities-w5.md §2, PrismUpdate 0x4504A0 +
// BulletClass::Detonate 0x469973]: DamageMultiplier = (PrismSupportModifier *
// SupportingPrisms + 100) * 256 / 100 (unsigned), damage = (Damage * mult) >> 8.
bool World::w5_prism_fire(int bi, int target_unit, int slot) {
    Building& b = buildings_[bi];
    const BuildingTypeDef& t = btypes()[b.type];
    const WeaponSlot& w = t.weapon[slot];
    const int n = b.w5.supporting;
    const unsigned mult = n > 0 ? static_cast<unsigned>(w5_.rules.prism_support_modifier * n + 100) * 256u / 100u
                                : 256u;
        // W6-02-03 [RE: Building vtbl 0x7E2EBC +0x318 = GetROF FUN_006FC460]: the owner's
        // handicap ROF multiplies a defence rearm too.
    b.def_rearm_frames = burst_rearm_frames(b.w6.burst_index, w.burst, house_rof_frames(b.house, w.rof), &combat_rng_);   // W6-04-01
    b.def_fire_flash = kFireFlashSecs;
    b.def_slot = slot;
    fire_events_.push_back({-1, bi, target_unit, -1, slot, n > 0 ? "PrismShotSupported" : w.id});
    Unit shooter = defense_shooter(bi, slot);
    shooter.w_damage = static_cast<int>((static_cast<long long>(w.damage) * mult) >> 8);
    if (w.cell_spread >= 1.0)
        apply_spread_damage(units_, occ_, shooter, units_[target_unit]);
    else
        deliver_hit_from(units_, occ_, shooter, -1, bi, target_unit);
    return true;
}

}  // namespace ra2engine
