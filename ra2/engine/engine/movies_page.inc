// movies_page.inc -- W6-06-13: Movies & Credits > Play Movies (dialog 0x101's
// "Play Movies" button) when the player has supplied movie files.
// Included by main.cpp after credits_scene.inc.
//
// [RE: mainmenu_true.md §3 -- 0x101 Play Movies lists the campaign movies.
// RE-FALLBACK: the list rule and its titles were not traced. The clone lists
// every movie a campaign mission names ([Basic] Intro / Brief / Win / Lose /
// Action / PostScore / PreMapSelect of each MapSel stage, Allied then
// Soviet, plus each battle's FinalMovie) that can be found (movie_player.inc
// search order), titled by the mission's UIName. A tap plays it; the movie
// returns to the list.]

// Cheap presence test (no Bink parse): a loose <name>.bik in a movie folder
// or an entry in a non-stub MOVIES* / MOVMD* archive of the data dir.
inline bool movie_present(const std::string& name_in) {
    if (name_in.empty()) return false;
    std::string name = movie_lower(name_in);
    if (name.size() > 4 && name.substr(name.size() - 4) == ".bik") name.resize(name.size() - 4);
    const std::string file = name + ".bik";
    namespace fs = std::filesystem;
    std::error_code ec;
    const std::string dir = env_install_dir();
    std::vector<std::string> dirs;
    if (const char* md = std::getenv("RA2_MOVIE_DIR")) dirs.push_back(std::string(md) + "/");
    dirs.insert(dirs.end(), {dir, dir + "Movies/", dir + "movies/"});
    const std::string pref = pref_dir_path();
    if (!pref.empty()) dirs.push_back(pref + "Movies/");
    for (const std::string& d : dirs) {
        if (!fs::is_directory(d, ec)) continue;
        for (const auto& e : fs::directory_iterator(d, ec))
            if (e.is_regular_file(ec) && movie_lower(e.path().filename().string()) == file && e.file_size(ec) >= 44)
                return true;
    }
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        const std::string fn = movie_lower(e.path().filename().string());
        if (fn.size() < 4 || fn.substr(fn.size() - 4) != ".mix") continue;
        if (fn.rfind("movies", 0) != 0 && fn.rfind("movmd", 0) != 0) continue;
        if (e.file_size(ec) < 64) continue;   // the install stubs
        try {
            ra2assets::MixFile m = ra2assets::MixFile::open_path(e.path().string());
            if (m.find(file)) return true;
        } catch (const std::exception&) {}
    }
    return false;
}

// True when the player supplied ANY movie (a .bik in a movie folder, or a
// real MOVIES*/MOVMD* archive): enables the Play Movies row. Cached per run.
bool movies_supplied() {
    static int cached = -1;
    if (cached >= 0) return cached != 0;
    cached = 0;
    namespace fs = std::filesystem;
    std::error_code ec;
    const std::string dir = env_install_dir();
    std::vector<std::string> dirs;
    if (const char* md = std::getenv("RA2_MOVIE_DIR")) dirs.push_back(std::string(md) + "/");
    dirs.insert(dirs.end(), {dir, dir + "Movies/", dir + "movies/"});
    const std::string pref = pref_dir_path();
    if (!pref.empty()) dirs.push_back(pref + "Movies/");
    for (const std::string& d : dirs) {
        if (!fs::is_directory(d, ec)) continue;
        for (const auto& e : fs::directory_iterator(d, ec)) {
            const std::string fn = movie_lower(e.path().filename().string());
            if (e.is_regular_file(ec) && fn.size() > 4 && fn.substr(fn.size() - 4) == ".bik") { cached = 1; return true; }
        }
    }
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        const std::string fn = movie_lower(e.path().filename().string());
        if (fn.size() >= 4 && fn.substr(fn.size() - 4) == ".mix" &&
            (fn.rfind("movies", 0) == 0 || fn.rfind("movmd", 0) == 0) && e.file_size(ec) >= 64) {
            cached = 1;
            return true;
        }
    }
    return false;
}

class MoviesPageScene : public Scene {
public:
    MoviesPageScene(const Rules& rules, bool no_audio, const MenuArt& art, int top = 0)
        : rules_(rules), no_audio_(no_audio), art_(art), top_(top) {
        build();
    }
    const char* name() const override { return "movies"; }
    void handle(const ra2engine::InputEvent& ev) override {
        if (ev.kind == ra2engine::InputEvent::PointerMove || ev.kind == ra2engine::InputEvent::PointerDown ||
            ev.kind == ra2engine::InputEvent::PointerUp) {
            mx_ = ev.x;
            my_ = ev.y;
        }
        if (ev.button == ra2engine::InputEvent::Left && ev.kind == ra2engine::InputEvent::PointerUp) down_ = false;
        if (ev.kind == ra2engine::InputEvent::Key && ev.key == ra2engine::kKeyEscape) { back_ = true; return; }
        if (ev.kind != ra2engine::InputEvent::PointerDown || ev.button != ra2engine::InputEvent::Left) return;
        down_ = true;
        if (ui_dev(shell_row(3)).contains(ev.x, ev.y) && top_ > 0) top_ = std::max(0, top_ - kRows);
        if (ui_dev(shell_row(4)).contains(ev.x, ev.y) && top_ + kRows < (int)list_.size()) top_ += kRows;
        if (ui_dev(shell_row(5)).contains(ev.x, ev.y)) back_ = true;
        for (int i = 0; i < kRows && top_ + i < (int)list_.size(); ++i)
            if (ui_dev(row_rect(i)).contains(ev.x, ev.y)) play_ = top_ + i;
    }
    void render(ra2engine::Platform& plat, ra2engine::TextureCache& tc) override {
        SDL_Renderer* ren = static_cast<SDL_Renderer*>(plat.renderer());
        const ShellXform xf = ui_xform();
        const ShellPainter p{ren, tc, art_, xf};
        const std::vector<ShellButton> b{
            ShellButton{shell_row(3), "Previous", top_ > 0, ACT_NONE, PAGE_MOVIES},
            ShellButton{shell_row(4), "Next", top_ + kRows < (int)list_.size(), ACT_NONE, PAGE_MOVIES},
            ShellButton{shell_row(5), gf_csf("GUI:Back", "Back"), true, ACT_NONE, PAGE_MOVIES}};
        int lx = 0, ly = 0;
        ui_to_logical(mx_, my_, lx, ly);
        int hov = -1;
        for (int i = 0; i < 3; ++i)
            if (b[(std::size_t)i].enabled && b[(std::size_t)i].rect.contains(lx, ly)) hov = i;
        draw_shell(ren, tc, art_, xf, b, hov, (hov >= 0 && down_) ? hov : -1, gf_csf("GUI:PlayMovies", "Play Movies"),
                   "", 0);
        static constexpr ra2engine::SbRect kWell{24, 40, 423, 380};
        p.fill(p.dev(kWell), 0, 0, 0, 170);
        p.frame(p.dev(kWell), true);
        if (list_.empty()) p.label("No movies found.", kWell.x + 8, kWell.y + 8, kWell.w - 16, 20, 0);
        for (int i = 0; i < kRows && top_ + i < (int)list_.size(); ++i) {
            const ra2engine::SbRect r = row_rect(i);
            if (r.contains(lx, ly)) p.fill(p.dev(r), 90, 0, 0, 160);
            p.label(p.fit(list_[(std::size_t)(top_ + i)].title, r.w - 8), r.x + 4, r.y, r.w - 8, r.h, 0);
        }
    }
    Scene* next() override {
        if (back_) {
            back_ = false;
            return new ShellScene(rules_, no_audio_, art_, PAGE_MOVIES);
        }
        if (play_ >= 0) {
            const int k = play_;
            play_ = -1;
            const Rules* r = &rules_;
            const MenuArt* a = &art_;
            const bool na = no_audio_;
            const int top = top_;
            return new MovieScene({list_[(std::size_t)k].movie},
                                  [r, a, na, top]() -> Scene* { return new MoviesPageScene(*r, na, *a, top); });
        }
        return nullptr;
    }
    void play_probe(ra2play::Probe& p) override {
        p.scene = "movies";
        p.set("movie_count", static_cast<double>(list_.size()));
        for (int i = 0; i < kRows && top_ + i < (int)list_.size(); ++i) {
            const ra2engine::SbRect r = ui_dev(row_rect(i));
            p.targets["movie" + std::to_string(i)] = ra2play::Rect{r.x, r.y, r.w, r.h};
        }
        const ra2engine::SbRect bk = ui_dev(shell_row(5));
        p.targets["back"] = ra2play::Rect{bk.x, bk.y, bk.w, bk.h};
    }

private:
    struct Entry { std::string movie, title; };
    static constexpr int kRows = 18;
    static ra2engine::SbRect row_rect(int i) { return {28, 46 + 20 * i, 415, 20}; }
    void build() {
        try {
            const std::string dir = env_install_dir();
            ra2assets::MixFile ra2 = ra2assets::MixFile::open_path(dir + "ra2.mix");
            const ra2engine::CampaignTables t = camp::load_tables(ra2);
            std::set<std::string> seen;
            auto add = [&](const std::string& mv, const std::string& title) {
                if (mv.empty() || !seen.insert(movie_lower(mv)).second || !movie_present(mv)) return;
                list_.push_back({mv, title});
            };
            for (const char* side : {"GDI", "Nod"}) {
                for (const std::string& sn : t.side_stages(side)) {
                    const ra2engine::MapSelStage* st = t.stage(sn);
                    if (!st || st->scenario.empty()) continue;
                    ra2engine::ScenarioBasic b;
                    try {
                        b = ra2engine::parse_scenario_basic(camp::load_map_ini(dir, st->scenario));
                    } catch (const std::exception&) { continue; }
                    const ra2engine::MissionInfo mi = t.mission(st->scenario);
                    const std::string mname = mi.ui_name.empty() ? st->scenario : camp::csf_text(dir, mi.ui_name);
                    add(b.intro, mname + " - Intro");
                    add(b.brief, mname + " - Briefing");
                    add(b.action, mname + " - Action");
                    add(b.win, mname + " - Victory");
                    add(b.lose, mname + " - Defeat");
                    add(b.post_score, mname + " - After the mission");
                    add(b.pre_map_select, mname + " - Next mission");
                }
                if (const ra2engine::BattleEntry* be = t.battle(std::string(side) == "GDI" ? "ALL1" : "SOV1"))
                    add(be->final_movie, std::string(std::string(side) == "GDI" ? "Allied" : "Soviet") + " Ending");
            }
        } catch (const std::exception& ex) {
            std::cerr << "[movies] list unavailable: " << ex.what() << "\n";
        }
        if (top_ >= (int)list_.size()) top_ = 0;
    }
    const Rules& rules_;
    bool no_audio_;
    const MenuArt& art_;
    std::vector<Entry> list_;
    int top_ = 0, play_ = -1;
    bool back_ = false, down_ = false;
    int mx_ = -1, my_ = -1;
};
