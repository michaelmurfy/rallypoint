// realplay_probe.inc -- `--probe-realplay`: gap owner A's REAL-PLAY acceptance
// harness. It constructs the actual GameScene (the exact object the menu's
// Start button creates: SkirmishSetupScene -> GameScene(cfg) -> launch_skirmish)
// headless, drives it through GameScene::update / GameScene::handle (real
// sidebar clicks at real cameo rects), and asserts the economy/production
// behaviour the player sees. Not a hashed gate: it prints CHECK lines and
// exits 1 on any failure. Included after run_demo_saveload (needs GameScene).

namespace {
struct RealPlayProbe {
    int fails = 0;
    void check(bool ok, const std::string& what) {
        std::cout << (ok ? "[probe] PASS " : "[probe] FAIL ") << what << "\n";
        if (!ok) ++fails;
    }
    static ra2engine::SkirmishConfig menu_config(const Rules& rules) {
        // What SkirmishSetupScene hands GameScene on Start (default lobby, a
        // real multiplayer map, real [Waypoints] starts).
        ra2engine::SkirmishConfig cfg = ra2engine::default_skirmish_config(rules);
        cfg.map_file = "xmp31s2.map";
        cfg.use_map_starts = true;
        return cfg;
    }
    static ra2engine::World& world(GameScene& g) { return g.world_; }
    static double credits(GameScene& g) { return g.lcredits(); }
    static const BuildingAssets& ba(GameScene& g) { return g.ba_; }
    static const UnitRoster& ur(GameScene& g) { return g.ur_; }
    static int own_index(GameScene& g, const char* id) {
        const ra2engine::World& w = g.world_;
        for (int i = 0; i < (int)w.buildings().size(); ++i) {
            const ra2engine::Building& b = w.buildings()[i];
            if (b.alive && b.house == 0 && b.state != ra2engine::Building::State::Dead &&
                g.ba_.roster[b.type].id == id)
                return i;
        }
        return -1;
    }
    // Find a legal cell for roster type `t` next to the player's base (the
    // same placement_check the confirm uses, adjacency enforced).
    static bool find_spot(GameScene& g, int t, int& ox, int& oy) {
        const ra2engine::World& w = g.world_;
        const ra2engine::BuildingTypeDef& d = g.ba_.roster[t];
        int cx = -1, cy = -1;
        for (const ra2engine::Building& b : w.buildings())
            if (b.alive && b.house == 0) { cx = b.cx; cy = b.cy; break; }
        if (cx < 0) return false;
        for (int r = 1; r <= 14; ++r)
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                    if (g.placement_ok(t, cx + dx, cy + dy)) {
                        ox = cx + dx; oy = cy + dy;
                        return true;
                    }
                }
        (void)d;
        return false;
    }
    // Instant own building (scripted base growth, used only to reach a state).
    static int add_instant(GameScene& g, const char* id) {
        const int t = roster_index(g.ba_, id);
        int x = 0, y = 0;
        if (t < 0 || !find_spot(g, t, x, y)) return -1;
        const ra2engine::BuildingTypeDef& d = g.ba_.roster[t];
        return g.world_.add_building(t, x, y, d.fw, d.fh, 0, d.strength, d.armor_ix, 0.0,
                                     /*instant=*/true, d.sight, d.grants_map_reveal);
    }
    // A real left click at the centre of rect r.
    static void click(GameScene& g, const ra2engine::SbRect& r) {
        ra2engine::InputEvent ev;
        ev.kind = ra2engine::InputEvent::PointerDown;
        ev.button = ra2engine::InputEvent::Left;
        ev.x = r.x + r.w / 2;
        ev.y = r.y + r.h / 2;
        g.handle(ev);
    }
    static void tab(GameScene& g, int t) { click(g, g.lay_.tabs[t]); }
    // Ids shown on the active tab, in cameo order.
    static std::vector<std::string> tab_ids(GameScene& g, int t) {
        tab(g, t);
        std::vector<std::string> v;
        for (int x : g.buildable_)
            v.push_back(t < 2 ? g.ba_.roster[x].id : g.ur_.defs[x].id);
        return v;
    }
    static bool has(const std::vector<std::string>& v, const char* id) {
        return std::find(v.begin(), v.end(), std::string(id)) != v.end();
    }
    static const ra2engine::QueueSlot& qslot(GameScene& g, ra2engine::QueueCat c) {
        return g.lq().state(c).slot;
    }
    static void run(GameScene& g, int frames) {
        for (int f = 0; f < frames; ++f) g.update(1.0 / 15.0);
    }
    // Click the cameo of `id` on tab `t` (real sidebar click). False when the
    // strip does not list it.
    static bool click_item(GameScene& g, int t, const std::string& id) {
        tab(g, t);
        for (std::size_t k = 0; k < g.buildable_.size() && k < g.lay_.cameos.size(); ++k) {
            const int x = g.buildable_[k];
            const std::string& xid = t < 2 ? g.ba_.roster[x].id : g.ur_.defs[x].id;
            if (xid == id) {
                click(g, g.lay_.cameos[k].rect);
                return true;
            }
        }
        return false;
    }
    // Train unit `id` through the sidebar and wait (bounded) for it to roll
    // out of its factory. Returns the new unit index or -1.
    static int train(GameScene& g, int t, const std::string& id, int max_frames = 15 * 120) {
        const std::size_t n0 = g.world_.units().size();
        if (!click_item(g, t, id)) return -1;
        for (int f = 0; f < max_frames; ++f) {
            g.update(1.0 / 15.0);
            for (std::size_t i = n0; i < g.world_.units().size(); ++i)
                if (g.world_.units()[i].house == 0) return static_cast<int>(i);
        }
        return -1;
    }
    // W2-AN-22: the skirmish starts from an MCV (gF G-23). Deploy the
    // player's MCV and run until its Construction Yard is up, so every
    // section below starts from the base the pre-G-23 probe assumed.
    static void deploy_start(GameScene& g) {
        ra2engine::World& w = g.world_;
        for (int i = 0; i < (int)w.units().size(); ++i)
            if (w.units()[(std::size_t)i].house == 0 && w.is_mcv(i)) { w.order_deploy_mcv(i); break; }
        for (int f = 0; f < 15 * 60; ++f) {
            for (const ra2engine::Building& b : w.buildings())
                if (b.alive && b.house == 0 && b.state == ra2engine::Building::State::Idle &&
                    g.ba_.roster[b.type].conyard)
                    return;
            g.update(1.0 / 15.0);
        }
    }
    static int count_cameos_not_laid_out(GameScene& g) {
        return static_cast<int>(g.buildable_.size()) - static_cast<int>(g.lay_.cameos.size());
    }
};

}  // namespace

int run_probe_realplay() {
    const std::string dir = env_install_dir();
    Ini rules_ini = load_ini_from_ra2md(dir, "rulesmd.ini");
    Rules rules = Rules::load(rules_ini);
    RealPlayProbe p;
    using P = RealPlayProbe;

    // ---- G-20 / G-21: wallet + armed economy (Allied human vs Soviet AI) ----
    {
        ra2engine::SkirmishConfig cfg = P::menu_config(rules);
        cfg.starting_cash = 7500.0;   // a non-default slider value
        GameScene g(/*no_audio=*/true, cfg);
        P::deploy_start(g);
        ra2engine::World& w = P::world(g);
        p.check(P::credits(g) == 7500.0,
                "G-21 human wallet == lobby Money (7500), got " + std::to_string(P::credits(g)));
        p.check(w.house_credits(0) == 0.0, "G-21 no second house-0 seed left in the World pool");
        p.check(w.ai_credits(1) == 7500.0 && w.house_credits(1) == 0.0,
                "G-21 AI wallet == lobby Money once (ai=" + std::to_string(w.ai_credits(1)) +
                    " pool=" + std::to_string(w.house_credits(1)) + ")");
        // A refinery for the human, built through Buildup like a placed one, so
        // the real FreeUnit grand-opening spawns the miner.
        P::add_instant(g, "GAPOWR");
        const int t = roster_index(P::ba(g), "GAREFN");
        int rx = 0, ry = 0;
        const bool spot = t >= 0 && P::find_spot(g, t, rx, ry);
        p.check(spot, "G-20 found a legal GAREFN spot by the ConYard");
        if (spot) g.place_building(t, rx, ry);
        const double c0 = P::credits(g);
        int ore0 = 0;
        for (int y = 0; y < w.ore().h(); ++y)
            for (int x = 0; x < w.ore().w(); ++x) ore0 += w.ore().density_at(x, y);
        bool moved = false, unloaded = false;
        int miner = -1;
        for (int f = 0; f < 15 * 150; ++f) {
            g.update(1.0 / 15.0);
            for (int i = 0; i < (int)w.units().size(); ++i)
                if (w.units()[i].is_harvester && w.units()[i].house == 0) miner = i;
            if (miner >= 0) {
                const ra2engine::Unit& u = w.units()[miner];
                if (u.harv == ra2engine::HarvState::Harvesting ||
                    u.harv == ra2engine::HarvState::ReturnToRef)
                    moved = true;
                if (u.harv == ra2engine::HarvState::Unloading) unloaded = true;
            }
        }
        int ore1 = 0;
        for (int y = 0; y < w.ore().h(); ++y)
            for (int x = 0; x < w.ore().w(); ++x) ore1 += w.ore().density_at(x, y);
        p.check(miner >= 0, "G-20 refinery grand opening spawned the human's Chrono Miner");
        p.check(moved, "G-20 human miner left the dock and harvested");
        p.check(unloaded, "G-20 human miner docked and unloaded");
        p.check(P::credits(g) > c0, "G-20 harvest income reached the sidebar wallet (" +
                                        std::to_string(c0) + " -> " +
                                        std::to_string(P::credits(g)) + ")");
        p.check(ore1 != ore0, "G-20 ore field changed (harvest/growth): " + std::to_string(ore0) +
                                  " -> " + std::to_string(ore1));
        int ai_miners = 0;
        for (const ra2engine::Unit& u : w.units())
            if (u.alive && u.is_harvester && u.house == 1 && u.harv != ra2engine::HarvState::None &&
                u.harv != ra2engine::HarvState::SeekOre)
                ++ai_miners;
        p.check(ai_miners > 0, "G-20 AI miners are working (" + std::to_string(ai_miners) + ")");
    }

    auto join = [](const std::vector<std::string>& v) {
        std::string s;
        for (const std::string& x : v) s += (s.empty() ? "" : ",") + x;
        return s;
    };

    // ---- G-22 / G-26 / row 15/18/19: Allied (Americans) roster + strip ----
    {
        ra2engine::SkirmishConfig cfg = P::menu_config(rules);
        GameScene g(/*no_audio=*/true, cfg);
        P::deploy_start(g);
        std::vector<std::string> t0 = P::tab_ids(g, 0);
        std::cout << "[probe] allied start tab0=" << join(t0) << "\n";
        p.check(P::has(t0, "GAPOWR") && !P::has(t0, "NAPOWR") && !P::has(t0, "GAREFN"),
                "G-26 lone ConYard: strip offers GAPOWR only (no NA*, no unmet GAREFN)");
        for (const char* id : {"GAPOWR", "GAREFN", "GAPILE", "GAWEAP", "AMRADR"})
            P::add_instant(g, id);
        P::run(g, 2);
        t0 = P::tab_ids(g, 0);
        const std::vector<std::string> t1 = P::tab_ids(g, 1);
        const std::vector<std::string> t2 = P::tab_ids(g, 2);
        const std::vector<std::string> t3 = P::tab_ids(g, 3);
        std::cout << "[probe] allied base tab0=" << join(t0) << "\n[probe] allied base tab1="
                  << join(t1) << "\n[probe] allied base tab2=" << join(t2)
                  << "\n[probe] allied base tab3=" << join(t3) << "\n";
        p.check(P::has(t1, "ATESLA") && P::has(t1, "GAPILL") && !P::has(t1, "NALASR") &&
                    !P::has(t1, "GTGCAN"),
                "G-26 defenses: Prism Tower listed (was prefix-dropped), Grand Cannon not for Americans");
        p.check(!P::has(t0, "GAAIRC"), "row 18 GAAIRC ForbiddenHouses=Americans honoured");
        p.check(P::has(t2, "E1") && P::has(t2, "ENGINEER") && P::has(t2, "ADOG") &&
                    P::has(t2, "JUMPJET") && !P::has(t2, "E2") && !P::has(t2, "SNIPE"),
                "G-22 Allied infantry: GI/Engineer/Dog/Rocketeer, no Conscript, no British Sniper");
        p.check(P::has(t3, "MTNK") && P::has(t3, "CMIN") && !P::has(t3, "HTNK") &&
                    !P::has(t3, "TNKD"),
                "G-22 Allied vehicles: Grizzly + Chrono Miner, no Rhino, no German Tank Destroyer");
        const int u = P::train(g, 3, "CMIN");
        p.check(u >= 0 && P::world(g).units()[u].is_harvester,
                "G-22 a sidebar-built Chrono Miner rolled out as a working miner");
        const int e = P::train(g, 2, "ENGINEER");
        std::string eng_image;
        for (const ra2engine::UnitTypeDef& d : P::ur(g).defs)
            if (d.id == "ENGINEER") eng_image = d.combat.image;
        p.check(e >= 0 && P::world(g).units()[e].image == eng_image,
                "G-22 a sidebar-built Engineer left the Barracks");
        P::add_instant(g, "GATECH");
        P::run(g, 2);
        p.check(P::has(P::tab_ids(g, 2), "TANY"), "G-22 Tanya offered once a Battle Lab stands");
        const int tanya = P::train(g, 2, "TANY", 15 * 200);
        p.check(tanya >= 0 && !P::has(P::tab_ids(g, 2), "TANY"),
                "row 17 BuildLimit=1: Tanya leaves the strip while one is alive");
    }

    // ---- row 19: Soviet human ----
    // W2-AN-22: the SK lobby now picks a COUNTRY (its side follows), and the
    // gF start force spawns the country's BaseUnit -- the old "side button,
    // stale Americans" set-up started a Soviet human on an AMCV. Model the
    // real lobby: Russia.
    {
        ra2engine::SkirmishConfig cfg = P::menu_config(rules);
        cfg.slots[0].side = ra2engine::World::Side::Soviet;
        cfg.slots[0].country = "Russians";
        cfg.slots[1].side = ra2engine::World::Side::Allied;
        cfg.slots[1].country = "Americans";
        GameScene g(/*no_audio=*/true, cfg);
        P::deploy_start(g);
        // W2-AN-22: the MCV start has no pre-built base -- grow the one the
        // pre-G-23 probe assumed (power, refinery, barracks, factory).
        for (const char* id : {"NAPOWR", "NAREFN", "NAHAND", "NAWEAP"}) P::add_instant(g, id);
        P::run(g, 2);
        const std::vector<std::string> t0 = P::tab_ids(g, 0);
        const std::vector<std::string> t2 = P::tab_ids(g, 2);
        const std::vector<std::string> t3 = P::tab_ids(g, 3);
        std::cout << "[probe] soviet tab0=" << join(t0) << "\n[probe] soviet tab2=" << join(t2)
                  << "\n[probe] soviet tab3=" << join(t3) << "\n";
        p.check(!P::has(t0, "GAPOWR") && P::has(t0, "NARADR"),
                "row 19 Soviet human gets NA* structures, no GA*");
        p.check(P::has(t2, "E2") && P::has(t2, "DOG") && !P::has(t2, "E1"),
                "row 19 Soviet infantry (Conscript, Attack Dog), no GI");
        p.check(P::has(t3, "HTNK") && P::has(t3, "HARV") && !P::has(t3, "MTNK"),
                "row 19 Soviet vehicles (Rhino, War Miner), no Grizzly");
        const int u = P::train(g, 2, "E2");
        p.check(u >= 0, "row 19 a Conscript trained at the Soviet human's NAHAND");
        const int h = P::train(g, 3, "HTNK");
        p.check(h >= 0, "row 19 a Rhino rolled out of the Soviet human's NAWEAP");
    }

    // ---- row 15 / row 18: lobby TechLevel + country units ----
    {
        ra2engine::SkirmishConfig cfg = P::menu_config(rules);
        cfg.tech_level = 3;
        cfg.slots[0].country = "Germans";
        GameScene g(/*no_audio=*/true, cfg);
        P::deploy_start(g);
        bool sref = false, tnkd = false, fv = false;
        for (const ra2engine::UnitTypeDef& d : P::ur(g).defs) {
            sref |= d.id == "SREF";
            tnkd |= d.id == "TNKD";
            fv |= d.id == "FV";
        }
        p.check(!sref && fv, "row 15 lobby TechLevel 3 drops the Prism Tank (TL8), keeps the IFV (TL3)");
        p.check(tnkd, "row 18 Germans get the Tank Destroyer (RequiredHouses=Germans)");
    }

    // ---- G-24 Naval Yard on water + ship production; G-30 walls ----
    {
        ra2engine::SkirmishConfig cfg = P::menu_config(rules);
        GameScene g(/*no_audio=*/true, cfg);
        P::deploy_start(g);
        ra2engine::World& w = P::world(g);
        for (const char* id : {"GAPOWR", "GAREFN", "GAPILE", "GAWEAP"}) P::add_instant(g, id);
        const std::vector<std::uint8_t>& water = w.grid_for(ra2engine::MovementDomain::Water);
        int nwater = 0;
        for (std::uint8_t v : water) nwater += v != 0;
        std::cout << "[probe] water cells=" << nwater << "\n";
        const int yard = roster_index(P::ba(g), "GAYARD");
        // A land cell never takes the yard.
        int lx = 0, ly = 0;
        const int pw = roster_index(P::ba(g), "GAPOWR");
        const bool land_spot = P::find_spot(g, pw, lx, ly);
        p.check(land_spot && !g.placement_ok(yard, lx, ly), "G-24 GAYARD refused on a land cell");
        // Grow an outpost to the nearest water (scripted, instant) so the yard
        // can be adjacent, then place it through the real rule set.
        int yx = -1, yy = -1;
        const int cy_ix = P::own_index(g, "GACNST");
        p.check(cy_ix >= 0, "W2-AN-22 the deployed MCV gives the probe a GACNST");
        const ra2engine::Building cy0 = cy_ix >= 0 ? w.buildings()[cy_ix] : ra2engine::Building{};
        int best = 1 << 30;
        const ra2engine::BuildingTypeDef& yd = P::ba(g).roster[yard];
        for (int y = 0; y < w.grid_h(); ++y)
            for (int x = 0; x < w.grid_w(); ++x) {
                if (!ra2engine::water_bound_ok(x, y, yd.fw, yd.fh, water, w.grid_w(), w.grid_h()))
                    continue;
                const int d = std::abs(x - cy0.cx) + std::abs(y - cy0.cy);
                if (d < best) { best = d; yx = x; yy = y; }
            }
        if (yx >= 0) {
            // outpost power plant on the nearest land cell next to the yard spot
            for (int r = 1; r <= 6 && !g.placement_ok(yard, yx, yy); ++r)
                for (int dy = -r; dy <= r; ++dy)
                    for (int dx = -r; dx <= r; ++dx) {
                        const ra2engine::BuildingTypeDef& d = P::ba(g).roster[pw];
                        const ra2engine::PlacementCheck c = ra2engine::placement_check(
                            yx + dx, yy + dy, d.fw, d.fh, 0, 0, w.passable(), w.grid_w(),
                            w.grid_h(), w.occupancy(), w.buildings(), true);
                        bool on_water = false;
                        for (const ra2engine::Cellxy& fc : ra2engine::foundation_cells(yx + dx, yy + dy, d.fw, d.fh))
                            on_water |= water[(size_t)fc.y * w.grid_w() + fc.x] != 0;
                        if (c.ok && !on_water && !g.placement_ok(yard, yx, yy))
                            w.add_building(pw, yx + dx, yy + dy, d.fw, d.fh, 0, d.strength, d.armor_ix,
                                           0.0, true, d.sight, false);
                    }
        }
        const bool yard_ok = yx >= 0 && g.place_building(yard, yx, yy);
        p.check(yard_ok, "G-24 GAYARD placed on water (" + std::to_string(yx) + "," + std::to_string(yy) + ")");
        if (yard_ok) {
            P::run(g, 15 * 10);   // buildup
            const int s = P::train(g, 3, "DEST", 15 * 200);
            p.check(s >= 0 && w.units()[s].is_naval &&
                        water[(size_t)w.units()[s].cell_y * w.grid_w() + w.units()[s].cell_x] != 0,
                    "G-24 a sidebar-built Destroyer launched from the yard onto water");
            // W2-AN-09: the move order reaches the ship over the WATER grid.
            if (s >= 0) {
                const int sx = w.units()[s].cell_x, sy = w.units()[s].cell_y;
                int gx = -1, gy = -1;
                for (int r = 6; r >= 3 && gx < 0; --r)
                    for (int dy = -r; dy <= r && gx < 0; ++dy)
                        for (int dx = -r; dx <= r && gx < 0; ++dx)
                            if (std::max(std::abs(dx), std::abs(dy)) == r &&
                                w.unit_can_enter_cell(w.units()[s], sx + dx, sy + dy)) {
                                gx = sx + dx; gy = sy + dy;
                            }
                g.ui_selection_mut().clear();
                g.ui_selection_mut().set(s);
                p.check(gx >= 0 && w.group_can_enter({s}, 0, gx, gy) && !w.walkable(gx, gy),
                        "W2-AN-09 a water cell is a Move target for the selected Destroyer");
                if (gx >= 0) w.group_move(0, {s}, gx, gy);

                P::run(g, 15 * 20);
                p.check(w.units()[s].cell_x != sx || w.units()[s].cell_y != sy,
                        "W2-AN-09 the Destroyer sailed (" + std::to_string(sx) + "," + std::to_string(sy) +
                            " -> " + std::to_string(w.units()[s].cell_x) + "," +
                            std::to_string(w.units()[s].cell_y) + ")");
            }
        }
        // Walls: build time x3 and free line fill.
        const int wall = roster_index(P::ba(g), "GAWALL");
        P::click_item(g, 1, "GAWALL");
        const ra2engine::QueueSlot& ws = P::qslot(g, ra2engine::QueueCat::Defense);
        p.check(ws.type == wall &&
                    ws.total_frames == ra2engine::wall_build_frames(P::ba(g).roster[wall].cost, 0.7),
                "G-30 GAWALL builds at WallBuildSpeedCoefficient x3 (" +
                    std::to_string(ws.total_frames) + " frames)");
        int wx = 0, wy = 0;
        bool placed = false;
        for (int tries = 0; tries < 400 && !placed; ++tries) {
            if (!P::find_spot(g, wall, wx, wy)) break;
            // need wx+4 also legal and the 3 between clear
            bool clear = true;
            for (int k = 1; k <= 4; ++k) clear &= g.placement_ok(wall, wx + k, wy);
            if (clear) placed = true;
            else { // block this cell for the scan with a scripted wall elsewhere: shift search
                w.add_building(wall, wx, wy, 1, 1, 0, 1, 0, 0.0, true, 0, false);
            }
        }
        int before = 0, after = 0;
        for (const ra2engine::Building& b : w.buildings()) before += b.alive && b.type == wall;
        if (placed) {
            g.place_building(wall, wx, wy);
            g.place_building(wall, wx + 4, wy);
        }
        for (const ra2engine::Building& b : w.buildings()) after += b.alive && b.type == wall;
        p.check(placed && after - before == 5,
                "G-30 two walls 4 cells apart close the gap for free (+" + std::to_string(after - before) + ")");
    }

    // ---- G-27: Ore Purifier pays PurifierBonus on top of every unload ----
    {
        double earned[2] = {0, 0};
        for (int with = 0; with < 2; ++with) {
            ra2engine::SkirmishConfig cfg = P::menu_config(rules);
            GameScene g(/*no_audio=*/true, cfg);
        P::deploy_start(g);
            P::add_instant(g, "GAPOWR");
            P::add_instant(g, "GAPOWR");
            if (with) P::add_instant(g, "GAOREP");
            const int t = roster_index(P::ba(g), "GAREFN");
            int rx = 0, ry = 0;
            if (P::find_spot(g, t, rx, ry)) g.place_building(t, rx, ry);
            const double c0 = P::credits(g);
            P::run(g, 15 * 150);
            earned[with] = P::credits(g) - c0;
        }
        std::cout << "[probe] income 150s without/with purifier = " << earned[0] << " / "
                  << earned[1] << "\n";
        p.check(earned[0] > 0 && earned[1] >= earned[0] * 1.2,
                "G-27 an Ore Purifier raises harvest income (+25% per bale)");
    }

    // ---- G-25 / G-29: SW from its building, power-gated, lost with it ----
    {
        ra2engine::SkirmishConfig cfg = P::menu_config(rules);
        cfg.slots[0].side = ra2engine::World::Side::Soviet;
        cfg.slots[0].country = "Russians";   // W2-AN-22: the lobby picks a country
        cfg.slots[1].side = ra2engine::World::Side::Allied;
        cfg.slots[1].country = "Americans";
        GameScene g(/*no_audio=*/true, cfg);
        P::deploy_start(g);
        ra2engine::World& w = P::world(g);
        P::run(g, 2);
        p.check(!w.sw_state(0) && !w.sw_state(1), "G-25 nobody holds a superweapon at game start");
        // Starve the base: drain-only buildings until low power, then the silo.
        const int radar = P::add_instant(g, "NARADR");
        p.check(radar >= 0, "G-29 probe set-up: own Radar Tower placed");
        const int silo = P::add_instant(g, "NAMISL");
        for (int k = 0; k < 8 && !w.house_low_power(0); ++k) P::add_instant(g, "NARADR");
        P::run(g, 2);
        const ra2engine::Superweapon* sw = w.sw_state(0);
        p.check(silo >= 0 && sw && sw->type == ra2engine::SuperweaponType::Nuke,
                "G-25 an operational Nuclear Silo grants the Nuke");
        p.check(w.house_low_power(0), "G-29 base driven to low power (output < drain)");
        const double f0 = w.sw_charge_fraction(0);
        P::run(g, 15 * 20);
        p.check(w.sw_charge_fraction(0) == f0, "G-29 IsPowered Nuke does not charge on low power");
        p.check(!house_owns_operational_radar(w, P::ba(g), 0), "G-29 radar offline on low power");
        for (int k = 0; k < 8 && w.house_low_power(0); ++k) P::add_instant(g, "NAPOWR");
        P::run(g, 15 * 20);
        p.check(!w.house_low_power(0) && w.sw_charge_fraction(0) > f0,
                "G-29 charge resumes once power is restored");
        p.check(house_owns_operational_radar(w, P::ba(g), 0), "G-29 radar back online with power");
        w.begin_sell(silo, 0);
        P::run(g, 15 * 10);
        p.check(!w.sw_state(0), "G-25 selling the silo revokes the Nuke");
    }

    std::cout << "[probe-realplay] " << (p.fails ? "FAILED" : "ok") << " fails=" << p.fails << "\n";
    return p.fails ? 1 : 0;
}
