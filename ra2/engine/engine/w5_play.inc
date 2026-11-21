// w5_play.inc -- wave-5 agent B (combat and movement core) --play support,
// #included INSIDE the GameScene class body right after w2air_play.inc.
// Probe keys for scripts/play/w5-*.play. Interactive-only: no gate reaches it.
//   subcells                 1 when infantry sub-cells are armed
//   max_cell_inf(h)          most house-h infantry standing in one cell
//   cells3(h)                cells holding three house-h infantry
//   uhpn(h,ID,n) ucx/ucy/uspot/ulx/uly(h,ID,n)  n-th live unit of the type:
//                            HP, cell, sub-cell spot, lepton position (+sub)
//   bhp(h,ID) bmaxhp(h,ID)   first live building of the type
//   ntgt(h)                  fv7c: live house-h units holding a unit target
//   bheal(h,ID)              fv7c: house-h buildings of the type being repaired
//   uspeed(h,ID,n) / uland(h,ID,n)  current cells/s and effective LandType
//                            of the unit's cell (W5-B R-062)
// Points: ore_near (the ore cell nearest the first selected unit),
//         clear_near (the nearest free Clear-land, ore-free cell 6+ cells
//         from the selected unit, same row band).
    void w5_play_probe(ra2play::Probe& p) const {
        for (const char* f : {"max_cell_inf", "cells3", "uhpn", "ucx", "ucy", "uspot", "ulx", "uly",
                              "bhp", "bmaxhp", "uspeed", "uland", "utgt", "ntgt", "bheal", "umov", "uwait", "ucargo", "upath", "nenemy", "ndog", "utgtb", "ucell", "bc4n"})
            p.zero_families.insert(f);
        p.set("subcells", world_.subcells_enabled() ? 1 : 0);
        std::map<std::pair<int, int>, int> per_cell[16];
        std::map<std::string, int> nth;
        const auto& us = world_.units();
        for (int i = 0; i < static_cast<int>(us.size()); ++i) {
            const ra2engine::Unit& u = us[(std::size_t)i];
            if (!u.alive || u.die_time >= 0.0 || u.cargo_of >= 0) continue;
            const std::string id = play_unit_id(u);
            const std::string base = std::to_string(u.house) + "," + id;
            const int n = nth[base]++;
            const std::string k = base + "," + std::to_string(n);
            p.set("uhpn(" + k + ")", u.hp);
            p.set("ucx(" + k + ")", u.cell_x);
            p.set("ucy(" + k + ")", u.cell_y);
            p.set("uspot(" + k + ")", u.spot);
            p.set("ulx(" + k + ")", u.fx * 256.0 + u.sub_x);
            p.set("uly(" + k + ")", u.fy * 256.0 + u.sub_y);
            p.set("uspeed(" + k + ")", world_.unit_speed_cps(i));
            p.set("uland(" + k + ")", world_.speed_land_at(u.cell_x, u.cell_y));
            p.set("utgt(" + k + ")", u.target);
            if (u.target >= 0) p.add("ntgt(" + std::to_string(u.house) + ")", 1);   // fv7c
            p.set("utgtb(" + k + ")", u.target_building);   // P3
            p.set("ucell(" + k + ")", u.cell_x * 1000 + u.cell_y);   // P3
            p.set("umov(" + k + ")", u.moving() ? 1 : 0);
            p.set("uwait(" + k + ")", u.wait_time);
            p.set("ucargo(" + k + ")", static_cast<double>(u.cargo.size()));
            p.set("upath(" + k + ")", u.path.empty() ? -1.0 : static_cast<double>(u.path.back().x * 1000 + u.path.back().y));
            if (world_.controllable(u.house)) {   // P3: nearest hostile ground unit (cells, cap 99)
                double best = 99.0 * 99.0, bdog = best;
                for (const ra2engine::Unit& e : us) {
                    if (!e.alive || e.die_time >= 0.0 || e.cargo_of >= 0 || e.is_aircraft || !world_.is_enemy(u.house, e.house)) continue;
                    const double dx = e.fx - u.fx, dy = e.fy - u.fy;
                    best = std::min(best, dx * dx + dy * dy);
                    if (e.image == "DOG") bdog = std::min(bdog, dx * dx + dy * dy);
                }
                p.set("nenemy(" + k + ")", std::sqrt(best));
                p.set("ndog(" + k + ")", std::sqrt(bdog));
            }
            if (!u.is_vehicle && !u.is_aircraft && !u.is_naval && u.house >= 0 && u.house < 16)
                ++per_cell[u.house][{u.cell_x, u.cell_y}];
        }
        for (int h = 0; h < world_.house_count() && h < 16; ++h) {
            int mx = 0, c3 = 0;
            for (const auto& kv : per_cell[h]) {
                mx = std::max(mx, kv.second);
                c3 += kv.second >= 3;
            }
            p.set("max_cell_inf(" + std::to_string(h) + ")", mx);
            p.set("cells3(" + std::to_string(h) + ")", c3);
        }
        const auto& bs = world_.buildings();
        for (const ra2engine::Building& b : bs) {
            if (!b.alive || b.state == ra2engine::Building::State::Dead) continue;
            if (b.type < 0 || b.type >= (int)world_.btypes().size()) continue;
            const std::string k = std::to_string(b.house) + "," + world_.btypes()[(std::size_t)b.type].id;
            if (b.c4_applied) p.add("bc4n(" + k + ")", 1);   // P3: C4 planted, ticking
            if (b.being_repaired) p.add("bheal(" + k + ")", 1);   // fv7c: AI/player repair running
            if (p.n.count("bhp(" + k + ")")) continue;
            p.set("bhp(" + k + ")", b.hp);
            p.set("bmaxhp(" + k + ")", b.max_hp);
        }
    }

    bool w5_find_point(const std::string& name, double& cx, double& cy) const {
        if (name != "ore_near" && name != "clear_near") return false;
        const int si = w2_first_selected();
        if (si < 0) return false;
        const ra2engine::Unit& s = world_.units()[(std::size_t)si];
        int best = -1;
        long bd = 0;
        for (int y = 0; y < world_.grid_h(); ++y)
            for (int x = 0; x < world_.grid_w(); ++x) {
                if (!world_.walkable(x, y) || !world_.occupancy().is_free(x, y)) continue;
                const bool ore = world_.ore().has_ore(x, y);
                const long d = (long)(x - s.cell_x) * (x - s.cell_x) + (long)(y - s.cell_y) * (y - s.cell_y);
                if (name == "ore_near" && !ore) continue;
                if (name == "clear_near" && (ore || world_.speed_land_at(x, y) != 0 || d < 36 || std::abs(y - s.cell_y) > 1))
                    continue;
                if (best < 0 || d < bd) { best = y * world_.grid_w() + x; bd = d; }
            }
        if (best < 0) return false;
        cx = best % world_.grid_w();
        cy = best / world_.grid_w();
        return true;
    }
