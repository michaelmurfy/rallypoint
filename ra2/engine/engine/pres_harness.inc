// pres_harness.inc -- G-8x presentation acceptance harness (included by main.cpp
// right after class GameScene). Drives the REAL GameScene (the object run_menu's
// lobby constructs) headlessly for N simulated seconds at a fixed 15 Hz step,
// optionally with a scripted scenario, captures PNGs of the real render path at
// the requested times, and prints the presentation event log (EVA / sounds /
// music / effects) so audio triggers can be proven through the real update()
// path without an audio device.
//
//   ra2engine --scene-shot <stem> [--map xmp31s2.map] [--secs 2,30,60] [--view X,Y] [--reveal]
//             [--ai N] [--script none|fight|build] [--side allied|soviet]
//             [--color N] [--audio]
//
// Writes <stem>_<sec>.png per requested time. Never used by a gate.

struct PresHarness {
    static inline int cam_x_ = 0, cam_y_ = 0;
    static int run(int argc, char** argv, int i0) {
        std::string stem = argv[i0];
        std::string map = "xmp31s2.map", script = "none", side = "allied";
        std::vector<double> secs = {2.0};
        int ai = -1, color = -1, mouse_x = -1, mouse_y = -1;
        bool select_all = false;
        int view_x = -1, view_y = -1;
        bool reveal_all = false;
        std::string peek;
        std::vector<std::string> give;   // --give ID: instant house-0 building near the yard   // terrain | overlay | civ: centre + reveal on the first such map object
        bool audio = false;
        for (int i = i0 + 1; i < argc; ++i) {
            const std::string a = argv[i];
            auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
            if (a == "--map") map = next();
            else if (a == "--script") script = next();
            else if (a == "--side") side = next();
            else if (a == "--ai") ai = std::atoi(next().c_str());
            else if (a == "--color") color = std::atoi(next().c_str());
            else if (a == "--audio") audio = true;
            else if (a == "--select") select_all = true;
            else if (a == "--peek") peek = next();
            else if (a == "--view") {   // --view X,Y: centre on + reveal that cell
                const std::string v = next();
                view_x = std::atoi(v.c_str());
                view_y = std::atoi(v.substr(v.find(',') + 1).c_str());
            }
            else if (a == "--reveal") reveal_all = true;
            else if (a == "--give") give.push_back(next());
            else if (a == "--mouse") {
                const std::string v = next();
                mouse_x = std::atoi(v.c_str());
                mouse_y = std::atoi(v.substr(v.find(',') + 1).c_str());
            }
            else if (a == "--secs") {
                secs.clear();
                std::stringstream ss(next());
                std::string tok;
                while (std::getline(ss, tok, ',')) secs.push_back(std::atof(tok.c_str()));
            }
        }
        const std::string dir = env_install_dir();
        Ini rules_ini = load_ini_from_ra2md(dir, "rulesmd.ini");
        Rules rules = Rules::load(rules_ini);
        ra2engine::Platform plat = ra2engine::Platform::create("RA2 Engine (scene-shot)", 1280, 720);
        ra2engine::TextureCache tc(plat.renderer());
        SDL_Renderer* ren = static_cast<SDL_Renderer*>(plat.renderer());

        ra2engine::SkirmishConfig cfg = ra2engine::default_skirmish_config(rules);
        cfg.map_file = map;
        cfg.use_map_starts = true;
        if (ai >= 1) {
            while (static_cast<int>(cfg.slots.size()) < ai + 1) {
                ra2engine::SkirmishSlot s = cfg.slots.back();
                s.is_ai = true;
                s.color_ix = static_cast<int>(cfg.slots.size()) % 8;
                s.side = (cfg.slots.size() % 2) ? ra2engine::World::Side::Soviet
                                                : ra2engine::World::Side::Allied;
                cfg.slots.push_back(s);
            }
            cfg.slots.resize(static_cast<std::size_t>(ai) + 1);
        }
        if (!cfg.slots.empty()) {
            if (side == "soviet") cfg.slots[0].side = ra2engine::World::Side::Soviet;
            if (color >= 0) cfg.slots[0].color_ix = color;
        }
        g_pres_log_on = true;
        GameScene gs(!audio, cfg);
        std::cerr << "[scene-shot] map=" << map << " houses=" << cfg.slots.size()
                  << " script=" << script << "\n";

        for (const std::string& id : give) give_building(gs, id);
        const double dt = 1.0 / 15.0;
        double t = 0.0;
        std::sort(secs.begin(), secs.end());
        std::size_t next_shot = 0;
        bool scripted = false;
        while (next_shot < secs.size()) {
            if (!scripted && t >= 0.5) {
                scripted = true;
                apply_script(gs, script);
            }
            if (script == "build") build_step(gs);
            gs.update(dt);
            t += dt;
            // The headless window has no mouse focus (SDL reports 0,0 -> edge
            // scroll); hold the scripted camera.
            if (scripted) { gs.cam_.x = cam_x_; gs.cam_.y = cam_y_; }
            if (mouse_x >= 0) { gs.mx_ = mouse_x; gs.my_ = mouse_y; }
            if (!peek.empty() && g_map_dressing) {
                const auto& list = peek == "overlay" ? g_map_dressing->overlays() : g_map_dressing->objects();
                for (const auto& o : list)
                    if (peek == "overlay" || o.key.rfind(peek == "civ" ? "civ:" : "terr:", 0) == 0) {
                        gs.world_.reveal_source(0, o.x, o.y, 9);
                        gs.cam_.x = static_cast<int>(gs.playfield_w_ / 2.0 - (o.x - o.y) * 30.0);
                        gs.cam_.y = static_cast<int>(GameScene::kWinH / 2.0 - (o.x + o.y) * 15.0);
                        break;
                    }
            }
            if (reveal_all) gs.world_.reveal_all_house(0);
            if (view_x >= 0) {
                gs.world_.reveal_source(0, view_x, view_y, 10);
                gs.cam_.x = static_cast<int>(gs.playfield_w_ / 2.0 - (view_x - view_y) * 30.0);
                gs.cam_.y = static_cast<int>(GameScene::kWinH / 2.0 - (view_x + view_y) * 15.0);
            }
            if (select_all)
                for (std::size_t k = 0; k < gs.world_.units().size(); ++k)
                    gs.input_.selection().set(static_cast<int>(k), gs.world_.units()[k].house == 0 && gs.world_.units()[k].alive);
            if (t + 1e-9 >= secs[next_shot]) {
                // Look at the most interesting spot: for "fight" the centroid
                // of the engaged units, else the scripted camera.
                if (script == "fight" || script == "tanks") follow_action(gs);
                for (int k = 0; k < 2; ++k) {
                    plat.begin_frame();
                    gs.render(plat, tc);
                    plat.present();
                }
                char path[512];
                std::snprintf(path, sizeof path, "%s_%03d.png", stem.c_str(),
                              static_cast<int>(std::lround(secs[next_shot])));
                capture_png(ren, path, 1280, 720);
                std::cerr << "[scene-shot] wrote " << path << " t=" << t << "\n";
                ++next_shot;
            }
        }
        for (const std::string& l : g_pres_log) std::cout << l << "\n";
        return 0;
    }

    // "fight": every player unit attacks the nearest enemy building, every
    // enemy unit attacks the nearest player unit (exercises reports, impacts,
    // deaths, EVA unit-lost / base-under-attack). "build": queue a power plant
    // then a barracks and place each at the first legal cell near the yard.
    static void apply_script(GameScene& g, const std::string& script) {
        ra2engine::World& w = g.world_;
        if (script == "fight" || script == "tanks") {
            if (script == "tanks") spawn_tanks(g);
            const auto& us = w.units();
            const auto& bs = w.buildings();
            for (int i = 0; i < static_cast<int>(us.size()); ++i) {
                if (!us[i].alive || us[i].house != 0) continue;
                int best = -1;
                double bd = 1e18;
                for (int b = 0; b < static_cast<int>(bs.size()); ++b) {
                    if (!bs[b].alive || bs[b].house == 0 || !w.is_enemy(0, bs[b].house)) continue;
                    const double dx = bs[b].cx - us[i].fx, dy = bs[b].cy - us[i].fy;
                    if (dx * dx + dy * dy < bd) { bd = dx * dx + dy * dy; best = b; }
                }
                if (best >= 0) { w.order_unit_attack_building(i, best); continue; }
                // MCV start: no enemy building yet -> the nearest enemy unit.
                for (int j = 0; j < static_cast<int>(us.size()); ++j) {
                    if (!us[j].alive || us[j].cargo_of >= 0 || !w.is_enemy(0, us[j].house)) continue;
                    const double dx = us[j].fx - us[i].fx, dy = us[j].fy - us[i].fy;
                    if (dx * dx + dy * dy < bd) { bd = dx * dx + dy * dy; best = j; }
                }
                if (best >= 0) w.order_unit_attack(i, best);
            }
            for (int i = 0; i < static_cast<int>(us.size()); ++i) {
                if (!us[i].alive || us[i].house == 0) continue;
                int best = -1;
                double bd = 1e18;
                for (int j = 0; j < static_cast<int>(us.size()); ++j) {
                    if (!us[j].alive || us[j].house != 0) continue;
                    const double dx = us[j].fx - us[i].fx, dy = us[j].fy - us[i].fy;
                    if (dx * dx + dy * dy < bd) { bd = dx * dx + dy * dy; best = j; }
                }
                if (best >= 0) w.order_unit_attack(i, best);
            }
            // Camera on the midpoint of the two bases.
            centre_on_player(g, 0.5);
        } else {
            centre_on_player(g, 0.0);
        }
    }
    // Three of the first vehicle type of each side's roster beside every
    // house's first building (exercises voxel house colours + tank weapons).
    static void spawn_tanks(GameScene& g) {
        ra2engine::World& w = g.world_;
        // Anchor per house: its first building, else (MCV start, gF G-23: no
        // building at frame 0) its first live unit -- the MCV.
        struct Anchor { int house, x, y; };
        std::vector<Anchor> anchors;
        auto has = [&](int h) {
            for (const Anchor& a : anchors) if (a.house == h) return true;
            return false;
        };
        for (const ra2engine::Building& bb : w.buildings())
            if (bb.alive && !has(bb.house)) anchors.push_back({bb.house, bb.cx, bb.cy + bb.fh + 1});
        for (const ra2engine::Unit& u : w.units())
            if (u.alive && u.cargo_of < 0 && !has(u.house)) anchors.push_back({u.house, u.cell_x, u.cell_y + 2});
        for (const Anchor& an : anchors) {
            const UnitRoster& r = (w.house_side(an.house) == ra2engine::World::Side::Soviet) ? g.ur_ai_ : g.ur_;
            const ra2engine::UnitTypeDef* d = nullptr;
            for (const auto& u : r.defs) if (u.is_vehicle && u.combat.damage > 0) { d = &u; break; }
            if (!d) continue;
            for (int k = 0; k < 3; ++k) w.add_combat_unit(an.x - 2 - k, an.y, an.house, d->combat);
        }
    }
    static void follow_action(GameScene& g) {
        double sx = 0, sy = 0;
        int n = 0;
        // The player's engaged units first (passes 0/1), so the camera stays in
        // explored ground; then anyone engaged (2/3).
        for (int pass = 0; pass < 4 && !n; ++pass)
            for (const auto& u : g.world_.units())
                if (u.alive && u.cargo_of < 0 && (pass >= 2 || u.house == 0) &&
                    ((pass % 2) == 0 ? (u.is_vehicle && (u.target >= 0 || u.target_building >= 0 || u.fire_flash > 0))
                                     : (u.target >= 0 || u.target_building >= 0 || u.fire_flash > 0))) {
                    sx += u.fx; sy += u.fy; ++n;
                }
        std::cerr << "[scene-shot] action centroid n=" << n << " (" << (n ? sx / n : 0) << ","
                  << (n ? sy / n : 0) << ")\n";
        if (!n) return;
        sx /= n; sy /= n;
        g.cam_.x = cam_x_ = static_cast<int>(g.playfield_w_ / 2.0 - (sx - sy) * 30.0);
        g.cam_.y = cam_y_ = static_cast<int>(GameScene::kWinH / 2.0 - (sx + sy) * 15.0);
    }
    static void centre_on_player(GameScene& g, double toward_enemy) {
        const auto& bs = g.world_.buildings();
        double px = 0, py = 0, ex = 0, ey = 0;
        bool hp = false, he = false;
        for (const auto& b : bs) {
            if (!b.alive) continue;
            if (b.house == 0 && !hp) { px = b.cx; py = b.cy; hp = true; }
            if (b.house != 0 && !he && g.world_.is_enemy(0, b.house)) { ex = b.cx; ey = b.cy; he = true; }
        }
        // MCV start (gF G-23): no building yet -> the house's first live unit.
        for (const auto& u : g.world_.units()) {
            if (!u.alive || u.cargo_of >= 0) continue;
            if (u.house == 0 && !hp) { px = u.cell_x; py = u.cell_y; hp = true; }
            if (u.house != 0 && !he && g.world_.is_enemy(0, u.house)) { ex = u.cell_x; ey = u.cell_y; he = true; }
        }
        if (!hp) return;
        const double cx = he ? px + (ex - px) * toward_enemy : px;
        const double cy = he ? py + (ey - py) * toward_enemy : py;
        g.cam_.x = static_cast<int>(g.playfield_w_ / 2.0 - (cx - cy) * 30.0);
        g.cam_.y = static_cast<int>(GameScene::kWinH / 2.0 - (cx + cy) * 15.0);
        cam_x_ = g.cam_.x;
        cam_y_ = g.cam_.y;
        std::cerr << "[scene-shot] camera on cell (" << cx << "," << cy << ") cam=" << g.cam_.x
                  << "," << g.cam_.y << "\n";
    }
    static void give_building(GameScene& g, const std::string& id) {
        const int type = roster_index(g.ba_, id);
        if (type < 0) { std::cerr << "[scene-shot] --give: no roster type " << id << "\n"; return; }
        const ra2engine::BuildingTypeDef& d = g.ba_.roster[type];
        int yx = 0, yy = 0;
        for (const auto& b : g.world_.buildings()) if (b.alive && b.house == 0) { yx = b.cx; yy = b.cy; break; }
        for (int r = 2; r < 14; ++r)
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx) {
                    const ra2engine::PlacementCheck chk = ra2engine::placement_check(
                        yx + dx, yy + dy, d.fw, d.fh, 0, d.adjacent, g.world_.passable(), g.world_.grid_w(),
                        g.world_.grid_h(), g.world_.occupancy(), g.world_.buildings(), true);
                    if (!chk.ok) continue;
                    g.world_.add_building(type, yx + dx, yy + dy, d.fw, d.fh, 0, d.strength, d.armor_ix, 0.0, true,
                                          d.sight, d.grants_map_reveal);
                    return;
                }
    }
    static void build_step(GameScene& g) {
        static const char* kOrder[] = {"GAPOWR", "GAPILE", "GAREFN", "GAWEAP", "GAAIRC"};
        static int step = 0;
        if (step >= 5) return;
        const ra2engine::ProductionState& bq = g.lq().state(ra2engine::QueueCat::Structure);
        const int type = roster_index(g.ba_, kOrder[step]);
        if (type < 0) { ++step; return; }
        if (bq.slot.type < 0) {   // L2: PRODUCE through the World
            g.world_.econ_produce(g.local_house_, ra2engine::QueueCat::Structure, type);
            return;
        }
        if (bq.slot.type == type && bq.slot.ready) {
            const ra2engine::BuildingTypeDef& d = g.ba_.roster[type];
            int yx = 0, yy = 0;
            for (const auto& b : g.world_.buildings())
                if (b.alive && b.house == 0) { yx = b.cx; yy = b.cy; break; }
            for (int r = 2; r < 12; ++r)
                for (int dy = -r; dy <= r; ++dy)
                    for (int dx = -r; dx <= r; ++dx) {
                        const ra2engine::PlacementCheck chk = ra2engine::placement_check(
                            yx + dx, yy + dy, d.fw, d.fh, 0, d.adjacent, g.world_.passable(),
                            g.world_.grid_w(), g.world_.grid_h(), g.world_.occupancy(),
                            g.world_.buildings(), false, nullptr, g.world_.build_mask(d.wall));   // W8-03-01
                        if (!chk.ok) continue;
                        if (g.world_.econ_place(g.local_house_, ra2engine::QueueCat::Structure, yx + dx,
                                                yy + dy) != ra2engine::ProdResult::Placed)
                            continue;
                        ++step;
                        return;
                    }
        }
    }
};

int run_scene_shot(int argc, char** argv, int i0) { return PresHarness::run(argc, argv, i0); }
