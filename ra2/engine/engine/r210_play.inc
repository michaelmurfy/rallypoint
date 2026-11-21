// r210_play.inc -- R-210 (bridges and cliffs) --play support, #included INSIDE
// the GameScene class body right after w5_play.inc. Interactive-only: no gate
// reaches it, nothing here is World state (never saved, never hashed).
//   cliff_steps / cliff_steps(h)   unit steps between neighbouring cells whose
//                                  standing heights (Level + 4 on a deck) differ
//                                  by more than one level -- the cliff drops the
//                                  original's height gate (FUN_004D9B70) refuses
//   deck_steps(h)                  steps that landed on a high-bridge deck
//   udeck(h,ID,n) / uheight(h,ID,n)  the n-th live unit's deck layer and
//                                  standing height (Level + 4 * on_deck)
//   sel_n / sel_deck / sel_min_cy / sel_max_cy  the selection: units, on a
//                                  deck, lowest / highest cell row
    struct R210Track { int x = -1, y = -1, h = 0; };
    std::vector<R210Track> r210_prev_;
    int r210_cliff_ = 0;
    int r210_cliff_h_[16] = {};
    int r210_deck_h_[16] = {};

    int r210_height(const ra2engine::Unit& u) const {
        return world_.cell_level(u.cell_x, u.cell_y) + (u.on_deck ? 4 : 0);
    }
    // Called after every World::step.
    void r210_track_steps() {
        const auto& us = world_.units();
        if (r210_prev_.size() < us.size()) r210_prev_.resize(us.size());
        for (std::size_t i = 0; i < us.size(); ++i) {
            const ra2engine::Unit& u = us[i];
            R210Track& t = r210_prev_[i];
            if (!u.alive || u.die_time >= 0.0 || u.cargo_of >= 0 || u.is_aircraft) { t.x = -1; continue; }
            const int h = r210_height(u);
            if (t.x >= 0 && (t.x != u.cell_x || t.y != u.cell_y) && std::abs(t.x - u.cell_x) <= 1 &&
                std::abs(t.y - u.cell_y) <= 1) {
                const int hh = u.house >= 0 && u.house < 16 ? u.house : 15;
                if (std::abs(h - t.h) > 1) {
                    ++r210_cliff_;
                    ++r210_cliff_h_[hh];
                    std::cerr << "[r210] cliff step unit " << i << " " << play_unit_id(u) << " (" << t.x << ","
                              << t.y << " h" << t.h << ") -> (" << u.cell_x << "," << u.cell_y << " h" << h << ")\n";
                }
                if (u.on_deck) ++r210_deck_h_[hh];
                static const bool trace = std::getenv("RA2_R210_TRACE") != nullptr;   // diagnostics
                if (trace && usel(u))
                    std::cerr << "[r210] step " << play_unit_id(u) << " (" << u.cell_x << "," << u.cell_y
                              << ") h" << h << (u.on_deck ? " deck" : "") << '\n';
            }
            t.x = u.cell_x;
            t.y = u.cell_y;
            t.h = h;
        }
    }
    void r210_play_probe(ra2play::Probe& p) const {
        for (const char* f : {"cliff_steps", "deck_steps", "udeck", "uheight"}) p.zero_families.insert(f);
        p.set("cliff_steps", r210_cliff_);
        for (int h = 0; h < 16; ++h) {
            if (r210_cliff_h_[h]) p.set("cliff_steps(" + std::to_string(h) + ")", r210_cliff_h_[h]);
            if (r210_deck_h_[h]) p.set("deck_steps(" + std::to_string(h) + ")", r210_deck_h_[h]);
        }
        std::map<std::string, int> nth;
        int sel_n = 0, sel_deck = 0, sel_min_cy = 1 << 20, sel_max_cy = -1;
        for (const ra2engine::Unit& u : world_.units()) {
            if (!u.alive || u.die_time >= 0.0 || u.cargo_of >= 0) continue;
            if (usel(u)) {   // sel_n / sel_deck / sel_min_cy / sel_max_cy: the selection
                ++sel_n;
                sel_deck += u.on_deck ? 1 : 0;
                sel_min_cy = std::min(sel_min_cy, u.cell_y);
                sel_max_cy = std::max(sel_max_cy, u.cell_y);
            }
            const std::string base = std::to_string(u.house) + "," + play_unit_id(u);
            const std::string k = base + "," + std::to_string(nth[base]++);
            p.set("udeck(" + k + ")", u.on_deck);
            p.set("uheight(" + k + ")", r210_height(u));
        }
        p.set("sel_n", sel_n);
        p.set("sel_deck", sel_deck);
        p.set("sel_min_cy", sel_n ? sel_min_cy : -1);
        p.set("sel_max_cy", sel_max_cy);
    }
