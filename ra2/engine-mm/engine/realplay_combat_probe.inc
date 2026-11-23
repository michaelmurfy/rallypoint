// realplay_combat_probe.inc -- `--probe-combat`: gap owner C's REAL-PLAY
// acceptance harness for combat / units / movement. Like A's --probe-realplay
// it constructs the actual GameScene the menu's Start button creates
// (SkirmishSetupScene -> GameScene(cfg) -> launch_skirmish, with every
// interactive wiring: projectiles, abilities, veterancy) headless and drives
// GameScene::update. Units come from the scene's OWN resolved rosters (the
// same UnitTypeDefs the sidebar builds), so the numbers are the real ones.
// Not a hashed gate: prints [probe] PASS/FAIL lines, exits 1 on any failure.

namespace {
struct CombatProbe {
    int fails = 0;
    void check(bool ok, const std::string& what) {
        std::cout << (ok ? "[probe] PASS " : "[probe] FAIL ") << what << "\n";
        if (!ok) ++fails;
    }
    static ra2engine::SkirmishConfig menu_config(const Rules& rules) {
        ra2engine::SkirmishConfig cfg = ra2engine::default_skirmish_config(rules);
        cfg.map_file = "xmp31s2.map";
        cfg.use_map_starts = true;
        return cfg;
    }
    static ra2engine::World& world(GameScene& g) { return g.world_; }
    static const BuildingAssets& ba(GameScene& g) { return g.ba_; }
    static std::vector<const ra2engine::UnitTypeDef*> roster(GameScene& g) {
        std::vector<const ra2engine::UnitTypeDef*> v;
        for (const UnitRoster* r : {&g.ur_, &g.ur_ai_})
            for (const ra2engine::UnitTypeDef& d : r->defs) v.push_back(&d);
        return v;
    }
    // The scene's real resolved combat profile for `id` (sidebar rosters
    // first, then the same resolve_combat_unit the rosters use).
    static ra2engine::UnitCombat combat_of(GameScene& g, const std::string& id) {
        for (const UnitRoster* r : {&g.ur_, &g.ur_ai_})
            for (const ra2engine::UnitTypeDef& d : r->defs)
                if (d.id == id) return d.combat;
        ra2engine::UnitCombat c = resolve_combat_unit(g.rules_, id).combat;
        return c;
    }
    static bool free_cell(const ra2engine::World& w, int x, int y, int& ox, int& oy) {
        for (int r = 0; r <= 12; ++r)
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                    const int cx = x + dx, cy = y + dy;
                    if (w.walkable(cx, cy) && w.unit_at_cell(cx, cy) < 0 &&
                        w.building_at_cell(cx, cy) < 0) {
                        ox = cx; oy = cy;
                        return true;
                    }
                }
        return false;
    }
    static int spawn(GameScene& g, const std::string& id, int house, int x, int y) {
        ra2engine::World& w = g.world_;
        int cx = 0, cy = 0;
        if (!free_cell(w, x, y, cx, cy)) return -1;
        w.add_combat_unit(cx, cy, house, combat_of(g, id));
        return static_cast<int>(w.units().size()) - 1;
    }
    // A clear spot well away from both bases (map centre of the walkable grid).
    static void field(const ra2engine::World& w, int& x, int& y) {
        long sx = 0, sy = 0, n = 0;
        for (int yy = 0; yy < w.grid_h(); ++yy)
            for (int xx = 0; xx < w.grid_w(); ++xx)
                if (w.walkable(xx, yy)) { sx += xx; sy += yy; ++n; }
        x = n ? static_cast<int>(sx / n) : 0;
        y = n ? static_cast<int>(sy / n) : 0;
    }
    static void run(GameScene& g, int frames) {
        for (int f = 0; f < frames; ++f) g.update(1.0 / 15.0);
    }
    static int own_building(GameScene& g, int house) {
        const ra2engine::World& w = g.world_;
        for (int i = 0; i < (int)w.buildings().size(); ++i)
            if (w.buildings()[i].alive && w.buildings()[i].house == house) return i;
        return -1;
    }
};
}  // namespace

int run_probe_combat() {
    const std::string dir = env_install_dir();
    Ini rules_ini = load_ini_from_ra2md(dir, "rulesmd.ini");
    Rules rules = Rules::load(rules_ini);
    CombatProbe p;
    using P = CombatProbe;
    const double kStep = 1.0 / 15.0;

    // ---- G-41: the Prism Tank fires (Weapon1=Comet) and kills a Rhino ----
    {
        GameScene g(true, P::menu_config(rules));
        ra2engine::World& w = P::world(g);
        int fx = 0, fy = 0;
        P::field(w, fx, fy);
        const ra2engine::UnitCombat sref = P::combat_of(g, "SREF");
        p.check(sref.damage > 0 && sref.range > 0.0,
                "G-41 SREF resolves Comet: damage=" + std::to_string(sref.damage) +
                    " range=" + std::to_string(sref.range));
        const int a = P::spawn(g, "SREF", 0, fx, fy);
        const int b = P::spawn(g, "HTNK", 1, fx + 5, fy);
        p.check(a >= 0 && b >= 0, "G-41 spawned SREF + HTNK in the real skirmish world");
        if (a >= 0 && b >= 0) {
            const int hp0 = w.units()[b].hp;
            for (int f = 0; f < 15 * 20 && w.units()[b].hp > 0; ++f) g.update(kStep);
            p.check(w.units()[b].hp < hp0, "G-41 Prism Tank damaged the Rhino (" +
                                               std::to_string(hp0) + " -> " +
                                               std::to_string(w.units()[b].hp) + ")");
        }
    }

    // ---- G-44: Flak Track's AA secondary shoots an airborne Kirov ----
    {
        GameScene g(true, P::menu_config(rules));
        ra2engine::World& w = P::world(g);
        int fx = 0, fy = 0;
        P::field(w, fx, fy);
        const ra2engine::UnitCombat htk = P::combat_of(g, "HTK");
        p.check(htk.secondary.valid && htk.secondary.aa,
                "G-44 HTK secondary " + htk.secondary.id + " is AA");
        const int a = P::spawn(g, "HTK", 0, fx, fy);
        const int k = P::spawn(g, "ZEP", 1, fx + 3, fy);
        if (a >= 0 && k >= 0) {
            ra2engine::Unit& z = w.units()[k];
            z.altitude = z.cruise_altitude > 0.0 ? z.cruise_altitude : 500.0;
            z.flight_state = ra2engine::FlightState::Cruising;
            const int hp0 = z.hp;
            for (int f = 0; f < 15 * 20 && w.units()[k].hp > 0; ++f) g.update(kStep);
            p.check(w.units()[k].hp < hp0, "G-44 Flak Track hit the airborne Kirov (" +
                                               std::to_string(hp0) + " -> " +
                                               std::to_string(w.units()[k].hp) + ")");
            p.check(w.units()[a].w_active == 1, "G-44 the shot came from the AA secondary slot");
        } else {
            p.check(false, "G-44 spawn HTK/ZEP");
        }
    }

    // ---- G-40: base defenses fire; Powered=yes goes offline at low power ----
    {
        GameScene g(true, P::menu_config(rules));
        ra2engine::World& w = P::world(g);
        const int cy_b = P::own_building(g, 0);
        p.check(cy_b >= 0, "G-40 player base exists");
        auto place = [&](const char* id, int near_x, int near_y) {
            const int t = roster_index(P::ba(g), id);
            if (t < 0) return -1;
            const ra2engine::BuildingTypeDef& d = P::ba(g).roster[t];
            for (int r = 2; r <= 10; ++r)
                for (int dy = -r; dy <= r; ++dy)
                    for (int dx = -r; dx <= r; ++dx) {
                        if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                        if (g.placement_ok(t, near_x + dx, near_y + dy))
                            return w.add_building(t, near_x + dx, near_y + dy, d.fw, d.fh, 0,
                                                  d.strength, d.armor_ix, 0.0, true, d.sight,
                                                  d.grants_map_reveal);
                    }
            return -1;
        };
        if (cy_b >= 0) {
            const ra2engine::Building cb = w.buildings()[cy_b];
            const int pill = place("GAPILL", cb.cx, cb.cy);
            p.check(pill >= 0, "G-40 placed a Pillbox by the ConYard");
            const int t = pill >= 0 ? w.buildings()[pill].type : -1;
            p.check(t >= 0 && P::ba(g).roster[t].armed,
                    "G-40 GAPILL resolved armed with " +
                        (t >= 0 ? P::ba(g).roster[t].weapon[0].id : std::string("?")));
            if (pill >= 0) {
                const ra2engine::Building& b = w.buildings()[pill];
                const int e = P::spawn(g, "E2", 1, b.cx + 3, b.cy);
                const int hp0 = e >= 0 ? w.units()[e].hp : 0;
                for (int f = 0; f < 15 * 10 && e >= 0 && w.units()[e].hp > 0; ++f) g.update(kStep);
                p.check(e >= 0 && w.units()[e].hp < hp0,
                        "G-40 Pillbox shot the Conscript (" + std::to_string(hp0) + " -> " +
                            std::to_string(e >= 0 ? w.units()[e].hp : 0) + ")");
            }
            // Patriot (Powered=yes, -50) with no power plant beyond the start.
            const int sam = place("NASAM", cb.cx, cb.cy);
            if (sam >= 0) {
                const bool low = w.house_low_power(0);
                const ra2engine::Building& s = w.buildings()[sam];
                const int k = P::spawn(g, "ZEP", 1, s.cx + 3, s.cy + 1);
                if (k >= 0) {
                    w.units()[k].altitude = 500.0;
                    w.units()[k].flight_state = ra2engine::FlightState::Cruising;
                }
                const int hp0 = k >= 0 ? w.units()[k].hp : 0;
                P::run(g, 15 * 6);
                if (low) {
                    p.check(w.buildings()[sam].def_offline && k >= 0 && w.units()[k].hp == hp0,
                            "G-29/G-40 Patriot offline at low power (Kirov untouched)");
                } else {
                    p.check(k >= 0 && w.units()[k].hp < hp0,
                            "G-40 powered Patriot shot the Kirov (" + std::to_string(hp0) +
                                " -> " + std::to_string(k >= 0 ? w.units()[k].hp : 0) + ")");
                }
            } else {
                p.check(false, "G-40 place NASAM");
            }
        }
    }

    // ---- G-42: recovered movement speed; G-45: GI deploy; G-43 water ----
    {
        GameScene g(true, P::menu_config(rules));
        ra2engine::World& w = P::world(g);
        int fx = 0, fy = 0;
        P::field(w, fx, fy);
        const int gi = P::spawn(g, "E1", 0, fx, fy);
        if (gi >= 0) {
            const double cps = w.units()[gi].speed_cps;
            p.check(std::abs(cps - 0.9375) < 1e-9,
                    "G-42 GI speed_cps = trunc(4*2.56)*1.6*15/256 = 0.9375 (got " +
                        std::to_string(cps) + ")");
            const double x0 = w.units()[gi].fx, y0 = w.units()[gi].fy;
            int gx = 0, gy = 0;
            P::free_cell(w, w.units()[gi].cell_x + 8, w.units()[gi].cell_y, gx, gy);
            w.order_unit_move(gi, gx, gy);
            P::run(g, 15 * 4);
            const double moved = std::hypot(w.units()[gi].fx - x0, w.units()[gi].fy - y0);
            p.check(moved > 2.5 && moved < 4.2,
                    "G-42 GI covered " + std::to_string(moved) + " cells in 4 s (expect ~3.75)");
        }
        const ra2engine::UnitCombat e1 = P::combat_of(g, "E1");
        p.check(e1.deploy_fire && e1.secondary.id == "Para",
                "G-45 E1 resolves DeployFire with Para");
        const int gi2 = P::spawn(g, "E1", 0, fx - 6, fy + 4);
        if (gi2 >= 0) {
            P::run(g, 3);
            const bool dep = w.order_deploy(gi2);
            const ra2engine::Unit& u = w.units()[gi2];
            const int e = P::spawn(g, "E2", 1, u.cell_x + 4, u.cell_y);
            const int hp0 = e >= 0 ? w.units()[e].hp : 0;
            P::run(g, 15 * 4);
            p.check(dep && w.units()[gi2].deployed && !w.units()[gi2].frozen,
                    "G-45 deployed GI is a firing post (deployed, not frozen)");
            p.check(e >= 0 && w.units()[e].hp < hp0 && w.units()[gi2].w_active == 1,
                    "G-45 deployed GI fires Para (E2 " + std::to_string(hp0) + " -> " +
                        std::to_string(e >= 0 ? w.units()[e].hp : 0) + ")");
        }
        // Water / cliffs closed to ground units.
        const auto& water = w.grid_for(ra2engine::MovementDomain::Water);
        int wx = -1, wy = -1, water_cells = 0, water_walkable = 0;
        for (int y = 0; y < w.grid_h(); ++y)
            for (int x = 0; x < w.grid_w(); ++x) {
                const std::size_t i = static_cast<std::size_t>(y) * w.grid_w() + x;
                if (i < water.size() && water[i]) {
                    ++water_cells;
                    if (w.walkable(x, y)) ++water_walkable;
                    if (wx < 0) { wx = x; wy = y; }
                }
            }
        std::cout << "[probe] water cells on xmp31s2: " << water_cells << "\n";
        if (water_cells > 0) {
            p.check(water_walkable == 0, "G-43 no water cell is walkable for ground units (" +
                                             std::to_string(water_walkable) + " of " +
                                             std::to_string(water_cells) + ")");
            const int tank = P::spawn(g, "MTNK", 0, wx, wy);
            if (tank >= 0) {
                w.order_unit_move(tank, wx, wy);
                bool wet = false;
                for (const ra2engine::Cellxy& c : w.units()[tank].path) {
                    const std::size_t i = static_cast<std::size_t>(c.y) * w.grid_w() + c.x;
                    if (i < water.size() && water[i]) wet = true;
                }
                p.check(!wet, "G-43 a Grizzly ordered onto water gets no path across it");
            }
        }
    }

    // ---- every buildable unit's weapon resolves (the 03 per-unit matrix) ----
    {
        GameScene g(true, P::menu_config(rules));
        int n = 0, armed = 0, bad = 0;
        for (const ra2engine::UnitTypeDef* d : P::roster(g)) {
            ++n;
            const std::string w0 = ra2engine::type_weapon_id(rules, d->id, 0, false);
            const ra2engine::UnitCombat& c = d->combat;
            const bool has = c.damage != 0 || c.range > 0.0;
            if (has) ++armed;
            // A weapon id that names a real [section] must resolve ("none" = no weapon).
            const bool ok = !ra2engine::resolve_weapon_slot(rules, w0).valid || has;
            if (!ok) ++bad;
            std::cout << "[probe] unit " << d->id << " w0=" << (w0.empty() ? "-" : w0)
                      << " dmg=" << c.damage << " rof=" << c.rof << " range=" << c.range
                      << " w1=" << (c.secondary.valid ? c.secondary.id : "-")
                      << (c.secondary.aa ? "(AA)" : "") << (c.primary_aa ? " primaryAA" : "")
                      << " speed_cps=" << c.speed_cps << (c.deploy_fire ? " deployfire" : "")
                      << (c.crusher ? " crusher" : "") << (c.crushable ? " crushable" : "")
                      << (ok ? "" : "  <-- UNRESOLVED") << '\n';
        }
        p.check(bad == 0, "roster: every unit with a weapon key resolves it (" +
                              std::to_string(armed) + "/" + std::to_string(n) + " armed, " +
                              std::to_string(bad) + " unresolved)");
    }

    std::cout << "[probe] combat: " << (p.fails ? "FAIL" : "ALL PASS") << " fails=" << p.fails
              << "\n";
    return p.fails ? 1 : 0;
}
