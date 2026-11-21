// gF: real-play verification driver (`ra2engine --gf-play <scenario> [outdir]`).
// Drives the REAL scene tree -- SkirmishSetupScene's Start button -> GameScene,
// or a campaign GameScene from campaign_config -- at a fixed 1/15 s tick with
// input-shaped orders (the D-key order_deploy path, sidebar cameo clicks, map
// clicks), prints `gf_play` probe lines, writes PNGs and exits 0 on PASS.
// Scenarios: skirmish | win | lose | campaign:<MAP>[:GDI|Nod].
// Included after run_menu (needs every scene class).

namespace gfplay {

struct Ctx {
    ra2engine::Platform* plat = nullptr;
    ra2engine::TextureCache* tc = nullptr;
    std::string out;
    bool ok = true;
    void shot(Scene& s, const std::string& name) {
        plat->begin_frame();
        s.render(*plat, *tc);
        try {
            capture_png(static_cast<SDL_Renderer*>(plat->renderer()), out + "/" + name, 1280, 720);
            std::cout << "gf_play shot " << out << "/" << name << "\n";
        } catch (const std::exception& ex) {
            std::cout << "gf_play shot-failed " << name << " " << ex.what() << "\n";
        }
        plat->present();
    }
    void check(bool c, const std::string& what) {
        std::cout << "gf_play " << (c ? "PASS " : "FAIL ") << what << "\n";
        if (!c) ok = false;
    }
};

int count_units(const ra2engine::World& w, int house, bool mcv_only) {
    int n = 0;
    for (int i = 0; i < (int)w.units().size(); ++i) {
        const ra2engine::Unit& u = w.units()[(std::size_t)i];
        if (!u.alive || u.die_time >= 0.0 || u.house != house) continue;
        if (mcv_only && !w.is_mcv(i)) continue;
        ++n;
    }
    return n;
}
int count_buildings(const ra2engine::World& w, int house, const std::string& id = "") {
    int n = 0;
    for (const ra2engine::Building& b : w.buildings()) {
        if (!b.alive || b.state == ra2engine::Building::State::Dead || b.house != house) continue;
        if (!id.empty() && w.btypes()[(std::size_t)b.type].id != id) continue;
        ++n;
    }
    return n;
}
int conyard_idle(const ra2engine::World& w, int house) {
    for (std::size_t i = 0; i < w.buildings().size(); ++i) {
        const ra2engine::Building& b = w.buildings()[i];
        if (b.alive && b.house == house && b.state == ra2engine::Building::State::Idle &&
            w.btypes()[(std::size_t)b.type].conyard)
            return (int)i;
    }
    return -1;
}
void step(GameScene& g, int frames) {
    for (int f = 0; f < frames; ++f) g.update(1.0 / 15.0);
}

// Deploy house 0's first MCV through the gameplay order and wait for an Idle
// ConYard. Returns the ConYard index or -1.
int deploy_player_mcv(Ctx& c, GameScene& g, int max_frames) {
    ra2engine::World& w = g.gf_world();
    int mcv = -1;
    for (int i = 0; i < (int)w.units().size() && mcv < 0; ++i)
        if (w.units()[(std::size_t)i].house == 0 && w.is_mcv(i)) mcv = i;
    c.check(mcv >= 0, "player has an MCV");
    if (mcv < 0) return -1;
    // The original gesture: left-click the MCV to select it, left-click it
    // again (ACTION_SELF) to deploy -- real events through GameScene::handle.
    const int mx = w.units()[(std::size_t)mcv].cell_x, my = w.units()[(std::size_t)mcv].cell_y;
    g.gf_click_cell(mx, my, ra2engine::InputEvent::Left);
    g.update(1.0 / 15.0);
    c.check(g.ui_selection().has(mcv), "click selects the MCV");
    for (int k = 0; k < 10; ++k) g.update(1.0 / 15.0);   // beyond the double-click window
    g.gf_click_cell(mx, my, ra2engine::InputEvent::Left);
    g.update(1.0 / 15.0);
    const bool ordered = w.units()[(std::size_t)mcv].deploy_mission != 0 || !w.units()[(std::size_t)mcv].alive;
    c.check(ordered, "clicking the selected MCV orders the deploy");
    for (int f = 0; f < max_frames; ++f) {
        g.update(1.0 / 15.0);
        if (f % 60 == 0) {
            const ra2engine::Unit& u = w.units()[(std::size_t)mcv];
            std::cout << "gf_play mcv f=" << f << " alive=" << u.alive << " mission=" << u.deploy_mission
                      << " cell=(" << u.cell_x << "," << u.cell_y << ") facing=" << u.body_facing
                      << " moving=" << u.moving() << " frozen=" << u.frozen << " buildings0=" << count_buildings(w, 0) << "\n";
        }
        const int cy = conyard_idle(w, 0);
        if (cy >= 0) {
            std::cout << "gf_play deployed f=" << f << " conyard=" << w.btypes()[(std::size_t)w.buildings()[(std::size_t)cy].type].id
                      << " at (" << w.buildings()[(std::size_t)cy].cx << "," << w.buildings()[(std::size_t)cy].cy << ")\n";
            return cy;
        }
    }
    return -1;
}

// Build `id` from the sidebar (cameo click -> wait ready -> cameo click ->
// map click on a legal origin near the ConYard).
bool build_from_sidebar(Ctx& c, GameScene& g, const std::string& id, int conyard) {
    ra2engine::World& w = g.gf_world();
    if (!g.gf_click_building_cameo(id)) {
        c.check(false, id + " cameo on the sidebar");
        return false;
    }
    int f = 0;
    for (; f < 3000 && !g.gf_structure_ready(); ++f) g.update(1.0 / 15.0);
    c.check(g.gf_structure_ready(), id + " finished building (f=" + std::to_string(f) + ")");
    if (!g.gf_structure_ready()) return false;
    g.gf_click_building_cameo(id);   // arm placement
    int t = -1;
    for (std::size_t k = 0; k < w.btypes().size(); ++k)
        if (w.btypes()[k].id == id) t = (int)k;
    const ra2engine::BuildingTypeDef& d = w.btypes()[(std::size_t)t];
    const ra2engine::Building& cy = w.buildings()[(std::size_t)conyard];
    const int before = count_buildings(w, 0, id);
    for (int r = 0; r <= 8; ++r)
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                const int ox = cy.cx + cy.fw + 1 + dx, oy = cy.cy + dy;
                const ra2engine::PlacementCheck chk = ra2engine::placement_check(
                    ox, oy, d.fw, d.fh, 0, d.adjacent, w.passable(), w.grid_w(), w.grid_h(),
                    w.occupancy(), w.buildings(), false, nullptr, w.build_mask(d.wall));   // W8-03-01
                if (!chk.ok) continue;
                g.gf_click_cell(ox + d.fw / 2, oy + d.fh / 2, ra2engine::InputEvent::Left);
                g.update(1.0 / 15.0);
                if (count_buildings(w, 0, id) > before) {
                    std::cout << "gf_play placed " << id << " near (" << ox << "," << oy << ")\n";
                    return true;
                }
            }
    c.check(false, id + " placed on the map");
    return false;
}

void kill_house(ra2engine::World& w, int house) {
    for (int i = 0; i < (int)w.units().size(); ++i) {
        const ra2engine::Unit& u = w.units()[(std::size_t)i];
        if (u.alive && u.die_time < 0.0 && u.house == house) w.campaign_kill_unit(i);
    }
    for (int i = 0; i < (int)w.buildings().size(); ++i) {
        const ra2engine::Building& b = w.buildings()[(std::size_t)i];
        if (b.alive && b.state != ra2engine::Building::State::Dead && b.house == house)
            w.campaign_damage_building(i, 1000000);
    }
}

GameScene* start_from_lobby(Ctx& c, const Rules& rules, const MenuArt& art, int country_clicks = 0) {
    SkirmishSetupScene lobby(rules, /*no_audio=*/true, art);
    ra2engine::InputEvent ev;
    for (int k = 0; k < country_clicks; ++k) {   // P0's country cell (slot_cell(0, 1))
        ev.kind = ra2engine::InputEvent::PointerDown;
        ev.button = ra2engine::InputEvent::Left;
        ev.x = 640 + 115 + 75;
        ev.y = 190 + 19;
        lobby.handle(ev);
    }
    c.shot(lobby, "gf_lobby.png");
    ev.kind = ra2engine::InputEvent::PointerDown;
    ev.button = ra2engine::InputEvent::Left;
    ev.x = 60 + 110;   // start_rect() centre
    ev.y = 630 + 28;
    lobby.handle(ev);
    Scene* nx = lobby.next();
    GameScene* g = dynamic_cast<GameScene*>(nx);
    c.check(g != nullptr, "lobby Start -> GameScene");
    if (!g) delete nx;
    return g;
}

}  // namespace gfplay

int run_gf_play(const std::string& scenario, const std::string& out_dir) {
    using namespace gfplay;
    const std::string dir = env_install_dir();
    Ini rules_ini = load_ini_from_ra2md(dir, "rulesmd.ini");
    Rules rules = Rules::load(rules_ini);
    ra2engine::Platform plat = ra2engine::Platform::create("RA2 Engine (gf-play)", 1280, 720);
    ra2engine::TextureCache tc(plat.renderer());
    MenuArtStore store;
    load_menu_art(dir, /*no_audio=*/true, store);
    g_menu_rules = &rules;
    g_menu_art = &store.art;
    Ctx c;
    c.plat = &plat;
    c.tc = &tc;
    c.out = out_dir;
    std::filesystem::create_directories(out_dir);

    if (scenario == "skirmish" || scenario == "soviet" || scenario == "win" || scenario == "lose") {
        // "soviet": P0 cycles Americans -> ... -> Russians (8 clicks) in the lobby.
        std::unique_ptr<GameScene> g(start_from_lobby(c, rules, store.art, scenario == "soviet" ? 8 : 0));
        if (!g) return 1;
        ra2engine::World& w = g->gf_world();
        const int pu = count_units(w, 0, false), pm = count_units(w, 0, true);
        const int pb = count_buildings(w, 0);
        std::cout << "gf_play start units=" << pu << " mcv=" << pm << " buildings=" << pb << "\n";
        c.check(pm == 1 && pb == 0, "skirmish starts with an MCV and no base");
        c.check(pu > 1, "starting force present (UnitCount)");
        for (int h = 1; h < w.house_count(); ++h)
            if (w.house_combatant(h))
                std::cout << "gf_play house " << h << " units=" << count_units(w, h, false)
                          << " mcv=" << count_units(w, h, true) << "\n";
        g->gf_center_on_player();
        c.shot(*g, "gf_start.png");
        step(*g, 30);
        c.check(!w.house_defeated(0), "MCV-only house is not defeated (ShortGame BaseUnit rule)");
        if (scenario == "soviet")
            c.check(w.house_side(0) == ra2engine::World::Side::Soviet, "lobby country Russians -> Soviet house");
        if (scenario == "skirmish" || scenario == "soviet") {
            const int cy = deploy_player_mcv(c, *g, 900);
            c.check(cy >= 0, "player MCV deployed into an Idle ConYard");
            if (cy >= 0) g->gf_center_cell(w.buildings()[(std::size_t)cy].cx + 1, w.buildings()[(std::size_t)cy].cy + 1);
            c.shot(*g, "gf_deployed.png");
            int ai_cy = -1;
            for (int h = 1; h < w.house_count() && ai_cy < 0; ++h)
                if (w.ai_enabled(h)) ai_cy = conyard_idle(w, h);
            c.check(ai_cy >= 0, "AI house deployed its MCV");
            if (cy >= 0) {   // (Soviet strip = A's G-03, merged)
                const bool soviet = w.house_side(0) == ra2engine::World::Side::Soviet;
                const bool built = build_from_sidebar(c, *g, soviet ? "NAPOWR" : "GAPOWR", cy);
                c.check(built, "ConYard builds a power plant from the sidebar");
                step(*g, 60);
                g->gf_center_cell(w.buildings()[(std::size_t)cy].cx + 2, w.buildings()[(std::size_t)cy].cy + 1);
                c.shot(*g, "gf_built.png");
            }
        } else {
            const bool win = scenario == "win";
            if (win) {
                for (int h = 1; h < w.house_count(); ++h)
                    if (w.house_combatant(h)) kill_house(w, h);
            } else {
                kill_house(w, 0);
            }
            int f = 0;
            for (; f < 600 && g->gf_outcome() == 0; ++f) g->update(1.0 / 15.0);
            c.check(g->gf_outcome() == (win ? 1 : -1),
                    std::string("outcome flagged ") + (win ? "WON" : "LOST") + " at f=" + std::to_string(f));
            c.shot(*g, win ? "gf_won_banner.png" : "gf_lost_banner.png");
            for (f = 0; f < 600 && !g->gf_peek_next(); ++f) g->update(1.0 / 15.0);
            c.check(g->gf_peek_next() != nullptr, "BorrowedTime elapsed -> result scene (f=" + std::to_string(f) + ")");
            std::unique_ptr<Scene> res(g->next());
            auto* rs = dynamic_cast<GfSkirmishResultScene*>(res.get());
            c.check(rs != nullptr, "result scene is the skirmish result page");
            if (rs) {
                c.shot(*rs, win ? "gf_result_won.png" : "gf_result_lost.png");
                rs->choose(2);
                std::unique_ptr<Scene> menu(rs->next());
                c.check(menu != nullptr && dynamic_cast<ShellScene*>(menu.get()) != nullptr,
                        "MAIN MENU returns to the shell");
                if (menu) c.shot(*menu, "gf_back_to_menu.png");
            }
        }
    } else if (scenario == "confirm") {
        // G-14: Exit Game asks (0x120) and Cancel keeps the shell; Abort Mission
        // asks (0x121) and Resume continues, Abort returns to the shell.
        auto click = [](Scene& sc, const ra2engine::SbRect& r) {
            ra2engine::InputEvent ev;
            ev.kind = ra2engine::InputEvent::PointerDown;
            ev.button = ra2engine::InputEvent::Left;
            ev.x = r.x + r.w / 2;
            ev.y = r.y + r.h / 2;
            sc.handle(ev);
            ev.kind = ra2engine::InputEvent::PointerUp;
            sc.handle(ev);
        };
        GfConfirm two;
        two.ask("", {"a", "b"});
        const ra2engine::SbRect exit_btn = ShellXform::make(1280, 720).hit(ra2engine::SbRect{484, 199 + 42 * 5, 156, 42});
        std::unique_ptr<Scene> menu(make_main_menu_scene(true));
        click(*menu, exit_btn);
        c.check(!menu->wants_quit(), "Exit Game does not quit at once");
        c.shot(*menu, "gf_exit_confirm.png");
        click(*menu, two.btn(1));
        c.check(!menu->wants_quit(), "Cancel keeps the shell");
        click(*menu, exit_btn);
        click(*menu, two.btn(0));
        c.check(menu->wants_quit(), "OK quits");
        std::unique_ptr<GameScene> g(start_from_lobby(c, rules, store.art));
        if (!g) return 1;
        // W2-UI-13/15: 0xB5 row 4 Abort Mission -> page 0xB6 (row 0 Abort, row 5 Resume).
        auto row = [](int k) { return ShellXform::make(1280, 720).hit(ra2engine::SbRect{484, 199 + 42 * k, 156, 42}); };
        const ra2engine::SbRect abort_btn = row(4);
        g->gf_open_menu();
        click(*g, abort_btn);
        c.check(g->gf_abort_open() && !g->gf_peek_next(), "Abort Mission asks first");
        c.shot(*g, "gf_abort_confirm.png");
        click(*g, row(5));   // skirmish: Abort / Resume Mission
        c.check(!g->gf_abort_open() && !g->gf_menu_open() && !g->gf_peek_next(), "Resume Mission returns to the game");
        g->gf_open_menu();
        click(*g, abort_btn);
        click(*g, row(0));
        c.check(g->gf_peek_next() != nullptr && dynamic_cast<ShellScene*>(g->gf_peek_next()) != nullptr,
                "Abort returns to the main menu");
    } else if (scenario.rfind("campaign:", 0) == 0) {
        std::string map = scenario.substr(9), side = "GDI";
        const std::size_t colon = map.find(':');
        if (colon != std::string::npos) { side = map.substr(colon + 1); map = map.substr(0, colon); }
        int campaign_frames = 450;   // campaign:MAP:SIDE[:FRAMES]
        const std::size_t c2 = side.find(':');
        if (c2 != std::string::npos) { campaign_frames = std::atoi(side.c_str() + c2 + 1); side = side.substr(0, c2); }
        std::unique_ptr<GameScene> g(new GameScene(true, campaign_config(map, side, 1)));
        ra2engine::World& w = g->gf_world();
        std::cout << "gf_play campaign " << map << " player units=" << count_units(w, 0, false)
                  << " mcv=" << count_units(w, 0, true) << " buildings=" << count_buildings(w, 0) << "\n";
        for (int tab = 0; tab < 4; ++tab) {
            std::cout << "gf_play campaign tab" << tab << ":";
            for (const std::string& id : g->gf_buildable_ids(tab)) std::cout << " " << id;
            std::cout << "\n";
        }
        c.shot(*g, "gf_campaign_start.png");
        if (count_units(w, 0, true) > 0) {
            const int cy = deploy_player_mcv(c, *g, 900);
            c.check(cy >= 0, "campaign MCV deployed into a ConYard");
            c.shot(*g, "gf_campaign_deployed.png");
        }
        // Play on; deploy any MCV the mission hands over (reinforcement).
        bool deployed_late = count_units(w, 0, true) == 0 && count_buildings(w, 0) > 0;
        for (int f = 0; f < campaign_frames; f += 150) {
            step(*g, 150);
            if (g->gf_outcome() != 0) break;
            if (!deployed_late && count_units(w, 0, true) > 0) {
                std::cout << "gf_play campaign MCV arrived by f=" << f + 150 << "\n";
                const int cy = deploy_player_mcv(c, *g, 900);
                c.check(cy >= 0, "reinforcement MCV deployed into a ConYard");
                deployed_late = true;
            }
        }
        std::cout << "gf_play campaign end player units=" << count_units(w, 0, false)
                  << " buildings=" << count_buildings(w, 0) << "\n";
        for (const std::string& l : g->gf_campaign_witness())
            if (l.find(" a108 ") != std::string::npos || l.find(" a101 ") != std::string::npos ||
                l.find(" a9 ") != std::string::npos || l.find("[camp] f=") != std::string::npos)
                std::cout << "gf_play trig" << l << "\n";
        std::cout << "gf_play campaign f=" << campaign_frames << " outcome=" << g->gf_outcome() << "\n";
        c.shot(*g, "gf_campaign_f450.png");
    } else {
        std::cout << "gf_play unknown scenario " << scenario << "\n";
        c.ok = false;
    }
    g_menu_rules = nullptr;
    g_menu_art = nullptr;
    std::cout << "gf_play RESULT " << (c.ok ? "PASS" : "FAIL") << " " << scenario << "\n";
    return c.ok ? 0 : 1;
}
