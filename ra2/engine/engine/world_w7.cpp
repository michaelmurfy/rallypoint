// Wave 7 shared combat (w7fix-combat): the mission model above the damage
// pipeline -- MissionControl, Enter_Idle_Mode's Area Guard default,
// CanRetaliateToAttacker + Override_Mission, the building / ToProtect call
// for help, the damage scatter, Harmless wake, attack-move.
// docs/gap/wave7/07-shared-combat.md (W7-07-01..12, -20..27) and the
// cross-references W7-02-09, W7-04-19/20, W7-09-07..12.
// Everything here is gated on World::combat_w7().armed (w5_arm_combat with the
// real rulesmd); a synthetic World keeps the pre-W7 behaviour.
#include "world.hpp"

#include <algorithm>

#include "combat.hpp"       // threat_value_unit, unit_in_weapon_range, HitLog
#include "locomotion.hpp"   // leptons_per_frame (fv7c call for help)
#include "orig_math.hpp"
#include "veterancy.hpp"    // has_ability, is_elite
#include "weapon_unit.hpp"  // unit_weapon_for_unit / _building, secondary_view

namespace ra2engine {

namespace {
bool live(const Unit& u) { return u.alive && u.die_time < 0.0 && u.cargo_of < 0; }

double verses_of(const Unit& u, int slot, int ai) {
    WeaponSlot w = slot == 1 ? secondary_view(u) : primary_view(u);
    const std::vector<int>& vs = (slot == u.w_active || w.verses.empty()) ? u.w_verses : w.verses;
    if (vs.empty()) return 1.0;
    if (ai < 0 || ai >= static_cast<int>(vs.size())) ai = 0;
    return vs[static_cast<std::size_t>(ai)] / 100.0;
}

bool armed_unit(const Unit& u) { return u.w_damage > 0 || secondary_view(u).valid; }

// [RE: MissionControl Recruitable=no] Sleep, Harmless, Sticky, Retreat,
// Enter, Eaten, Capture, Harvest, Hunt, Unload, Sabotage, Construction, Selling.
bool mission_recruitable(int m) {
    switch (m) {
        case 0: case 4: case 6: case 7: case 8: case 9: case 10: case 15: case 16: case 17: case 18:
        case 19: case 23:
            return false;
        default:
            return true;
    }
}
}  // namespace

void World::set_house_iq(int h, int iq) {
    if (h < 0) return;
    if (static_cast<int>(house_iq_.size()) <= h) house_iq_.resize(static_cast<std::size_t>(h) + 1, -1);
    house_iq_[static_cast<std::size_t>(h)] = iq;
}

int World::house_iq(int h) const {
    if (h >= 0 && h < static_cast<int>(house_iq_.size()) && house_iq_[static_cast<std::size_t>(h)] >= 0)
        return house_iq_[static_cast<std::size_t>(h)];
    // [rulesmd [IQ]: "When in skirmish mode or when multiplayer AIs are
    // active, the computer IQ is set to the maximum."] A human house is 0.
    return human_commanded(h) ? 0 : w7_rules_.max_iq;
}

// [CONFIRMED-DATA rulesmd [Sleep]..[Open], read through FUN_005B3430]:
// Sleep Zombie/Retaliate=no/Scatter=no; Harmless NoThreat/Retaliate=no;
// Sticky Paralyzed/Scatter=no; Attack Scatter=no; Retreat, Enter, Eaten, Hunt
// Retaliate=no; Capture, Harvest, Unload, Construction Retaliate=no
// Scatter=no; Selling NoThreat/Retaliate=no/Scatter=no. Everything else (and
// YR's Attack Move, which has no section) is Retaliate=yes, Scatter=yes.
World::MissionCtl World::mission_control(int m) {
    MissionCtl c{false, false, true, true, false};
    switch (m) {
        case 0: c.zombie = true; c.retaliate = false; c.scatter = false; break;    // Sleep
        case 1: c.scatter = false; break;                                         // Attack
        case 4: case 7: case 9: case 15: c.retaliate = false; break;              // Retreat Enter Eaten Hunt
        case 6: c.paralyzed = true; c.scatter = false; break;                     // Sticky
        case 8: case 10: case 16: case 18: c.retaliate = false; c.scatter = false; break;
        case 19: c.no_threat = true; c.retaliate = false; c.scatter = false; break;   // Selling
        case 23: c.no_threat = true; c.retaliate = false; break;                  // Harmless
        default: break;
    }
    return c;
}

int World::unit_mission(const Unit& u) const {
    const int m = u.tg.mission;
    if (m == 0 || m == 6 || m == 23) return m;   // Sleep / Sticky / Harmless hold
    if (u.attack_order) return 1;                // Attack (an order or Override_Mission)
    if (m >= 0) return m;                        // Attack Move, ...
    if (u.is_harvester && !u.manual_hold) return 10;   // Harvest
    if (u.moving() || u.has_flight_goal) return 2;     // Move
    return u.tg.area_guard ? 11 : 5;                   // Area Guard / Guard
}

bool World::incoming_scatters(const Unit& o, bool elite_in_cell, bool nokidding) const {
    if (!live(o) || o.frozen || o.is_aircraft || o.deployed) return false;
    if (!mission_control(unit_mission(o)).scatter) return false;
    // [RE: CellClass::Incoming FUN_00481580]
    if (nokidding || elite_in_cell) return true;
    if (w7_rules_.player_scatter) return true;
    if (has_ability(o.vet.veterancy, o.vet.vet_abilities, o.vet.elite_abilities, Ability::Scatter)) return true;
    return w7_rules_.iq_scatter <= house_iq(o.house);
}

void World::enter_idle_mode(int idx) {
    if (!w7_rules_.armed || idx < 0 || idx >= static_cast<int>(units_.size())) return;
    Unit& u = units_[static_cast<std::size_t>(idx)];
    if (!live(u) || u.tg.area_guard || u.tg.mission >= 0 || u.is_aircraft || u.an.t.fixed_wing) return;
    if (u.moving() || u.attack_order || u.target >= 0 || u.target_building >= 0 || u.is_harvester) return;
    if (!armed_unit(u)) return;
    // [RE: 0x737B90..0x737BD0 / 0x51CB0E..0x51CB45]
    const bool ga = house_iq(u.house) >= w7_rules_.iq_guard_area || u.tg.t.default_to_guard_area ||
                    u.w7.t.default_to_guard_area ||
                    has_ability(u.vet.veterancy, u.vet.vet_abilities, u.vet.elite_abilities, Ability::GuardArea);
    if (!ga) return;
    u.tg.area_guard = true;
    u.tg.home_x = u.cell_x;
    u.tg.home_y = u.cell_y;
}

bool World::can_retaliate(const Unit& v, int a, int ab) const {
    if (!live(v) || v.abil.no_retaliate || v.frozen || v.an.t.fixed_wing || v.an.t.missile) return false;
    const int m = unit_mission(v);
    if (!mission_control(m).retaliate) return false;
    int ah = -1, ws = -1, armor = 0;
    if (a >= 0 && a < static_cast<int>(units_.size())) {
        const Unit& A = units_[static_cast<std::size_t>(a)];
        if (!live(A) || A.disguised) return false;   // not against a disguised attacker
        ah = A.house;
        ws = unit_weapon_for_unit(v, A);
        armor = A.armor_ix;
    } else if (ab >= 0 && ab < static_cast<int>(buildings_.size())) {
        const Building& B = buildings_[static_cast<std::size_t>(ab)];
        if (!B.alive || B.state == Building::State::Dead) return false;
        ah = B.house;
        ws = unit_weapon_for_building(v, B.armor_ix, B.bomb_frame >= 0);
        armor = B.armor_ix;
    } else {
        return false;
    }
    if (allied_with(v.house, ah)) return false;                    // [0x4F98B0]
    if (ws < 0) return false;   // GetFireError ILLEGAL: no weapon can engage it
    if (v.w_damage <= 0 && !secondary_view(v).valid) return false;
    const bool human = human_commanded(v.house);
    const bool tarcom = v.target >= 0 || v.target_building >= 0;
    if (human && tarcom) return false;
    // [Rules+0x17EC PlayerReturnFire=no]: a human unit retaliates only in
    // Guard, Area Guard or Patrol (a building always).
    if (human && !w7_rules_.player_return_fire && !(m == 5 || m == 11 || m == 25)) return false;
    if (v.tg.team_suicide) return false;
    // [0x707F09/0x707F1A]: a computer unit switches only to a threat at least
    // as large as its current target.
    if (!human && tarcom && a >= 0 && v.target >= 0 && v.target < static_cast<int>(units_.size())) {
        const int vi = static_cast<int>(&v - units_.data());
        if (threat_value_unit(*this, v, vi, units_[static_cast<std::size_t>(a)]) <
            threat_value_unit(*this, v, vi, units_[static_cast<std::size_t>(v.target)]))
            return false;
    }
    // W7-07-04 [RE: 0x707F2x]: Verses <= 1% never retaliates.
    if (verses_of(v, ws, armor) <= 0.01) return false;
    return true;
}

void World::w7_override_attack(int vi, int a, int ab) {
    Unit& v = units_[static_cast<std::size_t>(vi)];
    if (!v.tg.overridden) {
        v.tg.resume_area_guard = v.tg.area_guard;
        if (v.moving()) {
            v.tg.resume_x = v.path.back().x;
            v.tg.resume_y = v.path.back().y;
        } else {
            v.tg.resume_x = v.tg.resume_y = -1;
        }
    }
    v.tg.overridden = true;
    v.tg.opportunity = false;
    v.target = a;
    v.target_building = a >= 0 ? -1 : ab;
    v.attack_order = true;
    v.guard_x = v.cell_x;
    v.guard_y = v.cell_y;
    // finish the cell being entered, then the attack mission takes over
    if (v.moving() && v.path_idx < v.path.size()) v.path.resize(v.path_idx + 1);
}

void World::order_scatter_from(int idx, int ax, int ay) {
    if (idx < 0 || idx >= static_cast<int>(units_.size())) return;
    Unit& u = units_[static_cast<std::size_t>(idx)];
    if (!live(u) || u.frozen || u.is_aircraft || u.moving()) return;
    static const int kDx[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    static const int kDy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
    const int dx = u.cell_x - ax, dy = u.cell_y - ay;
    int away = combat_rng_.random(0, 7);
    if (dx != 0 || dy != 0) {
        const int sx = dx > 0 ? 1 : dx < 0 ? -1 : 0, sy = dy > 0 ? 1 : dy < 0 ? -1 : 0;
        for (int k = 0; k < 8; ++k)
            if (kDx[k] == sx && kDy[k] == sy) away = k;
        away += u.is_vehicle ? combat_rng_.random(0, 2) - 1 : combat_rng_.random(0, 4) - 2;
    }
    for (int k = 0; k < 8; ++k) {
        const int d = ((away + k) % 8 + 8) % 8;
        const int x = u.cell_x + kDx[d], y = u.cell_y + kDy[d];
        // w8fix-layer [RE: FUN_00742880 -> IsCellOccupied on the mover's own
        // layer]: a free spot on the plane of the layer the step lands on.
        const int lay = scatter_cell_layer(u, x, y);
        const bool taken = occ_.subcells() ? !occ_.can_enter(x, y, idx, lay < 0 ? Occupancy::kAnyLayer : lay)
                                           : unit_at_cell(x, y) >= 0;
        if (lay < 0 || taken || building_at_cell(x, y) >= 0) continue;   // fv7c
        const UnitTgt keep = u.tg;
        u.target = -1;
        u.target_building = -1;
        u.attack_order = false;
        order_unit_move(idx, x, y);
        // a scatter step keeps the unit's guard posture (it is not an order)
        units_[static_cast<std::size_t>(idx)].tg.area_guard = keep.area_guard;
        units_[static_cast<std::size_t>(idx)].tg.home_x = keep.home_x;
        units_[static_cast<std::size_t>(idx)].tg.home_y = keep.home_y;
        return;
    }
}

// fv7c [RE: FUN_004D96B0, decompiled in D:\ra2-re\w7sus-combat-out\d1.txt]:
// the value candidate `ci` adds to a call for help against attacker `a`.
// ThreatPosed is TechnoClass vtbl+0x2C0 (FUN_00707FC0 -> type +0x670).
//   - already targeting the attacker: -ThreatPosed (spent from the budget);
//   - busy with an armed TarCom, in a non-base-defense team, or in Harvest: 0;
//   - else ThreatPosed<<10 divided by the frames to close to weapon-0 range
//     ((distance - range) / speed; a speed below 2 divides by 1), at least 1.
int World::help_value(int ci, int a, std::uint8_t tf) const {
    const Unit& u = units_[static_cast<std::size_t>(ci)];
    const Unit& A = units_[static_cast<std::size_t>(a)];
    const int threat = u.tg.t.threat_posed;
    if (u.target == a) return -threat;
    if (u.target >= 0 && u.target < static_cast<int>(units_.size()) &&
        armed_unit(units_[static_cast<std::size_t>(u.target)]))
        return 0;
    if (u.target_building >= 0 && u.target_building < static_cast<int>(buildings_.size())) {
        const Building& B = buildings_[static_cast<std::size_t>(u.target_building)];
        if (B.type >= 0 && B.type < static_cast<int>(btypes().size()) && btypes()[static_cast<std::size_t>(B.type)].armed)
            return 0;
    }
    if ((tf & kTeamMember) && !(tf & kTeamBaseDefense)) return 0;
    if (unit_mission(u) == 10) return 0;   // Harvest
    if (threat == 0) return 0;
    const double rng = u.w_active == 0 ? u.w_range : (u.vet.base.range > 0.0 ? u.vet.base.range : u.w_range);
    const int dist = om::ftol(om::hypot((u.fx - A.fx) * 256.0, (u.fy - A.fy) * 256.0));
    const int d = dist - om::ftol(rng * 256.0);
    if (d < 1) return threat << 10;
    const int spd = leptons_per_frame(unit_speed_cps(ci));
    int t = d / (spd < 2 ? 1 : spd);
    if (t <= 1) t = 1;
    const int v = (threat << 10) / t;
    return v < 2 ? 1 : v;
}

// fv7c (13b #3/#4): ONE call for help, FUN_00707500 (gamemd 0x707500; listing
// D:\ra2-re\w7-shc-B-out\b5.txt + disassembly 0x707560..0x707C33), for every
// computer house -- it replaces the W7-04-18 skirmish-only ai_protect and the
// earlier most-expensive-six stand-in. Callers: a ToProtect unit's hit
// (TechnoClass::ReceiveDamage 0x701C74) and every building hit
// (BuildingClass::ReceiveDamage 0x44223C), both with the real attacker from
// the hit log, so splash and opportunity hits call too.
//   head: no call for an allied attacker, a human victim house, an attacker
//     in limbo, an armed campaign victim, an attacker that is not Infantry/Unit
//     (an AircraftType: fixed-wing or a missile -- a Jumpjet Rocketeer IS
//     infantry), an Insignificant victim, or while the attacker's +0x650
//     cooldown runs;
//   budget: attacker ThreatPosed x Rules+0xB18 (ComputerBaseDefenseResponse);
//   candidates, infantry array then unit array: same owner, no team or an
//     IsBaseDefense team, armed, Recruitable mission (campaign only;
//     SessionType != 0 skips it), GetFireError(attacker, 0) != ILLEGAL,
//     reachable, help_value != 0; a negative value is spent at once; the six
//     best kept with the original's replace-the-minimum quirk;
//   then sorted (exchange sort, descending), each recruit gets Mission
//     Area_Guard (roll(0,99) >= 66, or a base-defense team) or Rescue, and
//     TarCom = attacker; the spent ThreatPosed passing the budget stops it and
//     arms the attacker's cooldown, ftol(BaseDefenseDelay x 900) frames.
// [RE-OPEN, not modelled: the x100 / x10 value multipliers taken when the
// victim's +0x218 equals itself; the unit-array tests UnitType +0x5EC, unit
// +0x2E4 and victim +0x2DC; FUN_0056CC30 zone reachability -- a water/land
// domain test stands in.] [RE-FALLBACK: Rescue (21) has no clone mission; it
// is an attack order on the attacker. Area Guard keeps the recruit's area
// guard home at its cell with the attacker as TarCom.]
void World::call_for_help(int victim_house, int victim_unit, int victim_building, int attacker) {
    if (attacker < 0 || attacker >= static_cast<int>(units_.size())) return;
    const Unit& A = units_[static_cast<std::size_t>(attacker)];
    if (!A.alive || A.die_time >= 0.0) return;
    if (allied_with(victim_house, A.house) || human_commanded(victim_house)) return;
    if (A.cargo_of >= 0 || A.garrison_of >= 0) return;   // +0x81 InLimbo
    if (victim_unit >= 0) {
        const Unit& V = units_[static_cast<std::size_t>(victim_unit)];
        if (playfield_.campaign && armed_unit(V)) return;
        if (V.tg.t.insignificant) return;
    }
    if (victim_building >= 0) {
        const Building& B = buildings_[static_cast<std::size_t>(victim_building)];
        if (playfield_.campaign && B.type >= 0 && B.type < static_cast<int>(btypes().size()) &&
            btypes()[static_cast<std::size_t>(B.type)].armed)
            return;
        if (B.tg.insignificant) return;
    }
    if (A.an.t.fixed_wing || A.an.t.missile) return;   // WhatAmI not 0xF / 1
    const int now = static_cast<int>(vet_frame_);
    if (A.tg.help_frame > now) return;   // the +0x650 timer
    int budget = A.tg.t.threat_posed * static_cast<int>(w7_rules_.call_for_help_budget);
    const int nu = static_cast<int>(units_.size());
    auto tflags = [&](int i) -> std::uint8_t {
        return i < static_cast<int>(team_flags_.size()) ? team_flags_[static_cast<std::size_t>(i)] : 0;
    };
    int objs[6] = {0}, vals[6] = {0};
    int count = 0, minv = 0;
    for (int pass = 0; pass < 2; ++pass) {   // the infantry array, then the units
        for (int i = 0; i < nu; ++i) {
            if (budget < 1) break;
            const Unit& u = units_[static_cast<std::size_t>(i)];
            if (u.is_infantry != (pass == 0)) continue;
            if (!live(u) || u.house != victim_house || u.garrison_of >= 0 || u.frozen) continue;
            if (u.an.t.fixed_wing || u.an.t.spawned || u.an.t.missile) continue;   // AircraftClass
            const std::uint8_t tf = tflags(i);
            if ((tf & kTeamMember) && !(tf & kTeamBaseDefense)) continue;
            if (!armed_unit(u)) continue;
            if (u.deployed && !u.deploy_fire) continue;   // a deployed Desolator cannot go
            if (playfield_.campaign && !mission_recruitable(unit_mission(u))) continue;
            if (!unit_primary_can_fire_at(u, A)) continue;
            // Movement zone stand-in: a ship only against an attacker on
            // water; a land unit only against one it can walk to.
            if (u.movement_domain == MovementDomain::Water && A.movement_domain != MovementDomain::Water) continue;
            if (u.movement_domain == MovementDomain::Land && A.movement_domain == MovementDomain::Water) continue;
            const int v = help_value(i, attacker, tf);
            if (v == 0) continue;
            if (v < 0) {
                budget += v;
            } else if (count < 6) {
                objs[count] = i;
                vals[count] = v;
                ++count;
            } else if (v > minv) {
                int nm = v;
                for (int k = 0; k < count; ++k) {
                    if (vals[k] == minv) {
                        vals[k] = v;
                        objs[k] = i;
                    } else if (vals[k] < nm) {
                        nm = vals[k];
                    }
                }
                minv = nm;
            }
        }
    }
    int spent = 0;
    if (budget > 0) {
        for (int i = 0; i + 1 < count; ++i)
            for (int j = i + 1; j < count; ++j)
                if (vals[i] < vals[j]) {
                    std::swap(vals[i], vals[j]);
                    std::swap(objs[i], objs[j]);
                }
        for (int k = 0; k < count; ++k) {
            const int ri = objs[k];
            const bool base_def = (tflags(ri) & kTeamBaseDefense) != 0;
            const int roll = combat_rng_.random(0, 99);
            if (roll >= 66 || base_def) {
                // Mission_Set(Area_Guard) + Assign_Target(attacker)
                Unit& r = units_[static_cast<std::size_t>(ri)];
                r.tg.overridden = false;
                r.tg.mission = -1;
                r.attack_order = false;
                r.target_building = -1;
                r.target = attacker;
                r.tg.area_guard = true;
                r.tg.home_x = r.cell_x;
                r.tg.home_y = r.cell_y;
                r.guard_x = r.cell_x;
                r.guard_y = r.cell_y;
            } else {
                // Mission_Set(Rescue) + Assign_Target(attacker) [RE-FALLBACK]
                order_unit_attack(ri, attacker);
            }
            spent += units_[static_cast<std::size_t>(ri)].tg.t.threat_posed;
            if (spent > budget) break;
        }
    }
    if (spent > budget) units_[static_cast<std::size_t>(attacker)].tg.help_frame = now + w7_rules_.base_defense_delay;
}

void World::wake_unit(int idx) {
    if (idx < 0 || idx >= static_cast<int>(units_.size())) return;
    Unit& u = units_[static_cast<std::size_t>(idx)];
    u.tg.mission = -1;
    enter_idle_mode(idx);
}

void World::group_attack_move(int issuer, const std::vector<int>& ids, int cx, int cy) {
    std::vector<int> movers;
    for (int i : ids) {
        if (i < 0 || i >= static_cast<int>(units_.size())) continue;
        const Unit& u = units_[static_cast<std::size_t>(i)];
        if (!can_command(issuer, u.house) || !live(u)) continue;
        // [RE: TechnoType +0x6C8 PreventAttackMove]: refused -> a plain move.
        movers.push_back(i);
    }
    if (movers.empty()) return;
    group_move(issuer, movers, cx, cy);
    for (int i : movers) {
        Unit& u = units_[static_cast<std::size_t>(i)];
        if (u.tg.t.prevent_attack_move || !armed_unit(u)) continue;
        u.tg.mission = 29;
        u.tg.amove_x = u.moving() ? u.path.back().x : cx;
        u.tg.amove_y = u.moving() ? u.path.back().y : cy;
        u.tg.scan_timer = 0;
    }
}

void World::step_missions_w7() {
    if (!w7_rules_.armed) return;
    const int nu = static_cast<int>(units_.size());
    // W7-04-19 / W7-07-12 [RE: FUN_00708710 head; TeamType Aggressive (+0xAD)
    // / Suicide (+0xAF)]: an Aggressive, non-Suicide AI team member scans on
    // the move; a Suicide member never retaliates.
    // fv7c (13b 1b): campaign teams too -- the original reads the TeamType
    // through Team +0x5D4 for every house (campaign_team_flags_, published by
    // the campaign runtime each logic frame). team_flags_ also feeds the call
    // for help's team test (FUN_00707500: no team, or an IsBaseDefense team).
    for (Unit& u : units_) u.tg.team_aggressive = u.tg.team_suicide = false;
    std::vector<std::uint8_t> in_team(static_cast<std::size_t>(nu), 0);
    team_flags_.assign(static_cast<std::size_t>(nu), 0);
    for (const HouseAiRec& rec : ai_houses_) {
        if (!rec.is_ai) continue;
        const std::vector<TeamType>& tts = rec.cfg.team.aimd.teams;
        for (const TeamInstance& t : rec.team_state.teams) {
            if (t.teamtype < 0 || t.teamtype >= static_cast<int>(tts.size())) continue;
            const TeamType& tt = tts[static_cast<std::size_t>(t.teamtype)];
            for (int m : t.members) {
                if (m < 0 || m >= nu) continue;
                in_team[static_cast<std::size_t>(m)] = 1;
                units_[static_cast<std::size_t>(m)].tg.team_aggressive = tt.aggressive && !tt.suicide;
                units_[static_cast<std::size_t>(m)].tg.team_suicide = tt.suicide;
                team_flags_[static_cast<std::size_t>(m)] |= static_cast<std::uint8_t>(
                    kTeamMember | (tt.is_base_defense ? kTeamBaseDefense : 0));
            }
        }
    }
    for (int m = 0; m < nu && m < static_cast<int>(campaign_team_flags_.size()); ++m) {
        const std::uint8_t f = campaign_team_flags_[static_cast<std::size_t>(m)];
        if (!(f & kTeamMember)) continue;
        team_flags_[static_cast<std::size_t>(m)] |= static_cast<std::uint8_t>(f & (kTeamMember | kTeamBaseDefense));
        Unit& u = units_[static_cast<std::size_t>(m)];
        u.tg.team_suicide = (f & kTeamSuicide) != 0;
        u.tg.team_aggressive = (f & kTeamAggressive) && !(f & kTeamSuicide);
        // (the idle-mode "not in a team" test below keeps its skirmish-only
        // in_team; campaign members' idle transition is unchanged here)
    }
    std::vector<HitRec> hits;
    hits.swap(w7_hits_);
    std::vector<std::uint8_t> handled(static_cast<std::size_t>(nu), 0);
    for (const HitRec& h : hits) {
        if (h.building) {
            // [RE: BuildingClass::ReceiveDamage 0x44223C -> FUN_00707500]
            if (h.victim < 0 || h.victim >= static_cast<int>(buildings_.size())) continue;
            const Building& b = buildings_[static_cast<std::size_t>(h.victim)];
            if (!b.alive || b.state == Building::State::Dead) continue;
            call_for_help(b.house, -1, h.victim, h.attacker);
            continue;
        }
        if (h.victim < 0 || h.victim >= nu) continue;
        if (h.incoming) {
            // W7-07-26 [RE: CellClass::Incoming(shooter, 1, 0) FUN_00481580]:
            // the occupants of the target cell may step away from the shooter.
            const Unit& t = units_[static_cast<std::size_t>(h.victim)];
            const int cx = t.cell_x, cy = t.cell_y;
            bool elite = false;
            // w8fix-layer [RE: Incoming walks one content chain -- the deck
            // +0xE8 or the ground +0xE4]: only the target's own layer.
            for (const Unit& o : units_)
                if (live(o) && o.cell_x == cx && o.cell_y == cy && o.on_deck == t.on_deck && is_elite(o.vet.veterancy))
                    elite = true;
            for (int o = 0; o < nu; ++o) {
                const Unit& ou = units_[static_cast<std::size_t>(o)];
                if (!live(ou) || ou.cell_x != cx || ou.cell_y != cy || ou.moving() || ou.on_deck != t.on_deck) continue;
                if (ou.target >= 0 || ou.target_building >= 0) continue;
                if (incoming_scatters(ou, elite, false)) order_scatter_from(o, h.ax, h.ay);
            }
            continue;
        }
        Unit& v = units_[static_cast<std::size_t>(h.victim)];
        if (!live(v) || v.hp <= 0) continue;
        const int m = unit_mission(v);
        const MissionCtl mc = mission_control(m);
        // W7-09-08 [RE: FootClass::ReceiveDamage 0x4D7250]: a NoThreat
        // (Harmless) non-Zombie unit hit by a source wakes into its idle mode.
        if (mc.no_threat && !mc.zombie && (h.attacker >= 0 || h.attacker_building >= 0)) {
            wake_unit(h.victim);
        }
        const bool had_order = v.target >= 0 || v.target_building >= 0 || v.moving();
        // W7-07-01 [RE: CanRetaliateToAttacker 0x707C40, ReceiveDamage tail
        // 0x701F00..0x701FCC]: retaliate (Override_Mission(Attack)).
        bool retaliated = false;
        if (!handled[static_cast<std::size_t>(h.victim)] && !h.disguised && can_retaliate(v, h.attacker, h.attacker_building)) {
            bool ok = true;
            if (human_commanded(v.house)) {
                // a human unit skips an attacker beyond (Sight + 0.5) cells
                int tx = h.ax, ty = h.ay;
                if (h.attacker >= 0) { tx = units_[static_cast<std::size_t>(h.attacker)].cell_x; ty = units_[static_cast<std::size_t>(h.attacker)].cell_y; }
                const double dx = (tx - v.cell_x) * 256.0, dy = (ty - v.cell_y) * 256.0;
                ok = om::sqrt(dx * dx + dy * dy) <= (v.sight + 0.5) * 256.0;
            }
            if (ok) {
                w7_override_attack(h.victim, h.attacker, h.attacker_building);
                retaliated = true;
                handled[static_cast<std::size_t>(h.victim)] = 1;
            }
        }
        // W7-07-09 [RE: TechnoClass::ReceiveDamage 0x701C43 -> FUN_00707500]:
        // a computer ToProtect=yes techno calls for help. fv7c: every computer
        // house (the skirmish AI's ai_protect path is merged in here).
        if (v.tg.t.to_protect && h.attacker >= 0)
            call_for_help(v.house, h.victim, -1, h.attacker);
        // W7-07-24 / W7-02-09 / W7-04-20 / W7-09-11 [RE: 0x701ED5..0x702196,
        // InfantryClass::ReceiveDamage 0x518A6C]: the damage scatter. Not after
        // a retaliation; the mission must allow it; a unit with a target (a
        // non-Fraidycat infantry) or a move stays; a computer unit always
        // scatters, a human one only with PlayerScatter or the SCATTER ability.
        if (retaliated || had_order || v.is_aircraft || !mc.scatter || v.deployed || v.frozen) continue;
        if (handled[static_cast<std::size_t>(h.victim)] == 2) continue;
        const bool human = human_commanded(v.house);
        const bool may = !human || w7_rules_.player_scatter ||
                         has_ability(v.vet.veterancy, v.vet.vet_abilities, v.vet.elite_abilities, Ability::Scatter);
        if (!may) continue;
        handled[static_cast<std::size_t>(h.victim)] = 2;
        int ax = h.ax, ay = h.ay;
        if (h.attacker >= 0) { ax = units_[static_cast<std::size_t>(h.attacker)].cell_x; ay = units_[static_cast<std::size_t>(h.attacker)].cell_y; }
        order_scatter_from(h.victim, ax, ay);
    }
    // W7-07-05: the idle transition (arrival, factory exit, a target's end).
    for (int i = 0; i < nu; ++i) {
        const Unit& u = units_[static_cast<std::size_t>(i)];
        // [RE: 0x737B90: "not in a team"]
        if (in_team[static_cast<std::size_t>(i)]) continue;
        if (!u.tg.area_guard && u.tg.mission < 0 && live(u) && !u.moving() && u.target < 0 &&
            u.target_building < 0 && !u.attack_order)
            enter_idle_mode(i);
    }
}

}  // namespace ra2engine
