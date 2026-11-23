#include "slave_miner.hpp"

#include <algorithm>
#include <climits>
#include <cmath>

namespace ra2engine {

namespace {

constexpr int kRingDx[8] = {0, 1, 1, 1, 0, -1, -1, -1};
constexpr int kRingDy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

bool is_passable(const std::vector<std::uint8_t>& passable, int gw, int gh, int x, int y) {
    if (x < 0 || y < 0 || x >= gw || y >= gh) return false;
    return passable[(size_t)y * (size_t)gw + (size_t)x] != 0;
}

int count_slaves_building(const std::vector<Unit>& units, int building) {
    int n = 0;
    for (const Unit& u : units)
        if (u.alive && u.die_time < 0.0 && u.is_slave && !u.slave_master_is_unit &&
            u.slave_master == building)
            ++n;
    return n;
}

int count_slaves_unit(const std::vector<Unit>& units, int master) {
    int n = 0;
    for (const Unit& u : units)
        if (u.alive && u.die_time < 0.0 && u.is_slave && u.slave_master_is_unit &&
            u.slave_master == master)
            ++n;
    return n;
}

bool building_master_alive(const Building& b) {
    return b.alive && b.state == Building::State::Idle;
}

bool unit_master_alive(const Unit& u) {
    return u.alive && u.die_time < 0.0 && u.is_slave_master;
}

int killer_house(const std::vector<Unit>& units, int killed_by) {
    if (killed_by < 0 || killed_by >= (int)units.size()) return -1;
    const Unit& k = units[(size_t)killed_by];
    return k.house;
}

}  // namespace

bool slave_pad_cell(const Building& b, const std::vector<std::uint8_t>& passable, int gw, int gh,
                    const Occupancy& occ, int& out_x, int& out_y) {
    const int dx = b.cx + b.fw / 2, dy = b.cy + b.fh;
    if (is_passable(passable, gw, gh, dx, dy) && occ.is_free(dx, dy)) {
        out_x = dx;
        out_y = dy;
        return true;
    }
    for (int y = b.cy - 1; y <= b.cy + b.fh; ++y)
        for (int x = b.cx - 1; x <= b.cx + b.fw; ++x) {
            const bool edge =
                y == b.cy - 1 || y == b.cy + b.fh || x == b.cx - 1 || x == b.cx + b.fw;
            if (!edge) continue;
            if (is_passable(passable, gw, gh, x, y) && occ.is_free(x, y)) {
                out_x = x;
                out_y = y;
                return true;
            }
        }
    out_x = dx;
    out_y = dy;
    return is_passable(passable, gw, gh, dx, dy);
}

bool slave_pad_cell_unit(const Unit& master, const std::vector<std::uint8_t>& passable, int gw,
                         int gh, const Occupancy& occ, int& out_x, int& out_y) {
    const int mx = master.cell_x, my = master.cell_y;
    for (int d = 0; d < 8; ++d) {
        const int x = mx + kRingDx[d], y = my + kRingDy[d];
        if (is_passable(passable, gw, gh, x, y) && occ.is_free(x, y)) {
            out_x = x;
            out_y = y;
            return true;
        }
    }
    for (int r = 2; r <= 4; ++r)
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
                if (std::abs(dx) != r && std::abs(dy) != r) continue;
                const int x = mx + dx, y = my + dy;
                if (is_passable(passable, gw, gh, x, y) && occ.is_free(x, y)) {
                    out_x = x;
                    out_y = y;
                    return true;
                }
            }
    out_x = mx;
    out_y = my;
    return false;
}

int slave_miner_nearest_ore_dist(const OreField& ore, int cx, int cy, int radius, int& out_x,
                                 int& out_y) {
    if (radius < 0) return -1;
    bool found = false;
    int best = INT_MAX;
    const int y0 = std::max(0, cy - radius), y1 = std::min(ore.h() - 1, cy + radius);
    const int x0 = std::max(0, cx - radius), x1 = std::min(ore.w() - 1, cx + radius);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            if (!ore.has_ore(x, y)) continue;
            const int dist = std::max(std::abs(x - cx), std::abs(y - cy));
            if (dist > radius) continue;
            if (!found || dist < best || (dist == best && (y < out_y || (y == out_y && x < out_x)))) {
                found = true;
                best = dist;
                out_x = x;
                out_y = y;
            }
        }
    return found ? best : -1;
}

bool slave_miner_pick_scoot(const OreField& ore, int mx, int my, int short_scan, int long_scan,
                            int correction, int& out_x, int& out_y) {
    if (short_scan <= 0) short_scan = kSlaveMinerShortScanDefault;
    if (long_scan <= 0) long_scan = kSlaveMinerLongScanDefault;
    if (correction < 0) correction = kSlaveMinerScanCorrectionDefault;
    int sx = 0, sy = 0, lx = 0, ly = 0;
    const int short_d = slave_miner_nearest_ore_dist(ore, mx, my, short_scan, sx, sy);
    const int long_d = slave_miner_nearest_ore_dist(ore, mx, my, long_scan, lx, ly);
    if (long_d < 0) return false;
    // [RE: FUN_006AFD60]: short miss → long scan for a new patch (KickFrameDelay
    // gates how often the caller asks). Short hit → scoot only if long is at
    // least ScanCorrection cells closer (@0x6B01A3).
    if (short_d < 0) {
        out_x = lx;
        out_y = ly;
        return true;
    }
    if (long_d + correction >= short_d) return false;
    if (lx == sx && ly == sy) return false;
    out_x = lx;
    out_y = ly;
    return true;
}

void liberate_orphaned_slaves(std::vector<Unit>& units, const std::vector<Building>& buildings) {
    for (Unit& u : units) {
        if (!u.is_slave) continue;
        bool ok = false;
        int lib = -1;
        const int m = u.slave_master;
        if (u.slave_master_is_unit) {
            ok = m >= 0 && m < (int)units.size() && unit_master_alive(units[(size_t)m]);
            if (!ok && m >= 0 && m < (int)units.size())
                lib = killer_house(units, units[(size_t)m].vet.killed_by);
        } else {
            ok = m >= 0 && m < (int)buildings.size() && building_master_alive(buildings[(size_t)m]);
            if (!ok && m >= 0 && m < (int)buildings.size())
                lib = killer_house(units, buildings[(size_t)m].killed_by);
        }
        if (ok) continue;
        u.is_slave = false;
        u.slave_master = -1;
        u.slave_master_is_unit = false;
        u.slave = SlaveState::None;
        u.bales_ore = 0.0;
        u.bales_gems = 0.0;
        u.harv_timer = 0.0;
        if (lib >= 0) u.house = lib;
    }
}

void step_slave_miners(std::vector<Unit>& units, std::vector<Building>& buildings,
                       const std::vector<BuildingTypeDef>& types, OreField& ore,
                       const std::vector<std::uint8_t>& passable, int gw, int gh, Occupancy& occ,
                       double dt, const OreValues& values, double* house_credits, int n_houses,
                       int slave_scan, const SlaveMinerScootRules* scoot, const std::uint8_t* ai_mask,
                       std::vector<SlaveSpawn>& spawns, std::vector<SlaveMove>& moves,
                       std::vector<SlaveScoot>& scoots) {
    if (slave_scan <= 0) slave_scan = kSlaveMinerSlaveScanDefault;
    liberate_orphaned_slaves(units, buildings);

    const int frame_steps_early =
        dt > 0.0 ? std::max(1, (int)std::lround(dt * kSlaveLogicFps)) : 0;
    auto house_is_ai = [&](int house) -> bool {
        if (!scoot || !ai_mask || n_houses <= 0) return false;
        if (house < 0 || house >= n_houses) return false;
        return ai_mask[(size_t)house] != 0;
    };
    auto try_scoot_master = [&](int master, bool master_is_unit, int house, int mx, int my,
                                int& guard_frames) {
        if (!house_is_ai(house)) return;
        int ox = 0, oy = 0;
        const int short_d =
            slave_miner_nearest_ore_dist(ore, mx, my, scoot->short_scan, ox, oy);
        if (short_d >= 0)
            guard_frames = 0;
        else
            guard_frames += frame_steps_early;
        int tx = 0, ty = 0;
        if (!slave_miner_pick_scoot(ore, mx, my, scoot->short_scan, scoot->long_scan,
                                    scoot->correction, tx, ty))
            return;
        // No local ore: wait KickFrameDelay (FUN_006B1020) before a long scoot.
        if (short_d < 0 && scoot->kick_delay > 0 && guard_frames < scoot->kick_delay) return;
        for (const SlaveScoot& s : scoots)
            if (s.master == master && s.master_is_unit == master_is_unit) return;
        guard_frames = 0;
        scoots.push_back({master, master_is_unit, tx, ty});
    };

    // Detect slave deaths: arm SlaveRegenRate on the living master.
    for (Unit& u : units) {
        if (!u.is_slave) continue;
        if (u.alive && u.die_time < 0.0) continue;
        const int m = u.slave_master;
        const bool on_unit = u.slave_master_is_unit;
        u.is_slave = false;
        u.slave_master_is_unit = false;
        if (on_unit) {
            if (m < 0 || m >= (int)units.size() || !unit_master_alive(units[(size_t)m])) continue;
            Unit& master = units[(size_t)m];
            master.slave_needs_regen = true;
            master.slave_spawn_frames =
                master.slave_regen_rate > 0 ? master.slave_regen_rate : 1;
        } else {
            if (m < 0 || m >= (int)buildings.size()) continue;
            Building& b = buildings[(size_t)m];
            if (!building_master_alive(b)) continue;
            if (b.type < 0 || b.type >= (int)types.size()) continue;
            const BuildingTypeDef& t = types[(size_t)b.type];
            if (t.enslaves.empty()) continue;
            b.slave_needs_regen = true;
            b.slave_spawn_frames = t.slave_regen_rate > 0 ? t.slave_regen_rate : 1;
        }
    }

    const int frame_steps = dt > 0.0 ? std::max(1, (int)std::lround(dt * kSlaveLogicFps)) : 0;

    // --- Building masters (YAREFN) ---
    for (int bi = 0; bi < (int)buildings.size(); ++bi) {
        Building& b = buildings[(size_t)bi];
        if (!building_master_alive(b)) continue;
        if (b.type < 0 || b.type >= (int)types.size()) continue;
        const BuildingTypeDef& t = types[(size_t)b.type];
        if (t.enslaves.empty() || t.slaves_number <= 0) continue;

        const int have = count_slaves_building(units, bi);
        int pending = 0;
        for (const SlaveSpawn& s : spawns)
            if (!s.master_is_unit && s.master == bi) ++pending;
        if (have + pending >= t.slaves_number) {
            if (!b.slave_needs_regen) b.slave_spawn_frames = 0;
            continue;
        }

        if (b.slave_spawn_frames > 0) {
            b.slave_spawn_frames -= frame_steps;
            if (b.slave_spawn_frames > 0) continue;
            b.slave_spawn_frames = 0;
        }

        int sx = 0, sy = 0;
        if (!slave_pad_cell(b, passable, gw, gh, occ, sx, sy)) continue;
        SlaveSpawn s;
        s.master = bi;
        s.master_is_unit = false;
        s.gx = sx;
        s.gy = sy;
        s.house = b.house;
        s.storage_bales = t.slave_storage > 0 ? t.slave_storage : 4;
        spawns.push_back(s);

        const bool was_regen = b.slave_needs_regen;
        b.slave_needs_regen = false;
        const int remain = t.slaves_number - have - pending - 1;
        if (remain > 0) {
            const int rate = was_regen ? t.slave_regen_rate : t.slave_reload_rate;
            b.slave_spawn_frames = rate > 0 ? rate : 1;
        }
    }

    // --- Unit masters (undeployed SMIN) [RE: FUN_006AF1A0 / FUN_006AF6C0] ---
    for (int ui = 0; ui < (int)units.size(); ++ui) {
        Unit& master = units[(size_t)ui];
        if (!unit_master_alive(master) || master.slaves_number <= 0) continue;

        const int have = count_slaves_unit(units, ui);
        int pending = 0;
        for (const SlaveSpawn& s : spawns)
            if (s.master_is_unit && s.master == ui) ++pending;
        if (have + pending >= master.slaves_number) {
            if (!master.slave_needs_regen) master.slave_spawn_frames = 0;
            continue;
        }

        if (master.slave_spawn_frames > 0) {
            master.slave_spawn_frames -= frame_steps;
            if (master.slave_spawn_frames > 0) continue;
            master.slave_spawn_frames = 0;
        }

        int sx = 0, sy = 0;
        if (!slave_pad_cell_unit(master, passable, gw, gh, occ, sx, sy)) continue;
        SlaveSpawn s;
        s.master = ui;
        s.master_is_unit = true;
        s.gx = sx;
        s.gy = sy;
        s.house = master.house;
        s.storage_bales = master.slave_worker_storage > 0 ? master.slave_worker_storage : 4;
        spawns.push_back(s);

        const bool was_regen = master.slave_needs_regen;
        master.slave_needs_regen = false;
        const int remain = master.slaves_number - have - pending - 1;
        if (remain > 0) {
            const int rate = was_regen ? master.slave_regen_rate : master.slave_reload_rate;
            master.slave_spawn_frames = rate > 0 ? rate : 1;
        }
    }

    // AI scoot / redeploy [RE: FUN_006AFD60 / FUN_006B1020].
    for (int bi = 0; bi < (int)buildings.size(); ++bi) {
        Building& b = buildings[(size_t)bi];
        if (!building_master_alive(b)) continue;
        if (b.type < 0 || b.type >= (int)types.size()) continue;
        const BuildingTypeDef& t = types[(size_t)b.type];
        if (t.enslaves.empty() || t.slaves_number <= 0) continue;
        try_scoot_master(bi, false, b.house, b.cx + b.fw / 2, b.cy + b.fh / 2,
                         b.slave_guard_frames);
    }
    for (int ui = 0; ui < (int)units.size(); ++ui) {
        Unit& master = units[(size_t)ui];
        if (!unit_master_alive(master) || master.slaves_number <= 0) continue;
        try_scoot_master(ui, true, master.house, master.cell_x, master.cell_y,
                         master.slave_guard_frames);
    }

    // --- Slave gather AI ---
    for (int idx = 0; idx < (int)units.size(); ++idx) {
        Unit& u = units[(size_t)idx];
        if (!u.alive || u.die_time >= 0.0 || !u.is_slave) continue;
        if (u.manual_hold) continue;
        const int m = u.slave_master;
        int pad_x = 0, pad_y = 0;
        int master_house = u.house;
        int reload = 25;
        bool master_ok = false;

        if (u.slave_master_is_unit) {
            if (m < 0 || m >= (int)units.size() || !unit_master_alive(units[(size_t)m])) {
                u.is_slave = false;
                u.slave = SlaveState::None;
                continue;
            }
            const Unit& master = units[(size_t)m];
            master_ok = true;
            master_house = master.house;
            reload = master.slave_reload_rate;
            (void)slave_pad_cell_unit(master, passable, gw, gh, occ, pad_x, pad_y);
        } else {
            if (m < 0 || m >= (int)buildings.size() || !building_master_alive(buildings[(size_t)m])) {
                u.is_slave = false;
                u.slave = SlaveState::None;
                continue;
            }
            const Building& master = buildings[(size_t)m];
            master_ok = true;
            master_house = master.house;
            if (master.type >= 0 && master.type < (int)types.size())
                reload = types[(size_t)master.type].slave_reload_rate;
            (void)slave_pad_cell(master, passable, gw, gh, occ, pad_x, pad_y);
        }
        if (!master_ok) continue;
        (void)master_house;

        switch (u.slave) {
            case SlaveState::None:
                u.slave = SlaveState::SeekOre;
                u.harv_timer = kRescanSecs;
                [[fallthrough]];
            case SlaveState::SeekOre: {
                u.harv_timer += dt;
                if (u.harv_timer < kRescanSecs) break;
                u.harv_timer = 0.0;
                int ox = 0, oy = 0;
                if (!find_reachable_ore(ore, passable, gw, gh, occ, idx, u.cell_x, u.cell_y,
                                        slave_scan, u.harv_skip_x, u.harv_skip_y, ox, oy)) {
                    break;
                }
                u.harv_cell_x = ox;
                u.harv_cell_y = oy;
                u.has_harv_cell = true;
                u.slave = SlaveState::MoveToOre;
                moves.push_back({idx, ox, oy});
                break;
            }
            case SlaveState::MoveToOre: {
                const bool at = u.cell_x == u.harv_cell_x && u.cell_y == u.harv_cell_y;
                if (at && !u.moving()) {
                    u.slave = SlaveState::Harvesting;
                    u.harv_timer = 0.0;
                    break;
                }
                if (!ore.has_ore(u.harv_cell_x, u.harv_cell_y)) {
                    u.slave = SlaveState::SeekOre;
                    u.harv_timer = 0.0;
                    break;
                }
                if (!at && !u.moving()) {
                    u.harv_skip_x = u.harv_cell_x;
                    u.harv_skip_y = u.harv_cell_y;
                    if (ore.has_ore(u.cell_x, u.cell_y)) {
                        u.harv_cell_x = u.cell_x;
                        u.harv_cell_y = u.cell_y;
                        u.slave = SlaveState::Harvesting;
                    } else {
                        u.slave = SlaveState::SeekOre;
                    }
                    u.harv_timer = 0.0;
                }
                break;
            }
            case SlaveState::Harvesting: {
                u.harv_timer += dt;
                while (u.harv_timer >= kGulpSecs && u.slave == SlaveState::Harvesting) {
                    u.harv_timer -= kGulpSecs;
                    int tx = u.cell_x, ty = u.cell_y;
                    OreKind k = OreKind::None;
                    if (ore.has_ore(tx, ty)) {
                        k = ore.take_bale(tx, ty);
                    } else {
                        for (int d = 0; d < 8 && k == OreKind::None; ++d) {
                            const int nx = tx + kRingDx[d], ny = ty + kRingDy[d];
                            if (ore.has_ore(nx, ny)) k = ore.take_bale(nx, ny);
                        }
                    }
                    if (k == OreKind::None) {
                        u.slave = SlaveState::SeekOre;
                        u.harv_timer = 0.0;
                        break;
                    }
                    if (k == OreKind::Ore) u.bales_ore += 1.0;
                    else u.bales_gems += 1.0;
                    if (u.bales_ore + u.bales_gems >= (double)u.storage_bales) {
                        u.slave = SlaveState::ReturnToMaster;
                        u.harv_timer = 0.0;
                        break;
                    }
                }
                break;
            }
            case SlaveState::ReturnToMaster: {
                bool beside = false;
                if (!u.slave_master_is_unit && m >= 0 && m < (int)buildings.size()) {
                    const Building& master = buildings[(size_t)m];
                    beside = u.cell_x >= master.cx - 1 && u.cell_x <= master.cx + master.fw &&
                             u.cell_y >= master.cy - 1 && u.cell_y <= master.cy + master.fh &&
                             !u.moving();
                } else if (u.slave_master_is_unit && m >= 0 && m < (int)units.size()) {
                    const Unit& master = units[(size_t)m];
                    const int dx = std::abs(u.cell_x - master.cell_x);
                    const int dy = std::abs(u.cell_y - master.cell_y);
                    beside = dx <= 1 && dy <= 1 && !u.moving();
                }
                const bool at = u.cell_x == pad_x && u.cell_y == pad_y && !u.moving();
                if (at || beside) {
                    u.slave = SlaveState::Delivering;
                    u.harv_timer = 0.0;
                } else if (!u.moving() || u.path.empty() || u.path.back().x != pad_x ||
                           u.path.back().y != pad_y) {
                    moves.push_back({idx, pad_x, pad_y});
                }
                break;
            }
            case SlaveState::Delivering: {
                if (house_credits && n_houses > 0) {
                    const int house = (u.house >= 0 && u.house < n_houses) ? u.house : 0;
                    house_credits[house] += u.bales_ore * (double)values.ore;
                    house_credits[house] += u.bales_gems * (double)values.gems;
                }
                u.bales_ore = 0.0;
                u.bales_gems = 0.0;
                u.slave = SlaveState::WaitReload;
                u.harv_timer = (double)(reload > 0 ? reload : 1);
                break;
            }
            case SlaveState::WaitReload: {
                const int steps = dt > 0.0 ? std::max(1, (int)std::lround(dt * kSlaveLogicFps)) : 0;
                u.harv_timer -= (double)steps;
                if (u.harv_timer <= 0.0) {
                    u.harv_timer = kRescanSecs;
                    u.slave = SlaveState::SeekOre;
                }
                break;
            }
        }
    }
}

void transplant_slave_manager_to_building(std::vector<Unit>& units, Building& building,
                                          int unit_master, int building_idx) {
    if (unit_master < 0 || unit_master >= (int)units.size()) return;
    if (building_idx < 0) return;
    Unit& from = units[(size_t)unit_master];
    if (!from.is_slave_master && from.slaves_number <= 0) return;
    building.slave_spawn_frames = from.slave_spawn_frames;
    building.slave_needs_regen = from.slave_needs_regen;
    building.slave_guard_frames = from.slave_guard_frames;
    for (Unit& u : units) {
        if (!u.is_slave || !u.slave_master_is_unit || u.slave_master != unit_master) continue;
        u.slave_master = building_idx;
        u.slave_master_is_unit = false;
    }
    from.is_slave_master = false;
    from.slaves_number = 0;
    from.slave_spawn_frames = 0;
    from.slave_needs_regen = false;
    from.slave_guard_frames = 0;
}

void transplant_slave_manager_to_unit(std::vector<Unit>& units, Building& building,
                                      int building_idx, int unit_idx, int slaves_number,
                                      int regen, int reload, int storage) {
    if (unit_idx < 0 || unit_idx >= (int)units.size()) return;
    if (building_idx < 0 || slaves_number <= 0) return;
    Unit& to = units[(size_t)unit_idx];
    arm_slave_master(to, slaves_number, regen, reload, storage);
    to.slave_spawn_frames = building.slave_spawn_frames;
    to.slave_needs_regen = building.slave_needs_regen;
    to.slave_guard_frames = building.slave_guard_frames;
    for (Unit& u : units) {
        if (!u.is_slave || u.slave_master_is_unit || u.slave_master != building_idx) continue;
        u.slave_master = unit_idx;
        u.slave_master_is_unit = true;
    }
    building.slave_spawn_frames = 0;
    building.slave_needs_regen = false;
    building.slave_guard_frames = 0;
}

}  // namespace ra2engine
