// gF: GameScene members for MCV deploy feedback and the skirmish game-over
// flow. Textually included INSIDE class GameScene (main.cpp) so the scene's
// own members are reachable without widening its interface.
// [RE: docs/re/mcv-deploy.md §2 (EVA_CannotDeployHere), §5 (Flag_To_Win /
//  Flag_To_Lose: TXT_VICTORIOUS / TXT_LOST, EVA_YouAreVictorious /
//  EVA_YouHaveLost, BorrowedTime, then back to the shell)]
private:
    std::deque<VehicleAssets> gf_va_;        // MCV + start-force hulls (stable addresses)
    int    gf_outcome_ = 0;                  // 0 playing, 1 won, -1 lost
    int    gf_borrowed_ = 0;                 // BorrowedTime frames left once flagged
    double gf_frame_accum_ = 0.0;
    long   gf_frames_ = 0;                   // 15 Hz game frames elapsed
    std::vector<char> gf_defeated_seen_;     // per house: EVA_PlayerDefeated already said
    std::string gf_end_text_;
    GfConfirm gf_abort_;                     // G-14 Abort Mission confirm (0x121)

    // G-15 [RE: techtree.md; campaign.md §4]: a type is offered only when its
    // TechLevel <= the house's TechLevel -- the lobby slider in a skirmish,
    // [<Player house>] TechLevel= of the mission map in a campaign (3 in
    // ALL01/SOV01). TechLevel <= 0 stays never-buildable (filtered upstream).
    int gf_tech_cap() const {
        if (cfg_.campaign_map.empty()) return cfg_.tech_level;
        if (gf_campaign_tech_ < 0) {
            int tl = 10;
            try {
                const Ini m = camp::load_map_ini(dir_, cfg_.campaign_map);
                const std::string house = m.get("Basic", "Player", "");
                if (!house.empty()) tl = m.get_int(house, "TechLevel", 10);
            } catch (const std::exception&) {}
            gf_campaign_tech_ = tl;
        }
        return gf_campaign_tech_;
    }
    mutable int gf_campaign_tech_ = -1;
    std::vector<int> gf_tech_filter(std::vector<int> v, bool units) const {
        const int cap = gf_tech_cap();
        std::vector<int> out;
        for (int i : v) {
            const int tl = units ? ur_.defs[(std::size_t)i].tech_level : ba_.roster[(std::size_t)i].tech_level;
            if (tl <= cap) out.push_back(i);
        }
        return out;
    }

    void gf_ask_abort() {
        std::vector<std::string> b{gf_csf("GUI:Abort", "Abort")};
        if (campaign_) b.push_back(gf_csf("GUI:Restart", "Restart"));
        b.push_back(gf_csf("GUI:ResumeMission", "Resume Mission"));
        gf_abort_.ask(gf_csf("GUI:AskAbortMission", "What would you like to do?"), b);
    }
    void gf_abort_answer(int c) {
        if (c < 0) return;
        if (mp_leave_confirm_) { mp_leave_answer(c); return; }   // W9-05-06: the Leave OK / Cancel
        const int resume = campaign_ ? 2 : 1;
        if (c == 0) {   // Abort -> the shell (P10), or exit a bare --interactive run
            menu_.open_ = false;
            if (net_) {
                // W9-03-16: the player who leaves a network match gets the
                // multiplayer score page too (outcome: lost); the EXIT is
                // scheduled first (net_filter_next holds the page).
                if (gf_outcome_ == 0) gf_outcome_ = -1;
                next_scene_ = gf_make_skirmish_result_scene(no_audio_, cfg_, gf_outcome_, gf_frames_, gf_stats());
                return;
            }
            if (Scene* m = make_main_menu_scene(no_audio_)) next_scene_ = m;
            else abort_ = true;
        } else if (campaign_ && c == 1) {   // Restart the campaign mission
            menu_.open_ = false;
            next_scene_ = gf_make_restart_scene(no_audio_, cfg_);
        } else if (c == resume) {
            menu_.open_ = false;   // Resume Mission
        }
    }

    // EVA lines go through E's presentation queue (evamd.ini, per side).
    void gf_eva(const char* key) {
        if (no_audio_) return;
        pres_.sound().eva(key);
    }

    bool gf_has_vehicle_art(const std::string& image) const {
        for (const VehicleAssets* v : vlist_)
            if (v && v->ok && v->combat.image == image) return true;
        return false;
    }

    // Render assets for every MCV type (so a produced, campaign-placed or
    // undeployed MCV draws) plus whatever launch_skirmish spawned at start.
    void gf_load_assets(const std::vector<std::string>& spawned) {
        std::vector<std::string> ids = gf_tokens(rules_.general_string("BaseUnit", "AMCV,SMCV"));
        for (const std::string& id : spawned) ids.push_back(id);
        MixFile* conquer = nullptr;
        std::unique_ptr<MixFile> conquer_holder;
        for (const std::string& id : ids) {
            if (!rules_.is_vehicle(id) && !rules_.is_infantry(id)) continue;
            const std::string image = rules_.image(id);
            if (rules_.is_vehicle(id)) {
                if (gf_has_vehicle_art(image)) continue;
                VehicleAssets va = load_vehicle_assets(dir_, ra2_, ua_.pal, id);
                if (!va.ok) continue;
                gf_va_.push_back(std::move(va));
                vlist_.push_back(&gf_va_.back());
            } else {
                if (ua_.shps.count(image)) {
                    if (ua_.anims.count(image)) world_.set_anim(image, ua_.anims.at(image));
                    continue;
                }
                try {
                    if (!conquer) {
                        conquer_holder = std::make_unique<MixFile>(ra2_.open_nested("conquer.mix"));
                        conquer = conquer_holder.get();
                    }
                    ua_.shps.emplace(image, load_unit_shp(*conquer, image));
                    ua_.anims.emplace(image, anim_from_art(art_, image));
                    world_.set_anim(image, ua_.anims.at(image));
                } catch (const std::exception& ex) {
                    std::cerr << "[gF] start infantry " << id << " art unavailable (" << ex.what() << ")\n";
                }
            }
        }
    }

    // Per frame, after world_.step: deploy feedback + (skirmish) game over.
    void gf_update(double dt) {
        for (const ra2engine::World::DeployEvent& e : world_.take_deploy_events()) {
            if (e.house != local_house_) continue;
            if (e.kind == ra2engine::World::DeployEvent::Kind::CannotDeploy) {
                gf_eva("EVA_CannotDeployHere");
            } else if (e.kind == ra2engine::World::DeployEvent::Kind::Deployed && e.unit >= 0 &&
                       e.unit < static_cast<int>(world_.units().size())) {
                // [AMCV] DeploySound=PlaceBuilding (the building slam sound) --
                // W5-E: through the presentation (positional + [pres] log).
                pres_.on_mcv_deployed(world_, pres_type_id(world_.units()[static_cast<std::size_t>(e.unit)]), e.building);
            }
        }
        if (campaign_) return;   // campaigns end through their triggers
        gf_frame_accum_ += dt * 15.0;
        int ticks = 0;
        while (gf_frame_accum_ >= 1.0) { gf_frame_accum_ -= 1.0; ++ticks; }
        gf_frames_ += ticks;
        if (!world_.victory_enabled()) return;
        const int n = world_.house_count();
        if ((int)gf_defeated_seen_.size() != n) gf_defeated_seen_.assign((std::size_t)n, 0);
        for (int h = 0; h < n; ++h)   // a player eliminated (FUN_004FBED0)
            if ((h != local_house_ || net_) && world_.house_defeated(h) && !gf_defeated_seen_[(std::size_t)h]) {
                gf_defeated_seen_[(std::size_t)h] = 1;
                // W9-05-05/10 [RE: FUN_004FBED0 @0x4FC0A0 / @0x4FC138]: in a
                // network game the line names the player (UIName = the
                // nickname) in the house colour, for the local player's own
                // defeat too, and an observer keeps hearing the others'.
                if (gf_outcome_ == 0 && (net_ || !world_.house_defeated(local_house_))) {
                    if (h != local_house_) gf_eva("EVA_PlayerDefeated");   // own defeat: EVA_YouHaveLost (pres_game)
                    std::string t = camp::csf_text(dir_, "TXT_PLAYER_DEFEATED");
                    std::string who = "Computer";
                    if (net_) who = mp_name(h);
                    else if (h < (int)cfg_.slots.size())
                        who = cfg_.slots[(std::size_t)h].country.empty()
                                  ? gf_default_country(cfg_.slots[(std::size_t)h].side)
                                  : cfg_.slots[(std::size_t)h].country;
                    const std::size_t ps = t.find("%s");
                    if (ps != std::string::npos) t.replace(ps, 2, who);
                    if (net_) post_message(t, -1.0, house_text_color(h));
                    else show_banner(t);   // W2-UI-21 mixed-case CSF line
                    if (net_) std::cerr << "[net] defeated: " << t << " (frame " << sim_frame_ << ")\n";
                }
            }
        if (gf_outcome_ == 0) {
            // L2: the outcome is per house in the sim (World::house_outcome,
            // latched by step_house_outcomes); the result screen reads the
            // local house's.
            const int outcome = world_.house_outcome(local_house_);
            // W9-05-05 [RE: FUN_004FBED0]: a defeated player of a network match
            // whose game goes on stays in it as an observer; the score comes
            // at the whole game's end (World::game_over).
            if (outcome < 0 && net_ && !world_.game_over()) {
                mp_enter_observer();
                return;
            }
            if (outcome != 0) {
                gf_outcome_ = outcome;
                gf_borrowed_ = ra2engine::borrowed_time_frames(
                    rules_.general_double("SavourDelay", 0.1));
                // EVA_YouAreVictorious / EVA_YouHaveLost: spoken by E's
                // presentation pass (pres_game.inc), not repeated here.
                gf_end_text_ = camp::csf_text(dir_, outcome > 0 ? "TXT_VICTORIOUS" : "TXT_LOST");   // W2-UI-20 mixed case
            }
            return;
        }
        gf_borrowed_ -= ticks;
        if (gf_borrowed_ <= 0 && !next_scene_) {
            next_scene_ = gf_make_skirmish_result_scene(no_audio_, cfg_, gf_outcome_, gf_frames_,
                                                        gf_stats());
        }
    }

    GfSkirmishStats gf_stats() const {
        GfSkirmishStats s;
        s.local_house = local_house_;   // L2
        s.multiplayer = net_ != nullptr;   // W9-05-10 (the score title: w9fix-menus)
        if (net_) {   // W9-03-16: Continue -> the MP page; the game number
            s.lan = net_->session.transport == ra2mp::MpTransportKind::Lan;
            s.game_number = g_mp_games_played;
        }
        for (const ra2engine::Unit& u : world_.units()) {
            if (u.deployed_away) continue;
            if (u.vet.dont_score) continue;   // W4: DontScore=yes (spent V3/DMisl rockets)
            const bool dead = !u.alive || u.die_time >= 0.0;
            if (!dead) continue;
            if (u.house == local_house_) ++s.units_lost; else ++s.units_killed;
        }
        for (const ra2engine::Building& b : world_.buildings()) {
            if (b.alive && b.state != ra2engine::Building::State::Dead) continue;
            if (b.sold) continue;
            if (b.house == local_house_) ++s.buildings_lost; else ++s.buildings_killed;
        }
        // W2-UI-20: the per-house score rows.
        // Only the lobby's players (the neutral / civilian houses are not listed).
        const int n = cfg_.slots.empty() ? world_.house_count()
                                         : std::min(world_.house_count(), (int)cfg_.slots.size());
        s.rows.assign((std::size_t)std::max(0, n), GfSkirmishStats::Row{});
        for (int h = 0; h < n; ++h) {
            std::string who = h < (int)cfg_.slots.size()
                                  ? (cfg_.slots[(std::size_t)h].country.empty()
                                         ? gf_default_country(cfg_.slots[(std::size_t)h].side)
                                         : cfg_.slots[(std::size_t)h].country)
                                  : std::string("Computer");
            const std::string lbl = camp::csf_text(dir_, "Name:" + who);
            s.rows[(std::size_t)h].name = lbl.rfind("Name:", 0) == 0 ? who : lbl;
            if (net_) s.rows[(std::size_t)h].name = mp_name(h);   // W9-05-10: the player's nickname
            if (campaign_) {
                // W6-06-08: the map's houses are INI ids ("Player House",
                // "BadGuy1 House") the original never shows; name the row by
                // the house's side (GUI:Allies / GUI:Soviets).
                using Sd = ra2engine::World::Side;
                const Sd sd = world_.house_side(h);
                s.rows[(std::size_t)h].name = sd == Sd::Allied   ? gf_csf("GUI:Allies", "Allies")
                                              : sd == Sd::Soviet ? gf_csf("GUI:Soviets", "Soviets")
                                              : sd == Sd::Yuri   ? std::string("Yuri")
                                                                 : std::string("Neutral");
                s.rows[(std::size_t)h].rival = h != local_house_ && sd != Sd::Neutral && !world_.are_allied(h, local_house_);
            }
            // Only the lobby's playing slots are listed (not the neutral house).
            s.rows[(std::size_t)h].listed = cfg_.slots.empty() || cfg_.slots[(std::size_t)h].combatant;
        }
        auto killer_house = [&](int ui) {
            return (ui >= 0 && ui < (int)world_.units().size()) ? world_.units()[(std::size_t)ui].house : -1;
        };
        // W6-06-08: in a campaign the map's own objects were not built.
        const std::size_t built_u0 = campaign_ ? map_units_ : 0, built_b0 = campaign_ ? map_buildings_ : 0;
        for (std::size_t ui = 0; ui < world_.units().size(); ++ui) {
            const ra2engine::Unit& u = world_.units()[ui];
            if (u.deployed_away || u.house < 0 || u.house >= n) continue;
            if (ui >= built_u0) ++s.rows[(std::size_t)u.house].built;
            if (u.alive && u.die_time < 0.0) continue;
            ++s.rows[(std::size_t)u.house].losses;
            const int k = killer_house(u.vet.killed_by);
            if (k >= 0 && k < n && k != u.house) ++s.rows[(std::size_t)k].kills;
        }
        for (std::size_t bi = 0; bi < world_.buildings().size(); ++bi) {
            const ra2engine::Building& b = world_.buildings()[bi];
            if (b.house < 0 || b.house >= n) continue;
            if (bi >= built_b0) ++s.rows[(std::size_t)b.house].built;
            if ((b.alive && b.state != ra2engine::Building::State::Dead) || b.sold) continue;
            ++s.rows[(std::size_t)b.house].losses;
            const int k = killer_house(b.killed_by);
            if (k >= 0 && k < n && k != b.house) ++s.rows[(std::size_t)k].kills;
        }
        // W5-E score [RE: FUN_007021D0 kill credit, FUN_005C92C0 tally]:
        // victim cost x (1 + victim rank), not for allied victims / DontScore.
        for (const ra2engine::Unit& u : world_.units()) {
            if (u.deployed_away || u.vet.dont_score || (u.alive && u.die_time < 0.0)) continue;
            const int k = killer_house(u.vet.killed_by);
            if (k < 0 || k >= n || k == u.house || world_.are_allied(k, u.house)) continue;
            s.rows[(std::size_t)k].score += static_cast<long>(u.vet.cost) * (1 + std::max(0, std::min(2, u.veteran_rank)));
        }
        for (const ra2engine::Building& b : world_.buildings()) {
            if ((b.alive && b.state != ra2engine::Building::State::Dead) || b.sold) continue;
            const int k = killer_house(b.killed_by);
            if (k < 0 || k >= n || k == b.house || world_.are_allied(k, b.house)) continue;
            if (b.type >= 0 && b.type < (int)ba_.roster.size()) s.rows[(std::size_t)k].score += ba_.roster[(std::size_t)b.type].cost;
        }
        // The survivors' bonus: score + score/2 + Random(score/2, score)
        // [RE-FALLBACK: the clone draws from a game-length-seeded LCG, not the
        // sim's Randomizer, so the result page never perturbs the simulation].
        std::uint32_t rng = static_cast<std::uint32_t>(gf_frames_) * 2654435761u + 0x5C0E5u;
        for (int h = 0; h < n; ++h) {
            GfSkirmishStats::Row& r = s.rows[(std::size_t)h];
            r.house = h;
            r.winner = !world_.house_defeated(h);
            if (!r.winner || r.score <= 0) continue;
            rng = rng * 1103515245u + 12345u;
            const long lo = r.score / 2, span = r.score - lo + 1;
            r.score = r.score + lo + lo + static_cast<long>((rng >> 8) % static_cast<std::uint32_t>(std::max(1L, span)));
        }
        std::stable_sort(s.rows.begin(), s.rows.end(),
                         [](const GfSkirmishStats::Row& a, const GfSkirmishStats::Row& b) { return a.score > b.score; });
        return s;
    }

    void gf_render(SDL_Renderer* ren, ra2engine::TextureCache& tc) {
        if (gf_outcome_ == 0 || gf_end_text_.empty()) return;
        draw_end_banner(ren, tc, gf_end_text_);   // W2-UI-20: game font, mixed case
    }
public:
    // Put cell (x, y) at the centre of the playfield.
    void gf_center_cell(int x, int y) {
        const ra2assets::MapCell* mc = a_.map.at(x, y);
        const int lvl = mc ? mc->level : 0;
        const ra2engine::ScreenPt p = ra2engine::cell_to_screen(x, y, lvl, 60, 30, 15);
        cam_.x = playfield_w_ / 2 - (p.x + 30);
        cam_.y = kWinH / 2 - (p.y + 15);
    }
    // The original opens a skirmish looking at the player's start (the base
    // centre set by FUN_0050DE20 in FUN_006880F0).
    void gf_center_on_player() {
        for (const ra2engine::Unit& u : world_.units())
            if (u.alive && u.house == local_house_) { gf_center_cell(u.cell_x, u.cell_y); return; }
    }
    // --- gF real-play driver seams (run_gf_play; read-only or input-shaped) ---
    ra2engine::World& gf_world() { return world_; }
    Scene* gf_peek_next() const { return next_scene_; }
    int gf_outcome() const {
        if (campaign_) return campaign_->outcome() == 0 ? 0 : (campaign_->outcome() == 1 ? 1 : -1);
        return gf_outcome_;
    }
    double gf_credits() const { return lcredits(); }
    // Click the sidebar cameo of building `id` (structures tab) with a real
    // PointerDown/Up through handle(). false = not on the strip.
    bool gf_click_building_cameo(const std::string& id) {
        if (active_tab_ != 0) {
            active_tab_ = 0;
            buildable_ = tab_buildable(active_tab_);
            lay_ = ra2engine::SidebarLayout::compute(kWinW, kWinH, buildable_);
        }
        for (std::size_t i = 0; i < buildable_.size() && i < lay_.cameos.size(); ++i) {
            if (ba_.roster[(std::size_t)buildable_[i]].id != id) continue;
            const ra2engine::SbRect r = lay_.cameos[i].rect;
            gf_click(r.x + r.w / 2, r.y + r.h / 2, ra2engine::InputEvent::Left);
            return true;
        }
        return false;
    }
    bool gf_structure_ready() const {
        const ra2engine::ProductionState& q = lq().state(ra2engine::QueueCat::Structure);
        return q.slot.type >= 0 && q.slot.ready;
    }
    // Centre the camera on cell (x, y) and left-click it (placement confirm).
    void gf_click_cell(int x, int y, ra2engine::InputEvent::Button b) {
        gf_center_cell(x, y);
        gf_click(playfield_w_ / 2, kWinH / 2, b);
    }
    void gf_click(int x, int y, ra2engine::InputEvent::Button b) {
        ra2engine::InputEvent ev;
        ev.kind = ra2engine::InputEvent::PointerMove;
        ev.x = x;
        ev.y = y;
        handle(ev);
        ev.kind = ra2engine::InputEvent::PointerDown;
        ev.button = b;
        handle(ev);
        ev.kind = ra2engine::InputEvent::PointerUp;
        handle(ev);
    }
    void gf_open_menu() { menu_.open(); }
    bool gf_menu_open() const { return menu_.open_; }
    bool gf_abort_open() const { return gf_abort_.open() || (menu_.open_ && menu_.page() == InGameMenu::P_ABORT); }   // W2-UI-15 page 0xB6
    std::vector<std::string> gf_buildable_ids(int tab) const {
        std::vector<std::string> out;
        for (int i : tab_buildable(tab))
            out.push_back(tab >= 2 ? ur_.defs[(std::size_t)i].id : ba_.roster[(std::size_t)i].id);
        return out;
    }
    // Campaign trigger witness (the --demo-campaign log) for the driver.
    std::vector<std::string> gf_campaign_witness() const {
        return campaign_ ? campaign_->witness() : std::vector<std::string>{};
    }
