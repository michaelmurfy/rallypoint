// P5 Veterancy + Crates [RE: docs/re/veterancy.md]: the World member functions
// of this phase, kept in their own TU so engine/world.cpp only carries the
// three small insertions (add_combat_unit copy, add_building flag, the two
// step() calls). Everything here is behind vet_any_ / crates_any_.
#include <algorithm>
#include <cmath>

#include "world.hpp"
#include "orig_math.hpp"   // Net L0: the original's FastMath, never libm in the sim
#include "weapon_unit.hpp"  // Gap fix C: active weapon slot

namespace ra2engine {

namespace {
constexpr int kLepton = 256;
}  // namespace

// ---------------------------------------------------------------- veterancy

void World::enable_veterancy(const VeterancyRules& r, std::vector<int> building_costs) {
    vet_any_ = true;
    vet_rules_ = r;
    vet_building_costs_ = std::move(building_costs);
    for (Unit& u : units_) {
        // Anything already dead/dying when the system opens is not credited.
        if (u.die_time >= 0.0 || !u.alive) u.vet.credited = true;
        init_unit_vet(u);
    }
    for (Building& b : buildings_) {
        b.vet_recv = true;
        if (!b.alive || b.state == Building::State::Dead) b.vet_credited = true;
    }
}

void World::init_unit_vet(Unit& u) {
    u.vet.recv = true;
    u.vet.vet_armor = vet_rules_.armor;
    const int idx = static_cast<int>(&u - units_.data());
    refresh_vet_stats(idx);
    // last_rank stays -1: the first step_veterancy observation is silent
    // (Update's stored rank starts at -1, V12).
}

void World::refresh_vet_stats(int idx) {
    if (!vet_any_ || idx < 0 || idx >= static_cast<int>(units_.size())) return;
    Unit& u = units_[idx];
    UnitVet& v = u.vet;
    const float vv = v.veterancy;
    auto has = [&](Ability a) { return has_ability(vv, v.vet_abilities, v.elite_abilities, a); };
    // [V10] the elite slot only when the type defines ElitePrimary.
    const WeaponStats& w = (is_elite(vv) && v.has_elite) ? v.elite : v.base;
    // [G-63 / W6-02-01..02] The house's AssignHandicap FirePower/ROF: the
    // per-house handicap once a campaign set it (player + PlayerControl houses
    // the slider class, every other house 2 - slider), else an AI house's
    // DifficultyMods, else 1.0.
    const double house_fp = house_firepower_mul(u.house), house_rof = house_rof_mul(u.house);
    u.w_damage = fire_damage(w.damage, v.firepower_mult, has(Ability::Firepower), vet_rules_.combat,
                             house_fp);
    u.w_rof = rof_frames(w.rof, has(Ability::Rof), vet_rules_.rof, house_rof);
    // w7fix-combat W7-07-14/15: keep the parts for the jitter-first rearm and
    // the slot switch (the house handicap survives activate_weapon_slot).
    u.tg.w7_rof = w7_rules_.armed;
    u.tg.rof_raw = w.rof;
    u.tg.house_rof = house_rof;
    u.tg.house_fp = house_fp;
    u.tg.rof_vet = has(Ability::Rof) ? vet_rules_.rof : 1.0;
    u.w6.burst = w.burst;   // W6-04-01: the elite slot's Burst (105mmE 2, 120mmxE 4)
    u.w6.omni_fire = w.omni_fire;   // W6-04-06
    u.w6.sonic = w.sonic;           // W6-04-09
    u.w6.wall_flags = w.wall_flags; // W6-04-04
    // W6-04-02: the elite projectile's shrapnel (SuperCometP SuperCometFragment
    // x5, Electricbounce TeslaFragment x2 for the elite Tesla Trooper / Tank).
    u.shrapnel_count = w.shrapnel_count;
    u.shrapnel_damage = w.shrapnel_damage;
    u.shrapnel_range = w.shrapnel_range;
    u.shrapnel_verses = w.shrapnel_verses;
    u.w_range = w.range;
    u.w_verses = w.verses;
    u.w_cell_spread = w.cell_spread;
    u.w_percent_at_max = w.percent_at_max;
    // Gap fix C (G-41) [V10]: the elite slot carries its own projectile
    // (EliteWeapon1=SuperComet / ElitePrimary) -- swapped with the stats.
    if (u.p_proj_elite_set) u.w_proj = is_elite(vv) ? u.p_proj_elite : u.p_proj;
    // A unit currently firing its secondary re-derives THAT slot.
    if (u.w_active == 1) {
        u.w_active = 0;
        activate_weapon_slot(u, 1, &vet_rules_);
    }
    // [V8] speed in the integer Speed= domain, mapped back through the unit's
    // own existing conversion (vehicles: speed_cps; infantry: World move_cps).
    // Aircraft FASTER is out of scope (spec §Scope).
    // Wave-5 (item 8): the remaining switch abilities a rank can grant
    // (no Allied/Soviet type of this install's rulesmd grants them; kept so
    // map/mod data that does behaves): CRUSHER -> Crusher, EXPLODES ->
    // Explodes (the P4 death weapon), C4 -> C4 (the P4 plant).
    if (has(Ability::Crusher)) u.crusher = true;
    if (has(Ability::Explodes)) u.abil.explodes = true;
    if (has(Ability::C4)) u.abil.c4 = true;
    // Wave-5 (item 8): aircraft FASTER -- the flight speeds scale by
    // VeteranSpeed like a ground unit's Speed= [RE-FALLBACK: same
    // multiplier, applied to the cruise speeds instead of Speed=].
    if (u.is_aircraft) {
        if (u.w5.base_flight_speed < 0.0) {
            u.w5.base_flight_speed = u.flight_speed;
            u.w5.base_fly_cps = u.an.t.fly_speed_cps;
        }
        const double m = has(Ability::Faster) ? vet_rules_.speed : 1.0;
        u.flight_speed = u.w5.base_flight_speed * m;
        u.an.t.fly_speed_cps = u.w5.base_fly_cps * m;
    }
    if (v.rules_speed > 0 && !u.is_aircraft) {
        // W7 / V8 [RE: FUN_004DB0B0 reads TechnoType+0x678 -- the Speed= in
        // LEPTONS, min(255, trunc(Speed*256/100))]: the crate and FASTER
        // multipliers truncate the lepton value (GI 10 -> 12), not Speed=.
        const int lept = std::min(255, static_cast<int>(std::min(100, v.rules_speed) * 256 / 100));
        const int s = lept > 0 ? speed_value(lept, v.speed_mult, has(Ability::Faster), vet_rules_.speed) : 0;
        if (s == lept || lept <= 0) {
            u.speed_cps = v.base_speed_cps;
        } else {
            const double base = v.base_speed_cps > 0.0 ? v.base_speed_cps : move_cps_;
            const double per = base / static_cast<double>(lept);
            u.speed_cps = per * static_cast<double>(s);
        }
    }
    // [V9] dead with VeteranSight=0.0, kept faithful.
    u.sight = sight_value(v.base_sight, has(Ability::Sight), vet_rules_.sight);
    v.vet_armor = vet_rules_.armor;
    // W2-SK-26 [RE: abilities.md §G]: a promotion / crate must not revert an
    // IFV gunner to its empty-hold weapon -- re-apply the passenger's mode.
    if (u.abil.gunner && u.ifv_current > 0 &&
        u.ifv_current < static_cast<int>(u.abil.ifv_weapons.size()) &&
        u.abil.ifv_weapons[static_cast<std::size_t>(u.ifv_current)].valid)
        ability_apply_weapon_fields(u, u.abil.ifv_weapons[static_cast<std::size_t>(u.ifv_current)]);
}

std::vector<World::VetEvent> World::take_vet_events() {
    std::vector<VetEvent> out;
    out.swap(vet_events_);
    return out;
}

void World::credit_kill(int killer, int victim_house, int victim_cost, float victim_vet,
                        bool dont_score) {
    // [V3] FUN_007021D0.
    if (dont_score) return;
    if (killer < 0 || killer >= static_cast<int>(units_.size())) return;
    // Wave-5 (item 8) [RE-FALLBACK: the original's spawn-manager kill credit
    // was not traced; in play the Carrier / V3 launcher is what gains rank]:
    // a spawned aircraft or a missile credits its spawner.
    if (w7_rules_.armed) {
        // W7-07-16 [RE: FUN_007021D0 0x70232D..0x70247A]: (1) an open-topped
        // passenger credits its Trainable transport; (2) else a Trainable
        // killer itself (Hornets, ASW planes); (3) else a MissileSpawn credits
        // its SpawnOwner.
        const Unit& s = units_[killer];
        const int tr = s.cargo_of;
        if (tr >= 0 && tr < static_cast<int>(units_.size()) && units_[tr].w5.p.open_topped &&
            units_[tr].vet.trainable)
            killer = tr;
        else if (!s.vet.trainable && s.an.t.missile && s.an.owner >= 0 &&
                 s.an.owner < static_cast<int>(units_.size()))
            killer = s.an.owner;
    } else {
        const Unit& s = units_[killer];
        if ((s.an.t.spawned || s.an.t.missile) && s.an.owner >= 0 &&
            s.an.owner < static_cast<int>(units_.size()))
            killer = s.an.owner;
    }
    Unit& k = units_[killer];
    if (!k.alive) return;
    // W7-07-16 [RE: 0x7022FC IsAlliedWith]: only an allied victim is worth 0
    // (a neutral kill scores).
    const bool allied = w7_rules_.armed ? allied_with(k.house, victim_house) : !is_enemy(k.house, victim_house);
    const int value = kill_value(victim_cost, victim_vet, allied);
    if (!k.vet.trainable) return;
    k.vet.veterancy = veterancy_add(k.vet.veterancy, k.vet.cost, value, vet_rules_);
}

void World::step_veterancy() {
    // The global Frame advances UNCONDITIONALLY (above the firewall) so the
    // self-heal phase is absolute [V11]. Nothing hashes it.
    const std::uint64_t frame = vet_frame_++;
    if (!vet_any_) return;

    // (1) Kill credit for every unit / building that died this step [V2/V3].
    const int n = static_cast<int>(units_.size());
    for (int i = 0; i < n; ++i) {
        Unit& d = units_[i];
        if (d.vet.credited) continue;
        if (d.die_time < 0.0 && d.alive) continue;
        d.vet.credited = true;
        credit_kill(d.vet.killed_by, d.house, d.vet.cost, d.vet.veterancy, d.vet.dont_score);
    }
    for (Building& b : buildings_) {
        if (b.vet_credited) continue;
        if (b.alive && b.state != Building::State::Dead) continue;
        b.vet_credited = true;
        const int cost = (b.type >= 0 && b.type < static_cast<int>(vet_building_costs_.size()))
                             ? vet_building_costs_[b.type] : 0;
        credit_kill(b.killed_by, b.house, cost, 0.0f, false);
    }

    // (2) Rank-change detection -> event + stat refresh [V12].
    for (int i = 0; i < n; ++i) {
        Unit& u = units_[i];
        if (!u.alive || u.die_time >= 0.0) continue;
        if (u.vet.elite_flash > 0) --u.vet.elite_flash;
        const int r = vet_rank(u.vet.veterancy);
        if (r != u.vet.last_rank) {
            if (u.vet.last_rank != -1) {
                vet_events_.push_back({i, u.vet.last_rank, r});
                if (r == 2) u.vet.elite_flash = vet_rules_.elite_flash;
            }
            u.vet.last_rank = r;
            refresh_vet_stats(i);
        }
    }

    // (3) Self-heal on the global ftol(RepairRate*900) phase [V11]. Limbo
    //     (carried) units return before the check in the original.
    const int interval = self_heal_interval(vet_rules_.repair_rate);
    for (int i = 0; i < n; ++i) {
        Unit& u = units_[i];
        if (!u.alive || u.die_time >= 0.0 || u.cargo_of >= 0) continue;
        const bool eligible =
            u.vet.self_healing ||
            has_ability(u.vet.veterancy, u.vet.vet_abilities, u.vet.elite_abilities, Ability::SelfHeal);
        if (should_self_heal(frame, interval, eligible, u.hp, u.max_hp)) u.hp += 1;
    }
}

// ------------------------------------------------------------------- crates

bool World::enable_crates(const CrateConfig& cfg) {
    if (!vet_any_) return false;
    crates_any_ = true;
    crate_rules_ = cfg.rules;
    crate_rng_ = GameRandom(cfg.seed);
    crate_pool_ = cfg.unit_pool;
    crates_.assign(256, Crate{});
    if (cfg.place_initial) {
        const int count = initial_crate_count(cfg.players, crate_rules_);   // [C2]
        for (int i = 0; i < count; ++i) place_random_crate();
    }
    return true;
}

std::vector<World::CrateEvent> World::take_crate_events() {
    std::vector<CrateEvent> out;
    out.swap(crate_events_);
    return out;
}

bool World::cell_is_water(int x, int y) const {
    if (water_passable_.empty() || x < 0 || y < 0 || x >= grid_w_ || y >= grid_h_) return false;
    return water_passable_[static_cast<std::size_t>(y) * grid_w_ + x] != 0;
}

// [RE-FALLBACK: NearbyLocation] the nearest cell (Chebyshev rings, scan order
// dy then dx) on the speed-type grid (water -> Float, else Track) that holds no
// unit. Buildings already clear passable_ under their foundations.
bool World::crate_nearby_cell(int x, int y, bool water, int& ox, int& oy) const {
    const std::vector<std::uint8_t>& g = water ? water_passable_ : passable_;
    if (g.empty() || grid_w_ <= 0 || grid_h_ <= 0) return false;
    const int maxr = std::max(grid_w_, grid_h_);
    for (int r = 0; r <= maxr; ++r) {
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                const int cx = x + dx, cy = y + dy;
                if (cx < 0 || cy < 0 || cx >= grid_w_ || cy >= grid_h_) continue;
                if (!g[static_cast<std::size_t>(cy) * grid_w_ + cx]) continue;
                if (!occ_.is_free(cx, cy)) continue;
                ox = cx;
                oy = cy;
                return true;
            }
    }
    return false;
}

// [C3] FUN_004A1800: the cell must accept an overlay and hold none (no ore,
// no other crate), and not be under a building.
bool World::crate_cell_valid(int x, int y) const {
    if (x < 0 || y < 0 || x >= grid_w_ || y >= grid_h_) return false;
    if (ore_.has_ore(x, y)) return false;
    if (building_at_cell(x, y) >= 0) return false;
    for (const Crate& c : crates_)
        if (c.x == x && c.y == y) return false;
    return true;
}

// [C3/C4] FUN_004A16D0 on a slot: validate, then draw the lifetime.
bool World::crate_slot_place(int slot, int x, int y, int powerup) {
    if (!crate_cell_valid(x, y)) return false;
    Crate& c = crates_[slot];
    c.x = x;
    c.y = y;
    c.powerup = powerup;
    c.start = vet_frame_;
    c.lifetime = crate_lifetime_frames(crate_rng_, crate_rules_.regen);
    return true;
}

bool World::place_random_crate() {
    // [C3] FUN_0056B870: a free slot, then up to 1000 random-cell tries.
    if (!crates_any_) return false;
    int slot = -1;
    for (int i = 0; i < static_cast<int>(crates_.size()); ++i)
        if (crates_[i].x < 0) { slot = i; break; }
    if (slot < 0 || grid_w_ <= 0 || grid_h_ <= 0) return false;
    for (int t = 0; t < 1000; ++t) {
        // [RE-FALLBACK: MapRect] the grid bounds stand in for MapRect.
        const int x = crate_rng_.ranged(0, grid_w_ - 1);
        const int y = crate_rng_.ranged(0, grid_h_ - 1);
        int cx = 0, cy = 0;
        if (!crate_nearby_cell(x, y, cell_is_water(x, y), cx, cy)) continue;
        if (crate_slot_place(slot, cx, cy, -1)) return true;
    }
    return false;
}

bool World::place_crate(int x, int y, int powerup) {
    // FUN_0056B9F0: NearbyLocation first, then the first free slot.
    if (!crates_any_) return false;
    int cx = 0, cy = 0;
    if (!crate_nearby_cell(x, y, cell_is_water(x, y), cx, cy)) return false;
    for (int i = 0; i < static_cast<int>(crates_.size()); ++i)
        if (crates_[i].x < 0) return crate_slot_place(i, cx, cy, powerup);
    return false;
}

void World::collect_crate(int slot, int unit_idx) {
    // [C5/C6] CellClass::CollectCrate FUN_00481910, multiplayer branch.
    const Crate cr = crates_[slot];
    Unit& u = units_[unit_idx];
    const int h = u.house;
    const auto& pw = crate_rules_.powerups;
    int p = cr.powerup >= 0 ? cr.powerup : pick_powerup(crate_rng_, pw);
    const int picked = p;
    const bool water = cell_is_water(cr.x, cr.y);
    auto house_units = [&](bool infantry_only) {
        int c = 0;
        for (const Unit& o : units_)
            if (o.alive && o.die_time < 0.0 && o.house == h && (!infantry_only || !o.is_vehicle)) ++c;
        return c;
    };
    // §3.3 conversion rules. Wave-5 (item 8): rule 1, the forced MCV [RE:
    // veterancy.md §3.3 rule 1, FUN_00481910]: a house with no buildings, more
    // than 1500 credits, no MCV and the MCV-redeploy option on gets the Unit
    // crate as its side's MCV (the UndeploysInto unit of its ConYard type).
    int forced_mcv = -1;   // index into undeploy_units_
    if (mcv_redeploys_ && h >= 0 && h < house_count()) {
        bool any_bld = false;
        for (const Building& b : buildings_) any_bld |= b.alive && b.state != Building::State::Dead && b.house == h;
        bool any_mcv = false;
        for (int i = 0; i < static_cast<int>(units_.size()); ++i)
            any_mcv |= units_[i].alive && units_[i].die_time < 0.0 && units_[i].house == h && is_mcv(i);
        const double cash = ai_enabled(h) ? econ_[(std::size_t)h].credits : house_credits_[h] + player_wallet(h);
        if (!any_bld && !any_mcv && cash > 1500.0) {
            const bool soviet = house_side(h) == Side::Soviet;
            for (int k = 0; k < static_cast<int>(undeploy_units_.size()) && forced_mcv < 0; ++k) {
                const int bt = k < static_cast<int>(undeploy_types_.size()) ? undeploy_types_[k] : -1;
                if (bt < 0 || bt >= static_cast<int>(btypes().size())) continue;
                const std::string& id = btypes()[bt].id;
                if ((id.rfind("NA", 0) == 0) == soviet) forced_mcv = k;
            }
            if (forced_mcv >= 0) p = static_cast<int>(Powerup::Unit);
        }
    }
    if (forced_mcv < 0) switch (static_cast<Powerup>(p)) {
        case Powerup::Unit:
            if (house_units(false) > 50 || water) p = static_cast<int>(Powerup::Money);
            break;
        case Powerup::Squad:
            if (house_units(true) > 100 || water) p = static_cast<int>(Powerup::Money);
            break;
        case Powerup::Armor:
            if (u.vet.armor_mult != 1.0) p = static_cast<int>(Powerup::Money);
            break;
        case Powerup::Speed:
            if (u.vet.speed_mult != 1.0 || u.is_aircraft) p = static_cast<int>(Powerup::Money);
            break;
        case Powerup::Firepower: {
            const bool armed = u.vet.base.damage > 0 || u.vet.base.range > 0.0;
            if (u.vet.firepower_mult != 1.0 || !armed) p = static_cast<int>(Powerup::Money);
            break;
        }
        case Powerup::Veteran:
            if (!u.vet.trainable || is_elite(u.vet.veterancy)) p = static_cast<int>(Powerup::Money);
            break;
        default:
            break;
    }
    if (p < 0 || p >= kPowerupCount) p = static_cast<int>(Powerup::Money);
    if (water && !pw[p].naval) p = static_cast<int>(Powerup::Money);

    // Remove this crate and place its replacement BEFORE the effect runs (the
    // original's RNG order: pick -> relocation draws -> effect draws).
    crates_[slot] = Crate{};
    place_random_crate();
    if (p == static_cast<int>(Powerup::Squad)) p = static_cast<int>(Powerup::Money);

    CrateEvent ev;
    ev.picked = picked;
    ev.house = h;
    ev.unit = unit_idx;
    ev.x = cr.x;
    ev.y = cr.y;
    const double arg = pw[p].arg;
    // [C7] area test: ftol(sqrt(dx^2 + dy^2)) < CrateRadius (leptons), from the
    // crate cell centre. 2D (no Z) -- the engine-wide documented divergence.
    const int ccx = cr.x * kLepton + kLepton / 2;
    const int ccy = cr.y * kLepton + kLepton / 2;
    auto in_radius_unit = [&](const Unit& o) {
        const int ox = static_cast<int>(om::lround(o.fx * kLepton)) + kLepton / 2;
        const int oy = static_cast<int>(om::lround(o.fy * kLepton)) + kLepton / 2;
        const double dx = static_cast<double>(ccx - ox);
        const double dy = static_cast<double>(ccy - oy);
        const double sq = dx * dx;
        const double d2 = sq + dy * dy;
        return ftol(om::sqrt(d2)) < crate_rules_.radius_leptons;
    };
    auto on_map = [](const Unit& o) { return o.alive && o.die_time < 0.0 && o.cargo_of < 0; };

    switch (static_cast<Powerup>(p)) {
        case Powerup::Unit: {
            if (crate_pool_.empty() && forced_mcv < 0) {
                // [RE-FALLBACK] no CrateGoodie type loadable -> Money.
                p = static_cast<int>(Powerup::Money);
                const int base = ftol(pw[p].arg);
                const int amt = crate_rng_.ranged(base, base + 900);
                if (h >= 0 && h < house_count()) house_credits_[h] += static_cast<double>(amt);
                ev.amount = amt;
                break;
            }
            const int k = forced_mcv >= 0 ? -1 : crate_rng_.ranged(0, static_cast<int>(crate_pool_.size()) - 1);
            // Unlimbo at the crate cell, else the nearest free walkable cell.
            int sx = -1, sy = -1;
            for (int r = 0; r <= 8 && sx < 0; ++r)
                for (int dy = -r; dy <= r && sx < 0; ++dy)
                    for (int dx = -r; dx <= r && sx < 0; ++dx) {
                        if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                        if (walkable(cr.x + dx, cr.y + dy) && unit_at_cell(cr.x + dx, cr.y + dy) < 0 &&
                            occ_.is_free(cr.x + dx, cr.y + dy)) {
                            sx = cr.x + dx;
                            sy = cr.y + dy;
                        }
                    }
            if (sx >= 0) {
                add_combat_unit(sx, sy, h, forced_mcv >= 0 ? undeploy_units_[forced_mcv] : crate_pool_[k]);
                ev.amount = static_cast<int>(units_.size()) - 1;
                ev.affected = 1;
            }
            break;
        }
        case Powerup::Money: {
            const int base = ftol(arg);
            const int amt = crate_rng_.ranged(base, base + 900);
            if (h >= 0 && h < house_count()) house_credits_[h] += static_cast<double>(amt);
            ev.amount = amt;
            break;
        }
        case Powerup::HealBase:
            for (Building& b : buildings_)
                if (b.alive && b.state != Building::State::Dead && b.house == h && b.hp < b.max_hp) {
                    b.hp = b.max_hp;
                    ++ev.affected;
                }
            break;
        case Powerup::Reveal:
            reveal_all_house(h);
            break;
        case Powerup::Armor:
        case Powerup::Firepower:
        case Powerup::Speed: {
            const Powerup kind = static_cast<Powerup>(p);
            for (int i = 0; i < static_cast<int>(units_.size()); ++i) {
                Unit& o = units_[i];
                if (!on_map(o) || !in_radius_unit(o)) continue;
                double* m = kind == Powerup::Armor ? &o.vet.armor_mult
                          : kind == Powerup::Firepower ? &o.vet.firepower_mult
                                                       : &o.vet.speed_mult;
                if (kind == Powerup::Speed && o.is_aircraft) continue;   // Foot, not aircraft
                if (*m != 1.0) continue;
                *m = *m * arg;
                refresh_vet_stats(i);
                ++ev.affected;
            }
            if (kind == Powerup::Armor) {
                // Buildings are Technos too: the Armor crate reaches them.
                for (Building& b : buildings_) {
                    if (!b.alive || b.state == Building::State::Dead) continue;
                    const int bx = b.cx * kLepton + b.fw * kLepton / 2;
                    const int by = b.cy * kLepton + b.fh * kLepton / 2;
                    const double dx = static_cast<double>(ccx - bx);
                    const double dy = static_cast<double>(ccy - by);
                    const double sq = dx * dx;
                    const double d2 = sq + dy * dy;
                    if (ftol(om::sqrt(d2)) >= crate_rules_.radius_leptons) continue;
                    if (b.armor_mult != 1.0) continue;
                    b.armor_mult = b.armor_mult * arg;
                    ++ev.affected;
                }
            }
            break;
        }
        case Powerup::Veteran:
            for (Unit& o : units_) {
                if (!on_map(o) || !in_radius_unit(o) || !o.vet.trainable) continue;
                const float before = o.vet.veterancy;
                if (arg > 0.0) o.vet.veterancy = promote_levels(o.vet.veterancy, arg);
                if (o.vet.veterancy != before) ++ev.affected;
            }
            break;
        default:
            break;   // the zero-weight powerups are CUT (spec §Scope)
    }
    ev.applied = p;
    crate_events_.push_back(ev);
}

void World::step_crates() {
    if (!crates_any_) return;
    // (1) Pickup: ascending unit index; a ground unit standing on a crate cell.
    for (int i = 0; i < static_cast<int>(units_.size()); ++i) {
        const Unit& u = units_[i];
        if (!u.alive || u.die_time >= 0.0 || u.cargo_of >= 0 || u.is_aircraft) continue;
        for (int s = 0; s < static_cast<int>(crates_.size()); ++s) {
            if (crates_[s].x == u.cell_x && crates_[s].y == u.cell_y && crates_[s].x >= 0) {
                collect_crate(s, i);
                break;
            }
        }
    }
    // (2) UpdateCrates FUN_0056B710: expired -> remove + place a new one.
    for (int s = 0; s < static_cast<int>(crates_.size()); ++s) {
        Crate& c = crates_[s];
        if (c.x < 0) continue;
        if (vet_frame_ - c.start >= static_cast<std::uint64_t>(c.lifetime)) {
            c = Crate{};
            place_random_crate();
        }
    }
}

}  // namespace ra2engine
